/* web_get for cartridges: see webget.h. */
#include "webget.h"

#include "webfile.h"
#include "webpolicy.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEB_STACK       (128u * 1024u)  /* as the Market's fibers: TLS needs the room */
#define WEB_SLICE_US    4000u
#define PUMP_US         3000u           /* a queued file_write gets this much of each web_poll */
#define PART_HEAD       1280u           /* the part file: this header, then the bytes */
#define PART_STEP       (1u << 20)      /* saved about every 1 MB */
#define PART_MIN        16384u          /* less than this is not worth a file */

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
    if (r->can_resume && r->size - r->persisted >= PART_STEP)
        r->snap_due = 1;
    return 0;
}

/* ---- the part file (stage 5): "BMPART1\n<total>\n<validator>\n<url hash>\n", zeros to PART_HEAD, bytes ---- */

static uint32_t hash_url(const char *s)
{
    uint32_t x = 2166136261u;
    for (; *s; s++)
        x = (x ^ (uint8_t)*s) * 16777619u;
    return x;
}

static void part_path(const web_req_t *r, char *p, size_t n)
{
    snprintf(p, n, "%s/%s", WEB_FILES_DIR, r->part);
}

/* in the fiber, before the request: the bytes of an earlier try become the start of the body */
static void part_load(web_req_t *r)
{
    char path[32];
    part_path(r, path, sizeof path);
    fat_entry_t e;
    if (fat_find(path, &e) || e.is_dir || e.size <= PART_HEAD || e.size - PART_HEAD > r->max)
        return;
    uint8_t *d = NULL;
    size_t n = 0;
    int (*was)(void) = fat_load_tick;
    fat_load_tick = fiber_slice_tick;               /* the frame goes on while the card is read */
    const int rc = fat_load(&e, &d, &n);
    fat_load_tick = was;
    if (rc || n <= PART_HEAD || !d)
        return free(d);
    char *h = (char *)d;
    char *end;
    int ok = memchr(d, 0, PART_HEAD) != NULL && strncmp(h, "BMPART1\n", 8) == 0;
    long long total = ok ? strtoll(h + 8, &end, 10) : 0;
    char *val = ok && *end == '\n' ? end + 1 : NULL;
    char *nl = val ? strchr(val, '\n') : NULL;
    ok = ok && nl && (size_t)(nl - val) < sizeof r->validator && total > (long long)(n - PART_HEAD) &&
         strtoul(nl + 1, NULL, 16) == r->url_hash;
    if (!ok) {
        free(d);
        return;
    }
    *nl = 0;
    snprintf(r->validator, sizeof r->validator, "%s", val);
    r->total = total;
    memmove(d, d + PART_HEAD, n - PART_HEAD);
    r->size = n - PART_HEAD;
    uint8_t *b = realloc(d, r->size + 1);
    r->body = b ? b : d;
    r->body[r->size] = 0;
    r->resumed = r->persisted = r->size;
    r->resume_from = (long long)r->size;
}

/* from web_poll or web_cancel (not in the fiber): what came so far, queued to be written */
static void part_snapshot(web_req_t *r)
{
    if (!r->part[0] || !r->can_resume || r->total <= 0 || !r->body || r->size < PART_MIN || r->size <= r->persisted ||
        (long long)r->size >= r->total)
        return;
    uint8_t *b = calloc(1, PART_HEAD + r->size);
    if (!b)
        return;
    snprintf((char *)b, PART_HEAD, "BMPART1\n%lld\n%s\n%08lx\n", r->total, r->validator, (unsigned long)r->url_hash);
    memcpy(b + PART_HEAD, r->body, r->size);
    char err[96];
    if (webfile_queue_own(r->part, b, PART_HEAD + r->size, (size_t)-1, err, sizeof err) == 0)
        r->persisted = r->size;
}

