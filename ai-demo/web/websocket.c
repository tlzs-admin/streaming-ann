/*
 * websocket.c
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

#include <search.h>
#include <libsoup/soup.h>
#include <json-c/json.h>

#include <stdint.h>
#include <time.h>
#include "utils.h"

/**
 * ws-session message format: (json)
 * 
 * header: 
 * {
 *     "session-type": 0,  // 0: "producer", 1: "consumer" 
 *     "message-type": 0, 	// 0: "event", 1: "command" 
 *     "is_binary": false, // true / false
 * }
 * 
 * event: 
 * {
 *     "name": "<event_name>",
 *     "data": { }
 * }
 * 
 * command: // eg.  command_name="add-events", args=[ "event_name1", "event_name2", ...]
 * {
 *     "name": "<command_name>",  
 *     "args": [ ]
 * }
 * 
 * 
 * Sample messages:
 * {
 *     "header": { "session-type": 1, "message-type": 1 },
 *     "command": { "name": "add-events", "args": [ "alerts" ] }
 * }
 * 
 * {
 *     "header": { "session-type": 0, "message-type": 1 },
 *     "command": { "name": "register-device", "args": [ { "name: "device1", "uri": "" } ] }
 * }
 * 
 * {
 *     "header": { "session-type": 0, "message-type": 0 },
 *     "event": { "name": "alert", "data": { "source": "device1", "type": 0, "alert-message": "", "alert-sound-url": "url" } }
 * }
 * 
*/ 

static inline int64_t query_timestamp_ms(clockid_t clock_id)
{
	if(clock_id == -1) clock_id = CLOCK_MONOTONIC;
	struct timespec ts = { .tv_sec = 0 };
	int rc = clock_gettime(clock_id, &ts);
	if(rc) {
		perror("clock_gettime() failed");
		exit(1);
	}
	return (int64_t)ts.tv_sec * 1000 + ts.tv_sec / 1000000;
}

static inline int64_t query_timestamp_us(clockid_t clock_id)
{
	if(clock_id == -1) clock_id = CLOCK_MONOTONIC;
	struct timespec ts = { .tv_sec = 0 };
	int rc = clock_gettime(clock_id, &ts);
	if(rc) {
		perror("clock_gettime() failed");
		exit(1);
	}
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_sec / 1000;
}

enum ws_session_type 
{
	ws_session_type_producer,
	ws_session_type_consumer,
};

struct websocket_session
{
	struct websocket_context *ctx;
	SoupWebsocketConnection *conn;
	enum ws_session_type type;
	int state; // 0: new connection, 1: closing, 2: closed, -1: error
	
	clockid_t clock_id;
	int64_t creation_timestamp_ms;
	int64_t modification_timestamp_ms;
	
	json_object *jdevice;
	
	void *events_root;	// tsearch root
};

int websocket_session_add_event(struct websocket_session *session, const char *event_name)
{
	assert(session && event_name);
	
	char *name = strdup(event_name);
	void *p_node = tsearch(name, &session->events_root, (__compar_fn_t)strcasecmp);
	assert(p_node);
	
	if(*(char **)p_node == name) {	// already exists
		free(name);
	}
	return 0;
}

int websocket_session_remove_event(struct websocket_session *session, const char *event_name)
{
	assert(session && event_name);
	
	void *p_node = tfind(event_name, &session->events_root, (__compar_fn_t)strcasecmp);
	if(NULL == p_node) return 1;
	
	char *name = *(char **)p_node;
	tdelete(event_name, &session->events_root, (__compar_fn_t)strcasecmp);
	if(name) free(name);
	
	return 0;
}


struct websocket_session *websocket_session_new(struct websocket_context *ctx, SoupWebsocketConnection *conn, enum ws_session_type type)
{
	struct websocket_session *session = calloc(1, sizeof(*session));
	assert(session);
	
	session->ctx = ctx;
	session->conn = conn;
	session->type = type;
	
	session->clock_id = CLOCK_REALTIME;
	session->creation_timestamp_ms = query_timestamp_ms(session->clock_id);
	session->modification_timestamp_ms = session->creation_timestamp_ms;
	
	return session;
}

void websocket_session_free(struct websocket_session *session)
{
	if(NULL == session) return;
	
	if(session->conn) {
		g_object_unref(session->conn);
		session->conn = NULL;
	}
	free(session);
}


struct websocket_context
{
	void *user_data;
	void *priv;
	
