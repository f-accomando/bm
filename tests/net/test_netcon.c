/*
 * Host test of the network console (src/net/netcon.c) on lwIP with its
 * loopback interface: a client connects to 127.0.0.1:3333, gets the
 * greeting, fails and passes the password, sees what kprintf prints, and
 * its keys come out of netcon_getc; a second client is turned away.
 * Then the transfers (src/net/netxfer.c): a file saved on the "SD card",
 * a wrong password, a damaged file, a cartridge to play, a kernel.
 */
#include "net/netcon.h"
#include "net/netxfer.h"
#include "lib/crc32.h"
#include "lib/printf.h"

#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- the kernel functions netcon.c uses */
const char bm33_version[] = "test";
static const char *cfg_pw = "secret";
static char saved_pw[40];
const char *config_get(const char *key) { return strcmp(key, "net_password") ? NULL : cfg_pw; }
void config_set(const char *key, const char *value) { (void)key; snprintf(saved_pw, sizeof saved_pw, "%s", value); }
void config_save(void) {}
static uint32_t fake_us;                /* advanced by spin() */
uint32_t timer_ticks(void) { return fake_us; }
void timer_delay_ms(uint32_t ms) { fake_us += ms * 1000; }
u32_t sys_now(void) { return fake_us / 1000; }

/* the SD card: the last file written */
static char w_dir[80], w_name[40];
static uint8_t *w_data;
static size_t w_len;
static int reboots;
int fat_mkdirs(const char *path) { (void)path; return 0; }
int fat_write_file(const char *dir, const char *name, const void *data, size_t len)
{
    snprintf(w_dir, sizeof w_dir, "%s", dir);
    snprintf(w_name, sizeof w_name, "%s", name);
    free(w_data);
    w_data = malloc(len);
    memcpy(w_data, data, len);
    w_len = len;
    return 0;
}
const char *fat_error(void) { return "test"; }
static int fails;
static void check(int ok, const char *what);
/* the kernel transfer is the last case: the reboot ends the test */
void watchdog_reboot(void)
{
    reboots++;
    check(strcmp(w_dir, "/") == 0 && strcmp(w_name, "kernel.img") == 0 && w_len == 100000,
          "kernel received, written as /kernel.img, then reboot");
    printf(fails ? "\n%d FAILED\n" : "\nnetcon: all passed\n", fails);
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
        fake_us += 1000;
    }
}

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

int main(void)
{
    lwip_init();
    check(netcon_start() == 0, "listening on port 3333");
    check(strcmp(netcon_password(), "secret") == 0, "password from the config");

    client_t a;
    connect_client(&a);
    check(a.connected, "client connected");
    check(strstr(a.got, "bm33 test network console") && strstr(a.got, "password: "), "greeting");

    kprintf("before login\n");
    spin(20);
    check(!strstr(a.got, "before login"), "nothing printed before the password");

    send_str(&a, "wrong\r\n");
    check(strstr(a.got, "wrong password") != NULL, "wrong password refused");
    check(!netcon_active(), "not logged in");

    a.len = 0; a.got[0] = 0;
    send_str(&a, "secret\n");
    check(strstr(a.got, "ok - ") != NULL, "right password accepted");
    check(netcon_active(), "logged in");

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
    check(b.closed && !strstr(b.got, "password"), "second client turned away");
    check(netcon_active(), "first client still logged in");

    tcp_close(a.pcb);
    spin(50);
    check(!netcon_active(), "client left");

    client_t c;
    connect_client(&c);
    send_str(&c, "a\r\n");
    send_str(&c, "b\r\n");
    send_str(&c, "c\r\n");
    spin(50);
    check(c.closed && strstr(c.got, "bye"), "three wrong passwords: closed");

    /* ---- transfers */
    check(netxfer_start() == 0, "transfers listening on port 3334");
    static uint8_t file[100000];
    for (unsigned i = 0; i < sizeof file; i++)
        file[i] = (uint8_t)(i * 7 + (i >> 8));

    struct { char op; const char *pw, *path; uint32_t crc_xor; const char *answer, *what; } cases[] = {
        { 'S', "secret", "carts/pong.b33", 0, "OKOK", "file saved on the SD card" },
        { 'S', "nope", "carts/pong.b33", 0, "PW", "wrong password refused" },
        { 'S', "secret", "carts/bad.b33", 1, "OKCE", "damaged file refused" },
        { 'P', "secret", "x.b33", 0, "OKOK", "cartridge to play received" },
        { 'K', "secret", "kernel.img", 0, "OKOK", "kernel received" },
    };
    for (unsigned t = 0; t < sizeof cases / sizeof cases[0]; t++) {
        w_name[0] = 0;
        client_t x;
        connect_port(&x, NETXFER_PORT);
        uint8_t h[200];
        unsigned n = 0;
        memcpy(h, "BM3X", 4); n = 4;
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
        spin(600);                      /* the answer, then the server closes */
        if (strcmp(x.got, cases[t].answer) != 0 || !x.closed)
            printf("  got \"%s\" closed %d connected %d\n", x.got, x.closed, x.connected);
        check(strcmp(x.got, cases[t].answer) == 0 && x.closed, cases[t].what);
        if (t == 0)
            check(strcmp(w_dir, "/carts") == 0 && strcmp(w_name, "pong.b33") == 0 &&
                  w_len == sizeof file && memcmp(w_data, file, w_len) == 0 && netxfer_saves() == 1,
                  "  in /carts/pong.b33, same bytes");
        if (t == 1 || t == 2)
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
