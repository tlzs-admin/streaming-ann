/*
 * video_source_common.c
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

#include <gst/app/gstappsink.h>

#include <gio/gio.h>
#include <sys/stat.h>
#include "utils.h"
#include "video_source_common.h"

#define PROTOCOL_rtsp       "rtsp://"
#define PROTOCOL_rtspt      "rtspt://"
#define PROTOCOL_https      "https://"
#define PROTOCOL_file       "file://"
#define PROTOCOL_v4l2       "/dev/video"


static const char * s_video_source_protocols[video_source_types_count] = {
	[video_source_type_unknown] = "default",
	[video_source_type_file] = PROTOCOL_file,
	[video_source_type_v4l2] = PROTOCOL_v4l2,
	[video_source_type_https] = PROTOCOL_https,
	[video_source_type_rtsp] = PROTOCOL_rtsp,
	[video_source_type_rtspt] = PROTOCOL_rtspt,
};

static gboolean is_regular_file(const char *path_name)
{
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc < 0) return FALSE;
	
	return ((st->st_mode & S_IFMT) == S_IFREG);
}
static gboolean is_video_file(const char * path_name, char **p_content_type, int * p_subtype)
{
	if(!is_regular_file(path_name)) return FALSE;
	gchar * content_type = NULL;
	gboolean uncertain = TRUE;
	content_type = g_content_type_guess(path_name, NULL, 0, &uncertain);
	if(uncertain || NULL == content_type) {
		if(content_type) g_free(content_type);
		return FALSE;
	}
	
	gboolean ret = TRUE;
	int subtype = video_source_subtype_default;
	if(g_content_type_equals(content_type, "video/mp4")) {
		subtype |= video_source_subtype_mp4;
	}else if(g_content_type_equals(content_type, "video/x-matroska")) {
		subtype |= video_source_subtype_mkv;
	}else if(g_content_type_equals(content_type, "application/vnd.rn-realmedia")) {
		subtype |= video_source_subtype_rmvb;
	}else if(g_content_type_equals(content_type, "video/x-msvideo")) {
		subtype |= video_source_subtype_avi;
	}else {
		ret = g_content_type_equals(content_type, "video/mpeg") 
		 || g_content_type_equals(content_type, "video/quicktime") 
		 || g_content_type_equals(content_type, "video/webm") 
		 || g_content_type_equals(content_type, "video/x-flv") 
		 || g_content_type_equals(content_type, "video/x-ms-wmv") 
		 || 0;
	}
	if(p_subtype) *p_subtype = subtype;

	if(p_content_type) *p_content_type = content_type;
	else g_free(content_type);
	
	return ret;
}

enum video_source_type video_source_type_from_uri(const char * uri, int * p_subtype)
{
	enum video_source_type type = video_source_type_unknown;
	
	for(type = video_source_type_v4l2; type < video_source_types_count; ++type)
	{
		if(s_video_source_protocols[type] && 
			strncasecmp(uri, s_video_source_protocols[type], strlen(s_video_source_protocols[type])) == 0) break;
	}
	
	int subtype = video_source_subtype_default;
	if(type == video_source_types_count) {
		if(strncasecmp(uri, PROTOCOL_file, sizeof(PROTOCOL_file) - 1) == 0) uri += sizeof(PROTOCOL_file) - 1;
		
		gboolean ok = is_video_file(uri, NULL, &subtype);
		if(ok) type = video_source_type_file;
	}
	else if(type == video_source_type_https) {
		uri += strlen(s_video_source_protocols[type]);
		if(strncasecmp(uri, "www.youtube.com", sizeof("www.youtube.com") - 1) == 0) {
			subtype |= video_source_subtype_youtube;
		}else {
			char * p_ext = strrchr(uri, '.');
			if(p_ext && strcasecmp(p_ext, ".m3u8") == 0) subtype |= video_source_subtype_hls;
		}
	}
	
	if(p_subtype) *p_subtype = subtype;
	return type;
}


ssize_t youtube_uri_parse(const char * youtube_url, char embed_uri[static 4096], size_t size)
{
	static const char * fmt = "youtube-dl " 
		" --format 'best[ext=mp4][protocol=https][height<=480]/best' "
		" --get-url '%s' ";
		
	char command[8192] = "";
	snprintf(command, sizeof(command), fmt, youtube_url);
	FILE * fp = popen(command, "r");
	if(NULL == fp) return -1;
	
	char * uri = fgets(embed_uri, size, fp);
	int rc = pclose(fp);
	
	ssize_t cb = 0;
	if(uri) {
		cb = strlen(uri);
		while(cb > 0 && (uri[cb - 1] == '\n' || uri[cb - 1] == '\r')) uri[--cb] = '\0';
	}
	
	debug_printf("rc=%d, embed_uri: %s\n", rc, uri?uri:"");
	
	return cb;
}


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

/******************************************************************************
 * video_frame
******************************************************************************/
struct video_frame *video_frame_new(long frame_number, int width, int height, const void *image_data, size_t length, int take_memory)
{
	struct video_frame *frame = calloc(1, sizeof(*frame));
	assert(frame);
	
