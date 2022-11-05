/*
 * test-video_source_common.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <time.h>
#include <unistd.h>

#include "video_source_common.h"

static inline int64_t get_time_ms(clockid_t clock_id)
{
	int64_t timestamp_ms = 0;
	struct timespec ts = { 0 };
	int rc = clock_gettime(clock_id, &ts);
	if(rc == -1) {
		perror("clock_gettime()");
		return -1;
	}
	timestamp_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	return timestamp_ms;
}

static int64_t s_begin_ticks_ms = 0;
static int on_new_frame(struct video_source_common *video, const struct video_frame *frame, void *user_data)
{
	printf("frame number: %ld\n", frame->frame_number);
	if(0 == s_begin_ticks_ms) {
		s_begin_ticks_ms = get_time_ms(CLOCK_MONOTONIC);
	}
	
	double ticks = (double)(frame->ticks_ms - s_begin_ticks_ms) / 1000.0;
	
	if(ticks > 0.0001) {
		printf("fps: %g\n", (double)(frame->frame_number + 1) / ticks);
	}
	
	return 0;
}

volatile int s_eos = 0;
volatile int quit = 0;

static int on_eos(struct video_source_common *video, void *user_data)
{
	printf("restart pipeline\n");
	s_eos = 1;
	
	return 0;
}

gboolean watchdog_process(struct video_source_common *video)
{
	if(quit) return G_SOURCE_REMOVE;
	if(s_eos) {
		s_eos = 0;
		assert(video->pipeline);
		video->init(video, NULL, -1, -1, NULL);
		
		video->play(video);
	
	}
	return G_SOURCE_CONTINUE;
}

#include <signal.h>

static GMainLoop *s_loop;
void on_signal(int sig)
{
	if(sig == SIGINT) {
		if(s_loop) {
			g_main_loop_quit(s_loop);
		}
	}
}

int main(int argc, char **argv)
{
	gst_debug_set_default_threshold(GST_LEVEL_WARNING);
	gst_init(&argc, &argv);
	
	int rc = 0;
	signal(SIGINT, on_signal);
	
	GstDeviceMonitor *monitor = gst_device_monitor_new();
	assert(monitor);
	gst_device_monitor_add_filter(monitor, "Video/Source", NULL);// gst_caps_new_empty_simple("video/x-raw"));
	GList *devices = gst_device_monitor_get_devices(monitor);
	
	// List information about available devices
	fprintf(stderr, "==== local device caps ====\n");
	for(GList *device = devices; NULL != device; device = device->next)
	{
		GstCaps *caps = gst_device_get_caps(devices->data);
		int num_infos = gst_caps_get_size(caps);
		for(int i = 0; i < num_infos; ++i) {
			GstStructure *info = gst_caps_get_structure(caps, i);
			assert(info);
			gchar *desc = gst_structure_to_string(info);
			fprintf(stderr, "[%d]: %s\n", i, desc);
			g_free(desc);
		}
	}
	fprintf(stderr, "==========================\n");
	g_list_free_full(devices, gst_object_unref);

	int width = 1280;
	int height = 720;
	struct framerate_fraction framerate = { 5, 1 };
	const char *uri = "/dev/video0";
	if(argc > 1) uri = argv[1];
	if(argc > 2) width = atoi(argv[2]);
	if(argc > 3) height = atoi(argv[3]);
	if(argc > 4) framerate.rate = atoi(argv[4]);
	
	printf("uri: %s\n", uri);
	printf("frame size: %d x %d\n", width, height);
	struct video_source_common *video = video_source_common_init(NULL, 
		video_frame_type_jpeg, NULL);
	assert(video);
	
	video->set_uri(video, uri);
	rc = video->init(video, NULL, width, height, &framerate);
	assert(0 == rc);
	
	assert(video->caps);
	printf("caps: %s\n", gst_caps_to_string(video->caps));
	
	// test dynamic change framerate
	GstCaps *new_caps = gst_caps_copy(video->caps);
	GValue frac = {};
	g_value_init(&frac, GST_TYPE_FRACTION);
	gst_value_set_fraction(&frac, 10, 1);
	gst_caps_set_value(new_caps, "framerate", &frac);
	g_object_set(video->caps_filter, "caps", new_caps, NULL);
	
	gst_object_unref(video->caps);
	video->caps = new_caps;
	g_value_unset(&frac);
	
	video->on_new_frame = on_new_frame;
	video->on_eos = on_eos;
	
	assert(video->pipeline);
	video->play(video);
	video->seek(video, 	80.0);
	
	g_timeout_add(1000, (GSourceFunc)watchdog_process, video);
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	s_loop = loop;
	
	g_main_loop_run(loop);
	quit = 1;
	s_loop = NULL;
	g_main_loop_unref(loop);
	
	sleep(1);
	video_source_common_cleanup(video);
	free(video);
	return 0;
}

