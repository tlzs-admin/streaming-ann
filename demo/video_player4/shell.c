/*
 * shell.c
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
#include "shell.h"
#include "shell_private.c.impl"
#include "utils.h"
#include "video_streams.h"


static GdkRGBA s_default_fg = { .red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0 };
static GdkRGBA s_default_bg = { .red = 0.7, .green = 0.7, .blue = 0.7, .alpha = 0.9 }; 
static GdkRGBA s_default_face_bg = { .red = 0.7, .green = 0.7, .blue = 0.0, .alpha = 0.9 }; 

ssize_t app_get_streams(struct app_context *app, struct video_stream **p_streams);

static int shell_reload_config(struct shell_context * shell, json_object * jconfig);
static int shell_init(struct shell_context * shell);
static int shell_run(struct shell_context * shell);
static int shell_stop(struct shell_context * shell);

struct shell_context * shell_context_init(struct shell_context * shell, void * _app)
{
	assert(_app);
	
	if(NULL == shell) shell = calloc(1, sizeof(*shell));
	assert(shell);
	
	struct app_context *app = _app;
	
	shell->app = app;
	shell->reload_config = shell_reload_config;
	shell->init = shell_init;
	shell->run = shell_run;
	shell->stop = shell_stop;
	
	
	struct shell_private *priv = shell->priv;
	if(NULL == priv) priv = shell_private_new(shell);
	assert(priv);
	shell->priv = priv;
	
	shell_reload_config(shell, app->jconfig);

	return shell;
}
void shell_context_cleanup(struct shell_context * shell)
{
	
}

static int shell_reload_config(struct shell_context * shell, json_object * jconfig)
{
	json_bool ok = FALSE;
	json_object *jui = NULL;
	ok = json_object_object_get_ex(jconfig, "ui", &jui);
	if(!ok || NULL == jui) return -1;
		
	struct shell_private *priv = shell->priv;
	priv->fps = json_get_value(jui, double, fps);
	ok = json_object_object_get_ex(jui, "colors", &priv->jcolors);
	if(!ok) return -1;
	
	return 0;
}

static int shell_init(struct shell_context * shell)
{
	init_windows(shell);
	return 0;
}

static gboolean on_timeout(struct shell_context *shell);
static int shell_run(struct shell_context * shell)
{
	struct shell_private *priv = shell->priv;
	
	if(priv->fps < 1 || priv->fps > 30) priv->fps = 10;
	
	priv->timer_id = g_timeout_add(1000 / priv->fps, (GSourceFunc)on_timeout, shell);
	gtk_main();
	
	if(priv->timer_id) {
		g_source_remove(priv->timer_id);
		priv->timer_id = 0;
	}
	return 0;
}
static int shell_stop(struct shell_context * shell)
{
	gtk_main_quit();
	return 0;
}





struct darknet_detection
{
	long class_id;
	const char * class_name;
	double confidence;
	double x;
	double y;
	double cx;
	double cy;
};
static ssize_t darknet_detection_parse_json(json_object * jdetections, struct darknet_detection **p_detections)
{
	debug_printf("%s()...", __FUNCTION__);
	assert(jdetections && p_detections);
	
	ssize_t count = json_object_array_length(jdetections);
	if(count <= 0) return count;
	
	struct darknet_detection * dets = calloc(count, sizeof(*dets));
	assert(dets);
	
	for(ssize_t i = 0; i < count; ++i) {
		json_object * jdet = json_object_array_get_idx(jdetections, i);
		
		dets[i].class_id = json_get_value_default(jdet, int, class_index, -1);
		dets[i].class_name = json_get_value(jdet, string, class);
		dets[i].confidence = json_get_value(jdet, double, confidence);
		dets[i].x = json_get_value(jdet, double, left);
		dets[i].y = json_get_value(jdet, double, top);
		dets[i].cx = json_get_value(jdet, double, width);
		dets[i].cy = json_get_value(jdet, double, height);
		
		assert(dets[i].class_id >= 0 || dets[i].class_name != NULL);
	}
	*p_detections = dets;
	return count;
}

#define AUTO_FREE_PTR __attribute__((cleanup(auto_free_ptr)))
static void auto_free_ptr(void * ptr)
{
	void * p = *(void **)ptr;
	if(p) { free(p); *(void **)ptr = NULL; }
}

static void show_text(cairo_t *cr, int x, int y, int font_size, const char *sz_text, GdkRGBA *fg_color, GdkRGBA *bg_color, int text_width)
{
	if(NULL == fg_color) fg_color = &s_default_fg;
	if(NULL == bg_color) bg_color = &s_default_bg;
	
	// draw text background
	cairo_text_extents_t extents;
	cairo_text_extents(cr, sz_text, &extents);
	cairo_set_source_rgba(cr, bg_color->red, bg_color->green, bg_color->blue, bg_color->alpha);
	cairo_rectangle(cr, x - 2, y - 2, 
		(text_width>0)?text_width:(extents.width + extents.x_bearing + 4), 
		extents.height + extents.y_advance + 4 - extents.y_bearing);
	cairo_fill(cr); 
	
	// draw label
	cairo_set_source_rgba(cr, fg_color->red, fg_color->green, fg_color->blue, fg_color->alpha);
	cairo_move_to(cr, x, y + font_size);
	cairo_show_text(cr, sz_text);
	cairo_stroke(cr);
	return;
}

static int calc_text_width(cairo_t *cr, int num_classes, char ** text_lines)
{
	int max_width = -1;
	for(int i = 0; i < num_classes; ++i) {
		cairo_text_extents_t extents;
		cairo_text_extents(cr, text_lines[i], &extents);
		int width = extents.width + extents.x_bearing + 4;
		if(width > max_width) max_width = width;
	}
	return max_width;
}

static void draw_counters(cairo_t *cr, const int font_size, json_object *jcolors, struct stream_viewer *viewer)
{
	struct classes_counter_context * counters = viewer->counter_ctx;
	int num_classes = counters->num_classes;
	if(num_classes <= 0) return;

	const size_t max_line_size = 200;
	int x = 10, y = 10;
	
	char ** text_lines = calloc(num_classes, sizeof(*text_lines));
	assert(text_lines);
	for(int i = 0; i < num_classes; ++i) {
		text_lines[i] = calloc(max_line_size, 1);
		assert(text_lines[i]);
		snprintf(text_lines[i], max_line_size, "%.10s: %d", _(counters->classes[i].name),  (int)counters->classes[i].count);
	}

	int text_width = calc_text_width(cr, num_classes, text_lines);
	
	for(int i = 0; i < num_classes; ++i) {
		gboolean color_parsed = FALSE;
		GdkRGBA fg_color;
		const char *color_name = NULL;
		if(jcolors) {
			json_bool ok = FALSE;
			json_object *jcolor = NULL;
			ok = json_object_object_get_ex(jcolors, counters->classes[i].name, &jcolor);
			if(ok && jcolor) color_name = json_object_get_string(jcolor);
			if(color_name) color_parsed = gdk_rgba_parse(&fg_color, color_name);
		}
		if(!color_parsed) gdk_rgba_parse(&fg_color, "green"); // default color
		show_text(cr, x, y, font_size, text_lines[i], &fg_color, NULL, text_width);
		y += font_size + 5;
	}
	
	for(int i = 0; i < num_classes; ++i) { free(text_lines[i]); }
	
	free(text_lines);
	return;
}

static void draw_bounding_boxes(cairo_t *cr, double width, double height,
	int font_size, 
	ssize_t num_detections, const struct darknet_detection * dets, 
	json_object *jcolors, 
	struct stream_viewer *viewer)
{
	struct classes_counter_context * counters = viewer->counter_ctx;
	counters->reset(counters);
	struct area_settings_dialog * settings = viewer->settings_dlg;
	for(ssize_t i = 0; i < num_detections; ++i) {
		gboolean color_parsed = FALSE;
		GdkRGBA fg_color;
		if(dets[i].class_id < 0) continue;
		
		const char *class_name = dets[i].class_name;
		const char *color_name = NULL;
		if(NULL == class_name) continue;
		
		double x = dets[i].x * width;
		double y = dets[i].y * height;
		double cx = dets[i].cx * width;
		double cy = dets[i].cy * height;
		
		struct class_counter *counter = NULL;
		int area_index = -1;
		if(settings->num_areas > 0 && settings->areas[0].num_vertexes >= 3) {
			double center_x = dets[i].x + dets[i].cx / 2;
			double bottom_y = dets[i].y + dets[i].cy;
			area_index = settings->pt_in_area(settings, center_x, bottom_y);
			
			if(area_index >= 0) {
				counter = counters->add_by_id(counters, dets[i].class_id);
			}
		}else {
			counter = counters->add_by_id(counters, dets[i].class_id);
		}
		if(counter) strncpy(counter->name, class_name, sizeof(counter->name));
		
		if(jcolors) {
			json_object *jcolor = NULL;
			json_bool ok = json_object_object_get_ex(jcolors, class_name, &jcolor);
			if(ok && jcolor) color_name = json_object_get_string(jcolor);
			if(color_name) color_parsed = gdk_rgba_parse(&fg_color, color_name);
		}
		if(!color_parsed) gdk_rgba_parse(&fg_color, "green"); // default color
		
		// draw bounding box
		cairo_set_source_rgb(cr, fg_color.red, fg_color.green, fg_color.blue);
		cairo_rectangle(cr, x, y, cx, cy);
		cairo_stroke(cr);
		
		show_text(cr, x, y, font_size, _(class_name), &fg_color, NULL, -1);
	}
	return;
}

static void draw_face_masking(cairo_t *cr, double width, double height,
	ssize_t num_detections, const struct darknet_detection * dets, 
	json_object *jcolors, 
	struct stream_viewer *viewer)
{
	static const double face_ratio = 0.7;
	static const double blur_face_size = 15;
	
	GdkRGBA face_bg = s_default_face_bg;
	
	for(ssize_t i = 0; i < num_detections; ++i) {
		if(dets[i].class_id != 0) continue; // not person
		
		double x = dets[i].x * width;
		double y = dets[i].y * height;
		double cx = dets[i].cx * width;
		double cy = dets[i].cy * height;
		
		double radius = (cx * face_ratio) / 2;
		double center_x = x + cx / 2;
		double center_y = y + radius;
		if(center_y > (y + cy - radius) ) center_y = y + cy - radius;
		
		
		// todo: blur_face(cr, kernel_size, center_x, center_y, radius);
		/* draw masks */
		cairo_set_source_rgba(cr, 
			face_bg.red, face_bg.green, face_bg.blue, 
			(radius>blur_face_size)?1.0:face_bg.alpha);
		cairo_arc(cr, center_x, center_y, radius, 0.0, 3.1415926 * 2);
		cairo_fill(cr);
	}
	return;
}

