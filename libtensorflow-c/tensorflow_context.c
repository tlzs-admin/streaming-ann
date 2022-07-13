/*
 * tensorflow_context.c
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

/***************************************************************
 * Reference:  https://github.com/Neargye/hello_tf_c_api.git
***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <stdint.h>

#include "tensorflow_context.h"
#include "tensorflow/c/c_api.h"

/*************************************************
 * utils
*************************************************/
#include <sys/stat.h>
static ssize_t load_binary_data(const char * filename, unsigned char **p_dst)
{
	struct stat st[1];
	int rc;
	rc = stat(filename, st);
	if(rc)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::stat::%s\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			strerror(rc));
		return -1;
	}
	
	if(!S_ISREG(st->st_mode) )
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::not regular file!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename);
		return -1;
	}
	
	ssize_t size = st->st_size;
	if(size <= 0)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::invalid file-length: %ld!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			(long)size
		);
		return -1;
	}
	if(NULL == p_dst) return (size + 1);		// return buffer size	( append '\0' for ptx file)
	
	FILE * fp = fopen(filename, "rb");
	assert(fp);
	
	unsigned char * data = *p_dst;
	*p_dst = realloc(data, size + 1);
	assert(*p_dst);
	
	data = *p_dst;
	ssize_t length = fread(data, 1, size, fp);
	fclose(fp);
	
	assert(length == size);	
	data[length] = '\0';
	return length;
}


/*************************************************
 * tensor_data
*************************************************/
static void no_free_data(void * data, size_t len, void * arg)
{
	return;
}
struct tensor_data * tensor_data_set(struct tensor_data * tdata,
	enum TF_DataType type, 
	int64_t * _dims, int num_dims, 
	void * data, size_t data_len,
	tensor_deallocator_fn free_data,
	void * arg
	)
{
	assert(_dims && num_dims > 0);
	
	if(NULL == tdata) {
		tdata = calloc(1, sizeof(*tdata));
		assert(tdata);
	}else if(tdata->tensor)
	{
		tensor_data_clear(tdata);
	}
	
	tdata->free_data = free_data;
	tdata->arg = arg;
	if(NULL == free_data) free_data = no_free_data;
	
	int64_t * dims = NULL;
	if(num_dims > 0) dims = calloc(num_dims, sizeof(*dims));
	assert(dims);
	for(int i = 0; i < num_dims; ++i) dims[i] = _dims[i];
	
	TF_Tensor * tensor = TF_NewTensor(type, dims, num_dims, data, data_len, free_data, arg);
	assert(tensor);
	
	tdata->tensor = tensor;
	tdata->type = type;
	tdata->num_dims = num_dims;
	tdata->dims = dims;
	tdata->data = data;
	tdata->data_len = data_len;
	return tdata;
}

void tensor_data_clear(struct tensor_data * tdata)
{
	if(NULL == tdata) return;
	if(tdata->tensor) TF_DeleteTensor(tdata->tensor);
	
	if(tdata->dims) {
		free(tdata->dims);
		tdata->dims = NULL;
	}
	memset(tdata, 0, sizeof(*tdata));
}

/*************************************************
 * struct tf_tensors
*************************************************/
int tf_tensors_set_data(struct tf_tensors * tensors, int index, 
	enum TF_DataType type, 
	int64_t * _dims, int num_dims, 
	void * data, size_t data_len,
	tensor_deallocator_fn free_data,
	void * arg)
{
	if(NULL == tensors || index < 0 || index >= tensors->num_tensors) return -1;
	
	struct tensor_data * tdata = &tensors->data[index];
	tensor_data_set(tdata, type, _dims, num_dims, data, data_len, free_data, arg);
	
	if(NULL == tdata->tensor) return -1;
	return 0;
}

struct tf_tensors * tf_tensors_init(struct tf_tensors * tensors, size_t num_tensors)
{
	if(num_tensors == 0) num_tensors = 1;
	if(NULL == tensors) {
		tensors = calloc(1, sizeof(*tensors));
		assert(tensors);
	}
	
	tensors->data = calloc(num_tensors, sizeof(*tensors->data));
	tensors->num_tensors = num_tensors;
	
	return tensors;
}
void tf_tensors_cleanup(struct tf_tensors * tensors)
{
	if(NULL == tensors) return;
	for(size_t i = 0; i < tensors->num_tensors; ++i) {
		tensor_data_clear(&tensors->data[i]);
	}
	free(tensors->data);
	tensors->data = NULL;
	tensors->num_tensors = 0;
	return;
}



