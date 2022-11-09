/*
 * time_spin_widget.c
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

#include <stdint.h>
#include <time.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include "time_spin_widget.h"

enum time_spin_label_id
{
	time_spin_label_up = 1,
	time_spin_label_down = 2,
};

static int time_spin_widget_set_time(struct time_spin_widget *spin, time_t timestamp)
{
	struct tm t;
	memset(&t, 0, sizeof(t));
	localtime_r(&timestamp, &t);
	spin->timestamp = timestamp;
	
	int hour = t.tm_hour;
	int minute = t.tm_min;
	char sz_value[100] = "";
	snprintf(sz_value, sizeof(sz_value), "%.2d", hour);
	gtk_entry_set_text(GTK_ENTRY(spin->hour), sz_value);
	
	snprintf(sz_value, sizeof(sz_value), "%.2d", minute);
	gtk_entry_set_text(GTK_ENTRY(spin->minute), sz_value);
	
	if(spin->show_seconds) {
		snprintf(sz_value, sizeof(sz_value), "%.2d", (int)t.tm_sec);
		gtk_entry_set_text(GTK_ENTRY(spin->second), sz_value);
	}
	return 0;
}

static gboolean on_updown_arrow_clicked(GtkWidget *eventbox, GdkEventButton *event, void *user_data)
{
	if(event->type != GDK_BUTTON_PRESS) return FALSE;
	
	GList *list = gtk_container_get_children(GTK_CONTAINER(eventbox));
	if(NULL == list) return FALSE;
	GtkWidget *label = list->data;
	g_list_free(list);
	if(NULL == label) return FALSE;
	
	enum time_spin_label_id label_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(eventbox), "label_id"));
	printf("label_id: %d\n", label_id);
	
	struct time_spin_widget *spin = user_data;
	time_t timestamp = spin->timestamp;
	
	switch(label_id) {
	case time_spin_label_up:
		timestamp += spin->step;
		break;
	case time_spin_label_down:
		timestamp -= spin->step;
		break;
	default:
		return FALSE;
	}
	
	time_spin_widget_set_time(spin, timestamp);
	return TRUE;
}

static gboolean on_spin_entry_scroll(GtkWidget *entry, GdkEventScroll *event, void *user_data)
{
	if(event->type != GDK_SCROLL) return FALSE;

	struct time_spin_widget *spin = user_data;
	assert(user_data);
	if(entry != spin->hour && entry != spin->minute && entry != spin->second) return FALSE;
	
	time_t timestamp = spin->timestamp;
	struct tm t;
	memset(&t, 0, sizeof(t));
	localtime_r(&timestamp, &t);
	
	char sz_value[100] = "";
	int hour = t.tm_hour;
	int minute = t.tm_min;
	int second = t.tm_sec;
	
	switch(event->direction) {
	case GDK_SCROLL_UP:
		if(entry == spin->hour) { 
			if(hour >= 24) return FALSE;
			++hour;
			timestamp += 3600;
			snprintf(sz_value, sizeof(sz_value), "%.2d", hour);
		}
		else if(entry == spin->minute) { 
			if(minute >= 60)  return FALSE;
			++minute; 
			timestamp += 60;
			snprintf(sz_value, sizeof(sz_value), "%.2d", minute);
		}else {
			if(second >= 60) return FALSE;
			++second;
			++timestamp;
			snprintf(sz_value, sizeof(sz_value), "%.2d", second);
		}
		break;
	case GDK_SCROLL_DOWN:
		if(entry == spin->hour) { 
			if(hour < 1) return FALSE;
			--hour;
			timestamp -= 3600;
			snprintf(sz_value, sizeof(sz_value), "%.2d", hour);
		}
		else if(entry == spin->minute){ 
			if(minute < 1)  return FALSE;
			--minute; 
			timestamp -= 60;
			snprintf(sz_value, sizeof(sz_value), "%.2d", minute);
		}else {
			if(second < 1) return FALSE;
			--second;
			--timestamp;
			snprintf(sz_value, sizeof(sz_value), "%.2d", second);
		}
		break;
	default:
		return FALSE;
	}
	
	spin->timestamp = timestamp;
	gtk_entry_set_text(GTK_ENTRY(entry), sz_value);
	
	return FALSE;
}

static void on_insert_text(GtkEditable *entry, char *new_text, int cb_text, gint * position, gpointer user_data)
{
	printf("%s()...\n", __FUNCTION__);
	for(int i = 0; i < cb_text; ++i) {
		if(!isdigit(new_text[i])) {
			g_signal_stop_emission_by_name(entry, "insert-text");
			fprintf(stdout, "\aBeep!\n");
			return;
		}
	}
	return;
}
static GtkWidget *create_spin_entry(struct time_spin_widget *spin)
{
	GtkWidget *entry = gtk_entry_new();
	GtkStyleContext *styles = gtk_widget_get_style_context(entry);
	gtk_style_context_add_class(styles, "spin_entry");
	
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 3);
	gtk_entry_set_max_length(GTK_ENTRY(entry), 2);
	gtk_entry_set_alignment(GTK_ENTRY(entry), 0.9);
	
	gtk_widget_set_events(entry, gtk_widget_get_events(entry) | GDK_SCROLL_MASK);
	g_signal_connect(entry, "scroll-event", G_CALLBACK(on_spin_entry_scroll), spin);
	g_signal_connect(entry, "insert-text", G_CALLBACK(on_insert_text), spin);
	return entry;
}

static GtkWidget *create_up_down_buttons(struct time_spin_widget *spin)
{
	static const char *up_arrow_char = "▲"; // utf-8("\xE2\x96\xb2") utf-16(0x25b2) 
	static const char *down_arrow_char = "▼"; // utf-8("\xE2\x96\xbc") utf-16(0x25bc)
	
	// create up/down arrow
	GtkWidget *up = gtk_label_new(up_arrow_char);
	GtkWidget *down = gtk_label_new(down_arrow_char);
	gtk_widget_set_margin_top(up, 0);
	gtk_widget_set_margin_bottom(up, 0);
	gtk_widget_set_margin_top(down, 0);
	gtk_widget_set_margin_bottom(down, 0);
	gtk_label_set_width_chars(GTK_LABEL(up), 2);
	
	GtkWidget *up_event_box = gtk_event_box_new();
	gtk_widget_set_events(up_event_box, GDK_BUTTON_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(up_event_box), up);
	g_object_set_data(G_OBJECT(up_event_box), "label_id", GINT_TO_POINTER(time_spin_label_up));
	g_signal_connect(up_event_box, "button_press_event", G_CALLBACK(on_updown_arrow_clicked), spin);
	
	GtkWidget *down_event_box = gtk_event_box_new();
	gtk_widget_set_events(down_event_box, GDK_BUTTON_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(down_event_box), down);
	g_object_set_data(G_OBJECT(down_event_box), "label_id", GINT_TO_POINTER(time_spin_label_down));
	g_signal_connect(down_event_box, "button_press_event", G_CALLBACK(on_updown_arrow_clicked), spin);
	
	
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), up_event_box, 0, 0, 1);
	gtk_box_pack_start(GTK_BOX(vbox), down_event_box, 0, 0, 1);
	return vbox;
}

struct time_spin_widget *time_spin_widget_init(struct time_spin_widget *spin, int show_seconds, void *user_data)
{
	if(NULL == spin) spin = calloc(1, sizeof(*spin));
	assert(spin);
	
	spin->user_data = user_data;
	spin->set_time = time_spin_widget_set_time;
	spin->show_seconds = show_seconds;
	spin->step = 5 * 60; // 5 minutes
	
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget *hour = create_spin_entry(spin);
	GtkWidget *minute = create_spin_entry(spin);
	GtkWidget *second = NULL;
	
	spin->hour = hour;
	spin->minute = minute;
	if(show_seconds) {
		second = create_spin_entry(spin);
		spin->second = second;
	}
	
	GtkWidget *up_down = create_up_down_buttons(spin);
	
	
	gtk_box_pack_start(GTK_BOX(box), hour, TRUE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(box), gtk_label_new(" : "), FALSE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(box), minute, TRUE, TRUE, 1);
	if(show_seconds && second) {
		gtk_box_pack_start(GTK_BOX(box), gtk_label_new(" : "), FALSE, TRUE, 1);
		gtk_box_pack_start(GTK_BOX(box), second, TRUE, TRUE, 1);
	}
	
	gtk_box_pack_start(GTK_BOX(box), up_down, 0, 0, 0);
	
	gtk_widget_set_size_request(box, 180, 30);
	gtk_widget_set_hexpand(box, TRUE);
	
	gtk_container_set_border_width(GTK_CONTAINER(box), 1);
	spin->box = box;

	struct timespec ts[1];
	memset(ts, 0, sizeof(ts));
	clock_gettime(CLOCK_REALTIME, ts);
	
	time_spin_widget_set_time(spin, ts->tv_sec);
	return spin;
}

#if 0 || defined(TEST_TIME_SPIN_WIDGET_) && defined(_STAND_ALONE) 
int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	
	// set default background
	static const char *css_string = "window,dialog label { background-color: #454545; color: #FFFFFF; }\n"
		".spin_entry { border: none; } \n"
		"entry:active { border: none; background-color: #ff0000; } \n"
		"box { border: 1px; background-color: white; color: black; } \n"
		"headerbar { color: black;}\n"
		;
	GtkCssProvider *css = gtk_css_provider_new();
	GError *gerr = NULL;
	gboolean ok = gtk_css_provider_load_from_data(css, css_string, strlen(css_string), &gerr);
	if(!ok) {
		fprintf(stderr, "load css failed: %s\n", gerr->message);
	}
	assert(ok);
	
	GdkScreen *screen = gdk_display_get_default_screen(gdk_display_get_default());
	gtk_style_context_add_provider_for_screen(screen,
		GTK_STYLE_PROVIDER(css), 
		GTK_STYLE_PROVIDER_PRIORITY_USER);
		
		
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "test time_spin_widget");
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
	
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 100);
	

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
	
	struct time_spin_widget *begin = time_spin_widget_new(1, NULL);
	struct time_spin_widget *end = time_spin_widget_new(0, NULL);
	end->set_time(end, begin->timestamp + 3600);
	
	int row = 0;
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Begin Time"), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), begin->box, 1, row, 1, 1);
	
	row = 1;
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("End Time"), 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), end->box, 1, row, 1, 1);
	
	
	gtk_container_add(GTK_CONTAINER(window), grid);
	gtk_widget_show_all(window);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	
	
	gtk_widget_grab_focus(window);
	gtk_main();
	
	free(begin);
	free(end);
	return 0;
}
#endif

