/*
 * video-player.c
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

#include <gtk/gtk.h>

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

#include "input-frame.h"

#include <stdint.h>
#include "video_source2.h"




struct shell_context;
struct global_params
{
	void * user_data;
	json_object * jconfig;
	const char * server_url;
	const char * css_file;
	const char * video_src;
	int width;
	int height;
	
	struct video_source2 input[1];
	struct shell_context * shell;
	
	int auto_restart;
};

struct shell_context
{
	struct global_params * params;
	
	int (* reload_config)(struct shell_context * shell, json_object * jconfig);
	int (* init)(struct shell_context * shell);
	int (* run)(struct shell_context * shell);
	int (* stop)(struct shell_context * shell);
	
	GtkWidget * window;
	GtkWidget * header_bar;
	GtkWidget * uri_entry;
	char uri[PATH_MAX];
	
	GtkWidget * slider;
	gulong slider_update_handler;
	
	da_panel_t *panels[1];
	guint timer_id;
	long frame_number;
	int quit;
	int paused;
	int is_busy;
};
struct shell_context * shell_context_init(struct shell_context * shell, void * user_data);
void shell_context_cleanup(struct shell_context * shell);

#include <getopt.h>

static void print_usuage(int argc, char ** argv)
{
	printf("Usuage: %s [--server_url=<url>] [--video_src=<rtsp://camera_ip>] \n", argv[0]);
	return;
}
static int global_params_parse_args(struct global_params * params, int argc, char ** argv)
{
	static struct option options[] = {
		{"server_url", required_argument, 0, 's' },	// AI server URL
		{"video_src", required_argument, 0, 'v' },	// camera(local/rtsp/http) or video file
		{"width", required_argument, 0, 'W' },
		{"height", required_argument, 0, 'H' },	
		{"help", no_argument, 0, 'h' },
		{NULL, 0, 0, 0 },
	};
	
	const char * video_src = NULL;
	
	int width = 1280;
	int height = 720;
	while(1)
	{
		int index = 0;
		int c = getopt_long(argc, argv, "s:v:W:H:h", options, &index);
		if(c < 0) break;
		switch(c)
		{
		case 's': params->server_url = optarg; break;
		case 'v': video_src = optarg; break;
		case 'W': width = atoi(optarg); break;
		case 'H': height = atoi(optarg); break;
		case 'h': 
		default:
			print_usuage(argc, argv); exit(0);
		}
	}
	
	if(video_src) params->video_src = strdup(video_src);
	else video_src = params->video_src;
	
	if(NULL == video_src) {
		video_src = "/dev/video0";
		params->video_src = strdup(video_src);
	}
	if(width > 0) params->width = width;
	if(height > 0) params->height = height;
	
	

	struct video_source2 * input = video_source2_init(params->input, params);
	assert(input);
	input->set_uri2(input, video_src, width, height);
	return 0;
}

static struct global_params g_params[1];



int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	gst_init(&argc, &argv);
	
	
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	
	struct global_params * params = g_params;
	int rc = global_params_parse_args(params, argc, argv);
	assert(0 == rc);
	
	struct shell_context * shell = shell_context_init(NULL, params);
	rc = shell->init(shell);
	assert(0 == rc);
	
	rc = shell->run(shell);
	shell_context_cleanup(shell);
	
	return rc;
}

/**********************************************
 * shell context
**********************************************/
static int shell_reload_config(struct shell_context * shell, json_object * jconfig);
static int shell_init(struct shell_context * shell);
static int shell_run(struct shell_context * shell);
static int shell_stop(struct shell_context * shell);
static struct shell_context g_shell[1] = {{
	.reload_config = shell_reload_config,
	.init = shell_init,
	.run = shell_run,
	.stop = shell_stop,
}};

struct shell_context * shell_context_init(struct shell_context * shell, void * user_data)
{
	if(NULL == shell) shell = g_shell;
	else {
		shell->reload_config = shell_reload_config;
		shell->init = shell_init;
		shell->run = shell_run;
		shell->stop = shell_stop;
	}
	shell->params = user_data;
	shell->params->shell = shell;
	
	return shell;
}
void shell_context_cleanup(struct shell_context * shell)
{
	
}



static int shell_reload_config(struct shell_context * shell, json_object * jconfig)
{
	// todo
	return 0;
}


