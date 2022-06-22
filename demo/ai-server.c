/*
 * ai-server.c
 * 
 * Copyright 2020 Che Hongwei <htc.chehw@gmail.com>
 * 
 * The MIT License (MIT)
 * 
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
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <json-c/json.h>

#include <pthread.h>
#include <libsoup/soup.h>
#include "ai-engine.h"
#include "ann-plugin.h"
#include "utils.h"

typedef struct global_param
{
	const char * conf_file;
	unsigned int port;
	const char * plugins_dir;
	
	SoupServer * server;
	json_object * jconfig;
	ssize_t count;
	ai_engine_t ** engines;
	
	
	// CORS
	json_object * jorigins_list;	// a white list for Access-Control-Allow-Origin
	char * access_control_allow_origin;
	char * access_control_expose_headers;
	int access_control_allow_credentials;
	char * access_control_allow_headers;
	char * access_control_allow_methods;
	uint64_t access_control_max_age;
	
}global_param_t;
global_param_t * global_param_parse_args(global_param_t * params, int argc, char ** argv);
void global_param_cleanup(global_param_t * params);

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
#define global_lock()	pthread_mutex_lock(&g_mutex)
#define global_unlock()	pthread_mutex_unlock(&g_mutex)

static ssize_t unix_time_to_string(
	const time_t tv_sec, 
	int use_gmtime, 
	const char * time_fmt, 
	char * sz_time, size_t max_size
)
{
	if(NULL == time_fmt) time_fmt = "%Y-%m-%d %H:%M:%S %Z";
	struct tm tm_buf[1], *t = NULL;
	memset(tm_buf, 0, sizeof(tm_buf));
	t = use_gmtime?gmtime_r(&tv_sec, tm_buf):localtime_r(&tv_sec, tm_buf);
	if(NULL == t) {
		perror("unix_time_to_string::gmtime_r/localtime_r");
		return -1;
	}
	
	ssize_t cb = strftime(sz_time, max_size, time_fmt, t);
	return cb;
}

static void server_log_fmt(SoupMessage * msg, SoupClientContext * client, const char * fmt, ...)
{
	char text[4096] = "";
	va_list ap;
	va_start(ap, fmt);
	int cb = vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	if(cb <= 0) return;
	if(text[cb - 1] == '\n') text[--cb] = '\0';
	
	char sz_time[100] = "";
	struct timespec timestamp[1];
	memset(timestamp, 0, sizeof(timestamp));
	clock_gettime(CLOCK_REALTIME, timestamp);
	ssize_t cb_time = unix_time_to_string(timestamp->tv_sec, 0, "%Y-%m-%d %H:%M:%S %Z", sz_time, sizeof(sz_time));
	assert(cb_time > 0);
	
	const char * remote_addr = soup_client_context_get_host(client);
	fprintf(stderr, "[LOG]: %s %s %s\n", sz_time, remote_addr, text);
	return;
}

static int check_origin_and_set_header(global_param_t * app, const char * origin, SoupMessageHeaders * response_headers)
{
	if(NULL == origin) return -1;
	if(NULL == app->access_control_allow_origin && NULL == app->jorigins_list) return -1;
	
	if(app->access_control_allow_origin != NULL) {
		if(app->access_control_allow_origin[0] == '*' 
		|| strcasecmp(origin, app->access_control_allow_origin) == 0) {
			soup_message_headers_append(response_headers, "Access-Control-Allow-Origin", app->access_control_allow_origin);
			return 0;
		}
	}else {
		int num_origins = json_object_array_length(app->jorigins_list);
		for(int i = 0; i < num_origins; ++i) {
			const char * origin_in_white_list = json_object_get_string(json_object_array_get_idx(app->jorigins_list, i));
			if(origin_in_white_list && strcasecmp(origin, origin_in_white_list) == 0) {
				soup_message_headers_append(response_headers, "Access-Control-Allow-Origin", origin_in_white_list);
				return 0;
			}
		}
	}
	return -1;
}
static guint on_options(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	global_param_t * app = user_data;
	assert(app);
	
	SoupMessageHeaders * req_hdrs = msg->request_headers;
	SoupMessageHeaders * response_headers = msg->response_headers;
	const char * origin = soup_message_headers_get_one(req_hdrs, "Origin");
	server_log_fmt(msg, client, "request method: %s, origin: %s\n", msg->method, origin);
	
	int rc = check_origin_and_set_header(app, origin, response_headers);
	if(rc) return SOUP_STATUS_BAD_REQUEST;
	
	if(app->access_control_expose_headers) {
		soup_message_headers_append(response_headers, 
			"Access-Control-Expose-Headers", app->access_control_expose_headers);
	}
	if(app->access_control_allow_headers) {
		soup_message_headers_append(response_headers, 
			"Access-Control-Allow-Headers", app->access_control_allow_headers);
	}
	if(app->access_control_max_age > 0) {
		char sz_max_age[100] = "";
		snprintf(sz_max_age, sizeof(sz_max_age), "%lu", (unsigned long)app->access_control_max_age);
		soup_message_headers_append(response_headers, 
			"Access-Control-Max-Age", sz_max_age);
	}
	if(app->access_control_allow_credentials) {
		soup_message_headers_append(response_headers, 
			"Access-Control-Allow-Credentials", "true");
	}
	if(app->access_control_allow_methods) {
		soup_message_headers_append(response_headers, 
			"Access-Control-Allow-Methods", app->access_control_allow_methods);
	}
	return SOUP_STATUS_OK;
}


void on_request_ai_engine(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	assert(user_data);
	global_param_t * params = user_data;
	
	debug_printf("method: %s\n", msg->method);
	if(msg->method == SOUP_METHOD_OPTIONS) {
		guint status = on_options(server, msg, path, query, client, user_data);
		soup_message_set_status(msg, status);
		return;
	}
	
	if(msg->method != SOUP_METHOD_POST) 
	{
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	int engine_index = 0;
	if(query)
	{
		const char * sz_index = g_hash_table_lookup(query, "engine");
		if(sz_index)
		{
			engine_index = atoi(sz_index);
			if(engine_index < 0 || engine_index >= params->count) {
				soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
				return;
			}
		}
	}
	ai_engine_t * engine = params->engines[engine_index];
	assert(engine);
	
	const char * content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
	printf("content-type: %s\n", content_type);
	gboolean uncertain = TRUE;
	char * image_type = g_content_type_guess(NULL, 
		(const unsigned char *)msg->request_body->data, 
		msg->request_body->length, 
		&uncertain);
	if(uncertain || NULL == image_type) {
		fprintf(stderr, "[ERROR]: invalid image format.\n");
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		if(image_type) g_free(image_type);
		return;
	}
	
	debug_printf("content_type: %s, real_image_type: %s", 
		content_type, image_type);
		
	gboolean is_jpeg = FALSE;
	gboolean is_png = FALSE;
	
	is_jpeg = g_content_type_equals(image_type, "image/jpeg");
	if(!is_jpeg) is_png = g_content_type_equals(image_type, "image/png");
	g_free(image_type);
	
	if(!is_jpeg && !is_png)
	{
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	int rc = 0;
	
	if(is_jpeg) {
		rc = input_frame_set_jpeg(frame, 
			(unsigned char *)msg->request_body->data, 
			msg->request_body->length, NULL, 0);
	}
	else {
		rc = input_frame_set_png(frame, 
			(unsigned char *)msg->request_body->data, 
			msg->request_body->length, NULL, 0);
	}
	
	if(rc) {
		fprintf(stderr, "[ERROR]: parse image file failed.\n");
		input_frame_clear(frame);
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	json_object * jresult = NULL;
	
	printf("frame: %d x %d\n", frame->width, frame->height);
	global_lock();
	rc = engine->predict(engine, frame, &jresult);
	global_unlock();
	input_frame_clear(frame);
	
	printf("rc=%d, jresult=%p\n", rc, jresult);
	if(rc || NULL == jresult)
	{
		if(jresult) json_object_put(jresult);
		jresult = json_object_new_object();
		json_object_object_add(jresult, "err_code", json_object_new_int(1));
	}
	
	const char * response = json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PLAIN);
	assert(response);
	int cb = strlen(response);
	
	SoupMessageHeaders * response_headers = msg->response_headers;
	
	soup_message_headers_append(response_headers, "Access-Control-Allow-Origin", 
		params->access_control_allow_origin?params->access_control_allow_origin:"*");
	soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, response, cb);
	
	soup_message_set_status(msg, SOUP_STATUS_OK);
	json_object_put(jresult);
	return;
}


static global_param_t g_params[1] = {{
	.conf_file = "ai-server.json",
	.port = 9090,
	.plugins_dir = "plugins",
}};
int main(int argc, char **argv)
{
	global_param_t * params = global_param_parse_args(NULL, argc, argv);
	assert(params && params->count && params->engines);
	
	SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "ai-server", NULL);
	assert(server);
	params->server = server;
	
	static const char * path = "/ai";
	soup_server_add_handler(server, path, 
		(SoupServerCallback)on_request_ai_engine, params, NULL);
	
	gboolean ok = FALSE;
	GError * gerr = NULL;

	ok = soup_server_listen_all(server, params->port, 0, &gerr);
	if(!ok || gerr)
	{
		fprintf(stderr, "soup_server_listen_all() failed: %s\n", gerr?gerr->message:"unknown error");
		if(gerr) g_error_free(gerr);
		exit(1);
	}
	
	GSList * uris = soup_server_get_uris(server);
	for(GSList * uri = uris; uri; uri = uri->next)
	{
		gchar * sz_uri = soup_uri_to_string(uri->data, FALSE);
		if(sz_uri) {
			printf("listening on: %s%s\n", sz_uri, path);
		}
		soup_uri_free(uri->data);
		uri->data = NULL;
		g_free(sz_uri);
	}
	g_slist_free(uris);
	
	GMainLoop * loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(loop);
	
	g_main_loop_unref(loop);
	global_param_cleanup(params);
	
	return 0;
}



/******************************************************************************
 * global_params
 *****************************************************************************/
