/*
 * webserver.c
 * 
 * Copyright 2020 chehw <htc.chehw@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <libsoup/soup.h>


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <search.h>

#include "utils.h"
#include "img_proc.h"
#include "ann-plugin.h"
#include "io-input.h"
#include "ai-engine.h"

#include "webserver-utils.c.impl"

#ifndef JSON_C_TO_STRING_NOSLASHESCAPE
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif


typedef struct webserver_context
{
	char * conf_file;
	json_object * jconfig;
	ai_engine_t * engine;
	int quit;
	
	SoupServer * server;
	pthread_mutex_t mutex;
	pthread_t th;
	
	struct pages_cache pages[1];
	
}webserver_context_t;
void webserver_context_cleanup(webserver_context_t * ctx)
{
	if(NULL == ctx) return;
	ctx->quit = 1;
	
	pages_cache_cleanup(ctx->pages);
	ai_engine_cleanup(ctx->engine);
	
	pthread_mutex_destroy(&ctx->mutex);
	if(ctx->jconfig) {
		json_object_put(ctx->jconfig);
		ctx->jconfig = NULL;
	}
	return;
}

static webserver_context_t g_context[1] = {{
	.mutex = PTHREAD_MUTEX_INITIALIZER,
}};
static void on_demo_handler(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data);
static void on_favicon(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	///< @todo generate or send a icon
	soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
}

static void on_clear_cache(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	///< @todo generate or send a icon
	struct webserver_context * ctx = user_data;
	assert(ctx);
	
	struct pages_cache * pages = ctx->pages;
	pages->clear_all(pages);

	soup_message_set_response(msg, "text/html", SOUP_MEMORY_STATIC, "ok", 2);
	soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
}
static json_object * generate_default_config(const char * conf_file)
{
	/*
	{
		"port": 8081,
		"document_root": "./html",
		"server_cert_file": "ssl/server_cert.pem",
		"server_key_file": "ssl/server_key.pem",
		"use_ssl": 1,
		
		"engine": {
			"type":"ai-engine::httpclient",
			"url":"http://127.0.0.1:9090/ai"
		},
	}
	*/
	json_object * jconfig = json_object_new_object();
	json_object_object_add(jconfig, "port", json_object_new_int(8081));
	json_object_object_add(jconfig, "document_root", json_object_new_string("./html"));
	json_object_object_add(jconfig, "server_cert_file", json_object_new_string("ssl/server_cert.pem"));
	json_object_object_add(jconfig, "server_key_file", json_object_new_string("ssl/server_key.pem"));
	json_object_object_add(jconfig, "use_ssl", json_object_new_int(1));
	
	json_object * jengine = json_object_new_object();
	json_object_object_add(jconfig, "engine", jengine);
	json_object_object_add(jengine, "type", json_object_new_string("ai-engine::httpclient"));
	json_object_object_add(jengine, "url", json_object_new_string("http://127.0.0.1:9090/ai"));
	
	json_object_to_file_ext(conf_file, jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	return 0;
	
}

