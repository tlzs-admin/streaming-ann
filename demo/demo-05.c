/*
 * demo-05.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
 * 
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
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include "ann-plugin.h"
#include "io-input.h"
#include "input-frame.h"
#include "ai-engine.h"

#include "utils.h"
#include "da_panel.h"
#include <getopt.h>
#include <libgen.h>	// dirname() || basename()
#include <locale.h>

#include <ppm.h>

//~ #include <tesseract/baseapi.h>
#include <tesseract/capi.h>
#include <leptonica/allheaders.h>

#define AI_PLUGIN_DEFAULT "ai-engine::tesseract-ocr"

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult);


struct tesseract_ocr
{
	TessBaseAPI * handle;
	void * user_data;
	int (* process)(struct tesseract_ocr * ocr, const PIX * img, char ** p_utf8_text);
	void (*free_text)(const char * text);
};
struct tesseract_ocr * tesseract_ocr_init(struct tesseract_ocr * ocr, const char * lang, void * user_data);
void tesseract_ocr_cleanup(struct tesseract_ocr * ocr);
int tesseract_ocr_process(struct tesseract_ocr * ocr, const PIX * img, char **p_utf8_text);


/********************************************
 * tesseract ocr 
********************************************/
struct tesseract_ocr * tesseract_ocr_init(struct tesseract_ocr * ocr, const char * lang, void * user_data)
{
	if(NULL == ocr) ocr = calloc(1, sizeof(*ocr));
	if(NULL == lang) lang = "jpn";
	
	ocr->user_data = user_data;
	ocr->process = tesseract_ocr_process;
	ocr->handle = TessBaseAPICreate();
	ocr->free_text = TessDeleteText;
	
	assert(ocr->handle);
	
	int rc = TessBaseAPIInit3(ocr->handle, NULL, lang);
	assert(0 == rc);
	
	return ocr;
}
void tesseract_ocr_cleanup(struct tesseract_ocr * ocr)
{
	if(NULL == ocr || NULL == ocr->handle) return;
	TessBaseAPIEnd(ocr->handle);
	TessBaseAPIDelete(ocr->handle);
	ocr->handle = NULL;
}

int tesseract_ocr_process(struct tesseract_ocr * ocr, const PIX * img, char ** p_utf8_text)
{
	assert(ocr && ocr->handle);
	int rc = 0;
	TessBaseAPISetImage2(ocr->handle, (PIX *)img);
	if(NULL == img) return -1;
	
	rc = TessBaseAPIRecognize(ocr->handle, NULL);
	if(rc) {
		fprintf(stderr, "[ERROR]: %s(): recognize failed.\n", __FUNCTION__);
		return -1;
	}
	
	char * utf8_text = TessBaseAPIGetUTF8Text(ocr->handle);
	*p_utf8_text = utf8_text; 
	
	return 0;
}


typedef struct shell_context shell_context_t;
struct global_params
{
	char * conf_file;
	json_object * jconfig;
	json_object * jshell;
	
	char app_path[PATH_MAX];
	char app_name[256];
	
	ssize_t num_langs;
	char ** langs;
	char * ocr_lang;
	
	bgra_image_t image[1];
	
	union {
		ai_engine_t * ai;
		struct tesseract_ocr *ocr;
	};
	
	char * work_dir;
	shell_context_t * shell;
};
void global_params_cleanup(struct global_params * params)
{
	if(NULL == params) return;
	if(params->ocr) {
		tesseract_ocr_cleanup(params->ocr);
		free(params->ocr);
		params->ocr = NULL;
	}
}

struct shell_context
{
	struct global_params * params;
	int (* load_config)(struct shell_context * shell, json_object * jconfig);
	int (* run)(struct shell_context * shell);
	int (* stop)(struct shell_context * shell);
	
	GtkWidget * window;
	GtkWidget * header_bar;
	GtkWidget * file_explorer;
	GtkWidget * file_chooser_btn;
	GtkWidget * preview;
	char current_folder[PATH_MAX];
	char current_file[PATH_MAX];
	
	da_panel_t panels[1];
	
	int quit;
	
	ssize_t num_langs;
	char ** langs;
	char * ocr_lang;
	
	GtkTextView * textview;
	GtkTextBuffer * textbuf;
};

