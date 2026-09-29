/*
 * Host test of the network console (src/net/netcon.c) on lwIP with its
 * loopback interface: a client connects to 127.0.0.1:3333, gets the
 * greeting, fails and passes the password, sees what kprintf prints, and
 * its keys come out of netcon_getc; a second client is turned away.
 */
#include "net/netcon.h"
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
uint32_t timer_ticks(void) { return (uint32_t)clock(); }
u32_t sys_now(void) { return (u32_t)(clock() * 1000 / CLOCKS_PER_SEC); }

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
    }
}

static void connect_client(client_t *c)
{
    memset(c, 0, sizeof *c);
    c->pcb = tcp_new();
    tcp_arg(c->pcb, c);
    tcp_recv(c->pcb, c_recv);
    tcp_err(c->pcb, c_err);
    ip_addr_t lo;
    IP_ADDR4(&lo, 127, 0, 0, 1);
    tcp_connect(c->pcb, &lo, NETCON_PORT, c_conn);
    spin(50);
}

static void send_str(client_t *c, const char *s)
{
    tcp_write(c->pcb, s, (u16_t)strlen(s), TCP_WRITE_FLAG_COPY);
    tcp_output(c->pcb);
    spin(50);
}

static int fails;
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

    printf(fails ? "\n%d FAILED\n" : "\nnetcon: all passed\n", fails);
    return fails != 0;
}
