/*
 * http-server.c
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

#include <sys/types.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "http-server.h"
#include "utils.h"

static inline int64_t get_time_ms(clockid_t clock_id)
{
	int64_t timestamp_ms = 0;
	struct timespec ts = { 0 };
	int rc = clock_gettime(clock_id, &ts);
	if(rc == -1) {
		perror("clock_gettime()");
		return -1;
	}
	timestamp_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	return timestamp_ms;
}

void http_buffer_clear(struct http_buffer *buffer)
{
	if(NULL == buffer) return;
	if(buffer->data) free(buffer->data);
	memset(buffer, 0, sizeof(*buffer));
}

char *http_buffer_get_data(const struct http_buffer *buffer) 
{
	if(buffer && buffer->data) return buffer->data + buffer->start_pos;
	return NULL;
}

int http_buffer_resize(struct http_buffer *buffer, size_t new_size)
{
	static const size_t ALLOC_SIZE = (1 << 20);
	if(new_size == -1 || new_size == 0) new_size = ALLOC_SIZE;
	else  new_size = (new_size + ALLOC_SIZE - 1) / ALLOC_SIZE * ALLOC_SIZE;
	
	if(new_size < buffer->max_size) return 0;
	void *data = realloc(buffer->data, new_size);
	assert(data);
	memset((unsigned char *)data + buffer->max_size, 0, new_size - buffer->max_size);
	buffer->data = data;
	buffer->max_size = new_size;
	return 0;
}

int http_buffer_push_data(struct http_buffer *buffer, const void *data, size_t length)
{
	assert(buffer);
	if(length == -1) length = strlen((char*)data);
	if(0 == length) return 0;
	
	int rc = http_buffer_resize(buffer, buffer->start_pos + buffer->length + length + 1);
	assert(0 == rc);
	assert(buffer->data);
	
	memcpy((unsigned char *)buffer->data + buffer->length, data, length);
	buffer->length += length;
	return 0;
}

/******************************************************************************
 * http_server_context
******************************************************************************/
static int http_accept(struct http_server_context *http, struct epoll_event *ev, int64_t current_time_ms)
{
	assert(http && http->efd != -1);
	int server_fd = ev->data.fd;
	assert(server_fd != -1);
	
	struct sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
	int fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
	if(fd == -1) return -1;
	make_nonblock(fd);
	
	struct http_client_context *client = http_client_context_new(fd, http, NULL);
	assert(client);
	client->timestamp_ms = current_time_ms;
	memcpy(&client->addr, &addr, addr_len);
	client->addr_len = addr_len;
	
	int rc = 0;
	if(http->on_accepted) rc = http->on_accepted(http, client, ev);
	
	if(rc == -1) {
		http_client_context_free(client);
		return -1;
	}
	
	rc = http->add_client(http, client);
	return rc;
}

static int http_on_accepted(struct http_server_context *http, struct http_client_context *client, void *event)
{
	return 0;
}

static int http_on_error(struct http_server_context *http, void *event)
{
	return 0;
}

