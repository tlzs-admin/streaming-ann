#ifndef MOTION_JPEG_SERVER_H_
#define MOTION_JPEG_SERVER_H_

#include <stdio.h>
#include <libsoup/soup.h>
#include <json-c/json.h>

#include <pthread.h>
#include "http-server.h"
#include "video_source_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_JPEG_BOUNDARY "1e4fabf93abf8eebc8e01d2e9433e5d5"

struct motion_jpeg_channel;
struct motion_jpeg_client
{
	struct http_client_context *client;
	long channel_id;
	char channel_name[100];
	struct motion_jpeg_channel *channel;
	pthread_t th;
	struct {
		pthread_cond_t cond;
		pthread_mutex_t mutex;
	}cond_mutex;
	long frame_number;
	int quit;
};
struct motion_jpeg_client *motion_jpeg_client_new(struct http_client_context *client);
void motion_jpeg_client_free(struct motion_jpeg_client *mjpeg_client);

struct motion_jpeg_channel_private;
struct motion_jpeg_channel
{
	char name[100];
	long channel_id;
	char boundary[40];
	
	double duration;
	long frame_number;
	
	struct motion_jpeg_channel_private *priv;
	void *user_data;

	long (*update_frame)(struct motion_jpeg_channel *channel, struct video_frame *frame);
	long (*update)(struct motion_jpeg_channel *channel, struct video_frame *frame);
	struct video_frame *(*get_frame)(struct motion_jpeg_channel *channel);
	void (*unref_frame)(struct motion_jpeg_channel *channel, struct video_frame *frame);
	
	double (*query_fps)(struct motion_jpeg_channel *channel);
	
	size_t max_clients;
	size_t num_clients;
	struct http_client_context **clients;
	
	int (*add_client)(struct motion_jpeg_channel *channel, struct motion_jpeg_client *client);
	int (*remove_client)(struct motion_jpeg_channel *channel, struct motion_jpeg_client *client);
};
struct motion_jpeg_channel * motion_jpeg_channel_new(const char *channel_name, long channel_id, void *user_data);
void motion_jpeg_channel_free(struct motion_jpeg_channel *channel);
void motion_jpeg_channel_lock(struct motion_jpeg_channel *channel);
void motion_jpeg_channel_unlock(struct motion_jpeg_channel *channel);

struct motion_jpeg_server
{
	struct http_server_context http[1]; // base class
	void *user_data;
	int quit;
	json_object *jconfig;
	
	// private data
	pthread_mutex_t mutex;
	size_t max_channels;
	size_t num_channels;
	char *root_path;
	struct motion_jpeg_channel **channels;
	void *channels_search_root;
	
	int (*load_config)(struct motion_jpeg_server *mjpeg, json_object *jconfig);
	struct motion_jpeg_channel *(*add_channel)(struct motion_jpeg_server *mjpeg, const char *channel_name);
	struct motion_jpeg_channel *(*find_channel)(struct motion_jpeg_server *mjpeg, const char *channel_name);
	struct motion_jpeg_channel *(*find_channel_by_path)(struct motion_jpeg_server *mjpeg, const char *path, char channel_name[static 100]);
};
struct motion_jpeg_server * motion_jpeg_server_init(struct motion_jpeg_server *mjpeg, void *user_data);
void motion_jpeg_server_cleanup(struct motion_jpeg_server *mjpeg);

#ifdef __cplusplus
}
#endif
#endif
