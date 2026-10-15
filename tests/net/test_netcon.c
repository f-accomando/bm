/*
 * Host test of the network console (src/net/netcon.c) on lwIP with its
 * loopback interface: a client connects to 127.0.0.1:3333, gets the
 * greeting, fails and passes the password, sees what kprintf prints, and
 * its keys come out of netcon_getc; a second client takes over (a lost
 * connection must not lock the console).
 * Then the transfers (src/net/netxfer.c): a file saved on the "SD card",
 * a wrong password, a damaged file, a cartridge to play, a kernel. A file
 * (S) is answered QD once checked and written later by netxfer_write_tick
 * (the menu's fiber, run straight through here): one waits at a time.
 * The kernel (K), the last case, three ways, one per run (its restart
 * ends the program): no menu, written at once, the network polled
 * meanwhile; "menu": the menu's fiber writes it, the answer after;
 * "gone": the menu was there when it arrived and then left (a game), so
 * it is written at once all the same.
 */
#include "net/netcon.h"
#include "net/netxfer.h"
#include "fs/fat.h"
#include "net/stream.h"
#include "lib/crc32.h"
#include "lib/printf.h"
#include "kernel/fiber.h"
#include "kernel/notice.h"

#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- the kernel functions netcon.c uses */
const char bm_version_tag[] = "bmVER=test";
static const char *cfg_pw = "secret";
static char saved_pw[40];
const char *config_get(const char *key) { return strcmp(key, "net_password") ? NULL : cfg_pw; }
void config_set(const char *key, const char *value) { (void)key; snprintf(saved_pw, sizeof saved_pw, "%s", value); }
void config_save(void) {}
/* netxfer's C: the lines it got; "bad" in them is a wrong line */
static char merged[256];
int config_merge(const char *text, size_t len, char *out, size_t out_len)
{
    snprintf(merged, sizeof merged, "%.*s", (int)len, text);
    if (strstr(merged, "bad"))
        return -1;
    snprintf(out, out_len, "layout=it\ngithub_token=(hidden, 3 characters)\n");
    return 1;
}
static uint32_t fake_us;                /* advanced by spin() */
uint32_t timer_ticks(void) { return fake_us; }
void timer_delay_ms(uint32_t ms) { fake_us += ms * 1000; }
u32_t sys_now(void) { return fake_us / 1000; }

/* the SD card: the last file written */
static char w_dir[80], w_name[40];
static uint8_t *w_data;
static size_t w_len;
static int reboots;
static int writing, write_again, polls_in_write;
int (*fat_write_tick)(void);
size_t fat_write_done;
int fat_mkdirs(const char *path) { (void)path; return 0; }
int fat_write_file(const char *dir, const char *name, const void *data, size_t len)
{
    write_again |= writing;
    writing = 1;
    for (int i = 0; i < 4 && fat_write_tick; i++) {   /* the clusters of a long write */
        fat_write_done = len * (size_t)(i + 1) / 4;
        if (fat_write_tick()) {
            writing = 0;
            return -1;                  /* stopped: nothing written */
        }
    }
    writing = 0;
    snprintf(w_dir, sizeof w_dir, "%s", dir);
    snprintf(w_name, sizeof w_name, "%s", name);
    free(w_data);
    w_data = malloc(len);
    memcpy(w_data, data, len);
    w_len = len;
    return 0;
}
const char *fat_error(void) { return "test"; }
/* G: /bm/shots/*.PNG is a file of 3000 bytes, what a screenshot is here */
int fat_find(const char *path, fat_entry_t *e)
{
    if (strcasecmp(path, "/bm/shots/PONG0001.PNG") && strcasecmp(path, "/bm/config.txt"))
        return -1;
    memset(e, 0, sizeof *e);
    e->size = 3000;
    return 0;
}
int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    *len = e->size;
    *data = malloc(*len);
    for (size_t i = 0; i < *len; i++)
        (*data)[i] = (uint8_t)(i * 7 + 1);
    return 0;
}
/* T: the runtime draws a frame only when `frame_in` rounds have passed (-1: no game) */
static int shot_state, shot_asked, shot_frame;   /* shot_frame: a frame comes at once */
void bm_shot_ask(void) { shot_asked++; shot_state = shot_frame ? 1 : 2; }
void bm_shot_cancel(void) { shot_state = 0; }
int bm_shot_done(char *name, size_t n)
{
    if (shot_state == 1)
        snprintf(name, n, "PONG0001.PNG");
    return shot_state == 2 ? 0 : shot_state;
}

