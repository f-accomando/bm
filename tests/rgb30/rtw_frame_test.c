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

/* --- joining, data --- */

static void test_join(void)
{
    uint8_t ies[128], f[512], out[512];
    wl_bss_t b;
    unsigned n = 0;
    n += ie(ies + n, 0, "CasaRossi", 9);
    n += ie(ies + n, 1, rates, sizeof rates);
    n += ie(ies + n, 3, "\x06", 1);
    n += ie(ies + n, 48, rsn_psk_ccmp, sizeof rsn_psk_ccmp);
    n += ie(ies + n, 50, xrates, sizeof xrates);
    unsigned len = mgmt(f, WL_FC_BEACON, NULL, bssid_a, 0x0431, ies, n);
    CHECK(wl_parse_bss(f, len, -50, &b) == 0 && wl_unsupported(&b) == NULL);
    CHECK(wl_rate_mask(&b) == 0xfff);

    /* authentication */
    unsigned m = wl_auth_req(out, b.bssid, our_mac);
    CHECK(m == 30 && out[0] == 0xb0 && !memcmp(out + 4, bssid_a, 6) && !memcmp(out + 10, our_mac, 6) &&
          !memcmp(out + 16, bssid_a, 6) && out[24] == 0 && out[26] == 1 && out[28] == 0);
    uint8_t resp[64];
    memset(resp, 0, sizeof resp);
    resp[0] = 0xb0;
    memcpy(resp + 4, our_mac, 6);
    memcpy(resp + 10, bssid_a, 6);
    memcpy(resp + 16, bssid_a, 6);
    resp[26] = 2;
    uint16_t status = 99, aid = 0;
    CHECK(wl_auth_resp(resp, 30, bssid_a, our_mac, &status) == 0 && status == 0);
    resp[28] = 17;                                  /* refused: AP full */
    CHECK(wl_auth_resp(resp, 30, bssid_a, our_mac, &status) == 0 && status == 17);
    resp[26] = 1;                                   /* our own request: not an answer */
    CHECK(wl_auth_resp(resp, 30, bssid_a, our_mac, &status) == -1);
    resp[26] = 2;
    resp[10] ^= 1;                                  /* another AP */
    CHECK(wl_auth_resp(resp, 30, bssid_a, our_mac, &status) == -1);
    resp[10] ^= 1;

    /* association request: rates echoed, our RSN IE */
    uint8_t rsn[22];
    CHECK(wl_rsn_ie(rsn, b.rsn_group) == 22 && rsn[0] == 48 && rsn[1] == 20 && rsn[7] == 4 &&
          rsn[13] == 4 && rsn[19] == 2);
    m = wl_assoc_req(out, &b, our_mac, rsn, sizeof rsn);
    CHECK(out[0] == 0x00 && out[24] == 0x31 && out[25] == 0x04 && out[26] == 10);
    CHECK(out[28] == 0 && out[29] == 9 && !memcmp(out + 30, "CasaRossi", 9));
    CHECK(out[39] == 1 && out[40] == 8 && !memcmp(out + 41, rates, 8));
    CHECK(out[49] == 50 && out[50] == 4 && !memcmp(out + 51, xrates, 4));
    CHECK(m == 55 + 22 && !memcmp(out + 55, rsn, 22));
    /* an open network: no RSN IE, no privacy bit */
    b.capab = 0x0001;
    m = wl_assoc_req(out, &b, our_mac, NULL, 0);
    CHECK(m == 55 && out[24] == 0x01 && out[25] == 0);

    /* association response */
    resp[0] = 0x10;
    resp[24] = 0x31; resp[25] = 0x04;               /* capabilities */
    resp[26] = 0; resp[27] = 0;                     /* status */
    resp[28] = 0x05; resp[29] = 0xc0;               /* AID 5 with the two top bits */
    CHECK(wl_assoc_resp(resp, 30, bssid_a, our_mac, &status, &aid) == 0 && status == 0 && aid == 5);
    resp[0] = 0x30;                                 /* reassociation response */
    CHECK(wl_assoc_resp(resp, 30, bssid_a, our_mac, &status, &aid) == 0);
    resp[0] = 0xb0;
    CHECK(wl_assoc_resp(resp, 30, bssid_a, our_mac, &status, &aid) == -1);

    /* deauthentication, to us or to everyone */
    uint16_t reason = 0;
    resp[0] = 0xc0;
    resp[24] = 15; resp[25] = 0;
    CHECK(wl_deauth(resp, 26, bssid_a, our_mac, &reason) == 0 && reason == 15);
    memset(resp + 4, 0xff, 6);
    resp[0] = 0xa0;
    CHECK(wl_deauth(resp, 26, bssid_a, our_mac, &reason) == 0);
    resp[4] = 0x02;                                 /* to another station */
    CHECK(wl_deauth(resp, 26, bssid_a, our_mac, &reason) == -1);

    /* what cannot be joined */
    wl_bss_t t = b;
    t.rates[t.nrates++] = 0xff;
    CHECK(wl_unsupported(&t) != NULL);
    t = b;
    t.security = "WPA3";
    CHECK(wl_unsupported(&t) != NULL);
    t = b;
    t.rsn_ccmp = 0;
    CHECK(wl_unsupported(&t) != NULL);
    t = b;
    t.rsn_group = 2;                                /* TKIP group: joined, CCMP pairwise */
    CHECK(wl_unsupported(&t) == NULL);
}

