#ifndef ALERT_SERVICE_H_ 
#define ALERT_SERVICE_H_

#include <stdio.h>
#include <stdint.h>
#include <json-c/json.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum alert_type
{
	alert_type_default,
	alert_type_run_scripts,
	alert_type_send_mail,
	alert_types_count
};

struct alert_data
{
	void *priv;
	char *channel_name;
	char *command;
	
	int (*notify)(struct alert_data *alert, const char *source, int64_t timestamp);
	// private data
	int64_t interval;
	int64_t timestamp;
	int64_t expires_at;
	pthread_mutex_t mutex;
	int busy;
	int quit;
	int err_code;
	void *exit_code;
	
	char *source; // sender name
};
struct alert_data *alert_data_new(const char *channel_name, const char *command);
void alert_data_free(struct alert_data *alert);


struct alert_server_context
{
	void *user_data;
	const char *conf_file;
	json_object *jconfig;
	int num_args;
	char **argv;

	int num_alerts;
	struct alert_data **alerts;
	
	unsigned int port;
	int64_t interval;
	int64_t expires_at;
	int is_busy;
	
	int (*parse_args)(struct alert_server_context *ctx, int argc, char **argv);
	int (*run)(struct alert_server_context *ctx);
};

struct alert_server_context *alert_server_context_init(struct alert_server_context *ctx, void *user_data);
void alert_server_context_cleanup(struct alert_server_context *ctx);
void alert_server_context_dump(const struct alert_server_context *ctx, FILE *fp);

#ifdef __cplusplus
}
#endif
#endif
