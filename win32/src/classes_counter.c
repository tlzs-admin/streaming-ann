/*
 * classes_counter.c
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

#include "classes_counter.h"

static struct class_counter * classes_counter_add_by_name(struct classes_counter_context* counters, const char * class_name);
static struct class_counter * classes_counter_add_by_id(struct classes_counter_context *counters, int class_id);
static void classes_counter_reset(struct classes_counter_context * counters);
static void classes_counter_clear_all(struct classes_counter_context * counters);
struct classes_counter_context * classes_counter_context_init(struct classes_counter_context * counters, void * user_data)
{
	if(NULL == counters) counters = malloc(sizeof(*counters));
	assert(counters);
	memset(counters, 0, sizeof(*counters));
	counters->user_data = user_data;
	
	counters->add_by_name = classes_counter_add_by_name;
	counters->add_by_id = classes_counter_add_by_id;
	counters->reset = classes_counter_reset;
	counters->clear_all = classes_counter_clear_all;
	
	return counters;
}
void classes_counter_context_cleanup(struct classes_counter_context * counters)
{
	if(counters) classes_counter_clear_all(counters);
}


static struct class_counter * classes_counter_add_by_name(struct classes_counter_context* counters, const char * class_name)
{
	struct class_counter * class = NULL;
	if(NULL == class_name) return NULL;
	
	for(int i = 0; i < counters->num_classes; ++i) {
		class = &counters->classes[i];
		
		const char * p_sep = strchr(class_name, ',');
		int compare_length = p_sep?(p_sep - class_name):strlen(class_name);
		
		if(strncasecmp(class->name, class_name, compare_length) == 0) {
			++class->count;
			return class;
		}
	}

	if(counters->num_classes >= CLASSES_COUNTER_MAX_CLASSES) return NULL;
	
	class = &counters->classes[counters->num_classes++];
	strncpy(class->name, class_name, sizeof(class->name));
	class->id = -1;
	class->count = 1;
	
	return class;
}
static struct class_counter *  classes_counter_add_by_id(struct classes_counter_context *counters, int class_id)
{
	struct class_counter * class = NULL;
	if(class_id < 0 || class_id > CLASSES_COUNTER_MAX_CLASSES) return NULL;
	for(int i = 0; i < counters->num_classes; ++i) {
		class = &counters->classes[i];
		if(class->id == class_id) {
			++class->count;
			return class;
		}
	}
	if(counters->num_classes >= CLASSES_COUNTER_MAX_CLASSES) return NULL;
	
	class = &counters->classes[counters->num_classes++];
	class->id = class_id;
	class->count = 1;
	return class;
}
static void classes_counter_reset(struct classes_counter_context * counters)
{
	for(int i = 0; i < counters->num_classes; ++i) counters->classes[i].count = 0;
}
static void classes_counter_clear_all(struct classes_counter_context * counters)
{
	memset(counters->classes, 0, sizeof(*counters->classes));
	counters->num_classes = 0;
}



