#ifndef DEVICE_VIDEO_STREAM_H_
#define DEVICE_VIDEO_STREAM_H_

#include <stdio.h>
#include <pthread.h>
#include <json-c/json.h>
#include "video_source_common.h"


#ifdef __cplusplus
extern "C" {
#endif

struct channel_data;
struct device_stream
{
	void *priv;
	void *user_data; // struct app_context
	json_object *jstream;
	
	struct video_source_common *video;
	const char * server_url;
	long stream_id;
	
	int busy;
	int is_running;
	int (*on_update_frame)(struct device_stream *stream, struct video_frame *frame, json_object *jresult);
	
	// private data
	pthread_mutex_t frame_mutex;
	struct video_frame *frame;
	
	pthread_t th;
	struct {
		pthread_cond_t cond;
		pthread_mutex_t mutex;
	}cond_mutex;
	
	struct channel_data *channel;
};
struct device_stream * device_stream_new_from_config(json_object *jstream, void *user_data);
void device_stream_free(struct device_stream *stream);


struct video_frame * device_stream_addref_frame(struct device_stream *stream, struct video_frame *frame);
void device_stream_unref_frame(struct device_stream *stream, struct video_frame *frame);

#ifdef __cplusplus
}
#endif
#endif
