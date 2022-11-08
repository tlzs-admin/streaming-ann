#ifndef TIME_SPIN_WIDGET_H_
#define TIME_SPIN_WIDGET_H_

#include <stdio.h>
#include <gtk/gtk.h>

struct time_spin_widget
{
	GtkWidget *box;
	GtkWidget *hour;
	GtkWidget *minute;
	GtkWidget *second;
	
	int show_seconds;
	int64_t timestamp;
	int64_t step;
	void *user_data;
	int (*set_time)(struct time_spin_widget *spin, time_t timestamp);
};
struct time_spin_widget *time_spin_widget_init(struct time_spin_widget *spin, int show_seconds, void *user_data);
#define time_spin_widget_new(show_seconds, user_data) time_spin_widget_init(NULL,show_seconds, user_data);
#define time_spin_widget_free(spin) free(spin)


#endif