static void draw_ai_result(cairo_surface_t *surface, json_object *jresult, json_object *jcolors, struct stream_viewer *viewer)
{
	assert(surface);
	if(NULL == jresult) return;
	
	double width = cairo_image_surface_get_width(surface);
	double height = cairo_image_surface_get_height(surface);
	if(width < 1 || height < 1) return;
	
	const double font_size = (double)height / 32; 
	const double line_width = (double)height / 240;
	const char * font_family = "Mono"; 
	
	json_object * jdetections = NULL;
	json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
	if(!ok || NULL == jdetections) return;
	
	AUTO_FREE_PTR struct darknet_detection * dets = NULL;
	ssize_t num_detections = darknet_detection_parse_json(jdetections, &dets);
	
	if(num_detections <= 0) return;
	cairo_t *cr = cairo_create(surface);
	cairo_select_font_face(cr, font_family, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, font_size);
	cairo_set_line_width(cr, line_width);
	
	if(!viewer->face_masking_flag) draw_bounding_boxes(cr, width, height, font_size, 
		num_detections, dets, jcolors, viewer);
	else draw_face_masking(cr, width, height, num_detections, dets, jcolors, viewer);
	
	if(viewer->show_counters) draw_counters(cr, font_size, jcolors, viewer);
	cairo_destroy(cr);
	return;
}

