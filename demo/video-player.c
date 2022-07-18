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
#include "ai-engine.h"

#include "utils.h"
#include "da_panel.h"

#include "input-frame.h"

#include <stdint.h>
#include "video_source2.h"
#include "classes_counter.h"

#define AI_PLUGIN_HTTPCLIENT 	"ai-engine::httpclient"

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
	
	ai_engine_t *ai;
	json_object * jui;
	
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
	GtkWidget * go_button;
	char uri[PATH_MAX];
	
	GtkWidget * slider_container;
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
	
	gboolean ai_enabled;
	
	double fps;
	json_object * jcolors;
	GdkRGBA default_fg;
	
	// classes counter
	gboolean counters_list_status;
	GtkTreeView * counters_list;
	GtkWidget * counters_list_container;
	struct classes_counter_context counter_ctx[1];
	
	GtkWidget * show_counters_menu;
	GtkWidget * fullscreen_switch_menu;
	GtkWidget * show_slider_menu;	// show/hide video control bar
	gboolean fullscreen_status;
	GtkWidget * context_menu;
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

static int on_video_status_changed(struct video_source2 * video, GstState old_state, GstState new_state, void * user_data);
static int global_params_parse_args(struct global_params * params, int argc, char ** argv)
{
	static struct option options[] = {
		{"conf_file", required_argument, 0, 'c' }, // config file
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
	const char * conf_file = "video-player.json";
	
	while(1)
	{
		int index = 0;
		int c = getopt_long(argc, argv, "c:s:v:W:H:h", options, &index);
		if(c < 0) break;
		switch(c)
		{
		case 'c': conf_file = optarg; break;
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
	
	json_object * jconfig = NULL;
	json_object * jstreams = NULL;
	json_object * jinput = NULL;
	json_object * jai_engine = NULL;
	json_bool ok = FALSE;
	if(conf_file) jconfig = json_object_from_file(conf_file);
	
	if(jconfig) {
		ok = json_object_object_get_ex(jconfig, "streams", &jstreams);
		if(ok && jstreams) {
			json_object * jstream = json_object_array_get_idx(jstreams, 0); // only use the first stream
			assert(jstream);
			
			ok = json_object_object_get_ex(jstream, "input", &jinput);
			
			json_object * jai_engines = NULL;
			ok = json_object_object_get_ex(jstream, "ai-engines", &jai_engines);
			if(ok && jai_engines) jai_engine = json_object_array_get_idx(jai_engines, 0); // only use the first ai-engine
		}
		
		(void) json_object_object_get_ex(jconfig, "ui", &params->jui);
		
	}
	
	if(NULL == video_src) {
		if(jinput) video_src = json_get_value(jinput, string, uri);
		else  video_src = "/dev/video0";
		params->video_src = strdup(video_src);
	}
	if(width > 0) params->width = width;
	if(height > 0) params->height = height;
	
	struct video_source2 * input = video_source2_init(params->input, params);
	assert(input);
	input->on_eos = on_end_of_stream;
	input->on_error = on_end_of_stream;
	input->set_uri2(input, video_src, width, height);
	input->on_state_changed = on_video_status_changed;
	
	if(jai_engine || params->server_url) {
		if(NULL == jai_engine) {
			jai_engine = json_object_new_object();
			json_object_object_add(jai_engine, "type", json_object_new_string(AI_PLUGIN_HTTPCLIENT));
		}
		if(params->server_url) {
			json_object_object_add(jai_engine, "url", json_object_new_string(params->server_url));
		}
		
		ai_engine_t * ai = ai_engine_init(NULL, AI_PLUGIN_HTTPCLIENT, params);
		assert(ai);
		int rc = ai->init(ai, jai_engine);
		assert(0 == rc);
		
		params->ai = ai;
	}
	
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

enum LISTVIEW_COLUMN
{
	LISTVIEW_COLUMN_index,
	LISTVIEW_COLUMN_class_name,
	LISTVIEW_COLUMN_counter,
	LISTVIEW_COLUMNS_count
};
static inline void reset_counters_list(GtkTreeView * listview)
{
	GtkListStore * store = gtk_list_store_new(LISTVIEW_COLUMNS_count, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
	gtk_tree_view_set_model(listview, GTK_TREE_MODEL(store));
	g_object_unref(store);
}


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
	if(uri && uri[0]) { // && strcmp(uri, shell->uri) != 0) {
		strncpy(shell->uri, uri, sizeof(shell->uri));
		
		struct video_source2 * video = params->input;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), TRUE);
		shell->is_running = 0;
		video->stop(video);
		
		// clear counters
		struct classes_counter_context * counters = shell->counter_ctx;
		if(counters && counters->clear_all) counters->clear_all(counters);
		reset_counters_list(shell->counters_list);
		
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
	
	GtkWidget * icon = gtk_image_new_from_icon_name(is_paused?"media-playback-start":"media-playback-pause", GTK_ICON_SIZE_BUTTON);
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
	video->seek(video, value);
	
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), FALSE);
	
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

static gboolean on_enable_ai_engine(GtkSwitch * switch_button, gboolean state, struct shell_context * shell)
{
	shell->ai_enabled = state;
	return FALSE;
}
static void on_show_hide_counters_list(GtkCheckMenuItem * check_menu, struct shell_context * shell)
{
	shell->counters_list_status = gtk_check_menu_item_get_active(check_menu);
	if(shell->counters_list_status) gtk_widget_show(GTK_WIDGET(shell->counters_list_container));
	else gtk_widget_hide(GTK_WIDGET(shell->counters_list_container));
	return;
}

static void init_counters_list(GtkTreeView * listview)
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
	
	gtk_tree_view_set_grid_lines(listview, GTK_TREE_VIEW_GRID_LINES_BOTH);
	reset_counters_list(listview);

	return;
}

static void add_files_filter(GtkWidget * file_chooser)
{
	GtkFileFilter * filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "video files");
	gtk_file_filter_add_mime_type(filter, "video/mp4");
	gtk_file_filter_add_mime_type(filter, "video/quicktime");
	gtk_file_filter_add_mime_type(filter, "video/mpeg");
	gtk_file_filter_add_mime_type(filter, "video/webm");
	gtk_file_filter_add_mime_type(filter, "video/x-matroska");
	gtk_file_filter_add_mime_type(filter, "video/x-msvideo");
	gtk_file_filter_add_mime_type(filter, "video/x-ms-wmv");
	gtk_file_filter_add_mime_type(filter, "application/vnd.rn-realmedia");
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(file_chooser), filter);
}