	frame->type = video_frame_type_unknown;
	frame->frame_number = frame_number;
	frame->ticks_ms = get_time_ms(CLOCK_MONOTONIC);
	frame->width = width;
	frame->height = height;
	if(image_data) {
		if(take_memory) frame->data = (unsigned char *)image_data;
		else {
			if(length > 0) {
				frame->data = malloc(length);
				assert(frame->data);
				memcpy(frame->data, image_data, length);
			}
		}
	}
	frame->length = length;
	frame->refs = 1;
	return frame;
}
void video_frame_unref(struct video_frame *frame)
{
	if(NULL == frame || frame->refs <= 0) return;
	if(0 == --frame->refs) {
		if(frame->data) { free(frame->data); frame->data = NULL; }
		free(frame);
	}
	return;
}

/******************************************************************************
 * video_source_common
******************************************************************************/
static int video_set_uri(struct video_source_common * video, const char *uri);
static int video_set_resolution(struct video_source_common *video, int width, int height);
static int video_set_framerate(struct video_source_common *video, int rate, int denominator);

static int video_init(struct video_source_common *video, const char *uri, int width, int height, const struct framerate_fraction *framerate);
static struct video_frame * video_get_frame(struct video_source_common * video);
static int video_play(struct video_source_common * video);
static int video_pause(struct video_source_common * video);
static int video_stop(struct video_source_common * video);
static int video_seek(struct video_source_common * video, double position  /* seconds */);
static int video_query_position(struct video_source_common *video, double *position, double *duration);

// callbacks
static int video_on_eos(struct video_source_common * video, void * user_data);
static int video_on_error(struct video_source_common * video, void * user_data);
static int video_on_state_changed(struct video_source_common * video, GstState old_state, GstState new_state, void * user_data);
static int video_on_new_frame(struct video_source_common *video, const struct video_frame *frame, void *user_data);

struct video_source_common *video_source_common_init(struct video_source_common *video, enum video_frame_type frame_type, void *user_data)
{
	if(NULL == video) video = calloc(1, sizeof(*video));
	assert(video);
	video->frame_type = frame_type;
	
	video->init = video_init;
	video->set_uri = video_set_uri;
	video->set_resolution = video_set_resolution;
	video->set_framerate = video_set_framerate;
	
	video->get_frame = video_get_frame;
	video->play = video_play;
	video->pause = video_pause;
	video->stop = video_stop;
	video->seek = video_seek;
	video->query_position = video_query_position;
	
	video->on_new_frame = video_on_new_frame;
	video->on_eos = video_on_eos;
	video->on_error = video_on_error;
	video->on_state_changed = video_on_state_changed;
	
	return video;
}
void video_source_common_cleanup(struct video_source_common *video)
{
	if(NULL == video) return;
	GstElement *pipeline = video->pipeline;
	video->pipeline = NULL;
	if(pipeline) {
		gst_element_set_state(pipeline, GST_STATE_NULL);
		
		GstState state = 0;
		gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
		printf("pipeline state: %d\n", (int) state);
		gst_object_unref(pipeline);
	}
}


static int video_set_uri(struct video_source_common * video, const char *uri)
{
	if(NULL == uri || uri == video->uri) return 0;
	strncpy(video->uri, uri, sizeof(video->uri) - 1);
	video->settings_changed = 1;
	return 0;
}

static int video_set_resolution(struct video_source_common *video, int width, int height)
{
	if((video->settings_changed = (width != video->width || height != video->height))) {
		video->width = width;
		video->height = height;
	}
	return 0;
}

static int video_set_framerate(struct video_source_common *video, int rate, int denominator)
{
	if(denominator <= 0) denominator = 1;
	if((video->settings_changed = (video->framerate.rate != rate || video->framerate.denominator != denominator)))
	{
		video->framerate.rate = rate;
		video->framerate.denominator = denominator;
	}
	return 0;
}

