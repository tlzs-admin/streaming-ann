#ifndef OPENCV_CAFFE_WRAPPER_H_
#define OPENCV_CAFFE_WRAPPER_H_

#ifdef __cplusplus 
extern "C" {
#endif

#include <json-c/json.h>


struct opencv_caffe_net
{
	void * net_params;
	void * user_data;
	int (* load_config)(struct opencv_caffe_net *net, json_object * jconfig);	// .proto or prototxt file
	int (* parse_prototxt)(struct opencv_caffe_net * net, const char * prototxt_file);
};

struct opencv_caffe_net * opencv_caffe_net_init(struct opencv_caffe_net * net, void * user_data);
void opencv_caffe_net_cleanup(struct opencv_caffe_net * net);


// utils
void google_protobuf_library_shutdown(void);


#ifdef __cplusplus 
}
#endif
#endif
