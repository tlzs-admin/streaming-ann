/*
 * camera-switch.c
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


#include <getopt.h>
#include <time.h>
#include <gst/gst.h>
#include <pthread.h>
#include <libsoup/soup.h>
#include <json-c/json.h>
#include <cairo/cairo.h>
#include <libsoup/soup.h>

#include "utils.h"
#include "video_source_common.h"
#include "img_proc.h"

#include "camera_manager.h"

#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/prctl.h>

#include <syslog.h>
#include <sys/file.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>


#define json_get(jobj, sz_key) ({ json_object *jvalue = NULL; \
		json_bool ok = json_object_object_get_ex(jobj, sz_key, &jvalue); \
		if(!ok && jvalue) {json_object_put(jvalue); jvalue = NULL; } \
		jvalue; })


#define log_fmt(level, fmt, ...) do { \
		debug_printf("[%d]::" fmt "\n", level, ##__VA_ARGS__); \
		syslog(level, fmt, ##__VA_ARGS__); \
	}while(0)


static sem_t *s_app_mutexs;
volatile int g_quit;
static void on_signal(int sig)
{
	switch(sig) {
	case SIGINT: case SIGUSR1:
		g_quit = 1;
		break;
	case SIGCHLD:
		write(2, "SIGCHLD\n", sizeof("SIGCHLD\n") - 1);
		break;
	default:
		write(2, "unknown signals\n", sizeof("unknown signals\n") - 1);
		abort();
	}
	return;
}

static void on_signal_usr2(int sig)
{
	static char msg[] = "killed by parent process\n";
	switch(sig) {
	case SIGUSR2:
		write(2, msg, sizeof(msg) - 1);
		break;
	default:
		break;
	}
	abort();
}

static void register_signals(void (*action_handler)(int), int sig1, ...) 
{
	int sig = sig1;
	va_list ap;
	va_start(ap, sig1);
	while(sig != -1) {
		signal(sig, action_handler);
		sig = va_arg(ap, int);
	}
	va_end(ap);
	return;
}

static int make_single_instance_app(const char *pidfile)
{
	int rc = 0;
	int lock_fd = open(pidfile, O_CREAT | O_WRONLY, 0666);
	if(lock_fd == -1) {
		perror("open lockfile");
		exit(1);
	}
	rc = flock(lock_fd, LOCK_EX | LOCK_NB);
	if(rc == -1) {
		int err = errno;
		if(err == EWOULDBLOCK) {
			syslog(LOG_WARNING, "instance is runing...");
			exit(1);
		}else {
			syslog(LOG_ALERT, "flock() failed: %s", strerror(errno));
		}
		exit(1);
	}

	pid_t pid = getpid();
	log_fmt(LOG_INFO, "granted lock for pid %ld\n", (long)pid);
	return lock_fd;
}


struct time_range
{
	struct tm begin;
	struct tm end;
};

struct schedule_plan
{
	int weekday_masks[7];
	size_t num_ranges;
	struct time_range *ranges;
};

#define APP_NAME "camera-switch"
struct app_instance
{
	void *user_data;
	char *app_name;
	int lock_fd;
	int quit;
	
	json_object *jconfig;
	int shmid;
	sem_t *app_mutexes;
	
	pid_t group_pid;
	int64_t expires_at;
	
	sigevent_t timer_ev[1];
	timer_t timer_id;
	pid_t child_pid;
	
	GMainLoop *loop;
	
};
__attribute__((unused))
static int app_lock(struct app_instance *app, int child)
{
	sem_t *mutex = NULL;
	if(app->app_mutexes) mutex = &app->app_mutexes[(child?1:0)];
	
	if(mutex) return sem_wait(mutex);
	return -1;
}
__attribute__((unused))
static int app_unlock(struct app_instance *app, int child)
{
	sem_t *mutex = NULL;
	if(app->app_mutexes) mutex = &app->app_mutexes[(child?1:0)];
	
	if(mutex) return sem_post(mutex);
	return -1;
}

static struct app_instance *app_instance_initialize(struct app_instance *app, 
	const char *app_name,
	const char *pidfile, 
	void *user_data)
{
	int rc = 0;
	if(NULL == app_name) app_name = APP_NAME;
	if(NULL == pidfile) pidfile = "/tmp/" APP_NAME "_ea300680ab.pid";
	
	openlog(app_name, LOG_PID, LOG_DAEMON);
	
	int lock_fd = make_single_instance_app(pidfile);
	assert(lock_fd != -1);
	
	/*
	 * init app context
	 */
	if(NULL == app) app = calloc(1, sizeof(*app));
	assert(app);
	srand(12345);
	
	app->user_data = user_data;
	app->app_name = strdup(app_name);
	app->lock_fd = lock_fd;

	
	/* 
	 * create processes shared semphores 
	 */
	size_t shared_size = sizeof(*s_app_mutexs) * 2;
	shared_size = (shared_size + SHMLBA - 1) / SHMLBA * SHMLBA;
	
	int shmid = 0;
	shmid = shmget(IPC_PRIVATE, shared_size, IPC_CREAT | 0600);
	if(shmid == -1) {
		log_fmt(LOG_ALERT, "shmget() failed: %s", strerror(errno));
		exit(1);
	}
	
	app->shmid = shmid;
	s_app_mutexs = shmat(shmid, NULL, 0);
	if(s_app_mutexs	== (void *)-1) {
		log_fmt(LOG_ALERT, "shmat() failed: %s", strerror(errno));
		exit(1);
	}
	app->app_mutexes = s_app_mutexs;
	
	// init parent semaphore with value 1
	rc = sem_init(&s_app_mutexs[0], 1, 1);
	if(rc) {
		log_fmt(LOG_ALERT, "sim_init(parent) failed: %s", strerror(errno));
		exit(1);
	}
	
	// init parent semaphore with value 0
	rc = sem_init(&s_app_mutexs[1], 1, 0);
	if(rc) {
		log_fmt(LOG_ALERT, "sim_init(child) failed: %s", strerror(errno));
		exit(1);
	}

	register_signals(on_signal, SIGUSR1, SIGCHLD, -1);
	return app;
}

