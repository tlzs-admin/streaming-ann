#ifndef VIDEO_PLAYER3_SETTINGS_H_
#define VIDEO_PLAYER3_SETTINGS_H_

#include <gtk/gtk.h>
#include "input-frame.h"

#ifdef __cplusplus
extern "C" {
#endif

struct point_d
{
	double x;
	double y;
};

#define MAX_AREA_VERTEX (64)
struct area_setting
{
	ssize_t num_vertexes;
	struct point_d vertexes[MAX_AREA_VERTEX];
};

#define MAX_SETTING_AREAS (16)
struct area_settings_dialog
{
// private data
	GtkWidget * dlg;
	void * user_data;
	GtkWidget * da;
	cairo_surface_t * surface;
	cairo_surface_t * area_masks;
	double da_width;
	double da_height;
	double image_width;
	double image_height;

	int area_index;
	int points_count;
	
	int edit_flags;

// public method
	guint (* open)(struct area_settings_dialog * dlg, const input_frame_t * bk_image);
	
// public data
	ssize_t num_areas;
	struct area_setting areas[MAX_SETTING_AREAS];
	
	
};
struct area_settings_dialog * area_settings_dialog_new(GtkWidget * window, const char * title, void * user_data);


#ifdef __cplusplus
}
#endif
#endif
