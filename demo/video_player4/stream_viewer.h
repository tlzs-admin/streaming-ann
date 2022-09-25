#ifndef DEMO_STREAM_VIEWER_H_
#define DEMO_STREAM_VIEWER_H_

#include <stdio.h>
#include <gtk/gtk.h>
#include "classes_counter.h"
#include "da_panel.h"

#include "area-settings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct video_stream;

struct stream_viewer
{
	struct shell_context *shell;
	int index;
	struct video_stream *stream;
	
	GtkWidget *grid;
	GtkWidget *hbox[2];
	da_panel_t panel[1];
	
	GtkWidget *uri_entry;
	GtkWidget *play_pause_button;
	GtkWidget *slider;
	gulong slider_update_handler;
	
	int show_counters;
	struct classes_counter_context counter_ctx[1];
	GtkWidget *context_menu;
	
	int show_area_settings;
	struct area_settings_dialog *settings_dlg;
	
//	int face_masking_flag;
	int is_busy;
};

struct stream_viewer * stream_viewer_init(struct stream_viewer *viewer, int index, int min_width, int min_height, struct shell_context *shell);
void stream_viewer_cleanup(struct stream_viewer *viewer);


#ifdef __cplusplus
}
#endif
#endif
