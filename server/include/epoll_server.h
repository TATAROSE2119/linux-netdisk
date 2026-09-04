#ifndef NETDISK_EPOLL_SERVER_H
#define NETDISK_EPOLL_SERVER_H

#include <stdatomic.h>
#include <stddef.h>

/* epoll 调度器只持有线程池指针，不了解线程池内部布局。 */
struct thread_pool;

/* 一次服务器运行期间的连接统计，由 epoll 事件循环单线程更新。 */
struct epoll_server_stats {
    /* accept4() 成功接收并登记到 epoll 的连接数。 */
    size_t accepted_connections;
    /* 已从 epoll 移交给工作线程池的连接数。 */
    size_t dispatched_connections;
    /* 因线程池队列已满而主动拒绝的连接数。 */
    size_t rejected_connections;
    /* 在规定时间内没有发送首个可读数据而被关闭的连接数。 */
    size_t expired_connections;
};

/*
 * 在 listen_fd 上运行 epoll 事件循环。
 *
 * 新连接先以非阻塞方式等待首个可读事件，避免空闲客户端长期占住工作线程；
 * 就绪后再恢复为带收发超时的阻塞 socket，并将所有权交给线程池。
 * stop_requested 由信号线程设置，stats 接收本次运行的统计结果。
 * 正常停止返回 0；参数错误或 epoll 系统调用失败返回 -1 并设置 errno。
 */
int epoll_server_run(int listen_fd, struct thread_pool *pool,
                     size_t client_timeout_seconds,
                     const atomic_bool *stop_requested,
                     struct epoll_server_stats *stats);

#endif /* NETDISK_EPOLL_SERVER_H */
