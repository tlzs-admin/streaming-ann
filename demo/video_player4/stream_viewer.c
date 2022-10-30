/*
 * stream_viewer.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
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
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "utils.h"
#include "app.h"
#include "stream_viewer.h"
#include "video_streams.h"

#include "shell_private.h"

ssize_t app_get_streams(struct app_context *app, struct video_stream **p_streams);

static void apply_video_files_filter(GtkFileChooser *file_chooser)
{
	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "video files");
	gtk_file_filter_add_mime_type(filter, "video/mp4");
	gtk_file_filter_add_mime_type(filter, "video/mpeg");
	gtk_file_filter_add_mime_type(filter, "video/x-mjpeg");
	gtk_file_filter_add_mime_type(filter, "video/webm");
	gtk_file_filter_add_mime_type(filter, "video/x-matroska");
	gtk_file_chooser_set_filter(file_chooser, filter);
}


static int on_video_status_changed(struct video_source2 * video, GstState old_state, GstState new_state, void * user_data);
static void on_uri_changed(GtkWidget * widget, struct stream_viewer *viewer);
static int on_eos(struct video_source2 * video, void * user_data)
{
	struct stream_viewer *viewer = user_data;
	assert(viewer && viewer->stream);
	assert(viewer->stream->video == video);
	
	// auto restart 
	on_uri_changed(NULL, viewer);
	
	return 0;
}

static void on_uri_changed(GtkWidget * widget, struct stream_viewer *viewer)
{
	assert(viewer && viewer->shell);
	GtkWidget * uri_entry = viewer->uri_entry;
	
	struct shell_context * shell = viewer->shell;
	struct video_stream *stream = viewer->stream;
	if(NULL == stream) {
		struct video_stream *streams = NULL;
		ssize_t num_streams = app_get_streams(shell->app, &streams);
		if(num_streams > 0 && viewer->index >= 0 && viewer->index < num_streams) stream = &streams[viewer->index];
	}
	
	if(NULL == stream) return;

	const char * uri = gtk_entry_get_text(GTK_ENTRY(uri_entry));
	if(uri && uri[0]) { // && strcmp(uri, shell->uri) != 0) {
		struct video_source2 * video = stream->video;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->play_pause_button), TRUE);
		
		stream->pause(stream);
		video->stop(video);
		stream->frame_number = 0;
		
		// clear counters
		struct classes_counter_context * counters = viewer->counter_ctx;
		if(counters && counters->clear_all) counters->clear_all(counters);
		
		int rc = video->set_uri2(video, uri, stream->image_width, stream->image_height);
		video->user_data = viewer;
		video->on_state_changed = on_video_status_changed;
		video->on_eos = on_eos;
		
		rc = video->play(video);
		rc = stream->run(stream);
		
		if(0 == rc) {
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->play_pause_button), FALSE);
		}else {
			fprintf(stderr, "invalid uri: %s\n", uri);
		}
	}
}

static void on_file_selection_changed(GtkFileChooser *file_chooser, struct stream_viewer *viewer)
{
	gchar *filename = gtk_file_chooser_get_filename(file_chooser);
	if(NULL == filename) return;
	
	gtk_entry_set_text(GTK_ENTRY(viewer->uri_entry), filename);
	on_uri_changed(viewer->uri_entry, viewer);
	g_free(filename);
}

static void on_menu_open_clicked(GtkMenuItem *item, struct stream_viewer *viewer)
{
	struct shell_context *shell = viewer->shell;
	struct shell_private *priv = shell->priv;
	
	static GtkWidget *dlg = NULL;
	
	if(NULL == dlg) {
		dlg = gtk_file_chooser_dialog_new(_("Open ..."), GTK_WINDOW(priv->window), 
			GTK_FILE_CHOOSER_ACTION_OPEN, 
			_("Open"), GTK_RESPONSE_OK, 
			_("Cancel"), GTK_RESPONSE_CANCEL,
			NULL);
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), shell->app->work_dir);
		apply_video_files_filter(GTK_FILE_CHOOSER(dlg));
	}
	
	gtk_widget_show_all(dlg);
	guint response_id = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_hide(dlg);
	
	
	if(response_id == GTK_RESPONSE_OK) {
		on_file_selection_changed(GTK_FILE_CHOOSER(dlg), viewer);
	}
	return;
}

static gboolean on_da_button_press(struct da_panel * panel, guint button, double x, double y, guint state, GdkEventButton * event)
{
	debug_printf("button: %u, pos:(%.2f,%.2f), state=%.8x\n", button, x, y, state);
	if(button == 1) gtk_widget_grab_focus(panel->da);
	
	
	struct stream_viewer * viewer = panel->shell;
	assert(viewer);
	if(button == 3) { // right button
		//~ gtk_menu_popup_at_widget(GTK_MENU(shell->context_menu), 
			//~ panel->da, 
			//~ GDK_GRAVITY_STATIC,
			//~ GDK_GRAVITY_NORTH_WEST, 
			//~ (GdkEvent *)event);
		gtk_menu_popup_at_pointer(GTK_MENU(viewer->context_menu), (GdkEvent *)event);
	}
	return FALSE;
}

static void on_check_menu_toggled_int_value(GtkCheckMenuItem *item, int *p_value)
{
	if(p_value) *p_value =  gtk_check_menu_item_get_active(item);
}
static void on_show_hide_toolbars_toggled(GtkCheckMenuItem *item, struct stream_viewer *viewer)
{
	gboolean active = gtk_check_menu_item_get_active(item);
	gtk_widget_set_visible(viewer->hbox[0], active);
	gtk_widget_set_visible(viewer->hbox[1], active);
}


int fullscreen_mode_switch(struct shell_context * shell);
static void on_stream_viewer_fullscreen_mode_switch(GtkCheckMenuItem *item, struct stream_viewer *viewer)
{
	struct shell_context *shell = viewer->shell;
	struct shell_private *priv = shell->priv;
	
	priv->fullscreen_viewer_index = viewer->index;
	fullscreen_mode_switch(shell);
	return;
}

static void on_area_settings_menu_clicked(GtkMenuItem *item, struct stream_viewer *viewer)
{
	struct area_settings_dialog * dlg = viewer->settings_dlg;
	assert(dlg);
	
	struct video_stream *stream = viewer->stream;
	if(stream) {
		input_frame_t frame[1];
		memset(frame, 0, sizeof(frame));
		
		long frame_number = stream->get_frame(stream, -1, frame);
		if(frame_number > 0) {
			long response_id = dlg->open(dlg, frame);
			printf("response_id: %ld\n", response_id);
		}
		input_frame_clear_all(frame);
	}	
	
	return;
}

void on_app_settings_button_clicked(GtkWidget *button, struct shell_context *shell);
GtkWidget * create_options_menu(struct stream_viewer * viewer)
{
	struct shell_context *shell = viewer->shell;
	struct video_stream * stream = viewer->stream;
	GtkWidget * menu = gtk_menu_new();
	
	GtkWidget * open = gtk_menu_item_new_with_label(_("Open ..."));
	g_signal_connect(open, "activate", G_CALLBACK(on_menu_open_clicked), viewer);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);
	
	
	/* App settings */
	GtkWidget * separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * settings_menu = gtk_menu_item_new_with_label(_("Settings"));
	GtkWidget * settings_submenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(settings_menu), settings_submenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings_menu);
	
	GtkWidget * app_settings = gtk_menu_item_new_with_label(_("Global"));
	g_signal_connect(app_settings, "activate", G_CALLBACK(on_app_settings_button_clicked), shell);
	gtk_menu_shell_append(GTK_MENU_SHELL(settings_submenu), app_settings);
	
	/* area settings */
	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(settings_submenu), separator);
	
	GtkWidget * area_settings = gtk_menu_item_new_with_label(_("Area Settings"));
	g_signal_connect(area_settings, "activate", G_CALLBACK(on_area_settings_menu_clicked), viewer);
	gtk_menu_shell_append(GTK_MENU_SHELL(settings_submenu), area_settings);
	
	/* view */
	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * view_menu = gtk_menu_item_new_with_label(_("view"));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), view_menu);
	GtkWidget * view_submenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_menu), view_submenu);
	
	GtkWidget * show_settings = gtk_check_menu_item_new_with_label(_("Show settings area"));
	gtk_menu_shell_append(GTK_MENU_SHELL(view_submenu), show_settings);
	g_signal_connect(show_settings, "toggled", G_CALLBACK(on_check_menu_toggled_int_value), &viewer->show_area_settings);
	
	GtkWidget * show_counters_menu = gtk_check_menu_item_new_with_label(_("Show Counters List"));
	gtk_menu_shell_append(GTK_MENU_SHELL(view_submenu), show_counters_menu);
	g_signal_connect(show_counters_menu, "toggled", G_CALLBACK(on_check_menu_toggled_int_value), &viewer->show_counters);
	
	GtkWidget * show_toolbars_menu = gtk_check_menu_item_new_with_label(_("Show toolbars"));
	gtk_menu_shell_append(GTK_MENU_SHELL(view_submenu), show_toolbars_menu);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(show_toolbars_menu), TRUE);
	g_signal_connect(show_toolbars_menu, "toggled", G_CALLBACK(on_show_hide_toolbars_toggled), viewer);
	viewer->show_toolbars_menu = show_toolbars_menu;
	
	/* ai menu */
	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * ai_menu = gtk_menu_item_new_with_label(_("AI engine"));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), ai_menu);
	
	GtkWidget * ai_submenu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(ai_menu), ai_submenu);
	
	GtkWidget * enable_ai = gtk_check_menu_item_new_with_label(_("Enable AI engine"));
	if(stream) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(enable_ai), stream->ai_enabled);
	g_signal_connect(enable_ai, "toggled", G_CALLBACK(on_check_menu_toggled_int_value), &stream->ai_enabled);
	gtk_menu_shell_append(GTK_MENU_SHELL(ai_submenu), enable_ai);
	
	GtkWidget * face_masking = gtk_check_menu_item_new_with_label(_("Masking Face"));
	g_signal_connect(face_masking, "toggled", G_CALLBACK(on_check_menu_toggled_int_value), &stream->face_masking_flag);
	gtk_menu_shell_append(GTK_MENU_SHELL(ai_submenu), face_masking);

	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * fullscreen_switch_menu = gtk_menu_item_new_with_label(_("Full Screen"));
	g_signal_connect(fullscreen_switch_menu, "activate", G_CALLBACK(on_stream_viewer_fullscreen_mode_switch), viewer);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), fullscreen_switch_menu);
	
	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * quit =  gtk_menu_item_new_with_label(_("Quit ..."));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
	g_signal_connect_swapped(quit, "activate", G_CALLBACK(shell->stop), viewer->shell);
	
	
	gtk_widget_show_all(menu);
	return menu;
}

