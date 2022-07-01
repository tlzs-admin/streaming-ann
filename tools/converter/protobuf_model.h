#ifndef PROTOBUF_MODEL_H_
#define PROTOBUF_MODEL_H_

#ifdef __cplusplus
extern "C" {
#endif

enum protobuf_model_type
{
	protobuf_model_type_opencv_caffe = 1,
	protobuf_model_type_onnx = 2,
	protobuf_model_type_onnx_caffe,
};


struct protobuf_model
{
	void * message;
	void * user_data;
	enum protobuf_model_type type;
	
	int (* load)(void * message, const char * prototxt_file, const char * model_file);
	void (* destroy)(struct protobuf_model * model);
};

struct protobuf_model * protobuf_model_new(enum protobuf_model_type type, void * user_data);
void protobuf_model_free(struct protobuf_model * model);


// utils
void dump_caffe_model(void * message);
void google_protobuf_library_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