	SoupServer *server;
	pthread_mutex_t mutex;
	
	size_t max_producers;
	size_t num_procuders;
	struct websocket_session **producers;
	
	size_t max_consumers;
	size_t num_consumers;
	struct websocket_session ** consumers;
	
	int (*add_session)(struct websocket_context *ctx, struct websocket_session *session);
	int (*remove_session)(struct websocket_context *ctx, struct websocket_session *session);
	
	int (*broadcast_event)(struct websocket_context *ctx, json_object *jevent);
};

struct websocket_context * websocket_context_init(struct websocket_context *ctx, void *user_data);
void websocket_context_cleanup(struct websocket_context *ctx);

static int websocket_broadcast_event(struct websocket_context *ctx, json_object *jevent)
{
	pthread_mutex_lock(&ctx->mutex);
	
	const char *event_name = json_get_value(jevent, string, event_name);
	printf("event_name: %s\n", event_name);
	
	for(size_t i = 0; i < ctx->num_consumers; ++i) {
		struct websocket_session *session = ctx->consumers[i];
		assert(session && session->conn);
	
	}
	pthread_mutex_unlock(&ctx->mutex);
	return 0;
}

static int websocket_add_session(struct websocket_context *ctx, struct websocket_session *session)
{
	int rc = -1;
	pthread_mutex_lock(&ctx->mutex);
	switch(session->type) {
	case ws_session_type_producer:
		if(ctx->num_procuders < ctx->max_producers) {
			ctx->producers[ctx->num_procuders++] = session;
			rc = 0;
		}
		break;
	case ws_session_type_consumer:
		if(ctx->num_consumers < ctx->max_consumers) {
			ctx->consumers[ctx->num_consumers++] = session;
			rc = 0;
		}
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&ctx->mutex);
	return rc;
}
static int websocket_remove_session(struct websocket_context *ctx, struct websocket_session *session)
{
	debug_printf("%s(): session=%p", __FUNCTION__, session);
	int rc = 0;
	if(NULL == session || NULL == session->ctx) return 1;
	
	pthread_mutex_lock(&ctx->mutex);
	switch(session->type) {
	case ws_session_type_producer:
		for(size_t i = 0; i < ctx->num_procuders; ++i) {
			if(ctx->producers[i] == session) {
				ctx->producers[i] = NULL;
				--ctx->num_procuders;
				
				if(i < ctx->num_procuders) {
					ctx->producers[i] = ctx->producers[ctx->num_procuders];
					ctx->producers[ctx->num_procuders] = NULL;
				}
				websocket_session_free(session);
			}
		}
		break;
	case ws_session_type_consumer:
		for(size_t i = 0; i < ctx->num_consumers; ++i) {
			if(ctx->consumers[i] == session) {
				ctx->consumers[i] = NULL;
				--ctx->num_consumers;
				
				if(i < ctx->num_consumers) {
					ctx->consumers[i] = ctx->consumers[ctx->num_consumers];
					ctx->consumers[ctx->num_consumers] = NULL;
				}
				websocket_session_free(session);
			}
		}
		break;
	default:
		rc = -1;
		break;
	}
	pthread_mutex_unlock(&ctx->mutex);
	return rc;
}

struct websocket_context * websocket_context_init(struct websocket_context *ctx, void *user_data)
{
	int rc = 0;
	if(NULL == ctx) ctx = calloc(1, sizeof(*ctx));
	assert(ctx);

	ctx->user_data = user_data;
	rc = pthread_mutex_init(&ctx->mutex, NULL);
	assert(0 == rc);
	
	ctx->max_producers = 64;
	ctx->max_consumers = 1024;
	
	struct websocket_session **producers = calloc(ctx->max_producers, sizeof(*producers));
	assert(producers);
	ctx->producers = producers;
	
	struct websocket_session **consumers = calloc(ctx->max_consumers, sizeof(*consumers));
	assert(consumers);
	ctx->consumers = consumers;
	
	ctx->add_session = websocket_add_session;
	ctx->remove_session = websocket_remove_session;
	ctx->broadcast_event = websocket_broadcast_event;
	
	return ctx;
}
void websocket_context_cleanup(struct websocket_context *ctx);