/*************************************************
 * struct tensorflow_context
*************************************************/
struct tensorflow_private
{
	struct tensorflow_context * tf;
	TF_Session * session;
	TF_Graph * graph;
	TF_Status * status;
	
	int num_inputs;
	TF_Output * inputs;
	TF_Tensor ** input_tensors;
	
	int num_outputs;
	TF_Output * outputs;
	TF_Tensor ** output_tensors;
};

static struct tensorflow_private * tensorflow_private_new(struct tensorflow_context * tf)
{
	struct tensorflow_private * priv = calloc(1, sizeof(*priv));
	assert(priv);
	
	priv->tf = tf;
	return priv;
}
static void tensorflow_private_free(struct tensorflow_private * priv)
{
	if(NULL == priv) return;
	TF_Status * status = priv->status;
	priv->status = NULL;
	
	if(priv->session) {
		TF_CloseSession(priv->session, status);
		TF_DeleteSession(priv->session, status);
		tf_check_status(status);
		priv->session = NULL;
		TF_DeleteStatus(status);
	}
	
	if(priv->graph) {
		TF_DeleteGraph(priv->graph);
		priv->graph = NULL;
	}
	
	if(priv->input_tensors) {
		for(int i = 0; i < priv->num_inputs; ++i) {
			if(NULL == priv->input_tensors[i]) continue;
			TF_DeleteTensor(priv->input_tensors[i]);
			priv->input_tensors[i] = NULL;
		}
		free(priv->input_tensors);
		priv->input_tensors = NULL;
	}
	free(priv->inputs);
	priv->num_inputs = 0;
	
	if(priv->output_tensors) {
		for(int i = 0; i < priv->num_outputs; ++i) {
			if(NULL == priv->output_tensors[i]) continue;
			TF_DeleteTensor(priv->output_tensors[i]);
			priv->output_tensors[i] = NULL;
		}
		free(priv->output_tensors);
		priv->output_tensors = NULL;
	}
	free(priv->outputs);
	priv->num_outputs = 0;
}


#define AUTO_DELETE_STATUS __attribute__((cleanup(auto_delete_status_ptr)))
static void auto_delete_status_ptr(void * ptr)
{
	TF_Status * status = *(TF_Status **)ptr;
	if(status) {
		TF_DeleteStatus(status);
		*(TF_Status **)ptr = NULL;
	}
}

static inline int get_shape(TF_Graph * graph, TF_Output output, struct tensor_shape * p_shape)
{
	AUTO_DELETE_STATUS TF_Status * status = TF_NewStatus();
	int num_dims = TF_GraphGetTensorNumDims(graph, output, status);
	TF_Code code = tf_check_status(status);
	
	if(code != TF_OK) return code;
	
	if(num_dims <= 0) {
		fprintf(stderr, "[ERROR]::%s():invalid num_dims(%d).\n", __FUNCTION__, num_dims);
		return -1;
	}
	
	int64_t * dims = calloc(num_dims, sizeof(*dims));
	assert(dims);
	TF_GraphGetTensorShape(graph, output, dims, num_dims, status);
	code = tf_check_status(status);
	
	if(code != TF_OK) {
		free(dims);
		return code;
	}
	
	tensor_shape_clear(p_shape);
	p_shape->num_dims = num_dims;
	p_shape->dims = dims;
	return code;
}

static int get_shape_by_name(struct tensorflow_context * tf, const char * _name, struct tensor_shape * p_shape)
{
	struct tensorflow_private * priv = tf->priv;
	assert(priv && priv->graph && _name);
	
	int index = 0;
	char * oper_name = strdup(_name);
	char * p_sep = strrchr(oper_name, ':');
	if(p_sep) {
		*p_sep++ = '\0';
		index = atoi(p_sep);
	}
	
	TF_Output output = { NULL };
	output.oper = TF_GraphOperationByName(priv->graph, oper_name);
	output.index = index;
	
	return get_shape(priv->graph, output, p_shape);
}
static int get_input_shape(struct tensorflow_context * tf, int index, struct tensor_shape * p_shape)
{
	struct tensorflow_private * priv = tf->priv;
	assert(priv && priv->graph && priv->num_inputs);
	if(index < 0 || index >= priv->num_inputs) return -1;
	
	TF_Output * inputs = priv->inputs;
	assert(inputs);
	
	return get_shape(priv->graph, inputs[index], p_shape);
}
static int get_output_shape(struct tensorflow_context * tf, int index, struct tensor_shape * p_shape)
{
	struct tensorflow_private * priv = tf->priv;
	assert(priv && priv->graph && priv->num_outputs);
	if(index < 0 || index >= priv->num_outputs) return -1;
	
	TF_Output * outputs = priv->outputs;
	assert(outputs);
	
	return get_shape(priv->graph, outputs[index], p_shape);
}


