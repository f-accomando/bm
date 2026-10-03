/*
 * The HTTP client: one request per connection ("Connection: close"),
 * headers read line by line, then the body by length, by chunks or until
 * the server closes. Up to 5 redirects (GitHub sends release downloads to
 * another host, with a signed address of about a thousand characters).
 */
#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef HTTP_USER_AGENT
#include "kernel/version.h"
#define HTTP_USER_AGENT bm_version
#endif

typedef struct {
    void *c;
    uint8_t buf[4096];
    size_t pos, len;
    uint32_t timeout;
} reader_t;

static int fill(reader_t *r)
{
    int n = http_transport->read(r->c, r->buf, sizeof r->buf, r->timeout);
    if (n <= 0)
        return n;
    r->pos = 0;
    r->len = (size_t)n;
    return n;
}

/* One header line without "\r\n"; -1 at the end of the data. */
static int read_line(reader_t *r, char *line, size_t max)
{
    size_t n = 0;
    for (;;) {
        if (r->pos == r->len && fill(r) <= 0)
            return -1;
        char c = (char)r->buf[r->pos++];
        if (c == '\n') {
            if (n && line[n - 1] == '\r')
                n--;
            line[n] = 0;
            return (int)n;
        }
        if (n < max - 1)
            line[n++] = c;
    }
}

/* Up to len body bytes from the buffer or the connection. */
static int read_some(reader_t *r, uint8_t *out, size_t len)
{
    if (r->pos == r->len) {
        int n = fill(r);
        if (n <= 0)
            return n;
    }
    size_t n = r->len - r->pos < len ? r->len - r->pos : len;
    memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return (int)n;
}

/* the longest address followed: GitHub's signed download links are ~1 KB */
#define URL_MAX 2048

typedef struct {
    int tls;
    char host[128];
    uint16_t port;
    const char *path;           /* in the address parsed; "" or "?..." need a "/" */
} url_t;

static int parse_url(const char *u, url_t *p)
{
    memset(p, 0, sizeof *p);
    if (strncmp(u, "https://", 8) == 0) {
        p->tls = 1;
        p->port = 443;
        u += 8;
    } else if (strncmp(u, "http://", 7) == 0) {
        p->port = 80;
        u += 7;
    } else {
        return -1;
    }
    size_t h = strcspn(u, ":/?");
    if (h == 0 || h >= sizeof p->host)
        return -1;
    memcpy(p->host, u, h);
    u += h;
    if (*u == ':') {
        p->port = (uint16_t)atoi(u + 1);
        u += strcspn(u, "/?");
    }
    p->path = u;
    return 0;
}

static int body_to_sink(reader_t *r, long long length, int chunked,
                        http_sink_t sink, void *ctx, http_info_t *info)
{
    static uint8_t chunk[4096];
    if (chunked) {
        char line[64];
        for (;;) {
            if (read_line(r, line, sizeof line) < 0)
                return -1;
            long long left = strtoll(line, NULL, 16);
            if (left == 0) {
                while (read_line(r, line, sizeof line) > 0)
                    ;                   /* trailers */
                return 0;
            }
            while (left > 0) {
                int n = read_some(r, chunk, left < (long long)sizeof chunk ? (size_t)left : sizeof chunk);
                if (n <= 0)
                    return -1;
                info->received += (uint32_t)n;
                if (sink && sink(ctx, chunk, (size_t)n))
                    return -2;
                left -= n;
            }
            if (read_line(r, line, sizeof line) < 0)    /* the chunk's "\r\n" */
                return -1;
        }
    }
    long long left = length;
    while (length < 0 || left > 0) {
        size_t want = sizeof chunk;
        if (length >= 0 && left < (long long)want)
            want = (size_t)left;
        int n = read_some(r, chunk, want);
        if (n == 0 && length < 0)
            return 0;                   /* the server closed: the end */
        if (n <= 0)
            return -1;
        info->received += (uint32_t)n;
        if (sink && sink(ctx, chunk, (size_t)n))
            return -2;
        left -= n;
    }
    return 0;
}