#include <getopt.h>
static void print_usuage(int argc, char ** argv)
{
	printf("Usuage: \n"
		   "    %s [--conf=<conf_file.json>] [--port=<port>] [--plugins_dir=<plugins>] \n", argv[0]);
	return;
}

static int parse_cors_configs(global_param_t * app, json_object * jconfig)
{
	assert(app && jconfig);
	
	json_bool ok = FALSE;
	app->access_control_allow_origin = json_get_value(jconfig, string, Access-Control-Allow-Origin);
	if(NULL == app->access_control_allow_origin) {
		ok = json_object_object_get_ex(jconfig, "origins_list", &app->jorigins_list);
		if(!ok || NULL == app->jorigins_list || json_object_array_length(app->jorigins_list) <= 0) {
			fprintf(stderr, 
				"[WARNING]: Access-Control-Allow-Origin was set to '*', "
				"DO NOT use it for responding to a credentialed requests request\n");
			app->access_control_allow_origin = "*";	
		}
	}
	app->access_control_expose_headers = json_get_value(jconfig, string, Access-Control-Expose-Headers);
	app->access_control_max_age = json_get_value_default(jconfig, int, Access-Control-Max-Age, 86400);
	app->access_control_allow_credentials = json_get_value(jconfig, int, Access-Control-Allow-Credentials);
	app->access_control_allow_methods = json_get_value_default(jconfig, string, Access-Control-Allow-Methods, "GET, POST, OPTIONS");
	app->access_control_allow_headers = json_get_value_default(jconfig, string, Access-Control-Allow-Headers, "Origin, Content-Type, Authorization");
	
	return 0;
}

