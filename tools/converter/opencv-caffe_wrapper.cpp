/*
 * opencv-caffe-parser.cpp
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

#include <fstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>

#include "opencv-caffe_wrapper.h"
#include "opencv/opencv-caffe.pb.h"

#ifndef debug_printf
#include <stdarg.h>
#define debug_printf(fmt, ...) do { fprintf(stderr, "\e[33m" fmt "\e[39m" "\n", ##__VA_ARGS__); } while(0)
#endif

extern "C" {
static int opencv_caffe_net_load_config(struct opencv_caffe_net * net, json_object * jconfig);
static int opencv_caffe_net_parse_prototxt(struct opencv_caffe_net * net, const char * prototxt_file);
static int opencv_caffe_net_load_model(struct opencv_caffe_net * net, const char * model_file);
}

struct opencv_caffe_net * opencv_caffe_net_init(struct opencv_caffe_net *net, void * user_data)
{
	if(NULL == net) {
		net = (struct opencv_caffe_net *)calloc(1, sizeof(*net));
		assert(net);
	}
	net->user_data = user_data;
	net->load_config = opencv_caffe_net_load_config;
	net->parse_prototxt = opencv_caffe_net_parse_prototxt;
	
	
	google::protobuf::TextFormat::Parser parser;
	net->net_params = new opencv_caffe::NetParameter();
	assert(net->net_params);
	
	return net;
	
	
}
void opencv_caffe_net_cleanup(struct opencv_caffe_net * net)
{
	if(net->net_params) {
		delete((opencv_caffe::NetParameter *)net->net_params);
		net->net_params = NULL;
	}
	return;
}


static int opencv_caffe_net_parse_prototxt(struct opencv_caffe_net * net, const char * prototxt_file)
{
	debug_printf("%s('%s')...\n", __FUNCTION__, prototxt_file);
	opencv_caffe::NetParameter * message = (opencv_caffe::NetParameter *)net->net_params;
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

static int opencv_caffe_net_load_model(struct opencv_caffe_net * net, const char * model_file)
{
	debug_printf("%s('%s')...\n", __FUNCTION__, model_file);
	opencv_caffe::NetParameter * message = (opencv_caffe::NetParameter *)net->net_params;
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

static int opencv_caffe_net_load_config(struct opencv_caffe_net * net, json_object * jconfig)
{
	int rc = -1;
	const char * prototxt_file = NULL;
	json_object * jprototxt_file = NULL;
	
	json_bool ok = json_object_object_get_ex(jconfig, "config", &jprototxt_file);
	assert(ok && jprototxt_file);
	prototxt_file = json_object_get_string(jprototxt_file);
	
	if(prototxt_file) {
		rc = opencv_caffe_net_parse_prototxt(net, prototxt_file);
	} 
	
	if(rc) return rc;
	
	
	json_object * jmodel_file = NULL;
	ok = json_object_object_get_ex(jconfig, "model", &jmodel_file);
	
	if(ok && jmodel_file) {
		// todo: ///
		const char * model_file = json_object_get_string(jmodel_file);
		return opencv_caffe_net_load_model(net, model_file);
	}
	
	return rc;
}


void google_protobuf_library_shutdown(void)
{
	google::protobuf::ShutdownProtobufLibrary();
}