static int start_ai_engine(struct webserver_context * ctx, json_object * jconfig)
{
	assert(ctx && jconfig);
	int rc = 0;
	json_object * jengine = NULL;
	json_bool ok = FALSE;
	ok = json_object_object_get_ex(jconfig, "engine", &jengine); 
	assert(ok && jengine);
	
	const char * engine_type = json_get_value(jengine, string, type);
	ai_engine_t * engine = ai_engine_init(NULL, engine_type, ctx);
	assert(engine);
	ctx->engine = engine;
	
	rc = engine->init(engine, jengine);	
	assert(0 == rc);
	return rc;
}
static int init_pages_caches(struct webserver_context * ctx, json_object * jconfig)
{
	const char * document_root = json_get_value(jconfig, string, document_root);
	const char * runas_user = json_get_value_default(jconfig, string, runas, "www-data");
	
	uid_t uid = -1;
	gid_t gid = -1;
	get_user_id(runas_user, &uid, &gid);
	
	struct pages_cache * cache = ctx->pages;
	pages_cache_init(cache, document_root, uid, gid, ctx);
	
	return 0;
}
static int start_web_server(struct webserver_context * ctx, json_object * jconfig)
{
	assert(ctx && jconfig);
	const char * server_cert = getenv("DEMO_SERVER_CERT");
	const char * server_key = getenv("DEMO_SERVER_KEY");
	const char * server_header = json_get_value_default(jconfig, string, server_header, "StreammingAnn-Demo");
	int use_ssl = 0;
	int port = json_get_value_default(jconfig, int, port, 8081);
	if(NULL == server_cert) server_cert = json_get_value(jconfig, string, server_cert_file);
	if(NULL == server_key) server_key = json_get_value(jconfig, string, server_key_file);
	

	SoupServer * server = NULL;
	GError * gerr = NULL;
	server= soup_server_new(
		SOUP_SERVER_SERVER_HEADER, server_header,
		NULL);
	assert(server);
	
	if(server_cert && server_key) {
		use_ssl = soup_server_set_ssl_cert_file(server, server_cert, server_key, &gerr);
		if(gerr) {
			fprintf(stderr, "[ERROR]::%s\n", gerr->message);
			g_error_free(gerr);
			exit(1);
		}
	}
	
	ctx->server = server;
	soup_server_add_early_handler(server, "/favicon.ico", on_favicon, ctx, NULL);
	soup_server_add_early_handler(server, "/admin/clear_cache", on_clear_cache, ctx, NULL);
	soup_server_add_handler(server, "/", on_demo_handler, ctx, NULL);
	
	soup_server_listen_all(server, port, use_ssl?SOUP_SERVER_LISTEN_HTTPS:0, &gerr);
	if(gerr)
	{
		fprintf(stderr, "[ERROR]::soup_server_listen_all() failed: %s\n", 
			gerr->message);
		g_error_free(gerr);
		exit(1);
	}
	
	GSList * uris = soup_server_get_uris(server);
	for(GSList * uri = uris; uri; uri = uri->next)
	{
		gchar * sz_uri = soup_uri_to_string(uri->data, FALSE);
		if(sz_uri) {
			printf("listening on: %s\n", sz_uri);
		}
		soup_uri_free(uri->data);
		uri->data = NULL;
		g_free(sz_uri);
	}
	g_slist_free(uris);
	
	return init_pages_caches(ctx, jconfig);
}

static int webserver_context_init(struct webserver_context * ctx, const char * conf_file)
{
	if(NULL == conf_file) conf_file = "webserver.json";
	json_object * jconfig = json_object_from_file(conf_file);
	if(NULL == jconfig) jconfig = generate_default_config(conf_file);
	assert(jconfig);
	
	ctx->jconfig = jconfig;
	start_ai_engine(ctx, jconfig);
	start_web_server(ctx, jconfig);
	
	return 0;
}

int main(int argc, char **argv)
{
	webserver_context_t * ctx = g_context;
	ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, "plugins", NULL);
	assert(helpler);
	
	int rc = webserver_context_init(ctx, NULL);
	assert(0 == rc);
		
	GMainLoop * loop = g_main_loop_new(NULL, FALSE);
	assert(loop);
	
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	
	webserver_context_cleanup(ctx);
	return 0;
}

static void on_get(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data);
static void on_ai_request(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data);
static void on_demo_handler(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	if(msg->method == SOUP_METHOD_GET)
	{
		on_get(server, msg, path, query, client, user_data);
		return;
	}else if(msg->method == SOUP_METHOD_POST || msg->method == SOUP_METHOD_PUT)
	{
		on_ai_request(server, msg, path, query, client, user_data);
		return;
	}else if(msg->method == SOUP_METHOD_HEAD)
	{
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
	return;
}

void show_error_page(SoupServer * server, SoupMessage * msg, const char * err_fmt, ...)
{
	static const char default_error[] = "{\"error_code\": 1, \"error_msg\": \"ERROR\" }\r\n";
	if(NULL == err_fmt)
	{
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_STATIC, default_error, sizeof(default_error) - 1);
	}else
	{
		char err_msg[4096] = "";
		va_list args;
		va_start(args, err_fmt);
		int cb = vsnprintf(err_msg, sizeof(err_msg), err_fmt, args);
		va_end(args);
		if(cb <= 0)
		{
			soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			return;
		}
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, err_msg, cb);
	}
