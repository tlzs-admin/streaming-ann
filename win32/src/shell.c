/*
 * shell.c
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
#include <gtk/gtk.h>
#include <pthread.h>

#include "app.h"
#include "shell.h"
#include "shell_private.h"
#include "utils.h"

#include "video_source_common.h"
struct video_source_common *app_get_stream(struct app_context *app, int index);

static struct shell_private *shell_private_new(struct shell_context *shell)
{
	int rc = 0;
	struct shell_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->shell = shell;
	
	rc = pthread_mutex_init(&priv->mutex, NULL);
	assert(0 == rc);
	
	return priv;
}
static void shell_private_free(struct shell_private *priv)
{
	if(NULL == priv) return;
	if(priv->shell) priv->shell->priv = NULL;
	
	pthread_mutex_destroy(&priv->mutex);
	free(priv);
}

static json_object *shell_generate_default_config(void)
{
	json_object *jconfig = json_object_new_object();
	///< @todo
	return jconfig;
}

static int shell_init(struct shell_context *shell, json_object *jconfig)
{
	assert(shell && shell->priv);
	struct shell_private *priv = shell->priv;
	struct app_context *app = shell->app;
	
	if(NULL == jconfig) jconfig = shell_generate_default_config();
	assert(jconfig);
	
	priv->jconfig = jconfig;
	int default_width = json_get_value_default(jconfig, int, default_width, 1280);
	int default_height = json_get_value_default(jconfig, int, default_height, 720);
	
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *header_bar = gtk_header_bar_new();
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_window_set_default_size(GTK_WINDOW(window), default_width, default_height);
	
	const char *title = app?app->title:"demo";
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), _(title));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	
	GtkWidget *grid = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(window), grid);
	
	struct da_panel *panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	priv->panels[0] = panel;
	priv->num_panels = 1;
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, 0, 1, 1);
	
	priv->window = window;
	priv->header_bar = header_bar;
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell->stop), shell);
	if(shell->on_init_windows) return shell->on_init_windows(shell, shell->user_data);
	
	gtk_widget_show_all(window);
	return 0;
}

struct idle_data
{
	struct shell_context *shell;
	struct video_source_common *video;
	struct video_frame *frame;
};
struct idle_data * idle_data_new(struct shell_context *shell, struct video_source_common *video, const struct video_frame *new_frame)
{
	struct idle_data *data = calloc(1, sizeof(*data));
	assert(data);
	data->shell = shell;
	data->video = video;
	
	struct video_frame *frame = calloc(1, sizeof(*frame));
	assert(frame);
	*frame = *new_frame;
	if(new_frame->data) {
		size_t length = frame->length;
		if(0 == length) {
			assert(frame->type == video_frame_type_bgra);
			length = frame->width * frame->height * 4;
		}
		assert(length > 0);
		frame->data = malloc(length);
		assert(frame->data);
		memcpy(frame->data, new_frame->data, length);
	}
	data->frame = frame;
	return data;
}

static gboolean on_update_frame(struct idle_data *data)
{
	assert(data && data->shell);
	struct shell_context *shell = data->shell;
	struct video_frame *frame = data->frame;
	
	struct shell_private *priv = shell->priv;
	struct da_panel *panel = priv->panels[0];
	assert(panel);
	
	if(frame && frame->data) {
		assert(frame->type == video_frame_type_bgra);
		assert(frame->data && frame->length > 0);
		assert(frame->width > 0 && frame->height > 0);
		da_panel_update_frame(panel, frame->data, frame->width, frame->height);
	}
	
	shell_lock(shell);
	shell->priv->busy = 0;
	shell_unlock(shell);
	return G_SOURCE_REMOVE;
}


static void * process_frame_thread(void *user_data)
{
	int rc = 0;
	
	
	pthread_exit((void *)(intptr_t)rc);
#ifdef _WIN32
	return (void *)(intptr_t)rc;
#endif
}

static int on_new_frame(struct video_source_common *video, const struct video_frame *frame, void *user_data)
{
	struct shell_context *shell = user_data;
	assert(shell);
	
	shell_lock(shell);
	if(shell->priv->busy) {
		shell_unlock(shell);
		return 0;
	}

	shell->priv->busy = 1;
	shell_unlock(shell);
	
	struct idle_data *data = idle_data_new(shell, video, frame);
	assert(data);
	g_idle_add((GSourceFunc)on_update_frame, data);
	
	return 0;
}

static int shell_run(struct shell_context *shell)
{
	assert(shell && shell->app);
	struct video_source_common *stream = app_get_stream(shell->app, 0);
	
	stream->user_data = shell;
	stream->on_new_frame = on_new_frame;
	if(stream) stream->play(stream);
	
	gtk_main();
	return 0;
}
static int shell_stop(struct shell_context *shell)
{
	assert(shell && shell->priv);
	struct shell_private *priv = shell->priv;
	if(priv->timer_id) {
		g_source_remove(priv->timer_id);
		priv->timer_id = 0;	
		priv->quit = 1;
	}
	gtk_main_quit();
	return 0;
}

static struct shell_context g_shell[1] = {{
	.init = shell_init,
	.run = shell_run,
	.stop = shell_stop,
}};
struct shell_context *shell_context_init(struct shell_context *shell, struct app_context *app)
{
	if(NULL == shell) shell = g_shell;
	else *shell = g_shell[0];
	
	shell->app = app;
	shell->priv = shell_private_new(shell);
	
	return shell;
}
void shell_context_cleanup(struct shell_context *shell)
{
	if(NULL == shell) return;
	shell_private_free(shell->priv);
}
