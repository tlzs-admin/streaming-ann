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

#include <gst/gst.h>
#include <glib.h>

#include <libsoup/soup.h>
#include <getopt.h>
#include <json-c/json.h>

#include "app.h"
#include "video_stream.h"
#include "webserver.h"
#include "utils.h"

#include <curl/curl.h>

volatile int s_quit = 0;

#define APP_MAX_DEVICE_STREAMS (64)
struct app_private
{
	struct app_context *app;
	int argc;
	char **argv;
	GMainLoop *loop;
	
	ssize_t num_streams;
	struct device_stream *streams[APP_MAX_DEVICE_STREAMS];
	
	struct webserver_context *web;
	
};
static struct app_private *app_private_new(struct app_context *app)
{
	struct app_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->app = app;
	
	return priv;
}
static void app_private_free(struct app_private *priv)
{
	for(ssize_t i = 0; i < priv->num_streams; ++i) {
		struct device_stream *stream = priv->streams[i];
		priv->streams[i] = NULL;
		
		if(stream) {
			device_stream_free(stream);
		}
	}
	free(priv);
}

GMainLoop *app_get_main_loop(struct app_context *app)
{
	if(NULL == app || NULL == app->priv) return NULL;
	return app->priv->loop;
}

struct app_idle_data
{
	struct app_context *app;
	struct device_stream *stream;
	struct video_frame *frame;
	json_object *jresult;
};

static gboolean on_app_idle(gpointer user_data)
{
	struct app_idle_data *data = user_data;
	assert(data);
	
	struct device_stream *stream = data->stream;
	struct video_frame *frame = data->frame;
	json_object *jresult = data->jresult;
	
	if(jresult) {
		debug_printf("frame: %d x %d, jresult=%s\n", frame->width, frame->height, 
			json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PRETTY));
	}
	
	if(stream->channel) {
		stream->channel->update_frame(stream->channel, frame->width, frame->height, frame->data, frame->length, jresult);
	}
	
	device_stream_unref_frame(data->stream, frame);
	json_object_put(jresult);
	free(data);
	
	return G_SOURCE_REMOVE;
}


static int app_update_device_stream(struct device_stream *stream, struct video_frame *frame, json_object *jresult)
{
	struct app_idle_data *data = calloc(1, sizeof(*data));
	assert(data);
	
	data->app = stream->user_data;
	data->stream = stream;
	data->frame = device_stream_addref_frame(stream, frame);
	data->jresult = json_object_get(jresult);
	
	g_idle_add(on_app_idle, data);
	return 0;
}

static int app_reload_config(struct app_context *app, const char *conf_file)
{
	assert(app && app->priv);
	int rc = 0;
	struct app_private *priv = app->priv;
	
	json_object *jconfig = json_object_from_file(conf_file);
	assert(jconfig);
	app->jconfig = jconfig;
	
	json_object *jstreams = NULL;
	
	json_bool ok = json_object_object_get_ex(jconfig, "streams", &jstreams);
	assert(ok && jstreams);
	
	ssize_t num_streams = json_object_array_length(jstreams);
	assert(num_streams > 0);
	assert(num_streams < APP_MAX_DEVICE_STREAMS);
	
	for(ssize_t i = 0; i < num_streams; ++i) {
		json_object *jstream = json_object_array_get_idx(jstreams, i);
		
		struct device_stream *stream = device_stream_new_from_config(jstream, app);
		assert(stream);
		
		stream->stream_id = i + 1;
		stream->on_update_frame = app_update_device_stream;
		
		priv->streams[i] = stream;
	}
	priv->num_streams = num_streams;
	
	struct webserver_context *web = webserver_context_init(NULL, app);
	assert(web);
	
	json_object *jwebserver = NULL;
	json_object_object_get_ex(jconfig, "webserver", &jwebserver);
	rc = web->init(web, num_streams, priv->streams, jwebserver);
	
	return rc;
}

static int app_parse_args(struct app_context *app, int argc, char **argv)
{
	static struct option options[] = {
		{"conf", required_argument, 0, 'c'},
		{"work_dir", required_argument, 0, 'w'},
		{"help",no_argument, 0, 'h'},
		{NULL}
	};
	
	assert(app && app->priv);
	int rc = 0;
	
	if(NULL == app->reload_config) app->reload_config = app_reload_config;
	
	const char *conf_file = NULL;
	const char *work_dir = NULL;
	
	while(1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "c:hw:", options, &option_index);
		if(c == -1) break;
		
		switch(c) {
		case 'c': conf_file = optarg; break;
		case 'w': work_dir = optarg; break;
		case 'h': 
		default:
			fprintf(stderr, "Usuage: ./%s [--conf=config.json] [--work_dir=<workdir>] ", argv[0]);
			exit((c != 'h'));
		}
	}
	
	if(work_dir) {
		strncpy(app->work_dir, work_dir, sizeof(app->work_dir) - 1);
		rc = chdir(work_dir);
		if(rc) {
			perror("chdir");
			exit(rc);
		}
	}
	
	if(NULL == conf_file) conf_file = "config.json";
	
	return app->reload_config(app, conf_file);
}

static int app_init(struct app_context *app, int argc, char **argv)
{
	gst_init(&argc, &argv);
	
	curl_global_init(CURL_GLOBAL_ALL);
	return app_parse_args(app, argc, argv);
}

static int app_run(struct app_context *app)
{
	assert(app && app->priv);
	struct app_private *priv = app->priv;
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	priv->loop = loop;
	g_main_loop_run(loop);
	
	g_main_loop_unref(loop);
	return 0;
}

static int app_stop(struct app_context *app)
{
	assert(app && app->priv);
	struct app_private *priv = app->priv;
	
	if(priv->loop) {
		g_main_loop_quit(priv->loop);
		priv->loop = NULL;
	}
	return 0;
}

static void app_cleanup(struct app_context *app)
{
	return;
}

struct app_context *app_context_init(struct app_context *app, void * user_data)
{
	if(NULL == app) app = calloc(1, sizeof(*app));
	assert(app);
	
	app->init = app_init;
	app->reload_config = app_reload_config;
	app->run = app_run;
	app->stop = app_stop;
	app->cleanup = app_cleanup;
	
	struct app_private *priv = app_private_new(app);
	assert(priv);
	app->priv = priv;
	
	return app;
}

void app_context_cleanup(struct app_context *app)
{
	if(NULL == app) return;
	if(app->cleanup) app->cleanup(app);
	
	app_private_free(app->priv);
	app->priv = NULL;
	return;
}

