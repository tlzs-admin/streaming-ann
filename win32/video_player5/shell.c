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
#include <math.h>

#include "app.h"
#include "shell.h"
#include "shell_private.c.impl"
#include "utils.h"
#include "video_streams.h"
#include <curl/curl.h>


static GdkRGBA s_default_fg = { .red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0 };
static GdkRGBA s_default_bg = { .red = 0.7, .green = 0.7, .blue = 0.7, .alpha = 0.9 }; 
static GdkRGBA s_default_face_bg = { .red = 0.7, .green = 0.7, .blue = 0.0, .alpha = 0.9 }; 

ssize_t app_get_streams(struct app_context *app, struct video_stream **p_streams);
struct streaming_proxy_context *app_get_streaming_proxy(struct app_context *app);

static int shell_reload_config(struct shell_context * shell, json_object * jconfig);
static int shell_init(struct shell_context * shell, json_object *jconfig);
static int shell_run(struct shell_context * shell);
static int shell_stop(struct shell_context * shell);

struct shell_context * shell_context_init(struct shell_context * shell, struct app_context *app)
{
	assert(app);
	if(NULL == shell) shell = calloc(1, sizeof(*shell));
	assert(shell);

	shell->app = app;
	shell->reload_config = shell_reload_config;
	shell->init = shell_init;
	shell->run = shell_run;
	shell->stop = shell_stop;
	
	
	struct shell_private *priv = shell->priv;
	if(NULL == priv) priv = shell_private_new(shell);
	assert(priv);
	shell->priv = priv;
	
	priv->bg.rgba = s_default_bg;
	priv->bg.use_alpha = 1;
	
	priv->fg.rgba = s_default_fg;
	priv->fg.use_alpha = 0;
	
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
	ok = json_object_object_get_ex(jui, "colors", &priv->jcolors);	///< @deprecated
	
	json_object *jglobal_colors = json_object_from_file("colors.json");
	assert(jglobal_colors);
	json_object *jcoco = NULL;
	ok = json_object_object_get_ex(jglobal_colors, "coco", &jcoco);
	assert(ok && jcoco);
	priv->jclass_colors = jcoco;
	
	if(!ok) return -1;
	return 0;
}

static int shell_init(struct shell_context * shell, json_object *jconfig)
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

