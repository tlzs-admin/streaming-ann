#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <inttypes.h>
#include <endian.h>
#include <json-c/json.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FALSE
#define FALSE 0
#define TRUE (!FALSE)
#endif

#ifndef VERBOSE
#define VERBOSE (0)
#endif

#ifndef debug_printf
#ifdef _DEBUG
#define debug_printf(fmt, ...) do { fprintf(stderr, "\e[33m%s(%d)::" fmt "\e[39m" "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
#else
#define debug_printf(fmt, ...) do { } while(0)
#endif
#endif

#ifndef log_printf
#if defined(VERBOSE) && (VERBOSE > 1)
extern FILE * g_log_fp;
#define log_printf(fmt, ...) do { fprintf(g_log_fp?g_log_fp:stdout, "\e[32m" fmt "\e[39m" "\n", ##__VA_ARGS__); } while(0)
#else
#define log_printf(fmt, ...) do {  } while(0)
#endif
#endif

#define UNUSED(x)	(void)((x))

#define make_nonblock(fd) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)
#define close_fd(fd) do { if(fd != -1) { close(fd); fd = -1; } } while(0)

int check_file(const char * path_name);
int check_folder(const char * path_name, int auto_create);

ssize_t load_binary_data(const char * filename, unsigned char **p_dst);
ssize_t bin2hex(const unsigned char * data, size_t length, char * hex);
ssize_t hex2bin(const char * hex, size_t length, unsigned char * data);
char * trim_left(char * p_begin, char * p_end);
char * trim_right(char * p_begin, char * p_end);
#define trim(p, p_end)	 trim_right(trim_left(p, p_end), p_end)

typedef struct app_timer
{
	double begin;
	double end;
}app_timer_t;
double app_timer_start(app_timer_t * timer);
double app_timer_stop(app_timer_t * timer);
double global_timer_start();
double global_timer_stop(const char * prefix);



/**
 * @ingroup utils
 * @{
 */
typedef char * string;
#define json_get_value(jobj, type, key)	({									\
		type value = (type)0;												\
		if (jobj) {															\
			json_object * jvalue = NULL;									\
			json_bool ok = FALSE;											\
			ok = json_object_object_get_ex(jobj, #key, &jvalue);			\
			if(ok && jvalue) value = (type)json_object_get_##type(jvalue);	\
		}																	\
		value;																\
	})

#define json_get_value_default(jobj, type, key, defval)	({					\
		type value = (type)defval;											\
		json_object * jvalue = NULL;										\
		json_bool ok = FALSE;												\
		ok = json_object_object_get_ex(jobj, #key, &jvalue);				\
		if(ok && jvalue) value = (type)json_object_get_##type(jvalue);		\
		value;																\
	})
	
/**
 * @}
 */


ssize_t read_password_stdin(char secret[], size_t size);

#ifdef __cplusplus
}
#endif
#endif