static void on_uri_changed(GtkWidget * widget, struct shell_context * shell)
{
	assert(shell && shell->uri_entry);
	GtkWidget * uri_entry = shell->uri_entry;
	
	struct global_params * params = shell->params;

	const char * uri = gtk_entry_get_text(GTK_ENTRY(uri_entry));
	if(uri && uri[0] && strcmp(uri, params->video_src)) {
		struct video_source2 * video = params->input;

		shell->paused = 1;
		
		video->stop(video);
		sleep(1);
		
		int rc = video->set_uri2(video, uri, params->width, params->height);
		if(0 == rc) {
			video->play(video);
			shell->paused = 0;
		}else {
			fprintf(stderr, "invalid uri: %s\n", uri);
		}
	}
}


static void on_play_clicked(GtkWidget * button, struct shell_context * shell);
static void on_slider_value_changed(GtkRange * slider, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params);
	struct video_source2 * video = params->input;
	
	double value = gtk_range_get_value(slider);
	gst_element_seek_simple(video->pipeline, GST_FORMAT_TIME, 
		GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
		(gint64)(value * GST_SECOND));
	on_play_clicked(NULL, shell);
	return;
}

static void on_audio_volume_changed(GtkRange * slider, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params);
	struct video_source2 * video = params->input;

	double value = gtk_range_get_value(slider);
	if(value >= 0 && value <= 1.0) video->set_volume(video, value);
	return;
}

static void on_play_clicked(GtkWidget * button, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	struct video_source2 * video = params->input;
	int rc = video->play(video);
	
	if(0 == rc) {
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), video->uri);
		shell->paused = 0;
	}
	
}

static void on_pause_clicked(GtkWidget * button, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	struct video_source2 * video = params->input;
	shell->paused = 1;
	
	int rc = video->pause(video);
	if(0 == rc) {
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), "paused");
	}
}


static void on_stop_clicked(GtkWidget * button, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	struct video_source2 * video = params->input;
	shell->paused = 1;
	
	int rc = video->stop(video);
	if(0 == rc) {
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), "stopped");
	}
}

static void init_windows(struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params);
	
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
	
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * grid = gtk_grid_new();
	
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), grid);
	
	if(params->css_file) {
		GError * gerr = NULL;
		GtkCssProvider * css = gtk_css_provider_new();
		gboolean ok = gtk_css_provider_load_from_path(css, params->css_file, &gerr);
		if(!ok || gerr) {
			fprintf(stderr, "gtk_css_provider_load_from_path(%s) failed: %s\n",
				params->css_file, 
				gerr?gerr->message:"unknown error");
			if(gerr) g_error_free(gerr);
		}else
		{
			GdkScreen* screen = gtk_window_get_screen(GTK_WINDOW(window));
			gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);
		}
	}
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Media Player Demo");
	

	GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
	GtkWidget * play = gtk_button_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
	GtkWidget * stop = gtk_button_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_BUTTON);
	GtkWidget * pause = gtk_button_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox), play, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), pause, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), stop, FALSE, TRUE, 1);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), hbox);
	
	g_signal_connect(play, "clicked", G_CALLBACK(on_play_clicked), shell);
	g_signal_connect(stop, "clicked", G_CALLBACK(on_stop_clicked), shell);
	g_signal_connect(pause, "clicked", G_CALLBACK(on_pause_clicked), shell);
	
	int row = 0;
	GtkWidget * uri_entry = gtk_search_entry_new();
	shell->uri_entry = uri_entry;
	gtk_widget_set_size_request(uri_entry, 300, 36);
	gtk_grid_attach(GTK_GRID(grid), uri_entry, 0, row, 1, 1);
	gtk_widget_set_hexpand(uri_entry, TRUE);
	g_signal_connect(uri_entry, "activate", G_CALLBACK(on_uri_changed), shell);
	
	GtkWidget * go_button = gtk_button_new_from_icon_name("system-search", GTK_ICON_SIZE_BUTTON);
	gtk_grid_attach(GTK_GRID(grid), go_button, 1, row++, 1, 1);
	g_signal_connect(go_button, "clicked", G_CALLBACK(on_uri_changed), shell);

	struct da_panel * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[0] = panel;
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, row++, 2, 1);
	gtk_widget_set_size_request(panel->da, 640, 480);
	panel->keep_ratio = 1;
	
	
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
	
	
	
	GtkWidget * go_first = gtk_button_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_BUTTON);
	GtkWidget * go_prev = gtk_button_new_from_icon_name("media-seek-backward", GTK_ICON_SIZE_BUTTON);
	GtkWidget * go_next = gtk_button_new_from_icon_name("media-seek-forward", GTK_ICON_SIZE_BUTTON);
	GtkWidget * go_last = gtk_button_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_BUTTON);
	GtkWidget * repeat = gtk_button_new_from_icon_name("media-playlist-repeat", GTK_ICON_SIZE_BUTTON);
	GtkWidget * scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.001);
	gtk_widget_set_hexpand(scale, TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
	shell->slider_update_handler = g_signal_connect(scale, "value-changed", G_CALLBACK(on_slider_value_changed), shell);
	shell->slider = scale;
	gtk_grid_attach(GTK_GRID(grid), scale, 0, row++, 1, 1);
	
	gtk_box_pack_start(GTK_BOX(hbox), go_first, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), go_prev, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), go_next, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), go_last, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), repeat, FALSE, TRUE, 1);
	
	GtkWidget * volume_control = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.05);
	g_signal_connect(volume_control, "value-changed", G_CALLBACK(on_audio_volume_changed), shell);
	gtk_scale_set_value_pos(GTK_SCALE(volume_control), GTK_POS_LEFT);
	gtk_range_set_value(GTK_RANGE(volume_control), 0.5);
	gtk_scale_set_digits(GTK_SCALE(volume_control), 2);
	gtk_widget_set_size_request(volume_control, 200, -1);
	gtk_box_pack_end(GTK_BOX(hbox), volume_control, FALSE, TRUE, 1);
	
	GtkWidget * mute = gtk_button_new_from_icon_name("audio-volume-muted", GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_end(GTK_BOX(hbox), mute, FALSE, TRUE, 1);
	
	
	
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, row++, 1, 1);
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	
	gtk_widget_show_all(window);
	shell->window = window;
	shell->header_bar = header_bar;
}
static int shell_init(struct shell_context * shell)
{
	init_windows(shell);
	
	return 0;
}