static int set_input_by_names(struct tensorflow_context * tf, int num_inputs, const char ** input_names)
{
	assert(num_inputs > 0 && input_names);
	
	struct tensorflow_private * priv = tf->priv;
	assert(priv && priv->graph);
	TF_Graph * graph = priv->graph;
	
	TF_Output * inputs = calloc(num_inputs, sizeof(*inputs));
	for(int i = 0; i < num_inputs; ++i) {
		assert(input_names[i]);
		inputs[i].oper = TF_GraphOperationByName(graph, input_names[i]);
		if(NULL == inputs[i].oper) {
			fprintf(stderr, "[ERROR]::%s():invalid input_name '%s'\n", __FUNCTION__, input_names[i]);
			free(inputs);
			return -1;
		}
		inputs[i].index = i;
	}
	if(priv->inputs) free(priv->inputs);
	priv->inputs = inputs;
	priv->num_inputs = num_inputs;
	priv->input_tensors = calloc(num_inputs, sizeof(*priv->input_tensors));
	return 0;
}

static int set_ouput_by_names(struct tensorflow_context * tf, int num_outputs, const char ** output_names)
{
	assert(num_outputs > 0 && output_names);
	struct tensorflow_private * priv = tf->priv;
	assert(priv && priv->graph);
	TF_Graph * graph = priv->graph;
	
	TF_Output * outputs = calloc(num_outputs, sizeof(*outputs));
	assert(outputs);
	
	for(int i = 0; i < num_outputs; ++i) {
		assert(output_names[i]);
		outputs[i].oper = TF_GraphOperationByName(graph, output_names[i]);
		if(NULL == outputs[i].oper) {
			fprintf(stderr, "[ERROR]::%s():invalid output_name '%s'\n", __FUNCTION__, output_names[i]);
			free(outputs);
			return -1;
		}
		outputs[i].index = i;
	}
	if(priv->outputs) free(priv->outputs);
	priv->outputs = outputs;
	priv->num_outputs = num_outputs;
	priv->output_tensors = calloc(num_outputs, sizeof(*priv->output_tensors));
	return 0;
}


static int tensorflow_load_model_data(struct tensorflow_context * tf, const void * model_data, size_t cb_model_data)
{
	TF_Code code = -1;
	TF_Buffer * buffer = TF_NewBuffer();
	
	buffer->data = (void *)model_data;
	buffer->length = cb_model_data;
	
	TF_Status * status = TF_NewStatus();
	TF_Graph * graph = TF_NewGraph();
	TF_ImportGraphDefOptions * import_options = TF_NewImportGraphDefOptions();
	assert(status && graph && import_options);
	
	TF_GraphImportGraphDef(graph, buffer, import_options, status);
	TF_DeleteBuffer(buffer);
	
	code = tf_check_status(status);
	TF_DeleteImportGraphDefOptions(import_options);
	
	if(code != TF_OK) {
		TF_DeleteGraph(graph);
		TF_DeleteStatus(status);
		return code;
	}
	
	struct tensorflow_private * priv = tf->priv;
	assert(priv);
	
	if(priv->status) TF_DeleteStatus(priv->status);
	priv->status = status;
	priv->graph = graph;
	
	TF_SessionOptions * options = TF_NewSessionOptions();
	TF_Session * session = TF_NewSession(priv->graph, options, status);
	TF_DeleteSessionOptions(options);
	
	code = tf_check_status(priv->status);
	if(code != TF_OK) {
		if(session) TF_DeleteSession(session, priv->status);
		tf_check_status(priv->status);
		return code;
	}
	priv->session = session;
	return code;
}

static int tensorflow_load_checkpoints(struct tensorflow_context * tf, const char * checkpoint_prefix)
{
	///< @todo ...
	return -1;
}

