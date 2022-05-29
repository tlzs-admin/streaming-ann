/*
 * demo-01.c
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

#define IO_PLUGIN_DEFAULT 	 "io-plugin::input-source"
#define AI_PLUGIN_HTTPCLIENT "ai-engine::httpclient"

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
	if(NULL == conf_file) conf_file = "demo.json";
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

static void init_windows(shell_context_t * shell)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "DEMO-01");

	struct da_panel * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[0] = panel;
	gtk_box_pack_start(GTK_BOX(vbox), panel->frame, TRUE, TRUE, 0);
	gtk_widget_set_size_request(panel->da, 640, 480);
	
	GtkWidget * label = gtk_label_new("水平反転");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), label);
	
	GtkWidget * horz_flip = gtk_switch_new();
	g_signal_connect(horz_flip, "state-set", G_CALLBACK(on_horz_flip_state_set), &shell->horz_flip);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), horz_flip);
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	return;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	assert(frame->width > 1 && frame->height > 1);
	cairo_surface_t * surface = panel->surface;
	if(NULL == panel->surface 
		|| panel->image_width != frame->width || panel->image_height != frame->height)
	{
		panel->surface = NULL;
		if(surface) cairo_surface_destroy(surface);
		
		unsigned char * data = realloc(panel->image_data, frame->width * frame->height * 4);
		assert(data);
		
		panel->image_data = data;
		panel->width = frame->width;
		panel->image_height = frame->height;
		
		surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
			frame->width, frame->height, 
			frame->width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		
		panel->surface = surface;
	}
	
	memcpy(panel->image_data, frame->data, frame->width * frame->height * 4);
	cairo_surface_mark_dirty(surface);
	
	if(jresult)
	{
		json_object * jdetections = NULL;
		cairo_t * cr = cairo_create(surface);
		
		json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
		
		double width = frame->width;
		double height = frame->height;
		if(ok && jdetections)
		{
			int count = json_object_array_length(jdetections);
			cairo_set_line_width(cr, 2);
			
			cairo_select_font_face(cr, "Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, 12);
			
			for(int i = 0; i < count; ++i)
			{
				gboolean color_parsed = FALSE;
				GdkRGBA fg_color;
				
				
				json_object * jdet = json_object_array_get_idx(jdetections, i);
				assert(jdet);
				const char * class_name = json_get_value(jdet, string, class);
				
				if(class_name) {
					json_object * jcolor = NULL;
					const char * color = NULL;
					ok = json_object_object_get_ex(shell->jcolors, class_name, &jcolor);
					if(ok && jcolor) color = json_object_get_string(jcolor);
					if(color) color_parsed = gdk_rgba_parse(&fg_color, color);
				}
				
				if(!color_parsed) fg_color = shell->default_fg;
				
				double x = json_get_value(jdet, double, left) * width;
				double y = json_get_value(jdet, double, top) * height;
				double cx = json_get_value(jdet, double, width);
				
			//~ #define PERSON_MAX_WIDTH 0.85
				//~ if(cx > PERSON_MAX_WIDTH && strcasecmp(class_name, "person") == 0) continue;
			//~ #undef PERSON_MAX_WIDTH
				cx *= width;
				
				double cy = json_get_value(jdet, double, height) * height;
				
				cairo_text_extents_t extents;
				cairo_text_extents(cr, class_name, &extents);
				cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.8);
				cairo_rectangle(cr, x - 2, y + 2 , extents.width + 4, extents.height + 4);
				cairo_fill(cr); 
				
				cairo_set_source_rgb(cr, fg_color.red, fg_color.green, fg_color.blue);
				cairo_rectangle(cr, x, y, cx, cy);
				cairo_stroke(cr);
				
				cairo_move_to(cr, x, y + 15);
				cairo_show_text(cr, class_name);
				cairo_stroke(cr);
			}
		}
		
		cairo_destroy(cr);
	}
	
	gtk_widget_queue_draw(panel->da);
	return;
}
