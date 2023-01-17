/*
 * reverse-proxy.c
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
#include <limits.h>

#include <search.h>

#include <json-c/json.h>
#include <pthread.h>
#include <libsoup/soup.h>
#include "video_source_common.h"
#include "utils.h"

#include "streaming-proxy.h"
#include "img_proc.h"

#include <time.h>

#if defined(WIN32) || defined(_WIN32)
typedef int (*__compar_fn_t)(const void *, const void*);
#endif

static inline int64_t get_time_ms(clockid_t clock_id)
{
	int64_t timestamp_ms = 0;
	struct timespec ts = { 0 };
	int rc = clock_gettime(clock_id, &ts);
	if(rc == -1) {
		perror("clock_gettime()");
		return -1;
	}
	timestamp_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	return timestamp_ms;
}

static struct video_frame * channel_get_frame(struct channel_context *channel)
{
	pthread_mutex_lock(&channel->mutex);
	struct video_frame *frame = channel->frame;
	if(frame) video_frame_addref(frame);
	pthread_mutex_unlock(&channel->mutex);
	return frame;
}

static struct video_frame * channel_get_output_frame(struct channel_context *channel)
{
	pthread_mutex_lock(&channel->mutex);
	struct video_frame *frame = channel->output_frame;
	if(frame) video_frame_addref(frame);
	pthread_mutex_unlock(&channel->mutex);
	return frame;
}

static long channel_set_output_frame(struct channel_context *channel, long frame_number, int width, int height, const unsigned char *jpeg_data, ssize_t cb_jpeg)
{
	if(frame_number <= 0) frame_number = 1;
	struct video_frame *frame = video_frame_new(frame_number, width, height, jpeg_data, cb_jpeg, 0);
	if(frame) {
		frame->type = video_frame_type_jpeg;
		pthread_mutex_lock(&channel->mutex);
		if(channel->output_frame) video_frame_unref(channel->output_frame);
		channel->output_frame = frame;
		pthread_mutex_unlock(&channel->mutex);
	}
	return frame_number;
}

static void channel_unref_frame(struct channel_context *channel, struct video_frame *frame)
{
	if(NULL == frame) return;
	pthread_mutex_lock(&channel->mutex);
	video_frame_unref(frame);
	pthread_mutex_unlock(&channel->mutex);
	return;
}

static long channel_update_frame(struct channel_context *channel, const void *jpeg_data, size_t length)
{
	long frame_number = channel->frame_number++;
	
	int width = -1, height = -1;
	int rc = img_utils_get_jpeg_size(jpeg_data, length, &width, &height);
	if(rc || width <= 0 || height <= 0) return -1;
	
	struct video_frame *frame = video_frame_new(frame_number, width, height, jpeg_data, length, 0);
	assert(frame);
	frame->type = video_frame_type_jpeg;
	
	if(frame->frame_number == 0) {
		channel->begin_ticks_ms = frame->ticks_ms;
		channel->begin_timestamp_ms = get_time_ms(CLOCK_REALTIME);
	}
	
	pthread_mutex_lock(&channel->mutex);
	struct video_frame *old_frame = channel->frame;
	channel->frame = frame;
	if(old_frame) video_frame_unref(old_frame);
	pthread_mutex_unlock(&channel->mutex);
	
	if(channel->on_new_frame) channel->on_new_frame(channel, frame, channel->user_data);
	return frame_number;
}


struct channel_context *channel_context_new(struct streaming_proxy_context *proxy, const char *name, int id, GHashTable *query)
{
	assert(name);
	struct channel_context *channel = calloc(1, sizeof(*channel));
	channel->proxy = proxy;
	strncpy(channel->name, name, sizeof(channel->name) - 1);
	channel->id = id;
	
	pthread_mutex_init(&channel->mutex, NULL);
	channel->get_frame = channel_get_frame;
	channel->update_frame = channel_update_frame;
	channel->unref_frame = channel_unref_frame;
	
	channel->get_output_frame = channel_get_output_frame;
	channel->set_output_frame = channel_set_output_frame;
	
	return channel;
}
void channel_context_free(struct channel_context *channel)
{
	
}

/******************************************************************************
 * streaming_proxy_context
******************************************************************************/

