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
#include <curl/curl.h>
#include <pthread.h>

#include "utils.h"
#include "ai-engine.h"
#include "img_proc.h"
static const char *s_plugin_type = "ai-engine::httpclient";
static const char *s_default_server_url = "http://localhost:9090/ai";
static const char *s_default_user_agent =  "ai-client/v0.1.0-aplha";

struct ai_engine_curl_client
{
	ai_engine_t *engine;
	const char *plugin_type;
	pthread_mutex_t mutex;
	
	json_object *jconfig;
	const char *server_url;
	const char *user_agent;
};

struct response_closure
{
	json_tokener *jtok;
	enum json_tokener_error jerr;
	json_object *jresult;
};

static struct response_closure * response_closure_init(struct response_closure *response)
{
	if(NULL == response) response = calloc(1, sizeof(*response));
	assert(response);
	
	json_tokener *jtok = json_tokener_new();
	assert(jtok);	
	response->jtok = jtok;
	response->jerr = json_tokener_error_parse_eof;
	return response;
}
static void response_closure_cleanup(struct response_closure *response)
{
	if(NULL == response) return;
	if(response->jtok) json_tokener_free(response->jtok);
	if(response->jresult) json_object_put(response->jresult);
	return;
}

static size_t on_response(char *str, size_t size, size_t n, void *user_data)
{
	struct response_closure *response = user_data;
	assert(response && response->jtok);
	
	size_t cb = size * n;
	if(cb == 0) return 0;
	
	if(response->jerr == json_tokener_success) return cb;
	
	json_object *jresult = json_tokener_parse_ex(response->jtok, str, cb);
	response->jerr = json_tokener_get_error(response->jtok);
	if(response->jerr == json_tokener_continue) return cb;
	if(response->jerr == json_tokener_success) {
		response->jresult = jresult;
		return cb;
	}
	return 0;
}

struct ai_engine_curl_client *ai_engine_curl_client_new(ai_engine_t *engine)
{
	struct ai_engine_curl_client *client = calloc(1, sizeof(*client));
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

static int curl_client_init(struct ai_engine * engine, json_object * jconfig)
{
	assert(engine && engine->priv);
	struct ai_engine_curl_client *client = engine->priv;
	if(NULL == jconfig) jconfig = generate_default_config(NULL);
	
	client->jconfig = jconfig;
	client->server_url = json_get_value_default(jconfig, string, url, s_default_server_url);
	client->user_agent = json_get_value_default(jconfig, string, user_agent, s_default_user_agent);

	return 0;
}
static void curl_client_cleanup(struct ai_engine * engine)
{
	if(NULL == engine || NULL == engine->priv) return;
	struct ai_engine_curl_client *client = engine->priv;	
	
	pthread_mutex_destroy(&client->mutex);
	free(client);
}
static int curl_client_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults)
{
	assert(engine && engine->priv);
	if(NULL == frame || NULL == frame->data || frame->width <= 0 || frame->height <= 0) return -1;
	
	
	struct ai_engine_curl_client *client = engine->priv;
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
	
	CURL *curl = curl_easy_init();
	assert(curl);
	
	long response_code = 400;
	struct response_closure response[1];
	memset(response, 0, sizeof(*response));
	response_closure_init(response);
	
	curl_easy_setopt(curl, CURLOPT_URL, client->server_url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jpeg_data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, cb_jpeg);
	
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: image/jpeg");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	
	if(ret != CURLE_OK) {
		fprintf(stderr, "curl perform failed: %s\n", curl_easy_strerror(ret));
		goto label_cleanup;
	}
	
	ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if(ret != CURLE_OK) {
		fprintf(stderr, "curl getinfo failed: %s\n", curl_easy_strerror(ret));
		goto label_cleanup;
	}
	
	if(response->jerr == json_tokener_success) {
		if(response->jresult) {
			*p_jresults = json_object_get(response->jresult);
		}
	}
	
label_cleanup:
	if(curl) curl_easy_cleanup(curl);
	response_closure_cleanup(response);
	if(jpeg_data && jpeg_data != frame->data) free(jpeg_data);
	return ret;
}


ai_engine_t * ai_engine_init(ai_engine_t * engine, const char * plugin_type, void * user_data)
{
	if(strcasecmp(plugin_type, s_plugin_type) != 0) return NULL;
	
	if(NULL == engine) engine = calloc(1, sizeof(*engine));
	assert(engine);
	engine->user_data = user_data;
	
	struct ai_engine_curl_client *client = ai_engine_curl_client_new(engine);
	engine->priv = client;
	engine->init = curl_client_init;
	engine->cleanup = curl_client_cleanup;
	engine->predict = curl_client_predict;
	
	return engine;
}

void ai_engine_cleanup(ai_engine_t * engine)
{
	if(NULL == engine) return;
	if(engine->cleanup) engine->cleanup(engine);
	return;
}