static void on_play_pause_toggled(GtkWidget * button, struct stream_viewer * viewer)
{
	assert(viewer && viewer->shell);
	if(NULL == viewer->stream) return;
	struct video_stream *stream = viewer->stream;
	if(NULL == stream->video) return;
	struct video_source2 *video = stream->video;
	
	if(NULL == button) button = viewer->play_pause_button;
	
	int rc = 0;
	int is_paused = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	
	GtkWidget * icon = gtk_image_new_from_icon_name(is_paused?"media-playback-start":"media-playback-pause", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button), icon);
	if(is_paused) {
		rc = video->pause(video);
		stream->pause(stream);
		if(0 == rc) {
			gtk_header_bar_set_subtitle(GTK_HEADER_BAR(viewer->shell->priv->header_bar), "paused");
		}
	}else {
		rc = video->play(video);
		stream->run(stream);
		if(0 == rc) {
			gtk_header_bar_set_subtitle(GTK_HEADER_BAR(viewer->shell->priv->header_bar), video->uri);
		}
	}
}

static void on_slider_value_changed(GtkRange *slider, struct stream_viewer *viewer)
{
	assert(viewer);
	struct video_stream *stream = viewer->stream;
	if(NULL == stream || NULL == stream->video) return;
	struct video_source2 *video = stream->video;
	
	double value = gtk_range_get_value(slider);
	video->seek(video, value);
	
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->play_pause_button), FALSE);
	return;
}


