/*
 * video_streams.c
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

#include "utils.h"
#include "video_streams.h"

struct streaming_proxy_context *app_get_streaming_proxy(struct app_context *app);

static long video_stream_get_frame(struct video_stream *stream, long prev_frame, input_frame_t *input)
{
	pthread_rwlock_rdlock(&stream->rwlock);
	if(stream->frame_number <= prev_frame) {
		pthread_rwlock_unlock(&stream->rwlock);
		return stream->frame_number;
	}

	struct video_frame *frame = stream->frame_buffer[0];
	if(NULL == frame) {
		pthread_rwlock_unlock(&stream->rwlock);
		return -1;
	}
	
	video_frame_addref(frame);
	input->type = frame->type;
	input->data = NULL;
	if(frame->data && frame->length > 0)
	{
		input->data = malloc(frame->length);
		memcpy(input->data, frame->data, frame->length);
	}
	video_frame_unref(frame);
	
	input->length = frame->length;
	input->width = frame->width;
	input->height = frame->height;
	
	json_object *jresults = frame->meta_data;
	if(jresults) input->meta_data = json_object_get(jresults); // add_ref
	
	stream->frame_number = frame->frame_number;
	prev_frame = stream->frame_number;
	
	
	pthread_rwlock_unlock(&stream->rwlock);
	return prev_frame;
}

static void swap_frame_buffer(struct video_stream *stream)
{
	pthread_rwlock_wrlock(&stream->rwlock);
	struct video_frame * frame = stream->frame_buffer[0];
	stream->frame_buffer[0] = stream->frame_buffer[1];
	stream->frame_buffer[1] = frame;
	pthread_rwlock_unlock(&stream->rwlock);
}



gboolean shell_update_frame(gpointer user_data);
static void * video_stream_thread(void *user_data)
{
	int rc = 0;
	struct video_stream *stream = user_data;
	assert(user_data);
	
	struct timespec interval = {
		.tv_sec = 0,
		.tv_nsec = 10 * 1000 * 1000, // 10ms
	};
	
//	pthread_mutex_lock(&stream->cond_mutex.mutex);
	while(!stream->quit) {
		if(stream->paused) {
			nanosleep(&interval, NULL);
			continue;
		}
		
		int sleep_flags = 1;
		
		struct streaming_proxy_context *proxy = stream->proxy;
		assert(proxy);
		
		struct channel_context *channel = proxy->find_channel_by_name(proxy, stream->channel_name);
		if(NULL == channel || channel->frame_number < 0)
		{
			nanosleep(&interval, NULL);
			continue;
		}
		
		struct video_frame *frame = stream->frame_buffer[1];
		stream->frame_buffer[1] = NULL;
		if(frame) {
			// clear frame
			if(frame->meta_data) {
				json_object_put((json_object *)frame->meta_data);
				frame->meta_data = NULL;
			}
			channel->unref_frame(channel, frame);
		}
		frame = channel->get_frame(channel);
		if(NULL == frame || NULL == frame->data || frame->frame_number < 0) {
			if(frame) channel->unref_frame(channel, frame);
			nanosleep(&interval, NULL);
			continue;
		}
		stream->frame_buffer[1] = frame;
		stream->frame_number = frame->frame_number;

		if(stream->ai_enabled && stream->num_ai_engines > 0) {
			printf("num_ai_engines: %d\n", stream->num_ai_engines);
			
			// todo: add multiple result support
			json_object *jresult = NULL;
			struct ai_context *ai = &stream->ai_engines[0];
			if(ai->quit) break;
			
			input_frame_t input[1];
			memset(input, 0, sizeof(input));
			input->type = frame->type;
			input->data = frame->data;
			input->length = frame->length;
			input->width = frame->width;
			input->height = frame->height;
			
			if(ai->enabled) {
				
				pthread_mutex_lock(&ai->mutex);
				rc = ai->engine->predict(ai->engine, input, &jresult);
				pthread_mutex_unlock(&ai->mutex);
				if(jresult) {
					frame->meta_data = jresult;	
					sleep_flags = 0;
				}
			}
			//~ if(stream->face_masking_flag && stream->cv_face) {
				//~ ai_engine_t *dnn_face = stream->cv_face;
				//~ json_object *jface_dets = NULL;
				//~ rc = dnn_face->predict(dnn_face, input, &jface_dets);
				//~ if(jface_dets) {
					//~ json_object *jfaces = NULL;
					//~ json_bool ok = json_object_object_get_ex(jface_dets, "detections", &jfaces);
					//~ if(ok && jfaces) {
						//~ if(NULL == jresult) { // generate default 
							//~ jresult = json_object_new_object();
							//~ json_object_object_add(jresult, "model", json_object_new_string("yolo+face"));
							//~ json_object_object_add(jresult, "detections", json_object_new_array());
						//~ }
						//~ assert(jresult);
						//~ json_object_object_add(jresult, "faces", json_object_get(jfaces));
					//~ }
					//~ json_object_put(jface_dets);
				//~ }
				//~ if(0 == rc && jresult) {
					//~ frame->meta_data = jresult;	
					//~ sleep_flags = 0;
				//~ }
			//~ }
		}
		swap_frame_buffer(stream);
		// g_idle_add(shell_update_frame, stream);
		if(sleep_flags) nanosleep(&interval, NULL);
		
	}
	
//	pthread_mutex_unlock(&stream->cond_mutex.mutex);
	pthread_exit((void *)(intptr_t)rc);
#if defined(WIN32) || defined(_WIN32)
	return ((void *)(intptr_t)rc);
#endif
}

static int video_stream_run(struct video_stream *stream)
{
	pthread_mutex_lock(&stream->cond_mutex.mutex);
	stream->paused = 0;
	pthread_mutex_unlock(&stream->cond_mutex.mutex);
	return 0;
}

static int video_stream_stop(struct video_stream *stream)
{
	pthread_mutex_lock(&stream->cond_mutex.mutex);
	
	stream->quit = 1;
	
	pthread_mutex_unlock(&stream->cond_mutex.mutex);
	return 0;
}


static int video_stream_pause(struct video_stream *stream)
{
	pthread_mutex_lock(&stream->cond_mutex.mutex);
	stream->paused = 1;
	pthread_mutex_unlock(&stream->cond_mutex.mutex);
	return 0;
}


static int video_stream_load_config(struct video_stream *stream, json_object *jstream)
{
	struct app_context *app = stream->app;
	if(NULL == jstream) return -1;
	
	static int default_width = 640;
	static int default_height = 360;

	stream->jstream = jstream;
	json_object *jinput = NULL;
	json_object *jai_engines = NULL;
	json_bool ok = json_object_object_get_ex(jstream, "input", &jinput);
	assert(ok && jinput);
	
	int width = json_get_value_default(jinput, int, width, default_width);
	int height = json_get_value_default(jinput, int, height, default_height);
	
	struct streaming_proxy_context *proxy = app_get_streaming_proxy(app);
	assert(proxy);
	stream->channel_name = json_get_value_default(jinput, string, channel_name, "channel0");
	stream->proxy = proxy;
	
	stream->image_width = width;
	stream->image_height = height;
	stream->ai_enabled = json_get_value(jinput, int, ai_enabled);
	stream->detection_mode = json_get_value(jstream, int, detection_mode);
	
	// parse alert server urls
	json_object *jalert_servers = NULL;
	ok = json_object_object_get_ex(jstream, "alert_server_urls", &jalert_servers);
	if(ok && jalert_servers) {
		ssize_t num_alert_servers = json_object_array_length(jalert_servers);
		if(num_alert_servers > MAX_ALERT_SERVERS_COUNT) num_alert_servers = MAX_ALERT_SERVERS_COUNT;
		
		stream->num_alert_servers = num_alert_servers;
		for(ssize_t i = 0; i < num_alert_servers; ++i) {
			stream->alert_server_urls[i] = json_object_get_string(json_object_array_get_idx(jalert_servers, i));
		}
	}

	int num_ai_engines = 0;
	ok = json_object_object_get_ex(jstream, "ai-engines", &jai_engines);
	if(ok && jai_engines) num_ai_engines = json_object_array_length(jai_engines);
	
	if(num_ai_engines > 0) {
		struct ai_context *engines = calloc(num_ai_engines, sizeof(*engines));
		
		stream->num_ai_engines = num_ai_engines;
		stream->ai_engines = engines;
		
		for(int ii = 0; ii < num_ai_engines; ++ii) {
			json_object *jai = json_object_array_get_idx(jai_engines, ii);
			if(NULL == jai) continue;
			
			int id = json_get_value(jai, int, id);
			engines[ii].id = id;
			engines[ii].enabled = json_get_value(jai, int, enabled);
			engines[ii].engine = find_ai_engine_by_id(app, id);
		}
	}
	return 0;
}

struct video_stream * video_stream_init(struct video_stream *stream, json_object *jstream, struct app_context *app)
{
	if(NULL == stream) stream = calloc(1, sizeof(*stream));
	assert(stream);
	
	stream->app = app;
	stream->get_frame = video_stream_get_frame;
	stream->run = video_stream_run;
	stream->pause = video_stream_pause;
	stream->stop = video_stream_stop;
	
	int rc = 0;
	rc = pthread_rwlock_init(&stream->rwlock, NULL);
	assert(0 == rc);
	
	rc = pthread_cond_init(&stream->cond_mutex.cond, NULL);
	assert(0 == rc);
	
	rc = pthread_mutex_init(&stream->cond_mutex.mutex, NULL);
	assert(0 == rc);
	
	stream->paused = 1;
	
	video_stream_load_config(stream, jstream);
	rc = pthread_create(&stream->th, NULL, video_stream_thread, stream);
	
	
	stream->cv_face = ai_engine_init(NULL, "ai-engine::cvface", stream);
	debug_printf("ai-engine::cvface: %p\n", stream->cv_face);
	if(stream->cv_face) {
		stream->cv_face->init(stream->cv_face, NULL);	// load config and models
	}
	
	return stream;
}


void video_stream_cleanup(struct video_stream *stream)
{
	if(NULL == stream) return;
	stream->stop(stream);
	
	
	void *exit_code = NULL;
	int rc = pthread_join(stream->th, &exit_code);
	
	debug_printf("pthread_join()=%d, stream %d (th=%ld) exited with code %ld\n", 
		rc,
		stream->id, 
		(long)stream->th,
		(long)(intptr_t)exit_code);
	
}
