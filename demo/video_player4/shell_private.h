#ifndef VIDEO_PLAYER_4_SHELL_PRIVATE_H_
#define VIDEO_PLAYER_4_SHELL_PRIVATE_H_

#include <stdio.h>
#include <gtk/gtk.h>

#include "shell.h"
#include "da_panel.h"

#ifdef __cplusplus
extern "C" {
#endif

struct shell_private
{
	struct shell_context * shell;
	int quit;
	
	GtkWidget * window;
	GtkWidget * grid;
	GtkWidget * header_bar;
	

	int num_inputs;
	da_panel_t **panels;
	guint timer_id;
	
	double fps;
	json_object * jcolors;
	GdkRGBA default_fg;
	
};

#ifdef __cplusplus
}
#endif
#endif
