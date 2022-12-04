#ifndef DEMO_APP_H_
#define DEMO_APP_H_

#include <stdio.h>
#include <json-c/json.h>
#include <limits.h>
#include <pthread.h>

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ai_context
{
	int id;
	int enabled;
	pthread_mutex_t mutex;
	struct ai_engine *engine;
	int quit;
};

struct app_private;
struct app_context
{
	void * user_data;
	struct app_private * priv;
	json_object * jconfig;
	
	char work_dir[PATH_MAX];
	char name[256];
	char title[256];
	
	int (*reload_config)(struct app_context *app, const char *conf_file);
	int (*init)(struct app_context *app, int argc, char **argv);
	int (*run)(struct app_context *app);
	int (*stop)(struct app_context *app);
	void (*cleanup)(struct app_context *app);
};

struct app_context *app_context_init(struct app_context *app, void * user_data);
void app_context_cleanup(struct app_context *app);


#ifdef __cplusplus
}
#endif
#endif