static void app_instance_cleanup(struct app_instance *app)
{
	if(NULL == app) return;
	if(app->lock_fd != -1) { close(app->lock_fd); app->lock_fd = -1; }
	if(app->app_name) { free(app->app_name); app->app_name = NULL; }
	if(app->jconfig) { json_object_put(app->jconfig); app->jconfig = NULL; }
	
	
	if(s_app_mutexs) {
		shmdt(s_app_mutexs);
		s_app_mutexs = NULL;
	}
	
	if(app->shmid != -1) {
		shmctl(app->shmid, IPC_RMID, NULL);
		app->shmid = -1;
	}
	
	closelog();
	
	if(app->lock_fd != -1) {
		if(getpid() == app->group_pid) {
			flock(app->lock_fd, LOCK_UN);
		}
		close(app->lock_fd);
		app->lock_fd = -1;
	}
	return;
}


struct child_process_info
{
	struct app_instance *app;
	int pipe_fd;
	pid_t pid;
	int64_t created_at;
	int64_t expires_at;
};

static int camera_switch_process(json_object *jconfig);
static pid_t app_health_check(struct app_instance *app, pid_t pid, const struct tm *expires_at);
static int app_schedule_run(struct app_instance *app, json_object *jconfig, int (*child_process)(json_object *));
static int child_exec(int (*child_process)(json_object *), json_object *jconfig);
static int start_web_service(struct app_instance *app, guint port);

static void *worker_thread(void *user_data)
{
	int rc = 0;
	struct app_instance *app = user_data;
	assert(app);
	
	rc = app_schedule_run(app, app->jconfig, camera_switch_process);
	
	pthread_exit((void *)(intptr_t)rc);
}

