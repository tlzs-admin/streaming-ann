/*
 * alert-service.c
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

#include <stdint.h>
#include <libsoup/soup.h>
#include <getopt.h>
#include <pthread.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "utils.h"
#include "alert-service.h"
#include <errno.h>

struct alert_data_private
{
	struct alert_data *alert;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	pthread_t th;
};

static int alert_data_get_busy_status(struct alert_data *alert)
{
	int busy = 0;
	pthread_mutex_lock(&alert->mutex);
	busy = alert->busy;
	pthread_mutex_unlock(&alert->mutex);
	return busy;
}

static int alert_data_set_busy_status(struct alert_data *alert, int busy)
{
	pthread_mutex_lock(&alert->mutex);
	alert->busy = busy;
	pthread_mutex_unlock(&alert->mutex);
	return 0;
}

static void *run_command_thread(void *user_data)
{
	struct alert_data *alert = user_data;
	const char *command = alert->command;

	fprintf(stderr, "%s(): command=%s\n", __FUNCTION__, command);
	
	int rc = system(command);
	if(rc) {
		int err = errno;
		fprintf(stderr, "%s(%s): ret_code=%d, error=%s\n", __FUNCTION__, command, 
			rc,
			strerror(err));
	}
	alert_data_set_busy_status(alert, 0);
	int busy = alert_data_get_busy_status(alert);
	fprintf(stderr, "%s(): is_busy=%d\n", __FUNCTION__, busy);
	return (void *)(intptr_t)rc;
}

static int run_command_async(struct alert_data *alert)
{
	pthread_t th;
	pthread_mutex_lock(&alert->mutex);
	if(alert->busy) {
		pthread_mutex_unlock(&alert->mutex);
		return 1;
	}
	alert->busy = 1;
	pthread_mutex_unlock(&alert->mutex);
	
	int rc = pthread_create(&th, NULL, run_command_thread, alert);
	if(rc) {
		fprintf(stderr, "%s()::pthread_create() failed, err=%d\n", __FUNCTION__, rc);
		return rc;
	}
	pthread_detach(th);
	return rc;
}

static void * alert_thread(void *user_data)
{
	struct alert_data *alert = user_data;
	assert(alert && alert->priv);
	
	struct alert_data_private *priv = alert->priv;
	pthread_mutex_lock(&priv->mutex);
	
	int rc = 0;
	while(!alert->quit) {
		rc = pthread_cond_wait(&priv->cond, &priv->mutex);
		if(rc || alert->quit) break;
		
		int is_busy = alert_data_get_busy_status(alert);
		if(is_busy) continue;
		
		(void) run_command_async(alert);
	}

	pthread_mutex_unlock(&priv->mutex);
	return (void *)(intptr_t)rc;
}

struct alert_data_private *alert_data_private_new(struct alert_data *alert)
{
	struct alert_data_private *priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->alert = alert;
	
	int rc = 0;
	rc = pthread_cond_init(&priv->cond, NULL);
	assert(0 == rc);
	
	rc = pthread_mutex_init(&priv->mutex, NULL);
	assert(0 == rc);
	
	rc = pthread_create(&priv->th, NULL, alert_thread, alert);
	assert(0 == rc);
	
	return priv;
}
static void alert_data_private_free(struct alert_data_private *priv)
{
	if(NULL == priv) return;
	
	struct alert_data *alert = priv->alert;
	
	if(alert && !alert->quit) {
		alert->quit = 1;
		pthread_mutex_lock(&priv->mutex);
		pthread_cond_signal(&priv->cond);
		pthread_mutex_unlock(&priv->mutex);
		alert->err_code = pthread_join(priv->th, &alert->exit_code);
		memset(&priv->th, 0, sizeof(priv->th));
	}
	
	pthread_cond_destroy(&priv->cond);
	pthread_mutex_destroy(&priv->mutex);
	free(priv);
}

static int alert_notify(struct alert_data *alert, const char *source, int64_t timestamp)
{
	assert(alert && alert->priv);
	
	int is_busy = alert_data_get_busy_status(alert);
	if(is_busy) return -1;
	
	pthread_mutex_lock(&alert->mutex);
	if(timestamp <= 0) {
		struct timespec ts[1];
		memset(ts, 0, sizeof(ts));
		int rc = clock_gettime(CLOCK_REALTIME, ts);
		if(rc) {
			perror("clock_gettime() failed");
			pthread_mutex_unlock(&alert->mutex);
			return rc;
		}
		timestamp = ts->tv_sec;
	}
	
	if(timestamp < alert->expires_at) {
		fprintf(stderr, "[command=%s]: %ld seconds left to run next alert.\n",
			alert->command, (long)(alert->expires_at - timestamp));
			
		pthread_mutex_unlock(&alert->mutex);
		return -2;
	}
	alert->timestamp = timestamp;
	alert->expires_at = timestamp + alert->interval;
	pthread_mutex_unlock(&alert->mutex);
	
	struct alert_data_private *priv = alert->priv;
	pthread_mutex_lock(&priv->mutex);
	if(source) {
		if(alert->source) free(alert->source);
		alert->source = strdup(source);
	}
	
	pthread_cond_signal(&priv->cond);
	pthread_mutex_unlock(&priv->mutex);
	return 0;
}

struct alert_data *alert_data_new(const char *channel_name, const char *command)
{
	struct alert_data *alert = calloc(1, sizeof(*alert));
	assert(alert);
	alert->notify = alert_notify;
	
	alert->channel_name = strdup(channel_name);
	alert->command = strdup(command);
	
	int rc = pthread_mutex_init(&alert->mutex, NULL);
	assert(0 == rc);
	
	alert->priv = alert_data_private_new(alert);
	return alert;
}
void alert_data_free(struct alert_data *alert)
{
	if(NULL == alert) return;
	
	alert_data_private_free(alert->priv);
	alert->priv = NULL;
	
	free(alert->channel_name);
	free(alert->command);
	free(alert->source);

	pthread_mutex_destroy(&alert->mutex);
	
	memset(alert, 0, sizeof(*alert));
	free(alert);
}


static const char *s_alert_type_strings[] = {
	[alert_type_default] = "default",
	[alert_type_run_scripts] = "run_script",
	[alert_type_send_mail] = "send_mail",
};
enum alert_type alert_type_from_string(const char *sz_type)
{
	if(NULL == sz_type) return alert_type_default;
	for(int i = 0; i < alert_types_count; ++i) {
		if(strcasecmp(s_alert_type_strings[i], sz_type) == 0) return i;
	}
	return -1;
}
const char *alert_type_to_string(enum alert_type type)
{
	if(type < 0 || type >= alert_types_count) return NULL;
	return s_alert_type_strings[type];
}
void alert_server_context_dump(const struct alert_server_context *ctx, FILE *fp)
{
	fprintf(stderr, "==== %s() ====\n", __FUNCTION__);
	assert(ctx && ctx->alerts);
	
	if(NULL == fp) fp = stderr;
	
	fprintf(fp, "  port: %u\n", (unsigned int)ctx->port);
	fprintf(fp, "  default interval: %ld\n", (long)ctx->interval);
	fprintf(fp, "  num_alerts: %d\n", (int)ctx->num_alerts);
	for(int i = 0; i < ctx->num_alerts; ++i) {
		struct alert_data *alert = ctx->alerts[i];
		assert(alert);
		fprintf(fp, "    alerts[%d]: channel_name=%s, interval=%ld, command=%s\n", i, 
			alert->channel_name,
			(long)alert->interval,
			alert->command);
	}
}

static json_object *generate_default_config(const char *output_file)
{
	if(NULL == output_file) output_file = "alert-server.json.template";
	json_object *jconfig = json_object_new_object();
	assert(jconfig);
	
	json_object *jalerts = json_object_new_array();
	json_object_object_add(jconfig, "port", json_object_new_int(9119));
	json_object_object_add(jconfig, "interval", json_object_new_int64(60)); // default interval: waiting for 60 seconds to run next alert
	
	json_object_object_add(jconfig, "alerts", jalerts);
	json_object *jalert = NULL;
	
	jalert = json_object_new_object();
	json_object_object_add(jalert, "channel_name", json_object_new_string("channel0"));
	json_object_object_add(jalert, "command", json_object_new_string("bin/alert-channel0.sh"));
	json_object_object_add(jalert, "interval", json_object_new_int64(300)); 
	json_object_array_add(jalerts, jalert);
	
	jalert = json_object_new_object();
	json_object_object_add(jalert, "channel_name", json_object_new_string("channel1"));
	json_object_object_add(jalert, "command", json_object_new_string("bin/alert-channel1.sh"));
	json_object_object_add(jalert, "interval", json_object_new_int64(300)); 
	json_object_array_add(jalerts, jalert);
	
	fprintf(stderr, "%s(%s):\n%s\n", __FUNCTION__, output_file,  
		json_object_to_json_string_ext(jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE));
	
	json_object_to_file_ext(output_file, jconfig, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	
	return jconfig;
}

static int alert_server_parse_args(struct alert_server_context *ctx, int argc, char **argv)
{
	static struct option options[] = {
		{"conf", required_argument, 0, 'c'},
		{"port", required_argument, 0, 'p'},
		{NULL},
	};
	
	const char *conf_file = "alert-server.json";
	unsigned int port = 0;
	
	while(1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "c:p:", options, &option_index);
		if(c == -1) break;
		switch(c) {
		case 'c': conf_file = optarg; break;
		case 'p': port = atol(optarg); break;
		default:
			fprintf(stderr, "invalid argument '%c'(%.2x)\n", (unsigned char)c, (unsigned char)c);
			exit(1);
		}
	}
	
	// unparsed args list
	ctx->num_args = argc - optind; 
	ctx->argv = &argv[optind];
	
	json_object * jconfig = NULL;
	if(NULL == conf_file) conf_file = ctx->conf_file;
	if(conf_file) {
		jconfig = json_object_from_file(conf_file);
	}
	
	if(NULL == jconfig) {
		jconfig = generate_default_config(conf_file);
		assert(jconfig);
	}
	
	ctx->conf_file = conf_file;
	ctx->jconfig = jconfig;
	
	if(0 == port) port = json_get_value(jconfig, int, port);
	if(port) ctx->port = port;
	if(0 == ctx->port) ctx->port = 9119;
	
	int interval = json_get_value(jconfig, int, interval);
	if(interval > 0) ctx->interval = interval;
	if(0 == ctx->interval) ctx->interval = 60;
	
	json_object *jalerts = NULL;
	json_bool ok = json_object_object_get_ex(jconfig, "alerts", &jalerts);
	assert(ok && jalerts);
	
	int num_alerts = json_object_array_length(jalerts);
	assert(num_alerts > 0);
	
	struct alert_data **alerts = calloc(num_alerts, sizeof(*alerts));
	assert(alerts);
	
	ctx->num_alerts = num_alerts;
	ctx->alerts = alerts;
	
	for(int i = 0; i < num_alerts; ++i)
	{
		json_object *jalert = json_object_array_get_idx(jalerts, i);
		const char *channel_name = json_get_value(jalert, string, channel_name);
		const char *command = json_get_value(jalert, string, command);
		assert(channel_name && command);
			
		struct alert_data *alert = alert_data_new(channel_name, command);
		assert(alert);
		alert->interval = json_get_value_default(jalert, int, interval, ctx->interval);
		alerts[i] = alert;
	}
	
	alert_server_context_dump(ctx, stderr);
	return 0;
}
void alert_server_context_cleanup(struct alert_server_context *ctx)
{
	///< @todo
}

static void on_reset(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct alert_server_context *ctx = user_data;
	assert(ctx);
	
	if(msg->method != SOUP_METHOD_GET) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}
	
	const char *channel_name = NULL;
	const char *source = NULL;
	if(query) {
		channel_name = g_hash_table_lookup(query, "channel");
		source = g_hash_table_lookup(query, "source");
	}
	
	char err_msg[1024] = "";
	ssize_t cb_err = 0;
	if(NULL == channel_name || !channel_name[0]) {
		cb_err = snprintf(err_msg, sizeof(err_msg) - 1, "invalid channel_name: '%s'", channel_name);
		soup_message_set_response(msg, "text/plain", SOUP_MEMORY_COPY, err_msg, cb_err);
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	struct alert_data *alert = NULL;
	for(int i = 0; i < ctx->num_alerts; ++i) {
		if(NULL == ctx->alerts[i] || NULL == ctx->alerts[i]->channel_name) continue;
		if(strcasecmp(ctx->alerts[i]->channel_name, channel_name) == 0) {
			alert = ctx->alerts[i];
			break;
		}
	}
	
	if(NULL == alert) {
		cb_err = snprintf(err_msg, sizeof(err_msg) - 1, "channel '%s' not found.", channel_name);
		soup_message_set_response(msg, "text/plain", SOUP_MEMORY_COPY, err_msg, cb_err);
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	static const char *response_fmt = "{ "
		"  \"err_code\": %d, "
		"  \"source\": \"%s\", "
		"  \"channel_name\": \"%s\", "
		"  \"command\": \"reset\", "
		"  \"is_busy\": %d, "
		"  \"expires_at\": %ld "
		"}\n";
		
	alert->expires_at = 0;
	char response[4096] = "";
	ssize_t cb_response = snprintf(response, sizeof(response) - 1, 
		response_fmt,
		0, 
		source, channel_name, alert->command, 
		alert->busy, (long)alert->expires_at);
	
	fprintf(stderr, "response=%s\n", response);
	
	soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, response, cb_response);
	soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
	return;
	
}


static void on_alert(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct alert_server_context *ctx = user_data;
	assert(ctx);
	
	if(msg->method != SOUP_METHOD_GET) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}
	
	if(NULL == ctx->alerts) {
		soup_message_set_status(msg, SOUP_STATUS_SERVICE_UNAVAILABLE);
		return;
	}
	
	const char *source = NULL;
	const char *channel_name = NULL;
	if(query) {
		channel_name = g_hash_table_lookup(query, "channel");
		source = g_hash_table_lookup(query, "source");
	}
	
	char err_msg[1024] = "";
	ssize_t cb_err = 0;
	if(NULL == channel_name || !channel_name[0]) {
		cb_err = snprintf(err_msg, sizeof(err_msg) - 1, "invalid channel_name: '%s'", channel_name);
		soup_message_set_response(msg, "text/plain", SOUP_MEMORY_COPY, err_msg, cb_err);
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	struct alert_data *alert = NULL;
	for(int i = 0; i < ctx->num_alerts; ++i) {
		if(NULL == ctx->alerts[i] || NULL == ctx->alerts[i]->channel_name) continue;
		if(strcasecmp(ctx->alerts[i]->channel_name, channel_name) == 0) {
			alert = ctx->alerts[i];
			break;
		}
	}
	
	if(NULL == alert) {
		cb_err = snprintf(err_msg, sizeof(err_msg) - 1, "channel '%s' not found.", channel_name);
		soup_message_set_response(msg, "text/plain", SOUP_MEMORY_COPY, err_msg, cb_err);
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	static const char *response_fmt = "{ "
		"  \"err_code\": %d, "
		"  \"source\": \"%s\", "
		"  \"channel_name\": \"%s\", "
		"  \"command\": \"%s\", "
		"  \"is_busy\": %d, "
		"  \"expires_at\": %ld "
		"  \"timeout\": %ld seconds "
		"}\n";
	
	struct timespec ts[1];
	memset(ts, 0, sizeof(ts));
	clock_gettime(CLOCK_REALTIME, ts);
	int64_t timestamp = ts->tv_sec;
	
	int rc = alert->notify(alert, source, timestamp);
	char response[4096] = "";
	ssize_t cb_response = snprintf(response, sizeof(response) - 1, 
		response_fmt,
		rc, 
		source, channel_name, alert->command, 
		alert->busy, (long)alert->expires_at, (long)(alert->expires_at - timestamp));
	
	fprintf(stderr, "response=%s\n", response);
	
	soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, response, cb_response);
	soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
	return;
}

int alert_server_run(struct alert_server_context *ctx)
{
	assert(ctx && ctx->alerts);
	assert(ctx->port != 0);
	
	GMainLoop *loop = NULL;
	SoupServer *server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "alert-server/v0.1.0-alpha", NULL);
	soup_server_add_handler(server, "/alert", on_alert, ctx, NULL);
	soup_server_add_handler(server, "/reset", on_reset, ctx, NULL);
	
	GError *gerr = NULL;
	gboolean ok = soup_server_listen_all(server, ctx->port, 0, &gerr);
	if(gerr) {
		fprintf(stderr, "soup_server_listen_all(:%u) failed: %s\n", ctx->port, gerr->message);
		g_error_free(gerr);
		gerr = NULL;
	}
	assert(ok);
	
	GSList *uris = soup_server_get_uris(server);
	for(GSList *uri = uris; NULL != uri; uri = uri->next)
	{
		fprintf(stderr, "listening: %salert\n", soup_uri_to_string(uri->data, FALSE));
		soup_uri_free(uri->data);
		uri->data = NULL;
	}
	g_slist_free(uris);
	
	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	return 0;
}

static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
static void service_init()
{
	curl_global_init(CURL_GLOBAL_ALL);
}
struct alert_server_context *alert_server_context_init(struct alert_server_context *ctx, void *user_data)
{
	int rc = 0;
	if(NULL == ctx) ctx = calloc(1, sizeof(*ctx));
	assert(ctx);
	ctx->user_data = user_data;
	rc = pthread_once(&s_once_key, service_init);
	assert(0 == rc);
	
	if(NULL == ctx->parse_args) ctx->parse_args = alert_server_parse_args;
	if(NULL == ctx->run) ctx->run = alert_server_run;
	
	return ctx;
}

#if defined(TEST_ALERT_SERVICE_) && defined(_STAND_ALONE)
static struct alert_server_context g_alert_server[1] = {{
	.parse_args = alert_server_parse_args,
}};

int main(int argc, char **argv)
{
	int rc = 0;
	struct alert_server_context *alert_server = alert_server_context_init(g_alert_server, NULL);
	rc = alert_server->parse_args(alert_server, argc, argv);
	assert(0 == rc);
	
	rc = alert_server_run(alert_server);

	alert_server_context_cleanup(alert_server);
	return rc;
}
#endif

