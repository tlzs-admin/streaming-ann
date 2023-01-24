#ifndef CAMERA_MANAGER_H_
#define CAMERA_MANAGER_H_

#include <stdio.h>
#include <json-c/json.h>
#include <pthread.h>
#include "video_source_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_SOURCE_INFO_NAME_LEN (100)
struct video_controller;
struct video_source_info
{
	struct video_source_common video[1]; // base_class
	char name[VIDEO_SOURCE_INFO_NAME_LEN];
	struct video_controller *controller;
	json_object *jinfo;
	int state;
	int quit;
};

struct video_controller
{
	void *user_data;
	void *priv;
	json_object *jconfig;
	
	int64_t begin_timestamp_ms;
	int64_t expires_at;

	int state;
	int quit;
	
	int (*reload_config)(struct video_controller *controller, json_object *jconfig);
	int (*run)(struct video_controller *controller, int extern_loop);
	
	struct video_frame *(*get_current_frame)(struct video_controller *controller, struct video_frame *frame);
	struct video_frame *(*addref_frame)(struct video_controller *controller, struct video_frame *frame);
	void (*unref_frame)(struct video_controller *controller, struct video_frame *frame);
	
	// callbacks
	int (*on_ai_result)(struct video_controller *controller, struct video_frame *frame, json_object *jresult);
};

struct video_controller *video_controller_init(struct video_controller *controller, json_object *jconfig, void *user_data);
void video_controller_cleanup(struct video_controller *controller);


#ifdef __cplusplus
}
#endif
#endif
