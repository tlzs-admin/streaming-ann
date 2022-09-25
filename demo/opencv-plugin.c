/*
 * opencv-plugin.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
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

#include <errno.h>

#include "utils.h"
#include "ai-engine.h"
#include "cv-wrapper.h"

#define AI_PLUGIN_TYPE_STRING "ai-engine::cvface"

/* Entry-Point Functions */
#ifdef __cplusplus
extern "C" {
#endif
const char * ann_plugin_get_type(void);
int ann_plugin_init(ai_engine_t * engine, json_object * jconfig);

#ifdef __cplusplus
}
#endif


const char * ann_plugin_get_type(void)
{
	return AI_PLUGIN_TYPE_STRING;
}


static int ai_plugin_cvface_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults)
{
	debug_printf("%s(): frame: type=%d, size=%d x %d", __FUNCTION__,
		frame->type,
		frame->width, frame->height);
		
	int rc = -1;
	struct cv_dnn_face * face_ctx = engine->priv;
	if(NULL == face_ctx) return -1;

	bgra_image_t * bgra = NULL;
	int type = frame->type & input_frame_type_image_masks;

	if(type == 0 || type == input_frame_type_bgra) bgra = (bgra_image_t *)frame->bgra;
	else if(type == input_frame_type_png || type == input_frame_type_jpeg)
	{
		bgra = bgra_image_init(NULL, frame->width, frame->height, NULL);
		rc = bgra_image_load_data(bgra, frame->data, frame->length);
		if(rc)
		{
			bgra_image_clear(bgra);
			bgra = NULL;
		}
	}
	if(bgra)
	{
		struct face_detection * results = NULL;
		const double threshold = 0.3;
		
		app_timer_t timer[1];
		double time_elapsed = 0;
		app_timer_start(timer);
		
		cvmat_t mat = cvmat_new(bgra->width, bgra->height, bgra->data, 4);
		assert(mat);
		
		ssize_t count = face_ctx->detect(face_ctx, mat, threshold, &results, NULL, NULL);
		cvmat_free(mat);
		time_elapsed = app_timer_stop(timer);
		debug_printf("[INFO]::cvface->predict()::time_elapsed=%.3f ms", 
			time_elapsed * 1000);
		
		if(count > 0  && p_jresults)
		{
			json_object * jresults = json_object_new_object();
			json_object_object_add(jresults, "model", json_object_new_string("dnn::face::res10_300x300"));
			
			json_object * jdetections = json_object_new_array();
			json_object_object_add(jresults, "detections", jdetections);

			for(ssize_t i = 0; i < count; ++i)
			{
				json_object * jdet = json_object_new_object();
				json_object_object_add(jdet, "class", json_object_new_string("face"));
				json_object_object_add(jdet, "class_index", json_object_new_int(results[i].klass));
				json_object_object_add(jdet, "confidence", json_object_new_double(results[i].confidence));
				json_object_object_add(jdet, "left", json_object_new_double(results[i].x));
				json_object_object_add(jdet, "top", json_object_new_double(results[i].y));
				json_object_object_add(jdet, "width", json_object_new_double(results[i].cx));
				json_object_object_add(jdet, "height", json_object_new_double(results[i].cy));

				json_object_array_add(jdetections, jdet);
			}
			*p_jresults = jresults;
			rc = 0;
		}

		if(results) free(results);
		if(bgra != frame->bgra)
		{
			bgra_image_clear(bgra);
			free(bgra);
		}
	}
	return rc;
}

int ann_plugin_init(ai_engine_t * engine, json_object * jconfig)
{
	static const char * default_cfg = "models/res10_300x300_ssd-deploy.prototxt";
	static const char * default_model = "models/res10_300x300_ssd_iter_140000_fp16.caffemodel";
	static double default_threshold = 0.5;
	//~ static const char * labels_file = "conf/coco.names";
	
	if(NULL == jconfig) {
		jconfig = json_object_new_object();
		json_object_object_add(jconfig, "conf_file", json_object_new_string(default_cfg));
		json_object_object_add(jconfig, "weights_file", json_object_new_string(default_model));
		json_object_object_add(jconfig, "threshold", json_object_new_double(default_threshold));
	}
	assert(jconfig);
	
	const char *cfg_file = json_get_value(jconfig, string, conf_file);
	const char *model_file = json_get_value(jconfig, string, weights_file);
	
	struct cv_dnn_face *face_ctx = cv_dnn_face_init(NULL, cfg_file, model_file, NULL, 0, 0, engine);
	assert(face_ctx);

	engine->priv = face_ctx;
	engine->init = ann_plugin_init;
	//~ engine->cleanup = ai_plugin_darknet_cleanup;
	//~ engine->load_config = ai_plugin_darknet_load_config;
	engine->predict = ai_plugin_cvface_predict;
	//~ engine->update = ai_plugin_darknet_update;
	//~ engine->get_property = ai_plugin_darknet_get_property;
	//~ engine->set_property = ai_plugin_darknet_set_property;
	return 0;
}

#undef AI_PLUGIN_TYPE_STRING