//	soup_message_set_status(msg, SOUP_STATUS_OK);
	return;
}


static void on_get(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	struct webserver_context * ctx = user_data;
	assert(ctx);
	
	if(NULL == path || path[0] != '/') {
		show_error_page(server, msg, "403 Forbidden, path=%s.", path);
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		return;
	}
	
	const char * default_page = "/index.html";
	if(path[0] == '/' && path[1] == '\0') path = default_page;
	
	struct pages_cache * cache = ctx->pages;
	const struct page_data * page = NULL;
	AUTO_LOCKER(&cache->mutex);
	
	int rc = cache->get(cache, path, &page);
	if(rc) {
		show_error_page(server, msg, "404 Not Found, get_path failed. path=%s.", path);
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		return;
	}
	
	gboolean uncertain = TRUE;
	char * content_type = g_content_type_guess(path, 
		page->data, page->length, 
		//~ NULL, 0, 
		&uncertain);
	if(uncertain) {
		show_error_page(server, msg, "403 Forbidden, unknown content type(%s). path=%s.", content_type, path);
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		if(content_type) g_free(content_type);
		return;
	}
	
	soup_message_set_response(msg, content_type, SOUP_MEMORY_COPY, (const char *)page->data, page->length);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	
	return;
}

static void on_ai_request(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	static const char * fmt = "{\"error_code\": %d, \"error_msg\": \"%s\" }\r\n";
	SoupMessageBody * request_body = msg->request_body;
	gboolean uncertain = TRUE;
	const char * content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
	
	if(NULL == request_body || NULL == request_body->data || !request_body->length) {
		show_error_page(server, msg, fmt, 1, "no payload");
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		return;
	}
	char * parsed_content_type = g_content_type_guess(NULL, (const unsigned char *)request_body->data, request_body->length, &uncertain);
	debug_printf("content-length: %ld\n", request_body->length);
	debug_printf("Content-Type: %s, parsed_type: %s\n", content_type, parsed_content_type);
	
	if(uncertain || NULL == parsed_content_type) {
		show_error_page(server, msg, fmt, 1, "unknown payload type");
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		if(parsed_content_type) g_free(parsed_content_type);
		return;
	}
	
	gboolean is_png = FALSE;
	gboolean is_jpeg = g_content_type_equals(parsed_content_type, "image/jpeg");
	if(!is_jpeg) is_png = g_content_type_equals(parsed_content_type, "image/png");
	
	
	if(!is_png && !is_jpeg) {
		show_error_page(server, msg, fmt, 2, parsed_content_type);
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		g_free(parsed_content_type);
		return;
	}
	g_free(parsed_content_type);
	
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	if(is_jpeg) input_frame_set_jpeg(frame, (unsigned char *)request_body->data, request_body->length, NULL, 0);
	else input_frame_set_png(frame, (unsigned char *)request_body->data, request_body->length, NULL, 0);
	
	webserver_context_t * ctx = user_data;
	ai_engine_t * engine = ctx->engine;
	assert(ctx && engine);
	json_object * jresult = NULL;
	int rc = engine->predict(engine, frame, &jresult);
	if(rc == 0 && jresult)
	{
		const char * json_str = json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PLAIN);
		assert(json_str);
		int cb_json = strlen(json_str);
		assert(cb_json > 0);
		
		soup_message_set_response(msg, "application/json",
			SOUP_MEMORY_COPY, json_str, cb_json);
		soup_message_set_status(msg, SOUP_STATUS_OK);
	}
	if(jresult) json_object_put(jresult);
	input_frame_clear(frame);
	if(0 == rc) return;
	
	show_error_page(server, msg, fmt, rc, "engine->predict() failed");
	soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
	return;
}
