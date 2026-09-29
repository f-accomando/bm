/*
 * HTTP/1.1 client (M19): GET and PUT, redirects, Content-Length or
 * chunked bodies, the body handed to a callback as it arrives. http:// on
 * a stream; https:// through TLS (tls.c) once it is in.
 */
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int status;                 /* 200, 404... */
    long long length;           /* Content-Length, -1 if not given */
    uint32_t received;          /* body bytes */
    char type[64];              /* Content-Type */
    char url[512];              /* after redirects */
    char error[96];             /* when the request itself failed */
} http_info_t;

/* The body in pieces; a non-zero return stops the transfer. */
typedef int (*http_sink_t)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
    const char *method;         /* "GET" (default), "PUT", "POST" */
    const char *headers;        /* extra lines, each ending "\r\n", or NULL */
    const void *body;           /* for PUT / POST */
    size_t body_len;
    uint32_t timeout_ms;        /* per read; 0: 15 s */
} http_req_t;

/* Returns the HTTP status (>= 100), or -1 with info->error set. req may be
 * NULL (a plain GET). */
int http_request(const char *url, const http_req_t *req, http_sink_t sink, void *ctx,
                 http_info_t *info);

/* A GET into a malloc'd buffer (NUL-terminated, at most max bytes); the
 * caller frees *data. */
int http_get_buffer(const char *url, const char *headers, size_t max,
                    uint8_t **data, size_t *len, http_info_t *info);

/* The transport under HTTP: plain streams, or TLS for https. */
typedef struct {
    void *(*open)(const char *host, uint16_t port, int tls, char *err, size_t err_len);
    int  (*write)(void *c, const void *data, size_t len);
    int  (*read)(void *c, void *buf, size_t len, uint32_t timeout_ms);
    void (*close)(void *c);
} http_transport_t;

extern const http_transport_t *http_transport;

#endif