static json_object *generate_default_config()
{
	json_object *jconfig = json_object_new_object();
	assert(jconfig);
	
	json_object_object_add(jconfig, "port", json_object_new_int(8800));
	json_object_object_add(jconfig, "endpoint_base", json_object_new_string("/default"));
	
	json_object *jchannels = json_object_new_array();
	json_object_object_add(jconfig, "channels", jchannels);
	
	return jconfig;
}

static int streaming_proxy_load_config(struct streaming_proxy_context *proxy, json_object *jconfig)
{
	if(NULL == jconfig) jconfig = generate_default_config();
	proxy->jconfig = jconfig;
	
	const char *base_path = json_get_value(jconfig, string, base_path);
	if(NULL == base_path) base_path = "/default";
	
	unsigned int port = json_get_value(jconfig, int, port);
	if(0 == port || port > 65535) port = 8800;
	
	assert(base_path[0] == '/');
	proxy->cb_path = snprintf(proxy->base_path, sizeof(proxy->base_path) - 1, "%s", base_path);
	proxy->port = port;
	
	const char *config_path = json_get_value_default(jconfig, string, config_path, "/config");
	assert(config_path && config_path[0] == '/');
	proxy->config_path = config_path;
	
	return 0;
}

static void on_channel_get(SoupServer *http, SoupMessage *msg, const char *channel_name, GHashTable *query, SoupClientContext *client, struct streaming_proxy_context *proxy)
{
	struct channel_context *channel = proxy->find_channel_by_name(proxy, channel_name);
	if(NULL == channel) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}
	
	struct video_frame *frame = channel->get_output_frame(channel);
	if(NULL == frame || frame->frame_number < 0) {
		soup_message_set_status(msg, SOUP_STATUS_NO_CONTENT);
		channel->unref_frame(channel, frame);
		return;
	}
	assert(frame->type == video_frame_type_jpeg);
	
	SoupMessageHeaders *response_headers = msg->response_headers;
	soup_message_headers_append(response_headers, "Connection", "close");
	soup_message_headers_append(response_headers, "Cache-control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
	soup_message_headers_append(response_headers, "Pragma", "no-cache");
	soup_message_headers_append(response_headers, "Expires", "Mon, 3 Jan 2000 00:00:00 GMT");
	soup_message_headers_append(response_headers, "Access-Control-Allow-Origin", "*");
	
	char sz_value[100] = "";
	ssize_t cb = snprintf(sz_value, sizeof(sz_value) - 1, "%ld", frame->frame_number);
	assert(cb > 0);
	sz_value[cb] = '\0';
	soup_message_headers_append(response_headers, "X-Framenumber", sz_value);
	
	cb = snprintf(sz_value, sizeof(sz_value) - 1, "%ld", (long)(frame->ticks_ms - channel->begin_ticks_ms + channel->begin_timestamp_ms));
	assert(cb > 0);
	sz_value[cb] = '\0';
	soup_message_headers_append(response_headers, "X-Timestamp", sz_value);
	
	soup_message_headers_set_content_type(response_headers, "image/jpeg", NULL);
	soup_message_body_append(msg->response_body, SOUP_MEMORY_COPY, (const char*)frame->data, frame->length);
	
	soup_message_set_status(msg, SOUP_STATUS_OK);
	channel->unref_frame(channel, frame);
	return;
}

