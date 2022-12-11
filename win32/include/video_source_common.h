#ifndef VIDEO_SOURCE_COMMON_H_
#define VIDEO_SOURCE_COMMON_H_

#include <stdio.h>
#include <gst/gst.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum video_source_type
{
	video_source_type_unknown,
	video_source_type_file,
	video_source_type_v4l2,
	video_source_type_https,
	video_source_type_rtsp,
	video_source_type_rtspt,
	video_source_types_count
};

enum video_source_subtype
{
	video_source_subtype_default,
	video_source_subtype_hls = 1,
	video_source_subtype_youtube = 2,
	
	video_source_subtype_file_mask = 0xFF00,
	video_source_subtype_mp4  = 0x100,
	video_source_subtype_mkv  = 0x200,
	video_source_subtype_rmvb = 0x300,
	video_source_subtype_avi  = 0x400,
};

enum video_source_type video_source_type_from_uri(const char * uri, int * p_subtype);
ssize_t youtube_uri_parse(const char * youtube_url, char embed_uri[static 4096], size_t size);

struct framerate_fraction
{
	int rate;
	int denominator;
};

enum video_frame_type
{
	video_frame_type_unknown = 0,
	video_frame_type_bgra = 1,
	video_frame_type_jpeg = 2, // input_frame_type_jpeg
	VIDEO_FRAME_TYPES_COUNT
};

struct video_frame
{
	enum video_frame_type type;
	long frame_number;
	long refs;
	int64_t ticks_ms;
	
	int width;
	int height;
	unsigned char *data;
	size_t length;
	void *meta_data;
	size_t cb_meta_data;
};
struct video_frame *video_frame_new(long frame_number, int width, int height, const void *image_data, size_t length, int take_memory);
struct video_frame *video_frame_addref(struct video_frame *frame);
void video_frame_unref(struct video_frame *frame);
#define video_frame_free(frame) video_frame_unref(frame)


struct video_source_common
{
	struct video_source_private *priv;
	void *user_data;
	
	char uri[4096];
	enum video_frame_type frame_type;
	int width;
	int height;
	struct framerate_fraction framerate;
	int (*set_uri)(struct video_source_common * video, const char *uri);
	int (*set_resolution)(struct video_source_common *video, int width, int height);
	int (*set_framerate)(struct video_source_common *video, int rate, int denominator);
	
	// private data
	int settings_changed;
	GstElement *pipeline;
	GstElement *caps_filter;
	GstCaps *caps;
	GstElement *appsink;
	char gst_command[8192];
	long frame_number;
	int64_t begin_timestamp_ms;
	int64_t begin_ticks_ms;
	GstState state;		/* Current state of the pipeline */
	double duration;  	/* Duration of the video, in seconds */
	double position;
	int err_code; // 0: no error, 1: eos, 2: error
	pthread_mutex_t mutex;
	struct video_frame *current_frame;

	// public methods
	int (*init)(struct video_source_common *video, 
		const char *uri,
		int width, int height, const struct framerate_fraction *framerate);
	int (*init_command)(struct video_source_common *video, const char *gst_command);
	struct video_frame *(*get_frame)(struct video_source_common * video);
	
	
	int (*play)(struct video_source_common * video);
	int (*pause)(struct video_source_common * video);
	int (*stop)(struct video_source_common * video);
	int (*seek)(struct video_source_common * video, double position  /* seconds */);
	int (*query_position)(struct video_source_common *video, double *position, double *duration);
	
	// callbacks
	int (*on_eos)(struct video_source_common * video, void * user_data);
	int (*on_error)(struct video_source_common * video, void * user_data);
	int (*on_state_changed)(struct video_source_common * video, GstState old_state, GstState new_state, void * user_data);
	int (*on_new_frame)(struct video_source_common *video, const struct video_frame *frame, void *user_data);
};

struct video_source_common *video_source_common_init(struct video_source_common *video, enum video_frame_type frame_type, void *user_data);
void video_source_common_cleanup(struct video_source_common *video);


#ifdef __cplusplus
}
#endif
#endif
