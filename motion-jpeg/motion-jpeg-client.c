/*
 * motion-jpeg-client.c
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

#include <errno.h>
#include <pthread.h>
#include "motion-jpeg.h"

static int on_client_destroy(struct http_client_context *client, void *user_data)
{
	if(NULL == user_data) return -1;
	struct motion_jpeg_client *mjpeg_client = user_data;
	
	pthread_mutex_lock(&mjpeg_client->cond_mutex.mutex);
	client->user_data = NULL;
	mjpeg_client->quit = 1;
	pthread_mutex_unlock(&mjpeg_client->cond_mutex.mutex);
	
	void *exit_code = NULL;
	int rc = pthread_join(mjpeg_client->th, &exit_code);
	fprintf(stderr, "mjpeg_client thread %p exited with code %p, rc = %d\n", 
		(void *)mjpeg_client->th, exit_code, rc);
		
	
	motion_jpeg_client_free(mjpeg_client);
	return 0;
}

static void * motion_jpeg_client_thread(void *user_data)
{
	int err = 0;
	struct motion_jpeg_client *mjpeg_client = user_data;
	assert(mjpeg_client && mjpeg_client->client);
	struct http_client_context *client = mjpeg_client->client;
	struct motion_jpeg_server *server = (struct motion_jpeg_server *)client->server;
	struct http_buffer *out_buf = client->out_buf;
	assert(server && out_buf);
	
	pthread_mutex_t *mutex = &mjpeg_client->cond_mutex.mutex;
	//~ pthread_mutex_lock(mutex);
	//~ pthread_cond_t *cond = &mjpeg_client->cond_mutex->cond;
	
	while(!mjpeg_client->quit)
	{
		usleep(100 * 1000);
		pthread_mutex_lock(mutex);
		if(mjpeg_client->quit) {
			pthread_mutex_unlock(mutex);
			break;
		}
		//~ int rc = pthread_cond_wait(cond, mutex);
		//~ if(rc) {
			//~ err = errno;
			//~ perror("pthread_cond_wait");
			//~ break;
		//~ }
		
		struct motion_jpeg_channel *channel = mjpeg_client->channel;
		if(NULL == channel) {
			channel = server->find_channel(server, mjpeg_client->channel_name);
			mjpeg_client->channel = channel;
		}
		if(NULL == channel) {
			pthread_mutex_unlock(mutex);
			continue;
		}
		
		struct video_frame * frame = channel->get_frame(channel);
		if(NULL == frame) {
			pthread_mutex_unlock(mutex);
			continue;
		}
		if(frame->frame_number <= mjpeg_client->frame_number) {
			channel->unref_frame(channel, frame);
			pthread_mutex_unlock(mutex);
			continue;
		}
		mjpeg_client->frame_number = frame->frame_number;
		
		char hdrs[1024] = "";
		ssize_t cb_hdrs = snprintf(hdrs, sizeof(hdrs), 
			"Content-Type: image/jpeg\r\n"
			"Content-Transfer-Encoding: 8bit\r\n"
			"Content-Length: %ld\r\n"
			"X-Framenumber: %ld\r\n"
			"X-Timestamp: %ld.%.3ld\r\n"
			"\r\n", 
			(long)frame->length, 
			(long)frame->frame_number,
			(long)frame->ticks_ms / 1000, (long)frame->ticks_ms % 1000);
		http_buffer_push_data(out_buf, hdrs, cb_hdrs);
		http_buffer_push_data(out_buf, frame->data, frame->length);
		http_buffer_push_data(out_buf, "--" MOTION_JPEG_BOUNDARY "\r\n", sizeof("--" MOTION_JPEG_BOUNDARY "\r\n") - 1);
		channel->unref_frame(channel, frame);
		
		http_server_set_client_writable(server->http, client, 1);
		pthread_mutex_unlock(mutex);
	}
	
	client->on_destroy = NULL;
	//~ pthread_mutex_unlock(mutex);
	
	pthread_exit((void *)(intptr_t)err);
}

struct motion_jpeg_client * motion_jpeg_client_new(struct http_client_context *client)
{
	int rc = 0;
	struct motion_jpeg_client * mjpeg_client = calloc(1, sizeof(*mjpeg_client));
	assert(mjpeg_client);
	
	client->user_data = mjpeg_client;
	mjpeg_client->client = client;
	
	client->on_destroy = on_client_destroy;
	
	rc = pthread_mutex_init(&mjpeg_client->cond_mutex.mutex, NULL);
	assert(0 == rc);
	
	rc = pthread_cond_init(&mjpeg_client->cond_mutex.cond, NULL);
	assert(0 == rc);
	
	rc = pthread_create(&mjpeg_client->th, NULL, motion_jpeg_client_thread, mjpeg_client);
	assert(0 == rc);
	
	return mjpeg_client;
}
void motion_jpeg_client_free(struct motion_jpeg_client *mjpeg_client)
{
	
}