static void on_channel_post(SoupServer *http, SoupMessage *msg, const char *channel_name, GHashTable *query, SoupClientContext *client, struct streaming_proxy_context *proxy)
{
	debug_printf("%s(): channel=%s\n", __FUNCTION__, channel_name);
	struct channel_context *channel = proxy->find_or_register_channel(proxy, channel_name, query);
	if(NULL == channel) {
		char err_msg[200] = "";
		ssize_t cb_msg = snprintf(err_msg, sizeof(err_msg) - 1, "{ \"err_code\": -2, \"err_msg\": \"channel '%s' not found\" }\n", channel_name);
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, err_msg, cb_msg);
		soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
		return;
	}
	
	const char *content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
	if(NULL == content_type || strcasecmp(content_type, "image/jpeg") != 0) {
		char err_msg[200] = "";
		ssize_t cb_msg = snprintf(err_msg, sizeof(err_msg) - 1, "{ \"err_code\": -1, \"err_msg\": \"invalid content type\" }");
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, err_msg, cb_msg);
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	channel->update_frame(channel, msg->request_body->data, msg->request_body->length);
	soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
	return;
}

static void on_streaming_proxy_handler(SoupServer *http, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, struct streaming_proxy_context *proxy)
{
	static const char *default_channel = "channel0";
	if(strncmp(path, proxy->base_path, proxy->cb_path) != 0) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}
	
	const char *channel_name = path + proxy->cb_path;
	switch(channel_name[0])
	{
	case '\0':
		channel_name = default_channel;
		break;
	case '/':
		++channel_name;
		if(channel_name[0] == '\0') channel_name = default_channel;
		break;
	default:
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	if(msg->method == SOUP_METHOD_GET){
		on_channel_get(http, msg, channel_name, query, client, proxy);
		return;
	}
	if(msg->method == SOUP_METHOD_POST || msg->method == SOUP_METHOD_PUT) {
		on_channel_post(http, msg, channel_name, query, client, proxy);
		return;
	}
	
	if(msg->method == SOUP_METHOD_HEAD) {
		///< @todo
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	
	if(msg->method == SOUP_METHOD_OPTIONS) {
		///< @todo
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	
	if(msg->method == SOUP_METHOD_DELETE) {
		// remove channel
		///< @todo
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	
	soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
	return;
}

static struct channel_context * find_channel_by_name(struct streaming_proxy_context *proxy, const char *name)
{
	debug_printf("%s(name=%s)\n", __FUNCTION__, name);
	pthread_mutex_lock(&proxy->mutex);
	void *p_node = tfind(name, &proxy->channels_search_root, (__compar_fn_t)strcmp);
	if(NULL == p_node) {
		pthread_mutex_unlock(&proxy->mutex);
		return NULL;
	}
	
	struct channel_context *channel = *(void **)p_node;
	pthread_mutex_unlock(&proxy->mutex);
	return channel;
}

static struct channel_context * find_or_register_channel(struct streaming_proxy_context *proxy, const char *name, GHashTable *query)
{
	debug_printf("%s(name=%s)\n", __FUNCTION__, name);
	pthread_mutex_lock(&proxy->mutex);
	
	struct channel_context *channel = NULL;
	
	void *p_node = tfind(name, &proxy->channels_search_root, (__compar_fn_t)strcmp);
	if(NULL == p_node) {
		channel = channel_context_new(proxy, name, ++proxy->last_channel_id, query);
		p_node = tsearch(channel, &proxy->channels_search_root, (__compar_fn_t)strcmp);
		assert(p_node);
	}
	
	channel = *(void **)p_node;
	pthread_mutex_unlock(&proxy->mutex);
	return channel;
}

static void on_web_viewer(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	debug_printf("%s(): method=%s, path=%s\n", __FUNCTION__, msg->method, path);
	struct streaming_proxy_context *web = user_data;
	assert(web);
	
	if(msg->method != SOUP_METHOD_GET || NULL == path || path[0] != '/' || NULL == web->viewer_html) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	const char *viewer_path = web->viewer_path;
	if(NULL == viewer_path) viewer_path = "/";
	
	if(strcmp(path, viewer_path) == 0) {
		char *html = NULL;
		ssize_t cb_html = load_binary_data(web->viewer_html, (unsigned char **)&html);
		
		if(cb_html > 0) {
			SoupMessageHeaders *response_headers = msg->response_headers;
			soup_message_headers_append(response_headers, "Connection", "close");
			soup_message_headers_append(response_headers, "Cache-Control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
			soup_message_headers_append(response_headers, "Pragma", "no-cache");
			soup_message_set_response(msg, "text/html", SOUP_MEMORY_TAKE, html, cb_html);
			soup_message_set_status(msg, SOUP_STATUS_OK);
			return;
		}
		if(html) free(html);
	}
	
	soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
	return;
}

static void on_config_handler(SoupServer *http, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct streaming_proxy_context *proxy = user_data;
	assert(proxy && proxy->http == http );
	
	guint status = SOUP_STATUS_BAD_REQUEST;
	if(proxy->on_config) status = proxy->on_config(proxy, msg, path, query, client, proxy->config_ctx);
	
	soup_message_set_status(msg, status);
	return;
}

struct streaming_proxy_context *streaming_proxy_context_init(struct streaming_proxy_context *proxy, json_object *jconfig, void *user_data)
{
	if(NULL == proxy) proxy = calloc(1, sizeof(*proxy));
	assert(proxy);
	
	proxy->user_data = user_data;
	proxy->find_channel_by_name = find_channel_by_name;
	proxy->find_or_register_channel = find_or_register_channel;
	
	proxy->last_channel_id = -1;	
	int rc = streaming_proxy_load_config(proxy, jconfig);
	assert(0 == rc);
	
	rc = pthread_mutex_init(&proxy->mutex, NULL);
	assert(0 == rc);
	
	SoupServer *http = soup_server_new(SOUP_SERVER_SERVER_HEADER, "streaming-proxy", NULL);
	assert(http);
	proxy->http = http;
	
	assert(proxy->base_path[0] == '/');
	soup_server_add_handler(http, proxy->base_path, (SoupServerCallback)on_streaming_proxy_handler, proxy, NULL);
	
	proxy->viewer_path = json_get_value_default(jconfig, string, viewer_path, "viewer");
	proxy->viewer_html = json_get_value(jconfig, string, viewer_html);
	if(proxy->viewer_path) {
		soup_server_add_handler(http, proxy->viewer_path, on_web_viewer, proxy, NULL);
	}
	
	if(proxy->config_path) {
		soup_server_add_early_handler(http, proxy->config_path, (SoupServerCallback)on_config_handler, proxy, NULL);
	}
	
	return proxy;
}
void streaming_proxy_context_cleanup(struct streaming_proxy_context *proxy)
{
	return;
}

int streaming_proxy_run(struct streaming_proxy_context *proxy, int extern_loop)
{
	assert(proxy && proxy->http);
	SoupServer *http = proxy->http;
	
	GError *gerr = NULL;
	gboolean ok = soup_server_listen_all(http, proxy->port, 0, &gerr);
	if(gerr) {
		fprintf(stderr, "\e[31m" "ERROR: %s() failed: %s." "\e[39m", __FUNCTION__, gerr->message);
		g_error_free(gerr);
		gerr = NULL;
	}
	assert(ok);
	
	proxy->http = http;
	GSList *uris = soup_server_get_uris(http);
	for(GSList *uri = uris; NULL != uri; uri = uri->next)
	{
		char *sz_uri = soup_uri_to_string(uri->data, FALSE);
		fprintf(stderr, "listening on %s\n", sz_uri);
		g_free(sz_uri);
		soup_uri_free(uri->data);
	}
	fprintf(stderr, "viewer path: %s\n", proxy->viewer_path);
	g_slist_free(uris);
	
	if(!extern_loop) {
		GMainLoop *loop = g_main_loop_new(NULL, FALSE);
		g_main_loop_run(loop);
		g_main_loop_unref(loop);
	}
	
	return 0;
}

#if defined(TEST_STREAMING_PROXY_) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	struct streaming_proxy_context *proxy = streaming_proxy_context_init(NULL, NULL, NULL);
	assert(proxy);
	
	streaming_proxy_run(proxy, 0);
	
	streaming_proxy_context_cleanup(proxy);
	return 0;
}
#endif