global_param_t * global_param_parse_args(global_param_t * params, int argc, char ** argv)
{
	if(NULL == params) params = g_params;
	static struct option options[] = {
		{"conf", required_argument, 0, 'c' },	// config file
		{"port", required_argument, 0, 'p' },	// AI server listening port
		{"plugins_dir", required_argument, 0, 'd' },	// plugins path
		{"help", no_argument, 0, 'h' },
		{NULL, 0, 0, 0 },
	};
	
	unsigned int port = 0;
	const char * plugins_dir = NULL;
	while(1)
	{
		int index = 0;
		int c = getopt_long(argc, argv, "c:p:d:h", options, &index);
		if(c < 0) break;
		switch(c)
		{
		case 'c': params->conf_file = optarg; 	break;
		case 'p': port = atoi(optarg); break;
		case 'd': plugins_dir = optarg; 	break;
		case 'h': 
		default:
			print_usuage(argc, argv); exit(0);
		}
	}
	
	// load config
	assert(params->conf_file && params->conf_file[0]);
	json_object * jconfig = json_object_from_file(params->conf_file);
	assert(jconfig);
	params->jconfig = jconfig;
	
	if(0 == port || port > 65535) {
		port = json_get_value_default(jconfig, int, port, params->port);
		params->port = port;
	}
	if(NULL == plugins_dir) {
		plugins_dir = json_get_value_default(jconfig, string, plugins_dir, params->plugins_dir);
		params->plugins_dir = plugins_dir;
	}
	
	// init plugins
	ann_plugins_helpler_init(NULL, plugins_dir, params);
	
	// init ai-engines
	json_object * jai_engines = NULL;
	json_bool ok = json_object_object_get_ex(jconfig, "engines", &jai_engines);
	assert(ok && jai_engines);
	
	int count = json_object_array_length(jai_engines);
	assert(count > 0);
	
	ai_engine_t ** engines = calloc(count, sizeof(*engines));
	for(int i = 0; i < count; ++i)
	{
		json_object * jengine = json_object_array_get_idx(jai_engines, i);
		assert(jengine);
		
		const char * plugin_name = json_get_value(jengine, string, plugin_name);
		if(NULL == plugin_name) plugin_name = "ai-engine::darknet";
		ai_engine_t * engine = ai_engine_init(NULL, plugin_name, params);
		assert(engines);
		
		int rc = engine->init(engine, jengine);
		assert(0 == rc);
		
		engines[i] = engine;
	}
	params->count = count;
	params->engines = engines;
	
	
	json_object * jcors = NULL;
	ok = json_object_object_get_ex(jconfig, "CORS", &jcors);
	if(ok && jcors)  parse_cors_configs(params, jcors);
	
	

	return params;
}

void global_param_cleanup(global_param_t * params)
{
	if(NULL == params) return;
	if(params->count && params->engines)
	{
		ai_engine_t ** engines = params->engines;
		for(ssize_t i = 0; i < params->count; ++i)
		{
			if(engines[i]) {
				ai_engine_cleanup(engines[i]);
				free(engines[i]);
				engines[i] = NULL;
			}
		}
		free(engines);
		params->engines = NULL;
		params->count = 0;
	}
	if(params->jconfig) json_object_put(params->jconfig);
	params->jconfig = NULL;
}