static void on_ws_source_producer(SoupServer *server, SoupWebsocketConnection *conn, const char *path, SoupClientContext *client, gpointer user_data);
static void on_ws_events_consumer(SoupServer *server, SoupWebsocketConnection *conn, const char *path, SoupClientContext *client, gpointer user_data);
static struct websocket_context g_ws[1];
int main(int argc, char **argv)
{
	struct websocket_context *ws = websocket_context_init(g_ws, NULL);
	GError *gerr = NULL;
	gboolean ok = TRUE;
	guint port = 9001;
	int use_ssl = 0;
	const char *cert_file = getenv("AI_DEMO_SERVER_CERT_FILE");
	const char *key_file = getenv("AI_DEMO_SERVER_KEY_FILE");
	
	SoupServer *server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "ai-demo-ws/v0.1.0-alpha", NULL);
	assert(server);

	if(cert_file && key_file) {
		use_ssl = 1;
		assert(cert_file && key_file);
		ok = soup_server_set_ssl_cert_file(server, cert_file, key_file, &gerr);
		if(gerr) {
			fprintf(stderr, "soup_server_set_ssl_cert_file(%s, %s) failed: %s\n", 
				cert_file, key_file, 
				gerr->message);
			g_error_free(gerr);
			gerr = NULL;
		}
		assert(ok);
		exit(1);
	}
	soup_server_add_websocket_handler(server, "/ws-source", NULL, NULL, on_ws_source_producer, ws, NULL);
	soup_server_add_websocket_handler(server, "/ws-events", NULL, NULL, on_ws_events_consumer, ws, NULL);
	
	
	ok = soup_server_listen_all(server, port, use_ssl?SOUP_SERVER_LISTEN_HTTPS:0, &gerr);
	if(gerr) {
		fprintf(stderr, "soup_server_listen_all(%u) failed: %s\n", 
			port, gerr->message);
		g_error_free(gerr);
		gerr = NULL;
	}
	assert(ok);
	
	GSList *uris = soup_server_get_uris(server);
	for(GSList *uri = uris; NULL != uri; uri = uri->next) {
		gchar *sz_uri = soup_uri_to_string(uri->data, FALSE);
		if(sz_uri) {
			fprintf(stderr, "websocket server: listening on %s\n", sz_uri);
			g_free(sz_uri);
		}
		soup_uri_free(uri->data);
	}
	g_slist_free(uris);
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	
	return 0;
}


static void on_ws_session_closing(SoupWebsocketConnection *conn, gpointer user_data)
{
	debug_printf("%s(): session=%p", __FUNCTION__, user_data);
	
	struct websocket_session *session = user_data;
	assert(session && session->conn == conn);
	session->state = 1;
	return;
}
static void on_ws_session_closed(SoupWebsocketConnection *conn, gpointer user_data)
{
	debug_printf("%s(): session=%p", __FUNCTION__, user_data);
	
	struct websocket_session *session = user_data;
	assert(session && session->conn == conn);

	session->state = 2;
	if(session->ctx) session->ctx->remove_session(session->ctx, session);
	else websocket_session_free(session);
	return;
}

static void on_ws_session_error(SoupWebsocketConnection *conn, GError *gerr, gpointer user_data)
{
	debug_printf("%s(): session=%p", __FUNCTION__, user_data);
	
	if(gerr) {
		fprintf(stderr, "on_ws_session_error(conn=%p, err_msg=%s)\n", conn, gerr->message);
	}
	return;
}

static void on_ws_session_pong(SoupWebsocketConnection *conn, GBytes *messgage, gpointer user_data)
{
	debug_printf("%s(): session=%p", __FUNCTION__, user_data);
	
	struct websocket_session *session = user_data;
	assert(session && session->conn == conn);
	
	session->modification_timestamp_ms = query_timestamp_ms(session->clock_id);
	return;
}

static int on_producer_command(SoupWebsocketConnection *conn, json_object *jcommand, struct websocket_session *session)
{
	char err_msg[1024] = "";
	const char *name = json_get_value(jcommand, string, name);
	json_object *jargs = NULL;
	(void)json_object_object_get_ex(jcommand, "args", &jargs);
	
	if(strcasecmp(name, "register-device") == 0) {
		int num_args = jargs?json_object_array_length(jargs):0;
		if(num_args < 1) {
			snprintf(err_msg, sizeof(err_msg) - 1, 
				"(command=%s) invalid args: (num_args=%d) < 1\n", 
				name, num_args);
			goto label_error;
		}
		
		json_object *jdevice = json_object_array_get_idx(jargs, 1);
		assert(jdevice);
		
		if(session->jdevice) json_object_put(session->jdevice); // unref
		session->jdevice = json_object_get(jdevice); // add_ref
	}
	
	
	return 0;
label_error:
	fprintf(stderr, "ERROR: %s.\n", err_msg);
	soup_websocket_connection_close(conn, SOUP_WEBSOCKET_CLOSE_BAD_DATA, err_msg);
	return -1;
}