static gchar * format_timer_slider_value(GtkScale * scale, gdouble value, struct stream_viewer *viewer)
{
	char buf[100] = "";
	
	int64_t value_i64 = (int64_t)value;
	int hours = value_i64 / 3600;
	int minutes = (value_i64 % 3600) / 60;
	int seconds = (value_i64 % 60);
	
	snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d", hours, minutes, seconds);
	return strdup(buf);
}

gboolean stream_viewer_update_ui(struct stream_viewer * viewer)
{
	assert(viewer && viewer->shell && viewer->stream);
	struct video_stream *stream = viewer->stream;
	struct video_source2 *video = stream->video;
	if(NULL == video) return FALSE;
	
	if(video->state < GST_STATE_PAUSED) return TRUE;
	
	//if(!GST_CLOCK_TIME_IS_VALID(video->duration)) 
	{
		if(gst_element_query_duration(video->pipeline, GST_FORMAT_TIME, &video->duration) && video->duration > 0) {
			gtk_range_set_range(GTK_RANGE(viewer->slider), 0, (gdouble)video->duration / GST_SECOND);
		}else {
		//	fprintf(stderr, "query duration failed\n");
		}
	}
	
	gint64 current = -1;
	if(gst_element_query_position(video->pipeline, GST_FORMAT_TIME, &current)) {
		g_signal_handler_block(viewer->slider, viewer->slider_update_handler);
		gtk_range_set_value(GTK_RANGE(viewer->slider), 
			(gdouble)current / GST_SECOND);
		g_signal_handler_unblock(viewer->slider, viewer->slider_update_handler);
	}
	
//	printf("duration: %ld, current: %ld\n", video->duration, current);
	return TRUE;
}

static int on_video_status_changed(struct video_source2 * video, GstState old_state, GstState new_state, void * user_data)
{
	struct stream_viewer *viewer = user_data;
	assert(viewer && viewer->stream);
	assert(viewer->stream->video == video);
	
	stream_viewer_update_ui(user_data);
	gboolean pause_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(viewer->play_pause_button));
	if(pause_mode && new_state == GST_STATE_PLAYING) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->play_pause_button), FALSE);
	}

	return 0;
}

