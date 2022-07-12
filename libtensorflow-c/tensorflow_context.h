#ifndef TENSORFLOW_CONTEXT_H_
#define TENSORFLOW_CONTEXT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "tensorflow/c/c_api.h"

#define tf_check_status(status) ({ \
		TF_Code code = TF_GetCode(status); \
		if(code != TF_OK) { \
			fprintf(stderr, "[ERROR]: %s@%d::%s(): code=%d\n", __FILE__, __LINE__, __FUNCTION__, code); \
		}; \
		code; \
	})


typedef void (* tensor_deallocator_fn)(void * data, size_t len, void * arg);
struct tensor_data
{
	TF_Tensor * tensor;
	enum TF_DataType type;
	int num_dims;
	int64_t *dims;
	void * data;
	size_t data_len;
	tensor_deallocator_fn free_data;
	void * arg;
};
struct tensor_data * tensor_data_set(struct tensor_data * tdata, 
	enum TF_DataType type, int64_t * _dims, int num_dims, 
	void * data, size_t data_len,
	tensor_deallocator_fn free_data,
	void * arg
	);
void tensor_data_clear(struct tensor_data * tdata);

struct tf_tensors
{
	size_t num_tensors;
	struct tensor_data *data;
};
struct tf_tensors * tf_tensors_init(struct tf_tensors * tensors, size_t num_tensors);
void tf_tensors_cleanup(struct tf_tensors * tensors);
int tf_tensors_set_data(struct tf_tensors * tensors, int index, 
	enum TF_DataType type, int64_t * _dims, int num_dims, 
	void * data, size_t data_len,
	tensor_deallocator_fn free_data,
	void * arg);



struct tensorflow_private;
struct tensorflow_context
{
	struct tensorflow_private * priv;
	void * user_data;
	
	int (* load_model)(struct tensorflow_context *tf, const char  *model_file, const char *checkpoint_prefix);
	int (* set_input_by_names)(struct tensorflow_context *tf, int num_inputs, const char **input_names);
	int (* set_output_by_names)(struct tensorflow_context *tf, int num_outputs, const char **output_names);
	int (* session_run)(struct tensorflow_context *tf, const struct tf_tensors * _inputs, struct tf_tensors * _outputs);
	
	const struct tf_tensor * (*get_outputs)(struct tensorflow_context *tf);
};
struct tensorflow_context * tensorflow_context_init(struct tensorflow_context * tf, void * user_data);
void tensorflow_context_cleanup(struct tensorflow_context * tf);


#ifdef __cplusplus
}
#endif
#endif
