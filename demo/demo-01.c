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

#include "ann-plugin.h"
#include "io-input.h"
#include "input-frame.h"

#include "utils.h"
#include "da_panel.h"

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif

#define IO_PLUGIN_DEFAULT "io-plugin::input-source"
#define IO_PLUGIN_HTTPD "io-plugin::httpd"
#define IO_PLUGIN_HTTP_CLIENT   "io-plugin::httpclient"


//~ static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
//~ #define global_lock() 	pthread_mutex_lock(&g_mutex)
//~ #define global_unlock()	pthread_mutex_unlock(&g_mutex)


#define MAX_CLASSES (80)
struct class_counter
{
	char name[100];
	long counter;
};

struct ai_context;
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
	
	input_frame_t frame[1];
	int is_busy;
	int is_dirty;
	
	char * url;
	CURL * curl;
	json_tokener * jtok;
	json_object * jresult;
	enum json_tokener_error jerr;
	
	GtkTreeView * listview;
	
	ssize_t num_detected_classes;
	struct class_counter detected_classes[MAX_CLASSES];
}shell_context_t;

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data);
void shell_context_cleanup(shell_context_t * shell);
int shell_run(shell_context_t * shell);

static inline void shell_set_frame(shell_context_t * shell, const input_frame_t * frame)
{
	input_frame_copy(shell->frame, frame);
	return;
}

typedef struct ai_context
{
	io_input_t * input;
	shell_context_t * shell;
	char * server_url;
	char * video_src;
	
}ai_context_t;

static int on_new_frame(io_input_t * input, const input_frame_t * frame)
{
	shell_context_t * shell = input->user_data;
	assert(shell);
	
	pthread_mutex_lock(&shell->mutex);
	if(!shell->is_busy) {
		shell->is_busy = 1;
		shell_set_frame(shell, frame);
		pthread_cond_signal(&shell->cond);
	}
	pthread_mutex_unlock(&shell->mutex);
	return 0;
}

static ai_context_t g_ai_context[1] = {{
	.server_url = "http://127.0.0.1:9090/ai",
	.video_src = "/dev/video2",
}};
#include <getopt.h>

