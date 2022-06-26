/*
 * video_source2.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
 * 
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
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>

#include "video_source2.h"
#include <gst/gst.h>

#include "utils.h"

#define PROTOCOL_rtsp       "rtsp://"
#define PROTOCOL_https      "https://"
#define PROTOCOL_file       "file://"
#define PROTOCOL_v4l2       "/dev/video"


enum video_source_type
{
	video_source_type_unknown,
	video_source_type_file,
	video_source_type_v4l2,
	video_source_type_https,
	video_source_type_rtsp,
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

static const char * s_video_source_protocols[video_source_types_count] = {
	[video_source_type_unknown] = "default",
	[video_source_type_file] = PROTOCOL_file,
	[video_source_type_v4l2] = PROTOCOL_v4l2,
	[video_source_type_https] = PROTOCOL_https,
	[video_source_type_rtsp] = PROTOCOL_rtsp,
};

static gboolean is_regular_file(const char *path_name)
{
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc < 0) return FALSE;
	
	return ((st->st_mode & S_IFMT) == S_IFREG);
}
static gboolean is_video_file(const char * path_name, char **p_content_type, int * p_subtype)
{
	if(!is_regular_file(path_name)) return FALSE;
	gchar * content_type = NULL;
	gboolean uncertain = TRUE;
	content_type = g_content_type_guess(path_name, NULL, 0, &uncertain);
	if(uncertain || NULL == content_type) {
		if(content_type) g_free(content_type);
		return FALSE;
	}
	
	gboolean ret = TRUE;
	int subtype = video_source_subtype_default;
	if(g_content_type_equals(content_type, "video/mp4")) {
		subtype |= video_source_subtype_mp4;
	}else if(g_content_type_equals(content_type, "video/x-matroska")) {
		subtype |= video_source_subtype_mkv;
	}else if(g_content_type_equals(content_type, "application/vnd.rn-realmedia")) {
		subtype |= video_source_subtype_rmvb;
	}else if(g_content_type_equals(content_type, "video/x-msvideo")) {
		subtype |= video_source_subtype_avi;
	}else {
		ret = g_content_type_equals(content_type, "video/mpeg") 
		 || g_content_type_equals(content_type, "video/quicktime") 
		 || g_content_type_equals(content_type, "video/webm") 
		 || g_content_type_equals(content_type, "video/x-flv") 
		 || g_content_type_equals(content_type, "video/x-ms-wmv") 
		 || 0;
	}
	if(p_subtype) *p_subtype = subtype;

	if(p_content_type) *p_content_type = content_type;
	else g_free(content_type);
	
	return ret;
}

enum video_source_type video_source2_type_from_uri(const char * uri, int * p_subtype)
{
	enum video_source_type type = video_source_type_unknown;
	
	for(type = video_source_type_v4l2; type < video_source_types_count; ++type)
	{
		if(s_video_source_protocols[type] && 
			strncasecmp(uri, s_video_source_protocols[type], strlen(s_video_source_protocols[type])) == 0) break;
	}
	
	int subtype = video_source_subtype_default;
	if(type == video_source_types_count) {
		if(strncasecmp(uri, PROTOCOL_file, sizeof(PROTOCOL_file) - 1) == 0) uri += sizeof(PROTOCOL_file) - 1;
		
		gboolean ok = is_video_file(uri, NULL, &subtype);
		if(ok) type = video_source_type_file;
	}
	else if(type == video_source_type_https) {
		uri += strlen(s_video_source_protocols[type]);
		if(strncasecmp(uri, "www.youtube.com", sizeof("www.youtube.com") - 1) == 0) {
			subtype |= video_source_subtype_youtube;
		}else {
			char * p_ext = strrchr(uri, '.');
			if(p_ext && strcasecmp(p_ext, ".m38u") == 0) subtype |= video_source_subtype_hls;
		}
	}
	
	if(p_subtype) *p_subtype = subtype;
	return type;
}



static int get_youtube_embed_uri(const char * youtube_url, char embed_uri[static 4096], size_t size)
{
	static const char * fmt = "youtube-dl " 
		" --format 'best[ext=mp4][protocol=https][height<=480]' "
		" --get-url '%s' ";
		
	char command[8192] = "";
	snprintf(command, sizeof(command), fmt, youtube_url);
	FILE * fp = popen(command, "r");
	if(NULL == fp) return -1;
	
	char * uri = fgets(embed_uri, size, fp);
	int rc = pclose(fp);
	
	debug_printf("rc=%d, embed_uri: %s\n", rc, uri);
	
	return uri?rc:-1;
}



/***********************************************************************************
 * video_source
***********************************************************************************/
static gboolean video_source_on_eos(GstBus * bus, GstMessage * message, struct video_source2 * video)
{
	debug_printf("%s()...\n", __FUNCTION__);
	g_print ("End-Of-Stream reached.\n");
	gst_element_set_state(video->pipeline, GST_STATE_READY);
	video->is_running = 0;
	video->stopped = 1;
	
	if(video->on_eos) video->on_eos(video, video->user_data);
	return FALSE;
}
static gboolean video_source_on_error(GstBus * bus, GstMessage * message, struct video_source2 * video)
{
	debug_printf("%s()...\n", __FUNCTION__);
	
	GError * gerr = NULL;
	gchar * debug_info = NULL;
	
	gst_message_parse_error(message, &gerr, &debug_info);
	fprintf(stderr, "[ERROR]::%s: %s\n"
		"  --> debug info: %s\n", 
		GST_OBJECT_NAME(message->src), 
		gerr->message,
		debug_info
	);
	g_error_free(gerr);
	g_free(debug_info);
	
	gst_element_set_state(video->pipeline, GST_STATE_READY);
	video->is_running = 0;
	video->stopped = 1;
	
	if(video->on_error) video->on_error(video, video->user_data);
	
	return FALSE;
}
static void on_state_changed(GstBus * bus, GstMessage * message, struct video_source2 * video)
{
	GstState old_state, new_state, pending_state;
	gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
	if(GST_MESSAGE_SRC(message) == GST_OBJECT(video->pipeline)) {
		video->state = new_state;
		debug_printf("State set to %s\n", gst_element_state_get_name(new_state));
		if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
			struct global_params * params = video->user_data;
			video->stopped = 0;
			video->is_running = 1;
			
			gboolean shell_update_ui(struct global_params * params);
			shell_update_ui(params);
		}else if(new_state == GST_STATE_READY || new_state == GST_STATE_NULL) {
			video->stopped = 1;
		}
		
		if(video->on_state_changed) video->on_state_changed(video, old_state, new_state, video->user_data);
	}
	return;
}