static void part_forget(web_req_t *r)
{
    char path[32];
    part_path(r, path, sizeof path);
    webfile_forget(r->part);
    fat_entry_t e;
    if (fat_find(path, &e) == 0 && !e.is_dir)
        fat_delete(path);
    r->persisted = 0;
}

/* the request is over (not running any more): the part file is deleted, or saved for later */
static void part_finish(web_req_t *r)
{
    if (!r->part[0])
        return;
    if (r->state == WEB_DONE && r->status >= 200 && r->status < 300)
        part_forget(r);
    else if (r->state != WEB_DONE)
        part_snapshot(r);
}

/* after the headers of the final answer: is it the rest we asked for? */
static int on_headers(void *ctx, const http_info_t *i)
{
    web_req_t *r = ctx;
    const int ok2xx = i->status >= 200 && i->status < 300;
    r->can_resume = 0;
    if (r->part[0] && ok2xx) {
        r->total = i->status == 206 ? i->range_total : i->length;
        snprintf(r->validator, sizeof r->validator, "%s", i->validator);
        r->can_resume = (i->ranges || i->status == 206) && r->total > 0;
    }
    if (r->resume_from <= 0)
        return 0;
    if (i->status == 206 && i->range_start == r->resume_from)
        return 0;                       /* the rest: appended to the bytes we have */
    if (i->status == 416 || i->status == 206) {
        r->restart = 1;                 /* not what we asked: again from the start */
        return 1;
    }
    free(r->body);                      /* 200 (the file changed, or no Range) or an error: its own body */
    r->body = NULL;
    r->size = r->resumed = r->persisted = 0;
    r->resume_from = 0;
    return 0;
}