static void on_menu_open_clicked(GtkMenuItem * item, struct shell_context * shell)
{
	GtkWidget * dlg = gtk_file_chooser_dialog_new("Open video files...", GTK_WINDOW(shell->window), GTK_FILE_CHOOSER_ACTION_OPEN, 
		"Open", GTK_RESPONSE_APPLY,
		"Cancel", GTK_RESPONSE_CANCEL, 
		NULL);
	add_files_filter(dlg);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), getenv("PWD"));
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_APPLY);
	
	gint response = gtk_dialog_run(GTK_DIALOG(dlg));
	switch(response)
	{
	case GTK_RESPONSE_APPLY:
		on_file_selection_changed(GTK_FILE_CHOOSER(dlg), shell);
		break;
	case GTK_RESPONSE_CANCEL:
	default:
		break;
	}
	gtk_widget_destroy(dlg);
}

static int fullscreen_mode_switch(struct shell_context * shell);
static void on_fullscreen_switch_toggled(GtkCheckMenuItem * menu_item, struct shell_context * shell)
{
	gboolean status = gtk_check_menu_item_get_active(menu_item);
	if(status != shell->fullscreen_status) {
		fullscreen_mode_switch(shell);
	}
}
static void on_show_hide_slider_bar(GtkCheckMenuItem * menu_item, struct shell_context * shell)
{
	gboolean is_visible = gtk_check_menu_item_get_active(menu_item);
	gboolean current_status = gtk_widget_is_visible(shell->slider_container);
	if(is_visible != current_status) {
		if(is_visible) gtk_widget_show(shell->slider_container);
		else gtk_widget_hide(shell->slider_container);
	}
}