static long video_source2_get_frame(struct video_source2 * video, long prev_frame, input_frame_t * dst)
{
	pthread_mutex_lock(&video->mutex);
	input_frame_t * frame = video->frame_buffer[0];
	
	//debug_printf("%s(frame=%p) ...\n", __FUNCTION__, frame);
	
	if(NULL == frame) {
		pthread_mutex_unlock(&video->mutex);
		return -1;
	}

	if(prev_frame != video->frame_number) {
		input_frame_copy(dst, frame);
	}
	pthread_mutex_unlock(&video->mutex);
	
	//debug_printf("%s(prev_frame=%ld, frame=%ld) final.\n", __FUNCTION__, prev_frame, video->frame_number);
	return video->frame_number;
}

static long video_source2_set_frame(struct video_source2 * video, input_frame_t * new_frame)
{
	input_frame_t * old_frame = video->frame_buffer[1];
	if(old_frame) {
		input_frame_free(old_frame);
		video->frame_buffer[1] = NULL;
	}

	pthread_mutex_lock(&video->mutex);
	video->frame_buffer[1] = video->frame_buffer[0];
	video->frame_buffer[0] = new_frame;
	++video->frame_number;
	pthread_mutex_unlock(&video->mutex);

	//debug_printf("%s(new_frame=%p) final.\n", __FUNCTION__, new_frame);
	return 0;
}

