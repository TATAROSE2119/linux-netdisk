#ifndef NETDISK_SERVER_THREAD_POOL_H
#define NETDISK_SERVER_THREAD_POOL_H

#include <stddef.h>

/* 线程池内部结构对调用者不可见，只能通过本头文件提供的 API 操作。 */
struct thread_pool;

/* 工作线程处理一个已连接客户端的回调；回调返回后由线程池关闭 fd。 */
typedef void (*thread_pool_task_handler)(int client_fd);

/*
 * 创建固定数量的工作线程以及一个有界 FIFO 任务队列。
 * 成功时把新线程池写入 pool_out 并返回 0；失败时返回 errno 风格错误码。
 *
 * client_fd 在 thread_pool_submit() 成功前仍属于调用者；提交成功后所有权
 * 转移给线程池，工作回调执行结束后线程池负责关闭它。
 */
int thread_pool_create(struct thread_pool **pool_out, size_t worker_count,
		       size_t queue_capacity,
		       thread_pool_task_handler handler);

/*
 * 将客户端连接放入队列。成功返回 0；队列已满返回 EAGAIN；线程池正在停止
 * 时返回 ECANCELED。失败时 fd 所有权仍属于调用者。
 */
int thread_pool_submit(struct thread_pool *pool, int client_fd);

/*
 * 停止接收任务，关闭尚未处理的连接，唤醒正在阻塞 I/O 的连接并等待全部
 * 工作线程退出。函数可重复调用。
 */
void thread_pool_shutdown(struct thread_pool *pool);

/* 必要时先关闭线程池，然后释放队列、线程数组和同步原语。 */
void thread_pool_destroy(struct thread_pool *pool);

#endif /* NETDISK_SERVER_THREAD_POOL_H */