static int http_listen(struct http_server_context *http, const char *host, const char *port, int use_ssl)
{
	int rc = 0;
	http->use_ssl = use_ssl; ///<@ todo
	
	int efd = http->efd;
	assert(efd != -1);
	
	struct addrinfo hints, *serv_info = NULL, *pai = NULL;
	memset(&hints, 0, sizeof(hints));
	
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	rc = getaddrinfo(host, port, &hints, &serv_info);
	if(rc) {
		fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(rc));
		if(serv_info) freeaddrinfo(serv_info);
		return rc;
	}
	
	for(pai = serv_info; NULL != pai; pai = pai->ai_next)
	{
		int fd = socket(pai->ai_family, pai->ai_socktype, pai->ai_protocol);
		if(fd == -1) continue;
		
		rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT | SO_DEBUG, &(int){1}, sizeof(int));
		assert(0 == rc);
		
		rc = bind(fd, pai->ai_addr, pai->ai_addrlen);
		if(rc) {
			perror("bind");
			close_fd(fd);
			continue;
		}
		
		rc = listen(fd, SOMAXCONN);
		if(rc) {
			perror("listen");
			close_fd(fd);
			continue;
		}
		
		assert(http->num_fds < HTTP_SERVER_MAX_LISTENING_FDS);
		http->server_fds[http->num_fds++] = fd;
		
		char hbuf[NI_MAXHOST] = ""; 
		char sbuf[NI_MAXSERV] = "";
		rc = getnameinfo(pai->ai_addr, pai->ai_addrlen, 
			hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
			NI_NUMERICHOST | NI_NUMERICSERV);
		fprintf(stderr, "listening on %s:%s\n", hbuf, sbuf);
	}
	
	freeaddrinfo(serv_info);
	if(http->num_fds == 0) return -1;
	
	struct epoll_event ev[1];
	memset(ev, 0, sizeof(ev));
	ev->events = EPOLLIN;
	
	for(size_t i = 0; i < http->num_fds; ++i) {
		ev->data.fd = http->server_fds[i];
		rc = epoll_ctl(efd, EPOLL_CTL_ADD, http->server_fds[i], ev);
		assert(0 == rc);
	}
	return 0;
}

#define MAX_HTTP_EVENTS (64)
#include <stdbool.h>
static inline _Bool is_server_event(struct http_server_context *http, struct epoll_event *ev)
{
	for(size_t i = 0; i < http->num_fds; ++i) {
		if(ev->data.fd == http->server_fds[i]) return TRUE;
	}
	return FALSE;
}

static void http_server_garbage_collection(struct http_server_context *http, int64_t current_time_ms)
{
	if(0 == http->num_clients) return;
	if(current_time_ms <= 0) current_time_ms = get_time_ms(CLOCK_MONOTONIC);
	
	size_t count = 0;
	ssize_t *indices_to_remove = calloc(http->num_clients, sizeof(*indices_to_remove));
	
	for(size_t i = 0; i < http->num_clients; ++i) {
		struct http_client_context *client = http->clients[i];
		if(NULL == client || client->fd == -1) continue;
		
		int timeout = client->timeout;
		if(0 == timeout) timeout = 300 * 1000; // 300 seconds
		if((client->timestamp_ms + timeout) < current_time_ms) {
			indices_to_remove[count++] = i;
		}
	}
	for(size_t i = 0; i < count; ++i) {
		struct http_client_context *client = http->clients[i];
		assert(client && client->fd != -1);
		debug_printf("client %d timeout\n", (int)i);
		http->remove_client(http, client);
	}
	free(indices_to_remove);
	return;
}

static void * http_server_process(struct http_server_context *http)
{
	int rc = 0;
	assert(http);
	
	const int timeout = 100; // ms
	int efd = http->efd;
	assert(efd != -1);
	
	struct epoll_event events[MAX_HTTP_EVENTS + 1];
	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGUSR1);
	sigaddset(&sigs, SIGPIPE);

	while(!http->quit) {
		int n = epoll_pwait(efd, events, MAX_HTTP_EVENTS, timeout, &sigs);
		int64_t current_time_ms = get_time_ms(CLOCK_MONOTONIC);
		
		if(n == 0) { // events timeout
			http_server_garbage_collection(http, current_time_ms);
			continue; 
		}

		for(int i = 0; i < n; ++i) {
			rc = 0;
			struct epoll_event *ev = &events[i];
			if(is_server_event(http, ev)) {
				if(ev->events & EPOLLIN) {
					rc = http_accept(http, ev, current_time_ms);
				}else {
					http->on_error(http, ev);
					rc = -1;
				}
				
				if(rc == -1) {
					http->quit = 1;
					break;
				}
				continue;
			}
			
			struct http_client_context *client = ev->data.ptr;
			int fd = client->fd;
			assert(fd != -1);
			if(!((ev->events & EPOLLIN) || (ev->events & EPOLLOUT)) ) {
				http->remove_client(http, client);
				continue;
			}
			
			client->timestamp_ms = current_time_ms;
			if(ev->events & EPOLLIN) {
				rc = -1;
				
				if(client->on_read) rc = client->on_read(client, ev);
				
				if(rc == -1) { // error
					debug_printf("client->on_read() = %d\n", rc);
					http->remove_client(http, client);
					continue;
				}
			}
			if(ev->events & EPOLLOUT) {
				rc = -1;
				if(client->on_write) rc = client->on_write(client, ev);
				if(rc == -1) { // error
					http->remove_client(http, client);
					continue;
				}
			}
		}
	}
	
	if(http->async_mode) pthread_exit((void *)(intptr_t)rc);
	return (void *)(intptr_t)rc;
}
static int http_run(struct http_server_context *http, int async_mode)
{
	int rc = 0;
	http->async_mode = async_mode;
	
	if(async_mode) {
		rc = pthread_create(&http->th, NULL, (void *(*)(void *))http_server_process, http);
	}else {
		void *exit_code = http_server_process(http);
		rc = (int)(intptr_t)exit_code;
	}
	return rc;
}
static int http_stop(struct http_server_context *http)
{
	int rc = 0;
	http->quit = 1;
	if(http->async_mode) {
		http->async_mode = 0;
		void *exit_code = NULL;
		rc = pthread_join(http->th, &exit_code);
		fprintf(stderr, "http server thread exited with code %p, rc = %d\n", exit_code, rc);
	}
	return rc;
}

