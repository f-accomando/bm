/*
 * The RGB30's WiFi without hardware (src/rgb30/rtw_frame.c): an RX buffer
 * with aggregated packets as the RTL8821C fills it (24-byte descriptors,
 * PHY status, FCS, 8-byte alignment, a C2H message, a bad CRC), beacons
 * and probe responses of every kind of network, a probe request.
 */
#include "rgb30/rtw_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* --- frames --- */

static unsigned ie(uint8_t *p, uint8_t id, const void *d, unsigned n)
{
    p[0] = id;
    p[1] = (uint8_t)n;
    memcpy(p + 2, d, n);
    return 2 + n;
}

static const uint8_t bssid_a[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const uint8_t our_mac[6] = { 0x00, 0xe0, 0x4c, 0x88, 0x21, 0xc0 };

/* a beacon (or probe response to `to`) with the given IEs after the fixed fields */
static unsigned mgmt(uint8_t *f, uint16_t kind, const uint8_t *to, const uint8_t *bssid,
                     uint16_t capab, const uint8_t *ies, unsigned ies_len)
{
    memset(f, 0, 36);
    f[0] = (uint8_t)kind;
    memcpy(f + 4, to ? to : (const uint8_t *)"\xff\xff\xff\xff\xff\xff", 6);
    memcpy(f + 10, bssid, 6);
    memcpy(f + 16, bssid, 6);
    f[32] = 100;                                    /* beacon interval */
    f[34] = (uint8_t)capab;
    f[35] = (uint8_t)(capab >> 8);
    memcpy(f + 36, ies, ies_len);
    return 36 + ies_len;
}

static const uint8_t rsn_psk_ccmp[] = {
    1, 0, 0x00, 0x0f, 0xac, 4,                      /* version, group CCMP */
    1, 0, 0x00, 0x0f, 0xac, 4,                      /* pairwise: CCMP */
    1, 0, 0x00, 0x0f, 0xac, 2,                      /* AKM: PSK */
    0x0c, 0,
};
static const uint8_t rsn_sae[] = {
    1, 0, 0x00, 0x0f, 0xac, 4,
    1, 0, 0x00, 0x0f, 0xac, 4,
    1, 0, 0x00, 0x0f, 0xac, 8,                      /* AKM: SAE */
    0xcc, 0,
};
static const uint8_t rsn_transition[] = {
    1, 0, 0x00, 0x0f, 0xac, 2,                      /* group TKIP */
    2, 0, 0x00, 0x0f, 0xac, 2, 0x00, 0x0f, 0xac, 4,
    2, 0, 0x00, 0x0f, 0xac, 8, 0x00, 0x0f, 0xac, 2, /* SAE and PSK */
};
static const uint8_t rsn_eap[] = {
    1, 0, 0x00, 0x0f, 0xac, 4,
    1, 0, 0x00, 0x0f, 0xac, 4,
    1, 0, 0x00, 0x0f, 0xac, 1,                      /* 802.1X */
};
static const uint8_t wpa1[] = { 0x00, 0x50, 0xf2, 0x01, 1, 0, 0x00, 0x50, 0xf2, 2 };
static const uint8_t rates[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
static const uint8_t xrates[] = { 0x30, 0x48, 0x60, 0x6c };

static void test_bss(void)
{
    uint8_t ies[256], f[512];
    wl_bss_t b;
    unsigned n = 0;
    n += ie(ies + n, 0, "CasaRossi", 9);
    n += ie(ies + n, 1, rates, sizeof rates);
    n += ie(ies + n, 3, "\x06", 1);
    n += ie(ies + n, 45, "\x2c\x01", 2);
    n += ie(ies + n, 48, rsn_psk_ccmp, sizeof rsn_psk_ccmp);
    n += ie(ies + n, 50, xrates, sizeof xrates);
    unsigned len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0431, ies, n);
    CHECK(wl_parse_bss(f, len, -48, &b) == 0);
    CHECK(strcmp(b.ssid, "CasaRossi") == 0 && b.ssid_len == 9);
    CHECK(memcmp(b.bssid, bssid_a, 6) == 0);
    CHECK(b.channel == 6 && b.rssi == -48 && b.interval == 100 && b.capab == 0x0431);
    CHECK(strcmp(b.security, "WPA2") == 0);
    CHECK(b.rsn && b.rsn_psk && b.rsn_ccmp && b.rsn_group == 4 && !b.rsn_sae && b.ht);
    CHECK(b.nrates == 12 && b.rates[0] == 0x82 && b.rates[11] == 0x6c);

    /* a truncated IE at the end is ignored, the rest still read */
    CHECK(wl_parse_bss(f, len - 3, -48, &b) == 0 && strcmp(b.security, "WPA2") == 0 &&
          b.nrates == 8);

    n = 0;
    n += ie(ies + n, 0, "Wpa3Only", 8);
    n += ie(ies + n, 48, rsn_sae, sizeof rsn_sae);
    len = mgmt(f, WL_FC_PROBE_RESP, our_mac, bssid_a, 0x0011, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && strcmp(b.security, "WPA3") == 0 && b.channel == 0);

    n = 0;
    n += ie(ies + n, 0, "Mixed", 5);
    n += ie(ies + n, 48, rsn_transition, sizeof rsn_transition);
    len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0011, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && strcmp(b.security, "WPA2") == 0 &&
          b.rsn_sae && b.rsn_psk && b.rsn_ccmp && b.rsn_group == 2);

    n = 0;
    n += ie(ies + n, 0, "Office", 6);
    n += ie(ies + n, 48, rsn_eap, sizeof rsn_eap);
    len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0011, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && strcmp(b.security, "EAP") == 0);

    n = 0;
    n += ie(ies + n, 0, "Old", 3);
    n += ie(ies + n, 221, wpa1, sizeof wpa1);
    len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0011, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && strcmp(b.security, "WPA") == 0 && b.wpa);

    n = 0;
    n += ie(ies + n, 0, "Wep", 3);
    len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0011, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && strcmp(b.security, "WEP") == 0);

    n = 0;
    n += ie(ies + n, 0, "Caf\xe8\x01", 5);          /* not printable */
    len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0001, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && strcmp(b.security, "open") == 0 &&
          strcmp(b.ssid, "Caf??") == 0 && b.ssid_raw[3] == 0xe8);

    n = 0;
    n += ie(ies + n, 0, "\0\0\0\0", 4);             /* hidden */
    len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0001, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == 0 && b.ssid[0] == 0 && b.ssid_len == 4);

    /* not a beacon, too short */
    len = mgmt(f, WL_FC_AUTH, our_mac, bssid_a, 0, ies, n);
    CHECK(wl_parse_bss(f, len, -70, &b) == -1);
    CHECK(wl_parse_bss(f, 30, -70, &b) == -1);
}