/* before each connection (the first address and each redirect): the same rules, the pause */
static int before(void *ctx, const char *url, char *err, size_t en)
{
    web_req_t *r = ctx;
    char host[128];
    if (r->lan ? web_lan_url_check(url, err, en) : web_url_check(url, host, sizeof host, err, en))
        return 1;
    if ((r->post || r->lan) && r->hops++) {
        snprintf(err, en, "the server answered with a redirect: %s follows none", r->post ? "a POST" : "a pairing");
        return 1;
    }
    if (r->lan)
        snprintf(host, sizeof host, "%.*s", (int)strcspn(url + 7, ":"), url + 7);
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

/* the POST's copies: the header lines may hold a key, so they are wiped before they are freed */
static void free_post(web_req_t *r)
{
    if (r->headers)
        for (volatile char *p = r->headers; *p; p++)
            *p = 0;
    free(r->headers);
    free(r->post);
    r->headers = r->post = NULL;
    r->post_len = 0;
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
    q.keep_alive = 1;
    q.on_headers = on_headers;
    q.on_headers_ctx = r;
    if (r->post) {
        q.method = "POST";
        q.body = r->post;
        q.body_len = r->post_len;
        q.headers = r->headers;
    }
    if (r->part[0])
        part_load(r);
    int st;
    for (int attempt = 0;; attempt++) {
        q.range_from = r->resume_from;
        q.if_range = r->validator[0] ? r->validator : NULL;
        st = http_request(r->url, &q, sink, r, &info);
        if (!r->restart || attempt)
            break;
        free(r->body);                  /* the range was refused: forget the part, ask for the whole */
        r->body = NULL;
        r->size = r->resumed = r->persisted = 0;
        r->resume_from = 0;
        r->restart = 0;
        r->validator[0] = 0;
    }
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
    free_post(r);
}

static void free_slot(web_req_t *r)
{
    free_post(r);
    free(r->body);
    memset(r, 0, sizeof *r);
}

static void end_fiber(void)
{
    free(fib_stack);
    fib_stack = NULL;
    running = NULL;
}

/* the checks, then a free slot cleared (not yet running, its id still 0), or NULL and why */
static web_req_t *claim(const char *url, int lan, char *err, size_t en)
{
    char host[128];
    if (lan ? web_lan_url_check(url, err, en) : web_url_check(url, host, sizeof host, err, en))
        return NULL;
    if (fiber_current()) {
        snprintf(err, en, "web_get cannot start from a background job");
        return NULL;
    }
    if (running) {
        snprintf(err, en, "a request is running: poll it or cancel it first");
        return NULL;
    }
    web_req_t *r = NULL;
    for (int i = 0; i < WEB_SLOTS && !r; i++)
        if (!slots[i].id)
            r = &slots[i];
    if (!r) {
        snprintf(err, en, "no free slot: web_cancel an old result first");
        return NULL;
    }
    memset(r, 0, sizeof *r);
    r->length = -1;
    r->total = -1;
    r->url_hash = hash_url(url);
    snprintf(r->url, sizeof r->url, "%s", url);
    return r;
}

/* the claimed slot gets its id and its fiber: the request runs from the next web_poll */
static int launch(web_req_t *r, size_t max, uint32_t timeout_ms, char *err, size_t en)
{
    if (!(fib_stack = malloc(WEB_STACK))) {
        free_slot(r);
        snprintf(err, en, "not enough memory");
        return -1;
    }
    r->id = ++next_id > 0 ? next_id : (next_id = 1);
    r->state = WEB_BUSY;
    r->max = max;
    r->timeout_ms = timeout_ms;
    running = r;
    fiber_prepare(&fib, fib_stack, WEB_STACK, run, r);
    return r->id;
}

int web_start(const char *url, size_t max, uint32_t timeout_ms, char *err, size_t en)
{
    return web_start_resume(url, max, timeout_ms, NULL, err, en);
}

int web_start_resume(const char *url, size_t max, uint32_t timeout_ms, const char *stem, char *err, size_t en)
{
    char part[13] = "";
    if (stem) {
        char name[13];
        if (web_file_name(stem, name, err, en))
            return -1;
        const char *dot = strchr(name, '.');
        snprintf(part, sizeof part, "%.*s.prt", (int)(dot ? (size_t)(dot - name) : strlen(name)), name);
    }
    web_req_t *r = claim(url, 0, err, en);
    if (!r)
        return -1;
    snprintf(r->part, sizeof r->part, "%s", part);
    return launch(r, max, timeout_ms, err, en);
}

int web_start_post(const char *url, const char *headers, const void *body, size_t body_len, size_t max,
                   uint32_t timeout_ms, char *err, size_t en)
{
    web_req_t *r = claim(url, 0, err, en);
    if (!r)
        return -1;
    const size_t hl = headers ? strlen(headers) : 0;
    r->headers = malloc(hl + 1);
    r->post = malloc(body_len ? body_len : 1);
    if (!r->headers || !r->post) {
        free_slot(r);
        snprintf(err, en, "not enough memory");
        return -1;
    }
    memcpy(r->headers, headers ? headers : "", hl + 1);
    if (body_len)
        memcpy(r->post, body, body_len);
    r->post_len = body_len;
    return launch(r, max, timeout_ms, err, en);
}

int web_start_lan(const char *url, size_t max, uint32_t timeout_ms, char *err, size_t en)
{
    web_req_t *r = claim(url, 1, err, en);
    if (!r)
        return -1;
    r->lan = 1;
    return launch(r, max, timeout_ms, err, en);
}

web_state_t web_poll(int id)
{
    if (webfile_busy())
        webfile_pump(PUMP_US);              /* a queued file_write goes on while the cartridge polls */
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
            part_finish(r);
        } else if (r->snap_due) {
            r->snap_due = 0;
            part_snapshot(r);
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
        if (r->state == WEB_WAIT || r->state == WEB_BUSY)
            r->state = WEB_ERROR;
        part_finish(r);
    }
    free_slot(r);
}

void web_reset(void)
{
    for (int i = 0; i < WEB_SLOTS; i++)
        if (slots[i].id)
            web_cancel(slots[i].id);
    webfile_reset();                    /* the part files and the queued file_write calls reach the card */
    http_idle_close();
}
