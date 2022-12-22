/*
 * video_stream.c
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

#include <curl/curl.h>
#include <pthread.h>

#include <cairo/cairo.h>
#include "video_stream.h"
#include "utils.h"
#include "img_proc.h"
#include "webserver.h"

static int on_new_frame(struct video_source_common *video, const struct video_frame *frame, void *user_data)
{
	if(NULL == frame || NULL == frame->data) return -1;
	assert(frame->type == video_frame_type_jpeg);
	assert(frame->length > 0);
	
	struct device_stream *stream = user_data;
	assert(stream && stream->video == video);
	
	if(stream->busy) return 1;
	
	struct video_frame *current = calloc(1, sizeof(*current));
	assert(current);
	
	*current = *frame;
	current->refs = 1;
	current->meta_data = NULL;
	current->cb_meta_data = 0;
	current->data = malloc(frame->length);
	assert(current->data);
	memcpy(current->data, frame->data, frame->length);
	
	pthread_mutex_lock(&stream->cond_mutex.mutex);
	stream->busy = 1;
	stream->frame = current;
	
	pthread_cond_signal(&stream->cond_mutex.cond);
	pthread_mutex_unlock(&stream->cond_mutex.mutex);
	
	return 0;
}


struct response_closure
{
	json_tokener *jtok;
	json_object *jresult;
	enum json_tokener_error jerr;
};

static size_t on_response(char *ptr, size_t size, size_t n, struct response_closure *ctx)
{
	size_t cb = size * n;
	if(cb == 0) return cb;
	if(ctx->jerr == json_tokener_success) return cb;
	
	assert(ctx->jtok);
	ctx->jresult = json_tokener_parse_ex(ctx->jtok, ptr, cb);
	ctx->jerr = json_tokener_get_error(ctx->jtok);
	if(ctx->jerr == json_tokener_continue || ctx->jerr == json_tokener_success) return cb;
	
	fprintf(stderr, "parse result failed: %s\n", json_tokener_error_desc(ctx->jerr));
	return 0;
}


static int ai_request(CURL *curl, const char *server_url, 
	struct curl_slist *request_headers, 
	const struct video_frame *frame,
	json_object **p_jresult
	)
{
	debug_printf("%s(server_url=%s)\n", __FUNCTION__, server_url);
	if(NULL == frame || NULL == frame->data || frame->length <= 0) return -1;
	assert(frame->type == video_frame_type_jpeg);
	
	struct response_closure closure = { NULL };
	closure.jtok = json_tokener_new();
	closure.jerr = json_tokener_error_parse_eof;
	
	curl_easy_setopt(curl, CURLOPT_URL, server_url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, frame->data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)frame->length);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &closure);
	
	CURLcode ret = curl_easy_perform(curl);
	long response_code = 0;
	
	printf("curl ret: %d\n", ret);
	if(ret == CURLE_OK) {
		ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	}
	curl_easy_reset(curl);
	json_tokener_free(closure.jtok);
	closure.jtok = NULL;
	
	if(ret == CURLE_OK) {
		*p_jresult = closure.jresult;
		return (response_code >=200 && response_code < 300)?0:1;
	}
	
	fprintf(stderr, "%s(%d)::send request failed: %s\n", 
		__FILE__, __LINE__, curl_easy_strerror(ret));
	if(closure.jresult) {
		fprintf(stderr, "  ==> jresult: %s\n", 
			json_object_to_json_string_ext(closure.jresult, JSON_C_TO_STRING_PRETTY));
	}
	json_object_put(closure.jresult);
	return -1;
}

static void draw_frame(struct video_frame *frame, json_object *jresult, const char *channel_name)
{
	assert(frame->type == video_frame_type_jpeg);
	assert(frame->data && frame->length > 0);
	
	if(NULL == jresult) return;
	json_object *jdetections = NULL;
	json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
	if(!ok || NULL == jdetections) return;
	
	int num_detections = json_object_array_length(jdetections);
	if(num_detections <= 0) return;
	
	struct bgra_image bgra[1];
	memset(bgra, 0, sizeof(bgra));
	
	bgra_image_from_jpeg_stream(bgra, frame->data, frame->length);
	cairo_surface_t *surface = cairo_image_surface_create_for_data(bgra->data, CAIRO_FORMAT_RGB24, 
		bgra->width, bgra->height, bgra->width * 4);
	assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
	
	
	cairo_t *cr = cairo_create(surface);
	
	double font_size = (double)bgra->height / 30;
	cairo_set_font_size(cr, font_size);
	cairo_select_font_face(cr, "mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	ssize_t persons_count = 0;
	for(int i = 0; i < num_detections; ++i) {
		json_object *jdet = json_object_array_get_idx(jdetections, i);
		if(NULL == jdet) continue;
		int class_index = json_get_value_default(jdet, int, class_index, -1);
		const char *class_name = json_get_value_default(jdet, string, class, "");
		
		double x = json_get_value(jdet, double, left);
		double y = json_get_value(jdet, double, top);
		double cx = json_get_value(jdet, double, width);
		double cy = json_get_value(jdet, double, height);
		
		x  *= bgra->width; 
		y  *= bgra->height; 
		cx *= bgra->width; 
		cy *= bgra->height;
		
		if(class_index == 0) ++persons_count;
		
		cairo_set_source_rgba(cr, class_index == 0, class_index == 0, class_index != 0, 1);
		cairo_rectangle(cr, x, y, cx, cy);
		cairo_stroke(cr);
		
		cairo_move_to(cr, x + 2, y + font_size);
		cairo_show_text(cr, class_name);
	}
	
	cairo_text_extents_t extent;
	cairo_set_font_size(cr, bgra->height / 20);
	if(channel_name) {
		cairo_text_extents(cr, channel_name, &extent);
		
		cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.8);
		cairo_rectangle(cr, 10, 10, extent.width + 20, extent.height + 20);
		cairo_fill(cr);
	
		cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
		cairo_move_to(cr, 10, 10 + bgra->height / 20);
		cairo_show_text(cr, channel_name);
	}
	
	char text[100] = "";
	
	snprintf(text, sizeof(text) - 1, "Persons Count: %ld", (long)persons_count);
	
	cairo_text_extents(cr, text, &extent);
	
	
		
	cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.8);
	cairo_rectangle(cr, bgra->width - 10 - extent.width, 10, extent.width + 20, extent.height + 20);
	cairo_fill(cr);
	
	cairo_move_to(cr, bgra->width - 10 - extent.width, 15 + extent.height);
	cairo_set_source_rgba(cr, 1, 1, 0, 1);
	cairo_show_text(cr, text);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	
	
	unsigned char *jpeg = NULL;
	ssize_t cb_jpeg = bgra_image_to_jpeg_stream(bgra, &jpeg, 90);
	
	if(jpeg && cb_jpeg > 0) {
		free(frame->data);
		frame->data = jpeg;
		frame->length = cb_jpeg;
	}else {
		if(jpeg) free(jpeg);
	}
	
	bgra_image_clear(bgra);
	return;
}

static void *stream_process(void *user_data)
{
	int rc = 0;
	struct device_stream *stream = user_data;
	assert(stream && stream->is_running);
	
	CURL *curl = curl_easy_init();
	assert(curl);
	struct curl_slist *request_headers = NULL;
	request_headers = curl_slist_append(request_headers, "Content-Type: image/jpeg");

	pthread_mutex_lock(&stream->cond_mutex.mutex);
	while(stream->is_running) {
		rc = pthread_cond_wait(&stream->cond_mutex.cond, &stream->cond_mutex.mutex);
		if(rc == -1) break;
		if(!stream->is_running) break;
	
		struct video_frame *frame = stream->frame;
		assert(frame);
		
		json_object *jresult = NULL;
		if(stream->server_url) {
			rc = ai_request(curl, stream->server_url, request_headers, frame, &jresult);
			if(0 == rc && jresult) {
				draw_frame(frame, jresult, stream->channel?stream->channel->name:NULL);
			}
		}
		if(0 == rc && stream->on_update_frame) {
			stream->on_update_frame(stream, frame, jresult);
		}
		
		device_stream_unref_frame(stream, frame);
		if(jresult) json_object_put(jresult);
		
		stream->busy = 0;
	}
	
	curl_slist_free_all(request_headers);
	curl_easy_cleanup(curl);
	
	pthread_exit((void *)(intptr_t)rc);
}


static gboolean restart_stream(gpointer user_data)
{
	struct video_source_common * video = user_data;
	assert(video);
	
	video->init(video, NULL, -1, -1, NULL);
	video->settings_changed = 1;
	video->play(video);

	return G_SOURCE_REMOVE;
}

static int on_video_eos(struct video_source_common * video, void * user_data)
{
	g_idle_add(restart_stream, video);
	return 0;
}
static int on_video_error(struct video_source_common * video, void * user_data)
{
	return 0;
}

struct device_stream *device_stream_new_from_config(json_object *jstream, void *user_data)
{
	int rc = 0;
	assert(jstream);
	struct device_stream *stream = calloc(1, sizeof(*stream));
	assert(stream);
	stream->user_data = user_data;
	
	rc = pthread_mutex_init(&stream->frame_mutex, NULL);
	rc = pthread_mutex_init(&stream->cond_mutex.mutex, NULL);
	rc = pthread_cond_init(&stream->cond_mutex.cond, NULL);
	assert(0 == rc);
	
	stream->server_url = json_get_value(jstream, string, ai_server_url);
	
	const char *uri = json_get_value(jstream, string, uri);
	int width = json_get_value_default(jstream, int, width, -1);
	int height = json_get_value_default(jstream, int, height, -1);
	
	assert(uri);
	struct video_source_common *video = video_source_common_init(NULL, video_frame_type_jpeg, stream);
	assert(video);
	stream->video = video;
	
	video->on_new_frame = on_new_frame;
	rc = video->init(video, uri, width, height, &(struct framerate_fraction){5, 1});
	assert(0 == rc);
	
	video->on_eos = on_video_eos;
	video->on_error = on_video_error;
	
	stream->is_running = 1;
	rc = pthread_create(&stream->th, NULL, stream_process, stream);
	assert(0 == rc);
	
	video->play(video);
	return stream;
}

void device_stream_free(struct device_stream *stream)
{
	if(NULL == stream) return;
	
	if(stream->is_running) {
		stream->is_running = 0;
		
		pthread_mutex_lock(&stream->cond_mutex.mutex);
		pthread_cond_broadcast(&stream->cond_mutex.cond);
		pthread_mutex_unlock(&stream->cond_mutex.mutex);
		
		void *exit_code = NULL;
		int rc = pthread_join(stream->th, &exit_code);
		
		fprintf(stderr, "%s(): pthread exited with code %p, rc = %d\n", __FUNCTION__, exit_code, rc);
	}
	
}

struct video_frame * device_stream_addref_frame(struct device_stream *stream, struct video_frame *frame)
{
	pthread_mutex_lock(&stream->frame_mutex);
	frame = video_frame_addref(frame);
	pthread_mutex_unlock(&stream->frame_mutex);
	return frame;
}
void device_stream_unref_frame(struct device_stream *stream, struct video_frame *frame)
{
	pthread_mutex_lock(&stream->frame_mutex);
	video_frame_unref(frame);
	pthread_mutex_unlock(&stream->frame_mutex);
	return;
}
