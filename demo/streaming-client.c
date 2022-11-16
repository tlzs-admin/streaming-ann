/*
 * streaming-client.c
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

#include <time.h>
#include <pthread.h>
#include <signal.h>

#include <gst/gst.h>
#include <curl/curl.h>
#include "video_source_common.h"
#include "utils.h"

#define MAX_CLIENTS (64)
volatile int g_quit = 0;

struct streaming_client_data
{
	char *server_url;
	char *video_uri;
	struct video_source_common video[1];
	long frame_number;
	
	pthread_t th;
	pthread_mutex_t mutex;
	int quit;
};

struct global_params
{
	size_t num_clients;
	struct streaming_client_data clients[MAX_CLIENTS];
};


static int upload_image(CURL *curl, const char *server_url, struct video_frame *frame)
{
	debug_printf("%s(url=%s), frame: %ld\n", __FUNCTION__, server_url, frame->frame_number);
	curl_easy_setopt(curl, CURLOPT_URL, server_url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, frame->data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)frame->length);
	//~ curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: image/jpeg");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	
	if(ret != CURLE_OK) {
		fprintf(stderr, "== ret: %d, server_url: %s, err_msg: %s\n", 
			ret,
			server_url, 
			curl_easy_strerror(ret));
		curl_easy_reset(curl);
		return ret;
	}
	curl_easy_reset(curl);
	return 0;
}


#include "../utils/video_source_common.c"


static json_object *generate_default_config()
{
	json_object *jconfig = json_object_new_array();

	json_object *jclient = json_object_new_object();
	json_object_object_add(jclient, "server_url", json_object_new_string("http://localhost:8800/default/channel0"));
	json_object_object_add(jclient, "video_uri", json_object_new_string("/dev/video0"));
	json_object_array_add(jconfig, jclient);
	
	
	jclient = json_object_new_object();
	json_object_object_add(jclient, "server_url", json_object_new_string("http://localhost:8800/default/channel1"));
	json_object_object_add(jclient, "video_uri", json_object_new_string("https://www.youtube.com/watch?v=QDC4mxC2NKM"));
	json_object_array_add(jconfig, jclient);
	
	return jconfig;
}

static GMainLoop *g_loop = NULL;
void on_signal(int sig)
{
	switch(sig) {
	case SIGINT: case SIGUSR1: {
		if(g_loop) g_main_loop_quit(g_loop);
		g_loop = NULL;
		g_quit = 1; 
		return;
	}
	default:
		break;
	}
	abort();
}

static void *client_thread(void *user_data)
{
	int rc = 0;
	struct streaming_client_data *client = user_data;
	assert(client);
	struct video_source_common *video = client->video;
	assert(video);
	
	const struct timespec timeout = {
		.tv_sec = 0,
		.tv_nsec = 300 * 1000 * 1000,
	};
	
	CURL *curl = curl_easy_init();
	
	while(!g_quit && ! client->quit)
	{
		
		if(NULL == video->pipeline) {
			nanosleep(&timeout, NULL);
			continue;
		}
		
		struct video_frame *frame = video->get_frame(video);
		if(NULL == frame || frame->frame_number <= client->frame_number) {
			if(frame) video_frame_unref(frame);
			nanosleep(&timeout, NULL);
			continue;
		}
		
		client->frame_number = frame->frame_number;
		upload_image(curl, client->server_url, frame);
		video_frame_unref(frame);
		
		nanosleep(&timeout, NULL);
	}
	
	curl_easy_cleanup(curl);
	video_source_common_cleanup(video);
	pthread_exit((void *)(intptr_t)rc);
}


struct global_params g_params[1];

int main(int argc, char **argv)
{
	gst_init(&argc, &argv);
	curl_global_init(CURL_GLOBAL_ALL);
	
	signal(SIGINT, on_signal);
	signal(SIGUSR1, on_signal);
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	
	json_object *jconfig = json_object_from_file("streaming-client.json");
	if(NULL == jconfig) {
		jconfig = generate_default_config();
		assert(jconfig);
		json_object_to_file_ext("streaming-client.json", jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	}
	
	int num_clients = json_object_array_length(jconfig);
	assert(num_clients > 0);
	
	
	struct global_params *params = g_params;
	params->num_clients = num_clients;
	
	for(int i = 0; i < num_clients; ++i)
	{
		json_object *jclient = json_object_array_get_idx(jconfig, i);
		assert(jclient);
		
		struct streaming_client_data *client = &params->clients[i];
		
		client->server_url = json_get_value(jclient, string, server_url);
		client->video_uri = json_get_value(jclient, string, video_uri);
		assert(client->server_url && client->video_uri);
		
		int rc = 0;
		struct video_source_common *video = video_source_common_init(client->video, video_frame_type_jpeg, client);
		assert(video);
		
		rc = video->init(video, client->video_uri, 640, 360, NULL);
		assert(0 == rc);
		video->play(video);
		
		rc = pthread_create(&client->th, NULL, client_thread, client);
		assert(0 == rc);
		
	}
	
	g_loop = g_main_loop_ref(loop);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	
	
	for(int i = 0; i < num_clients; ++i)
	{
		struct streaming_client_data *client = &params->clients[i];
		void *exit_code = NULL;
		int rc = pthread_join(client->th, &exit_code);
		fprintf(stderr, "thread %d exited with code %p, rc = %d\n", i, exit_code, rc);
	}
	
	json_object_put(jconfig);
	curl_global_cleanup();
	return 0;
}