shell_context_t * shell_context_new(int argc, char ** argv, struct global_params * params);
void shell_context_cleanup(shell_context_t * shell);


static struct global_params g_params[1];
static int parse_args(int argc, char ** argv, struct global_params * params);

static ssize_t list_languages(char *** p_langs)
{
	ssize_t count = 0;
	FILE * fp = popen("tesseract --list-langs", "r");
	if(!fp) {
		perror("tesseract not found.");
		return -1;
	}
	
	char buf[1024] = "";
	char * line = NULL;
	
#define MAX_LANGS (256)
	char ** langs = calloc(MAX_LANGS, sizeof(*langs));
	assert(langs);
	
	while((line = fgets(buf, sizeof(buf) - 1, fp)))
	{
		size_t cb = strlen(line);
		while(cb > 0 && line[cb - 1] == '\n') {
			line[--cb] = '\0';
		}
		if(cb == 0) continue; // skip empty lines
		if(strncasecmp(line, "list of", 7) == 0) continue; // skip comments
		
		langs[count++] = strdup(line);
		assert(count < MAX_LANGS);
		memset(buf, 0, sizeof(buf));
	}
#undef MAX_LANGS
	
	int rc = pclose(fp);
	if(rc == 0) {
		*p_langs = langs;
		return count;
	}
	
	if(rc == -1) perror("tesseract error.");
	else fprintf(stderr, "tesseract failed with code %d\n", rc);
	
	for(ssize_t i = 0; i < count; ++i) {
		free(langs[i]);
	}
	free(langs);
	return -1;
}

int main(int argc, char **argv)
{
	struct global_params * params = g_params;
	int rc = parse_args(argc, argv, params);
	assert(0 == rc);
	
	shell_context_t * shell = shell_context_new(argc, argv, params);
	assert(shell);
	rc = shell->run(shell);
	
	global_params_cleanup(params);


	return 0;

    return 0;
}

static void print_usuage(const char * exe_name){
	fprintf(stderr, "Usuage: %s [--conf_file=<config.json>] [--work_dir=<$(pwd)>] [--help]\n", exe_name);
}

static int get_app_path_name(const char * filename, char app_path[], size_t path_size, char app_name[], size_t name_size)
{
	char resolved_path[PATH_MAX] = "";
	char * path_name = realpath(filename, resolved_path);
	if(NULL == path_name) {
		fprintf(stderr, "[ERROR]: get_app_path(%s) failed.\n", filename);
		return -1;
	}
	
	char * name = basename(path_name);
	assert(name);
	
	char * path = dirname(path_name);
	assert(path);
	
	if(app_path) strncpy(app_path, path, path_size);
	if(app_name) strncpy(app_name, name, name_size);
	return 0;
}