static inline int make_launch_command(char command[static 8192], size_t size,
	enum video_source_type type, 
	const char *uri, enum video_frame_type frame_type, 
	int width, int height, const struct framerate_fraction framerate)
{
	assert(command && size > 0 && uri);
	assert(frame_type >= 0 && frame_type < VIDEO_FRAME_TYPES_COUNT);
	
	int cb = 0;
	char *p = command;
	char *p_end = p + size;
	
	int rate = framerate.rate;
	int denominator = framerate.denominator;
	if(denominator <= 0) denominator = 1;
	
	switch(type) {
	case video_source_type_v4l2: 
		cb = snprintf(p, p_end - p, "v4l2src device=%s io-mode=2", uri);
		break;
	case video_source_type_file:
		if(0 == strncasecmp(uri, "file://", 7)) uri += 7;
		cb = snprintf(p, p_end - p, "uridecodebin uri=\"file://%s\"", uri);
		break;
	default:
		cb = snprintf(p, p_end - p, "uridecodebin uri=\"%s\"", uri);
		break;
	}
	assert(cb > 0);
	p += cb;
	
	if(rate > 0) {
		cb = snprintf(p, p_end - p, " ! videorate");
		assert(cb > 0);
		p += cb;
	}
	if(width > 0 && height > 0 && type != video_source_type_v4l2) {
		cb = snprintf(p, p_end - p, " ! videoscale");
		assert(cb > 0);
		p += cb;
	}
	
	cb = snprintf(p, p_end -p, " ! capsfilter name=caps caps=video/x-raw");
	assert(cb > 0);
	p += cb;
	if(frame_type == video_frame_type_bgra) {
		cb = snprintf(p, p_end -p, ",format=BGRA");
		assert(cb > 0);
		p += cb;
	}
	
	if(width > 0 && height > 0) {
		cb = snprintf(p, p_end - p, ",width=%d,height=%d", width, height);
		assert(cb > 0);
		p += cb;
	}
	if(rate > 0) {
		cb = snprintf(p, p_end - p, ",framerate=%d/%d", rate, denominator);
		assert(cb > 0);
		p += cb;
	}
	
	if(frame_type == video_frame_type_jpeg) {
		cb = snprintf(p, p_end - p, " ! jpegenc");
		assert(cb > 0);
		p += cb;
	}
	
	cb = snprintf(p, p_end - p, " ! appsink name=appsink ");
	assert(cb > 0);
	p += cb;
	
	printf("gst_command: %s\n", command);
	return 0;
}

static void app_sink_eos(GstAppSink *sink, gpointer user_data)
{
	debug_printf("%s()...", __FUNCTION__);
	struct video_source_common *video = user_data;
	assert(video);
	if(video->on_eos) video->on_eos(video, video->user_data);
}

static const char *s_gst_state_string[GST_STATE_PLAYING + 1] = {
	[GST_STATE_VOID_PENDING] = "GST_STATE_VOID_PENDING",
	[GST_STATE_NULL] = "GST_STATE_NULL",
	[GST_STATE_READY] = "GST_STATE_READY",
	[GST_STATE_PAUSED] = "GST_STATE_PAUSED",
	[GST_STATE_PLAYING] = "GST_STATE_PLAYING",
};
const char *gst_state_to_string(GstState state) 
{
	if(state < 0 || state > GST_STATE_PLAYING) return NULL;
	return s_gst_state_string[(int)state];
}

GstFlowReturn app_sink_new_preroll(GstAppSink *sink, gpointer user_data)
{
	debug_printf("%s()...", __FUNCTION__);
	struct video_source_common *video = user_data;
	assert(video && video->pipeline);
	// query duration and positions
	
	gint64 duration = 0;
	gint64 position = 0;
	
	gst_element_query_duration(video->pipeline, GST_FORMAT_TIME, &duration);
	gst_element_query_position(video->pipeline, GST_FORMAT_TIME, &position);
	
	video->duration = (double)duration / GST_SECOND;
	video->position = (double)position / GST_SECOND;
	
	printf("\e[32m" "duration: %.3f s, position = %.3f s\n",
		video->duration,
		video->position);
	
	return GST_FLOW_OK;
}

