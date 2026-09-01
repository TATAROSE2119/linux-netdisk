#define _GNU_SOURCE

#include "epoll_server.h"
#include "thread_pool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EPOLL_BATCH_SIZE 256
#define EPOLL_TICK_MILLISECONDS 1000

enum event_source_type {
    EVENT_SOURCE_LISTENER,
    EVENT_SOURCE_PENDING_CLIENT,
};

struct event_source {
    enum event_source_type type;
    int fd;
};

struct pending_connection {
    struct event_source source;
    struct timespec accepted_at;
    struct pending_connection *previous;
    struct pending_connection *next;
};

struct epoll_server {
    int epoll_fd;
    int listen_fd;
    size_t client_timeout_seconds;
    struct thread_pool *pool;
    const atomic_bool *stop_requested;
    struct epoll_server_stats *stats;
    struct event_source listener_source;
    struct pending_connection *pending_head;
};

static void pending_insert(struct epoll_server *server,
                           struct pending_connection *connection)
{
    connection->previous = NULL;
    connection->next = server->pending_head;
    if (server->pending_head != NULL) {
        server->pending_head->previous = connection;
    }
    server->pending_head = connection;
}

static void pending_remove(struct epoll_server *server,
                           struct pending_connection *connection)
{
    if (connection->previous != NULL) {
        connection->previous->next = connection->next;
    } else {
        server->pending_head = connection->next;
    }
    if (connection->next != NULL) {
        connection->next->previous = connection->previous;
    }
    connection->previous = NULL;
    connection->next = NULL;
}

static void close_socket(int fd)
{
    if (fd < 0) {
        return;
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

static void close_pending(struct epoll_server *server,
                          struct pending_connection *connection)
{
    if (connection->source.fd >= 0) {
        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL,
                      connection->source.fd, NULL) != 0 &&
            errno != ENOENT && errno != EBADF) {
            perror("failed to remove pending client from epoll");
        }
        close_socket(connection->source.fd);
        connection->source.fd = -1;
    }
    pending_remove(server, connection);
    free(connection);
}

static int configure_blocking_client(int client_fd, size_t timeout_seconds)
{
    struct timeval timeout = {
        .tv_sec = (time_t)timeout_seconds,
        .tv_usec = 0,
    };
    int flags = fcntl(client_fd, F_GETFL, 0);

    if (flags < 0 ||
        fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK) < 0 ||
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) < 0 ||
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout)) < 0) {
        return -1;
    }
    return 0;
}

static void reject_detached_connection(struct epoll_server *server,
                                       struct pending_connection *connection,
                                       bool queue_full)
{
    close_socket(connection->source.fd);
    connection->source.fd = -1;
    if (queue_full) {
        server->stats->rejected_connections++;
        if (server->stats->rejected_connections == 1 ||
            server->stats->rejected_connections % 100 == 0) {
            fprintf(stderr,
                    "task queue full; rejected %zu connection(s)\n",
                    server->stats->rejected_connections);
        }
    }
    free(connection);
}

static void dispatch_pending(struct epoll_server *server,
                             struct pending_connection *connection)
{
    int client_fd = connection->source.fd;
    int result;

    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL) != 0 &&
        errno != ENOENT) {
        perror("failed to detach ready client from epoll");
        close_pending(server, connection);
        return;
    }
    pending_remove(server, connection);

    if (configure_blocking_client(client_fd,
                                  server->client_timeout_seconds) != 0) {
        perror("failed to configure ready client socket");
        reject_detached_connection(server, connection, false);
        return;
    }

    result = thread_pool_submit(server->pool, client_fd);
    if (result == 0) {
        server->stats->dispatched_connections++;
        connection->source.fd = -1;
        free(connection);
        return;
    }

    reject_detached_connection(server, connection, result == EAGAIN);
    if (result != EAGAIN && result != ECANCELED) {
        fprintf(stderr, "failed to enqueue client: %s\n", strerror(result));
    }
}

static bool retryable_accept_error(int error)
{
    return error == ECONNABORTED || error == ENETDOWN || error == EPROTO ||
           error == ENOPROTOOPT || error == EHOSTDOWN || error == ENONET ||
           error == EHOSTUNREACH || error == EOPNOTSUPP ||
           error == ENETUNREACH;
}

