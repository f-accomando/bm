/* The HTTP transport in the kernel: streams on lwIP; https through tls.c. */
#include "http.h"
#include "stream.h"

#include <stdio.h>

static void *k_open(const char *host, uint16_t port, int tls, char *err, size_t err_len)
{
    if (tls) {
        snprintf(err, err_len, "https is not available yet (M19.2)");
        return NULL;
    }
    return stream_open(host, port, 10000, err, err_len);
}

static int k_write(void *c, const void *d, size_t n) { return stream_write(c, d, n); }
static int k_read(void *c, void *b, size_t n, uint32_t ms) { return stream_read(c, b, n, ms); }
static void k_close(void *c) { stream_close(c); }

static const http_transport_t kernel_transport = { k_open, k_write, k_read, k_close };
const http_transport_t *http_transport = &kernel_transport;
