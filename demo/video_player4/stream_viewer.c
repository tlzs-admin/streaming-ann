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
		
		// clear counters
		struct classes_counter_context * counters = viewer->counter_ctx;
		if(counters && counters->clear_all) counters->clear_all(counters);
		
		int rc = video->set_uri2(video, uri, stream->image_width, stream->image_height);
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


GtkWidget * create_options_menu(struct stream_viewer * viewer)
{
	struct shell_context *shell = viewer->shell;
	GtkWidget * menu = gtk_menu_new();
	
	GtkWidget * open = gtk_menu_item_new_with_label(_("Open ..."));
	g_signal_connect(open, "activate", G_CALLBACK(on_menu_open_clicked), viewer);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);
	
	GtkWidget * separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	//~ GtkWidget * options = gtk_menu_item_new_with_label("options");
	//~ gtk_menu_shell_append(GTK_MENU_SHELL(menu), options);
	
	//~ GtkWidget * sub_menu = gtk_menu_new();
	//~ gtk_menu_item_set_submenu(GTK_MENU_ITEM(options), sub_menu);
	//~ GtkWidget * show_counters_menu = gtk_check_menu_item_new_with_label(_("Show Counters List"));
	//~ shell->show_counters_menu = show_counters_menu;
	//~ gtk_menu_shell_append(GTK_MENU_SHELL(sub_menu), show_counters_menu);
	//~ g_signal_connect(show_counters_menu, "toggled", G_CALLBACK(on_show_hide_counters_list), viewer);
	//~ gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(show_counters_menu), TRUE);
	
	//~ GtkWidget * show_slider_menu = gtk_check_menu_item_new_with_label(_("Show Video Control Bar"));
	//~ shell->show_slider_menu = show_slider_menu;
	//~ gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(show_slider_menu), TRUE);
	//~ gtk_menu_shell_append(GTK_MENU_SHELL(sub_menu), show_slider_menu);
	//~ g_signal_connect(show_slider_menu, "toggled", G_CALLBACK(on_show_hide_slider_bar), viewer);
	
	
	GtkWidget * enable_ai = gtk_check_menu_item_new_with_label(_("Enable AI engine"));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), enable_ai);

	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	//~ GtkWidget * fullscreen_switch_menu = gtk_check_menu_item_new_with_label(_("Full Screen"));
	//~ g_signal_connect(fullscreen_switch_menu, "toggled", G_CALLBACK(on_fullscreen_switch_toggled), viewer);
	//~ shell->fullscreen_switch_menu = fullscreen_switch_menu;
	//~ gtk_menu_shell_append(GTK_MENU_SHELL(menu), fullscreen_switch_menu);
	
	separator = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
	
	GtkWidget * quit =  gtk_menu_item_new_with_label(_("Quit ..."));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);
	g_signal_connect_swapped(quit, "activate", G_CALLBACK(shell->stop), viewer->shell);
	
	
	gtk_widget_show_all(menu);
	return menu;
}



struct stream_viewer * stream_viewer_init(struct stream_viewer *viewer, int index, int image_width, int image_height, struct shell_context *shell)
{
	if(NULL == viewer) viewer = calloc(1, sizeof(*viewer));
	assert(viewer);
	
	viewer->shell = shell;
	viewer->index = index;
	
	GtkWidget * grid = gtk_grid_new();
	GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, 0, 1, 1);
	viewer->hbox[0] = hbox;
	
	GtkWidget * file_chooser_btn = gtk_file_chooser_button_new(_("Open ..."), GTK_FILE_CHOOSER_ACTION_OPEN);
	apply_video_files_filter(GTK_FILE_CHOOSER(file_chooser_btn));
	
	g_signal_connect(file_chooser_btn, "file-set", G_CALLBACK(on_file_selection_changed), viewer);
	
	GtkWidget *uri_entry = gtk_search_entry_new();
	GtkWidget *clear_btn = gtk_button_new_from_icon_name("edit-delete", GTK_ICON_SIZE_BUTTON);
	gtk_box_pack_start(GTK_BOX(hbox), uri_entry, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), clear_btn, FALSE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), file_chooser_btn, FALSE, TRUE, 2);
	
	struct da_panel *panel = da_panel_init(viewer->panel, image_width, image_height, shell);
	assert(panel);
	gtk_widget_set_size_request(panel->frame, 320, 180);
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, 1, 1, 1);
	
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_grid_attach(GTK_GRID(grid), hbox, 0, 2, 1, 1);
	viewer->hbox[1] = hbox;
	
	GtkWidget * slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1.0, 0.001);
	gtk_widget_set_hexpand(slider, TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(slider), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(slider), GTK_POS_RIGHT);
	//~ viewer->slider_update_handler = g_signal_connect(slider, "value-changed", G_CALLBACK(on_slider_value_changed), viewer);
	GtkWidget * play_pause_button = gtk_toggle_button_new();
	GtkWidget * pause_icon = gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(play_pause_button), pause_icon);
	//~ g_signal_connect(play_pause_button, "toggled", G_CALLBACK(on_play_pause_toggled), viewer);
	
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
	
	
	return viewer;
}
void stream_viewer_cleanup(struct stream_viewer *viewer)
{
	
}
