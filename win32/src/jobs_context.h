#ifndef JOBS_CONTEXT_H_
#define JOBS_CONTEXT_H_

#include <stdio.h>
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif

struct job_data;
struct job_data *job_data_new(void (*free_data)(void *), void *user_data);
void job_data_unref(struct job_data *job);

struct jobs_queue
{
	void *user_data;
	int (*enter)(struct jobs_queue *queue, struct job_data *job);
	struct job_data *(*leave)(struct jobs_queue *queue);
	size_t (*get_length)(struct jobs_queue *queue);
	
	// private data
	pthread_mutex_t mutex;
	size_t max_size;
	size_t length;
	struct job_data **jobs;
};
struct jobs_queue *jobs_queue_init(struct jobs_queue *queue, size_t size, void *user_data);
void jobs_queue_cleanup(struct jobs_queue *queue);
#define jobs_queue_lock(queue) pthread_mutex_lock(&queue->mutex)
#define jobs_queue_unlock(queue) pthread_mutex_unlock(&queue->mutex)

#ifdef __cplusplus
}
#endif
#endif
