/*
 * shell.c
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

#include "app.h"
#include "shell.h"
#include "shell_private.c.impl"

#include "video_streams.h"

static int shell_reload_config(struct shell_context * shell, json_object * jconfig);
static int shell_init(struct shell_context * shell);
static int shell_run(struct shell_context * shell);
static int shell_stop(struct shell_context * shell);

struct shell_context * shell_context_init(struct shell_context * shell, void * app)
{
	if(NULL == shell) shell = calloc(1, sizeof(*shell));
	assert(shell);
	
	shell->app = app;
	shell->reload_config = shell_reload_config;
	shell->init = shell_init;
	shell->run = shell_run;
	shell->stop = shell_stop;
	return shell;
}
void shell_context_cleanup(struct shell_context * shell)
{
	
}

static int shell_reload_config(struct shell_context * shell, json_object * jconfig)
{
	return 0;
}

static int shell_init(struct shell_context * shell)
{
	struct shell_private *priv = shell->priv;
	if(NULL == priv) priv = shell->priv = shell_private_new(shell);
	assert(priv);
	
	init_windows(shell);
	return 0;
}

static gboolean on_timeout(struct shell_context *shell);
static int shell_run(struct shell_context * shell)
{
	struct shell_private *priv = shell->priv;
	
	priv->timer_id = g_timeout_add(100, (GSourceFunc)on_timeout, shell);
	gtk_main();
	
	if(priv->timer_id) {
		g_source_remove(priv->timer_id);
		priv->timer_id = 0;
	}
	return 0;
}
static int shell_stop(struct shell_context * shell)
{
	gtk_main_quit();
	return 0;
}


ssize_t app_get_streams(struct app_context *app, struct video_stream **p_streams);

static void draw_frame(da_panel_t *panel, const input_frame_t *frame, json_object *jresult)
{
	if(NULL == frame || NULL == frame->data || frame->width < 1 || frame->height < 1) return;
	
	cairo_surface_t *surface = panel->surface;
	unsigned char *image_data;
	if(NULL == surface  
		|| panel->image_width != frame->width 
		|| panel->image_height != frame->height) 
	{
		if(surface) cairo_surface_destroy(surface);
		surface = panel->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame->width, frame->height);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
	}	
	image_data = cairo_image_surface_get_data(surface);
	assert(image_data);
	memcpy(image_data, frame->data, frame->width * frame->height * 4);
	cairo_surface_mark_dirty(surface);


	gtk_widget_queue_draw(panel->da);
	return;
}

static gboolean on_timeout(struct shell_context *shell)
{
	struct app_context *app = shell->app;
	struct shell_private *priv = shell->priv;
	if(priv->quit) {
		priv->timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	
	struct video_stream *streams = NULL;
	ssize_t num_streams = app_get_streams(app, &streams);
	for(int i = 0; i < num_streams; ++i) {
		input_frame_t frame[1];
		memset(frame, 0, sizeof(frame));
		
		long frame_number = streams[i].get_frame(&streams[i], 0, frame);
		if(frame_number <= 0) continue;
		
		da_panel_t *panel = priv->panels[i];
		draw_frame(panel, frame, NULL);
		
		input_frame_clear(frame);
	}
	
	
	return G_SOURCE_CONTINUE;
}
