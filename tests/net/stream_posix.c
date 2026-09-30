/* stream.h over POSIX sockets, for the host tests of http.c and tls.c. */
#include "net/stream.h"

#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

struct stream {
    int fd;
};

stream_t *stream_open(const char *host, uint16_t port, uint32_t timeout_ms,
                      char *err, size_t err_len)
{
    (void)timeout_ms;
    struct addrinfo hints = { 0 }, *ai;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host, ps, &hints, &ai)) {
        snprintf(err, err_len, "%s: unknown name (DNS)", host);
        return NULL;
    }
    int fd = socket(ai->ai_family, SOCK_STREAM, 0);
    int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);
    if (r) {
        close(fd);
        snprintf(err, err_len, "%s port %u: connection refused", host, port);
        return NULL;
    }
    stream_t *s = malloc(sizeof *s);
    s->fd = fd;
    return s;
}

int stream_write(stream_t *s, const void *data, size_t len)
{
    return send(s->fd, data, len, MSG_NOSIGNAL) == (ssize_t)len ? 0 : -1;
}

int stream_read(stream_t *s, void *buf, size_t len, uint32_t timeout_ms)
{
    struct pollfd pf = { s->fd, POLLIN, 0 };
    if (poll(&pf, 1, (int)timeout_ms) <= 0)
        return -1;
    ssize_t n = recv(s->fd, buf, len, 0);
    return n < 0 ? -1 : (int)n;
}

void stream_close(stream_t *s)
{
    if (!s)
        return;
    close(s->fd);
    free(s);
}
