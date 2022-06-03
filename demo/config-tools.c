/*
 * config-tools.c
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
#include <math.h>

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <limits.h>
#include <libgen.h>

#include <gst/gst.h>
#include <gtk/gtk.h>
#include "da_panel.h"
#include "utils.h"

#include "io-input.h"
#include "ann-plugin.h"



#define IO_PLUGIN_DEFAULT "io-plugin::input-source"
#define APP_TITLE "Config Tool"
#define MAX_REGIONS (256)
#define MAX_POINTS	(16)
typedef struct point_d
{
	double x, y;
}point_d;

typedef struct region_data
{
	ssize_t count;
	point_d points[MAX_POINTS];
}region_data_t;

struct shell_context;
typedef struct global_param
{
	struct shell_context * shell;
	json_object * jconfig;
	json_object * jsettings;
	json_object * jinput;
	
	ssize_t count;
	region_data_t regions[MAX_REGIONS];
	
}global_param_t;

typedef struct shell_context
{
	void * user_data;
	GtkWidget * window;
	GtkWidget * header_bar;
	GtkWidget * uri_entry;
	GtkWidget * treeview;
	GtkWidget * statusbar;
	da_panel_t * panels[1];
	
	char app_path[PATH_MAX];
	char conf_file[PATH_MAX];

	pthread_t th;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	
	
	input_frame_t frame[1];

	int quit;
	int is_busy;
	int is_dirty;
	
	cairo_surface_t * region_masks;
	unsigned char * masks_data;
	int masks_width;
	int masks_height;
	
	int points_count;
	point_d points[MAX_POINTS];
	
	int edit_mode;
	struct {
		double x1, y1;
		double x2, y2;
	}selection;
	
	
	
}shell_context_t;
shell_context_t * shell_context_init(int argc, char ** argv, void * user_data);
int shell_run(shell_context_t * shell);
int shell_stop(shell_context_t * shell);
void shell_context_cleanup(shell_context_t * shell);

static int load_config(shell_context_t * shell, const char * conf_file);
static int save_config(shell_context_t * shell, const char * conf_file);

static global_param_t g_params[1];

static int parse_args(int argc, char ** argv, global_param_t * params)
{
	const char * conf_file = "demo-03.json";
	if(argc > 1) conf_file = argv[1];
	
	json_bool ok = FALSE;
	json_object * jconfig = json_object_from_file(conf_file);
	assert(jconfig);
	
	params->jconfig = jconfig;
	ok = json_object_object_get_ex(jconfig, "settings", &params->jsettings);
	assert(ok && params->jsettings);
	
	json_object * jsettings = params->jsettings;
	params->count = json_object_array_length(jsettings);
	if(params->count <= 0) return 0;
	
	region_data_t * regions = params->regions;
	for(int i = 0; i < params->count; ++i)
	{
		json_object * jpoints = json_object_array_get_idx(jsettings, i);
		assert(jpoints);
		int points_count = json_object_array_length(jpoints);
		if(points_count > MAX_POINTS) points_count = MAX_POINTS;
		
		regions[i].count = points_count;
		for(int ii = 0; ii < points_count; ++ii)
		{
			json_object * jpoint = json_object_array_get_idx(jpoints, ii);
			regions[i].points[ii].x = json_object_get_double(json_object_array_get_idx(jpoint, 0));
			regions[i].points[ii].y = json_object_get_double(json_object_array_get_idx(jpoint, 1));
		}
	}
	
	ok = json_object_object_get_ex(jconfig, "input", &params->jinput);
	assert(ok && params->jinput);
	
	return 0;
}

static gboolean on_idle(shell_context_t * shell);
static int on_new_frame(io_input_t * input, const input_frame_t * frame)
{
	shell_context_t * shell = input->user_data;
	assert(shell);
	global_param_t * params = shell->user_data;
	assert(params);
	
	pthread_mutex_lock(&shell->mutex);
	input_frame_copy(shell->frame, frame);
	shell->is_dirty = 1;
	pthread_mutex_unlock(&shell->mutex);
	g_idle_add((GSourceFunc)on_idle, shell);
	return 0;
}

int main(int argc, char **argv)
{
	global_param_t * ctx = g_params;
	parse_args(argc, argv, ctx);
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	shell_context_t * shell = shell_context_init(argc, argv, ctx);
	
	io_input_t * input = io_input_init(NULL, IO_PLUGIN_DEFAULT, shell);
	
	
	input->init(input, ctx->jinput);
	input->on_new_frame = on_new_frame;
	input->run(input);
	
	shell_run(shell);
	
	io_input_cleanup(input);
	return 0;
}

static shell_context_t g_shell[1];
static void init_windows(shell_context_t * shell);
shell_context_t * shell_context_init(int argc, char ** argv, void * user_data)
{
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);
	
	shell_context_t * shell = g_shell;
	shell->user_data = user_data;
	pthread_mutex_init(&shell->mutex, NULL);
	pthread_cond_init(&shell->cond, NULL);
	
	ssize_t cb = readlink("/proc/self/exe", shell->app_path, sizeof(shell->app_path));
	assert(cb > 0);
	dirname(shell->app_path);
	
	init_windows(shell);
	
	return shell;
}
int shell_run(shell_context_t * shell)
{
	shell->quit = 0;
	gtk_main();
	return 0;
}
int shell_stop(shell_context_t * shell)
{
	if(!shell->quit)
	{
		shell->quit = 1;
		gtk_main_quit();
	}
	return 0;
}
void shell_context_cleanup(shell_context_t * shell)
{
	if(NULL == shell) return;
	
	shell_stop(shell);
	pthread_mutex_destroy(&shell->mutex);
	pthread_cond_destroy(&shell->cond);
	
	return;
}

static gboolean on_draw(struct da_panel * panel, cairo_t * cr, void * user_data);
static gboolean on_key_press(struct da_panel * panel, guint keyval, guint state);
static gboolean on_key_release(struct da_panel * panel, guint keyval, guint state);
static gboolean on_button_press(struct da_panel * panel, guint button, double x, double y, guint state);
static gboolean on_button_release(struct da_panel * panel, guint button, double x, double y, guint state);
static gboolean on_mouse_move(struct da_panel * panel, double x, double y, guint state);
static gboolean on_leave_notify(struct da_panel * panel, double x, double y, guint state);

static void on_load_config(GtkWidget * button, shell_context_t * shell);
static void on_save_config(GtkWidget * button, shell_context_t * shell);

static void init_windows(shell_context_t * shell)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), APP_TITLE);
	
	GtkWidget * grid = gtk_grid_new();
	GtkWidget * uri_entry = gtk_entry_new();
	gtk_widget_set_hexpand(uri_entry, TRUE);
	GtkWidget * go_button = gtk_button_new_from_icon_name("go-jump", GTK_ICON_SIZE_BUTTON);
	gtk_grid_attach(GTK_GRID(grid), uri_entry, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), go_button, 1, 0, 1, 1);
	
	GtkWidget * hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_attach(GTK_GRID(grid), hpaned, 0, 1, 2, 1);
	
	da_panel_t * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	gtk_paned_add1(GTK_PANED(hpaned), panel->frame);
	gtk_widget_set_size_request(panel->da, 640, 480);
	
	GtkWidget * treeview = gtk_tree_view_new();
	GtkWidget * scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_vexpand(scrolled_win, TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled_win), treeview);
	
	gtk_paned_add2(GTK_PANED(hpaned), scrolled_win);
	gtk_container_add(GTK_CONTAINER(window), grid);
	gtk_widget_set_size_request(scrolled_win, 200, -1);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_win), GTK_SHADOW_ETCHED_IN);
	
	GtkWidget * button = gtk_button_new_from_icon_name("document-open", GTK_ICON_SIZE_BUTTON);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), button);
	g_signal_connect(button, "clicked", G_CALLBACK(on_load_config), shell);
	
	button = gtk_button_new_from_icon_name("document-save", GTK_ICON_SIZE_BUTTON);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), button);
	g_signal_connect(button, "clicked", G_CALLBACK(on_save_config), shell);
	
	shell->window = window;
	shell->header_bar = header_bar;
	shell->treeview = treeview;
	shell->uri_entry = uri_entry;
	shell->panels[0] = panel;
	
	panel->on_draw = on_draw;
	panel->on_button_press = on_button_press;
	panel->on_button_release = on_button_release;
	panel->on_key_press = on_key_press;
	panel->on_key_release = on_key_release;
	panel->on_mouse_move = on_mouse_move;
	panel->on_leave_notify = on_leave_notify;
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	return;
}

void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jsettings)
{
	assert(panel && frame && frame->width > 1 && frame->height > 1);
	cairo_surface_t * surface = panel->surface;
	
	if(NULL == surface || panel->image_width != frame->width || panel->image_height != frame->height)
	{
		panel->surface = NULL;
		if(surface) cairo_surface_destroy(surface);
		unsigned char * image_data = realloc(panel->image_data, frame->width * frame->height * 4);
		assert(image_data);
		surface = cairo_image_surface_create_for_data(image_data, 
			CAIRO_FORMAT_ARGB32, 
			frame->width, frame->height,
			frame->width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		panel->surface = surface;
		panel->image_data = image_data;
		panel->image_width = frame->width;
		panel->image_height = frame->height;
	}
	
	memcpy(panel->image_data, frame->data, frame->width * frame->height * 4);
	cairo_surface_mark_dirty(surface);
	
	gtk_widget_queue_draw(panel->da);
	return;
}

static gboolean on_idle(shell_context_t * shell)
{
	if(shell->quit) return G_SOURCE_REMOVE;
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	pthread_mutex_lock(&shell->mutex);
	input_frame_copy(frame, shell->frame);
	shell->is_dirty = 0;
	pthread_mutex_unlock(&shell->mutex);
	
	draw_frame(shell->panels[0], frame, NULL);
	
	return G_SOURCE_REMOVE;
}

static gboolean on_draw(struct da_panel * panel, cairo_t * cr, void * user_data)
{
	shell_context_t * shell = user_data;
	assert(shell);
	
	if(panel->width < 1|| panel->height < 1) return FALSE;
	if(NULL == panel->surface 
		|| panel->image_width < 1 || panel->image_height < 1)
	{
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_paint(cr);
		return FALSE;
	}
	
	double sx = (double)panel->width / (double)panel->image_width;
	double sy = (double)panel->height / (double)panel->image_height;
	
	cairo_save(cr);
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, panel->surface, panel->x_offset, panel->y_offset);
	cairo_paint(cr);
	cairo_restore(cr);
	//~ printf("points_count: %d\n", shell->points_count);
	
	global_param_t * params = shell->user_data;
	assert(params);
	
	printf("points_count: %d, params->count=%d\n", shell->points_count, (int)params->count);
	if(params->count > 0)
	{
		cairo_new_path(cr);
		for(ssize_t i = 0; i < params->count; ++i)
		{
			region_data_t * region = &params->regions[i];
			
			if(region->count > 0)
			{
				double x = region->points[0].x * panel->width;
				double y = region->points[0].y * panel->height;
				
				cairo_move_to(cr, x, y);
				cairo_arc(cr, x, y, 2, 0.0, M_PI * 2.0);
				for(ssize_t ii = 0; ii < region->count; ++ii)
				{
					x = region->points[ii].x * panel->width;
					y = region->points[ii].y * panel->height;
					cairo_line_to(cr, x, y);
				}
				cairo_close_path(cr);
				cairo_set_source_rgba(cr, 0, 1, 1, 0.6);
				cairo_fill_preserve(cr);
				cairo_set_source_rgba(cr, 0, 1, 1, 1);
				cairo_stroke(cr);
			}
			
		}
		
	}
	
	if(shell->points_count > 0)
	{
		cairo_new_path(cr);
		double x = shell->points[0].x * panel->width;
		double y = shell->points[0].y * panel->height;
		
		cairo_move_to(cr, x, y);
		cairo_arc(cr, x, y, 2, 0.0, M_PI * 2.0);
		for(int i = 1; i < shell->points_count; ++i)
		{
			x = shell->points[i].x * panel->width;
			y = shell->points[i].y * panel->height;
			cairo_line_to(cr, x, y);
		}
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 1, 1, 0, 0.6);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 1, 1, 0, 1);
		cairo_stroke(cr);
	}
	
	if(shell->edit_mode)
	{
		double dashes[1] = { 2 };
		cairo_set_line_width(cr, 1);
		cairo_set_source_rgba(cr, 0, 1, 0, 0.6);
		
		double x = shell->selection.x1 * panel->width;
		double y = shell->selection.y1 * panel->height;
		double cx = shell->selection.x2 * panel->width - x;
		double cy = shell->selection.y2 * panel->height - y;

	#define SIZE_THRESHOLD (5.0)
		if(fabs(cx) >= SIZE_THRESHOLD || fabs(cy) >= SIZE_THRESHOLD)
		{
			//~ cairo_rectangle(cr, x, y, cx, cy);
			//~ cairo_fill_preserve(cr);
			
			//~ cairo_set_source_rgba(cr, 0, 1, 0, 1);
			//~ cairo_set_dash(cr, dashes, 1, 0);
			//~ cairo_stroke(cr);
			
			cairo_move_to(cr, x, y);
			cairo_line_to(cr, x + cx, y + cy);
			cairo_set_source_rgba(cr, 0, 1, 0, 1);
			cairo_set_dash(cr, dashes, 1, 0);
			cairo_stroke(cr);
		}
	#undef SIZE_THRESHOLD
	}
	
	return FALSE;
}

static gboolean on_key_press(struct da_panel * panel, guint keyval, guint state)
{
	return FALSE;
}
static gboolean on_key_release(struct da_panel * panel, guint keyval, guint state)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	
	printf("keyval: %u, shift-masks: %d\n", keyval, (state & GDK_SHIFT_MASK));
	switch(keyval)
	{
	case GDK_KEY_Shift_L:
	case GDK_KEY_Shift_R:
		if(shell->edit_mode && (state & GDK_SHIFT_MASK)) {
			global_param_t * params = shell->user_data;
			assert(params);
			
			printf("points_count: %d, params->count=%d\n", shell->points_count, (int)params->count);
			
			int count = params->count;
			if(count < MAX_REGIONS && shell->points_count > 0)
			{
				++params->count;
				params->regions[count].count = shell->points_count;
				memcpy(params->regions[count].points, shell->points, shell->points_count * sizeof(point_d));
			}
			
			shell->points_count = 0;
			shell->edit_mode = 0;
			gtk_widget_queue_draw(panel->da);
		}
		break;
	default:
		break;
	}
	
	return FALSE;
}
static gboolean on_button_press(struct da_panel * panel, guint button, double x, double y, guint state)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	
	gtk_widget_grab_focus(panel->da);
	
	shell->edit_mode = 0;
	if(!(state & GDK_SHIFT_MASK)) return FALSE;
	
	
	
	shell->edit_mode = 1;
	
	if(shell->points_count == 0)
	{
		shell->selection.x1 = x / (double)panel->width;
		shell->selection.y1 = y / (double)panel->height;
		shell->selection.x2 = shell->selection.x1;
		shell->selection.y2 = shell->selection.y1;
	}else
	{
		gtk_widget_queue_draw(panel->da);
		gtk_main_iteration();
	}
	
	return FALSE;
}
static gboolean on_button_release(struct da_panel * panel, guint button, double x, double y, guint state)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	
	if(button == 3) {	// right-button
		shell->points_count = 0;
		shell->edit_mode = 0;
		gtk_widget_queue_draw(panel->da);
		return FALSE;
	}
	
	shell->edit_mode = state & GDK_SHIFT_MASK;
	if(shell->edit_mode && panel->width > 1 && panel->height > 1)
	{
		if(state & GDK_SHIFT_MASK && shell->points_count < MAX_POINTS)
		{
			int count = shell->points_count++;
			
			shell->points[count].x = x / (double)panel->width;
			shell->points[count].y = y / (double)panel->height;
			
			shell->selection.x1 = shell->points[count].x;
			shell->selection.y1 = shell->points[count].y;
			
			shell->selection.x2 = shell->points[count].x;
			shell->selection.y2 = shell->points[count].y;
		}
	}else {
		shell->points_count = 0;
	}
	
	
	//~ shell->edit_mode = 0;
	gtk_widget_queue_draw(panel->da);
	
	return FALSE;
}

static gboolean on_mouse_move(struct da_panel * panel, double x, double y, guint state)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	
	if(state & GDK_SHIFT_MASK)
	{
		shell->selection.x2 = x / (double)panel->width;
		shell->selection.y2 = y / (double)panel->height;
		gtk_widget_queue_draw(panel->da);
		while(gtk_main_iteration());
	}
	return FALSE;
}

static gboolean on_leave_notify(struct da_panel * panel, double x, double y, guint state)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	shell->edit_mode = 0;
	
	return FALSE;
}




static void on_load_config(GtkWidget * button, shell_context_t * shell)
{
	GtkWidget * dlg = gtk_file_chooser_dialog_new(APP_TITLE " - load config", GTK_WINDOW(shell->window), 
		GTK_FILE_CHOOSER_ACTION_OPEN, 
		"Load", GTK_RESPONSE_ACCEPT,
		"Cancel", GTK_RESPONSE_CANCEL,
		NULL);
	assert(dlg);
	GtkFileFilter * filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "json files");
	gtk_file_filter_add_mime_type(filter, "application/json");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);
	
	if(shell->conf_file[0] == '\0') gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), shell->app_path);
	else gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), shell->conf_file);
	
	gtk_widget_show_all(dlg);
	int rc = gtk_dialog_run(GTK_DIALOG(dlg));
	
	gchar * filename = NULL;
	switch(rc)
	{
	case GTK_RESPONSE_ACCEPT:
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		assert(filename && filename[0]);
		load_config(shell, filename);
		free(filename);
		break;
	default:
		break;
	}
	
	gtk_widget_destroy(dlg);
	return;
}

static void on_save_config(GtkWidget * button, shell_context_t * shell)
{
	GtkWidget * dlg = gtk_file_chooser_dialog_new(APP_TITLE " - save config", GTK_WINDOW(shell->window), 
		GTK_FILE_CHOOSER_ACTION_SAVE, 
		"Save as", GTK_RESPONSE_APPLY,
		"Cancel", GTK_RESPONSE_CANCEL,
		NULL);
	assert(dlg);
	GtkFileFilter * filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "json files");
	gtk_file_filter_add_mime_type(filter, "application/json");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);
	
	if(shell->conf_file[0] == '\0') gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), shell->app_path);
	else gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), shell->conf_file);
	
	gtk_widget_show_all(dlg);
	int rc = gtk_dialog_run(GTK_DIALOG(dlg));
	
	gchar * filename = NULL;
	switch(rc)
	{
	case GTK_RESPONSE_APPLY:
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		assert(filename && filename[0]);
		save_config(shell, filename);
		free(filename);
		break;
	default:
		break;
	}
	gtk_widget_destroy(dlg);
	return;
}


static int load_config(shell_context_t * shell, const char * conf_file)
{
	global_param_t * params = shell->user_data;
	assert(params);
	assert(conf_file && conf_file[0]);
	strncpy(shell->conf_file, conf_file, sizeof(shell->conf_file));
	
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), shell->conf_file);
	
	json_object * jconfig = json_object_from_file(conf_file);
	if(jconfig) {
		if(params->jconfig) {
			json_object_put(params->jconfig);
		}
		params->jconfig = jconfig;
		
		json_object * jsettings = NULL;
		json_bool ok = json_object_object_get_ex(jconfig, "settings", &jsettings);
		if(!ok || !jsettings) return -1;
		
		int count = json_object_array_length(jsettings);
		if(count > 0) 
		{
			params->count = count;
			assert(count <= MAX_REGIONS);
			region_data_t * regions = params->regions;
			for(int i = 0; i < count; ++i) {
				json_object * jpoints = json_object_array_get_idx(jsettings, i);
				region_data_t * region = &regions[i];
				region->count = json_object_array_length(jpoints);
				for(int ii = 0; ii < region->count; ++ii) {
					json_object * jpoint = json_object_array_get_idx(jpoints, ii);
					region->points[ii].x = json_object_get_double(json_object_array_get_idx(jpoint, 0));
					region->points[ii].y = json_object_get_double(json_object_array_get_idx(jpoint, 1));
				}
			}
		}
	}
	gtk_widget_queue_draw(shell->panels[0]->da);
	
	return 0;
}

static int save_config(shell_context_t * shell, const char * conf_file)
{
	global_param_t * params = shell->user_data;
	assert(params);
	
	if(NULL == conf_file) conf_file = shell->conf_file;
	else {
		strncpy(shell->conf_file, conf_file, sizeof(shell->conf_file));
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), shell->conf_file);
	}
	
	assert(conf_file && conf_file[0]);
	
	debug_printf("save to %s\n", conf_file);
	json_object * jconfig = params->jconfig;
	if(NULL == jconfig) jconfig = json_object_new_object();
	else json_object_object_del(jconfig, "settings");
	
	json_object * jsettings = json_object_new_array();
	assert(jsettings);
	json_object_object_add(jconfig, "settings", jsettings);
	
	printf("params->count: %d\n", (int)params->count);
	for(ssize_t i = 0; i < params->count; ++i)
	{
		region_data_t * region = &params->regions[i];
		if(region->count >= 3)
		{
			json_object * jpoints = json_object_new_array();
			for(ssize_t ii = 0; ii < region->count; ++ii) {
				json_object * jpoint = json_object_new_array();
				json_object_array_add(jpoint, json_object_new_double(region->points[ii].x));
				json_object_array_add(jpoint, json_object_new_double(region->points[ii].y));
				json_object_array_add(jpoints, jpoint);
			}
			json_object_array_add(jsettings, jpoints);
		}
	}
	json_object_to_file_ext(conf_file, jconfig, JSON_C_TO_STRING_SPACED);
	json_object_put(jconfig);
	
	return 0;
}

