/*
 * camera-switch.c
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
#include <gst/gst.h>
#include <pthread.h>
#include <libsoup/soup.h>
#include <json-c/json.h>
#include <cairo/cairo.h>

#include "utils.h"
#include "video_source_common.h"
#include "img_proc.h"


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

#define CAMERA_NAME_LEN (100)
struct camera_manager;
struct camera_info
{
	char name[CAMERA_NAME_LEN];
	json_object *jcamera;
	struct video_source_common *video;
	GstState state;
	struct camera_manager *mgr;
	int quit;
	int auto_play;
};
struct camera_info *camera_info_new(json_object *jcamera, struct camera_manager *mgr);
void camera_info_free(struct camera_info *camera);

#define CAMERA_MANAGER_MAX_DEVICES (64)
struct camera_manager
{
	json_object *jconfig;
	void *user_data;
	const char *server_url;
	
	size_t num_cameras;
	struct camera_info *cameras[CAMERA_MANAGER_MAX_DEVICES];
	
	int64_t begin_ticks;
	int64_t interval;
	
	ssize_t camera_index;
	
	// private data
	struct {
		pthread_cond_t cond;
		pthread_mutex_t mutex;
	}cond_mutex;
	
	int is_busy;
	int quit;
	pthread_t th;
	pthread_mutex_t frame_mutex;
	struct video_frame *frame;	// new frame
	struct video_frame *current_frame;
	
	// public method
	struct video_frame *(*set_frame)(struct camera_manager *mgr, struct video_frame *frame);
	struct video_frame *(*addref_frame)(struct camera_manager *mgr, struct video_frame *frame);
	void (*unref_frame)(struct camera_manager *mgr, struct video_frame *frame);
	
	// callbacks
	int (*on_upload_finished)(struct camera_manager *mgr, guint response_code, const char *body, size_t cb_body);
};
struct camera_manager *camera_manager_new_from_config(const char *conf_file, void *user_data);
void camera_manager_free(struct camera_manager *mgr);

/******************************************************************************
 * camera_manager
******************************************************************************/
static void upload_finished(SoupSession *session, SoupMessage *msg, gpointer user_data)
{
	struct camera_manager *mgr = user_data;
	assert(mgr);
	// printf("response_code: %u (%s)\n", msg->status_code, msg->reason_phrase);
	mgr->is_busy = 0;
	SoupMessageBody *body = msg->response_body;
	if(mgr->on_upload_finished) {
		mgr->on_upload_finished(mgr, msg->status_code,
			body?body->data:NULL, body?body->length:0);
	}
	
	struct video_frame *current_frame = mgr->current_frame;
	mgr->current_frame = NULL;
	mgr->unref_frame(mgr, current_frame);
	return;
}

static void *upload_frame(void *user_data)
{
	int rc = 0;
	struct camera_manager *mgr = user_data;
	assert(mgr);
	SoupSession *session = soup_session_new_with_options(SOUP_SESSION_USER_AGENT, "streaming-client/v0.1.0-alpha", NULL);
	assert(session);
	
	pthread_mutex_lock(&mgr->cond_mutex.mutex);
	
	const char *url = mgr->server_url;
	while(!mgr->quit) {
		rc = pthread_cond_wait(&mgr->cond_mutex.cond, &mgr->cond_mutex.mutex);
		if(rc) {
			perror("pthread_cond_wait()");
			break;
		}
		if(mgr->is_busy) continue;
		struct video_frame *frame = mgr->addref_frame(mgr, mgr->frame);
		if(NULL == frame) continue;
		mgr->current_frame = frame;
		
		mgr->is_busy = 1;
		SoupMessage *msg = soup_message_new("POST", url);
		assert(msg);
		soup_message_set_request(msg, "image/jpeg", SOUP_MEMORY_TEMPORARY, (char *)frame->data, frame->length);
		soup_session_queue_message(session, msg, upload_finished, mgr);
	}
	pthread_mutex_unlock(&mgr->cond_mutex.mutex);
	
	g_object_unref(session);
	pthread_exit((void *)(intptr_t)rc);
}

struct video_frame *camera_manager_set_frame(struct camera_manager *mgr, struct video_frame *frame)
{
	pthread_mutex_lock(&mgr->frame_mutex);
	
