/*
 * Host test of the web API for cartridges (src/bm/webpolicy.c, webget.c, webimg.c) over POSIX
 * sockets, against tests/web/run_webget_test.py. The kernel's fibers are replaced by ucontext
 * ones, and the TLS transport by plain sockets (web_policy_allow_http): the https rules are
 * checked on the address text, the TLS code is tested by test-https.
 *
 *   test_webget PORT FIXTURES_DIR
 */
#define _GNU_SOURCE
#include "webget.h"
#include "webimg.h"
#include "webpolicy.h"
#include "kernel/fiber.h"
#include "net/http.h"
#include "net/net.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...)                                                                                  \
    do {                                                                                               \
        int ok_ = (c);                                                                                 \
        printf("%s ", ok_ ? "ok  " : "FAIL");                                                          \
        printf(__VA_ARGS__);                                                                           \
        printf("\n");                                                                                  \
        if (!ok_)                                                                                      \
            fails++;                                                                                   \
    } while (0)

/* ---- the kernel's time and fibers, on the PC ---- */

uint32_t timer_ticks(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000000u + (uint32_t)t.tv_nsec / 1000u);
}

static fiber_t *cur;
static ucontext_t uc_f, uc_m;

static void tramp(void)
{
    fiber_t *f = cur;
    f->fn(f->arg);
    f->done = 1;
}

void fiber_prepare(fiber_t *f, void *stack, size_t size, void (*fn)(void *), void *arg)
{
    memset(f, 0, sizeof *f);
    f->stack = stack;
    f->size = size;
    f->fn = fn;
    f->arg = arg;
    getcontext(&uc_f);
    uc_f.uc_stack.ss_sp = stack;
    uc_f.uc_stack.ss_size = size;
    uc_f.uc_link = &uc_m;
    makecontext(&uc_f, tramp, 0);
}

int fiber_resume(fiber_t *f)
{
    if (f->done)
        return 0;
    cur = f;
    swapcontext(&uc_m, &uc_f);
    cur = NULL;
    return !f->done;
}

void fiber_yield(void) { swapcontext(&uc_f, &uc_m); }
fiber_t *fiber_current(void) { return cur; }
void fiber_cancel(fiber_t *f) { f->cancel = 1; }
int fiber_cancelled(void) { return cur && cur->cancel; }
int fiber_run(fiber_t *f, uint32_t until)
{
    f->until = until;
    return fiber_resume(f);
}

int net_wait_step(void)
{
    usleep(300);
    if (cur) {
        fiber_yield();
        return fiber_cancelled() ? -1 : 0;
    }
    return 0;
}

/* ---- plain sockets as the HTTP transport; the read waits as stream.c does ---- */

static void *p_open(const char *host, uint16_t port, int tls, char *err, size_t en)
{
    if (tls) {
        snprintf(err, en, "no tls here");
        return NULL;
    }
    struct addrinfo hints = { 0 }, *ai;
    hints.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host, ps, &hints, &ai)) {
        snprintf(err, en, "%s: unknown name", host);
        return NULL;
    }
    int fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen)) {
        snprintf(err, en, "connection refused");
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
    const uint32_t t0 = timer_ticks();
    for (;;) {
        struct pollfd pf = { *(int *)c, POLLIN, 0 };
        if (poll(&pf, 1, 0) > 0)
            return (int)recv(*(int *)c, b, n, 0);
        if ((timer_ticks() - t0) / 1000 >= ms)
            return -1;
        if (net_wait_step() < 0)
            return -1;
    }
}

static void p_close(void *c)
{
    close(*(int *)c);
    free(c);
}

static const http_transport_t posix = { p_open, p_write, p_read, p_close };
const http_transport_t *http_transport = &posix;

/* ---- helpers ---- */

static char base[64];

typedef struct {
    web_state_t state;
    int polls, saw_wait;
    web_req_t r;
    char body[8192];
    uint32_t took_ms;
} result_t;