GtkWidget * create_options_menu(struct shell_context * shell)
{
	GtkWidget * menu = gtk_menu_new();
	
	GtkWidget * open = gtk_menu_item_new_with_label(_("Open video files ..."));
	g_signal_connect(open, "activate", G_CALLBACK(on_menu_open_clicked), shell);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);
	
	GtkWidget * separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * options = gtk_menu_item_new_with_label("options");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), options);
	
	GtkWidget * sub_menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(options), sub_menu);
	GtkWidget * show_counters_menu = gtk_check_menu_item_new_with_label(_("Show Counters List"));
	shell->show_counters_menu = show_counters_menu;
	gtk_menu_shell_append(GTK_MENU_SHELL(sub_menu), show_counters_menu);
	g_signal_connect(show_counters_menu, "toggled", G_CALLBACK(on_show_hide_counters_list), shell);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(show_counters_menu), TRUE);
	
	GtkWidget * show_slider_menu = gtk_check_menu_item_new_with_label(_("Show Video Control Bar"));
	shell->show_slider_menu = show_slider_menu;
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(show_slider_menu), TRUE);
	gtk_menu_shell_append(GTK_MENU_SHELL(sub_menu), show_slider_menu);
	g_signal_connect(show_slider_menu, "toggled", G_CALLBACK(on_show_hide_slider_bar), shell);
	
	
	//~ GtkWidget * enable_ai = gtk_check_menu_item_new_with_label(_("Enable AI engine"));
	//~ gtk_menu_shell_append(GTK_MENU_SHELL(sub_menu), enable_ai);

	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * fullscreen_switch_menu = gtk_check_menu_item_new_with_label(_("Full Screen"));
	g_signal_connect(fullscreen_switch_menu, "toggled", G_CALLBACK(on_fullscreen_switch_toggled), shell);
	shell->fullscreen_switch_menu = fullscreen_switch_menu;
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), fullscreen_switch_menu);
	
	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * quit =  gtk_menu_item_new_with_label(_("Quit ..."));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
	g_signal_connect_swapped(quit, "activate", G_CALLBACK(shell_stop), shell);
	
	
	gtk_widget_show_all(menu);
	return menu;
}

static void load_css(struct global_params * params, GtkWidget * window)
{
	printf("load css file: %s\n", params->css_file);
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
			gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		}
	}
}

static gboolean on_da_button_press(struct da_panel * panel, guint button, double x, double y, guint state, GdkEventButton * event)
{
	debug_printf("button: %u, pos:(%.2f,%.2f), state=%.8x\n", button, x, y, state);
	if(button == 1) gtk_widget_grab_focus(panel->da);
	
	
	struct shell_context * shell = panel->shell;
	assert(shell);
	if(button == 3) { // right button
		//~ gtk_menu_popup_at_widget(GTK_MENU(shell->context_menu), 
			//~ panel->da, 
			//~ GDK_GRAVITY_STATIC,
			//~ GDK_GRAVITY_NORTH_WEST, 
			//~ (GdkEvent *)event);
		gtk_menu_popup_at_pointer(GTK_MENU(shell->context_menu), (GdkEvent *)event);
	}
	return FALSE;
}

static gboolean on_da_key_release(struct da_panel * panel, guint keyval, guint state)
{
	
	debug_printf("key: %u, state=%.8x\n", keyval, state);
	return FALSE;
}

