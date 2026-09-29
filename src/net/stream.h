/*
 * A TCP connection used like a blocking socket (M19): open by host name,
 * write, read with a timeout, close. The kernel has one thread, so waiting
 * means running net_poll until the data is there. On the PC the same
 * interface is backed by POSIX sockets (tests/net), so the HTTP and TLS
 * code above it is tested there.
 */
#ifndef STREAM_H
#define STREAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct stream stream_t;

/* NULL on failure, with the reason in err ("no DNS answer", ...). */
stream_t *stream_open(const char *host, uint16_t port, uint32_t timeout_ms,
                      char *err, size_t err_len);
/* All of it, or -1. */
int  stream_write(stream_t *s, const void *data, size_t len);
/* Up to len bytes: how many, 0 when the other side closed, -1 on error
 * or when nothing came in timeout_ms. */
int  stream_read(stream_t *s, void *buf, size_t len, uint32_t timeout_ms);
void stream_close(stream_t *s);

#endif