/* start, poll like a cartridge (once per "frame"), copy the result, free the slot */
static int fetch(const char *path, size_t max, result_t *out)
{
    char url[256], err[200];
    snprintf(url, sizeof url, "%s%s", base, path);
    memset(out, 0, sizeof *out);
    const uint32_t t0 = timer_ticks();
    const int id = web_start(url, max, 15000, err, sizeof err);
    if (id < 0) {
        printf("     start: %s\n", err);
        return -1;
    }
    for (;;) {
        out->state = web_poll(id);
        out->polls++;
        if (out->state == WEB_WAIT)
            out->saw_wait = 1;
        if (out->state == WEB_DONE || out->state == WEB_ERROR || out->state == WEB_FREE)
            break;
        usleep(1000);                   /* the rest of the frame */
        if (timer_ticks() - t0 > 20u * 1000000u)
            break;
    }
    out->took_ms = (timer_ticks() - t0) / 1000;
    const web_req_t *r = web_find(id);
    if (r) {
        out->r = *r;
        if (r->body && r->size < sizeof out->body)
            memcpy(out->body, r->body, r->size + 1);
    }
    web_cancel(id);
    return id;
}

static uint8_t *slurp(const char *dir, const char *name, size_t *n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) {
        printf("     cannot open %s\n", p);
        *n = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n ? *n : 1);
    if (fread(b, 1, *n, f) != *n)
        *n = 0;
    fclose(f);
    return b;
}

