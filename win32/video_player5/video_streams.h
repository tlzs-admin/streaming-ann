#ifndef DEMO_VIDEO_STREAMS_H_
#define DEMO_VIDEO_STREAMS_H_

#include <stdio.h>
#include <json-c/json.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ai-engine.h"
#include "app.h"

#include "cv-wrapper.h"
#include "video_source_common.h"
#include "streaming-proxy.h"

#define MAX_ALERT_SERVERS_COUNT (64)
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

	struct streaming_proxy_context *proxy;
	const char *channel_name;
	struct channel_context *channel;
	
	int image_width;
	int image_height;
	
	int ai_enabled;
	int num_ai_engines;
	struct ai_context *ai_engines;
	
	pthread_rwlock_t rwlock;
	int paused;
	long frame_number;
	struct video_frame * frame_buffer[2];	// double buffer
	
	pthread_t th;
	struct {
		pthread_cond_t cond;
		pthread_mutex_t mutex;
	}cond_mutex;
	
	struct ai_engine *cv_face;
	int face_masking_flag;
	int detection_mode;
	
	ssize_t num_alert_servers;
	const char *alert_server_urls[MAX_ALERT_SERVERS_COUNT];
};

struct video_stream *video_stream_init(struct video_stream *stream, json_object *jstream, struct app_context *app);
void video_stream_cleanup(struct video_stream *stream);

struct ai_engine * find_ai_engine_by_id(struct app_context *app, int id);
void input_frame_clear_all(input_frame_t *frame);

#ifdef __cplusplus
}
#endif
#endif
