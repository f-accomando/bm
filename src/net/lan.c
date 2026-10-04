/*
 * Games between consoles on the home network (M24): the hello on UDP,
 * the receiving side as a state machine in lwIP's callbacks (the player's
 * answer comes from the menu through lan_answer), the sending side as a
 * blocking client on a stream.
 */
#include "lan.h"
#include "net.h"
#include "stream.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "mbedtls/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELLO_US   2000000u
#define GONE_US    7000000u
#define ASK_US     60000000u            /* the player's answer */
#define DATA_US    15000000u            /* silence while the bytes come */

static struct udp_pcb *hello_pcb;
static struct tcp_pcb *listener, *peer;
static char my_name[24];
static uint32_t hello_at;
static lan_peer_t peers[LAN_MAX_PEERS];
static int npeers;

static enum { L_IDLE, L_HEADER, L_ASK, L_DATA, L_DONE } st;
static uint8_t hdr[4 + 1 + 3 * 65 + 4 + 32];
static unsigned hdr_len;
static lan_offer_t offer;
static uint8_t *buf;
static uint32_t got, st_at;

/* ---------------------------------------------------------------- hello */

static int text_ok(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || s[i] == 127)
            return 0;
    return 1;
}

int lan_hello(char *b, size_t n, const char *name)
{
    return snprintf(b, n, "BMHI 1 %s", name);
}

int lan_parse_hello(const char *b, size_t n, char *name, size_t nn)
{
    if (n < 8 || memcmp(b, "BMHI 1 ", 7) != 0)
        return -1;
    size_t l = n - 7;
    if (l >= nn || l >= sizeof peers[0].name || !text_ok(b + 7, l))
        return -1;
    memcpy(name, b + 7, l);
    name[l] = 0;
    return 0;
}

static void on_hello(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)pcb; (void)port;
    char msg[64], name[24];
    u16_t n = pbuf_copy_partial(p, msg, sizeof msg - 1, 0);
    pbuf_free(p);
    uint32_t ip = ip4_addr_get_u32(ip_2_ip4(addr));
    if (lan_parse_hello(msg, n, name, sizeof name) != 0 || ip == net_ip())
        return;                         /* not a console, or our own broadcast */
    int i = 0;
    while (i < npeers && peers[i].ip != ip)
        i++;
    if (i == npeers) {
        if (npeers == LAN_MAX_PEERS) {  /* full: the one heard least lately goes */
            i = 0;
            for (int k = 1; k < npeers; k++)
                if (peers[k].seen - peers[i].seen > 0x80000000u)
                    i = k;
        } else {
            npeers++;
        }
    }
    peers[i].ip = ip;
    snprintf(peers[i].name, sizeof peers[i].name, "%s", name);
    peers[i].seen = timer_ticks();
}

static void say_hello(void)
{
    char msg[48];
    int n = lan_hello(msg, sizeof msg, my_name);
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (!p)
        return;
    memcpy(p->payload, msg, (size_t)n);
    udp_sendto(hello_pcb, p, IP_ADDR_BROADCAST, LAN_HELLO_PORT);
    pbuf_free(p);
}

int lan_peers(lan_peer_t *out, int max)
{
    int n = npeers < max ? npeers : max;
    memcpy(out, peers, (size_t)n * sizeof *out);
    return n;
}

/* ---------------------------------------------------------------- receiving */

static void drop(void)
{
    if (peer) {
        tcp_arg(peer, NULL);
        tcp_recv(peer, NULL);
        tcp_err(peer, NULL);
        if (tcp_close(peer) != ERR_OK)
            tcp_abort(peer);
        peer = NULL;
    }
    if (st != L_DONE) {
        free(buf);
        buf = NULL;
        st = L_IDLE;
    }
}

