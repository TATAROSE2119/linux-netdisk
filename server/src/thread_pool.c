#define _GNU_SOURCE

/*
 * 固定大小线程池实现。
 *
 * 主线程把客户端 fd 放入有界环形队列；工作线程从队列取出 fd，调用上层
 * handler 处理一条客户端请求，并在 handler 返回后关闭连接。所有队列状态
 * 和 active_fds 都由同一把互斥锁保护，使关闭流程可以安全唤醒阻塞连接。
 */
#include "thread_pool.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 每个工作线程独有的上下文，用 index 定位 active_fds 中自己的槽位。 */
struct worker_context {
	struct thread_pool *pool;
	size_t index;
};

/*
 * 线程池完整状态。
 * queue 使用 queue_head/queue_tail 实现 FIFO 环形缓冲区；active_fds 记录各
 * 工作线程当前处理的 socket，服务器关闭时可对这些 socket 调用 shutdown()。
 */
struct thread_pool {
	/* 工作线程句柄及其稳定的启动参数。 */
	pthread_t *threads;
	struct worker_context *workers;
	/* 等待处理的 socket 环形队列，以及各线程正在处理的 socket。 */
	int *queue;
	int *active_fds;
	size_t worker_count;
	size_t created_workers;
	size_t queue_capacity;
	size_t queue_head;
	size_t queue_tail;
	size_t queue_count;
	/* handler 由服务器注入，线程池本身不理解应用层协议。 */
	thread_pool_task_handler handler;
	/* mutex 同时保护队列、stopping 和 active_fds；not_empty 唤醒消费者。 */
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;
	/* stopping 阻止继续取任务，joined 防止重复 pthread_join。 */
	bool stopping;
	bool joined;
};

/*
 * 关闭一个客户端连接。先 shutdown() 可唤醒其他线程中阻塞的 read/write，
 * 再 close() 释放文件描述符；负数 fd 被视为空槽位。
 */
static void close_client_fd(int client_fd)
{
	if (client_fd < 0)
		return;

	shutdown(client_fd, SHUT_RDWR);
	close(client_fd);
}

/*
 * 工作线程主循环：等待任务、取出 fd、在锁外处理请求、清除活动槽并关闭 fd。
 * handler 必须在函数返回前完成该连接的所有业务处理，但不应自行 close(fd)。
 */
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
		 * 必须先在锁内清除活动槽，再关闭 fd。shutdown 流程也持有同一把锁，
		 * 因而不会在线程关闭 fd、系统复用该编号后误操作新的连接。
		 */
		pthread_mutex_lock(&pool->mutex);
		pool->active_fds[worker->index] = -1;
		pthread_mutex_unlock(&pool->mutex);
		close_client_fd(client_fd);
	}

	return NULL;
}

/* 销毁条件变量和互斥锁；只能在线程全部退出后调用。 */
static void destroy_synchronization(struct thread_pool *pool)
{
	pthread_cond_destroy(&pool->not_empty);
	pthread_mutex_destroy(&pool->mutex);
}

/*
 * 分配线程池的所有数组、初始化同步原语并启动固定数量的工作线程。
 * 初始化中途失败时按照创建顺序的逆过程回收，保证 pool_out 不暴露半成品。
 */
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
	/* 唤醒已经启动的线程，让它们观察 stopping 后退出，再统一 join。 */
	pthread_mutex_lock(&pool->mutex);
	pool->stopping = true;
	pthread_cond_broadcast(&pool->not_empty);
	pthread_mutex_unlock(&pool->mutex);
	for (index = 0; index < pool->created_workers; index++)
		pthread_join(pool->threads[index], NULL);
	destroy_synchronization(pool);

free_allocations:
	/* calloc/malloc 失败时 free(NULL) 也是安全的。 */
	free(pool->active_fds);
	free(pool->queue);
	free(pool->workers);
	free(pool->threads);
	free(pool);
	return error;
}

/*
 * 在锁内检查运行状态和队列容量，再把 fd 放入 FIFO 队尾。
 * 只有返回 0 时 fd 所有权才转移给线程池。
 */
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

/*
 * 协作式关闭线程池。
 * 排队中的连接直接关闭；执行中的连接只调用 shutdown()，让 handler 从阻塞
 * I/O 返回并自行清理业务资源，最终仍由 worker_main() 执行 close()。
 */
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
	/* 广播唤醒所有可能正在等待空队列的工作线程。 */
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

/* 关闭线程池（若尚未关闭）并释放它拥有的全部内存。 */
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
