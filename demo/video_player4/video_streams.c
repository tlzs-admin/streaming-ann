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


static long video_stream_get_frame(struct video_stream *stream, long prev_frame, input_frame_t *frame)
{
	pthread_rwlock_rdlock(&stream->rwlock);
	if(stream->frame_number > prev_frame) {

		input_frame_t * current = stream->frame_buffer[0];
		if(NULL == current) {
			pthread_rwlock_unlock(&stream->rwlock);
			return -1;
		}
		
		input_frame_set_bgra(frame, current->bgra, NULL, 0);
		json_object *jresults = current->meta_data;
		if(jresults) frame->meta_data = json_object_get(jresults); // add_ref
	}
	
	prev_frame = stream->frame_number;
	pthread_rwlock_unlock(&stream->rwlock);
	return prev_frame;
}

static void swap_frame_buffer(struct video_stream *stream)
{
	pthread_rwlock_wrlock(&stream->rwlock);
	input_frame_t * frame = stream->frame_buffer[0];
	stream->frame_buffer[0] = stream->frame_buffer[1];
	stream->frame_buffer[1] = frame;
	pthread_rwlock_unlock(&stream->rwlock);
}


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
		struct video_source2 *video = stream->video;
		input_frame_t *frame = stream->frame_buffer[1];
		if(NULL == frame) {
			frame = stream->frame_buffer[1] = input_frame_new();
		}
		
		// clear frame
		if(frame->meta_data) {
			json_object_put((json_object *)frame->meta_data);
			frame->meta_data = NULL;
		}
		input_frame_clear(frame);
		long frame_number = video->get_frame(video, stream->frame_number, frame);
		if(frame_number <= stream->frame_number) {
			nanosleep(&interval, NULL);
			continue;
		}
		stream->frame_number = frame_number;
	//	debug_printf("cur_frame: %ld\n", frame_number);
		if(stream->ai_enabled && stream->num_ai_engines > 0) {
			printf("num_ai_engines: %d\n", stream->num_ai_engines);
			
			// todo: add multiple result support
			json_object *jresult = NULL;
			struct ai_context *ai = &stream->ai_engines[0];
			if(ai->enabled) {
				
				rc = ai->engine->predict(ai->engine, frame, &jresult);
				if(jresult) {
					frame->meta_data = jresult;	
					sleep_flags = 0;
				}
				
				//~ json_object *jresults = json_object_new_array();
				//~ for(int i = 0; i < stream->num_ai_engines; ++i) {
					//~ struct ai_context *ai = &stream->ai_engines[i];
					//~ if(!ai->enabled) continue;
					
					//~ char id[100] = "";
					//~ snprintf(id, sizeof(id), "id_%d", (ai->id>0)?ai->id:(i+1)); 
					//~ json_object *jresult = NULL;
					//~ rc = ai->engine->predict(ai->engine, frame, &jresult);
					//~ json_object_array_add(jresults, jresult?jresult:json_object_new_null());
				//~ }
			}
		}
		swap_frame_buffer(stream);
		if(sleep_flags) nanosleep(&interval, NULL);
		
	}
	
//	pthread_mutex_unlock(&stream->cond_mutex.mutex);
	pthread_exit((void *)(intptr_t)rc);
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
	
	struct video_source2 *video = stream->video;
	if(video && video->is_running) video->stop(video);
	
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
	const char * uri = json_get_value(jinput, string, uri);
	struct video_source2 *video = video_source2_init(NULL, app);
	assert(video);
	int rc = video->set_uri2(video, uri, width, height);
	fprintf(stderr, "== %s(): set_uri2(%s) = %d\n", __FUNCTION__, uri, rc);
	
	
	stream->video = video;
	stream->image_width = width;
	stream->image_height = height;
	stream->ai_enabled = json_get_value(jinput, int, ai_enabled);
	
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