GstFlowReturn app_sink_new_sample(GstAppSink *sink, gpointer user_data)
{
	debug_printf("%s()...", __FUNCTION__);
	struct video_source_common *video = user_data;
	assert(video);
	
	++video->frame_number;
	GstSample *sample = gst_app_sink_pull_sample(sink);
	if(sample) {
		if(video->on_new_frame) {
			int width = -1;
			int height = -1;
			GstCaps *caps = gst_sample_get_caps(sample);
			assert(caps);
			ssize_t num_infos = gst_caps_get_size(caps);
			assert(num_infos > 0);
			
			const GstStructure *info = gst_caps_get_structure(caps, 0);
			assert(info);
			gst_structure_get_int(info, "width", &width);
			gst_structure_get_int(info, "height", &height);
			assert(width > 0 && height > 0);
			
			GstBuffer *buffer = gst_sample_get_buffer(sample);
			if(buffer) {
				GstMapInfo map;
				memset(&map, 0, sizeof(map));
				gst_buffer_map(buffer, &map, GST_MAP_READ);
				
				struct video_frame *frame = video_frame_new(video->frame_number, 
					width, height, 
					map.data, map.size, 0);
				assert(frame);
				video->on_new_frame(video, frame, video->user_data);
				video_frame_free(frame);
				gst_buffer_unmap(buffer, &map);
			}
		}
		gst_sample_unref(sample);
	}
	return GST_FLOW_OK;
}

static gboolean on_pipeline_eos(GstBus *bus, GstMessage *message, struct video_source_common *video)
{
	debug_printf("%s() ...", __FUNCTION__);
	video->err_code = 1;
	return TRUE;
}
static gboolean on_pipeline_error(GstBus *bus, GstMessage *message, struct video_source_common *video)
{
	debug_printf("%s() ...", __FUNCTION__);
	GError *gerr = NULL;
	gchar *debug_info = NULL;
	
	const char *objname = GST_OBJECT_NAME(message->src);
	gst_message_parse_error(message, &gerr, &debug_info);
	
	video->err_code = 2;
	if(gerr) {
		fprintf(stderr, "%s(%d)::%s(): element=%s, err=%s\n", __FILE__, __LINE__, __FUNCTION__, 
			objname, gerr->message);
		g_error_free(gerr);
	}
	if(debug_info) {
		fprintf(stderr, "%s(%d)::%s(): debug_info=%s\n", __FILE__, __LINE__, __FUNCTION__, debug_info);
		g_free(debug_info);
	}
	return TRUE;
}
static gboolean on_pipeline_state_changed(GstBus *bus, GstMessage *message, struct video_source_common *video)
{
	GstState old_state, new_state, pending_state;
	if(GST_MESSAGE_SRC(message) == GST_OBJECT(video->pipeline)) {
		gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
		debug_printf("%s(): obj=%s, old_state=%d, new_state=%d, pending=%d", __FUNCTION__, 
			GST_OBJECT_NAME(message->src),
		old_state, new_state, pending_state);
		video->state = new_state;
	}
	return TRUE;
}



static int relaunch_pipeline(struct video_source_common *video)
{
	if(!video->uri[0]) {
		fprintf(stderr, "invalid uri\n");
		return -1;
	}
	
	GstElement *pipeline = video->pipeline;
	if(pipeline) {
		printf("release old pipeline");
		gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_object_unref(video->pipeline);
		video->pipeline = NULL;
	}
	
	debug_printf("uri: %s", video->uri);
	enum video_source_type type;
	int subtype = 0;
	type = video_source_type_from_uri(video->uri, &subtype);
	if(type == video_source_type_unknown || type >= video_source_types_count) {
		fprintf(stderr, "[ERROR]::invalid uri: %s\n", video->uri);
		return -1;
	}
	
	if(type == video_source_type_https && subtype == video_source_subtype_youtube) {
		ssize_t cb = youtube_uri_parse(video->uri, video->uri, sizeof(video->uri) - 1);
		if(cb <= 0) {
			fprintf(stderr, "[ERROR]::parse youtube uri failed. uri = %s\n", video->uri);
			return -1;
		}
	}
	
	if(0 != make_launch_command(video->gst_command, sizeof(video->gst_command),
		type, 
		video->uri, video->frame_type, 
		video->width, video->height, video->framerate)) return -1;
	
	GError *gerr = NULL;
	pipeline = gst_parse_launch(video->gst_command, &gerr);
	if(gerr) {
		fprintf(stderr, "error: %s\n", gerr->message);
		g_error_free(gerr);
		gerr = NULL;
		return -1;
	}
	assert(pipeline);
	
	GstElement *caps_filter = gst_bin_get_by_name(GST_BIN(pipeline), "caps");
	if(caps_filter) {
		video->caps_filter = caps_filter;
		g_object_get(G_OBJECT(caps_filter), "caps", &video->caps, NULL);
	}
	GstAppSink *sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline), "appsink"));
	assert(sink);
	video->appsink = GST_ELEMENT(sink);
	GstAppSinkCallbacks callbacks = {
		.eos = app_sink_eos,
		.new_preroll = app_sink_new_preroll,
		.new_sample = app_sink_new_sample,
	};
	gst_app_sink_set_callbacks(sink, &callbacks, video, NULL);
	video->pipeline = pipeline;
	
	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	assert(bus);
	gst_bus_add_signal_watch(bus);
	g_signal_connect(bus, "message::eos", G_CALLBACK(on_pipeline_eos), video);
	g_signal_connect(bus, "message::error", G_CALLBACK(on_pipeline_error), video);
	g_signal_connect(bus, "message::state-changed", G_CALLBACK(on_pipeline_state_changed), video);
	gst_object_unref(bus);
	
	gst_element_set_state(video->pipeline, GST_STATE_READY);
	return 0;
}

