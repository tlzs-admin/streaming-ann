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

#include <pthread.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "httpclient.h"
#include "utils.h"

static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
static void do_init(void)
{
	curl_global_init(CURL_GLOBAL_ALL);
}


static int http_set_url(struct ai_http_client * http, const char * url)
{
	if(!url || strncasecmp(url, "http://", 7) || strncasecmp(url, "https://", 8)) return -1;
	if(http->url) free(http->url);
	http->url = strdup(url);
	return 0;
}


struct http_data
{
	json_tokener * jtok;
	json_object * jresult;
	enum json_tokener_error jerr;
};

static size_t on_response(void * ptr, size_t size, size_t n, void * user_data)
{
	size_t cb = size * n;
	if(0 == cb) return 0;
	struct http_data * ctx = user_data;
	
	json_tokener * jtok = ctx->jtok;
	json_object * jresult = json_tokener_parse_ex(jtok, ptr, cb);
	enum json_tokener_error jerr = json_tokener_get_error(jtok);
	if(jerr != json_tokener_continue) {
		if(jerr != json_tokener_success) {
			if(jresult) json_object_put(jresult);
			jresult = NULL;
			return 0;
		}
		ctx->jresult = jresult;
		ctx->jerr = jerr;
		json_tokener_reset(jtok);
		ctx->jtok = NULL;
	}
	return cb;
}


static long http_post(struct ai_http_client * http, const char * content_type, const void * data, size_t length, json_object ** p_jresult)
{
	assert(http && http->url && http->curl);
	CURL * curl = http->curl;
	CURLcode ret = 0;
	long response_code = -1;
	
	struct http_data ctx[1];
	memset(ctx, 0, sizeof(ctx));
	ctx->jtok = json_tokener_new();
	ctx->jerr = json_tokener_error_parse_null;
	
	curl_easy_setopt(curl, CURLOPT_URL, http->url);
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)length);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
	
	struct curl_slist * headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: image/jpeg");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	
	if(ctx->jtok) {
		json_tokener_free(ctx->jtok);
		ctx->jtok = NULL;
	}
	
	if(ret == CURLE_OK) {
		ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	}else {
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
	}
	
	
	if(ctx->jerr == json_tokener_success) {
		assert(ctx->jresult);
		if(p_jresult) *p_jresult = json_object_get(ctx->jresult);
	}
	
	json_object_put(ctx->jresult);
	return response_code;
}


struct ai_http_client * ai_http_client_new(json_object * jconfig, void * user_data)
{
	pthread_once(&s_once_key, do_init);
	
	struct ai_http_client * http = calloc(1, sizeof(*http));
	assert(http);
	
	http->jconfig = jconfig;
	http->user_data = user_data;
	
	const char * url = json_get_value(jconfig, string, url);
	assert(url);
	
	CURL * curl = curl_easy_init();
	assert(curl);
	
	http->curl = curl;
	http->url = strdup(url);
	
	http->set_url = http_set_url;
	http->post = http_post;
	return http;
}

void ai_http_client_free(struct ai_http_client * http)
{
	if(NULL == http) return;
	if(http->curl) {
		curl_easy_cleanup(http->curl);
		http->curl = NULL;
	}
	if(http->url) {
		free(http->url);
		http->url = NULL;
	}
	free(http);
}

