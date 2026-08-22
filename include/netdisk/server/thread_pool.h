#ifndef NETDISK_THREAD_POOL_H
#define NETDISK_THREAD_POOL_H

#include <stddef.h>

struct thread_pool;

typedef void (*thread_pool_task_handler)(int client_fd);

/*
 * Create a fixed-size worker pool backed by a bounded FIFO queue.
 * The caller retains ownership of a client fd until thread_pool_submit()
 * succeeds. After a successful submit, the pool closes the fd after the
 * handler returns.
 */
int thread_pool_create(struct thread_pool **pool_out, size_t worker_count,
		       size_t queue_capacity,
		       thread_pool_task_handler handler);

/* Returns 0, EAGAIN when the queue is full, or ECANCELED while stopping. */
int thread_pool_submit(struct thread_pool *pool, int client_fd);

/* Stop accepting tasks, close queued sockets, wake active sockets, and join. */
void thread_pool_shutdown(struct thread_pool *pool);

/* Shutdown if necessary and release all pool resources. */
void thread_pool_destroy(struct thread_pool *pool);

#endif
