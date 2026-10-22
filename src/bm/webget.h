/*
 * web_get for cartridges: one HTTPS GET at a time, run in a fiber that the cartridge
 * advances with web_poll (a few milliseconds each call), so the game goes on drawing
 * while the page arrives. The rules are in webpolicy.h (https only, 3 redirects of the
 * same scheme, a pause of 1 s per host, a cap on the body). The result stays in a slot
 * until web_cancel or the end of the cartridge. The TLS handshake of a new connection
 * does not give the CPU back: one frame is long (menu stalls: line in the log).
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
} web_req_t;

/* Starts a request: its id (> 0), or -1 with the reason in err (bad address, one request
 * already running, no free slot, no memory, called from a fiber). max: the cap in bytes. */
int web_start(const char *url, size_t max, uint32_t timeout_ms, char *err, size_t en);
/* Runs the request a little (about 4 ms): its state, WEB_FREE for an id that is not there. */
web_state_t web_poll(int id);
/* The slot of id, or NULL. */
const web_req_t *web_find(int id);
/* Stops the request if it runs and frees the slot and its body. */
void web_cancel(int id);
/* Every request stopped and freed (the cartridge ends). The pause per host is kept. */
void web_reset(void);

#endif
