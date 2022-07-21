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
#include <gtk/gtk.h>

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif

#include "video-player3-settings.h"
#include "utils.h"

struct settings_private
{
	struct area_settings_dialog * settings;
	
	GtkWidget * dlg;
	GtkWidget * parent_window;
	
	GtkWidget * da;
	cairo_surface_t * surface;
	cairo_surface_t * area_masks;
	double da_width;
	double da_height;
	double image_width;
	double image_height;

	int current_area_index;
	int points_count;
	int edit_flags;
	
	int latest_vetex;
	
	int button_pressed_flag;
	struct {
		double x, y;
	}temp_pos;
};

static inline struct area_setting *get_current(struct settings_private * priv)
{
	// todo: add multiply areas support
	return &priv->settings->areas[priv->current_area_index];
}


static gboolean on_key_release(GtkWidget * da, GdkEventKey * event, struct area_settings_dialog * settings)
{
	debug_printf("%s(keyval=%d)\n", __FUNCTION__, event->keyval);
	struct settings_private * priv = settings->priv;

//	if(event->keyval != GDK_KEY_Shift_L && event->keyval != GDK_KEY_Shift_R) return FALSE;

	if((event->state & GDK_SHIFT_MASK))
	{
		debug_printf("shift key released\n");
		priv->edit_flags = 0;	// exit edit mode when 'shift' key released
		gtk_widget_queue_draw(da);
	}
	return TRUE;
}




static gboolean on_button_press(GtkWidget * da, GdkEventButton * event, struct area_settings_dialog * settings)
{
	struct settings_private * priv = settings->priv;
	assert(priv);
	
	if(event->type == GDK_DOUBLE_BUTTON_PRESS || event->button == 3) {
		// clear current settings by double click or right click
		
		struct area_setting * area = get_current(priv);
		if(area) area->num_vertexes = 0;
		priv->edit_flags = 0;
		gtk_widget_queue_draw(da);
		return FALSE;
	}
	
	if(event->type != GDK_BUTTON_PRESS) return FALSE;
	priv->button_pressed_flag = 1;
	priv->temp_pos.x = -1;
	priv->temp_pos.y = -1;
	
	gtk_widget_grab_focus(da);
	return FALSE;
}
static gboolean on_button_release(GtkWidget * da, GdkEventButton * event, struct area_settings_dialog * settings)
{
	debug_printf("%s(%f, %f)\n", __FUNCTION__, event->x, event->y);
	struct settings_private * priv = settings->priv;
	priv->button_pressed_flag = 0;
	
	double width = priv->da_width;
	double height = priv->da_height;
	if(width < 1 || height < 1) return FALSE;
	if(event->button != 1) return FALSE;	// only process left button clicked events
	
	guint shift_key_pressed = event->state & GDK_SHIFT_MASK;
	if(!shift_key_pressed) {
		return FALSE; // shift+click ==> add points
	}
		
	int area_index = 0; // todo: add multiple areas support
	struct area_setting * area = &settings->areas[area_index];
	
	if(priv->edit_flags == 0) {
		priv->edit_flags = 1;
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


static gboolean on_mouse_move(GtkWidget * da, GdkEventMotion * event, struct area_settings_dialog * settings)
{
	struct settings_private * priv = settings->priv;
	//~ double width = priv->da_width;
	//~ double height = priv->da_height;
	//~ if(width < 1 || height < 1 || !priv->button_pressed_flag) return FALSE;
	
	double x = event->x;
	double y = event->y;
	GdkModifierType state = event->state;
	if(event->is_hint) {
		gdk_window_get_device_position_double(event->window, event->device, &x, &y, &state);
	}
	guint shift_key_pressed = event->state & GDK_SHIFT_MASK;
	if(!shift_key_pressed) {
		return FALSE; // shift+click ==> add points
	}
	
	printf("current mouse position: %d x %d\n", (int)x, (int)y);
	priv->temp_pos.x = x;
	priv->temp_pos.y = y;
	gtk_widget_queue_draw(da);
	return FALSE;
}

static inline void clear_drawing_area(cairo_t * cr, double width, double height, struct area_settings_dialog * settings)
{
	struct settings_private * priv = settings->priv;
	
	if(NULL == priv->surface || priv->image_width < 1 || priv->image_height < 1) {
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		return;
	}

	double sx = width / priv->image_width;
	double sy = height / priv->image_height;
	cairo_save(cr);
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, priv->surface, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	
	return;
}

static gboolean on_da_draw(GtkWidget * da, cairo_t * cr, struct area_settings_dialog * settings)
{
	struct settings_private * priv = settings->priv;
	
	debug_printf("%s(): da_size: %dx%d\n", __FUNCTION__, (int)priv->da_width, (int)priv->da_height);
	
	double width = priv->da_width;
	double height = priv->da_height;
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
	
	if(priv->edit_flags == 0) { // setting completed
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0, 1, 1, 0.8);
		cairo_fill_preserve(cr);
	}

	cairo_set_source_rgba(cr, 0, 0, 1, 1);
	cairo_stroke(cr);
	
	/**
	 * if on edit-mode,
	 * draw an auxiliary line according to the current position of the mouse
	 */
	if(priv->edit_flags && priv->temp_pos.x > 0 && priv->temp_pos.y > 0) {
		int i = area->num_vertexes - 1;
		if(i >= 0) {
			double dashes[] = {
				1,
				0.5
			};
			cairo_set_dash(cr, dashes, 2, 0);
			cairo_set_line_width(cr, 1);
			cairo_move_to(cr, area->vertexes[i].x * width, area->vertexes[i].y * height);
			cairo_line_to(cr, priv->temp_pos.x, priv->temp_pos.y);
			cairo_stroke(cr);
		}
	}
	return FALSE;
}


static guint area_settings_dialog_open(struct area_settings_dialog * settings, const input_frame_t * bk_image)
{
	struct settings_private * priv = settings->priv;
	assert(priv->dlg);
	
	debug_printf("%s(): bk_image: %p(size=%dx%d)\n", __FUNCTION__,
		bk_image, bk_image->width, bk_image->height);
	
	if(bk_image && bk_image->width > 1 && bk_image->height > 1) {
		cairo_surface_t * surface = priv->surface;
		if(NULL == priv->surface 
			|| bk_image->width != priv->image_width 
			|| bk_image->height != priv->image_height)
		{
			if(surface) cairo_surface_destroy(surface);
			surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bk_image->width, bk_image->height);
			assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
			priv->surface = surface;
			priv->image_width = bk_image->width;
			priv->image_height = bk_image->height;
		}
		assert(surface);
		
		unsigned char * image_data = cairo_image_surface_get_data(surface);
		memcpy(image_data, bk_image->data, bk_image->width * bk_image->height * 4);
		cairo_surface_mark_dirty(surface);
	}
	
	gtk_widget_show_all(priv->dlg);
	priv->da_width = gtk_widget_get_allocated_width(priv->da);
	priv->da_height = gtk_widget_get_allocated_height(priv->da);
	
	gtk_widget_set_sensitive(priv->parent_window, FALSE);
	
	guint response_id = gtk_dialog_run(GTK_DIALOG(priv->dlg));
	gtk_widget_hide(priv->dlg);
	
	gtk_widget_set_sensitive(priv->parent_window, TRUE);
	
	return response_id;
}