int main(int argc, char **argv)
{
	int rc = 0;
	const char *conf_file = "camera-switch.json";
	if(argc > 1) conf_file = argv[1];
	
	struct app_instance app[1] = { NULL };
	app_instance_initialize(app, NULL, NULL, NULL);
	
	gst_init(&argc, &argv);
	json_object *jconfig = json_object_from_file(conf_file);
	assert(jconfig);
	guint webservice_port = json_get_value_default(jconfig, int, webservice_port, 8080);
	
	app->jconfig = jconfig;
	
	// detach from the controlling terminal and run in the background
	//~ rc = daemon(1, 1); 
	//~ if(rc == -1) { perror("daemon()"); exit(1); }
	//~ syslog(LOG_NOTICE, "daemon starting ...");
	

	pid_t pid = getpid();
	pid_t group_pid = getpgid(pid);
	assert(pid == group_pid);
	
	app->group_pid = group_pid;
	log_fmt(LOG_INFO, "[parent process]: pid=%ld, group_pid=%ld", (long)pid, (long)group_pid);
	
	pthread_t th;
	rc = pthread_create(&th, NULL, worker_thread, app);
	assert(0 == rc);

	rc = start_web_service(app, webservice_port);
	
	g_quit = 1;
	void *exit_code = NULL;
	rc = pthread_join(th, &exit_code);
	
	syslog(LOG_NOTICE, "daemon stopped. exit_code=%ld, rc=%d", (long)exit_code, rc);
	app_instance_cleanup(app);
	return rc;
}

static pid_t app_health_check(struct app_instance *app, pid_t pid, const struct tm *expires_at)
{
	int rc = 0;
	int wstate = 0;
	pid_t wpid = 0;
	
	debug_printf("[parent process(ppid=%ld)]: camera_switch pid = %ld\n", 
		(long)getpid(),
		(long)pid);
	
	while(0 == wpid && !g_quit && pid != -1) {
		wpid = waitpid(pid, &wstate, WNOHANG);
		if(wpid == pid) {
			debug_printf("waitpid(%ld): wstate = %d(%.8x)\n", (long)pid, wstate, (unsigned int)wstate);
			pid = -1;
			break;
		}
		
		if(expires_at) {
			struct timespec ts[1] = {{ 0 }};
			struct tm t[1] = {{ 0 }};
			clock_gettime(CLOCK_REALTIME, ts);
			localtime_r(&ts->tv_sec, t);
			if(t->tm_hour > expires_at->tm_hour
				|| (t->tm_hour == expires_at->tm_hour && t->tm_min > expires_at->tm_hour) 
				) 
			{
				rc = kill(pid, SIGUSR2);
				if(rc) perror("kill pid failed");
				wpid = waitpid(pid, &wstate, WNOHANG);
				log_fmt(LOG_NOTICE, "stop child(%ld): wstate = %d(%.8x)\n", (long)pid, wstate, (unsigned int)wstate);
				if(wpid == pid) pid = -1;
				break;
			}
		}
		sleep(10);
	}
	
	if(g_quit && pid > 0) {
		rc = kill(pid, SIGUSR2);
		if(rc) {
			perror("kill pid failed");
		}
		wpid = waitpid(pid, &wstate, WNOHANG);
		log_fmt(LOG_NOTICE, "waitpid(%ld): wstate = %d(%.8x)\n", (long)pid, wstate, (unsigned int)wstate);
		if(wpid == pid) pid = -1;
	}
	
	return pid;
}

static int child_exec(int (*child_process)(json_object *), json_object *jconfig)
{
	pid_t child_pid = getpid();
			
	log_fmt(LOG_INFO, "[child process]: status=running, pid=%ld", (long)child_pid);
	close(0); close(1); // close unused files
	
	register_signals(on_signal_usr2, SIGUSR2, -1);
	signal(SIGUSR2, on_signal_usr2);
	
	int rc = camera_switch_process(jconfig);
	log_fmt(LOG_INFO, 
		"[child process]: status=stopped, pid=%ld, exited with code %d.", 
		(long)child_pid,
		rc);
		
	return rc;
}

