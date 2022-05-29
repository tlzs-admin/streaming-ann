#ifndef AI_PLUGINS_HTTP_CLIENT_H_
#define AI_PLUGINS_HTTP_CLIENT_H_

#include <stdio.h>
#include <json-c/json.h>
#include <curl/curl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ai_http_client
{
	void * user_data;
	json_object * jconfig;
	
	CURL * curl;
	char * url;
	
	int (*set_url)(struct ai_http_client * http, const char * url);
	long (* post)(struct ai_http_client * http, const char * content_type, const void * data, size_t cb_data, json_object ** p_jresults);
};

typedef struct ai_http_client ai_http_client_t;

ai_http_client_t * ai_http_client_new(json_object * jconfig, void * user_data);
void ai_http_client_cleanup(struct ai_http_client * http);


#ifdef __cplusplus
}
#endif
#endif
