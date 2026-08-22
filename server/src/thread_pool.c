#define _GNU_SOURCE

#include "thread_pool.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct worker_context {
	struct thread_pool *pool;
	size_t index;
};

struct thread_pool {
	pthread_t *threads;
	struct worker_context *workers;
	int *queue;
	int *active_fds;
	size_t worker_count;
	size_t created_workers;
	size_t queue_capacity;
	size_t queue_head;
	size_t queue_tail;
	size_t queue_count;
	thread_pool_task_handler handler;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	bool stopping;
	bool joined;
};

static void close_client_fd(int client_fd)
{
	if (client_fd < 0)
		return;

	shutdown(client_fd, SHUT_RDWR);
	close(client_fd);
}

static void *worker_main(void *argument)
{
	struct worker_context *worker = argument;
	struct thread_pool *pool = worker->pool;
	for (;;) {
		int client_fd;

		pthread_mutex_lock(&pool->mutex);
		while (pool->queue_count == 0 && !pool->stopping)
			pthread_cond_wait(&pool->not_empty, &pool->mutex);

		if (pool->stopping) {
			pthread_mutex_unlock(&pool->mutex);
			break;
		}

		client_fd = pool->queue[pool->queue_head];
		pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
		pool->queue_count--;
		pool->active_fds[worker->index] = client_fd;
		pthread_mutex_unlock(&pool->mutex);

		pool->handler(client_fd);

		/*
		 * Clear the active slot before close. shutdown() holds the same
		 * mutex, so it can never act on an fd number after it is reused.
		 */
		pthread_mutex_lock(&pool->mutex);
		pool->active_fds[worker->index] = -1;
		pthread_mutex_unlock(&pool->mutex);
		close_client_fd(client_fd);
	}

	return NULL;
}

static void destroy_synchronization(struct thread_pool *pool)
{
	pthread_cond_destroy(&pool->not_empty);
	pthread_mutex_destroy(&pool->mutex);
}

int thread_pool_create(struct thread_pool **pool_out, size_t worker_count,
		       size_t queue_capacity,
		       thread_pool_task_handler handler)
{
	struct thread_pool *pool;
	size_t index;
	int error;

	if (!pool_out || !handler || worker_count == 0 || queue_capacity == 0)
		return EINVAL;

	*pool_out = NULL;
	pool = calloc(1, sizeof(*pool));
	if (!pool)
		return ENOMEM;

	pool->threads = calloc(worker_count, sizeof(*pool->threads));
	pool->workers = calloc(worker_count, sizeof(*pool->workers));
	pool->queue = calloc(queue_capacity, sizeof(*pool->queue));
	pool->active_fds = malloc(worker_count * sizeof(*pool->active_fds));
	if (!pool->threads || !pool->workers || !pool->queue || !pool->active_fds) {
		error = ENOMEM;
		goto free_allocations;
	}

	pool->worker_count = worker_count;
	pool->queue_capacity = queue_capacity;
	pool->handler = handler;
	for (index = 0; index < worker_count; index++)
		pool->active_fds[index] = -1;

	error = pthread_mutex_init(&pool->mutex, NULL);
	if (error)
		goto free_allocations;
	error = pthread_cond_init(&pool->not_empty, NULL);
	if (error) {
		pthread_mutex_destroy(&pool->mutex);
		goto free_allocations;
	}

	for (index = 0; index < worker_count; index++) {
		pool->workers[index].pool = pool;
		pool->workers[index].index = index;
		error = pthread_create(&pool->threads[index], NULL, worker_main,
				       &pool->workers[index]);
		if (error)
			goto stop_created_workers;
		pool->created_workers++;
	}

	*pool_out = pool;
	return 0;

stop_created_workers:
	pthread_mutex_lock(&pool->mutex);
	pool->stopping = true;
	pthread_cond_broadcast(&pool->not_empty);
	pthread_mutex_unlock(&pool->mutex);
	for (index = 0; index < pool->created_workers; index++)
		pthread_join(pool->threads[index], NULL);
	destroy_synchronization(pool);

free_allocations:
	free(pool->active_fds);
	free(pool->queue);
	free(pool->workers);
	free(pool->threads);
	free(pool);
	return error;
}

int thread_pool_submit(struct thread_pool *pool, int client_fd)
{
	int error = 0;

	if (!pool || client_fd < 0)
		return EINVAL;

	pthread_mutex_lock(&pool->mutex);
	if (pool->stopping) {
		error = ECANCELED;
	} else if (pool->queue_count == pool->queue_capacity) {
		error = EAGAIN;
	} else {
		pool->queue[pool->queue_tail] = client_fd;
		pool->queue_tail = (pool->queue_tail + 1) % pool->queue_capacity;
		pool->queue_count++;
		pthread_cond_signal(&pool->not_empty);
	}
	pthread_mutex_unlock(&pool->mutex);

	return error;
}

void thread_pool_shutdown(struct thread_pool *pool)
{
	size_t index;

	if (!pool || pool->joined)
		return;

	pthread_mutex_lock(&pool->mutex);
	pool->stopping = true;

	while (pool->queue_count > 0) {
		int client_fd = pool->queue[pool->queue_head];

		pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
		pool->queue_count--;
		close_client_fd(client_fd);
	}

	for (index = 0; index < pool->created_workers; index++) {
		if (pool->active_fds[index] >= 0)
			shutdown(pool->active_fds[index], SHUT_RDWR);
	}
	pthread_cond_broadcast(&pool->not_empty);
	pthread_mutex_unlock(&pool->mutex);

	for (index = 0; index < pool->created_workers; index++) {
		int error = pthread_join(pool->threads[index], NULL);

		if (error)
			fprintf(stderr, "failed to join worker %zu: %s\n", index,
				strerror(error));
	}
	pool->joined = true;
}

void thread_pool_destroy(struct thread_pool *pool)
{
	if (!pool)
		return;

	thread_pool_shutdown(pool);
	destroy_synchronization(pool);
	free(pool->active_fds);
	free(pool->queue);
	free(pool->workers);
	free(pool->threads);
	free(pool);
}