	struct video_frame *old_frame = mgr->frame;
	mgr->frame = video_frame_addref(frame);
	video_frame_unref(old_frame);
	pthread_mutex_unlock(&mgr->frame_mutex);
	
	pthread_mutex_lock(&mgr->cond_mutex.mutex);
	if(!mgr->is_busy) pthread_cond_signal(&mgr->cond_mutex.cond);
	pthread_mutex_unlock(&mgr->cond_mutex.mutex);
	
	return frame;
}
struct video_frame *camera_manager_addref_frame(struct camera_manager *mgr, struct video_frame *frame)
{
	if(NULL == frame) return NULL;
	pthread_mutex_lock(&mgr->frame_mutex);
	video_frame_addref(frame);
	pthread_mutex_unlock(&mgr->frame_mutex);
	return frame;
}
void camera_manager_unref_frame(struct camera_manager *mgr, struct video_frame *frame)
{
	if(NULL == frame) return;
	pthread_mutex_lock(&mgr->frame_mutex);
	video_frame_unref(frame);
	pthread_mutex_unlock(&mgr->frame_mutex);
}

static json_object *generate_default_config(void)
{
	json_object *jconfig = json_object_new_object();
	json_object *jcameras = json_object_new_array();
	json_object_object_add(jconfig, "server_url", json_object_new_string("http://localhost:8800/default/channel0"));
	json_object_object_add(jconfig, "switch_interval", json_object_new_int(10));  // 10 seconds
	json_object_object_add(jconfig, "cameras", jcameras);
	
	json_object *jcamera = json_object_new_object();
	json_object_object_add(jcamera, "name", json_object_new_string("local"));
	json_object_object_add(jcamera, "uri", json_object_new_string("/dev/video0"));
	json_object_object_add(jcamera, "auto_play", json_object_new_int(1));
	json_object_array_add(jcameras, jcamera);
	
	jcamera = json_object_new_object();
	json_object_object_add(jcamera, "name", json_object_new_string("video1"));
	json_object_object_add(jcamera, "uri", json_object_new_string("/home/chehw/projects/git/streaming-ann/demo/videos/1.mp4"));
	json_object_object_add(jcamera, "auto_play", json_object_new_int(1));
	json_object_array_add(jcameras, jcamera);
	
	return jconfig;
}

struct camera_manager *camera_manager_new_from_config(const char *conf_file, void *user_data)
{
	int rc = 0;
	struct camera_manager *mgr = calloc(1, sizeof(*mgr));
	assert(mgr);
	mgr->user_data = user_data;
	mgr->set_frame = camera_manager_set_frame;
	mgr->addref_frame = camera_manager_addref_frame;
	mgr->unref_frame = camera_manager_unref_frame;
	
	rc = pthread_mutex_init(&mgr->cond_mutex.mutex, NULL);
	rc = pthread_cond_init(&mgr->cond_mutex.cond, NULL);
	rc = pthread_mutex_init(&mgr->frame_mutex, NULL);
	
	mgr->interval = 10; // default switch interval: 10 seconds
	
	if(NULL == conf_file) conf_file = "camera-switch.json";
	json_object *jconfig = json_object_from_file(conf_file);
	if(NULL == jconfig) {
		jconfig = generate_default_config();
		assert(jconfig);
		json_object_to_file_ext(conf_file, jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	}
	
	const char *server_url = json_get_value(jconfig, string, server_url);
	assert(server_url);
	mgr->server_url = server_url;
	mgr->interval = json_get_value_default(jconfig, int, switch_interval, 10);
	
	json_object *jcameras = NULL;
	json_bool ok = json_object_object_get_ex(jconfig, "cameras", &jcameras);
	assert(ok && jcameras);
	int num_cameras = json_object_array_length(jcameras);
	assert(num_cameras < CAMERA_MANAGER_MAX_DEVICES);
	
	for(int i = 0; i < num_cameras; ++i) {
		json_object *jcamera = json_object_array_get_idx(jcameras, i);
		struct camera_info *camera = camera_info_new(jcamera, mgr);
		assert(camera);
		mgr->cameras[mgr->num_cameras++] = camera;
	}
	mgr->num_cameras = num_cameras;

	rc = pthread_create(&mgr->th, NULL, upload_frame, mgr);
	assert(0 == rc);
	
	return mgr;
}



struct video_source_common *camera_manager_get_active_source(struct camera_manager *mgr)
{
	if(mgr->num_cameras <= 0) return NULL;
	
