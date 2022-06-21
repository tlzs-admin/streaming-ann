/*
 * rgb2hsv.c
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gtk/gtk.h>

#define calc_min_max(r,g,b,min,max) do { \
		if(r > max) max = r; \
		if(r < min) min = r; \
		if(g > max) max = g; \
		if(g < min) min = g; \
		if(b > max) max = b; \
		if(b < min) min = b; \
	}while(0)

static int rgb_to_hsv(const double rgb[3], double hsv[3])
{
	double r = rgb[0] * 255;
	double g = rgb[1] * 255;
	double b = rgb[2] * 255;
	double min = 9999;
	double max = -1;
	calc_min_max(r,g,b,min,max);
	
	double h = -1, s = 0, v= 0;
	s = (max <= 0.0)?0:(1.0 - min / max) * 255.0;
	v = max;
	
	double diff = max - min;
	
	if(diff != 0) {
		double factor = 60.0 / diff;
		
		if(max == r) h = factor * (g - b) + (g<b)*360.0;
		else if(max == g) h = factor * (b - r) + 120.0;
		else if(max == b) h = factor * (r - g) + 240.0;
	}
	
	hsv[0] = h;
	hsv[1] = s;
	hsv[2] = v;
	
	printf("rgb(%g,%g,%g) ==> hsv(%g,%g,%g)\n",
		r, g, b,
		h, s, v);
	return 0;
}

static int init_windows();
static int shell_run();

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	
	init_windows();
	shell_run();
	
	return 0;
}

static GtkWidget * rgb_da;
static GtkWidget * hsv_da;

static GtkSpinButton * rgb_spins[3];
static GtkSpinButton * hsv_spins[3];

static double rgb_values[3];
static double hsv_values[3];

static void on_rgb_value_changed(GtkSpinButton * spin, gpointer user_data)
{
	int index = GPOINTER_TO_INT(user_data);
	assert(index >= 0 && index < 3);
	rgb_values[index] = gtk_spin_button_get_value(spin) / 255.0;
	
	rgb_to_hsv(rgb_values, hsv_values);
	if(hsv_values[0] != -1) gtk_spin_button_set_value(hsv_spins[0], hsv_values[0]);
	gtk_spin_button_set_value(hsv_spins[1], hsv_values[1]);
	gtk_spin_button_set_value(hsv_spins[2], hsv_values[2]);

	gtk_widget_queue_draw(rgb_da);
}
static void on_hsv_value_changed(GtkSpinButton * spin, gpointer user_data)
{
	int index = GPOINTER_TO_INT(user_data);
	assert(index >= 0 && index < 3);
	hsv_values[index] = gtk_spin_button_get_value(spin) / 255.0;
	gtk_widget_queue_draw(hsv_da);
}

static gboolean on_da_rgb_draw(GtkWidget * da, cairo_t * cr, gpointer user_data)
{
	cairo_set_source_rgb(cr, rgb_values[0], rgb_values[1], rgb_values[2]);
	cairo_paint(cr);
	return FALSE;
}

static gboolean on_da_hsv_draw(GtkWidget * da, cairo_t * cr, gpointer user_data)
{
	return FALSE;
}


static gboolean on_da_draw(GtkWidget * da, cairo_t * cr, gpointer user_data)
{
	int type = GPOINTER_TO_INT(user_data);
	switch(type) {
	case 0: return on_da_rgb_draw(da, cr, NULL); 
	case 1: return on_da_hsv_draw(da, cr, NULL);
	default: break;
	}
	
	return FALSE;
}
static int init_windows()
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	
	GtkWidget * grid = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(window), grid);
	
	int row = 0;
	GtkWidget * label;
	GtkWidget * spin;
	
	static const char * label_texts[6] = {
		"R:", "G:", "B:", 
		"H:", "S:", "V:", 
	};
	for(int i = 0; i < 3; ++i) {
		label = gtk_label_new(label_texts[i]);
		spin = gtk_spin_button_new_with_range(0, 255, 1);
		gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), spin, 1, row, 2, 1);
		g_signal_connect(spin, "value-changed", G_CALLBACK(on_rgb_value_changed), GINT_TO_POINTER(i));
		rgb_spins[i] = GTK_SPIN_BUTTON(spin);
		
		label = gtk_label_new(label_texts[i+3]);
		spin = gtk_spin_button_new_with_range(0, (i==0)?360:255, 1);
		gtk_grid_attach(GTK_GRID(grid), label, 3, row, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), spin, 4, row, 2, 1);
		g_signal_connect(spin, "value-changed", G_CALLBACK(on_hsv_value_changed), GINT_TO_POINTER(i));
		hsv_spins[i] = GTK_SPIN_BUTTON(spin);
		
		++row;
	}
	
	static const char * types[2] = {
		"RGB", "HSV"
	};
	
	GtkWidget * da[2];
	for(int i = 0; i < 2; ++i) {
		GtkWidget * frame = gtk_frame_new(types[i]);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
		gtk_widget_set_hexpand(frame, TRUE);
		gtk_widget_set_vexpand(frame, TRUE);
		gtk_widget_set_size_request(frame, 320, 240);
		da[i] = gtk_drawing_area_new();
		gtk_container_add(GTK_CONTAINER(frame), da[i]);
		gtk_grid_attach(GTK_GRID(grid), frame, i*3, row, 3, 1);
		g_signal_connect(da[i], "draw", G_CALLBACK(on_da_draw), GINT_TO_POINTER(i));
	}
	
	rgb_da = da[0];
	hsv_da = da[1];

	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(window);
	return 0;
}
static int shell_run()
{
	gtk_main();
	return 0;
}



