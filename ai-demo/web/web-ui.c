/*
 * web-ui.c
 * 
 * Copyright 2023 chehw <hongwei.che@gmail.com>
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

#include <libsoup/soup.h>
#include <dirent.h>
#include <glib.h>
#include <gio/gio.h>

#include "utils.h"


struct file_content
{
	char *content_type;
	unsigned char *data;
	ssize_t length;
};
void file_content_free(struct file_content *content)
{
	if(NULL == content) return;
	
	if(content->content_type) free(content->content_type);
	if(content->data) free(content->data);
	memset(content, 0, sizeof(*content));
	free(content);
	
	return;
}

struct file_content *file_content_new_from_file(struct file_content *content, const char *root_dir, const char *filename)
{
	if(NULL == content) content = calloc(1, sizeof(*content));
	if(NULL == root_dir) root_dir = ".";
	assert(filename);
	
	char fullname[PATH_MAX] = "";
	snprintf(fullname, sizeof(fullname) - 1, "%s/%s", root_dir, filename);
	
	unsigned char *data = NULL;
	ssize_t cb_data = load_binary_data(fullname, &data);
	
	gboolean uncertain = TRUE;
	char *content_type = g_content_type_guess(fullname, data, cb_data, &uncertain);
	if(uncertain) content_type = strdup("application/octet-stream");
	
	content->content_type = content_type;
	content->data = data;
	content->length = cb_data;
	
	return content;
}


struct webui_context
{
	void *user_data;
	char * document_root;
	
	size_t num_static_files;
	char **static_files_namelist;
	struct file_content **contents;

	SoupServer *server;
	SoupSession *reverse_proxy;
	
	char *proxy_base_url;
};


struct webui_context g_webui[1] = {{
	.document_root = "./web",
	.proxy_base_url = "http://localhost:8800",
}};

static int filenames_list_add(char **list, size_t index, const char *parent_dir, const char *filename)
{
	assert(filename);
	char path_name[PATH_MAX] = "";
	
	if(parent_dir) snprintf(path_name, sizeof(path_name) - 1, "%s/%s", parent_dir, filename);
	else strncpy(path_name, filename, sizeof(path_name) - 1);
	
	list[index] = strdup(path_name);
	
	printf("    ==> add file: %s\n", list[index]);
	return 0;
}


static ssize_t reload_static_filenames(struct webui_context *webui)
{
#define MAX_SUBFOLDERS	(256)
#define FILELIST_SIZE	(1024)

	if(NULL == webui->document_root) webui->document_root = ".";
	
	ssize_t num_subfolders = 0;
	char *subfolders[MAX_SUBFOLDERS] = { NULL };
	
	size_t num_files = 0;
	char **filenames_list = calloc(FILELIST_SIZE, sizeof(*filenames_list));
	
	// load filelist
	struct dirent *file_entry = NULL;
	DIR *dir = opendir(webui->document_root);
	assert(dir);
	while((file_entry = readdir(dir))) {
		if(file_entry->d_type == DT_REG || file_entry->d_type == DT_LNK) {
			assert(num_files < FILELIST_SIZE);
			filenames_list_add(filenames_list, num_files++, NULL, file_entry->d_name);
			continue;
		}
		if(file_entry->d_type != DT_DIR) continue;
		if(file_entry->d_name[0] == '.') continue;
		
		assert(num_subfolders < MAX_SUBFOLDERS);
		subfolders[num_subfolders++] = strdup(file_entry->d_name);
	}
	closedir(dir);
	
	char parent_dir[PATH_MAX] = "";
	for(size_t i = 0; i < num_subfolders; ++i) {
		snprintf(parent_dir, sizeof(parent_dir) - 1, "%s/%s", webui->document_root, subfolders[i]);
		
		printf("== parent_dir: %s\n", parent_dir);
		dir = opendir(parent_dir);
		assert(dir);
		while((file_entry = readdir(dir))) {
			if(file_entry->d_type == DT_REG) {
				assert(num_files < FILELIST_SIZE);
				filenames_list_add(filenames_list, num_files++, subfolders[i], file_entry->d_name);
				continue;
			}
		}
		closedir(dir);
	}
	
	webui->num_static_files = num_files;
	webui->static_files_namelist = realloc(filenames_list, num_files * sizeof(*filenames_list));
	
	webui->contents = calloc(num_files, sizeof(*webui->contents));
	
#undef MAX_SUBFOLDERS
#undef FILELIST_SIZE

	return num_files;
}


static void on_document_root(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct webui_context *webui = user_data;
	assert(webui);
	
	if(msg->method != SOUP_METHOD_GET || NULL == path || path[0] != '/') {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	++path;
	const char *path_name = NULL;
	struct file_content *content = NULL;
	for(size_t i = 0; i < webui->num_static_files; ++i) {
		if(strcmp(path, webui->static_files_namelist[i]) == 0) {
			path_name = path;
			
			content = webui->contents[i];
			if(NULL == content) {
				content = file_content_new_from_file(NULL, webui->document_root, path_name);
			}
			break;
		}
	}
	if(NULL == path_name) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}
	
	if(NULL == content) {
		soup_message_set_response(msg, "text/plain", SOUP_MEMORY_STATIC, "no content\n", 11);
		soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
	}
	
	
	soup_message_set_response(msg, content->content_type, SOUP_MEMORY_COPY, (char *)content->data, content->length);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	return;
	
}

static void on_proxy_channels(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct webui_context *webui = user_data;
	assert(webui);
	
	if(msg->method != SOUP_METHOD_GET || NULL == path || path[0] != '/') {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	char url[4096] = "";
	snprintf(url, sizeof(url), "%s%s", webui->proxy_base_url, path);
	fprintf(stderr, "%s(): upstream=%s\n", __FUNCTION__, url);
	
	SoupSession *session = webui->reverse_proxy;
	SoupMessage *request = soup_message_new(msg->method, url);
	assert(session && request);
	
	guint response_code = soup_session_send_message(session, request);
	if(response_code >= 200 && response_code < 300) {
		
		const char *key = NULL, *value = NULL;
		SoupMessageHeadersIter iter;
		soup_message_headers_iter_init(&iter, request->response_headers);
		while(soup_message_headers_iter_next(&iter, &key, &value))
		{
			if(key && value) soup_message_headers_append(msg->response_headers, key, value);
			key = NULL;
			value = NULL;
		}
		
		const char *content_type = soup_message_headers_get_content_type(request->response_headers, NULL);
		assert(content_type);
		
		if(request->response_body->length > 0) {
			soup_message_set_response(msg, content_type, SOUP_MEMORY_COPY, 
				request->response_body->data,
				request->response_body->length);
		}
		g_object_unref(request);
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	soup_message_set_status(msg, response_code);
	return;
}


int main(int argc, char **argv)
{
	SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "webui/v0.1.0-alpha", NULL);
	assert(server);
	SoupSession *proxy = soup_session_new_with_options(SOUP_SESSION_USER_AGENT, "webui-proxy/v0.1.0-alpha", NULL);
	assert(proxy);

	
	struct webui_context *webui = g_webui;
	webui->server = server;
	webui->reverse_proxy = proxy;
	webui->document_root = "../web";
	
	ssize_t num_files = reload_static_filenames(webui); 
	
	fprintf(stderr, "filelist: \n");
	for(int i = 0; i < num_files; ++i) {
		fprintf(stderr, "%s\n", webui->static_files_namelist[i]);
	}


	GError *gerr = NULL;
	soup_server_add_handler(server, "/default", on_proxy_channels, webui, NULL);
	soup_server_add_handler(server, "/", on_document_root, webui, NULL);


	gboolean ok = soup_server_listen_all(server, 8080, SOUP_SERVER_LISTEN_IPV4_ONLY, &gerr);
	if(!ok || gerr) {
		if(gerr) {
			fprintf(stderr, "Error: %s\n", gerr->message);
			g_error_free(gerr);
			gerr = NULL;
		}
	}
	assert(ok);
	
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	
	return 0;
}