static int tensorflow_load_model(struct tensorflow_context * tf, const char * model_file, const char * checkpoint_prefix)
{
	struct tensorflow_private * priv = tf->priv;
	
	unsigned char * model_data = NULL;
	ssize_t cb_model_data = load_binary_data(model_file, &model_data);
	TF_Code code = -1;
	if(cb_model_data > 0) {
		code = tensorflow_load_model_data(tf, model_data, cb_model_data);
		free(model_data);
		if(code != TF_OK) return code;
	}
	
	if(checkpoint_prefix && priv->session) {
		code = tensorflow_load_checkpoints(tf, checkpoint_prefix);
	}
	return code;
}


static int session_run(struct tensorflow_context * tf, const struct tf_tensors * in_tensors, struct tf_tensors * out_tensors)
{
	struct tensorflow_private * priv = tf->priv;
	assert(priv && priv->session);

	TF_Status * status = priv->status;
	TF_Code code = TF_OK;
	
	TF_Session * session = priv->session;
	
	TF_Output * inputs = priv->inputs;
	TF_Output * outputs = priv->outputs;
	int num_inputs = priv->num_inputs;
	int num_outputs = priv->num_outputs;
	
	assert(inputs && outputs);
	assert(num_inputs > 0 && num_outputs > 0);
	
	TF_Tensor ** input_tensors = priv->input_tensors;
	for(size_t i = 0; i < in_tensors->num_tensors; ++i) { 
		// move tensor pointer to priv->input_tensors
		input_tensors[i] = in_tensors->data[i].tensor;
		in_tensors->data[i].tensor = NULL;
	}

	TF_Tensor ** output_tensors = priv->output_tensors;
	assert(output_tensors);
	
	TF_Buffer * run_opts = TF_NewBuffer();
	TF_SessionRun(session, run_opts, 
		inputs, input_tensors, num_inputs, 
		outputs, output_tensors, num_outputs, 
		NULL, 0, // target operations
		NULL, // run metadata
		status);
	TF_DeleteBuffer(run_opts);
	code = tf_check_status(status);
	
	if(code == TF_OK && out_tensors) {
		tf_tensors_cleanup(out_tensors);
		tf_tensors_init(out_tensors, num_outputs);
		for(int i = 0; i < num_outputs; ++i) {
			TF_Tensor * tensor = output_tensors[i];
			assert(tensor);
			struct tensor_data * tdata = &out_tensors->data[i];
			tdata->type = TF_TensorType(tensor);
		//	tdata->tensor = tensor;
			tdata->num_dims = TF_NumDims(tensor);
			tdata->dims = calloc(tdata->num_dims, sizeof(*tdata->dims));
			for(int ii = 0; ii < tdata->num_dims; ++ii) tdata->dims[ii] = TF_Dim(tensor, ii);
			tdata->data = TF_TensorData(tensor);
			tdata->data_len = TF_TensorByteSize(tensor);
		}
	}
	return code;
}

struct tensorflow_context * tensorflow_context_init(struct tensorflow_context * tf, void * user_data)
{
	if(NULL == tf) tf = calloc(1, sizeof(tf));
	assert(tf);

	tf->priv = tensorflow_private_new(tf);
	
	tf->load_model = tensorflow_load_model;
	tf->set_input_by_names = set_input_by_names;
	tf->set_output_by_names = set_ouput_by_names;
	tf->session_run = session_run;
	
	tf->get_input_shape = get_input_shape;
	tf->get_output_shape = get_output_shape;
	tf->get_shape_by_name = get_shape_by_name;

	return tf;
}
void tensorflow_context_cleanup(struct tensorflow_context * tf)
{
	tensorflow_private_free(tf->priv);
}

#if defined(_TEST_TENSORFLOW_CONTEXT) && defined(_STAND_ALONE)

/***************************************************************
 * Reference:  https://github.com/Neargye/hello_tf_c_api.git
***************************************************************/

//~ TF_CAPI_EXPORT void TF_InitMain(const char* usage, int* argc, char*** argv);

#include <getopt.h>

struct global_params
{
	const char * model_file;
	const char * input_name;
	const char * output_name;
};
static int parse_args(struct global_params * params, int argc, char ** argv)
{
	static struct option options[] = {
		{"model", required_argument, 0, 'm'},
		{"input_name", required_argument, 0, 'I'},
		{"output_name", required_argument, 0, 'O'},
		{NULL},
	};
	
	while(1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "m:I:O:", options, &option_index);
		if(c == -1) break;
		
		switch(c) {
		case 'm': params->model_file = optarg; break;
		case 'I': params->input_name = optarg; break;
		case 'O': params->output_name = optarg; break;
		default: 
			break;
		}
	}
	return 0;
}