__attribute__((unused))
static pid_t fork_exec(struct app_instance *app, int64_t timeout, json_object *jconfig, int (*child_process)(json_object *))
{
	int rc = 0;
	pid_t pid = -1;
	pid_t parent_pid = getpid();
	
	union {
		int fds[2];
		struct {
			int parent_fd;
			int child_fd;
		};
	} pipe_fds = {{ -1, -1 }};
	rc = pipe(pipe_fds.fds);
	assert(0 == rc);
	
	pid = fork();
	if(-1 == pid) {
		perror("fork()"); 
		syslog(LOG_ALERT, "failed to start child process.");
		return -1;
	}
	
	if(0 == pid) {
		close(pipe_fds.parent_fd); // close parent write end
		pipe_fds.parent_fd = -1;
		
		rc = prctl(PR_SET_PDEATHSIG, SIGTERM);	// terminate child process if parent died
		if(-1 == rc) {
			log_fmt(LOG_ALERT, "prctl() failed: %s", strerror(errno));
			exit(1);
		}
		
		if(getppid() != parent_pid) {
			// parent exited before the prctl() call
			log_fmt(LOG_ALERT, "parent exited.");
			exit(1);
		}
		
		close(0); close(1); 
		
		close(pipe_fds.child_fd);
		pipe_fds.child_fd = -1;
		
		
		
		app_instance_cleanup(app);
		exit(0);
	}
	
	close(pipe_fds.child_fd);	// close child write end
	pipe_fds.child_fd = -1;
	

	if(timeout > 0) {
		clockid_t clock_id = CLOCK_MONOTONIC;
		
	//	sigevent_t ev;
		struct itimerspec its[1];
		memset(its, 0, sizeof(its));
		
		rc = clock_gettime(clock_id, &its->it_value);
		assert(0 == rc);
		
		its->it_value.tv_sec += timeout;
		its->it_interval = its->it_value;
		
		
		//~ timer_t timer_id = timer_create(clock_id, app->timer_ev, &app->timer_id);
		//~ timer_settime(
	}
	
	return pid;
}

struct schedule_plan *schedule_plan_load_config(struct schedule_plan *plan, json_object *jplan)
{
	assert(jplan);
	if(NULL == plan) plan = calloc(1, sizeof(*plan));
	
	json_object *jweekday_masks = json_get(jplan, "weekday_masks");
	if(jweekday_masks) {
		int num_wdays = json_object_array_length(jweekday_masks);
		if(num_wdays > 7) num_wdays = 7;
		for(int i = 0; i < num_wdays; ++i) {
			json_object *jmask = json_object_array_get_idx(jweekday_masks, i);
			if(NULL == jmask) break;
			plan->weekday_masks[i] = json_object_get_boolean(jmask);
		}
	}
	
	json_object *jtime_ranges = json_get(jplan, "time_ranges");
	int num_ranges = 0;
	if(jtime_ranges) num_ranges = json_object_array_length(jtime_ranges);
	if(num_ranges <= 0) return plan;
	
	struct time_range *ranges = calloc(num_ranges, sizeof(*ranges));
	for(int i = 0; i < num_ranges; ++i) {
		json_object *jrange = json_object_array_get_idx(jtime_ranges, i);
		if(NULL == jrange) break;
		const char *sz_range = json_object_get_string(jrange);
		if(NULL == sz_range) continue;
		
		struct tm begin, end;
		memset(&begin, 0, sizeof(begin));
		memset(&end, 0, sizeof(end));
		
		char *p_next = strptime(sz_range, "%H:%M", &begin);
		debug_printf("begin_time: %.2hd:%.2hd", begin.tm_hour, begin.tm_min);
		
		sz_range = p_next;
		while(sz_range && (sz_range[0] == ' ' || sz_range[0] == '-')) ++sz_range;
		if(sz_range && sz_range[0]) {
			p_next = strptime(sz_range, "%H:%M", &end);
			debug_printf("end_time: %.2hd:%.2hd", end.tm_hour, end.tm_min);
		}
		
		ranges[i].begin = begin;
		ranges[i].end = end;
	}
	
	plan->num_ranges = num_ranges;
	plan->ranges = ranges;
	
	return plan;
}

void schedule_plan_cleanup(struct schedule_plan *plan)
{
	if(NULL == plan) return;
	if(plan->ranges) {
		free(plan->ranges);
		plan->ranges = NULL;
	}
	memset(plan, 0, sizeof(*plan));
}

__attribute__((unused))
static void timer_notify(union sigval sival)
{
	sem_t * app_mutex = sival.sival_ptr;
	assert(app_mutex);
	
	debug_printf("%s(semaphore=%p) ...", __FUNCTION__, app_mutex); 
	
	sem_post(app_mutex);
	return;
}



