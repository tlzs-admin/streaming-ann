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
	long class_id;
};

struct settings_private;
struct area_settings_dialog
{
	struct settings_private *priv;
	long (* open)(struct area_settings_dialog * settings, const input_frame_t * bk_image);
	long (*pt_in_area)(struct area_settings_dialog * settings, double x, double y); // <x,y>::range = [0.0 ~ 1.0]

// public data
	void * user_data;
	
#define MAX_SETTING_AREAS (16)
	ssize_t num_areas;
	struct area_setting areas[MAX_SETTING_AREAS];
};
struct area_settings_dialog * area_settings_dialog_new(GtkWidget * parent_window, const char * title, void * user_data);


#ifdef __cplusplus
}
#endif
#endif
