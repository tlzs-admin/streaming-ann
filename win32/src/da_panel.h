#ifndef _DA_PANEL_H_
#define _DA_PANEL_H_

#include <stdio.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct da_panel da_panel_t;
struct da_panel
{
	void * shell;
	GtkWidget * frame;
	GtkWidget * da;
	int width, height;	// widget size
	
	cairo_surface_t * surface;
	unsigned char * image_data;
	int image_width;
	int image_height;
	
	double x_offset;
	double y_offset;
	
	int keep_ratio;
	
	// virtual methods
	void (* clear)(struct da_panel * panel);
	
	// callbacks
	gboolean (* on_draw)(struct da_panel * panel, cairo_t * cr, void * user_data);
	gboolean (* on_key_press)(struct da_panel * panel, guint keyval, guint state);
	gboolean (* on_key_release)(struct da_panel * panel, guint keyval, guint state);
	gboolean (* on_button_press)(struct da_panel * panel, guint button, double x, double y, guint state, GdkEventButton * event);
	gboolean (* on_button_release)(struct da_panel * panel, guint button, double x, double y, guint state, GdkEventButton * event);
	gboolean (* on_mouse_move)(struct da_panel * panel, double x, double y, guint state);
	gboolean (* on_leave_notify)(struct da_panel * panel, double x, double y, guint state);
	
	int (*on_update_frame)(struct da_panel *panel, cairo_t *cr);
	double scale_factor;
};
struct da_panel * da_panel_init(struct da_panel * panel, int image_width, int image_height, void * shell);
void da_panel_cleanup(struct da_panel * panel);

void da_panel_update_frame(struct da_panel *panel, const unsigned char *bgra_data, int width, int height);

#ifdef __cplusplus
}
#endif
#endif