static void print_usuage(int argc, char ** argv)
{
	printf("Usuage: %s [--server_url=<url>] [--video_src=<rtsp://camera_ip>] \n", argv[0]);
	return;
}
int parse_args(int argc, char ** argv, ai_context_t * ctx)
{
	static struct option options[] = {
		{"server_url", required_argument, 0, 's' },	// AI server URL
		{"video_src", required_argument, 0, 'v' },	// camera(local/rtsp/http) or video file
		{"help", no_argument, 0, 'h' },
		{NULL, 0, 0, 0 },
	};
	
	while(1)
	{
		int index = 0;
		int c = getopt_long(argc, argv, "s:v:h", options, &index);
		if(c < 0) break;
		switch(c)
		{
		case 's': ctx->server_url = optarg; break;
		case 'v': ctx->video_src = optarg; break;
		case 'h': 
		default:
			print_usuage(argc, argv); exit(0);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	ai_context_t * ctx = g_ai_context;
	int rc = parse_args(argc, argv, ctx);
	assert(0 == rc);
	
	shell_context_t * shell = shell_context_init(argc, argv, ctx);
	shell->url = ctx->server_url;
	
	const char * video_src = ctx->video_src;
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	io_input_t * input = io_input_init(NULL, IO_PLUGIN_DEFAULT, shell);
	ctx->input = input;
	
	json_object * jconfig = json_object_new_object();
	assert(jconfig);
	
	json_object_object_add(jconfig, "name", json_object_new_string("input1"));
	json_object_object_add(jconfig, "uri", json_object_new_string(video_src));
	input->init(input, jconfig);
	input->on_new_frame = on_new_frame;
	input->run(input);
	
	shell_run(shell);
	shell->quit = 1;
	
	io_input_cleanup(input);
	shell_context_cleanup(shell);
	
	return 0;
}

static shell_context_t g_shell[1];
static void init_windows(shell_context_t * shell);


static size_t on_response(void * ptr, size_t size, size_t n, void * user_data)
{
	assert(user_data);
	shell_context_t * shell = user_data;
	
	json_tokener * jtok = shell->jtok;
	assert(jtok);
	
	size_t cb = size * n;
	if(0 == cb) {
		json_tokener_reset(jtok);
		return 0;
	}
	
	enum json_tokener_error jerr = json_tokener_error_parse_eof;
	json_object * jresult = json_tokener_parse_ex(jtok, (char *)ptr, cb);
	jerr = json_tokener_get_error(jtok);
	
	if(jerr == json_tokener_continue) return cb;
	
	shell->jerr = jerr;
	if(jerr == json_tokener_success) 
	{
		pthread_mutex_lock(&shell->mutex);
		if(shell->jresult) json_object_put(shell->jresult);
		shell->jresult = jresult;
		pthread_mutex_unlock(&shell->mutex);
	}
	json_tokener_reset(jtok);
	return cb;
}

#define AUTO_FREE_PTR __attribute__((cleanup(auto_free_ptr)))
static void auto_free_ptr(void * ptr)
{
	void * p = *(void **)ptr;
	if(p) {
		free(p);
		*(void **)ptr = NULL;
	}
	return;
}


int ai_request(shell_context_t * shell, const input_frame_t * frame)
{
	CURL * curl = shell->curl;
	assert(curl);
	
	AUTO_FREE_PTR unsigned char * jpeg_data = NULL;
	long cb_jpeg = bgra_image_to_jpeg_stream((bgra_image_t *)frame->bgra, &jpeg_data, 95);
	assert(cb_jpeg > 0 && jpeg_data);
	curl_easy_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, shell->url);
	
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, cb_jpeg);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jpeg_data);
	
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, shell);
	
	struct curl_slist * headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: image/jpeg");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode ret = 0;
	ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	
	if(ret != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
		return -1;
	}
	
	return 0;
}

static gboolean on_idle(shell_context_t * shell);
void * ai_thread(void * user_data)
{
	shell_context_t * shell = user_data;
	int rc = 0;
	pthread_mutex_lock(&shell->mutex);
	while(!shell->quit)
	{
		rc = pthread_cond_wait(&shell->cond, &shell->mutex);
		if(rc || shell->quit) break;
		shell->is_busy = 1;
		pthread_mutex_unlock(&shell->mutex);
		
		rc = ai_request(shell, shell->frame);
		
		pthread_mutex_lock(&shell->mutex);
		//shell->is_busy = 0;
		shell->is_dirty = (0 == rc);
		g_idle_add((GSourceFunc)on_idle, shell);
		
	}
	
	pthread_mutex_unlock(&shell->mutex);
	pthread_exit((void *)(long)rc);
}

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data)
{
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	ai_context_t * ctx = user_data;
	shell_context_t * shell = g_shell;
	
	shell->user_data = user_data;
	shell->quit = 0;
	
	int rc = 0;
	rc = pthread_mutex_init(&shell->mutex, NULL);
	if(0 == rc) rc = pthread_cond_init(&shell->cond, NULL);
	assert(0 == rc);
	
	rc = pthread_create(&shell->th, NULL, ai_thread, shell);
	assert(0 == rc);
	
	shell->curl = curl_easy_init();
	assert(shell->curl);
	shell->jtok = json_tokener_new();
	

	assert(ctx);
	ctx->shell = shell;
	
	init_windows(shell);
	return shell;
}


