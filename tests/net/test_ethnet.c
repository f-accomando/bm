/*
 * Host test of the Pi 1 B's network path: lwIP and src/net/net.c on the
 * Ethernet driver (src/usb/smsc95xx.c), itself on a simulated LAN9512
 * (tests/usb/lan9512_sim.c). A small peer on the "cable" answers ARP,
 * DHCP (192.168.1.50 for us) and sends a ping. The cable is plugged in
 * after the start, pulled and plugged in again. The cartridges' UDP
 * (src/net/cartnet.c): a packet from the peer to a game's socket, a
 * broadcast of the game on the cable.
 */
#include "net/net.h"
#include "net/cartnet.h"
#include "usb/smsc95xx.h"
#include "../usb/lan9512_sim.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- what net.c needs besides lwIP and the Ethernet ---- */
static char logbuf[16384];
int kprintf(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(logbuf);
    va_start(ap, fmt);
    int r = vsnprintf(logbuf + n, sizeof logbuf - n, fmt, ap);
    va_end(ap);
    return r;
}
int netcon_start(void) { return -1; }
void netcon_poll(void) {}
const char *netcon_password(void) { return ""; }
int netxfer_start(void) { return -1; }
void netxfer_poll(void) {}
int wifi_linked(void) { return 0; }
const unsigned char *wifi_mac(void) { return NULL; }
void wifi_poll(void) {}
int wifi_recv(void *buf, int max) { (void)buf; (void)max; return 0; }
int wifi_send(const void *eth, int len) { (void)eth; (void)len; return -1; }

/* ---- the peer: 192.168.1.1, router and DHCP server ---- */
static const uint8_t peer_mac[6] = { 0x02, 0, 0, 0, 0, 1 };
static const uint8_t peer_ip[4] = { 192, 168, 1, 1 }, our_ip[4] = { 192, 168, 1, 50 };
static int discovers, requests, arp_asks, echo_replies, bad_frames, game_bcasts;
static uint8_t f[1600];

static uint16_t csum(const uint8_t *p, int len)
{
    uint32_t s = 0;
    for (int i = 0; i + 1 < len; i += 2)
        s += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (len & 1)
        s += (uint32_t)(p[len - 1] << 8);
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)~s;
}

