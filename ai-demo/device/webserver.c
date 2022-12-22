/*
 * webserver.c
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

#include <pthread.h>
#include <json-c/json.h>

#include "app.h"
#include "webserver.h"
#include "video_stream.h"
#include "video_source_common.h"
#include "utils.h"

static json_object *generate_default_config(void)
{
	json_object *jwebserver = json_object_new_object();
	json_object_object_add(jwebserver, "port", json_object_new_int(8080));
	json_object_object_add(jwebserver, "enabled", json_object_new_int(1));
	json_object_object_add(jwebserver, "use_ssl", json_object_new_int(0));
	return jwebserver;
}
static void on_favicon(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	///< @todo generate or send a icon
	soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
}

static void on_document_channels(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
//	debug_printf("%s(): method=%s, path=%s\n", __FUNCTION__, msg->method, path);
	if(msg->method != SOUP_METHOD_GET || NULL == path || path[0] != '/') {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	struct webserver_context *web = user_data;
	assert(web);
	
	struct channel_data *channels = web->channels;
	if(NULL == channels || web->num_channels <= 0) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}
	
	struct channel_data *channel = NULL;
	++path;
	
	for(ssize_t i = 0; i < web->num_channels; ++i) {
		channel = &channels[i];
		if(strcmp(path, channel->name) == 0) break;
		channel = NULL;
	}
	
	struct video_frame *frame = NULL;
	long frame_number = 0;
	if(channel) {
		frame_number = channel->get_frame(channel, &frame);
		UNUSED(frame_number);
	//	debug_printf("channel %d -- frame_%.8ld, frame=%p\n", (int)channel->id, frame_number, frame);
	}
	
	if(frame) {
		SoupMessageHeaders *response_headers = msg->response_headers;
		
		soup_message_headers_append(response_headers, "Connection", "close");
		soup_message_headers_append(response_headers, "Cache-Control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
		soup_message_headers_append(response_headers, "Pragma", "no-cache");
		soup_message_headers_append(response_headers, "Expires", "Mon, 3 Jan 2000 00:00:00 GMT");
		soup_message_headers_append(response_headers, "Access-Control-Allow-Origin", "*");
		soup_message_headers_append(response_headers, "Content-Type", "image/jpeg");
		
		soup_message_body_append(msg->response_body, SOUP_MEMORY_COPY, (char *)frame->data, frame->length);
		video_frame_unref(frame);
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	
	soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
}



static int generate_default_page(struct webserver_context *web)
{
	static const size_t max_size = 16384;
	char *html = calloc(max_size, 1);
	char *p = html;
	char *p_end = html + max_size;
	ssize_t cb = 0;
	
	cb = snprintf(p, p_end - p, 
	"<!DOCTYPE html><html lang=\"en\">\n"
		"<head>\n"
		"<meta charset=\"utf-8\" />\n"
		"<title>app-demo</title>\n"
		"<style> \n"
		"#viewer { max-width: 100%%; heigth: auto; width: auto; }</style>\n"
		"<script>\n"
		"window.onload = function() {\n"
		"	// bind events\n"
		"	const channels = document.getElementById(\"channels\");\n"
		"	channels.onchange = on_channels_selection_changed; \n"
		"	channels[1].selected = 'selected';\n"
		"	update_frame(); \n"
		"}\n"
		
		"function on_channels_selection_changed() { \n"
		"	let channel = document.getElementById(\"channels\").value;\n"
		"	if(channel) setTimeout(update_frame, 200);\n"
		"}\n"
	
		"function update_frame() {\n"
		"	const img = document.getElementById(\"viewer\");"
		"	let channel = channels.value;\n"
		"	if(channel) {\n"
		"		img.src = window.location.origin + '/' + channel;\n"
		"		setTimeout(update_frame, 200);\n"
		"	}else {\n"
		"		img.src = 'data:'; \n"
		"	}\n"
		"}\n"
		"</script></head>\n"
		"<body>\n"
		"<div><select id=\"channels\" class=\"channels\">\n"
		"<option value=\"\">Select Channel</option>\n");
	assert(cb > 0);
	p += cb;
	
	struct channel_data *channels = web->channels;
	for(ssize_t i = 0; i < web->num_channels; ++i) 
	{
		struct channel_data *channel = &channels[i];
		cb = snprintf(p, p_end - p, "<option value=\"%s\">Channel %ld</option>\n", channel->name, (long)channel->id);
		assert(cb > 0);
		p += cb;
	}
	
	cb = snprintf(p, p_end - p, "</select></div>\n"
		"<div style='width: 100%%; height: 100%%'><img id=\"viewer\" style='width: 100%%; height: 100%%'/></div>\n"
		"</body></html>\n");
	assert(cb > 0);
	p += cb;
	
	web->cb_html = p - html;
	if(web->html) free(web->html);
	web->html = html;

	return 0;
}


static void on_document_root(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	debug_printf("%s(): method=%s, path=%s\n", __FUNCTION__, msg->method, path);
	struct webserver_context *web = user_data;
	assert(web);
	
	if(msg->method != SOUP_METHOD_GET || NULL == path || path[0] != '/' || NULL == web->html) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	if(strcmp(path, "/") == 0 || strcmp(path, "/display") == 0) {
		SoupMessageHeaders *response_headers = msg->response_headers;
		soup_message_headers_append(response_headers, "Connection", "close");
		soup_message_headers_append(response_headers, "Cache-Control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
		soup_message_headers_append(response_headers, "Pragma", "no-cache");
		soup_message_headers_append(response_headers, "Expires", "Mon, 3 Jan 2000 00:00:00 GMT");
		soup_message_set_response(msg, "text/html", SOUP_MEMORY_TEMPORARY, web->html, web->cb_html);
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	
	soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
	return;

}
static int webserver_init(struct webserver_context *web, ssize_t num_streams, struct device_stream **streams, json_object *jwebserver)
{
	assert(web);
	assert(num_streams > 0 && streams);
	
	if(NULL == jwebserver) jwebserver = web->jconfig;
	if(NULL == jwebserver) {
		jwebserver = generate_default_config();
		assert(jwebserver);
		web->jconfig = jwebserver;
	}
	
	int port = json_get_value_default(jwebserver, int, port, 8080);
	assert(port > 0 && port < 65535);
	int enabled = json_get_value(jwebserver, int, enabled);
	if(!enabled) return 1;
	
	int use_ssl = json_get_value(jwebserver, int, use_ssl);
	
	GError *gerr = NULL;
	gboolean ok = FALSE;
	SoupServer *server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "device_web", NULL);
	assert(server);
	if(use_ssl) {
		const char *cert_file = json_get_value(jwebserver, string, cert_file);
		const char *key_file = json_get_value(jwebserver, string, key_file);
		assert(cert_file && key_file);
		
		ok = soup_server_set_ssl_cert_file(server, cert_file, key_file, &gerr);
		if(gerr) {
			fprintf(stderr, "%s(%d)::soup_server_set_ssl_cert_file(%s, %s) failed: %s\n", 
				__FILE__, __LINE__, cert_file, key_file, gerr->message);
			g_error_free(gerr);
			gerr = NULL;
		}
		assert(ok);
	}
	
	struct channel_data *channels = calloc(num_streams, sizeof(*channels));
	assert(channels);
	
	for(ssize_t i = 0; i < num_streams; ++i) {
		struct channel_data *channel = channel_data_init(&channels[i], i + 1, NULL, streams[i]);
		assert(channel);
		
		char path[200] = "";
		snprintf(path, sizeof(path), "/%s", channel->name);
		soup_server_add_handler(server, path, on_document_channels, web, NULL);
	}
	web->num_channels = num_streams;
	web->channels = channels;
	
	soup_server_add_handler(server, "/favicon", on_favicon, web, NULL);
	soup_server_add_handler(server, "/", on_document_root, web, NULL);
	
	
	ok = soup_server_listen_all(server, port, use_ssl?SOUP_SERVER_LISTEN_HTTPS:0, &gerr);
	if(!ok) {
		fprintf(stderr, "%s(%d)::soup_server_listen_all() failed: %s\n", 
			__FILE__, __LINE__, gerr->message);
		g_error_free(gerr);
		gerr = NULL;
		return -1;
	}
	
	GSList *uris = soup_server_get_uris(server);
	for(GSList *item = uris; NULL != item; item = item->next)
	{
		char *uri = soup_uri_to_string(item->data, FALSE);
		fprintf(stderr, "listening on: %s\n", uri);
		g_free(uri);
		soup_uri_free(item->data);
	}
	g_slist_free(uris);
	
	
	generate_default_page(web);
	
	return 0;
}


static int webserver_run(struct webserver_context *web)
{
	GMainLoop *loop = app_get_main_loop(web->app);
	if(loop) return 0;
	
	
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	return 0;
}

struct webserver_context *webserver_context_init(struct webserver_context *web, struct app_context *app)
{
	int rc = 0;
	if(NULL == web) web = calloc(1, sizeof(*web));
	assert(web);
	
	rc = pthread_mutex_init(&web->mutex, NULL);
	assert(0 == rc);
	
	web->init = webserver_init;
	web->run = webserver_run;

	return web;
}
void webserver_context_cleanup(struct webserver_context *web)
{
	return;
}

static int channel_update_frame(struct channel_data *channel, int width, int height, const unsigned char *jpeg_data, size_t cb_jpeg, json_object *jresult)
{
//	debug_printf("%s(size=%dx%d)\n", __FUNCTION__, width, height);
	if(width <= 0 || height <= 0 || NULL == jpeg_data || cb_jpeg <= 0) return -1;
	
	struct video_frame *frame = video_frame_new(0, width, height, jpeg_data, cb_jpeg, 0);
	assert(frame);
	frame->type = video_frame_type_jpeg;
	
	pthread_mutex_lock(&channel->mutex);
	frame->frame_number = ++channel->frame_number;
	struct video_frame *old_frame = channel->frame;
	channel->frame = frame;
	pthread_mutex_unlock(&channel->mutex);
	
	if(old_frame) video_frame_unref(old_frame);
	return 0;
}

static long channel_get_frame(struct channel_data *channel, struct video_frame **p_frame)
{
	struct video_frame *current = NULL;
	pthread_mutex_lock(&channel->mutex);
	struct video_frame *frame = channel->frame;
	if(frame) {
		current = video_frame_new(channel->frame_number, frame->width, frame->height, frame->data, frame->length, 0);
	}
	pthread_mutex_unlock(&channel->mutex);
	
	if(current) {
		*p_frame = current;
		return current->frame_number;
	}
	return -1;
}

struct channel_data *channel_data_init(struct channel_data *channel, long id, const char *name, struct device_stream *stream)
{
	if(NULL == channel) channel = calloc(1, sizeof(*channel));
	assert(channel);
	
	int rc = pthread_mutex_init(&channel->mutex, NULL);
	assert(0 == rc);
	channel->id = id;
	if(name && name[0]) strncpy(channel->name, name, sizeof(channel->name) - 1);
	else snprintf(channel->name, sizeof(channel->name) - 1, "channel%ld", id);
	
	channel->stream = stream;
	channel->update_frame = channel_update_frame;
	channel->get_frame = channel_get_frame;
	
	stream->channel = channel;
	
	return channel;
}
void channel_data_cleanup(struct channel_data *channel)
{
	return;
}