static void on_ws_producer_message(SoupWebsocketConnection *conn, gint type, GBytes *messages, gpointer user_data)
{
	debug_printf("%s(): session=%p", __FUNCTION__, user_data);
	
	struct websocket_session *session = user_data;
	assert(session && session->conn == conn);
	
	debug_printf("type: %d, messages: len=%ld", type, (long)(messages?g_bytes_get_size(messages):-1));
	
	if(type != SOUP_WEBSOCKET_DATA_TEXT) {
		fprintf(stderr, "unsupport ws-data type: %d\n", type);
		soup_websocket_connection_close(conn, SOUP_WEBSOCKET_CLOSE_UNSUPPORTED_DATA, "ERROR: accept only json data.");
		return;
	}
	
	gsize cb_data = 0;
	const char * data = g_bytes_get_data(messages, &cb_data);
	if(NULL == data || cb_data <= 0) {
		fprintf(stderr, "ERROR: empty data\n");
		soup_websocket_connection_close(conn, SOUP_WEBSOCKET_CLOSE_BAD_DATA, "ERROR: empty data");
		return;
	}
	
	const char *err_msg = NULL;
	json_tokener *jtok = json_tokener_new();
	enum json_tokener_error jerr = json_tokener_error_parse_eof;
	
	json_object *jmessage = json_tokener_parse_ex(jtok, data, cb_data);
	jerr = json_tokener_get_error(jtok);
	json_tokener_free(jtok);
	
	if(jerr != json_tokener_success) {
		err_msg = json_tokener_error_desc(jerr);
		goto label_error;
	}
	
	json_bool ok = FALSE;
	json_object *jheader = NULL;
	
	err_msg = "invalid header";
	ok = json_object_object_get_ex(jmessage, "header", &jheader);
	if(!ok || NULL == jheader) goto label_error;
	
	int message_type = json_get_value_default(jheader, int, message-type, 0);
	
	json_object *jpayload = NULL;
	switch(message_type) {
	case 0: // event
		break;
	case 1: // command
		ok = json_object_object_get_ex(jmessage, "command", &jpayload);
		if(ok && jpayload) on_producer_command(conn, jpayload, session);
		break;
	default:
		err_msg = "invalid payload";
		goto label_error;
	}
	
	return;
label_error:
	fprintf(stderr, "ERROR: invalid json format: %s.\n", err_msg);
	soup_websocket_connection_close(conn, SOUP_WEBSOCKET_CLOSE_BAD_DATA, err_msg);
	if(jmessage) json_object_put(jmessage);
	return;
}

static void on_ws_source_producer(SoupServer *server, SoupWebsocketConnection *conn, const char *path, SoupClientContext *client, gpointer user_data)
{
	struct websocket_context *ctx = user_data;
	
	const char *origin = soup_websocket_connection_get_origin(conn);
	fprintf(stderr, "origin: %s, path: %s\n", origin, path);
	
	struct websocket_session *session = websocket_session_new(ctx, conn, ws_session_type_producer);
	assert(session);
	
	g_signal_connect(conn, "message", G_CALLBACK(on_ws_producer_message), session);
	g_signal_connect(conn, "closing", G_CALLBACK(on_ws_session_closing), session);
	g_signal_connect(conn, "closed",  G_CALLBACK(on_ws_session_closed), session);
	g_signal_connect(conn, "error",   G_CALLBACK(on_ws_session_error), session);
	g_signal_connect(conn, "pong",    G_CALLBACK(on_ws_session_pong), session);
	
	
	ctx->add_session(ctx, session);
	
	debug_printf("num_producers: %ld, session=%p\n", ctx->num_procuders, session);
	
	// send hello message
	static const char hello_msg[] = "hello\n";
	soup_websocket_connection_send_text(conn, hello_msg);
	soup_websocket_connection_send_text(conn, hello_msg);
	g_object_ref(conn);
	return;
}

static void on_ws_events_consumer(SoupServer *server, SoupWebsocketConnection *conn, const char *path, SoupClientContext *client, gpointer user_data)
{
	
}
