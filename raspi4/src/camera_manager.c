/*
 * camera_manager.c
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

#include <json-c/json.h>
#include <pthread.h>
#include "video_source_common.h"
#include "camera_manager.h"


struct webservice_context
{
	void *user_data;
	void *priv;
	SoupServer *server;
	json_object *jweb;
	
	int (*run)(struct webservice_context *web, int extern_loop);
};
struct webservice_context_init(struct webservice_context *web, json_object *jweb, void *user_data);
void webservice_context_cleanup(struct webservice_context *web);


struct video_controller_private
{
	struct video_controller *controller;
	GMainLoop *loop;
	
	pthread_t th;
	pthread_mutex_t mutex;
	
	ssize_t num_videos;
	struct video_source_info **videos;
	
	const char *proxy_server_url;
	struct webservice_context *web;
};

struct video_controller_private *video_controller_private_new(struct video_controller *controller)
{
	struct video_controller_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	
	priv->controller = controller;
	
	pthread_mutex_init(&priv->mutex);
	
	
	return priv;
}

static int video_controller_reload_config(struct video_controller *controller, json_object *jconfig);
static int video_controller_run(struct video_controller *controller, int extern_loop);
static struct video_frame * video_controller_get_current_frame(struct video_controller *controller, struct video_frame *frame);
static struct video_frame * video_controller_addref_frame(struct video_controller *controller, struct video_frame *frame);
static void video_controller_unref_frame(struct video_controller *controller, struct video_frame *frame);

struct video_controller *video_controller_init(struct video_controller *controller, json_object *jconfig, void *user_data)
{
	if(NULL == controller) controller = calloc(1, sizeof(*controller));
	assert(controller);
	
	controller->user_data = user_data;
	controller->reload_config = video_controller_reload_config;
	controller->run = video_controller_run;
	controller->get_current_frame = video_controller_get_current_frame;
	controller->addref_frame = video_controller_addref_frame;
	controller->unref_frame = video_controller_unref_frame;
	
	struct video_controller_private *priv = video_controller_private_new(controller);
	assert(priv);
	controller->priv = priv;
	
	if(jconfig) {
		video_controller_reload_config(controller, jconfig);
	}
	return controller;
}

void video_controller_cleanup(struct video_controller *controller);



#if defined(TEST_CAMERA_MANAGER_) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif

