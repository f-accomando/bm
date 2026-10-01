/*
 * Host test of the games between consoles (src/net/lan.c) on lwIP with its
 * loopback interface: the hello heard (and our own ignored, and forgotten
 * after 7 s), a game sent with lan_send and accepted by the "player", one
 * refused, one never answered, a damaged one, a busy receiver, a game too
 * big, a bad header. The sender and the receiver are the same lwIP here.
 */
#include "net/lan.h"
#include "net/stream.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "mbedtls/sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fake_us;
uint32_t timer_ticks(void) { return fake_us; }
u32_t sys_now(void) { return fake_us / 1000; }
int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
void net_time_set(unsigned long sec) { (void)sec; }

static uint32_t my_ip = 0x0201A8C0;     /* 192.168.1.2: not the loopback */
uint32_t net_ip(void) { return my_ip; }

/* the player of the receiving console: 1 accept, 0 refuse, -1 never answer */
static int player = 1, asked;
static lan_offer_t seen_offer;

static void spin(int rounds)
{
    for (int i = 0; i < rounds; i++) {
        netif_poll_all();
        sys_check_timeouts();
        lan_poll();
        lan_offer_t o;
        if (lan_offer(&o) && player >= 0) {
            seen_offer = o;
            asked++;
            lan_answer(player);
        }
        fake_us += 1000;
    }
}

int net_wait_step(void) { spin(1); return 0; }

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* ---- a raw client, for what lan_send never sends */
typedef struct {
    struct tcp_pcb *pcb;
    char got[64];
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
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static void c_err(void *arg, err_t err) { client_t *c = arg; (void)err; c->closed = 1; c->pcb = NULL; }
static err_t c_conn(void *arg, struct tcp_pcb *pcb, err_t err) { (void)pcb; ((client_t *)arg)->connected = err == ERR_OK; return ERR_OK; }

static void raw_connect(client_t *c)
{
    memset(c, 0, sizeof *c);
    c->pcb = tcp_new();
    tcp_nagle_disable(c->pcb);          /* the last piece goes at once */
    tcp_arg(c->pcb, c);
    tcp_recv(c->pcb, c_recv);
    tcp_err(c->pcb, c_err);
    ip_addr_t lo;
    IP_ADDR4(&lo, 127, 0, 0, 1);
    tcp_connect(c->pcb, &lo, LAN_PORT, c_conn);
    spin(50);
}

static void raw_send(client_t *c, const void *d, size_t n)
{
    const uint8_t *p = d;
    while (n && c->pcb) {
        u16_t k = (u16_t)(n < tcp_sndbuf(c->pcb) ? n : tcp_sndbuf(c->pcb));
        if (k == 0) {
            spin(5);
            continue;
        }
        tcp_write(c->pcb, p, k, TCP_WRITE_FLAG_COPY);
        tcp_output(c->pcb);
        p += k;
        n -= k;
        spin(5);
    }
    spin(50);
}

static size_t header(uint8_t *h, const char *title, uint32_t size, const uint8_t *sha)
{
    size_t n = 0;
    memcpy(h, "BMLX\1", 5);
    n = 5;
    const char *f[3] = { "raw", title, "tests" };
    for (int i = 0; i < 3; i++) {
        h[n++] = (uint8_t)strlen(f[i]);
        memcpy(h + n, f[i], strlen(f[i]));
        n += strlen(f[i]);
    }
    h[n] = (uint8_t)size; h[n + 1] = (uint8_t)(size >> 8); h[n + 2] = (uint8_t)(size >> 16);
    h[n + 3] = (uint8_t)(size >> 24);
    memcpy(h + n + 4, sha, 32);
    return n + 36;
}

static void hello_from(const char *msg)
{
    struct udp_pcb *u = udp_new();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)strlen(msg), PBUF_RAM);
    memcpy(p->payload, msg, strlen(msg));
    ip_addr_t lo;
    IP_ADDR4(&lo, 127, 0, 0, 1);
    udp_sendto(u, p, &lo, LAN_HELLO_PORT);
    pbuf_free(p);
    udp_remove(u);
    spin(5);
}