static void test_data(void)
{
    static const uint8_t ap_mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
    static const uint8_t pc[6] = { 0x3c, 0x22, 0xfb, 0x01, 0x02, 0x03 };
    uint8_t eth[64], f[128], back[128];
    memcpy(eth, pc, 6);
    memcpy(eth + 6, our_mac, 6);
    eth[12] = 0x08; eth[13] = 0x00;                 /* IPv4 */
    for (int i = 14; i < 60; i++)
        eth[i] = (uint8_t)i;

    /* to the AP, plain */
    unsigned n = wl_from_eth(f, eth, 60, ap_mac, 0x123, 0, 0, 0);
    CHECK(n == 24 + 6 + 2 + 46);
    CHECK(f[0] == 0x08 && f[1] == 0x01 && !memcmp(f + 4, ap_mac, 6) && !memcmp(f + 10, our_mac, 6) &&
          !memcmp(f + 16, pc, 6) && f[22] == 0x30 && f[23] == 0x12);
    CHECK(f[24] == 0xaa && f[25] == 0xaa && f[26] == 3 && f[30] == 0x08 && f[31] == 0x00 &&
          !memcmp(f + 32, eth + 14, 46));
    /* protected: CCMP header with the packet number and key id 0 */
    n = wl_from_eth(f, eth, 60, ap_mac, 7, 1, 0x0000a1b2c3d4e5f6ull, 0);
    CHECK(n == 24 + 8 + 6 + 2 + 46 && f[1] == 0x41);
    CHECK(f[24] == 0xf6 && f[25] == 0xe5 && f[26] == 0 && f[27] == 0x20 && f[28] == 0xd4 &&
          f[29] == 0xc3 && f[30] == 0xb2 && f[31] == 0xa1 && f[32] == 0xaa);

    /* from the AP: FromDS, A1 = us, A2 = BSSID, A3 = the sender */
    uint8_t in[160];
    memset(in, 0, 24);
    in[0] = 0x08; in[1] = 0x42;                     /* data, FromDS, protected */
    memcpy(in + 4, our_mac, 6);
    memcpy(in + 10, ap_mac, 6);
    memcpy(in + 16, pc, 6);
    memset(in + 24, 0x77, 8);                       /* CCMP header */
    memcpy(in + 32, "\xaa\xaa\x03\x00\x00\x00\x08\x06", 8);   /* ARP */
    for (int i = 0; i < 28; i++)
        in[40 + i] = (uint8_t)(0x80 + i);
    memset(in + 68, 0x55, 8);                       /* MIC */
    int r = wl_to_eth(in, 76, ap_mac, our_mac, 8, 8, back, sizeof back);
    CHECK(r == 14 + 28 && !memcmp(back, our_mac, 6) && !memcmp(back + 6, pc, 6) &&
          back[12] == 0x08 && back[13] == 0x06 && back[14] == 0x80 && back[41] == 0x80 + 27);
    /* QoS data: 2 more header bytes */
    uint8_t q[160];
    memcpy(q, in, 24);
    q[0] = 0x88;
    q[24] = 0; q[25] = 0;
    memcpy(q + 26, in + 24, 52);
    CHECK(wl_to_eth(q, 78, ap_mac, our_mac, 8, 8, back, sizeof back) == 14 + 28 && back[14] == 0x80);
    /* null data, another BSS, our own broadcast reflected, ToDS, no LLC */
    in[0] = 0x48;
    CHECK(wl_to_eth(in, 24, ap_mac, our_mac, 0, 0, back, sizeof back) == 0);
    in[0] = 0x08;
    in[11] ^= 1;
    CHECK(wl_to_eth(in, 76, ap_mac, our_mac, 8, 8, back, sizeof back) == -1);
    in[11] ^= 1;
    memcpy(in + 16, our_mac, 6);
    CHECK(wl_to_eth(in, 76, ap_mac, our_mac, 8, 8, back, sizeof back) == -1);
    memcpy(in + 16, pc, 6);
    in[1] = 0x41;
    CHECK(wl_to_eth(in, 76, ap_mac, our_mac, 8, 8, back, sizeof back) == -1);
    in[1] = 0x42;
    in[32] = 0x42;
    CHECK(wl_to_eth(in, 76, ap_mac, our_mac, 8, 8, back, sizeof back) == -1);
    in[32] = 0xaa;
    /* too long for the caller's buffer, too short for its header */
    CHECK(wl_to_eth(in, 76, ap_mac, our_mac, 8, 8, back, 20) == -1);
    CHECK(wl_to_eth(in, 40, ap_mac, our_mac, 8, 8, back, sizeof back) == -1);
    /* round trip, plain: what we send, as the AP relays it back to us */
    n = wl_from_eth(f, eth, 60, ap_mac, 1, 0, 0, 0);
    memcpy(in, f, n);
    in[1] = 0x02;
    memcpy(in + 4, our_mac, 6);
    memcpy(in + 10, ap_mac, 6);
    memcpy(in + 16, pc, 6);
    eth[0] = our_mac[0];
    r = wl_to_eth(in, n, ap_mac, our_mac, 0, 0, back, sizeof back);
    CHECK(r == 60 && !memcmp(back + 12, eth + 12, 48));
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
    test_join();
    test_data();
    test_rx();
    if (fails) {
        printf("rtw_frame_test: %d failures\n", fails);
        return 1;
    }
    printf("rtw_frame_test: ok\n");
    return 0;
}
