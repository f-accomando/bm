/* web_get for cartridges: see webget.h. */
#include "webget.h"

#include "webpolicy.h"
#include "drivers/timer.h"
#include "kernel/fiber.h"
#include "net/http.h"
#include "net/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEB_STACK       (128u * 1024u)  /* as the Market's fibers: TLS needs the room */
#define WEB_SLICE_US    4000u

static web_req_t slots[WEB_SLOTS];
static int next_id;
static web_req_t *running;              /* the slot whose fiber is alive, or NULL */
static fiber_t fib;
static void *fib_stack;

static web_req_t *slot_of(int id)
{
    for (int i = 0; i < WEB_SLOTS; i++)
        if (id > 0 && slots[i].id == id)
            return &slots[i];
    return NULL;
}

const web_req_t *web_find(int id)
{
    return slot_of(id);
}

static int sink(void *ctx, const uint8_t *d, size_t n)
{
    web_req_t *r = ctx;
    if (r->size + n > r->max) {
        r->too_big = 1;
        return 1;
    }
    uint8_t *b = realloc(r->body, r->size + n + 1);
    if (!b)
        return 1;
    r->body = b;
    memcpy(b + r->size, d, n);
    r->size += n;
    b[r->size] = 0;
    return 0;
}

/* before each connection (the first address and each redirect): the same rules, the pause */
static int before(void *ctx, const char *url, char *err, size_t en)
{
    web_req_t *r = ctx;
    char host[128];
    if (web_url_check(url, host, sizeof host, err, en))
        return 1;
    for (;;) {
        const uint32_t left = web_gap_left_us(host, timer_ticks());
        if (!left)
            break;
        r->state = WEB_WAIT;
        if (net_wait_step() < 0) {
            snprintf(err, en, "cancelled");
            return 1;
        }
    }
    r->state = WEB_BUSY;
    web_gap_mark(host, timer_ticks());
    return 0;
}

static void run(void *arg)
{
    web_req_t *r = arg;
    http_req_t q;
    http_info_t info;
    memset(&q, 0, sizeof q);
    q.any_status = 1;
    q.max_redirects = WEB_MAX_REDIRECTS;
    q.same_scheme = 1;
    q.before = before;
    q.before_ctx = r;
    q.timeout_ms = r->timeout_ms;
    const int st = http_request(r->url, &q, sink, r, &info);
    snprintf(r->url, sizeof r->url, "%s", info.url);
    snprintf(r->type, sizeof r->type, "%s", info.type);
    snprintf(r->location, sizeof r->location, "%s", info.location);
    r->length = info.length;
    r->status = info.status > 0 ? info.status : 0;
    if (r->too_big) {
        snprintf(r->error, sizeof r->error, "the answer is over the cap of %lu bytes", (unsigned long)r->max);
        free(r->body);
        r->body = NULL;
        r->size = 0;
        r->state = WEB_ERROR;
    } else if (st < 0) {
        snprintf(r->error, sizeof r->error, "%s", info.error[0] ? info.error : "the request failed");
        r->state = WEB_ERROR;
    } else {
        r->state = WEB_DONE;
    }
}

static void free_slot(web_req_t *r)
{
    free(r->body);
    memset(r, 0, sizeof *r);
}

static void end_fiber(void)
{
    free(fib_stack);
    fib_stack = NULL;
    running = NULL;
}

int web_start(const char *url, size_t max, uint32_t timeout_ms, char *err, size_t en)
{
    char host[128];
    if (web_url_check(url, host, sizeof host, err, en))
        return -1;
    if (fiber_current()) {
        snprintf(err, en, "web_get cannot start from a background job");
        return -1;
    }
    if (running) {
        snprintf(err, en, "a request is running: poll it or cancel it first");
        return -1;
    }
    web_req_t *r = NULL;
    for (int i = 0; i < WEB_SLOTS && !r; i++)
        if (!slots[i].id)
            r = &slots[i];
    if (!r) {
        snprintf(err, en, "no free slot: web_cancel an old result first");
        return -1;
    }
    memset(r, 0, sizeof *r);
    if (!(fib_stack = malloc(WEB_STACK))) {
        snprintf(err, en, "not enough memory");
        return -1;
    }
    r->id = ++next_id > 0 ? next_id : (next_id = 1);
    r->state = WEB_BUSY;
    r->max = max;
    r->length = -1;
    r->timeout_ms = timeout_ms;
    snprintf(r->url, sizeof r->url, "%s", url);
    running = r;
    fiber_prepare(&fib, fib_stack, WEB_STACK, run, r);
    return r->id;
}

web_state_t web_poll(int id)
{
    web_req_t *r = slot_of(id);
    if (!r)
        return WEB_FREE;
    if (r == running) {
        const uint32_t until = timer_ticks() + WEB_SLICE_US;
        int more;
        do
            more = fiber_run(&fib, until);
        while (more && (int32_t)(until - timer_ticks()) > 0);
        if (!more) {
            end_fiber();
            if (r->state == WEB_WAIT || r->state == WEB_BUSY) {
                snprintf(r->error, sizeof r->error, "cancelled");
                r->state = WEB_ERROR;
            }
        }
    }
    return r->state;
}

void web_cancel(int id)
{
    web_req_t *r = slot_of(id);
    if (!r)
        return;
    if (r == running) {
        fiber_cancel(&fib);
        while (fiber_run(&fib, 0))
            ;
        end_fiber();
    }
    free_slot(r);
}

void web_reset(void)
{
    for (int i = 0; i < WEB_SLOTS; i++)
        if (slots[i].id)
            web_cancel(slots[i].id);
}