static void tensor_shape_dump(const struct tensor_shape * shape, const char * title)
{
	assert(shape);
	printf("== [%s]: shape(num_dims=%d): [ ", title, shape->num_dims);
	for(int i = 0; i < shape->num_dims; ++i) {
		if(i > 0) printf(", ");
		printf("%ld", (long)shape->dims[i]);
	}
	printf(" ]\n");
}


static struct global_params g_params[1] = {{
	.model_file = "models/graph.pb",
	.input_name = "input_4",
	.output_name = "output_node0",
}};
int main(int argc, char **argv)
{
	//~ TF_InitMain(argv[0], &argc, &argv);

	printf("TF_Version: %s\n", TF_Version());
	
	struct global_params * params = g_params;
	parse_args(params, argc, argv);

	struct tensorflow_context tf[1];
	memset(tf, 0, sizeof(tf));
	tensorflow_context_init(tf, NULL);
	
	int rc = tf->load_model(tf, params->model_file, NULL);
	assert(0 == rc);
	
	const char * input_names[] = { params->input_name };
	const char * output_names[] = { params->output_name };
	
	rc = tf->set_input_by_names(tf, 1, input_names);
	assert(0 == rc);
	
	rc = tf->set_output_by_names(tf, 1, output_names);
	assert(0 == rc);
	
	struct tensor_shape input_shape = { 0 };
	struct tensor_shape output_shape = { 0 };
	
	rc = tf->get_input_shape(tf, 0, &input_shape);
	assert(0 == rc);
	tensor_shape_dump(&input_shape, "input");
	
	rc = tf->get_output_shape(tf, 0, &output_shape);
	assert(0 == rc);
	tensor_shape_dump(&output_shape, "output");
	
	static float input_vals[] =  {
		-0.4809832f, -0.3770838f, 0.1743573f, 0.7720509f, -0.4064746f, 0.0116595f, 0.0051413f, 0.9135732f, 0.7197526f, -0.0400658f, 0.1180671f, -0.6829428f,
		-0.4810135f, -0.3772099f, 0.1745346f, 0.7719303f, -0.4066443f, 0.0114614f, 0.0051195f, 0.9135003f, 0.7196983f, -0.0400035f, 0.1178188f, -0.6830465f,
		-0.4809143f, -0.3773398f, 0.1746384f, 0.7719052f, -0.4067171f, 0.0111654f, 0.0054433f, 0.9134697f, 0.7192584f, -0.0399981f, 0.1177435f, -0.6835230f,
		-0.4808300f, -0.3774327f, 0.1748246f, 0.7718700f, -0.4070232f, 0.0109549f, 0.0059128f, 0.9133330f, 0.7188759f, -0.0398740f, 0.1181437f, -0.6838635f,
		-0.4807833f, -0.3775733f, 0.1748378f, 0.7718275f, -0.4073670f, 0.0107582f, 0.0062978f, 0.9131795f, 0.7187147f, -0.0394935f, 0.1184392f, -0.6840039f,
	};
	int num_dims = 3;
	int64_t dims[3] = {
		[0] = 1, 
		[1] = 5, 
		[2] = 12
	};
	
	struct tf_tensors in_tensors = { 0 };
	tf_tensors_init(&in_tensors, 1);
	tf_tensors_set_data(&in_tensors, 0, 
		TF_FLOAT, dims, num_dims, input_vals, sizeof(input_vals), no_free_data, NULL);
	
	struct tf_tensors out_tensors = { .num_tensors = 0 };
	tf->session_run(tf, &in_tensors, &out_tensors);

	printf("num outputs: %ld\n", (long)out_tensors.num_tensors);
	for(int i = 0; i < out_tensors.num_tensors; ++i) {
		struct tensor_data * tdata = &out_tensors.data[i];
		printf("tensor[%d]: num_dims = %d, data_len = %ld\n", 
			i, tdata->num_dims, (long)tdata->data_len);
			
			
			// output.dims = {1, 4 };
			assert(tdata->type == TF_FLOAT);
			const float * out_values = tdata->data;
			printf("out_values: [ ");
			for(int pos = 0; pos < (tdata->data_len / sizeof(float) ); ++pos) {
				printf("%f ", out_values[pos]);
			}
			printf("]\n");
			
		
	}
	
	tf_tensors_cleanup(&in_tensors);
	tf_tensors_cleanup(&out_tensors);
	
	tensorflow_context_cleanup(tf);
	return 0;
}
#endif
