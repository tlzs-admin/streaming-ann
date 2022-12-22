#ifndef SHELL_H_
#define SHELL_H_

#include <stdio.h>
#include <json-c/json.h>

#ifdef __cplusplus
extern "C" {
#endif

struct app_context;
struct shell_context
{
	struct shell_private *priv;
	struct app_context *app;
	
	int (*reload_config)(struct shell_context *shell, json_object *jconfig);
	int (*init)(struct shell_context *shell, json_object *jconfig);
	int (*run)(struct shell_context *shell);
	int (*stop)(struct shell_context *shell);
	
	// callbacks
	void *user_data;
	int (*on_init_windows)(struct shell_context *shell, void *user_data);
};
struct shell_context *shell_context_init(struct shell_context *shell, struct app_context *app);
void shell_context_cleanup(struct shell_context *shell);


#ifdef __cplusplus
}
#endif
#endif
