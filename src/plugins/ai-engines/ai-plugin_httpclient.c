/*
 * ai-httpclient.c
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

#include "httpclient.h"
#include "ai-engine.h"
#include "utils.h"

#include "input-frame.h"

#define AI_PLUGIN_TYPE_STRING "ai-engine::httpclient"

/* Entry-Point Functions */
#ifdef __cplusplus
extern "C" {
#endif
const char * ann_plugin_get_type(void);
int ann_plugin_init(ai_engine_t * engine, json_object * jconfig);

#define ai_plugin_httpclient_init ann_plugin_init

#ifdef __cplusplus
}
#endif


const char * ann_plugin_get_type(void)
{
	return AI_PLUGIN_TYPE_STRING;
}

static void ai_plugin_httpclient_cleanup(struct ai_engine * engine)
{
	return;
}
static int ai_plugin_httpclient_load_config(struct ai_engine * engine, json_object * jconfig)
{
	return 0;
}
static int ai_plugin_httpclient_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults)
{
	debug_printf("%s(): frame: type=%d, size=%d x %d", __FUNCTION__,
		frame->type,
		frame->width, frame->height);
		
	int rc = -1;
	
	struct ai_http_client * http = engine->priv;
	
	static int quality = 95;
	unsigned char * image_data = NULL;
	ssize_t cb_data = 0;
	const char * content_type = "image/jpeg";
	json_object * jresult = NULL;
	long response_code = -1;
	
	enum input_frame_type type = frame->type & input_frame_type_image_masks;
	switch(type)
	{
	case input_frame_type_bgra:
		cb_data = bgra_image_to_jpeg_stream((bgra_image_t *)frame->bgra, &image_data, quality);
		if(NULL == image_data) {
			fprintf(stderr, "[ERROR]: %s()::bgra_image_to_jpeg_stream() failed.\n", __FUNCTION__);
			return -1;
		}
		break;
	case input_frame_type_jpeg:
		image_data = frame->data;
		cb_data = frame->length;
		break;
	case input_frame_type_png:
		image_data = frame->data;
		cb_data = frame->length;
		content_type = "image/png";
		break;
	default:
		fprintf(stderr, "[ERROR]: invalid image type: %d\n", type);
		return -1;
	}
	
	response_code = http->post(http, content_type, image_data, cb_data, &jresult); 
	debug_printf("%s()::response_code=%ld, jresult=%p\n", __FUNCTION__, response_code, jresult);
	
	if(response_code >= 200 && response_code < 300) {
		rc = 0;
		if(p_jresults) *p_jresults = json_object_get(jresult);
	}
	
	json_object_put(jresult);
	if(image_data && image_data != frame->data) free(image_data);
	return rc;
}

static int ai_plugin_httpclient_update(struct ai_engine * engine, const ai_tensor_t * truth)
{
	return 0;
}
static int ai_plugin_httpclient_get_property(struct ai_engine * engine, const char * name, void ** p_value)
{
	return 0;
}
static int ai_plugin_httpclient_set_property(struct ai_engine * engine, const char * name, const void * value, size_t length)
{
	return 0;
}

int ann_plugin_init(ai_engine_t * engine, json_object * jconfig)
{
	const char * server_url = "http://127.0.0.1:9090";
	if(NULL == jconfig) {
		jconfig = json_object_new_object();
		json_object_object_add(jconfig, "url", json_object_new_string(server_url));
	}
	assert(jconfig);
	
	ai_http_client_t * http = ai_http_client_new(jconfig, engine);
	assert(http);
	
	engine->priv = http;
	engine->init = ai_plugin_httpclient_init;
	engine->cleanup = ai_plugin_httpclient_cleanup;
	engine->load_config = ai_plugin_httpclient_load_config;
	engine->predict = ai_plugin_httpclient_predict;
	engine->update = ai_plugin_httpclient_update;
	engine->get_property = ai_plugin_httpclient_get_property;
	engine->set_property = ai_plugin_httpclient_set_property;
	return 0;
}

#undef AI_PLUGIN_TYPE_STRING
