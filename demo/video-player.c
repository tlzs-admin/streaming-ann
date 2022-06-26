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

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif

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
	
	GtkWidget * play_pause_button;
	GtkWidget * slider;
	gulong slider_update_handler;
	
	da_panel_t *panels[1];
	guint timer_id;
	long frame_number;
	int quit;
	int is_running;
	int paused;
	int is_busy;

	double volume; // [0.0, 1.5]
	gboolean is_muted;
	
	
};
struct shell_context * shell_context_init(struct shell_context * shell, void * user_data);
void shell_context_cleanup(struct shell_context * shell);

#include <getopt.h>

static void print_usuage(int argc, char ** argv)
{
	printf("Usuage: %s [--server_url=<url>] [--video_src=<rtsp://camera_ip>] \n", argv[0]);
	return;
}

static int on_end_of_stream(struct video_source2 * video, void * user_data)
{
	struct global_params * params = user_data;
	assert(params);
	struct shell_context * shell = params->shell;
	assert(shell);
	if(NULL == shell) return -1;
	
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), TRUE);
	return 0;
}

static int global_params_parse_args(struct global_params * params, int argc, char ** argv)
{
	static struct option options[] = {
		{"server_url", required_argument, 0, 's' },	// AI server URL
		{"video_src", required_argument, 0, 'v' },	// camera(local/rtsp/http) or video file
		{"width", required_argument, 0, 'W' },
		{"height", required_argument, 0, 'H' },	
		{"style", required_argument, 0, 'S'},	// css filename
		{"help", no_argument, 0, 'h' },
		{NULL, 0, 0, 0 },
	};
	
	const char * video_src = NULL;
	
	int width = 1280;
	int height = 720;
	const char * css_file = "video-player.css";
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
		case 'S': css_file = optarg; break;
		case 'h': 
		default:
			print_usuage(argc, argv); exit(0);
		}
	}
	
	if(video_src) params->video_src = strdup(video_src);
	else video_src = params->video_src;
	
	if(css_file) params->css_file = css_file;
	
	if(NULL == video_src) {
		video_src = "/dev/video0";
		params->video_src = strdup(video_src);
	}
	if(width > 0) params->width = width;
	if(height > 0) params->height = height;
	
	

	struct video_source2 * input = video_source2_init(params->input, params);
	assert(input);
	input->on_eos = on_end_of_stream;
	input->on_error = on_end_of_stream;
	
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
	if(uri && uri[0] && strcmp(uri, shell->uri) != 0) {
		strncpy(shell->uri, uri, sizeof(shell->uri));
		
		struct video_source2 * video = params->input;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), TRUE);
		shell->is_running = 0;
		video->stop(video);
		int rc = video->set_uri2(video, uri, params->width, params->height);
		rc = video->play(video);
		if(0 == rc) {
			shell->is_running = 1;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), FALSE);
		}else {
			fprintf(stderr, "invalid uri: %s\n", uri);
		}
	}
}


static void on_play_pause_toggled(GtkWidget * button, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	struct video_source2 * video = params->input;
	
	if(NULL == button) button = shell->play_pause_button;
	
	int rc = 0;
	int is_paused = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	shell->paused = is_paused;
	
	GtkWidget * icon = gtk_image_new_from_icon_name(is_paused?"media-playback-pause":"media-playback-start", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), icon);
	if(is_paused) {
		rc = video->pause(video);
		if(0 == rc) {
			gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), "paused");
		}
	}else {
		rc = video->play(video);
		if(0 == rc) {
			gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), video->uri);
			shell->paused = 0;
		}
	}
}

static void on_slider_value_changed(GtkRange * slider, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params);
	struct video_source2 * video = params->input;
	
	double value = gtk_range_get_value(slider);
	gst_element_seek_simple(video->pipeline, GST_FORMAT_TIME, 
		GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
		(gint64)(value * GST_SECOND));
	
	if(shell->paused) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), TRUE);
	}
	
	return;
}

static void on_audio_volume_changed(GtkRange * slider, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params);
	struct video_source2 * video = params->input;

	double value = gtk_range_get_value(slider);
	if(value >= 0 && value <= 1.5) {
		shell->volume = value;
		video->set_volume(video, value);
	}
	return;
}

static void on_mute_toggled(GtkToggleButton * button, struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params && params->input);
	struct video_source2 * video = params->input;
	shell->is_muted = gtk_toggle_button_get_active(button);
	
	video->set_volume(video, shell->is_muted?-1:shell->volume);
	return;
}

