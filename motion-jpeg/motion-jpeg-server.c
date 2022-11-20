/*
 * motion-jpeg-server.c
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
#include <stdint.h>
#include <time.h>

#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>

#include <gst/gst.h>

#include "http-server.h"
#include "motion-jpeg.h"

#include <search.h>

#include "video_source_common.h"
#include "utils.h"

struct motion_jpeg_channel_private
{
	struct motion_jpeg_channel *channel;
	pthread_mutex_t mutex;
	long frame_number;
	int64_t begin_timestamp_ms;
	int64_t begin_ticks_ms;
	struct video_frame *frame;
	struct video_source_common *input;

#define channel_priv_lock(priv) pthread_mutex_lock(&priv->mutex)
#define channel_priv_unlock(priv) pthread_mutex_unlock(&priv->mutex)
};

void motion_jpeg_channel_lock(struct motion_jpeg_channel *channel)
{
	channel_priv_lock(channel->priv);
}
void motion_jpeg_channel_unlock(struct motion_jpeg_channel *channel)
{
	channel_priv_unlock(channel->priv);
}

static void motion_jpeg_channel_private_free(struct motion_jpeg_channel_private *priv)
{
	if(NULL == priv) return;
	channel_priv_lock(priv);
	struct video_frame *frame = priv->frame;
	priv->frame = NULL;
	if(frame) video_frame_unref(frame);
	channel_priv_unlock(priv);
	pthread_mutex_destroy(&priv->mutex);
	return;
}

static struct motion_jpeg_channel_private *motion_jpeg_channel_private_new(struct motion_jpeg_channel *channel)
{
	struct motion_jpeg_channel_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->channel = channel;
	
	int rc = pthread_mutex_init(&priv->mutex, NULL);
	assert(0 == rc);
	return priv;
}

#define channel_lock(channel) pthread_mutex_lock(&channel->priv->mutex)
#define channel_unlock(channel) pthread_mutex_unlock(&channel->priv->mutex)

static long channel_update_frame(struct motion_jpeg_channel *channel, struct video_frame *frame)
{
	assert(channel && channel->priv);
	struct motion_jpeg_channel_private *priv = channel->priv;
	long frame_number = -1;
	channel_lock(channel);
	if(NULL == frame || NULL == frame->data) {
		channel_unlock(channel);
		return -1;
	}
	struct video_frame *old_frame = priv->frame;
	if(frame != old_frame) {
		video_frame_addref(frame);
		priv->frame = frame;
		if(old_frame) video_frame_unref(old_frame);
	}
	frame_number = frame->frame_number;
	channel->frame_number = frame_number;
	
	channel_unlock(channel);
	return frame_number;
}

static struct video_frame *channel_get_frame(struct motion_jpeg_channel *channel)
{
	struct motion_jpeg_channel_private *priv = channel->priv;
	channel_lock(channel);
	struct video_frame *frame = priv->frame;
	if(frame) video_frame_addref(frame);
	channel_unlock(channel);
	return frame;
}
static void channel_unref_frame(struct motion_jpeg_channel *channel, struct video_frame *frame)
{
	channel_lock(channel);
	if(frame) video_frame_unref(frame);
	channel_unlock(channel);
	return;
}

struct motion_jpeg_channel * motion_jpeg_channel_new(const char *channel_name, long channel_id, void *user_data)
{
	struct motion_jpeg_channel *channel = calloc(1, sizeof(*channel));
	assert(channel);
	channel->user_data = user_data;
	
	struct motion_jpeg_channel_private *priv = motion_jpeg_channel_private_new(channel);
	assert(priv);
	channel->priv = priv;
	
	assert(channel_name);
	strncpy(channel->name, channel_name, sizeof(channel->name) - 1);
	channel->channel_id = channel_id;
	channel->user_data = user_data;
	
	channel->update_frame = channel_update_frame;
	channel->get_frame = channel_get_frame;
	channel->unref_frame = channel_unref_frame;
	
	return channel;
}
void motion_jpeg_channel_free(struct motion_jpeg_channel *channel)
{
	if(NULL == channel) return;
	motion_jpeg_channel_private_free(channel->priv);
	channel->priv = NULL;
	free(channel);
}

static int motion_jpeg_server_load_config(struct motion_jpeg_server *mjpeg, json_object *jconfig);
static struct motion_jpeg_channel *motion_jpeg_server_add_channel(struct motion_jpeg_server *server, const char *channel_name);
static struct motion_jpeg_channel *motion_jpeg_server_find_channel(struct motion_jpeg_server *server, const char *channel_name);
static struct motion_jpeg_channel *motion_jpeg_server_find_channel_by_path(struct motion_jpeg_server *mjpeg, const char *path, char name[static 100]);

#define MOTION_JPEG_RESPONSE_HEADER "HTTP/1.1 200 OK\r\n" \
	"Connection: close\r\n" \
	"Server: motion-jpeg-server/0.1.0-alpha\r\n" \
	"Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n" \
	"Pragma: no-cache\r\n" \
	"Expires: Mon, 3 Jan 2000 12:34:56 GMT\r\n" \
	"Access-Control-Allow-Origin: *\r\n" \
	"Content-Type: multipart/x-mixed-replace; boundary=" MOTION_JPEG_BOUNDARY "\r\n" \
	"\r\n" \
	"--" MOTION_JPEG_BOUNDARY "\r\n"

static int on_client_http_request(struct http_client_context *client, void *user_data)
{
	const char *method = client->method;
	const char *path = client->path;
	debug_printf("method: %s, path: %s\n", method, path);
	
	if(strcasecmp(method, "GET") != 0) {
		// invaid request
		client->stage = http_stage_final;
		write(client->fd, "HTTP/1.1 405 Method Not Allowed\r\n\r\n",
			(sizeof "HTTP/1.1 405 Method Not Allowed\r\n\r\n") - 1
		);
		return -1;
	}
	
	struct motion_jpeg_server *mjpeg = (struct motion_jpeg_server *)client->server;
	client->stage = http_stage_send_response;
	client->is_multipart = 1;
	client->timeout = 30 * 1000; // 30 seconds
	
	// send response
	struct http_buffer *out_buf = client->out_buf;
	http_buffer_push_data(out_buf, MOTION_JPEG_RESPONSE_HEADER, sizeof(MOTION_JPEG_RESPONSE_HEADER) - 1);
	http_server_set_client_writable(mjpeg->http, client, 1);
	
	struct motion_jpeg_client * mjpeg_client = motion_jpeg_client_new(client);
	assert(mjpeg_client);
	mjpeg_client->channel = mjpeg->find_channel_by_path(mjpeg, client->path, mjpeg_client->channel_name);
	
	debug_printf("channel: %s\n", mjpeg_client->channel?mjpeg_client->channel->name: "null");
	return 0;
}

static int motion_jpeg_server_on_accepted(struct http_server_context *http, struct http_client_context *client, void *event)
{
	client->on_http_request = on_client_http_request;
	return 0;
}



#define MOTION_JPEG_MAX_CHANNELS (256)
struct motion_jpeg_server * motion_jpeg_server_init(struct motion_jpeg_server *mjpeg, void *user_data)
{
	if(NULL == mjpeg) mjpeg = calloc(1, sizeof(*mjpeg));
	assert(mjpeg);
	
	mjpeg->user_data = user_data;
	struct http_server_context *http = http_server_context_init(mjpeg->http, mjpeg);
	assert(http);
	
	mjpeg->load_config = motion_jpeg_server_load_config;
	mjpeg->add_channel = motion_jpeg_server_add_channel;
	mjpeg->find_channel = motion_jpeg_server_find_channel;
	mjpeg->find_channel_by_path = motion_jpeg_server_find_channel_by_path;
	
	int rc = pthread_mutex_init(&mjpeg->mutex, NULL);
	assert(0 == rc);
	
	struct motion_jpeg_channel **channels = calloc(MOTION_JPEG_MAX_CHANNELS, sizeof(*channels));
	assert(channels);
	mjpeg->channels = channels;
	mjpeg->max_channels = MOTION_JPEG_MAX_CHANNELS;
	
	http->on_accepted = motion_jpeg_server_on_accepted;
	return mjpeg;
}
void motion_jpeg_server_cleanup(struct motion_jpeg_server *mjpeg)
{
	if(NULL == mjpeg) return;
	if(mjpeg->channels) {
		for(size_t i = 0; i < mjpeg->num_channels; ++i) {
			struct motion_jpeg_channel *channel = mjpeg->channels[i];
			if(channel) {
				mjpeg->channels[i] = NULL;
				motion_jpeg_channel_free(channel);
			}
		}
		free(mjpeg->channels);
		mjpeg->channels = NULL;
	}
	
	http_server_context_cleanup(mjpeg->http);
	pthread_mutex_destroy(&mjpeg->mutex);
	memset(mjpeg, 0, sizeof(*mjpeg));
	return;
}


#define server_lock(server) pthread_mutex_lock(&server->mutex)
#define server_unlock(server) pthread_mutex_unlock(&server->mutex)

static struct motion_jpeg_channel *motion_jpeg_server_add_channel(struct motion_jpeg_server *server, const char *channel_name)
{
	struct motion_jpeg_channel *channel = NULL;
	assert(server);
	
	server_lock(server);
	long channel_id = server->num_channels;
	void * p_node = tfind(channel_name, &server->channels_search_root, (__compar_fn_t)strcmp);
	if(p_node) channel = *(void **)p_node;
	else {
		channel = motion_jpeg_channel_new(channel_name, channel_id, server);
		assert(channel);
		p_node = tsearch(channel, &server->channels_search_root, (__compar_fn_t)strcmp);
		assert(p_node);
	}
	
	server->channels[server->num_channels++] = channel;
	server_unlock(server);
	return channel;
}
static struct motion_jpeg_channel *motion_jpeg_server_find_channel(struct motion_jpeg_server *server, const char *channel_name)
{
	if(server->num_channels <= 0) return NULL;
	if(NULL == channel_name || !channel_name[0]) return server->channels[0];
	
	void * p_node = tfind(channel_name, &server->channels_search_root, (__compar_fn_t)strcmp);
	if(NULL == p_node) return NULL;
	
	return *(void **)p_node;
}

static struct motion_jpeg_channel *motion_jpeg_server_find_channel_by_path(struct motion_jpeg_server *mjpeg, const char *path, char name[static 100])
{
	if(mjpeg->num_channels <= 0) return NULL;
	
	struct motion_jpeg_channel *default_channel = mjpeg->channels[0];
	
	if(NULL == path) return default_channel;
	char *p = strchr(path, '?');
	if(NULL == p) return default_channel;
	++p;
	
	struct motion_jpeg_channel *channel = NULL;
	char *channel_name = NULL;
	char query_string[1024] = "";
	strncpy(query_string, p, sizeof(query_string) - 1);

	char *token = NULL;
	char *kv = strtok_r(query_string, "&", &token);
	while(kv) {
		char *value = NULL;
		char *key = strtok_r(kv, "=", &value);
		
		if(key && strcasecmp(key, "channel") == 0) {
			channel_name = value;
			break;
		}
		
		kv = strtok_r(NULL, "&", &token);
	}
	if(channel_name) {
		channel = mjpeg->find_channel(mjpeg, channel_name);
	}
	
	if(name) {
		strncpy(name, channel_name, sizeof(channel->name) - 1);
	}
	return channel;
}

static int motion_jpeg_server_load_config(struct motion_jpeg_server *mjpeg, json_object *jconfig)
{
	return 0;
}


static json_object *generate_default_config(void)
{
	json_object *jconfig = json_object_new_object();
	
	///< @todo
	return jconfig;
}

static int update_channel(struct video_source_common *video, const struct video_frame *frame, void *user_data)
{
	struct motion_jpeg_channel *channel = user_data;
	assert(channel);
	
	long frame_number = channel->update_frame(channel, (struct video_frame *)frame);
	printf("channel: %s, frame_number = %ld\n", channel->name, frame_number);
	
	return 0;
}

int main(int argc, char **argv)
{
	gst_init(&argc, &argv);
	struct motion_jpeg_server *mjpeg = motion_jpeg_server_init(NULL, NULL);
	assert(mjpeg);
	struct http_server_context *http = mjpeg->http;
	
	json_object *jconfig = json_object_from_file("conf/motion-jpeg-server.json");
	if(NULL == jconfig) jconfig = generate_default_config();
	
	// mjpeg->load_config(mjpeg, jconfig);
	// add a test channel
	
	int rc = 0;
	struct video_source_common *video = video_source_common_init(NULL, video_frame_type_jpeg, mjpeg);
	assert(video);
	rc = video->init(video, "/dev/video0", 1280, 720, NULL);
	assert(0 == rc);
	
	struct motion_jpeg_channel *channel = mjpeg->add_channel(mjpeg, "default");
	assert(channel);
	
	video->user_data = channel;
	video->on_new_frame = update_channel;
	
	rc = http->listen(http, NULL, "8800", 0);
	assert(0 == rc);
	
	
	rc = video->play(video);
	rc = http->run(http, 1);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	
	return rc;
}