static void test_probe_req(void)
{
    uint8_t f[128];
    unsigned n = wl_probe_req(f, our_mac, NULL);
    CHECK(n == 24 + 2 + 10 + 6);
    CHECK(f[0] == 0x40 && f[1] == 0 && memcmp(f + 4, "\xff\xff\xff\xff\xff\xff", 6) == 0);
    CHECK(memcmp(f + 10, our_mac, 6) == 0 && memcmp(f + 16, "\xff\xff\xff\xff\xff\xff", 6) == 0);
    CHECK(f[24] == 0 && f[25] == 0 && f[26] == 1 && f[27] == 8 && f[28] == 0x82 && f[36] == 50);
    n = wl_probe_req(f, our_mac, "Nascosta");
    CHECK(n == 24 + 10 + 10 + 6 && f[25] == 8 && memcmp(f + 26, "Nascosta", 8) == 0);
}

/* --- RX buffer --- */

typedef struct {
    int n;
    rtw_rxpkt_t p[8];
    uint8_t copy[8][600];
} seen_t;

static void on_packet(const rtw_rxpkt_t *p, void *v)
{
    seen_t *s = v;
    if (s->n < 8) {
        s->p[s->n] = *p;
        memcpy(s->copy[s->n], p->data, p->len < 600 ? p->len : 600);
        s->n++;
    }
}

/* appends one packet: descriptor, PHY status (page, w0, w3) if phy, shift
 * bytes, the payload and (frames) a 4-byte FCS; pads to 8 bytes */
static unsigned add_pkt(uint8_t *buf, unsigned at, const uint8_t *payload, unsigned len,
                        int c2h, int phy, uint32_t ps_w0, uint32_t ps_w3, unsigned shift,
                        uint32_t extra_w0, uint8_t rate)
{
    uint8_t *d = buf + at;
    memset(d, 0, 24);
    unsigned pkt_len = c2h ? len : len + 4;
    put32(d, pkt_len | (phy ? 4u << 16 | 1u << 26 : 0) | shift << 24 | extra_w0);
    put32(d + 8, c2h ? 1u << 28 : 0);
    put32(d + 12, rate);
    unsigned off = 24;
    if (phy) {
        memset(d + off, 0, 32);
        put32(d + off + shift, ps_w0);
        put32(d + off + shift + 12, ps_w3);
        off += 32;
    }
    off += shift;
    memcpy(d + off, payload, len);
    if (!c2h)
        put32(d + off + len, 0xdeadbeef);           /* FCS */
    unsigned end = off + pkt_len;
    return at + ((end + 7) & ~7u);
}

