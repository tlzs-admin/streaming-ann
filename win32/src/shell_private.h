#ifndef SHELL_PRIVATE_H_
#define SHELL_PRIVATE_H_

#include "shell.h"

#include <gtk/gtk.h>
#include <json-c/json.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct da_panel;
struct shell_private
{
	struct shell_context *shell;
	char *conf_file;
	json_object *jconfig;
	pthread_mutex_t mutex;
	
	GtkWidget *window;
	GtkWidget *header_bar;
	GtkWidget *content_area;
	
	struct da_panel *panels[1];
	int quit;
	int timer_id;
	int busy;
	
	GtkWidget *da;
	int da_width;
	int da_height;
	
	cairo_surface_t *surface;
	unsigned char *bgra_data;
	int image_width;
	int image_height;
	
};
#define shell_lock(shell) pthread_mutex_lock(&shell->priv->mutex)
#define shell_unlock(shell) pthread_mutex_unlock(&shell->priv->mutex)

#ifdef __cplusplus
}
#endif
#endif