static int http_add_client(struct http_server_context *http, struct http_client_context *client)
{
	int efd = http->efd;
	int rc = 0;
	assert(http->num_clients < http->max_clients);
	http->clients[http->num_clients++] = client;
	
	struct epoll_event ev[1];
	memset(ev, 0, sizeof(ev));
	ev->events = EPOLLIN | EPOLLET;
	ev->data.ptr = client;
	rc = epoll_ctl(efd, EPOLL_CTL_ADD, client->fd, ev);
	if(rc == -1) return rc;
	
	return 0;
}
static int http_server_remove_client(struct http_server_context *http, struct http_client_context *client)
{
	for(size_t i = 0; i < http->num_clients; ++i) {
		if(client == http->clients[i]) {
			--http->num_clients;
			
			if(client->fd >= 0) {
				epoll_ctl(http->efd, EPOLL_CTL_DEL, client->fd, NULL);
			}
			http->clients[i] = NULL;
			http_client_context_free(client);
			
			if(i < http->num_clients) {
				http->clients[i] = http->clients[http->num_clients];
				http->clients[http->num_clients] = NULL;
			}
			break;
		}
	}
	return 0;
}
	
struct http_server_context *http_server_context_init(struct http_server_context *http, void *user_data)
{
	if(NULL == http) http = calloc(1, sizeof(*http));
	else memset(http, 0, sizeof(*http));
	
	http->user_data = user_data;
	for(size_t i = 0; i < HTTP_SERVER_MAX_LISTENING_FDS; ++i)
	{
		http->server_fds[i] = -1;
	}
	
	int efd = epoll_create1(EPOLL_CLOEXEC);
	assert(efd >= 0);
	http->efd = efd;
	
	http->listen = http_listen;
	http->run = http_run;
	http->stop = http_stop;
	http->add_client = http_add_client;
	http->remove_client = http_server_remove_client;
	
	http->on_accepted = http_on_accepted;
	http->on_error = http_on_error;
	
	http->max_clients = 65536;
	struct http_client_context **clients = calloc(http->max_clients, sizeof(*clients));
	assert(clients);
	http->clients = clients;
	
	return http;
}
void http_server_context_cleanup(struct http_server_context *http)
{
	return;
}

int http_server_set_client_writable(struct http_server_context *http, struct http_client_context *client, int writable)
{
	int efd = http->efd;
	struct epoll_event ev[1];
	memset(ev, 0, sizeof(ev));
	ev->events = EPOLLIN | EPOLLET;
	ev->data.ptr = client;
	
	if(writable) ev->events |= EPOLLOUT;
	return epoll_ctl(efd, EPOLL_CTL_MOD, client->fd, ev);
}

