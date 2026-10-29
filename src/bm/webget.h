/*
 * web_get for cartridges: one HTTPS GET at a time, run in a fiber that the cartridge
 * advances with web_poll (a few milliseconds each call), so the game goes on drawing
 * while the page arrives. The rules are in webpolicy.h (https only, 3 redirects of the
 * same scheme, a pause of 1 s per host, a cap on the body). The result stays in a slot
 * until web_cancel or the end of the cartridge. The TLS handshake of a new connection
 * does not give the CPU back: one frame is long (menu stalls: line in the log).
 * One connection is kept between two requests to the same host (keep-alive, stage 4).
 * A request may resume (stage 5): the bytes that came are kept on the SD card in
 * bm/web/NAME.PRT when the request fails or is cancelled, and the next request for the same
 * address asks the server for the rest ("Range"); see web_start_resume.
 */
#ifndef WEBGET_H
#define WEBGET_H

#include <stddef.h>
#include <stdint.h>

#define WEB_SLOTS 3

typedef enum { WEB_FREE, WEB_WAIT, WEB_BUSY, WEB_DONE, WEB_ERROR } web_state_t;

typedef struct {
    int id;                     /* > 0 while the slot is used */
    web_state_t state;          /* WEB_WAIT: the pause per host; WEB_BUSY: connecting or reading */
    int status;                 /* HTTP status, 0 if none came */
    long long length;           /* Content-Length, -1 if not given */
    char type[64];              /* Content-Type */
    char location[256];         /* Location header, if the last answer had one */
    char url[512];              /* the address as asked, then the one reached */
    char error[192];            /* when state is WEB_ERROR */
    uint8_t *body;              /* NUL-terminated after size bytes; NULL until bytes came */
    size_t size;                /* bytes received so far */
    size_t max;                 /* the cap */
    int too_big;                /* the body went over the cap */
    uint32_t timeout_ms;
    /* resume (stage 5) */
    char part[13];              /* "name.prt" in bm/web, or "" for a request that does not resume */
    size_t resumed;             /* bytes that came from the part file */
    size_t persisted;           /* bytes in the last part file saved */
    long long total;            /* the whole size, if the server said (206 or Content-Length), else -1 */
    char validator[96];         /* ETag or Last-Modified of the answer */
    int can_resume;             /* the server accepts "Range" (Accept-Ranges or 206) and the size is known */
    int snap_due;               /* the sink asks for the part file to be saved (done in web_poll) */
    long long resume_from;      /* the "Range" that was sent, 0 if none */
    int restart;                /* the server refused the range: start again from byte 0 */
    uint32_t url_hash;          /* of the address as asked: the part file belongs to it */
    /* a POST (ai_ask): the body and the extra header lines (the key: wiped when the slot ends) */
    char *post;
    size_t post_len;
    char *headers;
    int hops;                   /* connections opened: a POST follows no redirect */
    int lan;                    /* ai_pair's plain-http GET of a relay's ca.pem on the LAN (web_start_lan) */
} web_req_t;

/* Starts a request: its id (> 0), or -1 with the reason in err (bad address, one request
 * already running, no free slot, no memory, called from a fiber). max: the cap in bytes. */
int web_start(const char *url, size_t max, uint32_t timeout_ms, char *err, size_t en);
/* The same, resuming: stem is a bare 8.3 name of bm/web ("model.glb"); its part file is "model.prt"
 * (the same stem and "PRT"). If that file holds the start of the same address, the request asks for
 * the rest; the answer is the whole file in the body either way. The part file is saved when the
 * request fails (a lost connection, a timeout) or is cancelled, and about every 1 MB, only if
 * the server said it accepts "Range" and the size; it is deleted when the request is done with a
 * 2xx. The caller checks the whole file (a SHA-256 or its size). */
int web_start_resume(const char *url, size_t max, uint32_t timeout_ms, const char *stem, char *err, size_t en);
/* A POST of body (copied) with extra header lines (each ending "\r\n", copied; they may hold a key:
 * the copy is wiped when the request ends). No redirect is followed: an answer 3xx is the result.
 * No resume. The rest as web_start. */
int web_start_post(const char *url, const char *headers, const void *body, size_t body_len, size_t max,
                   uint32_t timeout_ms, char *err, size_t en);
/* ai_pair's only plain-http request: a GET of "http://<LAN address>:PORT/ca.pem" (web_lan_url_check), no
 * redirect. The caller checks the certificate that comes back against the user's pairing code. */
int web_start_lan(const char *url, size_t max, uint32_t timeout_ms, char *err, size_t en);
/* Runs the request a little (about 4 ms): its state, WEB_FREE for an id that is not there. */
web_state_t web_poll(int id);
/* The slot of id, or NULL. */
const web_req_t *web_find(int id);
/* Stops the request if it runs and frees the slot and its body. */
void web_cancel(int id);
/* Every request stopped and freed (the cartridge ends). The pause per host is kept. */
void web_reset(void);

#endif
