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
    char url[512];              /* after redirects (cut short if longer) */
    char location[256];         /* the last Location header seen (a redirect followed or refused) */
    char error[192];            /* when the request itself failed */
    char validator[96];         /* ETag (a strong one), else Last-Modified: for If-Range */
    long long range_start;      /* Content-Range of a 206: first byte, else -1 */
    long long range_total;      /* ... and the whole size (-1 if "*" or none) */
    int ranges;                 /* the answer had "Accept-Ranges: bytes" */
    int reused;                 /* the answer came over a connection kept from before */
} http_info_t;

/* The body in pieces; a non-zero return stops the transfer. */
typedef int (*http_sink_t)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
    const char *method;         /* "GET" (default), "PUT", "POST" */
    const char *headers;        /* extra lines, each ending "\r\n", or NULL */
    const void *body;           /* for PUT / POST */
    size_t body_len;
    uint32_t timeout_ms;        /* per read; 0: 15 s */
    int any_status;             /* the body of any status goes to the sink (an API's
                                 * error messages); else only 2xx bodies do */
    int max_redirects;          /* redirects followed; 0: 5 */
    int same_scheme;            /* 1: a redirect from https to http (or back) is refused */
    /* Called before each connection (the first and each redirect) with the address
     * about to be opened. Non-zero stops the request: it fails with the text put in err. */
    int (*before)(void *ctx, const char *url, char *err, size_t err_len);
    void *before_ctx;
    /* Keep-alive (multitasking stage 4): 1 keeps the connection after a complete answer (a
     * length or chunks, no "Connection: close") and uses it for the next request with
     * keep_alive to the same host, port and scheme. One connection is kept; a request to
     * another host closes it. A connection that the server closed meanwhile is opened again
     * once, when the request could not be sent or no answer byte came (as curl does). */
    int keep_alive;
    /* Range resume (stage 5): range_from > 0 asks for "Range: bytes=N-"; if_range (an ETag
     * or a date, or NULL) is sent as If-Range, so a changed file comes back whole (200). */
    long long range_from;
    const char *if_range;
    /* Called once the final answer's headers are read (not for a redirect), before the body.
     * Non-zero stops the request: it returns with the status and info->error "stopped...". */
    int (*on_headers)(void *ctx, const http_info_t *info);
    void *on_headers_ctx;
} http_req_t;

/* Returns the HTTP status (>= 100), or -1 with info->error set. req may be
 * NULL (a plain GET). */
int http_request(const char *url, const http_req_t *req, http_sink_t sink, void *ctx,
                 http_info_t *info);

/* Closes the connection kept by keep_alive, if any (a cartridge ends, a long pause). */
void http_idle_close(void);

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