static void video_source2_on_bgra_filter(GstElement * filter, GstBuffer * buffer, struct video_source2 * video)
{
	int width = 0;
	int height = 0;

	GstPad * pad = gst_element_get_static_pad(filter, "src");
	assert(pad);

	GstCaps * caps = gst_pad_get_current_caps(pad);
	assert(caps);

	GstStructure * info = gst_caps_get_structure(caps, 0);
	assert(info);

	gboolean rc = FALSE;
	rc = gst_structure_get_int(info, "width", &width); assert(rc);
	rc = gst_structure_get_int(info, "height", &height); assert(rc);

	if(width != video->width || height != video->height)
	{
		video->frame_number = 0;	// reset
		video->width = width;
		video->height = height;

		const char * fmt = gst_structure_get_string(info, "format");
		//~ printf("[thread_id: %ld]::uri: %s\n",
				 //~ (long)pthread_self(), priv->uri);
		debug_printf("== format: %s, size=%dx%d\n", fmt, width, height);
	}
	gst_object_unref(pad);

	GstMapInfo map[1];
	memset(map, 0, sizeof(map));

	input_frame_t * frame = input_frame_new();
	frame->type = input_frame_type_bgra;
	
	rc = gst_buffer_map(buffer, map, GST_MAP_READ);
	if(rc)
	{
		bgra_image_init(frame->image, width, height, map->data);
		gst_buffer_unmap(buffer, map);
	}

	++video->frame_number;
	video_source2_set_frame(video, frame);
	return;
}

static void on_application(GstBus * bus, GstMessage * msg, void * user_data)
{
	return;
}


static GstElement * relaunch_pipeline(const char * gst_command, struct video_source2 * video)
{
	debug_printf("%s(): gst_command=\ngst-launch-1.0 %s", __FUNCTION__, gst_command);
	video->is_running = 0;
	if(video->pipeline) {
		gst_element_set_state(video->pipeline, GST_STATE_NULL);
		gst_element_get_state(video->pipeline, &video->state, NULL, GST_CLOCK_TIME_NONE);
		gst_object_unref(video->pipeline);
		video->pipeline = NULL;
	}
	
	GError * gerr = NULL;
	GstElement * pipeline = gst_parse_launch(gst_command, &gerr);
	if(gerr) {
		fprintf(stderr, "[ERROR]::gst_parse_launch() failed: %s\n", 
			gerr->message);
		g_error_free(gerr);
		gerr = NULL;
		return NULL;
	}
	
	GstElement * filter = gst_bin_get_by_name(GST_BIN(pipeline), "filter");
	assert(filter);
	g_signal_connect(filter, "handoff", G_CALLBACK(video_source2_on_bgra_filter), video);
	
	GstElement * audio_volume = gst_bin_get_by_name(GST_BIN(pipeline), "audio_volume");
	
	GstBus * bus = gst_element_get_bus(pipeline);
	assert(bus);
	gst_bus_add_signal_watch(bus);
	g_signal_connect(G_OBJECT(bus), "message::eos", G_CALLBACK(video_source_on_eos), video);
	g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(video_source_on_error), video);
	g_signal_connect(G_OBJECT(bus), "message::state-changed", G_CALLBACK(on_state_changed), video);
	g_signal_connect(G_OBJECT(bus), "message::application", G_CALLBACK(on_application), video);
	gst_object_unref(bus);
	
	 
	
	video->pipeline = pipeline;
	video->filter = filter;
	video->audio_volume = audio_volume;
	
	gst_element_set_state(video->pipeline, GST_STATE_NULL);

	video->is_running = 1;
	return pipeline;
}