static gboolean on_timeout(struct shell_context * shell);
static int shell_run(struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	
	struct video_source2 * input = params->input;
	input->play(input);
	
	static const double fps = 20;
	shell->timer_id = g_timeout_add((guint)(1000.0 / fps), (GSourceFunc)on_timeout, shell);
	gtk_main();
	
	shell->quit = 1;
	if(shell->timer_id) {
		g_source_remove(shell->timer_id);
		shell->timer_id = 0;
	}
	return 0;
}
static int shell_stop(struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	
	shell->quit = 1;
	
	struct video_source2 * video = params->input;
	video->stop(video);
	gtk_main_quit();
	return 0;
}


gboolean shell_update_ui(struct global_params * params);
static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult);
static gboolean on_timeout(struct shell_context * shell)
{
	if(shell->quit) {
		shell->timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	
	struct global_params * params = shell->params;
	assert(params && params->input);
	
	struct video_source2 * input = params->input;
	if(shell->paused || shell->is_busy) return G_SOURCE_CONTINUE;
	if(input->state < GST_STATE_PAUSED) return G_SOURCE_CONTINUE;
	
	shell->is_busy = 1;
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	
	long frame_number = input->get_frame(input, shell->frame_number, frame);
	//printf("frame_number: %ld, size=%dx%d\n", input->frame_number, input->width, input->height);
	
	if(frame_number >= 0 && frame_number != shell->frame_number) {
		shell->frame_number = frame_number;
		draw_frame(shell->panels[0], frame, NULL);
		input_frame_clear(frame);
	}
	
	shell->is_busy = 0;
	
	if((frame_number % 5) == 0) shell_update_ui(params);
	return G_SOURCE_CONTINUE;
}




static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
	if(frame->width <= 1 || frame->height <= 1) return;
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
	
	
	gtk_widget_queue_draw(panel->da);
	return;
}

gboolean shell_update_ui(struct global_params * params)
{
	assert(params && params->shell && params->input);
	struct shell_context * shell = params->shell;
	struct video_source2 * video = params->input;
	
	if(video->state < GST_STATE_PAUSED) return TRUE;
	
	//if(!GST_CLOCK_TIME_IS_VALID(video->duration)) 
	{
		if(gst_element_query_duration(video->pipeline, GST_FORMAT_TIME, &video->duration) && video->duration > 0) {
			gtk_range_set_range(GTK_RANGE(shell->slider), 0, (gdouble)video->duration / GST_SECOND);
		}else {
		//	fprintf(stderr, "query duration failed\n");
		}
	}
	
	gint64 current = -1;
	if(gst_element_query_position(video->pipeline, GST_FORMAT_TIME, &current)) {
		g_signal_handler_block(shell->slider, shell->slider_update_handler);
		gtk_range_set_value(GTK_RANGE(shell->slider), 
			(gdouble)current / GST_SECOND);
		g_signal_handler_unblock(shell->slider, shell->slider_update_handler);
	}
	
//	printf("duration: %ld, current: %ld\n", video->duration, current);
	return TRUE;
}