static json_object * generate_default_config(void)
{
	json_object * jconfig = json_object_new_object();
	json_object * jshell = json_object_new_object();
	json_object * jai_engine = json_object_new_object();
	
	json_object_object_add(jai_engine, "type", json_object_new_string(AI_PLUGIN_DEFAULT));
	json_object_object_add(jai_engine, "lang", json_object_new_string("jpn"));
	json_object_object_add(jconfig, "ai-engine", jai_engine);
	
	json_object * jcolors = json_object_new_object();
	json_object_object_add(jcolors, "default", json_object_new_string("green"));
	json_object_object_add(jshell, "colors", jcolors);
	json_object_object_add(jconfig, "shell", jshell);
	
	fprintf(stderr, "%s(): \n%s\n", __FUNCTION__, 
		json_object_to_json_string_ext(jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE));
	return jconfig;
}
static int parse_args(int argc, char ** argv, struct global_params * params)
{
	assert(params);
	int rc = 0;
	
	char * lc_lang = getenv("LANG");
	printf("CURRENT LOCALE: LANG=%s\n", lc_lang);
	
	ssize_t count = 0;
	char ** langs = NULL;
	count = list_languages(&langs);
	assert(count > 0);
	params->langs = langs;
	params->num_langs = count;
	
	rc = get_app_path_name(argv[0], 
		params->app_path, sizeof(params->app_path), 
		params->app_name, sizeof(params->app_name));
	assert(0 == rc && params->app_path[0] && params->app_name[0]);
	debug_printf("app_path: %s, app_name: %s\n", params->app_path, params->app_name);
	
	static struct option options[] = {
		{"conf_file", required_argument, 0, 'c'},
		{"work_dir", required_argument, 0, 'w'},
		{"lang", required_argument, 0, 'l'},
		{"help", no_argument, 0, 'h'},
		{NULL},
	};
	int option_index = 0;
	
	const char * conf_file = NULL;
	const char * work_dir = NULL;
	const char * lang = NULL;
	while(1) {
		int c = getopt_long(argc, argv, "c:hw:", options, &option_index);
		if(-1 == c) break;
		switch(c) {
		case 'c': conf_file = optarg; break;
		case 'w': work_dir = optarg; break;
		case 'l': lang = optarg; break;
		case 'h':
			print_usuage(argv[0]);
			exit(0);
		default:
			fprintf(stderr, "[ERROR]: unknown args '%c'(%.2x)\n", c, (unsigned char)c);
			exit(1);
		}
	}
	
	char buf[PATH_MAX] = ""; 
	if(NULL == conf_file) {
		snprintf(buf, sizeof(buf), "%s.json", params->app_name);
		conf_file = buf;
	}
	if(NULL == work_dir) work_dir = params->app_path;
	
	params->conf_file = strdup(conf_file);
	params->work_dir = params->app_path;
	
	json_object * jconfig = json_object_from_file(conf_file);
	if(NULL == jconfig) jconfig = generate_default_config();
	assert(jconfig);
	params->jconfig = jconfig;
	
	json_bool ok = FALSE;
	json_object * jai_engine = NULL;
	ok = json_object_object_get_ex(jconfig, "ai-engine", &jai_engine);
	assert(ok && jai_engine);
	
	if(NULL == lang) {
		lang = json_get_value(jai_engine, string, lang);
		if(NULL == lang) lang = "jpn";
		params->ocr_lang = strdup(lang);
	}
	
	assert(lang);
	params->ocr = tesseract_ocr_init(NULL, lang, params);
	assert(params->ocr);
	
	return rc;
}

static shell_context_t g_shell[1];
static void init_windows(shell_context_t * shell);
static int shell_load_config(struct shell_context * shell, json_object * jconfig);
static int shell_run(struct shell_context * shell);
static int shell_stop(struct shell_context * shell);

shell_context_t * shell_context_new(int argc, char ** argv, struct global_params * params)
{
	gtk_init(&argc, &argv);
	shell_context_t * shell = g_shell;
	shell->params = params;
	shell->load_config = shell_load_config;
	shell->run = shell_run;
	shell->stop = shell_stop;
	
	strncpy(shell->current_folder, params->work_dir, sizeof(shell->current_folder));
	init_windows(shell);
	return shell;
}

static void on_update_preview(GtkFileChooser * file_chooser, 
	GtkWidget * preview)
{
	char * filename = gtk_file_chooser_get_filename(file_chooser);
	if(NULL == filename) return;
	
	GdkPixbuf *image = gdk_pixbuf_new_from_file_at_size(filename, 256, 256, NULL);
	g_free(filename);
	
	gtk_image_set_from_pixbuf(GTK_IMAGE(preview), image);
	if(image) g_object_unref(image);
	gtk_file_chooser_set_preview_widget_active(file_chooser, NULL != image);
}

static void on_current_folder_changed(GtkFileChooser * file_chooser, shell_context_t * shell)
{
	char * current_folder = gtk_file_chooser_get_current_folder(file_chooser);
	if(NULL == current_folder) return;
	strncpy(shell->current_folder, current_folder, sizeof(shell->current_folder));
	g_free(current_folder);
}


