/*
 * http-client.c
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

#include "utils.h"
#include "http-server.h"
#include <pthread.h>
#include <sys/epoll.h>

static char *parse_http_method(struct http_client_context *client, char *p_begin)
{
	char *line= p_begin;
	char *next_line = strchr(p_begin, '\n');
	if(NULL == next_line) return line;	// partial requests
	
	if(next_line > line && next_line[-1] == '\r') next_line[-1] = '\0';
	*next_line++ = '\0';
	
	debug_printf("%s(): line=%s\n", __FUNCTION__, line);
	char *token = NULL;
	char *method = strtok_r(line, " ", &token);
	if(NULL == method) return NULL;	// invalid foramt
	
	char *path = strtok_r(NULL, " ", &token);
	if(NULL == path) return NULL;
	
	char *protocol = strtok_r(NULL, " ", &token);
	if(NULL == protocol) return NULL;
	
	client->method = NULL;
	if(strcasecmp(method, SOUP_METHOD_GET) == 0) client->method = SOUP_METHOD_GET;
	else if(strcasecmp(method, SOUP_METHOD_PUT) == 0) client->method = SOUP_METHOD_PUT;
	else if(strcasecmp(method, SOUP_METHOD_POST) == 0) client->method = SOUP_METHOD_POST;
	else if(strcasecmp(method, SOUP_METHOD_HEAD) == 0) client->method = SOUP_METHOD_HEAD;
	else if(strcasecmp(method, SOUP_METHOD_OPTIONS) == 0) client->method = SOUP_METHOD_OPTIONS;
	else if(strcasecmp(method, SOUP_METHOD_DELETE) == 0) client->method = SOUP_METHOD_DELETE;
	
	if(NULL == client->method) return NULL; // unknown method
	
	if(path[0] != '/') return NULL; // invalid path
	if(strcasecmp(protocol, "HTTP/1.0") != 0 
		&& strcasecmp(protocol, "HTTP/1.1") != 0) return NULL; // unsupport protocol
	
	client->path = strdup(path);
	client->protocol = strdup(protocol);
	client->stage = http_stage_method_parsed;
	return  next_line;
}

static void dump_soup_headers(SoupMessageHeaders *headers)
{
	SoupMessageHeadersIter iter;
	soup_message_headers_iter_init(&iter, headers);
	while(1) {
		const char *key = NULL, *value = NULL;
		if(!soup_message_headers_iter_next(&iter, &key, &value)) break;
		printf("%s: %s\n", key, value);
	}
}

static char *parse_http_request_headers(struct http_client_context *client, char *p_begin, char *p_end)
{
	enum http_stage state = client->stage;
	assert(state < http_stage_parse_request_body);
	
	SoupMessageHeaders *headers = client->request_headers;
	if(NULL == headers) {
		headers = soup_message_headers_new(SOUP_MESSAGE_HEADERS_REQUEST);
		client->request_headers = headers;
	}
	
	char *line = p_begin;
	while(line < p_end) {
		char *next_line = strchr(line, '\n');
		if(NULL == next_line) return line;
		if(next_line[-1] == '\r') next_line[-1] = '\0';
		*next_line++ = '\0';
		
		if(line[0] == '\0') { // empty line
			client->stage = http_stage_parse_request_body;
			dump_soup_headers(headers);
			return next_line;
		}
		
		
		printf("%s(): line=%s\n", __FUNCTION__, line);
		char *token = NULL;
		char *key = strtok_r(line, ": ", &token);
		char *value = strtok_r(NULL, "\r\n", &token);
		
		printf("key: %s, value: %s\n", key, value);
		if(NULL == key || NULL == value) return NULL;
		soup_message_headers_append(headers, key, value);
		line = next_line;
	}
	return line;
}


static int http_client_parse_request(struct http_client_context *client, struct http_buffer *in_buf)
{
	debug_printf("%s(%p) ...", __FUNCTION__, client);
	
	int rc = 1;
	size_t bytes_left = 0;
	if(client->stage > http_stage_parse_request_finished) return -1;
	
	char *line = http_buffer_get_data(in_buf);
	char *next_line = NULL;
	char *p_end = line + in_buf->length;
	if(client->stage == http_stage_none) {
		next_line = parse_http_method(client, line);
		if(NULL == next_line) return -1;
		if(next_line == line) goto label_final;
		
		assert(client->stage == http_stage_method_parsed);
		line = next_line;
	}
	
	while(client->stage <= http_stage_parse_request_headers) {
		if(line == p_end) goto label_final; // partial headers
		next_line = parse_http_request_headers(client, line, p_end);
		if(NULL == next_line) return -1;
		
		if(line == next_line) goto label_final;
		line = next_line;
	}
	
	if(client->stage == http_stage_parse_request_body) {
		client->stage = http_stage_parse_request_finished;
	}
	
	if(client->stage == http_stage_parse_request_finished) {
		if(client->on_http_request) {
			return client->on_http_request(client, client->user_data);
		}else {
			// write sample response
			
		#define HTTP_RESPONSE_DEFAULT "HTTP/1.1 200 OK\r\n" \
			"Connection: Close\r\n" \
			"\r\n" \
			"Hello World\n"
			
			struct http_buffer *out_buf = client->out_buf;
			http_buffer_push_data(out_buf, HTTP_RESPONSE_DEFAULT, sizeof(HTTP_RESPONSE_DEFAULT) - 1);
			
			http_server_set_client_writable(client->server, client, (out_buf->length > 0));
			client->stage = http_stage_final;
		}
	}
label_final:
	
	if(line < p_end) {
		bytes_left = p_end - line;
		memmove(in_buf->data, line, bytes_left);
	}
	in_buf->length = bytes_left;
	in_buf->data[bytes_left] = '\0';
	return rc;
}

static int http_client_on_read(struct http_client_context *client, void *event)
{
	debug_printf("%s(%p) ...", __FUNCTION__, client);
	
	if(NULL == client || client->fd == -1) return -1;
	int err = 0;
	int fd = client->fd;
	
	struct http_buffer *in_buf = client->in_buf;
	while(!err) {
		ssize_t cb = 0;
		char data[4096] = "";
		cb = read(fd, data, sizeof(data) - 1);
		if(cb <= 0) {
			if(cb == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
				perror("read");
				err = 1;
			}
			break;
		}
		data[cb] = '\0';
		
		http_buffer_push_data(in_buf, data, cb);
		
		debug_printf("data(cb=%ld): %.*s\n", (long)cb,
			(int)((cb > 100)?100:cb), data);
	}
	
	if(err) return -1;
	return http_client_parse_request(client, in_buf);
}

static int http_client_on_write(struct http_client_context *client, void *event)
{
	debug_printf("%s(%p) ...", __FUNCTION__, client);
	
	if(NULL == client || client->fd == -1) return -1;
	int fd = client->fd;
	int err = 0;
	
	struct http_buffer *out_buf = client->out_buf;
	debug_printf("%s(): out_length = %ld\n", __FUNCTION__, (long)out_buf->length);
	
	pthread_mutex_lock(&client->mutex);
	char *data = http_buffer_get_data(out_buf);
	while(!err && out_buf->length > 0) {
		ssize_t cb = 0;
		cb = write(fd, data, out_buf->length);
		printf("cb = %ld\n", (long)cb);
		if(cb <= 0) {
			if(cb == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
				perror("read");
				err = 1;
			}
			break;
		}
		out_buf->start_pos += cb;
		out_buf->length -= cb;
		data += cb;
		if(out_buf->length == 0) {
			out_buf->start_pos = 0;
			break;
		}
	}
	pthread_mutex_unlock(&client->mutex);
	
	if(err) {
		pthread_mutex_unlock(&client->mutex);
		return -1;
	}
	
	if(out_buf->length == 0 ) {
		http_server_set_client_writable(client->server, client, 0);
		
		if(!client->is_multipart || client->stage == http_stage_final) {
			pthread_mutex_unlock(&client->mutex);
			return -1; // close connection
		}
	}
	pthread_mutex_unlock(&client->mutex);
	return 0;
}

struct http_client_context *http_client_context_new(int fd, struct http_server_context *server, void *user_data)
{
	struct http_client_context *client = calloc(1, sizeof(*client));
	assert(client);
	int rc = pthread_mutex_init(&client->mutex, NULL);
	assert(0 == rc);
	
	client->user_data = user_data;
	client->fd = fd;
	client->server = server;
	
	client->on_read = http_client_on_read;
	client->on_write = http_client_on_write;
	
	return client;
}
void http_client_context_free(struct http_client_context *client)
{
	if(NULL == client) return;
	debug_printf("%s(%p) ...", __FUNCTION__, client);
	
	if(client->on_destroy) client->on_destroy(client, client->user_data);
	
	int fd = client->fd;
	client->fd = -1;
	close_fd(fd);
	
	if(client->request_headers) {
		soup_message_headers_free(client->request_headers);
		client->request_headers = NULL;
	}
	if(client->response_headers) {
		soup_message_headers_free(client->response_headers);
		client->response_headers = NULL;
	}
	
	http_buffer_clear(client->in_buf);
	http_buffer_clear(client->out_buf);
	
	http_buffer_clear(client->request_body);
	http_buffer_clear(client->response_body);
	
	pthread_mutex_destroy(&client->mutex);
	free(client);
	return;
}
