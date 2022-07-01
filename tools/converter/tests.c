/*
 * tests.c
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

#include <json-c/json.h>
#include "opencv-caffe_wrapper.h"

int main(int argc, char **argv)
{
	struct opencv_caffe_net net[1];
	memset(&net, 0, sizeof(net));
	
	json_object * jconfig = json_object_new_object();
	json_object_object_add(jconfig, "config", json_object_new_string("models/deploy.prototxt"));
	json_object_object_add(jconfig, "model", json_object_new_string("models/res10_300x300_ssd_iter_140000.caffemodel"));
	
	opencv_caffe_net_init(net, NULL);
	
	int rc = net->load_config(net, jconfig);
	assert(0 == rc);
	
	printf("rc: %d\n", rc);
	
	opencv_caffe_net_cleanup(net);
	json_object_put(jconfig);
	
	google_protobuf_library_shutdown();
	return 0;
}