static int app_schedule_run(struct app_instance *app, json_object *jconfig, int (*child_process)(json_object *))
{
	int rc = 0;
	json_object *jschedules = NULL;
	int num_plans = 0;
	int active_schedule_index = json_get_value(jconfig, int, active_schedule_index);
	
	json_bool ok = json_object_object_get_ex(jconfig, "schedules", &jschedules);
	if(ok && jschedules) num_plans = json_object_array_length(jschedules);
	pid_t pid = -1;
	assert(num_plans > 0);
	
	/*
	 * load plans
	 */
	struct schedule_plan *plans = calloc(num_plans, sizeof(*plans));
	assert(plans);

	for(int i = 0; i < num_plans; ++i) {
		json_object *jplan = json_object_array_get_idx(jschedules, i);
		if(NULL == jplan) break;
		
		struct schedule_plan *plan = schedule_plan_load_config(&plans[i], jplan);
		assert(plan == &plans[i]);
	}

	assert(active_schedule_index < num_plans);
	struct schedule_plan *active_plan = &plans[active_schedule_index];
	
	while(!g_quit) {
		//~ rc = sem_wait(app->app_mutexes[0]);
		//~ assert(0 == rc);
		
		struct timespec ts[1] = {{ 0 }};
		struct tm t[1] = {{ 0 }};
		rc = clock_gettime(CLOCK_REALTIME, ts);
		assert(0 == rc);
		struct tm *now = localtime_r(&ts->tv_sec, t);
		assert(now && now == t);
	
		timer_t timer_id;
		memset(&timer_id, 0, sizeof(timer_id));
		
		if(!active_plan->weekday_masks[now->tm_wday]) {
			///< todo : use cron to restart app
			//~ rc = sem_wait(&app->app_mutexes[0]);
			//~ assert(0 == rc);
			
			//~ struct sigevent ev;
			//~ memset(&ev, 0, sizeof(ev));
			//~ ev.sigev_notify = SIGEV_THREAD;
			//~ ev.sigev_value.sival_ptr = &app->app_mutexes[0];
			//~ ev.sigev_notify_function = timer_notify;
			
			
			//~ rc = timer_create(CLOCK_MONOTONIC, &ev, &timer_id);
			//~ assert(0 == rc);
			
			//~ struct itimerspec its;
			//~ memset(&its, 0, sizeof(its));
			//~ its.it_value.tv_sec = 5;	// sleep 
			//~ its.it_interval.tv_sec = 3600;  // check status every hour
		
			//~ timer_settime(timer_id, 0, &its, NULL); 
			
			//~ rc = sem_wait(&app->app_mutexes[0]);
			//~ assert(0 == rc);
			
			log_fmt(LOG_INFO, "[waiting] weekday_masks: %d", active_plan->weekday_masks[now->tm_wday]);
				
			sleep(3600);
			continue;
		}
		
		int64_t seconds_of_the_day = (int)now->tm_hour * 3600 + (int)now->tm_hour * 60;
		
		int num_ranges = active_plan->num_ranges;
		int activated_range_index = -1;
		for(int i = 0; i < num_ranges; ++i) {
			struct time_range *range = &active_plan->ranges[i];
			int64_t begin = (int)range->begin.tm_hour * 3600 + (int)range->begin.tm_min * 60;
			int64_t end = (int)range->end.tm_hour * 3600 + (int)range->end.tm_min * 60;
			
			if(seconds_of_the_day >= begin && seconds_of_the_day < end) {
				activated_range_index = i;
				break;
			}
		}
		
		if(activated_range_index == -1) {
			///< todo: timer_wait
			unsigned int interval = 300; // 5 minutes
			if(active_plan->ranges) {
				struct time_range *range = &active_plan->ranges[0];
				unsigned int begin = (unsigned int)range->begin.tm_hour * 3600 + (unsigned int)range->begin.tm_min * 60;
				if(seconds_of_the_day < begin) interval = begin - seconds_of_the_day;
				
				log_fmt(LOG_INFO, 
					"sleep %u seconds, next start time: %.2d:%.2d", 
					interval,
					range->begin.tm_hour, 
					range->begin.tm_min
				);
			}
			sleep(interval);
			continue;
		}

		// create child process
		pid = fork();
		if(pid == -1) { 
			perror("fork()"); 
			syslog(LOG_ALERT, "failed to start child process.");
			return -1; 
		}
		if(pid == 0) { // child process
			child_exec(child_process, jconfig);
			app_instance_cleanup(app);
			closelog();
			exit(rc);
		}
		
		///< todo: create a timer to kill the child process on schedule
		pid = app_health_check(app, pid, &active_plan->ranges[activated_range_index].end);
	}
	return 0;
}