/* the menu's fiber, run straight through (kernel/fiber.c needs the ARM):
 * fiber_job_stop runs it with fiber_cancelled() 1, as the real one */
static int stub_cancel, jobs_run;
int fiber_job_start(fiber_job_t *j, size_t stack, void (*fn)(void *), void *arg)
{
    (void)stack;
    j->f.fn = fn;
    j->f.arg = arg;
    j->busy = 1;
    return 0;
}
int fiber_job_run(fiber_job_t *j, uint32_t until)
{
    (void)until;
    if (j->busy) {
        j->f.fn(j->f.arg);
        jobs_run++;
    }
    j->busy = 0;
    return 0;
}
fiber_t *fiber_current(void) { return NULL; }
void fiber_job_stop(fiber_job_t *j)
{
    if (!j->busy)
        return;
    stub_cancel = 1;
    j->f.fn(j->f.arg);
    stub_cancel = 0;
    j->busy = 0;
}
void fiber_slice(void) {}
int fiber_cancelled(void) { return stub_cancel; }
static char flashed[64];
void notice_flash(const char *title, const char *detail, int progress, uint32_t ms)
{
    (void)progress; (void)ms;
    snprintf(flashed, sizeof flashed, "%s %s", title, detail ? detail : "");
}
/* net.c's poll from inside a write (fat_write_tick): the servers run
 * again, and must not start the write a second time */
void net_poll(void)
{
    polls_in_write++;
    netcon_poll();
    netxfer_poll();
}
static int fails;
static void check(int ok, const char *what);
static int wifi_left;
void wifi_leave(void) { wifi_left++; }
/* an intentional restart says so before it (crumbs.c): the next boot must
 * not report a freeze */
static int clean_exits, flushed;
void crumbs_clean_exit(void) { clean_exits++; }
void uart_flush(void) { flushed += clean_exits; }
/* the kernel's way (main's argument): no menu, the menu's fiber, the menu
 * gone before it wrote; what had run when the kernel case began */
enum { K_AT_ONCE, K_MENU, K_GONE };
static int k_way, menu_frames;
static int k_jobs, k_polls;
/* the kernel transfer is the last case: the reboot ends the test */
void watchdog_reboot(void)
{
    reboots++;
    check(wifi_left == 1, "the access point told we leave before the restart");
    check(clean_exits == 1 && flushed == 1,
          "a clean exit marked before the restart (no freeze report at the next boot), the log flushed");
    if (k_way == K_MENU)
        check(jobs_run > k_jobs && polls_in_write == k_polls && !write_again && !fat_write_tick,
              "the menu's fiber wrote it (no write at once), nothing left set");
    else
        check(polls_in_write >= k_polls + 4 && !write_again && !fat_write_tick,
              "the network polled while the kernel was written, no write started again from inside");
    check(strcmp(w_dir, "/") == 0 && strcmp(w_name, "kernel.img") == 0 && w_len == 100000,
          "kernel received, written as /kernel.img, then reboot");
    if (fails)
        printf("\n%d FAILED\n", fails);
    else
        printf("\nnetcon (kernel %s): all passed\n",
               k_way == K_MENU ? "by the menu" : k_way == K_GONE ? "after the menu left" : "at once");
    exit(fails != 0);
}

static int quiet;
static void (*tap)(char c);
void kprintf_set_tap(void (*t)(char c)) { tap = t; }
int kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (!quiet)
        fputs(buf, stdout);
    if (tap)
        for (const char *p = buf; *p; p++)
            tap(*p);
    return n;
}

/* ---- a client on the same lwIP */
typedef struct {
    struct tcp_pcb *pcb;
    char got[32768];
    size_t len;
    int connected, closed;
} client_t;

static err_t c_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    client_t *c = arg;
    (void)err;
    if (!p) {
        c->closed = 1;
        return ERR_OK;
    }
    size_t n = p->tot_len;
    if (c->len + n >= sizeof c->got)
        n = sizeof c->got - c->len - 1;
    pbuf_copy_partial(p, c->got + c->len, (u16_t)n, 0);
    c->len += n;
    c->got[c->len] = 0;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void c_err(void *arg, err_t err) { client_t *c = arg; (void)err; c->closed = 1; c->pcb = NULL; }