static void test_rx(void)
{
    static uint8_t buf[4096];
    uint8_t ies[64], beacon[128], resp[128];
    unsigned n = 0;
    n += ie(ies + n, 0, "Uno", 3);
    n += ie(ies + n, 3, "\x0b", 1);
    unsigned blen = mgmt(beacon, WL_FC_BEACON, NULL, bssid_a, 0x0001, ies, n);
    unsigned rlen = mgmt(resp, WL_FC_PROBE_RESP, our_mac, bssid_a, 0x0001, ies, n);
    static const uint8_t c2h_msg[] = { 0x09, 0x01, 0xaa, 0xbb, 0xcc };   /* BT info */

    memset(buf, 0x5a, sizeof buf);
    unsigned at = 0;
    /* 1: OFDM beacon, PHY status page 1, PWDB 70 -> -40 dBm */
    at = add_pkt(buf, at, beacon, blen, 0, 1, 0x00004601, 0, 0, 0, 4);
    /* 2: a firmware message */
    at = add_pkt(buf, at, c2h_msg, sizeof c2h_msg, 1, 0, 0, 0, 0, 0, 0);
    /* 3: CCK probe response, page 0: LNA 1 (8 dB on RFE 0), VGA 10 -> -12 dBm;
     *    shifted by 2 bytes */
    at = add_pkt(buf, at, resp, rlen, 0, 1, 0x00000000, 1u << 13 | 10u << 8, 2, 0, 1);
    /* 4: bad CRC, no PHY status */
    at = add_pkt(buf, at, beacon, blen, 0, 0, 0, 0, 0, 1u << 14, 0);
    seen_t s;
    memset(&s, 0, sizeof s);
    int r = rtw_rx_walk(buf, at, 0, on_packet, &s);
    CHECK(r == 4 && s.n == 4);
    CHECK(s.p[0].len == blen && memcmp(s.copy[0], beacon, blen) == 0 && s.p[0].rssi == -40);
    CHECK(!s.p[0].c2h && !s.p[0].crc_err && s.p[0].rate == 4);
    CHECK(s.p[1].c2h && s.p[1].len == sizeof c2h_msg && memcmp(s.copy[1], c2h_msg, 5) == 0);
    CHECK(s.p[2].len == rlen && memcmp(s.copy[2], resp, rlen) == 0 && s.p[2].rssi == 8 - 20);
    CHECK(s.p[3].crc_err && s.p[3].rssi == -127 && s.p[3].len == blen);

    /* RFE 2: the other CCK gain table (LNA 1 = 6 dB) */
    memset(&s, 0, sizeof s);
    CHECK(rtw_rx_walk(buf, at, 2, on_packet, &s) == 4 && s.p[2].rssi == 6 - 20);

    /* the length the chip reports may include padding after the last packet */
    memset(&s, 0, sizeof s);
    CHECK(rtw_rx_walk(buf, at + 16, 0, on_packet, &s) == 4);

    wl_bss_t b;
    CHECK(wl_parse_bss(s.copy[0], s.p[0].len, s.p[0].rssi, &b) == 0 && b.channel == 11 &&
          strcmp(b.ssid, "Uno") == 0);

    /* a packet running past the buffer: dropped, with what came before */
    memset(&s, 0, sizeof s);
    CHECK(rtw_rx_walk(buf, at - 8, 0, on_packet, &s) == -1 - 3 && s.n == 3);

    /* a bad DRV_INFO_SIZE (not 0 or 4) */
    uint8_t bad[64];
    memset(bad, 0, sizeof bad);
    unsigned e = add_pkt(bad, 0, beacon, 20, 0, 0, 0, 0, 0, 2u << 16, 0);
    memset(&s, 0, sizeof s);
    CHECK(rtw_rx_walk(bad, e, 0, on_packet, &s) == -1 && s.n == 0);
    /* PHY status flag without driver info */
    e = add_pkt(bad, 0, beacon, 20, 0, 0, 0, 0, 0, 1u << 26, 0);
    CHECK(rtw_rx_walk(bad, e, 0, on_packet, &s) == -1);
    /* a frame no longer than its FCS */
    e = add_pkt(bad, 0, beacon, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK(rtw_rx_walk(bad, e, 0, on_packet, &s) == -1);
    /* a rate code past the last one */
    e = add_pkt(bad, 0, beacon, 20, 0, 0, 0, 0, 0, 0, 0x60);
    CHECK(rtw_rx_walk(bad, e, 0, on_packet, &s) == -1);
    /* nothing, or less than a descriptor */
    CHECK(rtw_rx_walk(bad, 0, 0, on_packet, &s) == 0 && rtw_rx_walk(bad, 20, 0, on_packet, &s) == 0);
}

int main(void)
{
    test_bss();
    test_probe_req();
    test_rx();
    if (fails) {
        printf("rtw_frame_test: %d failures\n", fails);
        return 1;
    }
    printf("rtw_frame_test: ok\n");
    return 0;
}