static int camera_switch_process(json_object *jconfig)
{
	sleep(30);
	//~ struct video_controller *controller = video_controller_init(NULL, jconfig, NULL);
	//~ assert(controller);

	//~ video_controller_cleanup(controller);
	//~ free(controller);

	return 0;
}

static guint show_config(SoupMessage *msg, struct app_instance *app)
{
	if(app && app->jconfig) {
		const char *response = json_object_to_json_string_ext(app->jconfig, JSON_C_TO_STRING_PRETTY);
		ssize_t cb_response = 0;
		if(response) cb_response = strlen(response);
		
		if(cb_response > 0) {
			soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, response, cb_response);
			return SOUP_STATUS_OK;
		}
	}
	return SOUP_STATUS_NOT_FOUND;
}
static void web_service_on_config(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct app_instance *app = user_data;
	assert(app);
	
	guint status = SOUP_STATUS_NOT_FOUND;
	if(msg->method == SOUP_METHOD_GET) {
		status = show_config(msg, app);
		soup_message_set_status(msg, status);
		return;
	}
	
	if(msg->method != SOUP_METHOD_POST) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	///< todo parse and reload config
	// ...
	
	soup_message_set_status(msg, SOUP_STATUS_ACCEPTED);
	return;
}


static guint show_lastest_notify(SoupMessage *msg, struct app_instance *app)
{
	return SOUP_STATUS_OK;
}
static guint on_event_notify(SoupMessage *msg, struct app_instance *app)
{
	///< todo : process event::notify
	return SOUP_STATUS_ACCEPTED;
}
static void web_service_on_notify(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	struct app_instance *app = user_data;
	assert(app);
	
	guint status = SOUP_STATUS_BAD_REQUEST;
	if(msg->method == SOUP_METHOD_GET) {
		status = show_lastest_notify(msg, app);
		soup_message_set_status(msg, status);
		return;
	}
	
	if(msg->method != SOUP_METHOD_POST) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	status = on_event_notify(msg, app);
	soup_message_set_status(msg, status);
	return;
}

#include <glib-unix.h>
static gboolean on_web_service_signal(gpointer user_data)
{
	GMainLoop *loop = user_data;
	if(loop) {
		g_main_loop_quit(loop);
	}
	return G_SOURCE_REMOVE;
}

static int start_web_service(struct app_instance *app, guint port)
{
	int rc = 0;
	if(0 == port) port = 8080;
	SoupServer *server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "camera-switch/webservice", NULL);
	assert(server);
	
	soup_server_add_early_handler(server, "/config", web_service_on_config, app, NULL);
	soup_server_add_early_handler(server, "/notify", web_service_on_notify, app, NULL);
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	assert(loop);
	app->loop = g_main_loop_ref(loop);
	
	g_unix_signal_add(SIGINT, on_web_service_signal, loop);
	
	GError *gerr = NULL;
	gboolean ok = soup_server_listen_all(server, port, 0, &gerr);
	if(!ok || gerr) {
		log_fmt(LOG_ALERT, "%s(%u) failed: %s", __FUNCTION__, port, gerr?gerr->message:"unknown error.");
	}
	assert(ok);
	
	GSList *uris = soup_server_get_uris(server);
	for(GSList *uri = uris; NULL != uri; uri = uri->next) {
		char *sz_uri = soup_uri_to_string(uri->data, FALSE);
		log_fmt(LOG_NOTICE, "webservice::listening on %s", sz_uri);
		soup_uri_free(uri->data);
		free(sz_uri);
	}
	g_slist_free(uris);
	
	log_fmt(LOG_INFO, "webservice::[notify, config]");
	
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	return rc;
}