static err_t c_conn(void *arg, struct tcp_pcb *pcb, err_t err) { (void)pcb; ((client_t *)arg)->connected = err == ERR_OK; return ERR_OK; }

static void spin(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        netif_poll_all();
        sys_check_timeouts();
        netcon_poll();
        netxfer_poll();
        if (menu_frames)
            netxfer_write_tick(0);      /* the menu's free time, every round */
        fake_us += 1000;
    }
}

/* stream.c's needs (net.c) */
uint32_t net_ip(void) { return 1; }
int net_wait_step(void) { spin(1); return 0; }
void net_time_set(unsigned long sec) { (void)sec; }

static void connect_port(client_t *c, u16_t port)
{
    memset(c, 0, sizeof *c);
    c->pcb = tcp_new();
    tcp_arg(c->pcb, c);
    tcp_recv(c->pcb, c_recv);
    tcp_err(c->pcb, c_err);
    ip_addr_t lo;
    IP_ADDR4(&lo, 127, 0, 0, 1);
    tcp_connect(c->pcb, &lo, port, c_conn);
    spin(50);
}

static void connect_client(client_t *c) { connect_port(c, NETCON_PORT); }

static void send_str(client_t *c, const char *s)
{
    tcp_write(c->pcb, s, (u16_t)strlen(s), TCP_WRITE_FLAG_COPY);
    tcp_output(c->pcb);
    spin(50);
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* one transfer as bm_net.py does it: the header, the data after OK, the
 * answers in `got` (closed once `want` bytes of them are in) */
static void xfer(client_t *x, char op, const char *pw, const char *path, const uint8_t *data, uint32_t sz,
                 uint32_t crc_xor, size_t want)
{
    connect_port(x, NETXFER_PORT);
    uint8_t h[200];
    unsigned n = 0;
    memcpy(h, "BMXF", 4); n = 4;
    h[n++] = (uint8_t)op;
    h[n++] = (uint8_t)strlen(pw);
    memcpy(h + n, pw, strlen(pw)); n += strlen(pw);
    h[n++] = (uint8_t)strlen(path);
    memcpy(h + n, path, strlen(path)); n += strlen(path);
    uint32_t c = crc32(data, sz) ^ crc_xor;
    memcpy(h + n, &sz, 4); n += 4;
    memcpy(h + n, &c, 4); n += 4;
    tcp_write(x->pcb, h, (u16_t)n, TCP_WRITE_FLAG_COPY);
    tcp_output(x->pcb);
    spin(50);
    if (strcmp(x->got, "OK") == 0)
        for (unsigned off = 0; off < sz && x->pcb; ) {
            u16_t room = tcp_sndbuf(x->pcb);
            unsigned k = sz - off < room ? sz - off : room;
            if (k && tcp_write(x->pcb, data + off, (u16_t)k, TCP_WRITE_FLAG_COPY) == ERR_OK)
                off += k;
            tcp_output(x->pcb);
            spin(5);
        }
    for (int i = 0; i < 4000 && x->len < want && !x->closed; i++)   /* (T waits 3 s for a frame) */
        spin(1);
    if (x->pcb && !x->closed) {
        tcp_recv(x->pcb, NULL);
        tcp_err(x->pcb, NULL);
        tcp_arg(x->pcb, NULL);
        tcp_close(x->pcb);
    }
    spin(600);
}

int main(int argc, char **argv)
{
    k_way = argc > 1 && !strcmp(argv[1], "menu") ? K_MENU : argc > 1 && !strcmp(argv[1], "gone") ? K_GONE : K_AT_ONCE;
    lwip_init();
    check(netcon_start() == 0, "listening on port 3333");
    check(strcmp(netcon_password(), "secret") == 0, "password from the config");

    client_t a;
    connect_client(&a);
    check(a.connected, "client connected");
    check(strstr(a.got, "bm test network console") && strstr(a.got, "password: "), "greeting");

    kprintf("before login\n");
    spin(20);
    check(!strstr(a.got, "before login"), "nothing printed before the password");

    send_str(&a, "wrong\r\n");
    check(strstr(a.got, "wrong password") != NULL, "wrong password refused");
    check(!netcon_active(), "not logged in");

    a.len = 0; a.got[0] = 0;
    netcon_focus("the menu (test keys)", NULL);     /* what takes the keys: told at login */
    send_str(&a, "secret\r\n");                     /* as bm_net.py sends it */
    check(strstr(a.got, "ok - ") != NULL, "right password accepted");
    check(netcon_active(), "logged in");
    check(netcon_getc() < 0, "the \\n after the password is no key (it played a game in the menu)");
    check(strstr(a.got, "the keys go to the menu (test keys)\r\n") && !strstr(a.got, "> "),
          "the login says what takes the keys (the menu: no prompt)");
    netcon_focus("the monitor (test)", "> ");
    netcon_focus("the monitor (test)", "> ");       /* the same again: said once */
    spin(20);
    {
        const char *f = strstr(a.got, "[the keys go to the monitor (test)]\r\n");
        check(f && !strstr(f + 1, "[the keys go to the monitor"), "a change of what takes the keys, said once");
    }

    kprintf("hello\nworld\n");
    spin(20);
    check(strstr(a.got, "hello\r\nworld\r\n") != NULL, "kprintf reaches the client, \\r\\n line ends");

    send_str(&a, "h\r\nx");
    char keys[8] = { 0 };
    int k = 0, ch;
    while ((ch = netcon_getc()) >= 0 && k < 7)
        keys[k++] = (char)ch;
    check(strcmp(keys, "h\rx") == 0, "keys typed on the client, \\r\\n as one Enter");

    /* lots of output: all of it arrives, in order */
    quiet = 1;
    for (int i = 0; i < 400; i++)
        kprintf("line %03d of the long output\n", i);
    quiet = 0;
    spin(400);
    check(strstr(a.got, "line 000 of") && strstr(a.got, "line 399 of the long output\r\n"), "long output delivered");

    client_t b;
    connect_client(&b);
    spin(50);
    check(!b.closed && strstr(b.got, "password") != NULL, "a second client takes over");
    check(a.closed && !netcon_active(), "the first one is dropped");

    tcp_recv(b.pcb, NULL);
    tcp_err(b.pcb, NULL);
    tcp_close(b.pcb);
    spin(50);
    check(!netcon_active(), "client left");

    client_t c;
    connect_client(&c);
    send_str(&c, "a\r\n");
    send_str(&c, "b\r\n");
    send_str(&c, "c\r\n");
    spin(50);
    check(c.closed && strstr(c.got, "bye"), "three wrong passwords: closed");

    /* ---- stream.c: the blocking connection, against the console */
    {
        char err[64] = "";
        stream_t *s = stream_open("127.0.0.1", NETCON_PORT, 2000, err, sizeof err);
        check(s != NULL, "stream: connected to 127.0.0.1:3333");
        char buf[256] = { 0 };
        int n = s ? stream_read(s, buf, sizeof buf - 1, 1000) : -1;
        check(n > 0 && strstr(buf, "password: "), "stream: greeting read");
        check(s && stream_write(s, "secret\r\n", 8) == 0, "stream: password written");
        memset(buf, 0, sizeof buf);
        int got = 0;
        for (int i = 0; i < 20 && !strstr(buf, "ok - "); i++) {
            n = stream_read(s, buf + got, sizeof buf - 1 - (size_t)got, 200);
            if (n > 0) got += n;
        }
        check(strstr(buf, "ok - ") != NULL, "stream: logged in");
        quiet = 1;
        for (int i = 0; i < 300; i++)
            kprintf("stream line %03d of the output\n", i);
        quiet = 0;
        static char all[16384];
        got = 0;
        while (got < (int)sizeof all - 1 && !strstr(all, "stream line 299")) {
            n = stream_read(s, all + got, sizeof all - 1 - (size_t)got, 500);
            if (n <= 0) break;
            got += n;
            all[got] = 0;
        }
        check(strstr(all, "stream line 000") && strstr(all, "stream line 299 of the output\r\n"),
              "stream: 10 KB read in pieces, in order");
        stream_close(s);
        spin(50);
        check(stream_open("127.0.0.1", 9, 500, err, sizeof err) == NULL && err[0],
              "stream: closed port, an error");
    }

    /* ---- transfers */
    check(netxfer_start() == 0, "transfers listening on port 3334");
    static uint8_t file[100000];
    for (unsigned i = 0; i < sizeof file; i++)
        file[i] = (uint8_t)(i * 7 + (i >> 8));

    /* C: settings from the PC, the answer has them after */
    for (int bad = 0; bad < 2; bad++) {
        const char *lines = bad ? "bad line\n" : "github_token=abc\nlayout=it\n";
        const char *list = "layout=it\ngithub_token=(hidden, 3 characters)\n";
        client_t x;
        connect_port(&x, NETXFER_PORT);
        uint8_t h[64];
        unsigned n = 0;
        uint32_t sz = (uint32_t)strlen(lines), c = crc32(lines, sz);
        memcpy(h, "BMXFC\x06secret\x0d" "bm/config.txt", 26); n = 26;
        memcpy(h + n, &sz, 4); n += 4;
        memcpy(h + n, &c, 4); n += 4;
        tcp_write(x.pcb, h, (u16_t)n, TCP_WRITE_FLAG_COPY);
        tcp_output(x.pcb);
        spin(50);
        check(x.len == 2 && memcmp(x.got, "OK", 2) == 0, bad ? "settings: wrong lines, the header taken" :
              "settings: the header taken");
        tcp_write(x.pcb, lines, (u16_t)sz, TCP_WRITE_FLAG_COPY);
        tcp_output(x.pcb);
        spin(100);
        check(strcmp(merged, lines) == 0, "  the lines reach config_merge");
        if (bad) {
            check(x.len == 4 && memcmp(x.got + 2, "KV", 2) == 0, "  wrong lines: KV");
        } else {
            uint32_t ll = (uint32_t)strlen(list), got_ll;
            memcpy(&got_ll, x.got + 4, 4);
            check(x.len == 8 + ll && memcmp(x.got + 2, "OK", 2) == 0 && got_ll == ll &&
                  memcmp(x.got + 8, list, ll) == 0, "  OK, then the length and the settings after");
        }
        if (x.pcb && !x.closed) {
            tcp_recv(x.pcb, NULL);
            tcp_err(x.pcb, NULL);
            tcp_arg(x.pcb, NULL);
            tcp_close(x.pcb);
        }
        spin(600);
    }

    /* S: QD at once, written from the menu (netxfer_write_tick); one file
     * waits at a time: another is busy (BY), the same one replaces it */
    {
        static uint8_t a[5000], b[7000];
        memset(a, 'a', sizeof a);
        memset(b, 'b', sizeof b);
        client_t x;
        w_name[0] = 0;
        xfer(&x, 'S', "secret", "carts/game.bm", a, sizeof a, 0, 4);
        check(strcmp(x.got, "OKQD") == 0 && w_name[0] == 0, "file: QD once checked, nothing written yet");
        check(netxfer_write_pending() && netxfer_updating("/CARTS/GAME.BM") == NETXFER_QUEUED &&
              !netxfer_updating("/carts/other.bm"), "  queued: the menu tags that game only");
        char nm[16];
        uint32_t d = 1, t = 0;
        check(netxfer_file_state(nm, sizeof nm, &d, &t) == NETXFER_QUEUED && strcmp(nm, "game.bm") == 0 &&
              d == 0 && t == sizeof a, "  its name and size");
        xfer(&x, 'S', "secret", "carts/other.bm", b, sizeof b, 0, 2);
        check(strcmp(x.got, "BY") == 0, "  another file meanwhile: BY, refused after the header");
        xfer(&x, 'K', "secret", "kernel.img", b, sizeof b, 0, 2);
        check(strcmp(x.got, "BY") == 0, "  a kernel meanwhile: BY");
        xfer(&x, 'S', "secret", "/carts/GAME.BM", b, sizeof b, 0, 4);
        check(strcmp(x.got, "OKQD") == 0 && w_name[0] == 0, "  the same game again: QD, it replaces the copy waiting");
        netxfer_write_pause();
        check(w_name[0] == 0 && netxfer_write_pending(), "  a pause before the write started: still waiting");
        /* a write stopped half way (an application starts): the old file
         * stays, it starts again later */
        stub_cancel = 1;
        netxfer_write_tick(0);
        stub_cancel = 0;
        check(w_name[0] == 0 && netxfer_write_pending(), "  a write stopped half way: nothing written, still waiting");
        const unsigned before = netxfer_saves();
        netxfer_write_tick(0);
        check(strcmp(w_dir, "/carts") == 0 && strcmp(w_name, "GAME.BM") == 0 && w_len == sizeof b &&
              memcmp(w_data, b, sizeof b) == 0 && netxfer_saves() == before + 1 && !netxfer_write_pending() &&
              !netxfer_updating("/carts/game.bm") && !fat_write_tick,
              "  written from the menu: the newer copy, counted, the tag gone");
        check(strncmp(flashed, "Updated", 7) == 0, "  a notice says so");
        /* a file for the card's root (bm_net.py --to ""): "/top.bm" */
        xfer(&x, 'S', "secret", "top.bm", a, sizeof a, 0, 4);
        netxfer_write_tick(0);
        check(strcmp(x.got, "OKQD") == 0 && strcmp(w_dir, "/") == 0 && strcmp(w_name, "top.bm") == 0 &&
              w_len == sizeof a, "  a file for the root: written in /");
    }

    /* T and G (R-NET2): a screenshot of the running game, and the file back */
    {
        client_t x;
        shot_frame = 1;                 /* a frame is drawn at once */
        xfer(&x, 'T', "secret", "", NULL, 0, 0, 14);
        printf("DBG len=%zu got=%.20s\n", x.len, x.got);
        check(strncmp(x.got, "OK", 2) == 0 && x.len == 18 && memcmp(x.got + 6, "PONG0001.PNG", 12) == 0,
              "  T: OK and the name of the screenshot");
        shot_frame = 0;                 /* no game: the frame never comes */
        xfer(&x, 'T', "secret", "", NULL, 0, 0, 2);
        check(strcmp(x.got, "NG") == 0, "  T without a game: NG after the wait");
        xfer(&x, 'T', "nope", "", NULL, 0, 0, 2);
        check(strcmp(x.got, "PW") == 0, "  T with a wrong password: PW");
        uint8_t want[3000];
        for (unsigned i = 0; i < sizeof want; i++)
            want[i] = (uint8_t)(i * 7 + 1);
        xfer(&x, 'G', "secret", "bm/shots/pong0001.png", NULL, 0, 0, 10 + 3000);
        check(x.len == 3010 && strncmp(x.got, "OK", 2) == 0 && memcmp(x.got + 10, want, 3000) == 0 &&
              (uint8_t)x.got[2] == (3000 & 0xFF) && (uint8_t)x.got[3] == (3000 >> 8), "  G: OK, length, data");
        uint32_t c;
        memcpy(&c, x.got + 6, 4);
        check(c == crc32(want, 3000), "  G: the crc of the data");
        xfer(&x, 'G', "secret", "bm/config.txt", NULL, 0, 0, 2);
        check(strcmp(x.got, "PA") == 0, "  G outside bm/shots: PA");
        xfer(&x, 'G', "secret", "bm/shots/../config.txt", NULL, 0, 0, 2);
        check(strcmp(x.got, "PA") == 0, "  G with ..: PA");
        xfer(&x, 'G', "secret", "bm/shots/NOPE0001.PNG", NULL, 0, 0, 2);
        check(strcmp(x.got, "NF") == 0, "  G of a file that is not there: NF");
        xfer(&x, 'G', "nope", "bm/shots/pong0001.png", NULL, 0, 0, 2);
        check(strcmp(x.got, "PW") == 0, "  G with a wrong password: PW");
    }

    /* mark: what start.S puts at +4 of a kernel image (kernel.img or the
     * Pi Zero 2 W's kernel7.img); this is kernel.img's build. arm64: the
     * RGB30's kernel8.img, "ARM\x64" at +56 (no bmK mark) */
    const unsigned saves0 = netxfer_saves();
    struct { char op; const char *pw, *path; uint32_t crc_xor; const char *answer, *what, *mark; int arm64; } cases[] = {
        { 'S', "secret", "carts/pong.bm", 0, "OKQD", "file saved on the SD card", NULL, 0 },
        { 'S', "nope", "carts/pong.bm", 0, "PW", "wrong password refused", NULL, 0 },
        { 'S', "secret", "carts/bad.bm", 1, "OKCE", "damaged file refused", NULL, 0 },
        { 'P', "secret", "x.bm", 0, "OKOK", "cartridge to play received", NULL, 0 },
        { 'K', "secret", "kernel7.img", 0, "OKKA", "the Pi Zero 2 W's kernel refused", "bmK7", 0 },
        { 'K', "secret", "kernel8.img", 0, "OKKA", "the RGB30's kernel refused", "\x10\0\0\x14", 1 },
        { 'K', "secret", "kernel.img", 0, "OKOK", "kernel received", "bmK6", 0 },
    };
    for (unsigned t = 0; t < sizeof cases / sizeof cases[0]; t++) {
        w_name[0] = 0;
        if (cases[t].op == 'K' && strcmp(cases[t].answer, "OKOK") == 0) {
            k_jobs = jobs_run;
            k_polls = polls_in_write;
            if (k_way == K_MENU)
                menu_frames = 1;        /* the menu runs all along */
            else if (k_way == K_GONE)
                netxfer_write_tick(0);  /* the menu's last frame, before a game starts */
        }
        if (cases[t].mark)
            memcpy(file + 4, cases[t].mark, 4);
        memcpy(file + 56, cases[t].arm64 ? "ARM\x64" : "\0\0\0\0", 4);
        client_t x;
        connect_port(&x, NETXFER_PORT);
        uint8_t h[200];
        unsigned n = 0;
        memcpy(h, "BMXF", 4); n = 4;
        h[n++] = (uint8_t)cases[t].op;
        h[n++] = (uint8_t)strlen(cases[t].pw);
        memcpy(h + n, cases[t].pw, strlen(cases[t].pw)); n += strlen(cases[t].pw);
        h[n++] = (uint8_t)strlen(cases[t].path);
        memcpy(h + n, cases[t].path, strlen(cases[t].path)); n += strlen(cases[t].path);
        uint32_t sz = sizeof file, c = crc32(file, sizeof file) ^ cases[t].crc_xor;
        memcpy(h + n, &sz, 4); n += 4;
        memcpy(h + n, &c, 4); n += 4;
        tcp_write(x.pcb, h, (u16_t)n, TCP_WRITE_FLAG_COPY);
        tcp_output(x.pcb);
        spin(50);
        if (strcmp(x.got, "OK") == 0)
            for (unsigned off = 0; off < sizeof file && x.pcb; ) {
                u16_t room = tcp_sndbuf(x.pcb);
                unsigned k = sizeof file - off < room ? sizeof file - off : room;
                if (k && tcp_write(x.pcb, file + off, (u16_t)k, TCP_WRITE_FLAG_COPY) == ERR_OK)
                    off += k;
                tcp_output(x.pcb);
                spin(5);
            }
        /* like bm_net.py: close as soon as the whole answer is in (the
         * kernel case must still reboot) */
        for (int i = 0; i < 600 && x.len < strlen(cases[t].answer) && !x.closed; i++)
            spin(1);
        if (x.pcb && !x.closed) {
            tcp_recv(x.pcb, NULL);      /* x goes out of scope: no more callbacks */
            tcp_err(x.pcb, NULL);
            tcp_arg(x.pcb, NULL);
            tcp_close(x.pcb);
        }
        spin(600);
        if (cases[t].op == 'K' && strcmp(cases[t].answer, "OKOK") == 0) {
            /* the kernel is in: 3 s counted down, then the restart */
            int secs = -1;
            check(netxfer_kernel_state(NULL, NULL, &secs) == NETXFER_K_RESTART && secs >= 2 && secs <= 3,
                  "  restart counted down from 3 s");
            spin(3000);
        }
        if (strcmp(x.got, cases[t].answer) != 0)
            printf("  got \"%s\"\n", x.got);
        check(strcmp(x.got, cases[t].answer) == 0, cases[t].what);
        if (t == 0)
            netxfer_write_tick(0);      /* the menu's next frame */
        if (t == 0)
            check(strcmp(w_dir, "/carts") == 0 && strcmp(w_name, "pong.bm") == 0 &&
                  w_len == sizeof file && memcmp(w_data, file, w_len) == 0 && netxfer_saves() == saves0 + 1,
                  "  in /carts/pong.bm, same bytes");
        if (t == 1 || t == 2 || t == 4)
            check(w_name[0] == 0, "  nothing written");
        if (t == 3) {
            uint8_t *pb;
            size_t pl;
            check(netxfer_play_announce() == 1 && netxfer_play_announce() == 0, "  announced once");
            check(netxfer_take_play(&pb, &pl) && pl == sizeof file && memcmp(pb, file, pl) == 0,
                  "  the monitor gets the same bytes");
            free(pb);
            check(!netxfer_take_play(&pb, &pl), "  taken only once");
        }
    }

    printf("FAIL the kernel transfer did not reboot\n");
    return 1;
}