static int accept_pending_connections(struct epoll_server *server)
{
    for (;;) {
        struct pending_connection *connection;
        struct epoll_event event;
        int client_fd;

        client_fd = accept4(server->listen_fd, NULL, NULL,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EINTR || retryable_accept_error(errno)) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (atomic_load(server->stop_requested) &&
                (errno == EINVAL || errno == EBADF)) {
                return 0;
            }
            return -1;
        }

        connection = calloc(1, sizeof(*connection));
        if (connection == NULL) {
            close_socket(client_fd);
            continue;
        }
        connection->source.type = EVENT_SOURCE_PENDING_CLIENT;
        connection->source.fd = client_fd;
        if (clock_gettime(CLOCK_MONOTONIC, &connection->accepted_at) != 0) {
            close_socket(client_fd);
            free(connection);
            continue;
        }

        pending_insert(server, connection);
        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
        event.data.ptr = &connection->source;
        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) != 0) {
            close_pending(server, connection);
            continue;
        }
        server->stats->accepted_connections++;
    }
}

static bool has_expired(const struct timespec *now,
                        const struct timespec *accepted_at,
                        size_t timeout_seconds)
{
    time_t seconds = now->tv_sec - accepted_at->tv_sec;
    long nanoseconds = now->tv_nsec - accepted_at->tv_nsec;

    if (nanoseconds < 0) {
        seconds--;
    }
    return seconds >= 0 && (size_t)seconds >= timeout_seconds;
}

static void expire_pending_connections(struct epoll_server *server)
{
    struct pending_connection *connection = server->pending_head;
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return;
    }
    while (connection != NULL) {
        struct pending_connection *next = connection->next;

        if (has_expired(&now, &connection->accepted_at,
                        server->client_timeout_seconds)) {
            close_pending(server, connection);
            server->stats->expired_connections++;
        }
        connection = next;
    }
}

static void close_all_pending(struct epoll_server *server)
{
    while (server->pending_head != NULL) {
        close_pending(server, server->pending_head);
    }
}

int epoll_server_run(int listen_fd, struct thread_pool *pool,
                     size_t client_timeout_seconds,
                     const atomic_bool *stop_requested,
                     struct epoll_server_stats *stats)
{
    struct epoll_server server = {
        .epoll_fd = -1,
        .listen_fd = listen_fd,
        .client_timeout_seconds = client_timeout_seconds,
        .pool = pool,
        .stop_requested = stop_requested,
        .stats = stats,
        .listener_source = {
            .type = EVENT_SOURCE_LISTENER,
            .fd = listen_fd,
        },
    };
    struct epoll_event listener_event;
    struct epoll_event events[EPOLL_BATCH_SIZE];
    int status = 0;

    if (listen_fd < 0 || pool == NULL || client_timeout_seconds == 0 ||
        stop_requested == NULL || stats == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(stats, 0, sizeof(*stats));

    server.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (server.epoll_fd < 0) {
        return -1;
    }

    memset(&listener_event, 0, sizeof(listener_event));
    listener_event.events = EPOLLIN;
    listener_event.data.ptr = &server.listener_source;
    if (epoll_ctl(server.epoll_fd, EPOLL_CTL_ADD, listen_fd,
                  &listener_event) != 0) {
        status = -1;
        goto cleanup;
    }

    while (!atomic_load(stop_requested)) {
        int event_count = epoll_wait(server.epoll_fd, events,
                                     EPOLL_BATCH_SIZE,
                                     EPOLL_TICK_MILLISECONDS);

        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = -1;
            break;
        }
        if (atomic_load(stop_requested)) {
            break;
        }

        for (int index = 0; index < event_count; index++) {
            struct event_source *source = events[index].data.ptr;
            uint32_t flags = events[index].events;

            if (source->type == EVENT_SOURCE_LISTENER) {
                if ((flags & EPOLLIN) != 0 &&
                    accept_pending_connections(&server) != 0) {
                    status = -1;
                    goto cleanup;
                }
                if ((flags & (EPOLLERR | EPOLLHUP)) != 0 &&
                    !atomic_load(stop_requested)) {
                    errno = EIO;
                    status = -1;
                    goto cleanup;
                }
                continue;
            }

            struct pending_connection *connection =
                (struct pending_connection *)source;
            if ((flags & EPOLLERR) != 0) {
                close_pending(&server, connection);
            } else if ((flags & EPOLLIN) != 0) {
                dispatch_pending(&server, connection);
            } else if ((flags & (EPOLLHUP | EPOLLRDHUP)) != 0) {
                close_pending(&server, connection);
            }
        }
        expire_pending_connections(&server);
    }

cleanup:
    close_all_pending(&server);
    close(server.epoll_fd);
    return status;
}
