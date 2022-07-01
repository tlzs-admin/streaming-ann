/*
 * test1.c
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



int main(int argc, char **argv)
{
	const char * caffe_prototxt_file = "models/deploy.prototxt";
	const char * caffe_model_file = "models/res10_300x300_ssd_iter_140000.caffemodel";
	const char * onnx_model_file = "models/googelnet-9.onnx";

	
	int rc = 0;
	struct protobuf_model * caffe = protobuf_model_new(protobuf_model_type_opencv_caffe, NULL);
	struct protobuf_model * onnx = protobuf_model_new(protobuf_model_type_onnx, NULL);
	assert(caffe && onnx); 
	
	printf("load caffe model: (%s:%s)...\n", caffe_prototxt_file, caffe_model_file);
	rc = caffe->load(caffe->message, caffe_prototxt_file, caffe_model_file);
	printf("  ==> rc = %d\n", rc);
	
	if(0 == rc) dump_caffe_model(caffe->message);
	
	
	
	printf("load onnx model: (%s:%s)...\n", "", onnx_model_file);
	rc = onnx->load(onnx->message, NULL, onnx_model_file);
	printf("  ==> rc = %d\n", rc);
	
	caffe->destroy(caffe);
	onnx->destroy(onnx);
	
	google_protobuf_library_shutdown();
	
	
	return 0;
}