/* two letters to the sender, then the connection closes */
static void reply_close(const char *two)
{
    if (peer) {
        tcp_write(peer, two, 2, TCP_WRITE_FLAG_COPY);
        tcp_output(peer);
    }
    drop();
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 1: more bytes needed; 0: complete (offer filled); -1: bad; -2: too big */
static int parse_header(void)
{
    if (hdr_len >= 4 && memcmp(hdr, "BMLX", 4) != 0)
        return -1;
    if (hdr_len >= 5 && hdr[4] != 1)
        return -1;
    unsigned at = 5;
    char *fields[3] = { offer.from, offer.title, offer.author };
    size_t caps[3] = { sizeof offer.from, sizeof offer.title, sizeof offer.author };
    for (int f = 0; f < 3; f++) {
        if (hdr_len < at + 1)
            return 1;
        unsigned l = hdr[at];
        if (l >= caps[f])
            return -1;
        if (hdr_len < at + 1 + l)
            return 1;
        if (!text_ok((const char *)hdr + at + 1, l))
            return -1;
        memcpy(fields[f], hdr + at + 1, l);
        fields[f][l] = 0;
        at += 1 + l;
    }
    if (hdr_len < at + 36)
        return 1;
    offer.size = le32(hdr + at);
    memcpy(offer.sha256, hdr + at + 4, 32);
    if (!offer.title[0] || !offer.size)
        return -1;
    return offer.size > LAN_MAX_SIZE ? -2 : 0;
}

static void check_done(void)
{
    uint8_t h[32];
    if (mbedtls_sha256(buf, got, h, 0) != 0 || memcmp(h, offer.sha256, 32) != 0) {
        kprintf("lan: %s from %s: damaged\n", offer.title, offer.from);
        reply_close("BD");
        return;
    }
    st = L_DONE;
    reply_close("OK");
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (!p || err != ERR_OK) {          /* the sender left */
        if (p)
            pbuf_free(p);
        if (pcb == peer)
            drop();
        return ERR_OK;
    }
    tcp_recved(pcb, p->tot_len);
    for (struct pbuf *q = p; q && peer == pcb; q = q->next) {
        const uint8_t *d = q->payload;
        for (u16_t i = 0; i < q->len && peer == pcb; ) {
            if (st == L_HEADER) {
                if (hdr_len == sizeof hdr) {
                    reply_close("BH");
                    break;
                }
                hdr[hdr_len++] = d[i++];
                int r = parse_header();
                if (r < 0) {
                    reply_close(r == -2 ? "BS" : "BH");
                } else if (r == 0) {
                    offer.ip = ip4_addr_get_u32(ip_2_ip4(&pcb->remote_ip));
                    st = L_ASK;
                    st_at = timer_ticks();
                    kprintf("lan: %s offers %s (%lu bytes)\n", offer.from, offer.title,
                            (unsigned long)offer.size);
                }
            } else if (st == L_DATA) {
                u16_t n = q->len - i;
                if (n > offer.size - got)
                    n = (u16_t)(offer.size - got);
                memcpy(buf + got, d + i, n);
                got += n;
                i += n;
                st_at = timer_ticks();
                if (got == offer.size)
                    check_done();
            } else {                    /* bytes before the answer, or after the end */
                reply_close("BH");
            }
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    (void)arg; (void)err;
    peer = NULL;                        /* freed by lwIP */
    if (st != L_DONE) {
        free(buf);
        buf = NULL;
        st = L_IDLE;
    }
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !pcb)
        return ERR_VAL;
    if (peer || st != L_IDLE) {         /* one game at a time */
        tcp_write(pcb, "BZ", 2, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        tcp_close(pcb);
        return ERR_OK;
    }
    peer = pcb;
    memset(&offer, 0, sizeof offer);
    hdr_len = 0;
    got = 0;
    st = L_HEADER;
    st_at = timer_ticks();
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    return ERR_OK;
}

int lan_offer(lan_offer_t *o)
{
    if (st != L_ASK)
        return 0;
    *o = offer;
    return 1;
}

void lan_answer(int yes)
{
    if (st != L_ASK)
        return;
    if (!yes) {
        reply_close("NO");
        return;
    }
    if (!(buf = malloc(offer.size))) {
        reply_close("BS");
        return;
    }
    got = 0;
    st = L_DATA;
    st_at = timer_ticks();
    tcp_write(peer, "OK", 2, TCP_WRITE_FLAG_COPY);
    tcp_output(peer);
}

long lan_receiving(void)
{
    return st == L_DATA ? (long)got : -1;
}

int lan_take(uint8_t **data, size_t *len, lan_offer_t *o)
{
    if (st != L_DONE)
        return 0;
    *data = buf;
    *len = offer.size;
    *o = offer;
    buf = NULL;
    st = L_IDLE;
    return 1;
}

/* ---------------------------------------------------------------- on, off */

int lan_start(const char *name)
{
    if (hello_pcb)
        return 0;
    snprintf(my_name, sizeof my_name, "%s", name);
    hello_pcb = udp_new();
    listener = tcp_new();
    if (!hello_pcb || !listener || udp_bind(hello_pcb, IP_ADDR_ANY, LAN_HELLO_PORT) != ERR_OK ||
        tcp_bind(listener, IP_ADDR_ANY, LAN_PORT) != ERR_OK) {
        if (listener)
            tcp_close(listener);
        if (hello_pcb)
            udp_remove(hello_pcb);
        hello_pcb = NULL;
        listener = NULL;
        return -1;
    }
    udp_recv(hello_pcb, on_hello, NULL);
    struct tcp_pcb *l = tcp_listen(listener);
    if (!l) {
        tcp_close(listener);
        udp_remove(hello_pcb);
        hello_pcb = NULL;
        listener = NULL;
        return -1;
    }
    listener = l;
    tcp_accept(listener, on_accept);
    hello_at = timer_ticks() - HELLO_US;    /* the first hello at once */
    npeers = 0;
    return 0;
}

void lan_stop(void)
{
    if (!hello_pcb)
        return;
    drop();
    free(buf);
    buf = NULL;
    st = L_IDLE;
    tcp_close(listener);
    udp_remove(hello_pcb);
    listener = NULL;
    hello_pcb = NULL;
    npeers = 0;
}

int lan_running(void)
{
    return hello_pcb != NULL;
}

void lan_poll(void)
{
    if (!hello_pcb)
        return;
    uint32_t now = timer_ticks();
    if (now - hello_at >= HELLO_US) {
        hello_at = now;
        say_hello();
    }
    for (int i = 0; i < npeers; )
        if (now - peers[i].seen > GONE_US)
            peers[i] = peers[--npeers];
        else
            i++;
    if (st == L_ASK && now - st_at > ASK_US) {
        kprintf("lan: no answer to %s, refused\n", offer.from);
        reply_close("NO");
    } else if ((st == L_DATA || st == L_HEADER) && now - st_at > DATA_US) {
        kprintf("lan: %s went silent\n", offer.from[0] ? offer.from : "a console");
        drop();
    }
}

/* ---------------------------------------------------------------- sending */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* two letters from the receiver, waiting up to ms */
static int answer(stream_t *s, char *two, uint32_t ms)
{
    int n = 0;
    while (n < 2) {
        int r = stream_read(s, two + n, (size_t)(2 - n), ms);
        if (r <= 0)
            return -1;
        n += r;
    }
    return 0;
}

static const char *meaning(const char *two)
{
    if (!memcmp(two, "NO", 2)) return "refused (or no answer)";
    if (!memcmp(two, "BZ", 2)) return "busy with another game";
    if (!memcmp(two, "BS", 2)) return "too big for it";
    if (!memcmp(two, "BD", 2)) return "it arrived damaged";
    return "it did not understand";
}

int lan_send(uint32_t ip, const char *from, const char *title, const char *author,
             const uint8_t *data, size_t len, void (*progress)(const char *), char *err, size_t err_len)
{
    if (len == 0 || len > LAN_MAX_SIZE) {
        snprintf(err, err_len, "%lu bytes: too big", (unsigned long)len);
        return -1;
    }
    ip4_addr_t a;
    ip4_addr_set_u32(&a, ip);
    char host[16], two[2], line[96];
    snprintf(host, sizeof host, "%s", ip4addr_ntoa(&a));
    stream_t *s = stream_open(host, LAN_PORT, 5000, err, err_len);
    if (!s)
        return -1;
    uint8_t h[sizeof hdr];
    unsigned n = 0;
    memcpy(h, "BMLX\1", 5);
    n = 5;
    const char *fields[3] = { from, title, author };
    size_t caps[3] = { 24, 49, 33 };
    for (int f = 0; f < 3; f++) {
        size_t l = strlen(fields[f]);
        if (l >= caps[f])
            l = caps[f] - 1;
        h[n++] = (uint8_t)l;
        memcpy(h + n, fields[f], l);
        n += (unsigned)l;
    }
    put32(h + n, (uint32_t)len);
    mbedtls_sha256(data, len, h + n + 4, 0);
    n += 36;
    if (progress)
        progress("waiting for the other player to accept...");
    int r = -1;
    if (stream_write(s, h, n) != 0 || answer(s, two, ASK_US / 1000 + 5000) != 0) {
        snprintf(err, err_len, "no answer from %s", host);
        goto out;
    }
    if (memcmp(two, "OK", 2) != 0) {
        snprintf(err, err_len, "%s", meaning(two));
        goto out;
    }
    for (size_t off = 0; off < len; ) {
        size_t k = len - off < 65536 ? len - off : 65536;
        if (stream_write(s, data + off, k) != 0) {
            snprintf(err, err_len, "connection lost after %lu bytes", (unsigned long)off);
            goto out;
        }
        off += k;
        if (progress) {
            snprintf(line, sizeof line, "%lu of %lu KiB", (unsigned long)(off / 1024),
                     (unsigned long)((len + 1023) / 1024));
            progress(line);
        }
    }
    if (answer(s, two, DATA_US / 1000) != 0) {
        snprintf(err, err_len, "no answer after the game");
        goto out;
    }
    if (memcmp(two, "OK", 2) != 0) {
        snprintf(err, err_len, "%s", meaning(two));
        goto out;
    }
    r = 0;
out:
    stream_close(s);
    return r;
}
