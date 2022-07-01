/*
 * protobuf_model.cpp
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

#include "protobuf_model.h"

#include <fstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>


#include "opencv/opencv-caffe.pb.h"
#include "onnx/onnx.pb.h"

#ifndef debug_printf
#include <stdarg.h>
#define debug_printf(fmt, ...) do { fprintf(stderr, "\e[33m" fmt "\e[39m" "\n", ##__VA_ARGS__); } while(0)
#endif


void google_protobuf_library_shutdown(void)
{
	google::protobuf::ShutdownProtobufLibrary();
}


static inline int parse_prototxt(google::protobuf::Message * message, const char * prototxt_file)
{
	debug_printf("%s('%s')...\n", __FUNCTION__, prototxt_file);
	
	std::ifstream fs(prototxt_file, std::ifstream::in);
	if(!fs.is_open()) {
		fprintf(stderr, "[ERROR]: open file '%s' failed.", prototxt_file);
		return -1;
	}
	google::protobuf::io::IstreamInputStream input(&fs);
	google::protobuf::TextFormat::Parser parser;
	
	int ok = parser.Parse(&input, message);
	return ok?0:-1;
}

static inline int protobuf_model_load(void * _message, const char * prototxt_file, const char * model_file)
{
	debug_printf("%s('%s')...\n", __FUNCTION__, model_file);
	
	int rc = -1;
	google::protobuf::Message * message = (google::protobuf::Message *)_message;
	assert(message);
	
	if(prototxt_file && prototxt_file[0]) {
		rc = parse_prototxt(message, prototxt_file);
		if(rc) return -1;
	}
	
	std::ifstream fs(model_file, std::ifstream::in | std::ifstream::binary);
	if(!fs.is_open()) {
		fprintf(stderr, "[ERROR]: open file '%s' failed.", model_file);
		return -1;
	}
	google::protobuf::io::IstreamInputStream input(&fs);
	google::protobuf::io::CodedInputStream coded_input(&input);
	
	int ok = message->ParseFromCodedStream(&coded_input);
	return ok?0:-1;
}

/****************************
 * opencv_caffe_model impl.
****************************/
static void opencv_caffe_model_destroy(struct protobuf_model * model)
{
	if(model && model->message) {
		delete (opencv_caffe::NetParameter *)model->message;
		model->message = NULL;
	}
	free(model);
}

void dump_caffe_model(void * _message)
{
	opencv_caffe::NetParameter * net = (opencv_caffe::NetParameter *)_message;
	
	int size = net->layer_size();
	printf("layer_size: %d\n", size);
	for(int i = 0; i < size; ++i) {
		const opencv_caffe::LayerParameter & layer = net->layer(i);
		printf("layer[%d]: name=%s\n", i, layer.name().c_str());
		
	}
	
}
/****************************
 * onnx_model impl.
****************************/
static void onnx_model_destroy(struct protobuf_model * model)
{
	if(model && model->message) {
		delete (onnx::ModelProto *)model->message;
		model->message = NULL;
	}
	free(model);
}



/****************************
 * protobuf_model
****************************/
struct protobuf_model * protobuf_model_new(enum protobuf_model_type type, void * user_data)
{
	struct protobuf_model * model = (struct protobuf_model *)calloc(1, sizeof(*model));
	assert(model);
	
	model->user_data = user_data;
	model->type = type;
	model->load = protobuf_model_load;
	
	switch(type) 
	{
	case protobuf_model_type_opencv_caffe: 
		model->message = new opencv_caffe::NetParameter;
		model->destroy = opencv_caffe_model_destroy;
		break;
	case protobuf_model_type_onnx:
		model->message = new onnx::ModelProto;
		model->destroy = onnx_model_destroy;
		break;
	default:
		break;
	}
	
	assert(model->message);
	return model;
}