static void test_policy(void)
{
    char host[128], err[200];
    web_policy_allow_http(0);
    CHECK(web_url_check("https://poly.pizza/search/tree", host, sizeof host, err, sizeof err) == 0 &&
              strcmp(host, "poly.pizza") == 0, "https url accepted, host poly.pizza");
    CHECK(web_url_check("https://Static.Poly.Pizza:8443/a?b=c", host, sizeof host, err, sizeof err) == 0 &&
              strcmp(host, "static.poly.pizza") == 0, "host in lower case, a port is fine");
    CHECK(web_url_check("https://example.org", host, sizeof host, err, sizeof err) == 0, "no path is fine");
    static const char *const bad[] = {
        "http://example.org/", "ftp://example.org/", "HTTPS://example.org/", "//example.org/", "example.org",
        "https://", "https:///x", "https://user@example.org/", "https://exa mple.org/", "https://example.org/a b",
        "https://example.org/#top", "https://example.org:0/", "https://example.org:99999/",
        "https://example.org:80a/", "https://-example.org/", "https://example..org/", "https://exa_mple.org/",
        "https://example.org/\x01", "https://example.org/\xc3\xa9", "https://example.org\\x", "file:///etc/passwd", "",
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(web_url_check(bad[i], host, sizeof host, err, sizeof err) != 0 && err[0], "refused \"%s\": %s", bad[i], err);
    char longurl[WEB_URL_MAX + 40] = "https://example.org/";
    memset(longurl + 20, 'a', WEB_URL_MAX);
    longurl[20 + WEB_URL_MAX] = 0;
    CHECK(web_url_check(longurl, host, sizeof host, err, sizeof err) != 0, "an address over 1024 characters is refused");
    web_policy_allow_http(1);
    CHECK(web_url_check("http://127.0.0.1:80/", host, sizeof host, err, sizeof err) == 0, "test switch: http accepted");
    web_policy_allow_http(0);
    CHECK(web_url_check("http://127.0.0.1:80/", host, sizeof host, err, sizeof err) != 0, "test switch off: http refused");

    char n[13];
    static const char *const good[] = { "MODEL.GLB", "model.glb", "bm/web/a.txt", "/bm/web/A.TXT", "web/x.png", "NOEXT",
                                        "a-b_c.d", "12345678.123", "BM/WEB/x.y" };
    for (size_t i = 0; i < sizeof good / sizeof *good; i++)
        CHECK(web_file_name(good[i], n, err, sizeof err) == 0, "file name \"%s\" -> %s", good[i], n);
    CHECK(web_file_name("model.glb", n, err, sizeof err) == 0 && strcmp(n, "MODEL.GLB") == 0, "upper case");
    static const char *const badf[] = {
        "../x.txt", "..", "a/../b.txt", "/etc/passwd", "/kernel.img", "/bm/config.txt", "carts/x.txt", "bm/web/sub/x.txt",
        "bm/web/../x.txt", "x\\y.txt", ".hidden", "game.bm", "GAME.BME", "game.b16", "Game.Bm", "toolongname1.txt",
        "name.toolong", "a.b.c", "sp ace.txt", "", "dir/", "bm/web/", "name.", "bm/web//x.txt",
    };
    for (size_t i = 0; i < sizeof badf / sizeof *badf; i++)
        CHECK(web_file_name(badf[i], n, err, sizeof err) != 0 && err[0], "file refused \"%s\": %s", badf[i], err);

    web_gap_reset();
    CHECK(web_gap_left_us("a.org", 5000) == 0, "gap: a host never seen waits 0");
    web_gap_mark("a.org", 5000);
    CHECK(web_gap_left_us("a.org", 5000) == WEB_MIN_GAP_US, "gap: a full second right after");
    CHECK(web_gap_left_us("A.ORG", 505000) == WEB_MIN_GAP_US - 500000, "gap: half a second later, case ignored");
    CHECK(web_gap_left_us("a.org", 5000 + WEB_MIN_GAP_US) == 0, "gap: free after 1 s");
    CHECK(web_gap_left_us("b.org", 5000) == 0, "gap: another host is free");
    web_gap_mark("a.org", 0xFFFFFF00u);
    CHECK(web_gap_left_us("a.org", 0xFFFFFF00u + 100000u) == WEB_MIN_GAP_US - 100000u, "gap: across the counter wrap");
    for (int i = 0; i < 20; i++) {
        char h[32];
        snprintf(h, sizeof h, "h%d.org", i);
        web_gap_mark(h, 77);
    }
    CHECK(web_gap_left_us("h19.org", 77) == WEB_MIN_GAP_US, "gap: the table keeps the newest hosts");
    web_gap_reset();

    CHECK(web_cap_bytes(NULL, 4096, 32768) == 4096u * 1024, "cap: default 4 MiB");
    CHECK(web_cap_bytes("1024", 4096, 32768) == 1024u * 1024, "cap: from the config");
    CHECK(web_cap_bytes("999999", 4096, 32768) == 32768u * 1024, "cap: kept under the limit");
    CHECK(web_cap_bytes("abc", 4096, 32768) == 4096u * 1024 && web_cap_bytes("0", 4096, 32768) == 4096u * 1024, "cap: bad text is the default");
}

static void test_web(void)
{
    result_t r;
    web_policy_allow_http(1);
    web_gap_reset();
    fetch("/hello", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.status == 200 && r.r.size == 10 && memcmp(r.body, "hello, bm\n", 10) == 0,
          "GET 200: %d bytes, status %d", (int)r.r.size, r.r.status);
    CHECK(strncmp(r.r.type, "text/plain", 10) == 0 && r.r.length == 10, "headers: type \"%s\", length %lld", r.r.type, r.r.length);

    web_gap_reset();
    fetch("/nothere", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.status == 404 && strcmp(r.body, "not here") == 0, "GET 404 keeps status and body (%d)", r.r.status);

    web_gap_reset();
    fetch("/chunked", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.size == 3000 && r.r.length == -1, "chunked body: %d bytes", (int)r.r.size);

    web_gap_reset();
    fetch("/r1", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.status == 200 && strstr(r.r.url, "/hello"), "3 redirects are followed (url %s)", r.r.url);

    web_gap_reset();
    fetch("/r0", 1 << 20, &r);
    CHECK(r.state == WEB_ERROR && strstr(r.r.error, "too many redirects"), "4 redirects are refused: %s", r.r.error);

    web_gap_reset();
    fetch("/to-https", 1 << 20, &r);
    CHECK(r.state == WEB_ERROR && strstr(r.r.error, "another scheme") && r.r.status == 302 &&
              strncmp(r.r.location, "https://", 8) == 0,
          "redirect to another scheme refused, location kept: %s / %s", r.r.error, r.r.location);

    web_gap_reset();
    fetch("/redirect-rel", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.status == 200, "a relative redirect works");

    web_gap_reset();
    fetch("/big", 1u << 20, &r);
    CHECK(r.state == WEB_ERROR && strstr(r.r.error, "cap") && r.r.body == NULL, "a 3 MB body over a 1 MB cap: %s", r.r.error);
    web_gap_reset();
    fetch("/big", 4u << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.size == (3u << 20), "the same body under a 4 MB cap: %d bytes", (int)r.r.size);

    web_gap_reset();
    fetch("/slow", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.r.size == 3000 && r.polls > 5, "slow body arrives over %d polls", r.polls);

    /* the pause of 1 s per host */
    web_gap_reset();
    fetch("/hello", 1 << 20, &r);
    fetch("/hello", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.saw_wait && r.took_ms >= 900 && r.took_ms < 3000,
          "second request to the same host waits: %u ms, state wait seen", r.took_ms);

    /* a redirect to the same host also waits */
    web_gap_reset();
    fetch("/r3", 1 << 20, &r);
    CHECK(r.state == WEB_DONE && r.took_ms >= 900, "the redirect hop waits for the pause too: %u ms", r.took_ms);

    /* cancel in the middle, then a new request */
    web_gap_reset();
    char err[200], url[256];
    snprintf(url, sizeof url, "%s/slow", base);
    int id = web_start(url, 1 << 20, 15000, err, sizeof err);
    CHECK(id > 0, "started /slow");
    for (int i = 0; i < 20; i++) {
        web_poll(id);
        usleep(1000);
    }
    CHECK(web_poll(id) == WEB_BUSY, "still busy after 20 polls");
    snprintf(url, sizeof url, "%s/hello", base);
    CHECK(web_start(url, 1 << 20, 15000, err, sizeof err) < 0 && strstr(err, "running"), "second start while running refused: %s", err);
    web_cancel(id);
    CHECK(web_find(id) == NULL && web_poll(id) == WEB_FREE, "cancel frees the slot");
    web_gap_reset();
    fetch("/hello", 1 << 20, &r);
    CHECK(r.state == WEB_DONE, "a new request after the cancel works");

    /* slots */
    web_gap_reset();
    int ids[WEB_SLOTS + 1];
    snprintf(url, sizeof url, "%s/hello", base);
    for (int i = 0; i < WEB_SLOTS; i++) {
        web_gap_reset();
        ids[i] = web_start(url, 1 << 20, 15000, err, sizeof err);
        while (ids[i] > 0 && web_poll(ids[i]) != WEB_DONE)
            usleep(500);
    }
    CHECK(web_start(url, 1 << 20, 15000, err, sizeof err) < 0 && strstr(err, "no free slot"), "all %d slots used: %s", WEB_SLOTS, err);
    CHECK(web_find(ids[0]) && web_find(ids[0])->size == 10, "a finished result stays until web_cancel");
    web_reset();
    CHECK(web_find(ids[0]) == NULL && web_find(ids[1]) == NULL, "web_reset frees everything");

    /* bad addresses never reach the network */
    web_policy_allow_http(0);
    CHECK(web_start("http://127.0.0.1/", 1000, 1000, err, sizeof err) < 0, "http:// refused by web_start: %s", err);
    web_policy_allow_http(1);
    web_gap_reset();
}

static int px_near(uint16_t v, int r, int g, int b)
{
    const int pr = ((v >> 11) & 31) << 3, pg = ((v >> 5) & 63) << 2, pb = (v & 31) << 3;
    return abs(pr - r) <= 24 && abs(pg - g) <= 24 && abs(pb - b) <= 24;
}

static void test_image(const char *dir)
{
    size_t n;
    char err[100];
    webimg_t im;
    uint8_t *d = slurp(dir, "block_32x24.jpg", &n);
    CHECK(d && strcmp(webimg_kind(d, n), "jpeg") == 0, "kind jpeg");
    CHECK(webimg_decode(d, n, 0, 0, &im, err, sizeof err) == 0 && im.w == 32 && im.h == 24, "jpeg decodes 32x24");
    CHECK(px_near(im.px[10 * 32 + 2], 255, 0, 0) && px_near(im.px[10 * 32 + 16], 0, 200, 0) &&
              px_near(im.px[5 * 32 + 28], 0, 0, 255) && im.alpha[0] == 1,
          "jpeg pixels: red, green, blue, opaque");
    webimg_free(&im);
    CHECK(webimg_decode(d, n, 16, 10, &im, err, sizeof err) == 0 && im.w == 16 && im.h == 10 && px_near(im.px[3 * 16 + 2], 255, 0, 0),
          "jpeg cut to 16x10");
    webimg_free(&im);
    CHECK(webimg_decode(d, n / 2, 0, 0, &im, err, sizeof err) != 0 || im.w > 0, "a truncated jpeg does not crash (%s)", err);
    webimg_free(&im);
    free(d);

    d = slurp(dir, "block_prog.jpg", &n);
    CHECK(webimg_decode(d, n, 0, 0, &im, err, sizeof err) != 0 && strstr(err, "progressive"), "progressive jpeg refused: %s", err);
    free(d);

    d = slurp(dir, "alpha_16x8.png", &n);
    CHECK(strcmp(webimg_kind(d, n), "png") == 0, "kind png");
    CHECK(webimg_decode(d, n, 0, 0, &im, err, sizeof err) == 0 && im.w == 16 && im.h == 8, "png decodes 16x8");
    CHECK(px_near(im.px[0], 255, 0, 255) && im.alpha[0] == 1 && px_near(im.px[3 * 16 + 3], 255, 255, 0) && im.alpha[3 * 16 + 3] == 1 &&
              im.alpha[3 * 16 + 12] == 0,
          "png pixels: magenta, yellow, a transparent half");
    webimg_free(&im);
    /* a PNG that claims 3000x3000 is refused before any decoding */
    uint8_t big[64];
    memcpy(big, d, n < sizeof big ? n : sizeof big);
    big[18] = 0x0B; big[19] = 0xB8; big[22] = 0x0B; big[23] = 0xB8;
    CHECK(webimg_decode(big, sizeof big, 0, 0, &im, err, sizeof err) != 0 && strstr(err, "megapixels"), "a 9 megapixel png is refused: %s", err);
    free(d);

    static const char *const webps[] = { "lossless_64x48.webp", "lossy_q75_64x48.webp" };
    for (int i = 0; i < 2; i++) {
        char fx[256];
        snprintf(fx, sizeof fx, "%s/../../webp/fixtures", dir);
        d = slurp(fx, webps[i], &n);
        CHECK(d && strcmp(webimg_kind(d, n), "webp") == 0, "kind webp (%s)", webps[i]);
        CHECK(webimg_decode(d, n, 0, 0, &im, err, sizeof err) == 0 && im.w == 64 && im.h == 48, "%s decodes 64x48", webps[i]);
        webimg_free(&im);
        CHECK(webimg_decode(d, n, 20, 10, &im, err, sizeof err) == 0 && im.w == 20 && im.h == 10, "%s cut to 20x10", webps[i]);
        webimg_free(&im);
        CHECK(webimg_decode(d, n / 2, 0, 0, &im, err, sizeof err) != 0, "%s truncated: %s", webps[i], err);
        free(d);
    }
    CHECK(webimg_decode((const uint8_t *)"hello world, not a picture", 27, 0, 0, &im, err, sizeof err) != 0 && strstr(err, "not a JPEG"), "text is refused: %s", err);
    CHECK(webimg_decode((const uint8_t *)"", 0, 0, 0, &im, err, sizeof err) != 0, "empty is refused");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_webget PORT FIXTURES\n");
        return 2;
    }
    snprintf(base, sizeof base, "http://127.0.0.1:%s", argv[1]);
    test_policy();
    test_image(argv[2]);
    test_web();
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
