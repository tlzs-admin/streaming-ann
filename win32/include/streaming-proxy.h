#ifndef STREAMING_PROXY_H_
#define STREAMING_PROXY_H_

#include <stdio.h>
#include <json-c/json.h>
#include <pthread.h>
#include <libsoup/soup.h>
#include "video_source_common.h"

#ifdef __cplusplus
extern "C" {
#endif


struct streaming_proxy_context;
struct channel_context
{
	char name[100];
	struct streaming_proxy_context *proxy;
	int id;
	
	pthread_t th;
	pthread_mutex_t mutex;
	
	struct framerate_fraction framerate;
	struct video_frame *frame;
	long frame_number;
	int64_t begin_ticks_ms;
	int64_t begin_timestamp_ms;
	
	// public methods
	struct video_frame * (*get_frame)(struct channel_context *channel);
	void (*unref_frame)(struct channel_context *channel, struct video_frame *frame);
	long (*update_frame)(struct channel_context *channel, const void *jpeg_data, size_t length);
	
	// virtual callbacks
	void *user_data;
	int (*on_new_frame)(struct channel_context *channel, struct video_frame *frame, void *user_data);
	
	struct video_frame *output_frame;
	struct video_frame *(*get_output_frame)(struct channel_context *channel);
	long (*set_output_frame)(struct channel_context *channel, long frame_number, int width, int height, const unsigned char *jpeg_data, ssize_t cb_jpeg);
};

struct channel_context *channel_context_new(struct streaming_proxy_context *proxy, const char *name, int id, GHashTable *query);
void channel_context_free(struct channel_context *channel);


#define STREAMING_PROXY_MAX_CHANNELS (256)
struct streaming_proxy_private;
struct streaming_proxy_context
{
	void *user_data;
	struct streaming_proxy_private *priv;
	
	const char *viewer_path;
	const char *viewer_html;
	char *html;
	ssize_t cb_html;
	
	SoupServer *http;
	json_object *jconfig;
	GMainLoop *loop;
	unsigned int port;
	char base_path[PATH_MAX];
	size_t cb_path;
	
	size_t num_engines;
	struct ai_engine_t **engines;
	int auto_channels;
	
	pthread_mutex_t mutex;
	int last_channel_id;
	size_t max_channels;
	size_t num_channels;
	struct channel_context **channels;
	void *channels_search_root;
	
	struct channel_context * (*find_channel_by_name)(struct streaming_proxy_context *proxy, const char *name);
	struct channel_context * (*find_or_register_channel)(struct streaming_proxy_context *proxy, const char *name, GHashTable *querystring);
	
	
	/* 
	 * doc/streaming-proxy.md 
	*/
	const char *config_path;
	void *config_ctx;
	guint (*on_config)(struct streaming_proxy_context *proxy, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, void *config_ctx);
};

struct streaming_proxy_context *streaming_proxy_context_init(struct streaming_proxy_context *proxy, json_object *jconfig, void *user_data);
int streaming_proxy_run(struct streaming_proxy_context *proxy, int extern_loop);
void streaming_proxy_context_cleanup(struct streaming_proxy_context *proxy);



#ifdef __cplusplus
}
#endif
#endif