int shell_run(shell_context_t * shell)
{
//	shell->timer_id = g_timeout_add(200, (GSourceFunc)on_timeout, shell);
	gtk_main();
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
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "DEMO-01");

	GtkWidget * hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);

	struct da_panel * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[0] = panel;
	gtk_widget_set_size_request(panel->da, 640, 480);
	
	gtk_paned_add1(GTK_PANED(hpaned), panel->frame);
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
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	return;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult);
static gboolean on_idle(shell_context_t * shell)
{
	if(shell->quit) return G_SOURCE_REMOVE;
	
	pthread_mutex_lock(&shell->mutex);
	if(!shell->frame->data) {
		pthread_mutex_unlock(&shell->mutex);
		return G_SOURCE_REMOVE;
	}
	
	
	json_object * jresult = NULL;
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	input_frame_copy(frame, shell->frame);
	if(shell->jresult) jresult = json_object_get(shell->jresult);
	shell->is_dirty = 0;
	shell->is_busy = 0;
	pthread_mutex_unlock(&shell->mutex);
	
	
	draw_frame(shell->panels[0], frame, jresult);
	input_frame_clear(frame);
	if(jresult) json_object_put(jresult);
	return G_SOURCE_REMOVE;
}



static void clear_class_counters(struct shell_context * shell)
{
	for(ssize_t i = 0; i < shell->num_detected_classes; ++i) {
		shell->detected_classes[i].counter = 0;
	}
}

static void update_class_counter(struct shell_context * shell, const char * class_name)
{
	assert(shell && class_name);
	for(ssize_t i = 0; i < shell->num_detected_classes; ++i)  {
		if(strncasecmp(class_name, shell->detected_classes[i].name, sizeof(shell->detected_classes[i].name))) continue;
		++shell->detected_classes[i].counter;
		return;
	}
	
	assert(shell->num_detected_classes < MAX_CLASSES);
	
	struct class_counter * detected = &shell->detected_classes[shell->num_detected_classes++];
	strncpy(detected->name, class_name, sizeof(detected->name));
	++detected->counter;
}


static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
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
		shell_context_t * shell = panel->shell;
		json_object * jdetections = NULL;
		cairo_t * cr = cairo_create(surface);
		
		json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
		
		double width = frame->width;
		double height = frame->height;
		if(ok && jdetections)
		{
			int count = json_object_array_length(jdetections);
			cairo_set_line_width(cr, 2);
			cairo_set_source_rgb(cr, 1, 1, 0);
			cairo_select_font_face(cr, "Droid Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cr, 12);
			
			clear_class_counters(shell);
			
			for(int i = 0; i < count; ++i)
			{
				json_object * jdet = json_object_array_get_idx(jdetections, i);
				assert(jdet);
				const char * class_name = json_get_value(jdet, string, class);
				update_class_counter(shell, class_name);
				
				double x = json_get_value(jdet, double, left) * width;
				double y = json_get_value(jdet, double, top) * height;
				double cx = json_get_value(jdet, double, width);
			#define PERSON_MAX_WIDTH 0.85
				if(cx > PERSON_MAX_WIDTH && strcasecmp(class_name, "person") == 0) continue;	
			#undef PERSON_MAX_WIDTH
				cx *= width;
				
				double cy = json_get_value(jdet, double, height) * height;
				
				cairo_rectangle(cr, x, y, cx, cy);
				cairo_stroke(cr);
				
				cairo_move_to(cr, x, y + 15);
				cairo_show_text(cr, class_name);
			}
			
			GtkListStore * store = gtk_list_store_new(LISTVIEW_COLUMNS_count, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
			GtkTreeIter iter;
			for(ssize_t i = 0; i < shell->num_detected_classes; ++i) {
				struct class_counter * detected = &shell->detected_classes[i];
				gtk_list_store_append(store, &iter);
				gtk_list_store_set(store, &iter, LISTVIEW_COLUMN_index, (gint)i, 
					LISTVIEW_COLUMN_class_name, detected->name,
					LISTVIEW_COLUMN_counter, (gint)detected->counter, 
					-1);
			}
			gtk_tree_view_set_model(shell->listview, GTK_TREE_MODEL(store));
			g_object_unref(store);
		}
		
		cairo_destroy(cr);
	}
	
	gtk_widget_queue_draw(panel->da);
	return;
}
