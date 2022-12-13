/*
 * ai-client.c
 * 
* Copyright 2022 Che Hongwei <htc.chehw@gmail.com>
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
#include <assert.h>
#include <json-c/json.h>
#include <libsoup/soup.h>
#include <pthread.h>

#include "utils.h"
#include "ai-engine.h"
#include "img_proc.h"
static const char *s_plugin_type = "ai-engine::httpclient";
static const char *s_default_server_url = "http://localhost:9090/ai";
static const char *s_default_user_agent =  "ai-client/v0.1.0-aplha";

struct ai_engine_soup_client
{
	ai_engine_t *engine;
	const char *plugin_type;
	pthread_mutex_t mutex;
	
	SoupSession *session;
	json_object *jconfig;
	const char *server_url;
	const char *user_agent;
};

struct ai_engine_soup_client *ai_engine_soup_client_new(ai_engine_t *engine)
{
	struct ai_engine_soup_client *client = calloc(1, sizeof(*client));
	assert(client);
	client->engine = engine;
	client->plugin_type = s_plugin_type;
	
	int rc = pthread_mutex_init(&client->mutex, NULL);
	assert(0 == rc);
	
	UNUSED(rc);
	return client;
}

static json_object *generate_default_config(const char *url)
{
	if(NULL == url) url = s_default_server_url;
	json_object *jconfig = json_object_new_object();
	assert(jconfig);
	json_object_object_add(jconfig, "type", json_object_new_string(s_plugin_type));
	json_object_object_add(jconfig, "user-agent", json_object_new_string(s_plugin_type));
	json_object_object_add(jconfig, "url", json_object_new_string(url));
	return jconfig;
}

static int soup_client_init(struct ai_engine * engine, json_object * jconfig)
{
	assert(engine && engine->priv);
	struct ai_engine_soup_client *client = engine->priv;
	if(NULL == jconfig) jconfig = generate_default_config(NULL);
	
	client->jconfig = jconfig;
	client->server_url = json_get_value_default(jconfig, string, url, s_default_server_url);
	client->user_agent = json_get_value_default(jconfig, string, user_agent, s_default_user_agent);
	
	client->session = soup_session_new_with_options(SOUP_SESSION_USER_AGENT, client->user_agent, NULL);
	return 0;
}
static void soup_client_cleanup(struct ai_engine * engine)
{
	if(NULL == engine || NULL == engine->priv) return;
	struct ai_engine_soup_client *client = engine->priv;	
	
	if(client->session) {
		g_object_unref(client->session);
		client->session = NULL;
	}
	pthread_mutex_destroy(&client->mutex);
	free(client);
}
static int soup_client_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults)
{
	assert(engine && engine->priv);
	if(NULL == frame || NULL == frame->data || frame->width <= 0 || frame->height <= 0) return -1;
	
	unsigned char *jpeg_data = NULL;
	ssize_t cb_jpeg = 0;
	switch(frame->type) {
	case input_frame_type_jpeg:
		jpeg_data = frame->data;
		cb_jpeg = frame->length;
		break;
	case input_frame_type_bgra:
		cb_jpeg = bgra_image_to_jpeg_stream((struct bgra_image *)frame->bgra, &jpeg_data, 90);
		break;
	default:
		return -2;
	}

	struct ai_engine_soup_client *client = engine->priv;
	assert(client->session);
	
	pthread_mutex_lock(&client->mutex);
	SoupMessage *msg = soup_message_new("POST", client->server_url);
	
	soup_message_headers_set_content_type(msg->request_headers, "image/jpeg", NULL);
	soup_message_body_append(msg->request_body, 
		(jpeg_data == frame->data)?SOUP_MEMORY_COPY:SOUP_MEMORY_TAKE,
		jpeg_data, cb_jpeg);
	
	guint response_code = soup_session_send_message(client->session, msg);
	enum json_tokener_error jerr = -1;
	json_object *jresult = NULL;
		
		
	if(response_code >= 200 && response_code < 300) {
		if(p_jresults) {
			SoupMessageBody *response = msg->response_body;
			if(response->data && response->length > 0) {
				json_tokener *jtok = json_tokener_new();
				jresult = json_tokener_parse_ex(jtok, response->data, response->length);
				jerr = json_tokener_get_error(jtok);
				json_tokener_free(jtok);
			}
			if(jerr == json_tokener_success) *p_jresults = jresult;
		}
	}
	g_object_unref(msg);
	pthread_mutex_unlock(&client->mutex);
	return jerr;
}


ai_engine_t * ai_engine_init(ai_engine_t * engine, const char * plugin_type, void * user_data)
{
	if(strcasecmp(plugin_type, s_plugin_type) != 0) return NULL;
	
	if(NULL == engine) engine = calloc(1, sizeof(*engine));
	assert(engine);
	engine->user_data = user_data;
	
	struct ai_engine_soup_client *client = ai_engine_soup_client_new(engine);
	engine->priv = client;
	engine->init = soup_client_init;
	engine->cleanup = soup_client_cleanup;
	engine->predict = soup_client_predict;
	
	return engine;
}

void ai_engine_cleanup(ai_engine_t * engine)
{
	if(NULL == engine) return;
	if(engine->cleanup) engine->cleanup(engine);
	return;
}

