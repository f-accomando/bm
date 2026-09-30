/* The HTTP transport in the kernel (and in the host tests, over POSIX
 * streams): plain streams, or TLS for https. */
#include "http.h"
#include "stream.h"
#include "tls.h"

#include <stdlib.h>

/* a connection: plain or TLS */
typedef struct {
    stream_t *s;
    tls_t *t;
} conn_t;

static void *k_open(const char *host, uint16_t port, int tls, char *err, size_t err_len)
{
    conn_t *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    if (tls)
        c->t = tls_open(host, port, err, err_len);
    else
        c->s = stream_open(host, port, 10000, err, err_len);
    if (!c->s && !c->t) {
        free(c);
        return NULL;
    }
    return c;
}

static int k_write(void *p, const void *d, size_t n)
{
    conn_t *c = p;
    return c->t ? tls_write(c->t, d, n) : stream_write(c->s, d, n);
}

static int k_read(void *p, void *b, size_t n, uint32_t ms)
{
    conn_t *c = p;
    return c->t ? tls_read(c->t, b, n, ms) : stream_read(c->s, b, n, ms);
}

static void k_close(void *p)
{
    conn_t *c = p;
    if (c->t)
        tls_close(c->t);
    else
        stream_close(c->s);
    free(c);
}

static const http_transport_t kernel_transport = { k_open, k_write, k_read, k_close };
const http_transport_t *http_transport = &kernel_transport;