static ssize_t face_detection_parse_json(json_object * jfaces, struct face_detection **p_faces)
{
	debug_printf("%s()...", __FUNCTION__);
	assert(jfaces && p_faces);
	
	ssize_t count = json_object_array_length(jfaces);
	if(count <= 0) return count;
	
	struct face_detection * faces = calloc(count, sizeof(*faces));
	assert(faces);
	for(ssize_t i = 0; i < count; ++i) {
		json_object * jface = json_object_array_get_idx(jfaces, i);

		faces[i].klass = json_get_value(jface, int, class_index);
		faces[i].confidence = json_get_value(jface, double, confidence);
		faces[i].x = json_get_value(jface, double, left);
		faces[i].y = json_get_value(jface, double, top);
		faces[i].cx = json_get_value(jface, double, width);
		faces[i].cy = json_get_value(jface, double, height);
	}
	*p_faces = faces;
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

static void draw_counters(cairo_t *cr, const int font_size, json_object *jcolors, 
	double width, double height,
	struct stream_viewer *viewer)
{
	struct classes_counter_context * counters = viewer->counter_ctx;
	int num_classes = counters->num_classes;
	if(num_classes <= 0) return;
	
	struct shell_context *shell = viewer->shell;
	assert(shell && shell->priv);
	
	
	const size_t max_line_size = 200;
	int x = 10, y = 10;
	
	char ** text_lines = calloc(num_classes, sizeof(*text_lines));
	assert(text_lines);
	
	long persons_count = -1;
	for(int i = 0; i < num_classes; ++i) {
		text_lines[i] = calloc(max_line_size, 1);
		assert(text_lines[i]);
		if(counters->classes[i].id == 0) {
			persons_count = counters->classes[i].count;
		}
		snprintf(text_lines[i], max_line_size, "%s: %d", _(counters->classes[i].name),  (int)counters->classes[i].count);
	}

	int text_width = calc_text_width(cr, num_classes, text_lines);
	
	struct video_stream *stream = viewer->stream;
	assert(stream);
	
	
	for(int i = 0; i < num_classes; ++i) {
		gboolean color_parsed = FALSE;
		GdkRGBA fg_color;
		const char *color_name = NULL;
		if(counters->classes[i].id < 0 || !counters->classes[i].name[i]) continue;
		if(stream->face_masking_flag && counters->classes[i].id != 0) continue;
		 
		if(jcolors) {
			json_bool ok = FALSE;
			json_object *jcolor = NULL;
			ok = json_object_object_get_ex(jcolors, counters->classes[i].name, &jcolor);
			if(!ok || NULL == jcolor) continue;
			
			if(ok && jcolor) color_name = json_object_get_string(jcolor);
			if(color_name) color_parsed = gdk_rgba_parse(&fg_color, color_name);
		}
		if(!color_parsed) {
			fg_color = shell->priv->fg.rgba;
		//	gdk_rgba_parse(&fg_color, "green"); // default color
		}
		show_text(cr, x, y, font_size, text_lines[i], &fg_color, NULL, text_width);
		y += font_size + 5;
	}
	
	for(int i = 0; i < num_classes; ++i) { free(text_lines[i]); }
	free(text_lines);
	
	
	if(persons_count > 0 && viewer->show_counters_mode2) {
		char text[100] = "";
		snprintf(text, sizeof(text), "%ld 人", persons_count);
		
		
		cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.3);
		cairo_rectangle(cr, width / 30, height / 30, width - width / 30, height - height / 30);
		cairo_fill(cr);
		
		cairo_select_font_face(cr, "IPAGothic", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_line_width(cr, 10);
		cairo_set_font_size(cr, height / 3);
		
		cairo_set_source_rgba(cr, 1, 1, 0, 0.8);
		cairo_text_extents_t extents;
		memset(&extents, 0, sizeof(extents));
		cairo_text_extents(cr, text, &extents);
		
		cairo_move_to(cr, (width - extents.width) / 2, height / 2  + extents.height / 2);
		cairo_show_text(cr, text);
	}
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

static const double face_ratio = 0.7;
static const double opencv_face_scale = 1.3;
static double face_bbox_iou(const struct face_detection *restrict face, const struct darknet_detection *restrict bbox)
{
	assert(bbox && face);
	if((bbox->cx * bbox->cy) < 0.0000001) return 0;
	if((face->cx * face->cy) < 0.0000001) return 0;
	
	struct {
		double x1, y1, x2, y2;
		double radius;
	}rt1, rt2, rt_intersect;
	memset(&rt_intersect, 0, sizeof(rt_intersect));
	
	double x = face->x;
	double y = face->y;
	double cx = face->cx;
	double cy = face->cy;
	double radius = cx / 2.0 * opencv_face_scale;
	double center_x = x + cx / 2;
	double center_y = y + cy / 2;
	
	rt1.radius = radius;
	rt1.x1 = center_x - radius; rt1.x2 = center_x + radius;
	rt1.y1 = center_y - radius; rt1.y2 = center_y + radius;
	
	rt2.x1 = bbox->x; rt2.x2 = bbox->x + bbox->cx;
	rt2.y1 = bbox->y; rt2.y2 = bbox->y + bbox->cy;
	
	if(rt2.x1 > rt1.x2 || rt2.x2 < rt1.x1) return 0.0;
	if(rt2.y1 > rt1.y2 || rt2.y2 < rt1.y1) return 0.0;
	
	rt_intersect.x1 = (rt1.x1 > rt2.x1)?rt1.x1:rt2.x1;
	rt_intersect.x2 = (rt1.x2 < rt2.x2)?rt1.x2:rt2.x2;
	rt_intersect.y1 = (rt1.y1 > rt2.y1)?rt1.y1:rt2.y1;
	rt_intersect.y2 = (rt1.y2 < rt2.y2)?rt1.y2:rt2.y2;
	
	double area_face = rt1.radius * rt1.radius * 4;
	double area_intersect = (rt_intersect.x2 - rt_intersect.x1) * (rt_intersect.y2 - rt_intersect.y1);
	if(area_intersect < 0) area_intersect *= -1;	
	return (area_intersect / area_face);
}

static void draw_face_masking(cairo_t *cr, double width, double height,
	ssize_t num_detections, const struct darknet_detection * dets,
	ssize_t num_faces, const struct face_detection *faces,
	json_object *jcolors, 
	struct stream_viewer *viewer)
{
	
	static const double blur_face_size = 15;
	
	struct shell_context *shell = viewer->shell;
	assert(shell && shell->priv);
	struct shell_private *priv = shell->priv;
	
	char msg[100] = "";
	gchar *color_name = gdk_rgba_to_string(&priv->bg.rgba);
	snprintf(msg, sizeof(msg), 
		"face bg-color: %s (%g,%g,%g,%g)",
		color_name, 
		priv->bg.rgba.red, 
		priv->bg.rgba.green,
		priv->bg.rgba.blue,
		priv->bg.rgba.alpha);
	free(color_name);
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(priv->header_bar), msg);
	
	GdkRGBA face_bg = (priv->bg.rgba.alpha > 0)?priv->bg.rgba:s_default_face_bg;
	
	struct classes_counter_context * counters = viewer->counter_ctx;
	counters->reset(counters);
	struct area_settings_dialog * settings = viewer->settings_dlg;
	
	// draw opencv faces
	for(ssize_t i = 0; i < num_faces; ++i) {
		if(faces[i].confidence < 0.3) continue;
		struct class_counter *counter = NULL;
		int area_index = -1;
		if(settings->num_areas > 0 && settings->areas[0].num_vertexes >= 3) {
			double center_x = faces[i].x + faces[i].cx / 2;
			double center_y = faces[i].y + faces[i].cy / 2;
			area_index = settings->pt_in_area(settings, center_x, center_y);
			
			if(area_index >= 0) {
				counter = counters->add_by_id(counters, faces[i].klass);
			}
		}else {
			counter = counters->add_by_id(counters, faces[i].klass);
		}
		if(counter) strncpy(counter->name, "face", sizeof(counter->name));
		
		double x = faces[i].x * width;
		double y = faces[i].y * height;
		double cx = faces[i].cx * width;
		double cy = faces[i].cy * height;
		
		double radius = cx / 2 * opencv_face_scale;
		double center_x = x + cx / 2;
		double center_y = y + cy / 2;
		
		// todo: blur_face(cr, kernel_size, center_x, center_y, radius);
		/* draw masks */
		cairo_set_source_rgba(cr, 
			face_bg.red, face_bg.green, face_bg.blue, 
			(radius>blur_face_size)?1.0:face_bg.alpha);
		cairo_arc(cr, center_x, center_y, radius, 0.0, 3.1415926 * 2);
		cairo_fill(cr);
	}
	
	for(ssize_t i = 0; i < num_detections; ++i) {
		if(dets[i].class_id != 0) continue; // not person
		
		if(num_faces) {
			double max_iou = -1.0;
			for(ssize_t ii = 0; ii < num_faces; ++ii) {
				double iou = face_bbox_iou(&faces[ii], &dets[i]);
				if(iou > max_iou) max_iou = iou;
			}
			printf("max_iou: %g\n", max_iou);
			if(max_iou > 0.5) continue;
		}
		
		const char *class_name = dets[i].class_name;
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

static int alert_client_notify(struct video_stream *stream, const char *server_url)
{
	static FILE *alert_log;
	if(NULL == alert_log) {
		alert_log = fopen("alerts.log", "a+");
	}
	FILE *fp = alert_log;
	if(NULL == fp) fp = stderr;
	
	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, server_url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	
	CURLcode ret = curl_easy_perform(curl);
	long response_code = -1;
	
	if(ret == CURLE_OK) {
		ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		fprintf(fp, "%s(): response_code = %ld\n", __FUNCTION__, response_code);
		
	}else {
		fprintf(fp, "ERROR: %s(): %s\n", __FUNCTION__, curl_easy_strerror(ret)); 
	}
	curl_easy_cleanup(curl);
	return ret;
}

static void draw_leaving_behind(cairo_t *cr, double width, double height,
	int font_size, 
	ssize_t num_detections, const struct darknet_detection * dets, 
	json_object *jcolors, 
	struct stream_viewer *viewer)
{
	struct classes_counter_context * counters = viewer->counter_ctx;
	counters->reset(counters);
	struct area_settings_dialog * settings = viewer->settings_dlg;
	
	int alert_flags = 0;
	for(ssize_t i = 0; i < num_detections; ++i) {
		gboolean color_parsed = FALSE;
		GdkRGBA fg_color;
		if(dets[i].class_id != 0) continue;
		
		const char *class_name = dets[i].class_name;
		const char *color_name = NULL;
		if(NULL == class_name) continue;
		
		double x = dets[i].x * width;
		double y = dets[i].y * height;
		double cx = dets[i].cx * width;
		double cy = dets[i].cy * height;
		
		double center_x = dets[i].x + dets[i].cx / 2;
		double bottom_y = dets[i].y + dets[i].cy * 0.99;
		
		struct class_counter *counter = NULL;
		int area_index = -1;
		if(settings->num_areas > 0 && settings->areas[0].num_vertexes >= 3) {
			
			
			area_index = settings->pt_in_area(settings, center_x, bottom_y);
			if(area_index < 0) {
				area_index = settings->pt_in_area(settings, dets[i].x + 1, bottom_y); // check left pos
			}
			if(area_index < 0) {
				area_index = settings->pt_in_area(settings, dets[i].x + dets[i].cx - 1, bottom_y); // check right pos
			}
			
			if(area_index >= 0) {
				counter = counters->add_by_id(counters, dets[i].class_id);
				alert_flags = 1;
			}
		}else {
			counter = counters->add_by_id(counters, dets[i].class_id);
			alert_flags = 1;
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
		
		// draw detection points
		const double radius = cy / 120.0;
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_set_line_width(cr, 2);
		cairo_arc(cr, x, bottom_y * height, radius, 0, M_PI * 2);
		cairo_stroke(cr);
		
		cairo_arc(cr, x + cx, bottom_y * height, radius, 0, M_PI * 2);
		cairo_stroke(cr);
		
		cairo_arc(cr, center_x * width, bottom_y *height, radius, 0, M_PI * 2);
		cairo_stroke(cr);
		
		show_text(cr, x, y, font_size, _(class_name), &fg_color, NULL, -1);
	}
	
	if(alert_flags) {
		cairo_rectangle(cr, 0, 0, width, height);
		cairo_set_source_rgba(cr, 1, 0, 0, 0.2);
		cairo_paint(cr);
		
		cairo_set_source_rgba(cr, 1, 0, 0, 1);
		cairo_set_line_width(cr, height / 20);
		cairo_stroke(cr);
		
		struct video_stream *stream = viewer->stream;
		for(ssize_t i = 0; i < stream->num_alert_servers; ++i) {
			if(NULL == stream->alert_server_urls[i]) continue;
			alert_client_notify(stream, stream->alert_server_urls[i]);
		}
	}
}

static void draw_ai_result(cairo_surface_t *surface, json_object *jresult, json_object *jcolors, struct stream_viewer *viewer)
{
	assert(surface);
	if(NULL == jresult) return;
	
	struct shell_context *shell = viewer->shell;
	assert(jcolors == shell->priv->jclass_colors);
	
	double width = cairo_image_surface_get_width(surface);
	double height = cairo_image_surface_get_height(surface);
	if(width < 1 || height < 1) return;
	
	const double font_size = (double)height / 32; 
	const double line_width = (double)height / 240;
	const char * font_family = "IPAGothic"; 
	
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
	
	struct video_stream *stream = viewer->stream;
	assert(stream);
	if(stream->detection_mode == 1) { //
		draw_leaving_behind(cr, width, height, font_size, num_detections, dets, jcolors, viewer);
	}else if(!stream->face_masking_flag) {
		draw_bounding_boxes(cr, width, height, font_size, 
			num_detections, dets, jcolors, viewer);
	}
	if(stream->face_masking_flag) {
		debug_printf("jresult(+faces): %s\n", json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PRETTY));
		json_object * jfaces = NULL;
		ok = json_object_object_get_ex(jresult, "faces", &jfaces);
		AUTO_FREE_PTR struct face_detection * faces = NULL;
		ssize_t num_faces = 0;
		if(ok && jfaces) {
			num_faces = face_detection_parse_json(jfaces, &faces);
		}
		draw_face_masking(cr, width, height, 
			num_detections, dets, 
			num_faces, faces,
			jcolors, viewer);
	}

	if(viewer->show_counters) draw_counters(cr, font_size, jcolors, width, height, viewer);
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

static void update_output_channel(struct video_stream *stream, long frame_number, const unsigned char *bgra_data, int width, int height)
{
	assert(stream && stream->app);
	struct app_context *app = stream->app;
	struct streaming_proxy_context *proxy = app_get_streaming_proxy(app);
	assert(proxy);
	
	struct channel_context *channel = NULL;
	if(proxy) channel = proxy->find_channel_by_name(proxy, stream->channel_name);
	assert(channel);
	
	if(channel && bgra_data && width > 0 && height > 0) {
		unsigned char *jpeg = NULL;
		struct bgra_image bgra[1] = {{
			.data = (unsigned char *)bgra_data,
			.width = width,
			.height = height,
			.channels = 4,
		}};
		ssize_t cb_jpeg = bgra_image_to_jpeg_stream(bgra, &jpeg, 90);
		if(jpeg && cb_jpeg > 0) {
			channel->set_output_frame(channel, frame_number, width, height, jpeg, cb_jpeg);
		}
		free(jpeg);
	}
	return;
}

static void draw_frame(da_panel_t *panel, const input_frame_t *frame, json_object *jresult, json_object *jcolors, struct stream_viewer *viewer)
{
	if(NULL == frame || NULL == frame->data || frame->width < 1 || frame->height < 1) return;
	if(viewer->is_busy) return;
	viewer->is_busy = 1;
	
	cairo_surface_t *surface = panel->surface;
	unsigned char *image_data;
	if(NULL == surface  
		|| panel->image_width != frame->width 
		|| panel->image_height != frame->height) 
	{
		if(surface) cairo_surface_destroy(surface);
		surface = panel->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, frame->width, frame->height);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		
		panel->image_width = frame->width;
		panel->image_height = frame->height;
		
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
	
	update_output_channel(viewer->stream, frame->frame_number, image_data, frame->width, frame->height);
	
	viewer->is_busy = 0;
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
struct shell_context *app_get_shell(struct app_context *app);

//~ gboolean shell_update_frame(gpointer user_data)
//~ {
	//~ struct video_stream *stream = user_data;
	//~ assert(stream);
	//~ struct app_context *app = stream->app;
	//~ struct shell_context *shell = app_get_shell(app);
	//~ struct shell_private *priv = shell->priv;
	
	//~ struct video_stream *streams = NULL;
	//~ ssize_t num_streams = app_get_streams(app, &streams);
	//~ ssize_t stream_index = -1;
	//~ for(ssize_t i = 0; i < num_streams; ++i) {
		//~ if(stream == &streams[i]) {
			//~ stream_index = i;
			//~ break;
		//~ }
	//~ }
	//~ if(stream_index < 0 || stream_index >= num_streams) return G_SOURCE_REMOVE;
	
	//~ if(stream->paused) return G_SOURCE_REMOVE;
	//~ input_frame_t frame[1];
	//~ memset(frame, 0, sizeof(frame));
	
	//~ long frame_number = stream->get_frame(stream, 0, frame);
	//~ if(frame_number < 0 || NULL == frame->data) {
		//~ input_frame_clear_all(frame);
		//~ return G_SOURCE_REMOVE;
	//~ }
	
	
	//~ static long s_frame_number = 0;
	//~ GtkWidget *header_bar = priv->header_bar;
	//~ char title[100] = "";
	
	//~ assert(frame->type == input_frame_type_jpeg);
	//~ app_timer_t timer[1];
	//~ double time_elapsed_jpeg_decode = 0.0;
	//~ double time_elapsed_draw = 0.0;
	//~ double time_elapsed_cleanup = 0.0;
	//~ app_timer_start(timer);
	
	//~ input_frame_t bgra_frame[1];
	//~ memset(bgra_frame, 0, sizeof(bgra_frame));
	//~ bgra_image_from_jpeg_stream(bgra_frame->bgra, frame->data, frame->length);
	//~ time_elapsed_jpeg_decode = app_timer_stop(timer);
	
	
	//~ da_panel_t *panel = priv->views[stream_index].panel;
	//~ json_object *jclass_colors = priv->jclass_colors;
	//~ if(NULL == jclass_colors) {
		//~ jclass_colors = priv->jcolors;
	//~ }
	//~ app_timer_start(timer);
	//~ draw_frame(panel, bgra_frame, (json_object *)frame->meta_data, jclass_colors, &priv->views[stream_index]);
	//~ time_elapsed_draw = app_timer_stop(timer);
	
	//~ app_timer_start(timer);
	//~ input_frame_clear_all(frame);
	//~ input_frame_clear(bgra_frame);
	
	//~ time_elapsed_cleanup = app_timer_stop(timer);
	//~ // stream_viewer_update_ui(&priv->views[i]);
	
	//~ snprintf(title, sizeof(title), "frame: %ld, jpeg_decod=%.3f, draw=%.3f, cleanup=%.3f", 
		//~ ++s_frame_number,
		//~ time_elapsed_jpeg_decode, time_elapsed_draw, time_elapsed_cleanup);
	//~ gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), title);
	
	//~ input_frame_clear_all(frame);
	//~ return G_SOURCE_REMOVE;
//~ }

static gboolean on_timeout(struct shell_context *shell)
{
	struct app_context *app = shell->app;
	struct shell_private *priv = shell->priv;
	if(priv->quit) {
		priv->timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	
	static long s_frame_number = 0;
	GtkWidget *header_bar = priv->header_bar;
	char title[100] = "";
	
	struct video_stream *streams = NULL;
	ssize_t num_streams = app_get_streams(app, &streams);
	for(int i = 0; i < num_streams; ++i) {
		struct video_stream *stream = &streams[i];
		if(stream->paused) continue;
		
		input_frame_t frame[1];
		memset(frame, 0, sizeof(frame));
		
		long frame_number = stream->get_frame(stream, 0, frame);
		if(frame_number < 0 || NULL == frame->data) continue;
		
		assert(frame->type == input_frame_type_jpeg);
		
		app_timer_t timer[1];
		double time_elapsed_jpeg_decode = 0.0;
		double time_elapsed_draw = 0.0;
		double time_elapsed_cleanup = 0.0;
		app_timer_start(timer);
		
		input_frame_t bgra_frame[1];
		memset(bgra_frame, 0, sizeof(bgra_frame));
		bgra_image_from_jpeg_stream(bgra_frame->bgra, frame->data, frame->length);
		time_elapsed_jpeg_decode = app_timer_stop(timer);
		
		
		da_panel_t *panel = priv->views[i].panel;
		
		json_object *jclass_colors = priv->jclass_colors;
		if(NULL == jclass_colors) {
			jclass_colors = priv->jcolors;
		}
		app_timer_start(timer);
		draw_frame(panel, bgra_frame, (json_object *)frame->meta_data, jclass_colors, &priv->views[i]);
		time_elapsed_draw = app_timer_stop(timer);
		
		app_timer_start(timer);
		input_frame_clear_all(frame);
		input_frame_clear(bgra_frame);
		
		time_elapsed_cleanup = app_timer_stop(timer);
		// stream_viewer_update_ui(&priv->views[i]);
		
		snprintf(title, sizeof(title), "frame: %ld, jpeg_decod=%.3f, draw=%.3f, cleanup=%.3f", 
			++s_frame_number,
			time_elapsed_jpeg_decode, time_elapsed_draw, time_elapsed_cleanup);
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), title);
	}
	return G_SOURCE_CONTINUE;
}