int main(void)
{
    lwip_init();
    char name[24], buf[64];
    int n = lan_hello(buf, sizeof buf, "bm-108");
    check(n == 13 && memcmp(buf, "BMHI 1 bm-108", 13) == 0, "hello written");
    check(lan_parse_hello(buf, (size_t)n, name, sizeof name) == 0 && strcmp(name, "bm-108") == 0, "hello read");
    check(lan_parse_hello("BMHI 2 x", 8, name, sizeof name) == -1 && lan_parse_hello("BMHI 1 ", 7, name, sizeof name) == -1 &&
          lan_parse_hello("BMHI 1 a\nb", 10, name, sizeof name) == -1 &&
          lan_parse_hello("BMHI 1 a name much longer than the 23 bytes", 43, name, sizeof name) == -1,
          "other datagrams, empty, control characters, too long: not a console");

    check(lan_start("bm-test") == 0 && lan_running(), "hello and server on");
    lan_peer_t peers[LAN_MAX_PEERS];
    hello_from("BMHI 1 Kitchen Pi");
    n = lan_peers(peers, LAN_MAX_PEERS);
    check(n == 1 && strcmp(peers[0].name, "Kitchen Pi") == 0 && peers[0].ip == 0x0100007F, "a console heard");
    hello_from("BMHI 1 Kitchen Pi");
    check(lan_peers(peers, LAN_MAX_PEERS) == 1, "the same console once");
    spin(7500);
    check(lan_peers(peers, LAN_MAX_PEERS) == 0, "silent for 7 s: gone");
    my_ip = 0x0100007F;
    hello_from("BMHI 1 me");
    check(lan_peers(peers, LAN_MAX_PEERS) == 0, "our own hello: ignored");
    my_ip = 0x0201A8C0;

    /* a game sent and accepted */
    size_t size = 300000;
    uint8_t *game = malloc(size);
    for (size_t i = 0; i < size; i++)
        game[i] = (uint8_t)(i * 7 + (i >> 9));
    char err[128] = "";
    int r = lan_send(0x0100007F, "Living room", "Snake", "bm", game, size, NULL, err, sizeof err);
    check(r == 0, "sent and accepted");
    if (r)
        printf("     %s\n", err);
    check(asked == 1 && strcmp(seen_offer.from, "Living room") == 0 && strcmp(seen_offer.title, "Snake") == 0 &&
          strcmp(seen_offer.author, "bm") == 0 && seen_offer.size == size && seen_offer.ip == 0x0100007F,
          "the offer: from, title, author, size, address");
    uint8_t *data;
    size_t len;
    lan_offer_t o;
    check(lan_take(&data, &len, &o) == 1 && len == size && memcmp(data, game, size) == 0, "the game received intact");
    free(data);
    check(lan_take(&data, &len, &o) == 0 && lan_receiving() == -1, "taken once");

    player = 0;
    r = lan_send(0x0100007F, "Living room", "Pong", "bm", game, 1000, NULL, err, sizeof err);
    check(r == -1 && strstr(err, "refused"), "refused by the player");
    check(lan_take(&data, &len, &o) == 0, "nothing received");

    player = -1;
    r = lan_send(0x0100007F, "Living room", "Pong", "bm", game, 1000, NULL, err, sizeof err);
    check(r == -1 && strstr(err, "refused (or no answer)"), "no answer in 60 s: refused");
    player = 1;

    /* a damaged game: the bytes are not the announced ones */
    uint8_t sha[32], h[256];
    mbedtls_sha256(game, 2000, sha, 0);
    client_t c, c2;
    raw_connect(&c);
    raw_send(&c, h, header(h, "Bad", 2000, sha));
    check(c.len == 2 && memcmp(c.got, "OK", 2) == 0, "raw: accepted");
    game[100] ^= 1;
    raw_send(&c, game, 2000);
    game[100] ^= 1;
    check(c.len == 4 && memcmp(c.got + 2, "BD", 2) == 0 && lan_take(&data, &len, &o) == 0,
          "damaged on the way: BD, nothing kept");

    /* busy: one offer waits for the player, another console knocks */
    player = -1;
    raw_connect(&c);
    raw_send(&c, h, header(h, "First", 2000, sha));
    check(lan_offer(&o) && strcmp(o.title, "First") == 0, "an offer waits");
    raw_connect(&c2);
    check(c2.len == 2 && memcmp(c2.got, "BZ", 2) == 0, "a second console: busy");
    lan_answer(0);
    spin(20);
    check(c.len == 2 && memcmp(c.got, "NO", 2) == 0, "the first refused");
    player = 1;

    raw_connect(&c);
    raw_send(&c, h, header(h, "Huge", LAN_MAX_SIZE + 1, sha));
    check(c.len == 2 && memcmp(c.got, "BS", 2) == 0, "too big: BS");
    raw_connect(&c);
    raw_send(&c, "BMXF\1\0\0\0", 8);
    check(c.len == 2 && memcmp(c.got, "BH", 2) == 0, "not our protocol: BH");

    /* the hello goes out every 2 s while on, and lan_stop closes it all */
    lan_stop();
    check(!lan_running(), "off");
    r = lan_send(0x0100007F, "Living room", "Snake", "bm", game, size, NULL, err, sizeof err);
    check(r == -1, "off: nobody listens");
    free(game);
    printf(fails ? "lan: %d FAILED\n" : "lan: all passed\n", fails);
    return fails != 0;
}
