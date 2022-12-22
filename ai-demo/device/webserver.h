#ifndef DEVICE_WEBSERVER_H_
#define DEVICE_WEBSERVER_H_

#include <stdio.h>
#include <pthread.h>
#include <json-c/json.h>
#include <libsoup/soup.h>
#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

struct device_stream;
struct webserver_context;
struct channel_data
{
	long id;
	char name[100];
	
	struct webserver_context *web;
	struct device_stream *stream;
	
	pthread_mutex_t mutex;
	long frame_number;
	struct video_frame *frame;
	
	int (*update_frame)(struct channel_data *channel, int width, int height, const unsigned char *jpeg_data, size_t cb_jpeg, json_object *jresult);
	long (*get_frame)(struct channel_data *channel, struct video_frame **p_frame);
	
};

struct channel_data *channel_data_init(struct channel_data *channel, long id, const char *name, struct device_stream *stream);
void channel_data_cleanup(struct channel_data *channel);

struct webserver_context
{
	struct app_context *app;
	void *priv;
	json_object *jconfig;
	
	pthread_mutex_t mutex;
	int num_channels;
	struct channel_data *channels;
	SoupServer *server;
	
	ssize_t num_streams;
	struct device_stream *streams;
	
	char *html;
	ssize_t cb_html;
	
	int (*init)(struct webserver_context *web, ssize_t num_streams, struct device_stream **streams, json_object *jconfig);
	int (*run)(struct webserver_context *web);
};

struct webserver_context *webserver_context_init(struct webserver_context *web, struct app_context *app);
void webserver_context_cleanup(struct webserver_context *web);

GMainLoop *app_get_main_loop(struct app_context *app);

#ifdef __cplusplus
}
#endif
#endif
