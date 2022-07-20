/*
 * video-player3-settings.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
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

#include <math.h>

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif

#include "video-player3-settings.h"

static gboolean on_key_release(GtkWidget * da, GdkEventKey * event, struct area_settings_dialog * settings)
{
	printf("%s(keyval=%d)\n", __FUNCTION__, event->keyval);
//	if(event->keyval != GDK_KEY_Shift_L || event->keyval != GDK_KEY_Shift_R) return FALSE;

	if((event->state & GDK_SHIFT_MASK))
	{
		printf("shift key released\n");
		settings->edit_flags = 0;	// exit edit mode when 'shift' key released
		gtk_widget_queue_draw(da);
	}
	return TRUE;
}

static gboolean on_button_press(GtkWidget * da, GdkEventButton * event, struct area_settings_dialog * settings)
{
	gtk_widget_grab_focus(da);
	return FALSE;
}
static gboolean on_button_release(GtkWidget * da, GdkEventButton * event, struct area_settings_dialog * settings)
{
	printf("%s(%f, %f)\n", __FUNCTION__, event->x, event->y);
	
	double width = gtk_widget_get_allocated_width(da);
	double height = gtk_widget_get_allocated_height(da);
	if(width < 1 || height < 1) return FALSE;
	
	if(event->button != 1) return FALSE;	// only process left button clicked events
	
	int shift_flags = (event->state & GDK_SHIFT_MASK);
	if(!shift_flags) return FALSE; // shift+click ==> add points

	int area_index = 0; // todo: add multiple areas support
	struct area_setting * area = &settings->areas[area_index];
	
	if(settings->edit_flags == 0) {
		settings->edit_flags = 1;
		area->num_vertexes = 0;
	}
	
	assert(area->num_vertexes < MAX_AREA_VERTEX);
	area->vertexes[area->num_vertexes].x = event->x / width;
	area->vertexes[area->num_vertexes].y = event->y / height;
	
	printf("num_vertexes: %d, pos:(%f, %f)\n", 
		(int)area->num_vertexes,
		(double)event->x, (double)event->y);
	
	++area->num_vertexes;
	gtk_widget_queue_draw(da);
	return TRUE;
}

static inline void clear_drawing_area(cairo_t * cr, double width, double height, struct area_settings_dialog * settings)
{
	if(NULL == settings->surface || settings->image_width < 1 || settings->image_height < 1) {
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		return;
	}

	double sx = width / settings->image_width;
	double sy = height / settings->image_height;
	cairo_save(cr);
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, settings->surface, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	
	return;
}
static gboolean on_da_draw(GtkWidget * da, cairo_t * cr, struct area_settings_dialog * settings)
{
	double width = gtk_widget_get_allocated_width(da);
	double height = gtk_widget_get_allocated_height(da);
	if(width < 1 || height < 1) return FALSE;
	
	clear_drawing_area(cr, width, height, settings);
	
	int area_index = 0; // todo: add multiple areas support
	struct area_setting * area = &settings->areas[area_index];
	
	
	cairo_set_line_width(cr, 2);
	for(ssize_t i = 0; i < area->num_vertexes; ++i)
	{
		if(i == 0) {
			cairo_arc(cr, area->vertexes[i].x * width, area->vertexes[i].y * height, 3, 0, M_PI * 2);
			continue;
		}
		
		cairo_line_to(cr, area->vertexes[i].x * width, area->vertexes[i].y * height);
	}
	
	if(settings->edit_flags == 0) { // setting completed
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0, 1, 1, 0.8);
		cairo_fill_preserve(cr);
	}
	cairo_set_source_rgba(cr, 1, 1, 0, 1);
	cairo_stroke(cr);
	
	return FALSE;
}


static guint area_settings_dialog_open(struct area_settings_dialog * settings, const input_frame_t * bk_image)
{
	assert(settings->dlg);
	
	if(bk_image && bk_image->width > 1 && bk_image->height > 1) {
		cairo_surface_t * surface = settings->surface;
		if(NULL == settings->surface 
			|| bk_image->width != settings->image_width 
			|| bk_image->height != settings->image_height)
		{
			if(surface) cairo_surface_destroy(surface);
			surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bk_image->width, bk_image->height);
			assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
			settings->surface = surface;
			settings->image_width = bk_image->width;
			settings->image_height = bk_image->height;
		}
		assert(surface);
		
		unsigned char * image_data = cairo_image_surface_get_data(surface);
		memcpy(image_data, bk_image->data, bk_image->width * bk_image->height * 4);
		cairo_surface_mark_dirty(surface);
	}
	
	gtk_widget_show_all(settings->dlg);
	guint response_id = gtk_dialog_run(GTK_DIALOG(settings->dlg));
	gtk_widget_hide(settings->dlg);
	
	return response_id;
}
struct area_settings_dialog * area_settings_dialog_new(GtkWidget * window, const char * title, void * user_data)
{
	struct area_settings_dialog * settings = calloc(1, sizeof(*settings));
	assert(settings);
	
	settings->user_data = user_data;
	settings->open = area_settings_dialog_open;
	
	GtkWidget * dlg = gtk_dialog_new_with_buttons(_("Settings"), GTK_WINDOW(window), 
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, 
		_("Apply"), GTK_RESPONSE_APPLY,
		_("Cancel"), GTK_RESPONSE_CANCEL, 
		NULL);
	assert(dlg);
	
	GtkWidget * content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	GtkWidget * da = gtk_drawing_area_new();
	gint events = gtk_widget_get_events(da);
	gtk_widget_set_events(da, events 
		| GDK_KEY_PRESS_MASK
		| GDK_KEY_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_POINTER_MOTION_HINT_MASK
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_LEAVE_NOTIFY_MASK
		);
	gtk_widget_set_can_focus(da, TRUE);
	
	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_add(GTK_CONTAINER(frame), da);
	gtk_widget_set_size_request(frame, 640, 480);
	
	gtk_box_pack_start(GTK_BOX(content_area), frame, TRUE, TRUE, 0);
	g_signal_connect(da, "draw", G_CALLBACK(on_da_draw), settings);
	g_signal_connect(da, "button-press-event", G_CALLBACK(on_button_press), settings);
	g_signal_connect(da, "button-release-event", G_CALLBACK(on_button_release), settings);
	g_signal_connect(da, "key-release-event", G_CALLBACK(on_key_release), settings);

	settings->dlg = dlg;
	settings->da = da;
	
	return settings;
}