static int video_source2_set_uri2(struct video_source2 * video, const char * uri, int width, int height)
{
	enum video_source_type type;
	int subtype = 0;
	if(width <= 0) width = 1280;
	if(height <= 0) height = 720;
	
	type = video_source2_type_from_uri(uri, &subtype);
	if(type == video_source_type_unknown || type >= video_source_types_count) {
		fprintf(stderr, "[ERROR]::invalid uri: %s\n", uri);
		return -1;
	}
	
	char gst_command[8192] = "";
	
	static const char * default_decoder = " decodebin ! videoconvert ";
	static const char * hls_decoder = " hlsdemux ! decodebin ! videoconvert ";

#ifndef JETSON_TX2
	static const char * mp4_decoder = " qtdemux name=demux "
				" demux.audio_0 ! queue silent=true ! decodebin ! audioconvert ! audioresample "
				" ! volume name=\"audio_volume\" volume=0.5 ! autoaudiosink "
				" demux.video_0 ! queue silent=true ! decodebin ! videoconvert ";
#else
	static const char * mp4_decoder = " qtdemux name=demux "
				//~ " demux.audio_0 ! queue silent=true ! decodebin ! audioconvert ! audioresample "
				//~ " ! volume name=\"audio_volume\" volume=0.5 ! autoaudiosink "
				" demux.video_0 ! queue silent=true ! h264parse ! omxh264dec ! videoconvert ";
#endif
	static const char * mkv_decoder = " matroskademux name=demux "
				" demux.audio_0 ! queue silent=true ! decodebin ! audioconvert ! audioresample "
				" ! volume name=\"audio_volume\" volume=0.5 ! autoaudiosink "
				" demux.video_0 ! queue silent=true ! decodebin ! videoconvert ";
	static const char * rmvb_decoder = " rmdemux name=demux "
				//~ " demux.audio_0 ! queue ! decodebin ! audioconvert ! audioresample "   ///< @BUG unable to sync with audio channels 
				//~ " ! volume name=\"audio_volume\" volume=0.5 ! autoaudiosink "
				" demux.video_0 ! queue silent=true ! decodebin ! videoconvert ";
	static const char * avi_decoder = " avidemux name=demux "
				" demux.audio_0 ! queue ! decodebin ! audioconvert ! audioresample "   ///< @BUG unable to sync with audio channels 
				" ! volume name=\"audio_volume\" volume=0.5 ! autoaudiosink "
				" demux.video_0 ! queue silent=true ! decodebin ! videoconvert ";
	

#define BGRA_PIPELINE " ! videoscale ! video/x-raw,format=BGRA,width=%d,height=%d ! videoconvert ! identity name=filter ! fakesink name=sink sync=true"

	const char * decoder = default_decoder;
	switch(type) {
	case video_source_type_v4l2:
		snprintf(gst_command, sizeof(gst_command), 	
			"v4l2src device=%s ! videoconvert " BGRA_PIPELINE,
			uri, width, height);
		break;
	case video_source_type_file: 
		if(strncasecmp(uri, PROTOCOL_file, sizeof(PROTOCOL_file) - 1) == 0) uri += sizeof(PROTOCOL_file) - 1;
		
		subtype &= video_source_subtype_file_mask;
		printf("subtype: %x\n", subtype);
		switch(subtype) {
		case video_source_subtype_mp4:  decoder = mp4_decoder;  break;
		case video_source_subtype_mkv:  decoder = mkv_decoder;  break;
		case video_source_subtype_rmvb: decoder = rmvb_decoder; break;
		case video_source_subtype_avi:  decoder = avi_decoder;  break;
		default:
			break;
		}
		snprintf(gst_command, sizeof(gst_command), 	
			"filesrc location=\"%s\" ! %s " BGRA_PIPELINE,
			uri, decoder,
			width, height);
		break;
	case video_source_type_https:
		if(subtype & video_source_subtype_hls) {
			snprintf(gst_command, sizeof(gst_command), 	
				"souphttpsrc location=\"%s\" ! %s " BGRA_PIPELINE,
				uri, hls_decoder,
				width, height);
			break;
		}else if(subtype & video_source_subtype_youtube) {
			char embed_uri[4096] = "";
			int rc = get_youtube_embed_uri(uri, embed_uri, sizeof(embed_uri));
			if(rc) return -1;
			
			snprintf(gst_command, sizeof(gst_command), 	
				"souphttpsrc is-live=true location=\"%s\" ! %s " BGRA_PIPELINE,
				embed_uri, mp4_decoder,
				width, height);
			break;
		}
		return -1;
	case video_source_type_rtsp:
		snprintf(gst_command, sizeof(gst_command), 	
			"rtspsrc location='%s' ! %s " BGRA_PIPELINE, uri, default_decoder,
			width, height);
		break;
	default:
		return -1;
	}
	
	debug_printf("gst-command: %s\n", gst_command);
	if(video->gst_command) free(video->gst_command);
	video->gst_command = strdup(gst_command);
	
	if(video->uri) free(video->uri);
	video->uri = strdup(uri);
	
	relaunch_pipeline(gst_command, video);
	return 0;
}
static int video_source2_play(struct video_source2 * video);
static int video_source2_pause(struct video_source2 * video);
static int video_source2_stop(struct video_source2 * video);
static int video_source2_seek(struct video_source2 * video, double position);
static int video_source2_set_volume(struct video_source2 * video, double volume);
struct video_source2 * video_source2_init(struct video_source2 * video, void * user_data)
{
	if(NULL == video) video = calloc(1, sizeof(*video));
	assert(video);
	video->user_data = user_data;
	
