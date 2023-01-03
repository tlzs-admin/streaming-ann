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
#include <curl/curl.h>
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
	
#if defined(_WIN32) || defined(WIN32)
	if(content_type) {
		char *mime_type = NULL;
		if(strcasecmp(content_type, ".html") == 0) mime_type = strdup("text/html");
		else if(strcasecmp(content_type, ".js") == 0) mime_type = strdup("text/javascript");
		else if(strcasecmp(content_type, ".json") == 0) mime_type = strdup("application/json");
		else mime_type = g_content_type_get_mime_type(content_type);
		
		g_free(content_type);
		if(NULL == mime_type) mime_type = strdup("application/octet-stream");
		content_type = mime_type;
	}
#endif
	
	printf("content-type: %s\n", content_type);
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


#define MAX_SUBFOLDERS	(256)
#define FILELIST_SIZE	(1024)
#if !defined(WIN32) && !defined(_WIN32)
static ssize_t reload_static_filenames(struct webui_context *webui)
{
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
	return num_files;
}
#else
#include <windows.h>
static ssize_t reload_static_filenames(struct webui_context *webui)
{
	if(NULL == webui->document_root) webui->document_root = ".";
	
	char pattern[4096] = "";
	snprintf(pattern, sizeof(pattern) - 1, "%s/*", webui->document_root);
	
	ssize_t num_subfolders = 0;
	char *subfolders[MAX_SUBFOLDERS] = { NULL };
	
	size_t num_files = 0;
	char **filenames_list = calloc(FILELIST_SIZE, sizeof(*filenames_list));
	
	WIN32_FIND_DATA wfd[1];
	memset(wfd, 0, sizeof(wfd));
	
	DWORD dwError = 0;
	HANDLE hff = FindFirstFile(pattern, wfd);
	if(hff == INVALID_HANDLE_VALUE) {
		dwError = GetLastError();
		assert(dwError == ERROR_FILE_NOT_FOUND);
		return -1;
	}
	
	do {
		if(wfd->cFileName[0] == '.') continue;
		if(wfd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			assert(num_subfolders < MAX_SUBFOLDERS);
			subfolders[num_subfolders++] = strdup(wfd->cFileName);
		}else {
			assert(num_files < FILELIST_SIZE);
			filenames_list_add(filenames_list, num_files++, NULL, wfd->cFileName);
			fprintf(stderr, "file_attr: 0x%.8x, 	filename: %s\n", (unsigned int)wfd->dwFileAttributes, wfd->cFileName);
		}
		memset(wfd, 0, sizeof(wfd));
	}while(FindNextFile(hff, wfd));
	
	dwError = GetLastError();
	if(dwError != ERROR_NO_MORE_FILES) {
		fprintf(stderr, "error: %ld\n", dwError);
		return -1;
	}
	FindClose(hff);
	
	for(size_t i = 0; i < num_subfolders; ++i) {
		snprintf(pattern, sizeof(pattern) - 1, "%s/%s/*", webui->document_root, subfolders[i]);
		memset(wfd, 0, sizeof(wfd));
		hff = FindFirstFile(pattern, wfd);
		if(hff == INVALID_HANDLE_VALUE) continue;
		do {
			if(wfd->cFileName[0] == '.') continue;
			if(wfd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			
			assert(num_files < FILELIST_SIZE);
			filenames_list_add(filenames_list, num_files++, subfolders[i], wfd->cFileName);
			fprintf(stderr, "file_attr: 0x%.8x, 	filename: %s\n", (unsigned int)wfd->dwFileAttributes, wfd->cFileName);
			
			memset(wfd, 0, sizeof(wfd));
		}while(FindNextFile(hff, wfd));
		FindClose(hff);
	}
	webui->num_static_files = num_files;
	webui->static_files_namelist = realloc(filenames_list, num_files * sizeof(*filenames_list));
	
	webui->contents = calloc(num_files, sizeof(*webui->contents));
	return num_files;
}
#endif
#undef MAX_SUBFOLDERS
#undef FILELIST_SIZE


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
				content = webui->contents[i] = file_content_new_from_file(NULL, webui->document_root, path_name);
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

struct response_headers_closure
{
	size_t max_lines;
	size_t count;
	char ** lines;
};

struct response_closure
{
	size_t size;
	size_t length;
	char *data;
};

#define RESPONSE_ALLOC_SIZE (65536)
static int response_closure_resize(struct response_closure *response, size_t new_size)
{
	if(new_size == 0) new_size = RESPONSE_ALLOC_SIZE;
	else new_size = (new_size + RESPONSE_ALLOC_SIZE - 1) / RESPONSE_ALLOC_SIZE * RESPONSE_ALLOC_SIZE;
	
	if(new_size <= response->size) return 0;
	char *data = realloc(response->data, new_size);
	assert(data);
	memset(data + response->size, 0, (new_size - response->size) * sizeof(*response->data));
	
	response->size = new_size;
	response->data = data;
	return 0;
}
#undef RESPONSE_ALLOC_SIZE

static int response_closure_push_data(struct response_closure *response, const char *data, size_t length)
{
	if(length == 0) return -1;
	int rc = response_closure_resize(response, response->length + length);
	assert(0 == rc);
	
	memcpy(response->data + response->length, data, length);
	response->length += length;
	return 0;
}
static void response_closure_cleanup(struct response_closure *response)
{
	if(NULL == response) return;
	if(response->data) {
		free(response->data);
		response->data = NULL;
	}
	memset(response, 0, sizeof(*response));
	return;
}

static size_t on_proxy_response(char *data, size_t size, size_t n, void *user_data)
{
	struct response_closure *response = user_data;
	assert(response);
	
	size_t length = size * n;
	if(length == 0) return 0;
	
	int rc = response_closure_push_data(response, data, length);
	if(rc) return 0;

	return length;
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
	
	struct response_closure response[1] = {{
		.size = 0,
	}};
	struct curl_slist *headers = NULL;
	long response_code = SOUP_STATUS_BAD_GATEWAY;
	
	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_proxy_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
	
	SoupMessageHeaders *request_headers = msg->request_headers;
	const char *key = NULL, *value = NULL;
	SoupMessageHeadersIter iter;
	soup_message_headers_iter_init(&iter, request_headers);
	while(soup_message_headers_iter_next(&iter, &key, &value))
	{
		if(key && value) {
			char header_line[4096] = "";
			snprintf(header_line, sizeof(header_line) - 1, "%s: %s", key, value);
			headers = curl_slist_append(headers, header_line);
		}
		key = NULL; value = NULL;
	}
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	headers = NULL;
	
	if(ret != CURLE_OK) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_GATEWAY);
		fprintf(stderr, "curl perform failed: %s\n", curl_easy_strerror(ret));
		
		goto label_cleanup;
		
	}
	ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if(ret != CURLE_OK) {
		soup_message_set_status(msg, SOUP_STATUS_GATEWAY_TIMEOUT);
		fprintf(stderr, "curl perform failed: %s\n", curl_easy_strerror(ret));
		goto label_cleanup;
	}
	
	SoupMessageHeaders *response_headers = msg->response_headers;
	soup_message_headers_append(response_headers, "Connection", "close");
	soup_message_headers_append(response_headers, "Cache-control", "no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
	soup_message_headers_append(response_headers, "Pragma", "no-cache");
	soup_message_headers_append(response_headers, "Expires", "Mon, 3 Jan 2000 00:00:00 GMT");
	soup_message_headers_append(response_headers, "Access-Control-Allow-Origin", "*");
	if(response->data) {
		soup_message_set_response(msg, "image/jpeg", SOUP_MEMORY_TAKE, response->data, response->length);
		response->data = NULL;
	}else {
		response_code = SOUP_STATUS_NO_CONTENT;
	}

label_cleanup:
	soup_message_set_status(msg, response_code);
	
	if(curl) curl_easy_cleanup(curl);
	response_closure_cleanup(response);
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
	
	GSList *uris = soup_server_get_uris(server);
	assert(uris);
	for(GSList *uri = uris; NULL != uri; uri = uri->next) {
		char *sz_uri = soup_uri_to_string(uri->data, FALSE);
		assert(sz_uri);
		fprintf(stderr, "Listening on: %s\n", sz_uri);
		free(sz_uri);
		soup_uri_free(uri->data);
	}
	g_slist_free(uris);
	
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	
	return 0;
}