static int video_init(struct video_source_common *video, const char *uri, int width, int height, const struct framerate_fraction *framerate)
{
	if(uri) video->set_uri(video, uri);
	video->set_resolution(video, width, height);
	if(framerate) video->set_framerate(video, framerate->rate, framerate->denominator);
	
	video->frame_number = -1;
	return relaunch_pipeline(video);
}

static struct video_frame * video_get_frame(struct video_source_common * video)
{
	return NULL;
}
static int video_play(struct video_source_common * video)
{
	GstElement *pipeline = video->pipeline;
	if(NULL == pipeline) return -1;
	
	GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
	debug_printf("%s(): ret = %d\n", __FUNCTION__, ret);
	assert(ret != GST_STATE_CHANGE_FAILURE);
	
	if(ret == GST_STATE_CHANGE_FAILURE) return -1;
	
	if(ret == GST_STATE_CHANGE_ASYNC) {
		ret = gst_element_get_state(pipeline, &video->state, NULL, GST_CLOCK_TIME_NONE);
	}
	if(video->state != GST_STATE_PLAYING) return -1;
	return 0;
}
static int video_pause(struct video_source_common * video)
{
	GstElement *pipeline = video->pipeline;
	if(NULL == pipeline) return -1;
	
	GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
	debug_printf("%s(): ret = %d\n", __FUNCTION__, ret);
	assert(ret != GST_STATE_CHANGE_FAILURE);
	
	if(ret == GST_STATE_CHANGE_FAILURE) return -1;
	
	if(ret == GST_STATE_CHANGE_ASYNC) {
		ret = gst_element_get_state(pipeline, &video->state, NULL, GST_CLOCK_TIME_NONE);
	}
	if(video->state != GST_STATE_PAUSED) return -1;
	return 0;
}
static int video_stop(struct video_source_common * video)
{
	GstElement *pipeline = video->pipeline;
	if(NULL == pipeline) return -1;
	
	GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_NULL);
	debug_printf("%s(): ret = %d\n", __FUNCTION__, ret);
	assert(ret != GST_STATE_CHANGE_FAILURE);
	
	if(ret == GST_STATE_CHANGE_FAILURE) return -1;
	if(ret == GST_STATE_CHANGE_ASYNC) {
		ret = gst_element_get_state(pipeline, &video->state, NULL, GST_CLOCK_TIME_NONE);
	}
	return 0;
}
static int video_seek(struct video_source_common * video, double position  /* seconds */)
{
	if(!gst_element_seek_simple(video->pipeline, GST_FORMAT_TIME, 
		GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
		(gint64)(position * GST_SECOND))) return -1;
		
	gint64 cur_pos = 0;
	if(gst_element_query_position(video->pipeline, GST_FORMAT_TIME, &cur_pos)) {
		video->position = cur_pos / GST_SECOND;
	}
	return 0;
}

static int video_query_position(struct video_source_common *video, double *position, double *duration)
{
	gint64 value = -1;
	if(position) {
		if(!gst_element_query_position(video->pipeline, GST_FORMAT_TIME, &value)) return -1;
		*position = (double)value / GST_SECOND;
	}
	if(duration) {
		if(!gst_element_query_duration(video->pipeline, GST_FORMAT_TIME, &value)) return -1;
		*duration = (double)value / GST_SECOND;
	}
	return 0;
}

// callbacks
static int video_on_eos(struct video_source_common * video, void * user_data)
{
	return 0;
}
static int video_on_error(struct video_source_common * video, void * user_data)
{
	return -1;
}
static int video_on_state_changed(struct video_source_common * video, GstState old_state, GstState new_state, void * user_data)
{
	return -1;
}
static int video_on_new_frame(struct video_source_common *video, const struct video_frame *frame, void *user_data)
{
	return -1;
}
