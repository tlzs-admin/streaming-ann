#ifndef DEMO_VIDEO_STREAMS_H_
#define DEMO_VIDEO_STREAMS_H_

#include <stdio.h>
#include <json-c/json.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "video_source2.h"
#include "ai-engine.h"
#include "app.h"


struct video_stream
{
	struct app_context *app;
	json_object *jstream;
	int id;
	int quit;
	
	long (*get_frame)(struct video_stream *stream, long prev_frame, input_frame_t *frame);
	int (*run)(struct video_stream *stream);
	int (*pause)(struct video_stream *stream);
	int (*stop)(struct video_stream *stream);

	struct video_source2 *video;
	int image_width;
	int image_height;
	
	int ai_enabled;
	int num_ai_engines;
	struct ai_context *ai_engines;
	
	pthread_rwlock_t rwlock;
	int paused;
	long frame_number;
	input_frame_t * frame_buffer[2];	// double buffer
	
	pthread_t th;
	struct {
		pthread_cond_t cond;
		pthread_mutex_t mutex;
	}cond_mutex;
};

struct video_stream *video_stream_init(struct video_stream *stream, json_object *jstream, struct app_context *app);
void video_stream_cleanup(struct video_stream *stream);

struct ai_engine * find_ai_engine_by_id(struct app_context *app, int id);
void input_frame_clear_all(input_frame_t *frame);

#ifdef __cplusplus
}
#endif
#endif
