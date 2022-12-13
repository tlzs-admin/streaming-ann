/*
 * jobs_context.c
 * 
 * Copyright 2022 Che Hongwei <htc.chehw@gmail.com>
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
#include <pthread.h>
#include "jobs_context.h"

#include "video_source_common.h"
#include "shell.h"
#include "da_panel.h"
 
struct job_data
{
	void *data;
	long refs;
	void (*free_data)(void *data);
};
struct job_data *job_data_new(void (*free_data)(void *), void *data)
{
	struct job_data *job = calloc(1, sizeof(*job));
	assert(job);
	
	job->data = data;
	job->refs = 1;
	job->free_data = free_data;
	return job;
}
void job_data_unref(struct job_data *job)
{
	if(NULL == job || job->refs == 0) return;
	if(0 == --job->refs) {
		if(job->free_data) job->free_data(job->data);
		free(job);
	}
	return;
}

static int jobs_queue_resize(struct jobs_queue *queue, size_t new_size)
{
	static const size_t ALLOC_SIZE = 256;
	if(0 == new_size || new_size == -1) new_size = ALLOC_SIZE;
	else {
		new_size = (new_size + ALLOC_SIZE - 1) / ALLOC_SIZE * ALLOC_SIZE;
	}
	if(new_size <= queue->max_size) return 0;
	
	struct job_data ** jobs = realloc(queue->jobs, sizeof(*jobs) * new_size);
	assert(jobs);
	memset(jobs + queue->max_size, 0, sizeof(*jobs) * (new_size - queue->max_size));
	queue->jobs = jobs;
	queue->max_size = new_size;
	return 0;
}


int jobs_queue_enter(struct jobs_queue *queue, struct job_data *job)
{
	int rc = 0;
	jobs_queue_lock(queue);
	rc = jobs_queue_resize(queue, queue->length + 1);
	assert(0 == rc);
	
	queue->jobs[queue->length++] = job;
	jobs_queue_unlock(queue);
	return rc;
}
struct job_data *jobs_queue_leave(struct jobs_queue *queue)
{
	struct job_data *job = NULL;
	jobs_queue_lock(queue);
	if(queue->jobs && queue->length > 0) {
		job = queue->jobs[--queue->length];
		queue->jobs[queue->length] = NULL;
	}
	jobs_queue_unlock(queue);
	return job;
}
size_t jobs_queue_get_length(struct jobs_queue *queue)
{
	size_t length = 0;
	jobs_queue_lock(queue);
	length = queue->length;
	jobs_queue_unlock(queue);
	return length;
}

struct jobs_queue *jobs_queue_init(struct jobs_queue *queue, size_t size, void *user_data)
{
	if(NULL == queue) queue = calloc(1, sizeof(*queue));
	assert(queue);
	
	queue->user_data = user_data;
	
	queue->enter = jobs_queue_enter;
	queue->leave = jobs_queue_leave;
	queue->get_length = jobs_queue_get_length;
	
	int rc = pthread_mutex_init(&queue->mutex, NULL);
	assert(0 == rc);
	
	return queue;
}
void jobs_queue_cleanup(struct jobs_queue *queue)
{
	jobs_queue_lock(queue);
	for(size_t i = 0; i < queue->length; ++i) {
		struct job_data *job = queue->jobs[i];
		queue->jobs[i] = NULL;
		job_data_unref(job);
	}
	return;
}
