#ifndef NETDISK_EPOLL_SERVER_H
#define NETDISK_EPOLL_SERVER_H

#include <stdatomic.h>
#include <stddef.h>

struct thread_pool;

struct epoll_server_stats {
    size_t accepted_connections;
    size_t dispatched_connections;
    size_t rejected_connections;
    size_t expired_connections;
};

int epoll_server_run(int listen_fd, struct thread_pool *pool,
                     size_t client_timeout_seconds,
                     const atomic_bool *stop_requested,
                     struct epoll_server_stats *stats);

#endif /* NETDISK_EPOLL_SERVER_H */
