/*
 * app.c
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

#include <unistd.h>
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <getopt.h>

#include <curl/curl.h>

#include <libgen.h>
#include <pthread.h>

#include "utils.h"
#include "app.h"
#include "shell.h"

#include "ai-engine.h"
#include "video_streams.h"
#include "streaming-proxy.h"

static int app_reload_config(struct app_context *app, const char *conf_file);
static int app_init(struct app_context *app, int argc, char **argv);
static int app_run(struct app_context *app);
static int app_stop(struct app_context *app);
static void app_cleanup(struct app_context *app);

struct app_private
{
	struct app_context *app;
	struct shell_context *shell;
	
	int num_ai_engines;
	struct ai_context * ai_engines;
	
	int image_width;
	int image_height;
	int num_streams;
	struct video_stream *streams;
	struct streaming_proxy_context *proxy;
};

struct app_private * app_private_new(struct app_context *app)
{
	struct app_private *priv = calloc(1, sizeof(*priv));
	
	return priv;
}
void app_private_free(struct app_private *priv)
{
	if(NULL == priv) return;
	
	free(priv);
}

struct app_context *app_context_init(struct app_context *app, void * user_data)
{
	if(NULL == app) app = calloc(1, sizeof(*app));
	assert(app);
	
	curl_global_init(CURL_GLOBAL_ALL);
	
	app->user_data = user_data;
	app->reload_config = app_reload_config;
	app->init = app_init;
	app->run = app_run;
	app->stop = app_stop;
	app->cleanup = app_cleanup;
	
	app->priv = app_private_new(app);
	assert(app->priv);
	
	char *app_name = NULL;
	char *work_dir = NULL;
	
#if defined(WIN32) || defined(_WIN32)
	DWORD cb = GetModuleFileName(NULL, app->work_dir, sizeof(app->work_dir) - 1);
	assert(cb > 0);
	char *base_name = strrchr(app->work_dir, '\\');
	if(base_name) *base_name++ = '\0';

	char *p = strrchr(app->work_dir, '\\');
	if(p && strcasecmp(p, "\\bin") == 0) *p = '\0';	
	
	p = strrchr(base_name, '.');
	if(p) *p = '\0';
	
	app_name = base_name;
	work_dir = app->work_dir;
#else	
	ssize_t cb = readlink("/proc/self/exe", app->work_dir, sizeof(app->work_dir));
	assert(cb > 0);
	char *app_name = basename(app->work_dir);
	char *work_dir = dirname(app->work_dir);
	
#endif

	if(app_name) strncpy(app->name, app_name, sizeof(app->name));
	strncpy(app->title, app->name, sizeof(app->title));
	
	chdir(work_dir);
	printf("== work_dir: %s\n"
		   "== app_name: %s\n", 
		work_dir, app->name);
	
	return app;
}

void app_context_cleanup(struct app_context *app)
{
	if(NULL == app) return;
	app_private_free(app->priv);
	app->priv = NULL;
}

static int parse_ai_engines(struct app_context *app, json_object *jai_engines)
{
	assert(app && app->priv && jai_engines);
	
	struct app_private *priv = app->priv;
	int num_ai_engines = json_object_array_length(jai_engines);
	assert(num_ai_engines > 0);
	
	struct ai_context *ai_engines = calloc(num_ai_engines, sizeof(*ai_engines));
	assert(ai_engines);
	for(int i = 0; i < num_ai_engines; ++i) {
		pthread_mutex_init(&ai_engines[i].mutex, NULL);
		
		json_object *jai = json_object_array_get_idx(jai_engines, i);
		if(NULL == jai) continue;
		
		int id = json_get_value(jai, int, id);
		const char *type = json_get_value(jai, string, type);
		//~ const char *url = json_get_value(jai, string, url);
		//~ assert(type && url);
		if(NULL == type) type = "ai-engine::darknet";
		
		ai_engine_t *engine = ai_engine_init(NULL, type, app);
		assert(engine);
		
		int rc = engine->init(engine, jai);
		assert(0 == rc);
		ai_engines[i].id = id;
		ai_engines[i].engine = engine;
	}
	
	priv->num_ai_engines = num_ai_engines;
	priv->ai_engines = ai_engines;
	
	return 0;
}

struct ai_engine * find_ai_engine_by_id(struct app_context *app, int id)
{
	assert(app && app->priv && id >= 0);
	struct app_private *priv = app->priv;
	
	for(int i = 0; i < priv->num_ai_engines; ++i) {
		if(priv->ai_engines[i].id == id) return priv->ai_engines[i].engine;
	}
	return NULL;
}


static int parse_streams(struct app_context *app, json_object *jstreams)
{
	assert(app && app->priv && jstreams);
	struct app_private *priv = app->priv;
	
	int num_streams = json_object_array_length(jstreams);
	assert(num_streams > 0);
	
	struct video_stream *streams = calloc(num_streams, sizeof(*streams));
	assert(streams);
	
	priv->num_streams = num_streams;
	priv->streams = streams;
	
	for(int i = 0; i < num_streams; ++i) {
		json_object *jstream = json_object_array_get_idx(jstreams, i);
		if(NULL == jstream) continue;
		
		struct video_stream *stream = video_stream_init(&streams[i], jstream, app);
		assert(stream);
	}
	return 0;
}

static int app_reload_config(struct app_context *app, const char *conf_file)
{
	if(conf_file && NULL == app->jconfig) {
		app->jconfig = json_object_from_file(conf_file);
		assert(app->jconfig);
	}
	
	json_object *jconfig = app->jconfig;
	if(NULL == jconfig) return -1;
	
	json_object *jai_engines = NULL;
	json_object *jstreams = NULL;
	json_bool ok = FALSE;
	
	ok = json_object_object_get_ex(jconfig, "ai-engines", &jai_engines);
	assert(ok && jai_engines);
	
	int rc = parse_ai_engines(app, jai_engines);
	if(rc) return rc;
	
	ok = json_object_object_get_ex(jconfig, "streams", &jstreams);
	assert(ok && jstreams);
	
	rc = parse_streams(app, jstreams);
	return rc;
}


static int app_parse_args(struct app_context *app, int argc, char **argv)
{
	static struct option options[] = {
		{"conf", required_argument, NULL, 'c' },
		{"video_src", required_argument, NULL, 'v' },
		{"width", required_argument, NULL, 'W' },
		{"height", required_argument, NULL, 'H' },
		{NULL}
	};
	struct app_private *priv = app->priv;
	assert(priv);
	
	
#define MAX_INPUTS (36)
	int num_inputs = 0;
	const char *input_uris[MAX_INPUTS] = { NULL };
	
	int image_width = 640;
	int image_height = 360;
	
	const char *conf_file = NULL;
	
	while(1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "c:v:W:H:", options, &option_index);
		if(c == -1) break;
		switch(c) {
		case 'c': conf_file = optarg; break;
		case 'v': 
			assert(num_inputs < MAX_INPUTS);
			input_uris[num_inputs++] = optarg; 
			break;
		case 'W': image_width = atoi(optarg); break;
		case 'H': image_height = atoi(optarg); break;
		default:
			fprintf(stderr, "unknown args '%c'(%.2x)\n", c, (unsigned char)c);
			return -1;
		}
	}
	
	char conf_file_buf[PATH_MAX] = "";
	if(NULL == conf_file) {
		snprintf(conf_file_buf, sizeof(conf_file_buf), 
			"%s.json", 
			app->name[0]?app->name:"video-player5"); 
		conf_file = conf_file_buf;
	}
	
	debug_printf("conf_file: %s\n", conf_file);
	json_object *jconfig = json_object_from_file(conf_file);
	assert(jconfig);
	
	json_object *jstreams = NULL;
	json_bool ok = json_object_object_get_ex(jconfig, "streams", &jstreams);
	if(!ok || NULL == jstreams) {
		jstreams = json_object_new_array();
		json_object_object_add(jconfig, "streams", jstreams);
	}
	
	if(num_inputs > 0) {
		for(int i = 0; i < num_inputs; ++i) {
			json_object *jstream = json_object_array_get_idx(jstreams, i);
			if(NULL == jstream) {
				jstream = json_object_new_object();
				json_object_array_add(jstreams, jstream);
			}
			
			if(input_uris[i] && input_uris[i][0]) {
				json_object_object_add(jstream, "uri", json_object_new_string(input_uris[i]));
			}
		}
	}
	
	app->jconfig = jconfig;
	priv->image_width = image_width;
	priv->image_height = image_height;
	
#undef MAX_INPUTS
	return 0;
}

static int app_init(struct app_context *app, int argc, char **argv)
{
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);
	
	struct app_private *priv = app->priv;
	if(NULL == priv) priv = app->priv = app_private_new(app);
	assert(priv);
	
	struct streaming_proxy_context *proxy = streaming_proxy_context_init(NULL, NULL, app);
	assert(proxy);
	priv->proxy = proxy;
	
	int rc = app_parse_args(app, argc, argv);
	assert(0 == rc);
	if(rc) return rc;
	app->reload_config(app, NULL);
	
	json_object *jconfig = app->jconfig;
	priv->shell = shell_context_init(priv->shell, app);
	assert(priv->shell);
	
	rc = priv->shell->init(priv->shell, jconfig);
	
	return rc;
}

static int app_run(struct app_context *app)
{
	struct app_private *priv = app->priv;
	struct shell_context *shell = priv->shell;
	assert(priv && shell);
	
	for(int i = 0; i < priv->num_streams; ++i)
	{
		priv->streams[i].run(&priv->streams[i]);
	}
	
	shell->run(shell);
	return 0;
}

static int app_stop(struct app_context *app)
{
	gtk_main_quit();
	return 0;
}

static void app_cleanup(struct app_context *app)
{
	if(NULL == app || NULL == app->priv) return;
	struct app_private *priv = app->priv;
	
	struct ai_context *ai_engines = priv->ai_engines;
	int num_ai_engines = priv->num_ai_engines;
	if(NULL == ai_engines) return;
	
	
	for(int i = 0; i < num_ai_engines; ++i) {
		
		pthread_mutex_lock(&ai_engines[i].mutex);
		ai_engines[i].quit = 1;
		
		if(ai_engines[i].engine) {
			ai_engine_cleanup(ai_engines[i].engine);
			ai_engines[i].engine = NULL;
		}
	}
	
	priv->num_ai_engines = 0;
	free(priv->ai_engines);
	priv->ai_engines = NULL;
	return;
}

ssize_t app_get_streams(struct app_context *app, struct video_stream **p_streams)
{
	if(NULL == app || NULL == app->priv) return -1;
	struct app_private *priv = app->priv;
	if(p_streams) *p_streams = priv->streams;
	return priv->num_streams;
}

struct streaming_proxy_context *app_get_streaming_proxy(struct app_context *app)
{
	if(NULL == app || NULL == app->priv) return NULL;
	struct app_private *priv = app->priv;
	return priv->proxy;
}
