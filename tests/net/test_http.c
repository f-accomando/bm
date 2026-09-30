/*
 * Host test of the HTTP client (src/net/http.c) over POSIX sockets,
 * against tests/net/http_server.py (started by run_http_test.py):
 * Content-Length and chunked bodies, redirects, 404, a big body, PUT.
 */
#include "net/http.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *p_open(const char *host, uint16_t port, int tls, char *err, size_t err_len)
{
    if (tls) {
        snprintf(err, err_len, "no tls here");
        return NULL;
    }
    struct addrinfo hints = { 0 }, *ai;
    hints.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host, ps, &hints, &ai)) {
        snprintf(err, err_len, "%s: unknown name", host);
        return NULL;
    }
    int fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        snprintf(err, err_len, "connection refused");
        close(fd);
        freeaddrinfo(ai);
        return NULL;
    }
    freeaddrinfo(ai);
    int *c = malloc(sizeof *c);
    *c = fd;
    return c;
}

static int p_write(void *c, const void *d, size_t n)
{
    return send(*(int *)c, d, n, 0) == (ssize_t)n ? 0 : -1;
}

static int p_read(void *c, void *b, size_t n, uint32_t ms)
{
    struct pollfd pf = { *(int *)c, POLLIN, 0 };
    if (poll(&pf, 1, (int)ms) <= 0)
        return -1;
    return (int)recv(*(int *)c, b, n, 0);
}

static void p_close(void *c)
{
    close(*(int *)c);
    free(c);
}

static const http_transport_t posix = { p_open, p_write, p_read, p_close };
const http_transport_t *http_transport = &posix;

static struct { uint8_t b[256]; size_t n; } out;
static int sink(void *ctx, const uint8_t *p, size_t k)
{
    (void)ctx;
    memcpy(out.b + out.n, p, k);
    out.n += k;
    return 0;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    char base[64], url[128];
    snprintf(base, sizeof base, "http://127.0.0.1:%s", argv[1]);
    uint8_t *d;
    size_t n;
    http_info_t info;

    snprintf(url, sizeof url, "%s/hello", base);
    int st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 10 && !memcmp(d, "hello, bm\n", 10) && info.length == 10 &&
          !strcmp(info.type, "text/plain"), "GET with Content-Length");
    free(d);

    snprintf(url, sizeof url, "%s/chunked", base);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 3000 && d[0] == 'a' && d[2999] == 'c' && info.length == -1, "chunked body");
    free(d);

    snprintf(url, sizeof url, "%s/redirect", base);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 10 && strstr(info.url, "/hello"), "redirect (relative Location)");
    free(d);

    snprintf(url, sizeof url, "%s/redirect-abs", base);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 10, "redirect (absolute Location)");
    free(d);

    snprintf(url, sizeof url, "%s/missing", base);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 404 && n == 0, "404: status, no body kept");
    free(d);

    snprintf(url, sizeof url, "%s/big", base);
    st = http_get_buffer(url, NULL, 4 << 20, &d, &n, &info);
    int same = n == 3 << 20;
    for (size_t i = 0; same && i < n; i++)
        same = d[i] == (uint8_t)(i * 31 + (i >> 12));
    check(st == 200 && same, "3 MiB body, same bytes");
    free(d);

    snprintf(url, sizeof url, "%s/big", base);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(n <= 1 << 20 && strstr(info.error, "too big"), "limit: stops at max bytes");
    free(d);

    snprintf(url, sizeof url, "%s/close", base);
    st = http_get_buffer(url, NULL, 1 << 20, &d, &n, &info);
    check(st == 200 && n == 5000, "body until the server closes");
    free(d);

    static const char body[] = "{\"message\":\"up\"}";
    http_req_t req = { "PUT", "Authorization: Bearer abc\r\nContent-Type: application/json\r\n",
                       body, sizeof body - 1, 0 };
    snprintf(url, sizeof url, "%s/echo", base);
    st = http_request(url, &req, sink, NULL, &info);
    check(st == 201 && strstr((char *)out.b, "PUT Bearer abc {\"message\":\"up\"}"), "PUT with headers and body");

    st = http_get_buffer("http://127.0.0.1:1/x", NULL, 100, &d, &n, &info);
    check(st == -1 && info.error[0], "connection refused: an error");
    free(d);
    check(http_get_buffer("ftp://x/y", NULL, 100, &d, &n, &info) == -1, "not http: an error");
    free(d);

    printf(fails ? "\n%d FAILED\n" : "\nhttp: all passed\n", fails);
    return fails != 0;
}
