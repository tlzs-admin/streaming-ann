#ifndef DEMO_CLASSES_COUNTER_H_
#define DEMO_CLASSES_COUNTER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CLASSES_COUNTER_MAX_CLASSES 	(80)
#define CLASSES_COUNTER_MAX_NAME_LEN 	(100)
struct class_counter
{
	int id;
	char name[CLASSES_COUNTER_MAX_NAME_LEN];
	long count;
};

struct classes_counter_context
{
	void * user_data;
	ssize_t num_classes;
	struct class_counter classes[CLASSES_COUNTER_MAX_CLASSES];
	
	struct class_counter * (*add_by_name)(struct classes_counter_context* counters, const char * class_name);
	struct class_counter * (*add_by_id)(struct classes_counter_context *counters, int class_id);
	void (*reset)(struct classes_counter_context * counters);		// reset all classes[i].count to 0
	void (*clear_all)(struct classes_counter_context * counters);	// remove all classes
};
struct classes_counter_context * classes_counter_context_init(struct classes_counter_context * counters, void * user_data);
void classes_counter_context_cleanup(struct classes_counter_context * counters);

#ifdef __cplusplus
}
#endif
#endif