static void on_da_resize(GtkWidget * da, GdkRectangle *allocation, struct area_settings_dialog * settings)
{
	debug_printf("%s(current allocation={%d, %d, %d, %d}\n", 
		__FUNCTION__,
		allocation->x, allocation->y, allocation->width, allocation->y);
	
	struct settings_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->da_width = gtk_widget_get_allocated_width(da);
	priv->da_height = gtk_widget_get_allocated_height(da);
	
	printf("da_size: %d x %d\n", (int)priv->da_width, (int)priv->da_height);
	//gtk_widget_queue_draw(da);
	return;
}

static struct settings_private *settings_private_new(struct area_settings_dialog * settings, 
	GtkWidget * parent_window, const char * title)
{
	struct settings_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->settings = settings;
	
	priv->parent_window = parent_window;
	GtkWidget * dlg = gtk_dialog_new_with_buttons(_("Settings"), GTK_WINDOW(parent_window), 
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, 
		_("Apply"), GTK_RESPONSE_APPLY,
		_("Cancel"), GTK_RESPONSE_CANCEL, 
		NULL);
	assert(dlg);
	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(parent_window));
	
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
	g_signal_connect(da, "size-allocate", G_CALLBACK(on_da_resize), settings);
	g_signal_connect(da, "button-press-event", G_CALLBACK(on_button_press), settings);
	g_signal_connect(da, "button-release-event", G_CALLBACK(on_button_release), settings);
	g_signal_connect(da, "motion-notify-event", G_CALLBACK(on_mouse_move), settings);
	g_signal_connect(da, "key-release-event", G_CALLBACK(on_key_release), settings);

	priv->dlg = dlg;
	priv->da = da;
	
	return priv;
}

struct area_settings_dialog * area_settings_dialog_new(GtkWidget * parent_window, const char * title, void * user_data)
{
	struct area_settings_dialog * settings = calloc(1, sizeof(*settings));
	assert(settings);
	
	settings->user_data = user_data;
	settings->open = area_settings_dialog_open;
	
	struct settings_private *priv = settings_private_new(settings, parent_window, title);
	assert(priv);
	settings->priv = priv;

	return settings;
}