int http_request(const char *url, const http_req_t *req, http_sink_t sink, void *ctx,
                 http_info_t *info)
{
    static const http_req_t get = { 0 };
    if (!req)
        req = &get;
    memset(info, 0, sizeof *info);
    static char where[URL_MAX], location[URL_MAX];
    if (strlen(url) >= sizeof where) {
        snprintf(info->error, sizeof info->error, "address too long");
        return -1;
    }
    snprintf(where, sizeof where, "%s", url);

    for (int hop = 0; hop < 6; hop++) {
        snprintf(info->url, sizeof info->url, "%.*s", (int)sizeof info->url - 1, where);    /* shown only */
        url_t u;
        if (parse_url(where, &u)) {
            snprintf(info->error, sizeof info->error, "not an http(s) address");
            return -1;
        }
        void *c = http_transport->open(u.host, u.port, u.tls, info->error, sizeof info->error);
        if (!c)
            return -1;
        const char *method = req->method ? req->method : "GET";
        static char head[URL_MAX + 1024];
        int hl = snprintf(head, sizeof head,
                          "%s %s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: bm/%s\r\n"
                          "Accept: */*\r\nConnection: close\r\n%s",
                          method, u.path[0] == '/' ? "" : "/", u.path, u.host, HTTP_USER_AGENT,
                          req->headers ? req->headers : "");
        if (req->body)
            hl += snprintf(head + hl, sizeof head - (size_t)hl, "Content-Length: %u\r\n",
                           (unsigned)req->body_len);
        hl += snprintf(head + hl, sizeof head - (size_t)hl, "\r\n");
        if (hl >= (int)sizeof head
            || http_transport->write(c, head, (size_t)hl)
            || (req->body && req->body_len && http_transport->write(c, req->body, req->body_len))) {
            http_transport->close(c);
            snprintf(info->error, sizeof info->error, "could not send the request");
            return -1;
        }

        static reader_t r;
        memset(&r, 0, sizeof r);
        r.c = c;
        r.timeout = req->timeout_ms ? req->timeout_ms : 15000;
        static char line[URL_MAX + 64];
        if (read_line(&r, line, sizeof line) < 0 || strncmp(line, "HTTP/1.", 7) != 0) {
            http_transport->close(c);
            snprintf(info->error, sizeof info->error, "no HTTP answer");
            return -1;
        }
        info->status = atoi(line + 9);
        info->length = -1;
        int chunked = 0;
        location[0] = 0;
        int too_long = 0;
        while (read_line(&r, line, sizeof line) > 0) {
            char *v = strchr(line, ':');
            if (!v)
                continue;
            *v++ = 0;
            while (*v == ' ')
                v++;
            if (!strcasecmp(line, "Content-Length"))
                info->length = strtoll(v, NULL, 10);
            else if (!strcasecmp(line, "Transfer-Encoding") && strstr(v, "chunked"))
                chunked = 1;
            else if (!strcasecmp(line, "Content-Type"))
                snprintf(info->type, sizeof info->type, "%s", v);
            else if (!strcasecmp(line, "Location"))
                too_long = snprintf(location, sizeof location, "%s", v) >= (int)sizeof location;
        }
        int redirect = info->status == 301 || info->status == 302 || info->status == 303 ||
                       info->status == 307 || info->status == 308;
        if (redirect && location[0]) {
            http_transport->close(c);
            if (location[0] == '/')             /* same host */
                too_long |= snprintf(where, sizeof where, "%s://%s:%u%s", u.tls ? "https" : "http",
                                     u.host, u.port, location) >= (int)sizeof where;
            else
                snprintf(where, sizeof where, "%s", location);
            if (too_long) {
                snprintf(info->error, sizeof info->error, "redirect to an address too long");
                return -1;
            }
            continue;
        }
        int e = body_to_sink(&r, info->length, chunked,
                             (info->status >= 200 && info->status < 300) || req->any_status ? sink : NULL,
                             ctx, info);
        http_transport->close(c);
        if (e == -2)
            snprintf(info->error, sizeof info->error, "stopped after %lu bytes (too big?)",
                     (unsigned long)info->received);
        if (e == -1) {
            snprintf(info->error, sizeof info->error, "connection lost after %lu bytes",
                     (unsigned long)info->received);
            return -1;
        }
        return info->status;
    }
    snprintf(info->error, sizeof info->error, "too many redirects");
    return -1;
}

typedef struct {
    uint8_t *data;
    size_t len, cap, max;
} buf_t;

static int to_buffer(void *ctx, const uint8_t *d, size_t n)
{
    buf_t *b = ctx;
    if (b->len + n > b->max)
        return 1;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 16384;
        while (cap < b->len + n + 1)
            cap *= 2;
        uint8_t *p = realloc(b->data, cap);
        if (!p)
            return 1;
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, d, n);
    b->len += n;
    b->data[b->len] = 0;
    return 0;
}

int http_get_buffer(const char *url, const char *headers, size_t max,
                    uint8_t **data, size_t *len, http_info_t *info)
{
    buf_t b = { 0 };
    b.max = max;
    http_req_t req = { 0 };
    req.headers = headers;
    int st = http_request(url, &req, to_buffer, &b, info);
    if (st < 0 || !b.data) {
        free(b.data);
        b.data = calloc(1, 1);
        b.len = 0;
    }
    *data = b.data;
    *len = b.len;
    return st;
}