static int fullscreen_mode_switch(struct shell_context * shell)
{
	shell->fullscreen_status = !shell->fullscreen_status;
	if(shell->fullscreen_status) {
		gtk_window_fullscreen(GTK_WINDOW(shell->window));
		gtk_widget_hide(shell->uri_entry);
		gtk_widget_hide(shell->go_button);
	//	gtk_widget_hide(shell->slider_container);
		
	}else {
		gtk_window_unfullscreen(GTK_WINDOW(shell->window));
		gtk_widget_show(shell->uri_entry);
		gtk_widget_show(shell->go_button);
	//	gtk_widget_show(shell->slider_container);
	}
	
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(shell->fullscreen_switch_menu), shell->fullscreen_status);
	
	return 0;
}

static gboolean on_window_key_release(GtkWidget * window, GdkEventKey * event, struct shell_context * shell)
{
	debug_printf("%s()::key: %u, state=%.8x\n", __FUNCTION__, event->keyval, event->state);
	switch(event->keyval) {
	case GDK_KEY_F11:
		fullscreen_mode_switch(shell);
		return TRUE;
	case GDK_KEY_Escape:
		if(shell->fullscreen_status) fullscreen_mode_switch(shell);
		return TRUE;
	}
	return FALSE;
}

static void init_windows(struct shell_context * shell)
{
	struct global_params * params = shell->params;
	assert(params);
	
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
	g_signal_connect(window, "key-release-event", G_CALLBACK(on_window_key_release), shell);
	
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * grid = gtk_grid_new();
	
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), grid);
	
	GtkWidget * menu_button = gtk_menu_button_new();
	GtkWidget * menu_icon = gtk_image_new_from_icon_name("applications-system", GTK_ICON_SIZE_MENU);
	gtk_button_set_image(GTK_BUTTON(menu_button), menu_icon);
	
	GtkWidget * menu = create_options_menu(shell);
	shell->context_menu = menu;
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), menu_button);

	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Media Player Demo");
	
	GtkWidget * ai_switcher = gtk_switch_new();
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), ai_switcher);
	g_signal_connect(ai_switcher, "state-set", G_CALLBACK(on_enable_ai_engine), shell);
	
	//~ GtkWidget * file_chooser = gtk_file_chooser_button_new(_("select video file"), GTK_FILE_CHOOSER_ACTION_OPEN);
	//~ add_files_filter(file_chooser);
	
	//~ g_signal_connect(file_chooser, "file-set", G_CALLBACK(on_file_selection_changed), shell);
	//~ gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), file_chooser);
	
	int row = 0;
	GtkWidget * uri_entry = gtk_search_entry_new();
	shell->uri_entry = uri_entry;
	gtk_widget_set_size_request(uri_entry, 300, 36);
	gtk_grid_attach(GTK_GRID(grid), uri_entry, 0, row, 1, 1);
	gtk_widget_set_hexpand(uri_entry, TRUE);
	g_signal_connect(uri_entry, "activate", G_CALLBACK(on_uri_changed), shell);
	
	GtkWidget * go_button = gtk_button_new_from_icon_name("system-search", GTK_ICON_SIZE_BUTTON);
	shell->go_button = go_button;
	
	gtk_grid_attach(GTK_GRID(grid), go_button, 1, row, 1, 1);
	g_signal_connect(go_button, "clicked", G_CALLBACK(on_uri_changed), shell);
	++row;
	
	GtkWidget * hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_hexpand(hpaned, TRUE);
	gtk_widget_set_vexpand(hpaned, TRUE);
	gtk_grid_attach(GTK_GRID(grid), hpaned, 0, row, 2, 1);

	struct da_panel * panel = da_panel_init(NULL, 640, 360, shell);
	assert(panel);
	shell->panels[0] = panel;
	panel->keep_ratio = 1;
	gtk_widget_set_hexpand(panel->frame, TRUE);
	gtk_widget_set_vexpand(panel->frame, TRUE);
	gtk_widget_set_can_focus(panel->da, TRUE);
	panel->on_button_press = on_da_button_press;
	panel->on_key_release = on_da_key_release;
	
	gtk_paned_pack1(GTK_PANED(hpaned), panel->frame, TRUE, FALSE);
	
	GtkWidget * counters_list = gtk_tree_view_new();
	GtkWidget * scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_win), GTK_SHADOW_ETCHED_IN);
	gtk_container_add(GTK_CONTAINER(scrolled_win), counters_list);
	
	
	shell->counters_list = GTK_TREE_VIEW(counters_list);
	shell->counters_list_container = scrolled_win;
	gtk_widget_set_size_request(scrolled_win, 180, -1);
	gtk_widget_set_vexpand(scrolled_win, TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned),scrolled_win, TRUE, TRUE);
	
	init_counters_list(GTK_TREE_VIEW(counters_list));
	++row;
	
	
	GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
	shell->slider_container = hbox;
	
	GtkWidget * slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.001);
	gtk_widget_set_hexpand(slider, TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_RIGHT);
	shell->slider_update_handler = g_signal_connect(slider, "value-changed", G_CALLBACK(on_slider_value_changed), shell);
	g_signal_connect(slider, "format-value", G_CALLBACK(format_timer_slider_value), shell);
	shell->slider = slider;
	
	GtkWidget * play_pause_button = gtk_toggle_button_new();
	GtkWidget * pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
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
	
	
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, row, 2, 1);
	++row;
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	
	gtk_widget_show_all(window);
	shell->window = window;
	shell->header_bar = header_bar;
	
	load_css(params, window);
	return;
}
static int shell_init(struct shell_context * shell)
{
	init_windows(shell);
	
	struct global_params * params = shell->params;
	json_object * jcolors = NULL;
	
	if(params->jui) {
		shell->fps = json_get_value(params->jui, double, fps);
		(void)json_object_object_get_ex(params->jui, "colors", &jcolors);
	}
	
	if(NULL == jcolors) {
		jcolors = json_object_new_object();
		json_object_object_add(jcolors, "default", json_object_new_string("green"));
	}
	
	const char * default_fg = json_get_value(jcolors, string, default);
	if(NULL == default_fg) default_fg = "green";
	
	shell->jcolors = jcolors;
	gdk_rgba_parse(&shell->default_fg, default_fg);
	
	classes_counter_context_init(shell->counter_ctx, shell);
	
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
	
	if(shell->fps <= 0.1 || shell->fps > 30) shell->fps = fps;
	
	shell->timer_id = g_timeout_add((guint)(1000.0 / shell->fps), (GSourceFunc)on_timeout, shell);
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
	
	int rc = 0;
	ai_engine_t * ai = params->ai;
	json_object * jresult = NULL;
	
	if(frame_number >= 0 && frame_number != shell->frame_number) {
		shell->frame_number = frame_number;
		
		if(ai && shell->ai_enabled) {
			rc = ai->predict(ai, frame, &jresult);
			if(rc) {
				// err
			}
		}
		draw_frame(shell->panels[0], frame, jresult);
		input_frame_clear(frame);
		if(jresult) json_object_put(jresult);
	}	
	shell->is_busy = 0;
	
	static const int update_freq = 1;
	if((frame_number % update_freq) == 0) shell_update_ui(params);
	return G_SOURCE_CONTINUE;
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

struct darknet_detection
{
	long class_id;
	const char * class_name;
	double confidence;
	double x;
	double y;
	double cx;
	double cy;
};
static ssize_t darknet_detection_parse_json(json_object * jdetections, struct darknet_detection **p_detections)
{
	debug_printf("%s()...", __FUNCTION__);
	assert(jdetections && p_detections);
	
	ssize_t count = json_object_array_length(jdetections);
	if(count <= 0) return count;
	
	struct darknet_detection * dets = calloc(count, sizeof(*dets));
	assert(dets);
	
	for(ssize_t i = 0; i < count; ++i) {
		json_object * jdet = json_object_array_get_idx(jdetections, i);
		
		dets[i].class_id = json_get_value_default(jdet, int, class_index, -1);
		dets[i].class_name = json_get_value(jdet, string, class);
		dets[i].confidence = json_get_value(jdet, double, confidence);
		dets[i].x = json_get_value(jdet, double, left);
		dets[i].y = json_get_value(jdet, double, top);
		dets[i].cx = json_get_value(jdet, double, width);
		dets[i].cy = json_get_value(jdet, double, height);
		
		assert(dets[i].class_id >= 0 || dets[i].class_name != NULL);
	}
	*p_detections = dets;
	return count;
}

#define AUTO_FREE_PTR __attribute__((cleanup(auto_free_ptr)))
static void auto_free_ptr(void * ptr)
{
	void * p = *(void **)ptr;
	if(p) { free(p); *(void **)ptr = NULL; }
}
static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
	assert(panel && panel->shell);
	
	struct shell_context * shell = panel->shell;
	if(frame->width <= 1 || frame->height <= 1) return;
	assert(frame->width > 1 && frame->height > 1);
	
	cairo_surface_t * surface = update_panel_surface(panel, frame);
	assert(surface);
	
	const double font_size = (double)frame->height / 32; 
	const double line_width = (double)frame->height / 240;
	const char * font_family = "Mono"; 
	const double width = frame->width;
	const double height = frame->height;
	
	if(jresult)
	{
		json_object * jdetections = NULL;
		json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
		if(ok && jdetections)
		{
			struct classes_counter_context * counters = shell->counter_ctx;
			AUTO_FREE_PTR struct darknet_detection * dets = NULL;
			counters->reset(counters);
			ssize_t num_detections = darknet_detection_parse_json(jdetections, &dets);
			
			cairo_t * cr = cairo_create(surface);
			cairo_set_line_width(cr, line_width);
			cairo_select_font_face(cr, font_family, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, font_size);
			
			for(ssize_t i = 0; i < num_detections; ++i)
			{
				gboolean color_parsed = FALSE;
				GdkRGBA fg_color;
				
				struct class_counter * counter = NULL;
				const char *class_name = dets[i].class_name;
				
				if(dets[i].class_id >= 0) {
					counter = counters->add_by_id(counters, dets[i].class_id);
					if(counter && class_name) strncpy(counter->name, class_name, sizeof(counter->name));
				}else
				{
					counter = counters->add_by_name(counters, class_name);
					if(counter) counter->id = dets[i].class_id;
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
				
				double x = dets[i].x * width;
				double y = dets[i].y * height;
				double cx = dets[i].cx * width;
				double cy = dets[i].cy * height;
				
				// draw text background
				cairo_text_extents_t extents;
				cairo_text_extents(cr, class_name, &extents);
				cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.8);
				cairo_rectangle(cr, x - 2, y - 2, 
					extents.width + 4, 
					extents.height + extents.y_advance + 4 - extents.y_bearing);
				cairo_fill(cr); 
				
				// draw bounding box
				cairo_set_source_rgb(cr, fg_color.red, fg_color.green, fg_color.blue);
				cairo_rectangle(cr, x, y, cx, cy);
				cairo_stroke(cr);
				
				// draw label
				cairo_move_to(cr, x, y + font_size);
				cairo_show_text(cr, class_name);
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
			gtk_tree_view_set_model(shell->counters_list, GTK_TREE_MODEL(store));
			g_object_unref(store);
			
			cairo_destroy(cr);
		}
	}
	
	
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
static int on_video_status_changed(struct video_source2 * video, GstState old_state, GstState new_state, void * user_data)
{
	struct global_params * params = user_data;
	struct shell_context * shell = params->shell;
	shell_update_ui(user_data);
	

	gboolean pause_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shell->play_pause_button));
	if(pause_mode && new_state == GST_STATE_PLAYING) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shell->play_pause_button), FALSE);
	}

	return 0;
}
