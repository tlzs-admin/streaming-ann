/*
 * app.c
 * 
 * Copyright 2022 Che Hongwei <htc.chehw@gmail.com>
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
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include <json-c/json.h>
#include <limits.h>
#include <pthread.h>
#include <libintl.h>	// gettext()
#include <libgen.h>		// dirname() / basename()

#include "app.h"
#include "shell.h"
#include "utils.h"
#include "ai-engine.h"
#include <locale.h>

#if defined(WIN32) || defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#endif


//#include "ai-client.h"

#ifndef debug_printf
#define debug_printf(fmt, ...) do { \
		fprintf(stderr, "\e[33m" "%s(%d):" fmt "\e[39m\n", __FILE__, __LINE__, ##__VA_ARGS__); \
	} while(0) 
#endif

struct shell_context;
struct ai_engine;
#define MAX_VIDEO_STREAMS (64)
#define MAX_AI_ENGINES (16)
struct app_private
{
	struct app_context *app;
	int argc;
	char **argv;
	char *conf_file;
	json_object *jconfig;
	
	struct shell_context *shell;
	size_t num_streams;
	struct video_source_common *streams[MAX_VIDEO_STREAMS];
	size_t num_engines;
	struct ai_engine *engines[MAX_AI_ENGINES];
};
static struct app_private *app_private_new(struct app_context *app)
{
	struct app_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->app = app;
	priv->shell = shell_context_init(NULL, app);
	assert(priv->shell);
	
	return priv;
}
static void app_private_free(struct app_private *priv)
{
	if(NULL == priv) return;
	if(priv->app) priv->app->priv = NULL;
	free(priv);
	return;
}

static int app_reload_config(struct app_context *app, const char *conf_file)
{
	debug_printf("%s(%p, %s)", __FUNCTION__, app, conf_file);
	return 0;
}

static int app_parse_args(struct app_private *priv, int argc, char **argv)
{
	return 0;
}

static int app_init(struct app_context *app, int argc, char **argv)
{
	debug_printf("%s(%p) ...", __FUNCTION__, app);
	assert(app && app->priv);
	struct app_private *priv = app->priv;
	priv->argc = argc;
	priv->argv = argv;
	
	int rc = app_parse_args(priv, argc, argv);
	json_object *jconfig = priv->jconfig;
	const char *title = NULL;
	if(jconfig) {
		title = json_get_value(jconfig, string, title);
	}
	if(title) strncpy(app->title, title, sizeof(app->title) - 1);
	
	struct shell_context *shell = priv->shell;
	if(shell) shell->init(shell, jconfig);
	
	return rc;
}
static int app_run(struct app_context *app)
{
	debug_printf("%s(%p) ...", __FUNCTION__, app);
	assert(app && app->priv);
	struct app_private *priv = app->priv;
	struct shell_context *shell = priv->shell;
	assert(shell);
	
	return shell->run(shell);
}
static int app_stop(struct app_context *app)
{
	debug_printf("%s(%p) ...", __FUNCTION__, app);
	assert(app && app->priv);
	struct app_private *priv = app->priv;
	struct shell_context *shell = priv->shell;
	
	return shell->stop(shell);
}
static void app_cleanup(struct app_context *app)
{
	debug_printf("%s(%p) ...", __FUNCTION__, app);
}

struct app_context *app_context_init(struct app_context *app, void * user_data)
{
	if(NULL == app) app = calloc(1, sizeof(*app));
	assert(app);
	app->user_data = user_data;
	
	app->reload_config = app_reload_config;
	app->init = app_init;
	app->run = app_run;
	app->stop = app_stop;
	app->cleanup = app_cleanup;
	
	char *base_name = NULL;
#ifdef _WIN32
	DWORD cb = GetModuleFileName(NULL, app->work_dir, sizeof(app->work_dir) - 1);
	assert(cb > 0);
	base_name = strrchr(app->work_dir, '\\');
	if(base_name) *base_name++ = '\0';
	
	char *p = strrchr(app->work_dir, '\\');
	if(p && strcasecmp(p, "\\bin") == 0) *p = '\0';	
#else
	char *app_path = realpath("/proc/self/exe", app->work_dir);
	assert(app_path);
	
	base_name = basename(app_path);
	char *dir_name = dirname(app_path);
	assert(base_name && dir_name);
	
	char *p = strrchr(app->work_dir, '/');
	if(p && strcasecmp(p, "/bin") == 0) *p = '\0';
#endif

	strncpy(app->name, base_name, sizeof(app->name) - 1);
	strncpy(app->title, base_name, sizeof(app->title) - 1);
	debug_printf("work_dir: %s", app->work_dir);
	debug_printf("app_name: %s", app->name);
	
	app->priv = app_private_new(app);
	
	fflush(stdout);
	return app;
}
void app_context_cleanup(struct app_context *app)
{
	if(NULL == app) return;
	app_private_free(app->priv);
	return;
}

struct video_source_common *app_get_stream(struct app_context *app, int index)
{
	assert(app && app->priv && index >= 0 && index < MAX_VIDEO_STREAMS);
	return app->priv->streams[index];
}

ai_engine_t *app_get_ai_engine(struct app_context *app, int index)
{
	assert(app && app->priv && index >= 0);
	struct app_private *priv = app->priv;
	if(index >= priv->num_engines) return NULL;
	return priv->engines[index];
}

#if 1 || defined(TEST_APP_) && defined(_STAND_ALONE)
#include <gtk/gtk.h>
#include <gst/gst.h>
#include "video_source_common.h"
int relaunch_pipeline(struct video_source_common *video);

int main(int argc, char **argv)
{
	int rc = 0;
	setlocale(LC_ALL, "");
	gtk_init(&argc, &argv);
	gst_init(&argc, &argv);
	struct app_context *app = app_context_init(NULL, NULL);
	assert(app);
	
	struct video_source_common *video = video_source_common_init(NULL, video_frame_type_bgra, app);
	assert(video);
	//~ snprintf(video->gst_command, sizeof(video->gst_command), 
		//~ "ksvideosrc do-stats=TRUE ! videorate "
		//~ "! capsfilter name=\"caps\" caps=\"video/x-raw,width=640,height=480,framerate=5/1\" "
		//~ "! videoconvert ! appsink name=appsink");
	//~ video->settings_changed = 1;
	//~ rc = relaunch_pipeline(video);
	
	const char *stream_uri = "/dev/video0";
	if(argc > 1) stream_uri = argv[1];
	
	rc = video->init(video, stream_uri, -1, -1, &(struct framerate_fraction){10,1});
	assert(0 == rc);
	app->priv->streams[app->priv->num_streams++] = video;
	
	rc = app->init(app, argc, argv);
	assert(0 == rc);
	
	rc = app->run(app);
	
	app_context_cleanup(app);
	return rc;
}


#endif

