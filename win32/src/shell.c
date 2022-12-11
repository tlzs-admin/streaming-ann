/*
 * shell.c
 * 
 * Copyright 2022 chehw <chehw@DESKTOP-K8CSD87>
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

static gboolean on_da_draw(GtkWidget *da, cairo_t *cr, struct shell_context *shell)
{
	assert(shell && shell->priv);
	struct shell_private *priv = shell->priv;
	
	if(priv->surface && priv->image_width > 0 && priv->image_height > 0) {
		double width = gtk_widget_get_allocated_width(da);
		double height = gtk_widget_get_allocated_height(da);
		
		double sx = (double)width / priv->image_width;
		double sy = (double)height / priv->image_height;
		cairo_scale(cr, sx, sy);
		cairo_set_source_surface(cr, priv->surface, 0, 0);
		cairo_paint(cr);
		return FALSE;
	}
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_paint(cr);
	return FALSE;
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
	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, 320, 180);
	gtk_widget_set_hexpand(da, TRUE);
	gtk_widget_set_vexpand(da, TRUE);
	gtk_grid_attach(GTK_GRID(grid), da, 0, 0, 1, 1);
	
	priv->window = window;
	priv->header_bar = header_bar;
	
	priv->da = da;
	g_signal_connect(da, "draw", G_CALLBACK(on_da_draw), shell);
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
	
	if(frame && frame->data) {
		assert(frame->type == video_frame_type_bgra);
		assert(frame->length > 0);
		
		cairo_surface_t *surface = priv->surface;
		if(NULL == priv->surface || priv->image_width != frame->width || priv->image_height != frame->height)
		{
			priv->surface = NULL;
			if(surface) {
				cairo_surface_destroy(surface);
				surface = NULL;
			}
			unsigned char *bgra_data = realloc(priv->bgra_data, frame->length);
			assert(bgra_data);
			memcpy(bgra_data, frame->data, frame->length);
			
			surface = cairo_image_surface_create_for_data(bgra_data, CAIRO_FORMAT_RGB24,
				frame->width, frame->height, frame->width * 4);
			assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
			priv->surface = surface;
			priv->bgra_data = bgra_data;
			priv->image_height = frame->height;
			priv->image_width = frame->width;
		}else {
			memcpy(priv->bgra_data, frame->data, frame->length);
			cairo_surface_mark_dirty(surface);
		}
		gtk_widget_queue_draw(priv->da);
	}
	
	shell_lock(shell);
	shell->priv->busy = 0;
	shell_unlock(shell);
	return G_SOURCE_REMOVE;
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