struct stream_viewer * stream_viewer_init(struct stream_viewer *viewer, int index, int image_width, int image_height, struct shell_context *shell)
{
	assert(shell && shell->app);
	if(NULL == viewer) viewer = calloc(1, sizeof(*viewer));
	assert(viewer);
	
	viewer->shell = shell;
	viewer->index = index;
	
	classes_counter_context_init(viewer->counter_ctx, viewer);
	viewer->settings_dlg = area_settings_dialog_new(shell->priv->window, "area settings", viewer);
	
	struct video_stream *stream = viewer->stream;
	if(NULL == stream) {
		struct video_stream *streams = NULL;
		ssize_t num_streams = app_get_streams(shell->app, &streams);
		if(num_streams > 0 && viewer->index >= 0 && viewer->index < num_streams) {
			stream = &streams[viewer->index];
			viewer->stream = stream;
		}
	}
	assert(stream);
	
	struct video_source2 *video = stream->video;
	const char *uri = NULL;
	if(video){
		video->user_data = viewer;
		video->on_state_changed = on_video_status_changed;
		video->on_eos = on_eos;
		uri = video->uri;
	}
	
	GtkWidget *grid = gtk_grid_new();	
	GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, 0, 1, 1);
	viewer->hbox[0] = hbox;
	
	GtkWidget * file_chooser_btn = gtk_file_chooser_button_new(_("Open ..."), GTK_FILE_CHOOSER_ACTION_OPEN);
	apply_video_files_filter(GTK_FILE_CHOOSER(file_chooser_btn));
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(file_chooser_btn), shell->app->work_dir);
	
	g_signal_connect(file_chooser_btn, "file-set", G_CALLBACK(on_file_selection_changed), viewer);
	
	GtkWidget *uri_entry = gtk_search_entry_new();
	GtkWidget *refresh_btn = gtk_button_new_from_icon_name("view-refresh", GTK_ICON_SIZE_BUTTON);
	
	gtk_entry_set_text(GTK_ENTRY(uri_entry), uri);
	gtk_box_pack_start(GTK_BOX(hbox), uri_entry, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), refresh_btn, FALSE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), file_chooser_btn, FALSE, TRUE, 2);
	g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_uri_changed), viewer);
	g_signal_connect(uri_entry, "activate", G_CALLBACK(on_uri_changed), viewer);
	
	struct da_panel *panel = da_panel_init(viewer->panel, image_width, image_height, viewer);
	assert(panel);
	panel->on_button_press = on_da_button_press;
	gtk_widget_set_size_request(panel->frame, 320, 180);
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, 1, 1, 1);
	
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, 2, 1, 1);
	viewer->hbox[1] = hbox;
	
	GtkWidget * slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.001);
	gtk_widget_set_hexpand(slider, TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_RIGHT);
	viewer->slider_update_handler = g_signal_connect(slider, "value-changed", G_CALLBACK(on_slider_value_changed), viewer);
	g_signal_connect(slider, "format-value", G_CALLBACK(format_timer_slider_value), viewer);
	
	GtkWidget * play_pause_button = gtk_toggle_button_new();
	GtkWidget * pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);
	g_signal_connect(play_pause_button, "toggled", G_CALLBACK(on_play_pause_toggled), viewer);
	
	gtk_box_pack_start(GTK_BOX(hbox), play_pause_button, FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(hbox), slider, TRUE, TRUE, 1);
	GtkWidget * volume_icon = gtk_button_new_from_icon_name("audio-volume-medium", GTK_ICON_SIZE_BUTTON);
	//~ g_signal_connect(volume_icon, "clicked", G_CALLBACK(on_popup_volume_control), viewer);
	gtk_box_pack_end(GTK_BOX(hbox), volume_icon, FALSE, TRUE, 1);
	
	GtkWidget * mute = gtk_toggle_button_new();
	GtkWidget * icon = gtk_image_new_from_icon_name("audio-volume-muted", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(mute), icon);
	//~ g_signal_connect(mute, "toggled", G_CALLBACK(on_mute_toggled), viewer);
	gtk_box_pack_end(GTK_BOX(hbox), mute, FALSE, TRUE, 1);
	
	
	viewer->slider = slider;
	viewer->play_pause_button = play_pause_button;
	viewer->grid = grid;
	viewer->uri_entry = uri_entry;
	
	gtk_widget_set_child_visible(viewer->hbox[0], TRUE);
	gtk_widget_set_child_visible(viewer->hbox[1], TRUE);
	
	viewer->context_menu = create_options_menu(viewer);
	return viewer;
}
void stream_viewer_cleanup(struct stream_viewer *viewer)
{
	
}
