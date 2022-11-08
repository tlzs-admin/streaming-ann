#ifndef DEMO_VIDEO_SOURCE2_H_
#define DEMO_VIDEO_SOURCE2_H_

#include <stdio.h>
#include <stdint.h>

#include <gst/gst.h>
#include "input-frame.h"

#include "video_source_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct video_source_common video_source_t;

//~ struct video_source2
//~ {
	//~ void * user_data;
	//~ GstElement * pipeline;
	//~ GstElement * filter;
	//~ GstElement * sink;
	//~ GstElement * audio_volume;
	//~ char * gst_command;
	
	//~ input_frame_t * frame_buffer[2];
	//~ char * uri;
	//~ char * cooked_uri;
	
	//~ int64_t start_time_ms;
	//~ long frame_number;
	
	//~ int is_running;
	//~ int is_paused;
	//~ int stopped;
	
	//~ int width;
	//~ int height;
	//~ pthread_t th;
	//~ pthread_mutex_t mutex;
	
	//~ GstState state;		/* Current state of the pipeline */
	//~ gint64 duration;  	/* Duration of the video, in nanoseconds */

	//~ // methods
	//~ int (*set_uri2)(struct video_source2 * video, const char * uri, int width, int height);
	//~ long (*get_frame)(struct video_source2 * video, long frame_number, input_frame_t frame[1]);
	
	//~ int (* play)(struct video_source2 * video);
	//~ int (* pause)(struct video_source2 * video);
	//~ int (* stop)(struct video_source2 * video);
	//~ int (* seek)(struct video_source2 * video, double position  /* seconds */);
	//~ int (* set_volume)(struct video_source2 * video, double volume);
	
	//~ // callbacks
	//~ int (* on_eos)(struct video_source2 * video, void * user_data);
	//~ int (* on_error)(struct video_source2 * video, void * user_data);
	//~ int (* on_state_changed)(struct video_source2 * video, GstState old_state, GstState new_state, void * user_data);
//~ };
//~ struct video_source2 * video_source2_init(struct video_source2 * video, void * user_data);
//~ void video_source2_cleanup(struct video_source2 * video);

#ifdef __cplusplus
}
#endif
#endif