static gboolean is_regular_file(const char *path_name)
{
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc < 0) return FALSE;
	
	return ((st->st_mode & S_IFMT) == S_IFREG);
	
}
static void on_file_selection_changed(GtkFileChooser * file_chooser, shell_context_t * shell)
{
	assert(shell && shell->params);
	struct global_params * params = shell->params;
	int rc = 0;
	
	// clear contents
	gtk_text_buffer_set_text(shell->textbuf, "", 0);
	
	
	char * filename = gtk_file_chooser_get_filename(file_chooser);
	if(!filename || !filename[0] || !is_regular_file(filename)) return;

	strncpy(shell->current_file, filename, sizeof(shell->current_file));
	g_free(filename);
	filename = shell->current_file;
	debug_printf("filename: %s\n", filename);

	PIX * img = pixRead(filename);
	if(NULL == img) return;
	if(img->spp != 3 && img->spp != 4) {
		fprintf(stderr, "[ERROR]: invalid color format (spp=%d).\n", img->spp);
		pixDestroy(&img);
	}
	
	// rgb to bgra
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	frame->type = input_frame_type_bgra;
	frame->width = img->w;
	frame->height = img->h;
	frame->channels = 4;
	frame->stride = frame->width * 4;
	unsigned char * image_data = malloc(frame->width * frame->height * 4);
	uint32_t * src_data = img->data;
	//~ if(img->spp == 4) 
	{
		// RGBA TO ARGB
		for(size_t i = 0; i < img->w * img->h; ++i)
		{
			*(uint32_t *)(image_data + i * 4) = be32toh(src_data[i]);
			unsigned char c = image_data[i*4+2];
			image_data[i*4+2] = image_data[i*4+0];
			image_data[i*4+0] = c;
			image_data[i*4+3] = 255;
			//~ uint8_t r = image_data[i*4 + 0];
			//~ image_data[i*4 + 0] = image_data[i*4 + 2];
			//~ image_data[i*4 + 2] = r; 
		}
	}
	
	frame->data = image_data;
	
	static GdkCursor * cursor_default = NULL;
	static GdkCursor * cursor_wait = NULL;
	if(NULL == cursor_default || NULL == cursor_wait) {
		GdkDisplay * display = gtk_widget_get_display(shell->window);
		cursor_default = gdk_cursor_new_from_name(display, "default");
		cursor_wait = gdk_cursor_new_from_name(display, "wait");
	}
	
	gdk_window_set_cursor(gtk_widget_get_window(shell->window), cursor_wait);
	gtk_widget_set_sensitive(shell->window, FALSE);
	
	struct tesseract_ocr * ocr = params->ocr;
	assert(ocr);
	
	
	// ocr
	char * text = NULL;
	rc = ocr->process(ocr, img, &text);
	if(0 == rc && text) {
		gtk_text_buffer_set_text(shell->textbuf, text, strlen(text));
		ocr->free_text(text);
	}
	pixDestroy(&img);
	

	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(shell->header_bar), filename);	
	draw_frame(&shell->panels[0], frame, NULL);
	input_frame_clear(frame);
	
	
	gdk_window_set_cursor(gtk_widget_get_window(shell->window), cursor_default);
	gtk_widget_set_sensitive(shell->window, TRUE);
	
	return;
}

static gboolean on_file_chooser_mode_changed(GtkSwitch * switch_btn, gboolean state, shell_context_t * shell)
{
	assert(shell && shell->file_explorer);
	gtk_widget_set_sensitive(shell->file_chooser_btn, state);
	
	if(state) {
		gtk_widget_hide(shell->file_explorer);
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(shell->file_chooser_btn), shell->current_folder);
	}
	else {
		gtk_widget_show(shell->file_explorer);
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(shell->file_explorer), shell->current_folder);
	}
	
	return FALSE;
}

