/*
 * settings-dlg.c
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

#include <limits.h>
#include <gtk/gtk.h>
#include <json-c/json.h>
#include "app.h"
#include "shell.h"
#include "utils.h"

#include "shell_private.h"

static json_object *app_get_config(struct app_context *app)
{
	return app->jconfig;
}
GtkWidget * settings_dlg_new(const char *title, GtkWidget *parent_window, struct shell_context *shell);


static void on_alpha_value_changed(GtkSpinButton *spin, struct color_context *color)
{
	GdkRGBA *rgba = &color->rgba;
	double value = gtk_spin_button_get_value(spin);
	if(value >= 0 && value <= 1) rgba->alpha = value;
	
	if(color->use_alpha) {
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color->color_btn), rgba);
	}
	debug_printf("== %s(): rgba = {%g,%g,%g,%g}", __FUNCTION__, rgba->red, rgba->green, rgba->blue, rgba->alpha);
	return; 
}

static void on_rgb_value_changed(GtkWidget *color_btn, struct color_context *color)
{
	GdkRGBA *rgba = &color->rgba;
	double old_alpha = rgba->alpha;
	
	gboolean use_alpha = gtk_color_chooser_get_use_alpha(GTK_COLOR_CHOOSER(color_btn));
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_btn), rgba);
	if(use_alpha && color->alpha_spin) {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(color->alpha_spin), rgba->alpha);
	}
	else {
		rgba->alpha = old_alpha;
	}
	
	debug_printf("== %s(use_alpha=%d): rgba = {%g,%g,%g,%g}", __FUNCTION__, 
		use_alpha,
		rgba->red, rgba->green, rgba->blue, rgba->alpha);
	return;
}

struct stack_child {
	char *name;
	char *title;
	GtkWidget *stack;
	GtkWidget *frame;
	void *user_data;
	int (*init)(struct stack_child *child, void *user_data);
};

struct stack_child *stack_child_new(GtkWidget *stack, 
	const char *name, const char *title,
	int (*init)(struct stack_child *info, void *user_data), 
	void *user_data);
void stack_child_free(struct stack_child *info);

static void on_stack_child_changed(GtkStack *stack, GParamSpec *property, gpointer user_data)
{
	GtkWidget *child = gtk_stack_get_visible_child(stack);
	const char *name = gtk_stack_get_visible_child_name(stack);
	printf("name: %s, child: %p\n", name, child);
}

static int stack_child_init_default(struct stack_child *child, void *user_data)
{
	GtkWidget *frame = gtk_frame_new(NULL); //(child->title);
	child->frame = frame;
	return 0;
}


static int stack_child_stream_settings_init(struct stack_child *child, void *user_data)
{
	stack_child_init_default(child, user_data);
	
	struct shell_context *shell = user_data;
	assert(shell);
	struct app_context *app = shell->app;
	json_object *jconfig = app_get_config(app);
	
	assert(jconfig);
	
	json_bool ok = FALSE;
	json_object *jstreams = NULL;
	printf("jconfig: %s\n", json_object_to_json_string_ext(jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE));
	ok = json_object_object_get_ex(jconfig, "streams", &jstreams);
	assert(ok && jstreams); 
	
	GtkWidget *scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(child->frame), scrolled_win);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	
	GtkWidget *listbox = gtk_list_box_new();
	
	gtk_container_add(GTK_CONTAINER(scrolled_win), listbox);
	
	int num_streams = json_object_array_length(jstreams);
	for(int i = 0; i < num_streams; ++i) {
		json_object *jstream = json_object_array_get_idx(jstreams, i);
		if(NULL == jstream) continue;
		json_object *jinput = NULL;
		ok = json_object_object_get_ex(jstream, "input", &jinput);
		if(NULL == jinput) continue;
		
		GtkWidget *row = gtk_list_box_row_new();
		GtkWidget *frame = gtk_frame_new(NULL);
		gtk_container_add(GTK_CONTAINER(row), frame);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
		
		GtkWidget *grid = gtk_grid_new();
		gtk_container_add(GTK_CONTAINER(frame), grid);
		
		gtk_grid_set_column_spacing(GTK_GRID(grid), 3);
		gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
		
		
		char text[PATH_MAX] = "";
		snprintf(text, sizeof(text), "stream %d", i + 1);
		GtkWidget *label = gtk_label_new(text);
		gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 3, 1);
		
		label = gtk_label_new("input");
		gtk_widget_set_halign(GTK_WIDGET(label), GTK_ALIGN_START);
		gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
		const char *uri = json_get_value(jinput, string, uri);
		GtkWidget *uri_entry = gtk_search_entry_new();
		gtk_entry_set_text(GTK_ENTRY(uri_entry), uri);
		gtk_widget_set_hexpand(uri_entry, TRUE);
		gtk_grid_attach(GTK_GRID(grid), uri_entry, 1, 1, 1, 1);
		
		GtkWidget *button = gtk_file_chooser_button_new("Open", GTK_FILE_CHOOSER_ACTION_OPEN);
		gtk_grid_attach(GTK_GRID(grid), button, 2, 1, 1, 1);
		
		label = gtk_label_new("ai-engine");
		gtk_widget_set_halign(GTK_WIDGET(label), GTK_ALIGN_START);
		gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
		
		GtkWidget *combo = gtk_combo_box_new_with_entry();
		gtk_grid_attach(GTK_GRID(grid), combo, 1, 2, 2, 1);
		gtk_widget_set_hexpand(combo, TRUE);
		
		gtk_container_add(GTK_CONTAINER(listbox), row);
	}
	
	
	
	return 0; 
}

static void clear_surface(cairo_t *cr)
{
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_paint(cr);
}
static gboolean on_da_draw(GtkWidget *da, cairo_t *cr, struct stack_child *child)
{
	clear_surface(cr);
	return FALSE;
}
static int stack_child_area_settings_init(struct stack_child *child, void *user_data)
{
	stack_child_init_default(child, user_data);
	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_hexpand(da, TRUE);
	gtk_widget_set_vexpand(da, TRUE);
	g_signal_connect(da, "draw", G_CALLBACK(on_da_draw), child);
	
	gtk_container_add(GTK_CONTAINER(child->frame), da);
	return 0;
}

static inline void create_color_widget(GtkWidget *grid, const char *title, int row, struct color_context *color)
{
	GtkWidget *label = NULL, *button = NULL, *spin = NULL;
	
	label = gtk_label_new(title);
	gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
	button = gtk_color_button_new_with_rgba(&color->rgba);
	color->color_btn = button;
	
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(button), color->use_alpha);
	g_signal_connect(button, "color-set", G_CALLBACK(on_rgb_value_changed), color);
	gtk_widget_set_hexpand(button, TRUE);
	gtk_grid_attach(GTK_GRID(grid), button, 1, row, 1, 1);
	
	//if(!color->use_alpha) 
	{
		label = gtk_label_new(_("alpha"));
		gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);
		spin = gtk_spin_button_new_with_range(0.0, 1.0, 0.1);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), color->rgba.alpha);
		color->alpha_spin = spin;
		
		g_signal_connect(spin, "value-changed", G_CALLBACK(on_alpha_value_changed), color);
		gtk_grid_attach(GTK_GRID(grid), spin, 3, row, 1, 1);
		
	}
	return;
}

static void on_class_color_set(GtkWidget *color_btn, json_object *jclass_colors)
{
	assert(jclass_colors);
	
	const char *class_name = g_object_get_data(G_OBJECT(color_btn), "class_name");
	
	printf("class_name: %s\n", class_name);
	if(NULL == class_name) return;
	
	GdkRGBA rgba = { 0 };
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_btn), &rgba);
	
	gchar *color_str = gdk_rgba_to_string(&rgba);
	if(color_str) {
		json_object_object_add(jclass_colors, class_name, json_object_new_string(color_str));
		free(color_str);
	}
	return;
}
static int stack_child_colors_settings_init(struct stack_child *child, void *user_data)
{
	static GdkRGBA default_color = {.red = 0, .green = 1, .blue = 0, .alpha = 1};
	
	struct shell_context *shell = user_data;
	assert(shell && shell->priv);
	struct shell_private *priv = shell->priv;
	
	stack_child_init_default(child, user_data);
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(child->frame), vbox);
	
	//assert(priv->jclass_colors);
	json_object *jclass_colors = NULL;
	if(NULL == priv->jclass_colors) {
		json_object *jcolors = json_object_from_file("../demo/colors.json");
		assert(jcolors);
		
		json_bool ok = json_object_object_get_ex(jcolors, "coco", &jclass_colors);
		assert(ok && jclass_colors);
		priv->jclass_colors = jclass_colors;
	}
	jclass_colors = priv->jclass_colors;
	
	GtkWidget *frame = gtk_frame_new(_("UI settings"));
	GtkWidget *grid = gtk_grid_new();
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_OUT);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, TRUE, 2);
	gtk_container_add(GTK_CONTAINER(frame), grid);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	create_color_widget(grid, _("bg color"), 0, &priv->bg);
	create_color_widget(grid, _("fg color"), 1, &priv->fg);
	
	frame = gtk_frame_new(_("Class Settings"));
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 2);
	
	GtkWidget *scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(frame), scrolled_win);
	
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget *flow_box = gtk_flow_box_new();
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow_box), 8);
	gtk_container_add(GTK_CONTAINER(scrolled_win), flow_box);
	
	struct json_object_iterator iter = json_object_iter_begin(jclass_colors);
	struct json_object_iterator end = json_object_iter_end(jclass_colors);
	while(!json_object_iter_equal(&iter, &end))
	{
		const char *class_name = json_object_iter_peek_name(&iter);
		assert(class_name);
		
		json_object *jvalue = json_object_iter_peek_value(&iter);
		const char *color_name = jvalue?json_object_get_string(jvalue):NULL;
		
		GdkRGBA color;
		gboolean color_parsed = FALSE;
		if(color_name) color_parsed = gdk_rgba_parse(&color, color_name);
		if(!color_parsed) color = default_color;
		
		printf("%s: %s\n", class_name, color_name);
		
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *label = gtk_label_new(_(class_name));
		GtkWidget *color_btn = gtk_color_button_new_with_rgba(&color);
		g_object_set_data(G_OBJECT(color_btn), "class_name", strdup(class_name));
		gtk_color_button_set_title(GTK_COLOR_BUTTON(color_btn), class_name);
		g_signal_connect(color_btn, "color-set", G_CALLBACK(on_class_color_set), jclass_colors);
		
		
		gtk_box_pack_start(GTK_BOX(box), color_btn, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, TRUE, 0);
		gtk_container_add(GTK_CONTAINER(flow_box), box);
	
		json_object_iter_next(&iter);
	}
	return 0; 
}


struct stack_child *stack_child_new(GtkWidget *stack, 
	const char *name, const char *title,
	int (*init)(struct stack_child *info, void *user_data), 
	void *user_data)
{
	assert(stack && name);
	struct stack_child *child = calloc(1, sizeof(*child));
	assert(child);
	
	if(NULL == init) init = stack_child_init_default;
	
	child->name = strdup(name);
	child->title = strdup(title?title:name);
	child->stack = stack;
	child->user_data = user_data;
	
	int rc = init(child, user_data);
	if(0 == rc) {
		gtk_stack_add_titled(GTK_STACK(child->stack), child->frame, child->name, _(child->title));
	}
	return child;
}


#define NUM_STACKS (3)

GtkWidget * settings_dlg_new(const char *title, GtkWidget *parent_window, struct shell_context *shell)
{
	GtkWidget * dlg = gtk_dialog_new_with_buttons(title,
		GTK_WINDOW(parent_window), 
		GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
		"Cancel", GTK_RESPONSE_CANCEL,
		"Apply", GTK_RESPONSE_APPLY,
		NULL);
	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(content_area), hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	
	GtkWidget *sidebar = gtk_stack_sidebar_new();
	gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, TRUE, 0);
	
	GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start(GTK_BOX(hbox), separator, FALSE, FALSE, 0);
	
	GtkWidget *stack = gtk_stack_new();
	gtk_container_set_border_width(GTK_CONTAINER(stack), 5);
	gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
	gtk_box_pack_start(GTK_BOX(hbox), stack, TRUE, TRUE, 0);
	g_signal_connect(stack, "notify::visible-child", G_CALLBACK(on_stack_child_changed), shell);
	
	static const struct { 
		char *name; 
		char *title; 
		int (*init)(struct stack_child *info, void *user_data);
	}stack_properties[NUM_STACKS] = 
	{
		[0] = {.name = "colors", .title = "Colors", .init = stack_child_colors_settings_init },
		[1] = {.name = "areas", .title = "Area", .init = stack_child_area_settings_init },
		//~ [2] = {.name = "streams", .title = "Streams", .init = stack_child_stream_settings_init },
	};

	for(int i = 0; i < NUM_STACKS; ++i) {
		if(NULL == stack_properties[i].name) continue;
		
		struct stack_child * child = stack_child_new(stack,
			stack_properties[i].name, stack_properties[i].title, 
			stack_properties[i].init, shell);
		assert(child && child->frame);
		//~ gtk_stack_add_titled(GTK_STACK(stack), child->frame, info[i].name, info[i].title);
		gtk_widget_set_size_request(child->frame, 600, 400);
	}
	
	#define UNUSED(x) (void)((x))
	UNUSED(stack_child_stream_settings_init);

	return dlg;
}
#undef NUM_STACKS



#if defined(TEST_DEMO_SETTINGS_DLG_) && defined(_STAND_ALONE)
#include <locale.h>

struct app_private
{
	struct app_context *app;
	struct shell_context *shell;
};

static struct app_private g_app_priv[1];
static struct app_context g_app[1] = {{
	.user_data = NULL,
	.priv = g_app_priv,
}};

static struct shell_private g_shell_priv[1] = {{
	.bg = {.rgba = {0.5, 0.5, 0.5, 0.8}, .use_alpha = 1 },
	.fg = {.rgba = {0.0, 1.0, 0.0, 1.0}, .use_alpha = 1 },
}};
static struct shell_context g_shell[1] = {{
	.priv = g_shell_priv,
}};


struct app_context *app_context_init(struct app_context *app, void *user_data)
{
	if(NULL == app) app = g_app;
	json_object *jconfig = json_object_from_file("../demo/video-player4.json");
	assert(jconfig);
	
	app->user_data = user_data;
	app->priv->app = app;
	app->jconfig = jconfig;
	return app;
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL,"");
	gtk_init(&argc, &argv);
	
	struct app_context *app = app_context_init(NULL, NULL);
	assert(app);
	
	char *domain_path = bindtextdomain("demo", "../demo/langs");
	printf("langs.base_dir = %s\n", domain_path);
	
	// set domain for future gettext() calls 
	char *text_domain = textdomain("demo");
	printf("text_domain: %s\n", text_domain);
	
	struct shell_context *shell = g_shell;
	shell->app = app;
	app->priv->shell = shell;
	
	GtkWidget *dlg = settings_dlg_new("Settings", NULL, shell);
	
	gtk_widget_show_all(dlg);
	gtk_dialog_run(GTK_DIALOG(dlg));
	
	gtk_widget_destroy(dlg);
	return 0;
}
#endif

