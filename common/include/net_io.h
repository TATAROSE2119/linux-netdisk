#ifndef NETDISK_NET_IO_H
#define NETDISK_NET_IO_H

#include <stddef.h>
#include <stdint.h>

int net_read_full(int fd, void *buffer, size_t length);
int net_write_full(int fd, const void *buffer, size_t length);

int net_read_string(int fd, char *buffer, size_t buffer_size,
                    size_t maximum_length);

int send_all(int socket_fd, const void *buffer, size_t length);
int receive_all(int socket_fd, void *buffer, size_t length);

#endif /* NETDISK_NET_IO_H */