	video->set_uri2 = video_source2_set_uri2;
	video->get_frame = video_source2_get_frame;
	
	video->play = video_source2_play;
	video->stop = video_source2_stop;
	video->pause = video_source2_pause;
	video->seek = video_source2_seek;
	video->set_volume = video_source2_set_volume;
	
	pthread_mutex_init(&video->mutex, NULL);
	
	return video;
}
void video_source2_cleanup(struct video_source2 * video)
{
	video->is_running = 0;
	if(video->state >= GST_STATE_PAUSED) {
		video->stop(video);
	}
	gst_object_unref(video->pipeline);
	video->pipeline = NULL;
	
	free(video->gst_command);
	video->gst_command = NULL;
	
	free(video->uri);
	video->uri = NULL;
}

static int video_source2_play(struct video_source2 * video)
{
	if(!video->is_running) relaunch_pipeline(video->gst_command, video);

	//debug_printf("%s(%p): pipeline=%p...\n", __FUNCTION__, video, video->pipeline);
	assert(video->pipeline);

	GstStateChangeReturn ret_code = gst_element_set_state(video->pipeline, GST_STATE_PLAYING);
	if(ret_code == GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "[ERROR]::%s(), ret_code=GST_STATE_CHANGE_FAILURE(%d)\n", __FUNCTION__, (int)GST_STATE_CHANGE_FAILURE);
		gst_object_unref(video->pipeline);
		video->pipeline = NULL;
		return -1;
	}
	video->is_running = 1;
	return 0;
}
static int video_source2_pause(struct video_source2 * video)
{
	if(NULL == video->pipeline) return -1;
	gst_element_set_state(video->pipeline, GST_STATE_PAUSED);
	return 0;
}
static int video_source2_stop(struct video_source2 * video)
{
	if(NULL == video->pipeline) return -1;
	gst_element_set_state(video->pipeline, GST_STATE_NULL);
	video->is_running = 0;
	return 0;
}
static int video_source2_seek(struct video_source2 * video, double position /* seconds */)
{
	gst_element_seek_simple(video->pipeline, GST_FORMAT_TIME, 
		GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
		(gint64)(position * GST_SECOND));
	return 0;
}
static int video_source2_set_volume(struct video_source2 * video, double volume)
{
	if(NULL == video->audio_volume) return -1;
	
	if(volume == -1) {
		g_object_set(G_OBJECT(video->audio_volume), "mute", (gboolean)TRUE, NULL);
		return 0;
	}
	
	if(volume < 0 || volume > 1.5) return -1;
	if(video->audio_volume) {
		g_object_set(G_OBJECT(video->audio_volume), 
			"mute", (gboolean)FALSE, 
			"volume", volume, 
			NULL); 
	}
	
	return 0;
}