/* Ethernet + IPv4 headers into f; returns the offset of the IP payload */
static int ip_frame(const uint8_t *dst_mac, const uint8_t *dst_ip, uint8_t proto, int payload)
{
    memcpy(f, dst_mac, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08; f[13] = 0x00;
    uint8_t *ip = f + 14;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (uint8_t)((20 + payload) >> 8); ip[3] = (uint8_t)(20 + payload);
    ip[8] = 64;
    ip[9] = proto;
    memcpy(ip + 12, peer_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    uint16_t c = csum(ip, 20);
    ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)c;
    return 34;
}

static void dhcp_reply(const uint8_t *req, uint8_t type)
{
    static const uint8_t bcast[6] = { 255, 255, 255, 255, 255, 255 }, all[4] = { 255, 255, 255, 255 };
    int len = 8 + 236 + 4 + 3 + 6 + 6 + 6 + 6 + 1;
    int o = ip_frame(req + 28, all, 17, len);       /* to the client's MAC (chaddr) */
    (void)bcast;
    uint8_t *u = f + o;
    memset(u, 0, (size_t)len);
    u[1] = 67; u[3] = 68;
    u[4] = (uint8_t)(len >> 8); u[5] = (uint8_t)len;
    uint8_t *b = u + 8;
    b[0] = 2; b[1] = 1; b[2] = 6;
    memcpy(b + 4, req + 4, 4);                      /* xid */
    memcpy(b + 16, our_ip, 4);                      /* yiaddr */
    memcpy(b + 28, req + 28, 16);                   /* chaddr */
    uint8_t *p = b + 236;
    p[0] = 99; p[1] = 130; p[2] = 83; p[3] = 99;
    p += 4;
    *p++ = 53; *p++ = 1; *p++ = type;
    *p++ = 54; *p++ = 4; memcpy(p, peer_ip, 4); p += 4;
    *p++ = 51; *p++ = 4; *p++ = 0; *p++ = 0; *p++ = 0x0E; *p++ = 0x10;     /* 1 hour */
    *p++ = 1; *p++ = 4; *p++ = 255; *p++ = 255; *p++ = 255; *p++ = 0;
    *p++ = 3; *p++ = 4; memcpy(p, peer_ip, 4); p += 4;
    *p++ = 255;
    sim_queue_rx(f, (uint32_t)(o + len), 0);
}

/* Every frame the driver sends, after its 8-byte command header. */
static void on_out(const uint8_t *data, uint32_t n)
{
    if (n == 0)
        return;                                     /* the empty packet after whole ones */
    uint32_t len = (uint32_t)data[4] | (uint32_t)data[5] << 8;
    const uint8_t *e = data + 8;
    if (n != len + 8 || len < 14) {
        bad_frames++;
        return;
    }
    if (e[12] == 0x08 && e[13] == 0x06) {           /* ARP */
        const uint8_t *a = e + 14;
        if (a[7] == 1 && memcmp(a + 24, peer_ip, 4) == 0) {
            arp_asks++;
            memcpy(f, a + 8, 6);
            memcpy(f + 6, peer_mac, 6);
            f[12] = 0x08; f[13] = 0x06;
            memcpy(f + 14, a, 6);                   /* Ethernet / IPv4, sizes */
            f[20] = 0; f[21] = 2;                   /* reply */
            memcpy(f + 22, peer_mac, 6);
            memcpy(f + 28, peer_ip, 4);
            memcpy(f + 32, a + 8, 10);              /* the asker's MAC and IP */
            sim_queue_rx(f, 60, 0);
        }
        return;
    }
    if (e[12] != 0x08 || e[13] != 0x00)
        return;
    const uint8_t *ip = e + 14;
    int ihl = (ip[0] & 15) * 4;
    if (csum(ip, ihl) != 0) {
        bad_frames++;
        return;
    }
    if (ip[9] == 17 && ip[ihl + 3] == 67) {         /* DHCP */
        const uint8_t *b = ip + ihl + 8;
        CHECK(memcmp(b + 28, sim_board_mac, 6) == 0);
        uint8_t type = 0;
        for (const uint8_t *p = b + 240; p < e + len && *p != 255; p += p[0] ? p[1] + 2 : 1)
            if (p[0] == 53)
                type = p[2];
        if (type == 1) {
            discovers++;
            dhcp_reply(b, 2);                       /* offer */
        } else if (type == 3) {
            requests++;
            dhcp_reply(b, 5);                       /* ack */
        }
    } else if (ip[9] == 17 && (ip[ihl + 2] << 8 | ip[ihl + 3]) == 47310) {
        /* a game's broadcast: to everyone on the cable */
        static const uint8_t all[4] = { 255, 255, 255, 255 };
        CHECK(memcmp(e, "\xff\xff\xff\xff\xff\xff", 6) == 0 && memcmp(ip + 16, all, 4) == 0);
        CHECK(memcmp(ip + ihl + 8, "OB1hello", 8) == 0);
        game_bcasts++;
    } else if (ip[9] == 1 && ip[ihl] == 0 && memcmp(ip + 16, peer_ip, 4) == 0) {
        const uint8_t *icmp = ip + ihl;             /* echo reply to us */
        int ilen = (ip[2] << 8 | ip[3]) - ihl;
        CHECK(csum(icmp, ilen) == 0);
        CHECK(icmp[4] == 0x12 && icmp[5] == 0x34 && icmp[7] == 7 && ilen == 8 + 32);
        CHECK(icmp[8] == 'p' && icmp[8 + 31] == 'p' + 31);
        echo_replies++;
    }
}

static void ping(void)
{
    int o = ip_frame(sim_board_mac, our_ip, 1, 8 + 32);
    uint8_t *icmp = f + o;
    memset(icmp, 0, 8);
    icmp[0] = 8;
    icmp[4] = 0x12; icmp[5] = 0x34; icmp[7] = 7;
    for (int i = 0; i < 32; i++)
        icmp[8 + i] = (uint8_t)('p' + i);
    uint16_t c = csum(icmp, 8 + 32);
    icmp[2] = (uint8_t)(c >> 8); icmp[3] = (uint8_t)c;
    sim_queue_rx(f, (uint32_t)(o + 8 + 32), 0);
}

/* a UDP packet from the peer's port 47320 to ours, 47310 */
static void game_packet(const char *msg)
{
    int n = (int)strlen(msg);
    int o = ip_frame(sim_board_mac, our_ip, 17, 8 + n);
    uint8_t *u = f + o;
    u[0] = 47320 >> 8; u[1] = 47320 & 255; u[2] = 47310 >> 8; u[3] = 47310 & 255;
    u[4] = (uint8_t)((8 + n) >> 8); u[5] = (uint8_t)(8 + n);
    u[6] = u[7] = 0;                                /* no checksum (allowed for UDP) */
    memcpy(u + 8, msg, (size_t)n);
    sim_queue_rx(f, (uint32_t)(o + 8 + n), 0);
}

static void run(int ms)
{
    for (int i = 0; i < ms; i++) {
        sim_now_us += 1000;
        net_poll();
    }
}

int main(void)
{
    sim_on_out = on_out;
    CHECK(eth_attach(&sim_dev, sim_cfg, sizeof sim_cfg) == 0);

    /* started without a cable: DHCP waits for the link */
    CHECK(net_start(&net_eth) == 0);
    CHECK(strstr(logbuf, "once the cable has a link") != NULL);
    run(2000);
    CHECK(discovers == 0 && net_ip() == 0);

    /* cable in: link within half a second, then the address */
    sim_link(1, 0x45E1);
    run(3000);
    CHECK(strstr(logbuf, "eth: link up, 100 Mbit/s full duplex") != NULL);
    CHECK(discovers == 1 && requests == 1);
    CHECK(strcmp(net_ip_text(), "192.168.1.50") == 0);
    CHECK(strstr(logbuf, "net: IP 192.168.1.50") != NULL);

    /* a ping from the router: ARP for its address, then the echo reply */
    ping();
    run(50);
    CHECK(arp_asks == 1 && echo_replies == 1);
    ping();
    run(50);
    CHECK(arp_asks == 1 && echo_replies == 2);     /* the ARP entry is kept */

    /* cable out and in again: the lease stays, DHCP checks it */
    sim_link(0, 0);
    run(1000);
    CHECK(strstr(logbuf, "eth: link down") != NULL);
    CHECK(strcmp(net_ip_text(), "192.168.1.50") == 0);
    sim_link(1, 0x45E1);
    run(2000);
    CHECK(requests == 2 && discovers == 1);
    CHECK(strcmp(net_ip_text(), "192.168.1.50") == 0);
    ping();
    run(50);
    CHECK(echo_replies == 3);
    CHECK(bad_frames == 0);

    /* a game's socket: what the peer sends arrives, a broadcast goes out */
    int sk = cartnet_open(47310);
    CHECK(sk >= 0 && cartnet_port(sk) == 47310);
    CHECK(cartnet_ip() == 0xC0A80132u);
    char buf[64];
    uint32_t from;
    uint16_t port;
    CHECK(cartnet_recv(sk, buf, sizeof buf, &from, &port) < 0);
    game_packet("OB1 inputs");
    run(5);
    int n = cartnet_recv(sk, buf, sizeof buf, &from, &port);
    CHECK(n == 10 && memcmp(buf, "OB1 inputs", 10) == 0 && from == 0xC0A80101u && port == 47320);
    CHECK(cartnet_recv(sk, buf, sizeof buf, &from, &port) < 0);
    CHECK(cartnet_send(sk, CARTNET_BROADCAST, 47310, "OB1hello", 8) == 0);
    run(5);
    CHECK(game_bcasts == 1);
    CHECK(cartnet_resolve("192.168.1.9") == 0xC0A80109u);
    cartnet_reset();
    CHECK(cartnet_recv(sk, buf, sizeof buf, &from, &port) < 0);

    /* the WiFi cannot take over the running interface */
    CHECK(net_start(&net_wifi) == -1);

    fails += sim_fails;
    if (fails)
        printf("%s", logbuf);
    printf("test_ethnet: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
