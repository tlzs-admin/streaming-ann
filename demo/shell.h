#ifndef DEMO_SHELL_H_
#define DEMO_SHELL_H_


#ifdef __cplusplus
extern "C" {
#endif

struct app_context;
struct shell_private;

struct shell_context
{
	struct app_context * app;
	struct shell_private * priv;
	
	int (* reload_config)(struct shell_context * shell, json_object * jconfig);
	int (* init)(struct shell_context * shell);
	int (* run)(struct shell_context * shell);
	int (* stop)(struct shell_context * shell);
};
struct shell_context * shell_context_init(struct shell_context * shell, void * app);
void shell_context_cleanup(struct shell_context * shell);


#ifdef __cplusplus
}
#endif
#endif
