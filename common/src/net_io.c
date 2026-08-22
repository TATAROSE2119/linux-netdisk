#include "net_io.h"
#include <arpa/inet.h>// For inet_ntoa
#include <unistd.h>
#include <errno.h>

int net_read_full(int fd, void *buffer, size_t length) {
    unsigned char *position = buffer;

    while (length > 0) {
        ssize_t bytes_read = read(fd, position, length);

        if (bytes_read == 0) {
            errno = ECONNRESET;
            return -1;
        }
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        position += bytes_read;
        length -= (size_t)bytes_read;
    }
    return 0;
}
int net_write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *position = buffer;

    while (length > 0) {
        ssize_t bytes_written = write(fd, position, length);

        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_written == 0) {
            errno = EPIPE;
            return -1;
        }
        position += bytes_written;
        length -= (size_t)bytes_written;
    }
    return 0;
}
static int net_read_u32(int fd, uint32_t *value) {
    uint32_t network_value;

    if (net_read_full(fd, &network_value, sizeof(network_value)) != 0) {
        return -1;
    }
    *value = ntohl(network_value);
    return 0;
}

int net_read_string(int fd, char *buffer, size_t buffer_size,
                    size_t maximum_length) {
    uint32_t length;

    if (buffer_size == 0 || maximum_length >= buffer_size ||
        net_read_u32(fd, &length) != 0) {
        return -1;
    }
    if (length > maximum_length) {
        errno = EMSGSIZE;
        return -1;
    }
    if (net_read_full(fd, buffer, length) != 0) {
        return -1;
    }
    buffer[length] = '\0';
    return 0;
}
int send_all(int socket_fd, const void *buffer, size_t length) {
    const unsigned char *position = buffer;

    while (length > 0) {
        ssize_t bytes_sent;

#ifdef MSG_NOSIGNAL
        bytes_sent = send(socket_fd, position, length, MSG_NOSIGNAL);
#else
        bytes_sent = send(socket_fd, position, length, 0);
#endif
        if (bytes_sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_sent == 0) {
            errno = EPIPE;
            return -1;
        }
        position += bytes_sent;
        length -= (size_t)bytes_sent;
    }
    return 0;
}

int receive_all(int socket_fd, void *buffer, size_t length) {
    unsigned char *position = buffer;

    while (length > 0) {
        ssize_t bytes_received = recv(socket_fd, position, length, 0);

        if (bytes_received == 0) {
            errno = ECONNRESET;
            return -1;
        }
        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        position += bytes_received;
        length -= (size_t)bytes_received;
    }
    return 0;
}
