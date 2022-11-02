/*
 * demo-06.c
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

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <pthread.h>

#include <json-c/json.h>
#include <curl/curl.h>
#include <libsoup/soup.h>

#include "ann-plugin.h"
#include "io-input.h"
#include "input-frame.h"
#include "ai-engine.h"

#include "utils.h"
#include "da_panel.h"
#include <getopt.h>

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif

#define IO_PLUGIN_DEFAULT 	 "io-plugin::input-source"
#define AI_PLUGIN_HTTPCLIENT "ai-engine::httpclient"

#include "classes_counter.h"

struct global_params
{
	
};


struct stream_context
{
	io_input_t input[1];
	ssize_t num_ai_engines;
	ai_engine_t ** engines;
	
	input_frame_t frame[1];
};

typedef struct shell_context
{
	void * user_data;
	GtkWidget * window;
	GtkWidget * header_bar;
	da_panel_t * panels[1];
	
	guint timer_id;
	int quit;
	
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t th;
	
	ssize_t num_streams;
	struct stream_context * streams;
	
	json_object * jcolors;
	GdkRGBA default_fg;
	
	double fps;
	long frame_number;
	int is_busy;
	int is_paused;
	
	int horz_flip;
	
	// classes counter
	GtkTreeView * listview;
	struct classes_counter_context counter_ctx[1];
}shell_context_t;

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data);
void shell_context_cleanup(shell_context_t * shell);
int shell_run(shell_context_t * shell);

int main(int argc, char **argv)
{
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	
	shell_context_t * shell = shell_context_init(argc, argv, NULL);
	assert(shell);
	
	shell_run(shell);
	
	return 0;
}

static shell_context_t g_shell[1];
static void init_windows(shell_context_t * shell);

int shell_load_config(shell_context_t * shell, const char * conf_file)
{
	int rc = 0;
	if(NULL == conf_file) conf_file = "demo-06.json";
	json_object * jconfig = json_object_from_file(conf_file);
	assert(jconfig);
	
	json_object * jstreams = NULL;
	json_bool ok = FALSE;
	ok = json_object_object_get_ex(jconfig, "streams", &jstreams);
	assert(ok && jstreams);

	ssize_t num_streams = json_object_array_length(jstreams);
	assert(num_streams > 0);
	
	struct stream_context * streams = calloc(num_streams, sizeof(*streams));
	assert(streams);
	
	shell->streams = streams;
	for(ssize_t i = 0; i < num_streams; ++i) {
		json_object * jstream = json_object_array_get_idx(jstreams, i);
		assert(jstream);
		
		json_object * jinput = NULL;
		ssize_t num_ai_engines = 0;
		json_object * jai_engines = NULL;
		
		ok = json_object_object_get_ex(jstream, "input", &jinput);
		assert(ok && jinput);
		
		ok = json_object_object_get_ex(jstream, "ai-engines", &jai_engines);
		assert(ok && jai_engines);
		
		const char * io_type = json_get_value(jinput, string, type);
		io_input_t * input = io_input_init(streams[i].input, io_type, shell);
		assert(input);
		rc = input->init(input, jinput);
		assert(0 == rc);
		
		input->run(input);
		
		num_ai_engines = json_object_array_length(jai_engines);
		ai_engine_t ** engines = calloc(num_ai_engines, sizeof(*engines));
		assert(engines);
		streams->num_ai_engines = num_ai_engines;
		streams->engines = engines;
		
		for(ssize_t ii = 0; ii < num_ai_engines; ++ii){
			json_object * jengine = json_object_array_get_idx(jai_engines, ii);
			const char * ai_type = json_get_value(jengine, string, type);
			ai_engine_t * engine = ai_engine_init(NULL, ai_type, shell);
			assert(engine);
			
			rc = engine->init(engine, jengine);
			assert(0 == rc);
			streams[i].engines[ii] = engine;
		}
	}
	
	json_object * jui = NULL;
	json_object * jcolors = NULL;
	ok = json_object_object_get_ex(jconfig, "ui", &jui);
	assert(jui);
	
	ok = json_object_object_get_ex(jui, "colors", &jcolors);
	assert(ok && jcolors);
	shell->jcolors = jcolors;
	
	const char * default_color = json_get_value_default(jcolors, string, default, "green");
	gdk_rgba_parse(&shell->default_fg, default_color);
	return 0;
}

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data)
{
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);

	shell_context_t * shell = g_shell;
	
	shell->user_data = user_data;
	shell->quit = 0;
	
	int rc = 0;
	rc = pthread_mutex_init(&shell->mutex, NULL);
	if(0 == rc) rc = pthread_cond_init(&shell->cond, NULL);
	assert(0 == rc);
	
	shell_load_config(shell, NULL);
	
	classes_counter_context_init(shell->counter_ctx, shell);
	
	init_windows(shell);
	return shell;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult);
static gboolean on_timeout(shell_context_t * shell)
{
	if(shell->quit) {
		shell->timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	if(shell->is_busy) return G_SOURCE_CONTINUE;
	
	shell->is_busy = 1;
	struct stream_context * streams = &shell->streams[0];
	assert(streams);
	
	io_input_t * input = streams[0].input;
	
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	
	long frame_number = input->get_frame(input, shell->frame_number, frame);
	if(frame_number < 0 || frame->width < 1 || frame->height < 1) {
		input_frame_clear(frame);
		shell->is_busy = 0;
		return G_SOURCE_CONTINUE;
	}
	
	shell->frame_number = frame_number;
	if(shell->horz_flip) {
		uint32_t * bgra = (uint32_t *)frame->data;
		for(int row = 0; row < frame->height; ++row) {
			for(int col = 0; col < (frame->width / 2); ++col) {
				uint32_t tmp = bgra[col];
				bgra[col] = bgra[frame->width - col - 1];
				bgra[frame->width - col - 1] = tmp;
			}
			bgra += frame->width;
		}
	}
	
	json_object * jresult = NULL;
	ai_engine_t * engine = streams->engines[0];
	assert(engine);
	engine->predict(engine, frame, &jresult);
	
	draw_frame(shell->panels[0], frame, jresult);
	if(jresult) json_object_put(jresult);
	input_frame_clear(frame);
	
	shell->is_busy = 0;
	
	return G_SOURCE_CONTINUE;
}

int shell_run(shell_context_t * shell)
{
	shell->timer_id = g_timeout_add(200, (GSourceFunc)on_timeout, shell);
	gtk_main();
	
	if(shell->timer_id) {
		g_source_remove(shell->timer_id);
		shell->timer_id = 0;
	}
	
	return 0;
}

int shell_stop(shell_context_t * shell)
{
	if(shell->timer_id)
	{
		g_source_remove(shell->timer_id);
		shell->timer_id = 0;
	}
	gtk_main_quit();
	return 0;
}

void shell_context_cleanup(shell_context_t * shell)
{
	if(shell && !shell->quit)
	{
		shell->quit = 1;
		shell_stop(shell);
	}
	
	if(shell->th)
	{
		pthread_cond_broadcast(&shell->cond);
		void * exit_code = NULL;
		int rc = pthread_join(shell->th, &exit_code);
		printf("ai_thread exited with code %ld, rc = %d\n", (long)exit_code, rc);
	}
	return;
}

static gboolean on_horz_flip_state_set(GtkSwitch * button, gboolean state, int * horz_flip)
{
	*horz_flip = state;
	return FALSE;
}

enum LISTVIEW_COLUMN
{
	LISTVIEW_COLUMN_index,
	LISTVIEW_COLUMN_class_name,
	LISTVIEW_COLUMN_counter,
	LISTVIEW_COLUMNS_count
};

static void init_listview(GtkTreeView * listview)
{
	GtkTreeViewColumn * col;
	GtkCellRenderer * cr;
	
	cr = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes(_("Class"), cr, "text", LISTVIEW_COLUMN_class_name, NULL);
	gtk_tree_view_append_column(listview, col);
	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_column_set_fixed_width(col, 120);
	
	cr = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new_with_attributes(_("Count"), cr, "text", LISTVIEW_COLUMN_counter, NULL);
	gtk_tree_view_append_column(listview, col);
	gtk_tree_view_column_set_resizable(col, TRUE);
	
	GtkListStore * store = gtk_list_store_new(LISTVIEW_COLUMNS_count, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_view_set_model(listview, GTK_TREE_MODEL(store));
	
	gtk_tree_view_set_grid_lines(listview, GTK_TREE_VIEW_GRID_LINES_BOTH);
	return;
}

static void init_windows(shell_context_t * shell)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
	GtkWidget * header_bar = gtk_header_bar_new();

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	
	gtk_container_add(GTK_CONTAINER(window), vbox);
	GtkWidget * hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "DEMO-01");

	struct da_panel * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[0] = panel;
	gtk_paned_add1(GTK_PANED(hpaned), panel->frame);
	gtk_widget_set_size_request(panel->da, 640, 480);
	gtk_paned_set_position(GTK_PANED(hpaned), 960);
	
	
	GtkWidget * scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget * listview = gtk_tree_view_new();
	init_listview(GTK_TREE_VIEW(listview));
	shell->listview = GTK_TREE_VIEW(listview);
	
	gtk_container_add(GTK_CONTAINER(scrolled_win), listview);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_win), GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_widget_set_size_request(scrolled_win, 180, -1);
	gtk_widget_set_hexpand(scrolled_win, TRUE);
	gtk_widget_set_vexpand(scrolled_win, TRUE);
	gtk_paned_add2(GTK_PANED(hpaned), scrolled_win);
	
	GtkWidget * label = gtk_label_new("水平反転");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), label);
	
	GtkWidget * horz_flip = gtk_switch_new();
	g_signal_connect(horz_flip, "state-set", G_CALLBACK(on_horz_flip_state_set), &shell->horz_flip);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), horz_flip);

	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	return;
}


static inline cairo_surface_t * update_panel_surface(da_panel_t * panel, const input_frame_t *frame)
{
	cairo_surface_t * surface = panel->surface;
	if(NULL == panel->surface 
		|| panel->image_width != frame->width || panel->image_height != frame->height)
	{
		panel->surface = NULL;
		if(surface) cairo_surface_destroy(surface);
		
		unsigned char * data = realloc(panel->image_data, frame->width * frame->height * 4);
		assert(data);
		
		panel->image_data = data;
		panel->image_width = frame->width;
		panel->image_height = frame->height;
		
		surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
			frame->width, frame->height, 
			frame->width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		
		panel->surface = surface;
	}
	
	memcpy(panel->image_data, frame->data, frame->width * frame->height * 4);
	cairo_surface_mark_dirty(surface);
	
	return surface;
}

typedef struct rect_d
{
	double x, y, cx, cy;
}rect_d;
static inline gboolean pt_in_rect(const struct rect_d rect, double x, double y)
{
	if(x < rect.x || x > (rect.x + rect.cx)) return FALSE;
	if(y < rect.y || y > (rect.y + rect.cy)) return FALSE;
	return TRUE;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	assert(frame->width > 1 && frame->height > 1);
	
	cairo_surface_t * surface = update_panel_surface(panel, frame);
	assert(surface);
	
	double width = frame->width;
	double height = frame->height;
	
	static struct rect_d detection_area[1] = {{
		.x = 0.05, 
		.y = 0.5, 
		.cx = 0.9,
		.cy = 0.5
	}};
	
	// draw detection area
	cairo_t * cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0, 1, 0, 0.2);
	cairo_set_line_width(cr, 2);
	cairo_rectangle(cr, detection_area[0].x * width, detection_area[0].y * height, 
		detection_area[0].cx *width, 
		detection_area[0].cy *height);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0, 1, 0, 0.9);
	cairo_stroke(cr);
	
	
	if(jresult)
	{
		json_object * jdetections = NULL;
		json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
		
		
		if(ok && jdetections)
		{
			int count = json_object_array_length(jdetections);
			cairo_set_line_width(cr, 2);
			
			cairo_select_font_face(cr, "Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, 12);
			
			
			struct classes_counter_context * counters = shell->counter_ctx;
			counters->reset(counters);
			
			int alert_flags = 0;
			for(int i = 0; i < count; ++i)
			{
				gboolean color_parsed = FALSE;
				GdkRGBA fg_color;
				
				
				json_object * jdet = json_object_array_get_idx(jdetections, i);
				assert(jdet);
				int class_id = json_get_value_default(jdet, int, class_index, -1);
				const char * class_name = json_get_value(jdet, string, class);
				
				if(class_id != 0) continue;	// detect persons only
				if(class_id < 0 && (!class_name || !class_name[0])) continue;
				
				struct class_counter * counter = NULL;
				if(class_id >= 0) {
					counter = counters->add_by_id(counters, class_id);
					if(counter && class_name) strncpy(counter->name, class_name, sizeof(counter->name));
				}else
				{
					counter = counters->add_by_name(counters, class_name);
					if(counter) counter->id = class_id;
				}
				assert(counter);
				
				if(class_name) {
					json_object * jcolor = NULL;
					const char * color = NULL;
					ok = json_object_object_get_ex(shell->jcolors, class_name, &jcolor);
					if(ok && jcolor) color = json_object_get_string(jcolor);
					if(color) color_parsed = gdk_rgba_parse(&fg_color, color);
				}
				
				if(!color_parsed) fg_color = shell->default_fg;
				
				double x = json_get_value(jdet, double, left);
				double y = json_get_value(jdet, double, top);
				double cx = json_get_value(jdet, double, width);
				
			//~ #define PERSON_MAX_WIDTH 0.85
				//~ if(cx > PERSON_MAX_WIDTH && strcasecmp(class_name, "person") == 0) continue;
			//~ #undef PERSON_MAX_WIDTH
				double cy = json_get_value(jdet, double, height);
				
				double center_x = x + cx / 2.0;
				double bottom_y = y + cy;
				if(pt_in_rect(detection_area[0], center_x, bottom_y)) {
					alert_flags = 1;
				}
				
				x *= width;
				y *= height;
				cx *= width;
				cy *= height;
				
				cairo_set_line_width(cr, 2);
				cairo_text_extents_t extents;
				cairo_text_extents(cr, class_name, &extents);
				cairo_set_source_rgba(cr, 1, 0, 0, 0.8);
				cairo_rectangle(cr, x - 2, y + 2 , extents.width + 4, extents.height + 4);
				cairo_fill(cr); 
				
				cairo_set_source_rgb(cr, 1, 1, 0);
				cairo_rectangle(cr, x, y, cx, cy);
				cairo_stroke(cr);
				
				cairo_move_to(cr, x, y + 15);
				cairo_show_text(cr, class_name);
				cairo_stroke(cr);
			}
			
			if(alert_flags) {
				cairo_rectangle(cr, 0, 0, width, height);
				cairo_set_source_rgba(cr, 1, 0, 0, 0.2);
				cairo_fill_preserve(cr);
			
				cairo_set_source_rgb(cr, 1, 0, 0);
				cairo_set_line_width(cr, width / 30);
				cairo_stroke(cr);
			}
			
			GtkListStore * store = gtk_list_store_new(LISTVIEW_COLUMNS_count, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
			GtkTreeIter iter;
			for(ssize_t i = 0; i < counters->num_classes; ++i) {
				struct class_counter * class = &counters->classes[i];
				gtk_list_store_append(store, &iter);
				gtk_list_store_set(store, &iter, LISTVIEW_COLUMN_index, (gint)class->id,
					LISTVIEW_COLUMN_class_name, class->name,
					LISTVIEW_COLUMN_counter, (gint)class->count, 
					-1);
			}
			gtk_tree_view_set_model(shell->listview, GTK_TREE_MODEL(store));
			g_object_unref(store);
		}
		
		
	}
	cairo_destroy(cr);
	gtk_widget_queue_draw(panel->da);
	return;
}