	pthread_mutex_lock(&mgr->frame_mutex);
	if(0 == mgr->begin_ticks) {
		mgr->begin_ticks = get_time_ms(CLOCK_MONOTONIC) / 1000;
		mgr->camera_index = 0;
	}else {
		int64_t ticks = get_time_ms(CLOCK_MONOTONIC) / 1000;
		if((ticks - mgr->begin_ticks) > mgr->interval) {
			mgr->begin_ticks = ticks;
			++mgr->camera_index;
			mgr->camera_index %= mgr->num_cameras;
		}
	}
	struct camera_info *camera = mgr->cameras[mgr->camera_index];
	pthread_mutex_unlock(&mgr->frame_mutex);
	if(NULL == camera) return NULL;
	
	struct video_source_common *video = camera->video;
	if(NULL == video->pipeline) {
		//  retry connection
		video->settings_changed = 1;
		video->init(video, NULL, -1, -1, NULL);
		if(video->pipeline) gst_element_set_state(video->pipeline, GST_STATE_PLAYING);
	}
	
	return camera->video;
}

volatile int g_quit;
static guint g_timer_id;
static gboolean on_timeout(gpointer user_data)
{
	struct camera_manager *mgr = user_data;
	if(g_quit || NULL == mgr) {
		g_timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	
	struct video_source_common *video = camera_manager_get_active_source(mgr);
	if(NULL == video) return G_SOURCE_CONTINUE;
	
	struct camera_info *camera = video->user_data;
	assert(camera);
	
	if(NULL == video->pipeline || video->state != GST_STATE_PLAYING) { // generate a blank image
		
		struct bgra_image bgra[1];
		memset(bgra, 0, sizeof(bgra));
		bgra_image_init(bgra, 320, 240, NULL);
		
		cairo_surface_t *surface = cairo_image_surface_create_for_data(
			bgra->data, CAIRO_FORMAT_RGB24, bgra->width, bgra->height, bgra->width * 4);
		cairo_t *cr = cairo_create(surface);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_set_font_size(cr, 18);
		cairo_move_to(cr, 20, 50);
		cairo_show_text(cr, camera->name);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		
		unsigned char *jpeg = NULL;
		ssize_t cb_jpeg = 0;
		cb_jpeg = bgra_image_to_jpeg_stream(bgra, &jpeg, 90);
		
		struct video_frame *frame = video_frame_new(1, bgra->width, bgra->height, 
			jpeg, cb_jpeg, 
			1 // move memory ( jpeg  ==> frame->data ) 
		);
		assert(frame);
		bgra_image_clear(bgra);
		
		mgr->set_frame(mgr, frame);
		mgr->unref_frame(mgr, frame);
	
		return G_SOURCE_CONTINUE;
	}
	
	
	if(video->state != GST_STATE_PLAYING) return G_SOURCE_CONTINUE;
	
	struct video_frame *frame = video->get_frame(video);
	if(NULL == frame) return G_SOURCE_CONTINUE;
	
	mgr->set_frame(mgr, frame);
	mgr->unref_frame(mgr, frame);
	
	return G_SOURCE_CONTINUE;
}

int camera_manager_run(struct camera_manager *mgr)
{
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_timer_id = g_timeout_add(1000 / 10, on_timeout, mgr);
	g_main_loop_run(loop);
	
	if(g_timer_id) {
		g_source_remove(g_timer_id);
		g_timer_id = 0;
	}
	g_main_loop_unref(loop);
	
	return 0;
}

int camera_manager_stop(struct camera_manager *mgr)
{
	int rc = 0;
	
	for(size_t i = 0; i < mgr->num_cameras; ++i)
	{
		struct camera_info *camera = mgr->cameras[i];
		if(NULL == camera) continue;
		
		mgr->cameras[i] = NULL;
		camera->video->stop(camera->video);
		camera_info_free(camera);
		
	}
	
	if(!mgr->quit) {
		mgr->quit = 1;
		pthread_cond_broadcast(&mgr->cond_mutex.cond);
		void *exit_code = NULL;
		rc = pthread_join(mgr->th, &exit_code);
		fprintf(stderr, "%s() thread exited with code %p, rc = %d\n", __FUNCTION__, exit_code, rc);
	}
	return rc;
}

void camera_manager_free(struct camera_manager *mgr)
{
	if(NULL == mgr) return;
	camera_manager_stop(mgr);
	
	free(mgr);
	return;
}



/******************************************************************************
 * camera_info
******************************************************************************/

//~ static int on_new_frame(struct video_source_common *video, const struct video_frame *frame, void *user_data)
//~ {
	//~ struct camera_info *camera = user_data;
	//~ struct camera_manager *mgr = camera->mgr;
	//~ assert(camera && mgr);
	
	//~ mgr->set_frame(mgr, (struct video_frame *)frame);
	//~ return 0;
//~ }

static int on_eos(struct video_source_common * video, void * user_data)
{
	// auto restart camera
	struct camera_info *camera = video->user_data;
	assert(camera);
	
	debug_printf("%s(camera=%s)\n", __FUNCTION__, camera->name);
	
	video->pause(video);
	video->state = 0;
	video->settings_changed = 1;
	int rc = video->init(video, NULL, -1, -1, NULL);
	rc = video->play(video); 
	
	return rc;
}
static int on_error(struct video_source_common * video, void * user_data)
{
	struct camera_info *camera = video->user_data;
	assert(camera);
	
	debug_printf("%s(camera=%s)\n", __FUNCTION__, camera->name);
	camera->state = GST_STATE_NULL;
	
	return -1;
}
static int on_state_changed(struct video_source_common * video, GstState old_state, GstState new_state, void * user_data)
{
	struct camera_info *camera = user_data;
	camera->state = new_state;
	return 0;
}
struct camera_info *camera_info_new(json_object *jcamera, struct camera_manager *mgr)
{
	assert(jcamera);
	
	int rc = 0;
	struct camera_info *camera = calloc(1, sizeof(*camera));
	assert(camera);
	camera->mgr = mgr;
	camera->jcamera = json_object_get(jcamera);
	
	const char *name = json_get_value(jcamera, string, name);
	if(name) strncpy(camera->name, name, sizeof(camera->name) - 1);
	
	const char *uri = json_get_value(jcamera, string, uri);
	int width = json_get_value_default(jcamera, int, width, -1);
	int height = json_get_value_default(jcamera, int, height, -1);
	camera->auto_play = json_get_value(jcamera, int, auto_play);
	
	struct video_source_common *video = video_source_common_init(NULL, video_frame_type_jpeg, camera);
	assert(video);
	video->set_framerate(video, 5, 1);
	rc = video->init(video, uri, width, height, NULL);
	assert(0 == rc);
	
//	video->on_new_frame = on_new_frame;
	video->on_eos = on_eos;
	video->on_error = on_error;
	video->on_state_changed = on_state_changed;
	
	if(camera->auto_play && video->pipeline) {
		gst_element_set_state(video->pipeline, GST_STATE_PLAYING);
		// video->play(video);
	}
	camera->video = video;
	return camera;
}
void camera_info_free(struct camera_info *camera)
{
	if(NULL == camera) return;
	struct camera_manager *mgr = camera->mgr;
	assert(mgr);
	///< @todo  mgr->remove_camera(mgr, camera);
	
	if(camera->jcamera) {
		json_object_put(camera->jcamera);
		camera->jcamera = NULL;
	}
	
	struct video_source_common *video = camera->video;
	camera->video = NULL;
	if(video) {
		video_source_common_cleanup(video);
		free(video);
	}
	free(camera);
}

/******************************************************************************
 * main
******************************************************************************/

int main(int argc, char **argv)
{
	gst_init(&argc, &argv);
	
	const char *conf_file = "camera-switch.json";
	if(argc > 1) conf_file = argv[1];
	struct camera_manager *mgr = camera_manager_new_from_config(conf_file, NULL); 
	assert(mgr);
	
	camera_manager_run(mgr);
	
	camera_manager_free(mgr);
	return 0;
}