static void draw_area_settings(cairo_surface_t * surface, double width, double height, struct stream_viewer * viewer)
{
	if(width < 1 || height < 1) return;
	struct area_setting * area = &viewer->settings_dlg->areas[0];
	if(area->num_vertexes < 3) return;

	double x = area->vertexes[0].x * width;
	double y = area->vertexes[0].y * height;
	
	cairo_t * cr = cairo_create(surface);
	cairo_move_to(cr, x, y);
	for(int i = 1; i < area->num_vertexes; ++i) {
		x = area->vertexes[i].x * width;
		y = area->vertexes[i].y * height;
		cairo_line_to(cr, x, y);
	}
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0, 1, 0, 0.6);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 0, 1);
	cairo_stroke(cr);
	cairo_destroy(cr);
	return;
}

static void draw_frame(da_panel_t *panel, const input_frame_t *frame, json_object *jresult, json_object *jcolors, struct stream_viewer *viewer)
{
	if(NULL == frame || NULL == frame->data || frame->width < 1 || frame->height < 1) return;
	
	cairo_surface_t *surface = panel->surface;
	unsigned char *image_data;
	if(NULL == surface  
		|| panel->image_width != frame->width 
		|| panel->image_height != frame->height) 
	{
		if(surface) cairo_surface_destroy(surface);
		surface = panel->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame->width, frame->height);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
	}	
	image_data = cairo_image_surface_get_data(surface);
	assert(image_data);
	memcpy(image_data, frame->data, frame->width * frame->height * 4);
	cairo_surface_mark_dirty(surface);
	
	if(viewer->show_area_settings) {
		draw_area_settings(surface, frame->width, frame->height, viewer);
	}
	
	draw_ai_result(surface, jresult, jcolors, viewer);
	gtk_widget_queue_draw(panel->da);
	return;
}

void input_frame_clear_all(input_frame_t *frame)
{
	json_object *jresult = frame->meta_data;
	frame->meta_data = NULL;
	if(jresult) json_object_put(jresult);
	
	input_frame_clear(frame);
}

gboolean stream_viewer_update_ui(struct stream_viewer * viewer);
static gboolean on_timeout(struct shell_context *shell)
{
	struct app_context *app = shell->app;
	struct shell_private *priv = shell->priv;
	if(priv->quit) {
		priv->timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	
	struct video_stream *streams = NULL;
	ssize_t num_streams = app_get_streams(app, &streams);
	for(int i = 0; i < num_streams; ++i) {
		input_frame_t frame[1];
		memset(frame, 0, sizeof(frame));
		
		long frame_number = streams[i].get_frame(&streams[i], 0, frame);
		if(frame_number <= 0) continue;
		
		da_panel_t *panel = priv->views[i].panel;
		draw_frame(panel, frame, (json_object *)frame->meta_data, priv->jcolors, &priv->views[i]);
		
		input_frame_clear_all(frame);
		
		stream_viewer_update_ui(&priv->views[i]);
	}
	return G_SOURCE_CONTINUE;
}