static void init_windows(shell_context_t * shell)
{
	assert(shell && shell->params);
	struct global_params * params = shell->params;
	
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), params->app_name);

	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	
	GtkWidget * vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start(GTK_BOX(vbox), vpaned, TRUE, TRUE, 0);
	
	// filechooser mode switch
	GtkWidget * switch_btn = gtk_switch_new();
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), switch_btn);
	g_signal_connect(switch_btn, "state-set", G_CALLBACK(on_file_chooser_mode_changed), shell);
	
	// file-explorer mode
	GtkWidget * file_explorer;
	file_explorer = gtk_file_chooser_widget_new(GTK_FILE_CHOOSER_ACTION_OPEN);
	gtk_paned_add1(GTK_PANED(vpaned), file_explorer);
	
	GtkWidget * preview = gtk_image_new();	
	gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(file_explorer), preview);
	shell->file_explorer = file_explorer;
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(file_explorer), shell->current_folder);
	g_signal_connect(file_explorer, "update-preview", G_CALLBACK(on_update_preview), preview);
	g_signal_connect(file_explorer, "current-folder-changed", G_CALLBACK(on_current_folder_changed), shell);
	g_signal_connect(file_explorer, "selection-changed", G_CALLBACK(on_file_selection_changed), shell);
	
	// file-button mode
	GtkWidget * file_chooser_btn = gtk_file_chooser_button_new("image files", GTK_FILE_CHOOSER_ACTION_OPEN);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), file_chooser_btn);
	gtk_widget_set_sensitive(file_chooser_btn, FALSE);
	shell->file_chooser_btn = file_chooser_btn;
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(file_chooser_btn), params->work_dir);
	g_signal_connect(file_chooser_btn, "current-folder-changed", G_CALLBACK(on_current_folder_changed), shell);
	g_signal_connect(file_chooser_btn, "selection-changed", G_CALLBACK(on_file_selection_changed), shell);
	
	GtkFileFilter * filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "images");
	gtk_file_filter_add_mime_type(filter, "image/jpeg");
	gtk_file_filter_add_mime_type(filter, "image/png");
	gtk_file_filter_add_pattern(filter, "*.ppm");
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(file_explorer), filter);
	
	filter = g_object_ref(filter);
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(file_chooser_btn), filter);
	
	GtkWidget * hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_add2(GTK_PANED(vpaned), hpaned);
	
	da_panel_t * panel = da_panel_init(&shell->panels[0], 640, 480, shell);
	gtk_paned_add1(GTK_PANED(hpaned), panel->frame);
	panel->keep_ratio = 1;
	
	GtkWidget * scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_paned_add2(GTK_PANED(hpaned), scrolled_win);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_win), GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_widget_set_size_request(scrolled_win, 300, -1);
	gtk_widget_set_hexpand(scrolled_win, TRUE);
	gtk_widget_set_vexpand(scrolled_win, TRUE);
	GtkWidget * textview = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
	gtk_container_add(GTK_CONTAINER(scrolled_win), textview);
	shell->textview = GTK_TEXT_VIEW(textview);
	shell->textbuf = gtk_text_view_get_buffer(shell->textview);
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	
	shell->window = window;
	shell->header_bar = header_bar;
	return;
}

static int shell_load_config(struct shell_context * shell, json_object * jconfig)
{
	return 0;
}

static int shell_run(struct shell_context * shell)
{
	gtk_main();
	return 0;
}

static int shell_stop(struct shell_context * shell)
{
	shell->quit = 1;
	gtk_main_quit();
	return 0;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
	shell_context_t * shell = panel->shell;
	assert(shell);
	assert(frame->width > 1 && frame->height > 1);
	cairo_surface_t * surface = panel->surface;
	
	bgra_image_t bgra_buf[1];
	memset(bgra_buf, 0, sizeof(bgra_buf));
	
	int rc = 0;
	bgra_image_t * bgra = NULL;
	enum input_frame_type type = frame->type & input_frame_type_image_masks;
	
	switch(type) {
	case input_frame_type_bgra: 
		bgra = (bgra_image_t *)frame->bgra; 
		break;
	case input_frame_type_jpeg:
	case input_frame_type_png:
		bgra = bgra_buf;
		if(type == input_frame_type_jpeg) rc = bgra_image_from_jpeg_stream(bgra, frame->data, frame->length);
		else rc = bgra_image_from_png_stream(bgra, frame->data, frame->length);
		assert(0 == rc);
		break;
	default:
		break;
	}
	assert(bgra);
	
	
	if(NULL == panel->surface 
		|| panel->image_width != bgra->width || panel->image_height != bgra->height)
	{
		panel->surface = NULL;
		if(surface) cairo_surface_destroy(surface);
		
		unsigned char * data = realloc(panel->image_data, bgra->width * bgra->height * 4);
		assert(data);
		
		panel->image_data = data;
		panel->image_width = bgra->width;
		panel->image_height = bgra->height;
		
		surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
			bgra->width, bgra->height, 
			bgra->width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		
		panel->surface = surface;
	}
	
	memcpy(panel->image_data, bgra->data, bgra->width * bgra->height * 4);
	cairo_surface_mark_dirty(surface);
	
	gtk_widget_queue_draw(panel->da);
	
	bgra_image_clear(bgra_buf);
	return;
}