static void on_popup_volume_control(GtkWidget * button, struct shell_context * shell)
{
	GtkWidget * popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(popup), 1);
	
	GtkWidget * scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0.0, 1.5, 0.05);
	gtk_range_set_inverted(GTK_RANGE(scale), TRUE);
	gtk_container_add(GTK_CONTAINER(popup), scale);
	gtk_widget_set_size_request(scale, 10, 128);
	gtk_range_set_value(GTK_RANGE(scale), shell->volume);
	g_signal_connect(scale, "value-changed", G_CALLBACK(on_audio_volume_changed), shell);
	
	gtk_window_set_transient_for(GTK_WINDOW(popup), GTK_WINDOW(shell->window));
	gtk_window_set_decorated(GTK_WINDOW(popup), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(popup), TRUE);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(popup), TRUE);
	gtk_window_set_position(GTK_WINDOW(popup), GTK_WIN_POS_MOUSE);
	
	gtk_widget_set_events(popup, GDK_FOCUS_CHANGE_MASK);
	g_signal_connect(popup, "focus-out-event", G_CALLBACK(gtk_widget_destroy), NULL);
	
	gtk_widget_show_all(popup);
	gtk_widget_grab_focus(popup);
	
	return;
}

static gchar * format_timer_slider_value(GtkScale * scale, gdouble value)
{
	char buf[100] = "";
	
	int64_t value_i64 = (int64_t)value;
	int hours = value_i64 / 3600;
	int minutes = (value_i64 % 3600) / 60;
	int seconds = (value_i64 % 60);
	
	snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d", hours, minutes, seconds);
	return strdup(buf);
}

static void on_file_selection_changed(GtkFileChooser * file_chooser, struct shell_context * shell)
{
	gchar *filename = gtk_file_chooser_get_filename(file_chooser);
	if(NULL == filename) return;
	
	gtk_entry_set_text(GTK_ENTRY(shell->uri_entry), filename);
	on_uri_changed(shell->uri_entry, shell);
	g_free(filename);
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
	
	GtkWidget * file_chooser = gtk_file_chooser_button_new(_("select video file"), GTK_FILE_CHOOSER_ACTION_OPEN);
	GtkFileFilter * filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "video files");
	gtk_file_filter_add_mime_type(filter, "video/mp4");
	gtk_file_filter_add_mime_type(filter, "video/quicktime");
	gtk_file_filter_add_mime_type(filter, "video/mpeg");
	gtk_file_filter_add_mime_type(filter, "video/webm");
	gtk_file_filter_add_mime_type(filter, "video/x-matroska");
	gtk_file_filter_add_mime_type(filter, "video/x-msvideo");
	gtk_file_filter_add_mime_type(filter, "video/x-ms-wmv");
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(file_chooser), filter);
	g_signal_connect(file_chooser, "file-set", G_CALLBACK(on_file_selection_changed), shell);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), file_chooser);
	
	
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
	
	
	GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
	GtkWidget * slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.001);
	gtk_widget_set_hexpand(slider, TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_RIGHT);
	shell->slider_update_handler = g_signal_connect(slider, "value-changed", G_CALLBACK(on_slider_value_changed), shell);
	g_signal_connect(slider, "format-value", G_CALLBACK(format_timer_slider_value), shell);
	shell->slider = slider;
	
	GtkWidget * play_pause_button = gtk_toggle_button_new();
	GtkWidget * pause_icon = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);
	g_signal_connect(play_pause_button, "toggled", G_CALLBACK(on_play_pause_toggled), shell);
	
	shell->play_pause_button = play_pause_button;
	
	gtk_box_pack_start(GTK_BOX(hbox), play_pause_button, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), slider, FALSE, TRUE, 1);
	
	
	shell->volume = 0.5;
	GtkWidget * volume_icon = gtk_button_new_from_icon_name("audio-volume-medium", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(volume_icon, "clicked", G_CALLBACK(on_popup_volume_control), shell);
	gtk_box_pack_end(GTK_BOX(hbox), volume_icon, FALSE, TRUE, 1);
	
	GtkWidget * mute = gtk_toggle_button_new();
	GtkWidget * icon = gtk_image_new_from_icon_name("audio-volume-muted", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(mute), icon);
	g_signal_connect(mute, "toggled", G_CALLBACK(on_mute_toggled), shell);
	gtk_box_pack_end(GTK_BOX(hbox), mute, FALSE, TRUE, 1);
	
	
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, row++, 2, 1);
	
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
	shell->is_running = 1;
	gtk_main();
	shell->is_running = 0;
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
	if(shell->paused || shell->is_busy || !shell->is_running) return G_SOURCE_CONTINUE;
	
	struct global_params * params = shell->params;
	assert(params && params->input);
	
	struct video_source2 * input = params->input;
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
	
	static const int update_freq = 1;
	if((frame_number % update_freq) == 0) shell_update_ui(params);
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

