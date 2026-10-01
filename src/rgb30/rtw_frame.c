/*
 * RX buffer walking (rtw88 sdio.c rtw_sdio_rxfifo_recv, rx.c
 * rtw_rx_query_rx_desc, rtw8821c.c query_phy_status; GPL-2.0 OR
 * BSD-3-Clause, used under BSD-3-Clause, Copyright(c) 2018-2019 Realtek
 * Corporation) and the 802.11 management frames of a scan.
 */
#include "rtw_frame.h"

#include <string.h>

#define RX_DESC     24
#define RATE_MAX    0x54                /* DESC_RATE_MAX */
#define MPDU_MAX    11454

static uint32_t le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static const int8_t lna_gain_0[8] = { 22, 8, -6, -22, -31, -40, -46, -52 };
static const int8_t lna_gain_1[16] = { 10, 6, 2, -2, -6, -10, -14, -17,
                                       -20, -24, -28, -31, -34, -37, -40, -44 };

static int phy_rssi(const uint8_t *ps, unsigned rfe)
{
    uint32_t w0 = le32(ps), w3 = le32(ps + 12);
    switch (ps[0] & 0xf) {
    case 0: {                                       /* CCK */
        unsigned vga = (w3 >> 8) & 0x1f, lna = ((w3 >> 23) & 1) << 3 | ((w3 >> 13) & 7);
        if (rfe == 0 ? lna >= 8 : lna >= 16)
            return -120;
        return (rfe == 0 ? lna_gain_0[lna] : lna_gain_1[lna]) - 2 * (int)vga;
    }
    case 1:                                         /* OFDM, HT, VHT */
        return (int)((w0 >> 8) & 0xff) - 110;
    default:
        return -127;
    }
}

int rtw_rx_walk(const uint8_t *buf, uint32_t len, unsigned rfe, rtw_rx_cb cb, void *ctx)
{
    int n = 0;
    while (len >= RX_DESC) {
        uint32_t w0 = le32(buf), w1 = le32(buf + 4), w2 = le32(buf + 8), w3 = le32(buf + 12);
        uint32_t pkt_len = w0 & 0x3fff, drv_info = ((w0 >> 16) & 0xf) * 8, shift = (w0 >> 24) & 3;
        int physt = (w0 >> 26) & 1;
        rtw_rxpkt_t p;
        memset(&p, 0, sizeof p);
        p.rate = (uint8_t)(w3 & 0x7f);
        p.c2h = (w2 >> 28) & 1;
        p.crc_err = (w0 >> 14) & 1;
        p.icv_err = (w0 >> 15) & 1;
        p.enc = (uint8_t)((w0 >> 20) & 7);
        p.decrypted = !((w0 >> 27) & 1) && p.enc != 0;
        p.macid = (uint8_t)(w1 & 0x7f);
        p.rssi = -127;
        uint32_t off = RX_DESC + drv_info + shift;
        if (p.rate >= RATE_MAX || (drv_info && drv_info != 32) || (physt && !drv_info) ||
            pkt_len > MPDU_MAX || off + pkt_len > len || (!p.c2h && pkt_len <= 4))
            return -1 - n;
        p.data = buf + off;
        p.len = p.c2h ? pkt_len : pkt_len - 4;      /* frames end with the FCS */
        if (!p.c2h && physt)
            p.rssi = phy_rssi(buf + RX_DESC + shift, rfe);
        cb(&p, ctx);
        n++;
        uint32_t step = (off + pkt_len + 7) & ~7u;
        if (step + RX_DESC >= len)
            break;
        buf += step;
        len -= step;
    }
    return n;
}

/* --- 802.11 --- */

static int is_suite(const uint8_t *s, uint8_t type)
{
    return s[0] == 0x00 && s[1] == 0x0f && s[2] == 0xac && s[3] == type;
}

static void parse_rsn(const uint8_t *ie, unsigned len, wl_bss_t *b)
{
    b->rsn = 1;
    if (len < 6)
        return;
    b->rsn_group = ie[2 + 3];                       /* version, group suite */
    unsigned pos = 6;
    if (pos + 2 > len)
        return;
    unsigned np = le16(ie + pos);
    pos += 2;
    for (unsigned i = 0; i < np && pos + 4 <= len; i++, pos += 4)
        if (is_suite(ie + pos, 4))
            b->rsn_ccmp = 1;
    if (pos + 2 > len)
        return;
    unsigned na = le16(ie + pos);
    pos += 2;
    for (unsigned i = 0; i < na && pos + 4 <= len; i++, pos += 4) {
        if (is_suite(ie + pos, 2) || is_suite(ie + pos, 6))
            b->rsn_psk = 1;
        if (is_suite(ie + pos, 8))
            b->rsn_sae = 1;
    }
}

static void add_rates(const uint8_t *r, unsigned n, wl_bss_t *b)
{
    for (unsigned i = 0; i < n && b->nrates < sizeof b->rates; i++)
        b->rates[b->nrates++] = r[i];
}

int wl_parse_bss(const uint8_t *f, uint32_t len, int rssi, wl_bss_t *b)
{
    if (len < 36)
        return -1;
    uint16_t kind = wl_kind(f);
    if (kind != WL_FC_BEACON && kind != WL_FC_PROBE_RESP)
        return -1;
    memset(b, 0, sizeof *b);
    memcpy(b->bssid, f + 16, 6);
    b->rssi = rssi;
    b->interval = le16(f + 32);
    b->capab = le16(f + 34);
    const uint8_t *ie = f + 36, *end = f + len;
    while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
        unsigned id = ie[0], n = ie[1];
        const uint8_t *d = ie + 2;
        switch (id) {
        case 0:                                     /* SSID */
            if (n <= 32 && !b->ssid_len) {
                b->ssid_len = (uint8_t)n;
                memcpy(b->ssid_raw, d, n);
                for (unsigned i = 0; i < n; i++)
                    b->ssid[i] = d[i] >= 32 && d[i] <= 126 ? (char)d[i] : '?';
                b->ssid[n] = 0;
                int blank = 1;                      /* hidden: all zero bytes */
                for (unsigned i = 0; i < n; i++)
                    if (d[i]) blank = 0;
                if (blank)
                    b->ssid[0] = 0;
            }
            break;
        case 1:                                     /* supported rates */
        case 50:                                    /* extended rates */
            add_rates(d, n, b);
            break;
        case 3:                                     /* DS parameter set */
            if (n >= 1)
                b->channel = d[0];
            break;
        case 45:                                    /* HT capabilities */
            b->ht = 1;
            break;
        case 48:
            parse_rsn(d, n, b);
            break;
        case 221:                                   /* vendor: WPA1 */
            if (n >= 4 && d[0] == 0x00 && d[1] == 0x50 && d[2] == 0xf2 && d[3] == 0x01)
                b->wpa = 1;
            break;
        }
        ie += 2 + n;
    }
    if (b->rsn)
        b->security = b->rsn_psk ? "WPA2" : b->rsn_sae ? "WPA3" : "EAP";
    else if (b->wpa)
        b->security = "WPA";
    else if (b->capab & 0x10)
        b->security = "WEP";
    else
        b->security = "open";
    return 0;
}

static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

unsigned wl_probe_req(uint8_t *out, const uint8_t mac[6], const char *ssid)
{
    static const uint8_t rates[] = { 1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
                                     50, 4, 0x30, 0x48, 0x60, 0x6c };
    unsigned sl = ssid ? (unsigned)strlen(ssid) : 0;
    if (sl > 32)
        sl = 32;
    memset(out, 0, 24);
    out[0] = WL_FC_PROBE_REQ;
    memcpy(out + 4, bcast, 6);
    memcpy(out + 10, mac, 6);
    memcpy(out + 16, bcast, 6);
    unsigned n = 24;
    out[n++] = 0;
    out[n++] = (uint8_t)sl;
    if (sl)
        memcpy(out + n, ssid, sl);
    n += sl;
    memcpy(out + n, rates, sizeof rates);
    return n + sizeof rates;
}

/* --- joining --- */

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* a management frame's header: kind, A1 = A3 = bssid, A2 = mac */
static unsigned mgmt_hdr(uint8_t *out, uint16_t kind, const uint8_t bssid[6], const uint8_t mac[6])
{
    memset(out, 0, 24);
    out[0] = (uint8_t)kind;
    memcpy(out + 4, bssid, 6);
    memcpy(out + 10, mac, 6);
    memcpy(out + 16, bssid, 6);
    return 24;
}

/* from bssid (A2, A3) to mac or to everyone (A1) */
static int from_ap(const uint8_t *f, uint32_t len, uint16_t kind, const uint8_t bssid[6],
                   const uint8_t mac[6], uint32_t min)
{
    if (len < min || wl_kind(f) != kind || memcmp(f + 10, bssid, 6) != 0)
        return 0;
    return memcmp(f + 4, mac, 6) == 0 || memcmp(f + 4, bcast, 6) == 0;
}

unsigned wl_rsn_ie(uint8_t *out, uint8_t group)
{
    static const uint8_t ie[22] = {
        48, 20, 1, 0,
        0x00, 0x0f, 0xac, 4,                        /* group (patched) */
        1, 0, 0x00, 0x0f, 0xac, 4,                  /* pairwise: CCMP */
        1, 0, 0x00, 0x0f, 0xac, 2,                  /* AKM: PSK */
        0, 0,                                       /* capabilities: no MFP */
    };
    memcpy(out, ie, sizeof ie);
    out[7] = group;
    return sizeof ie;
}

const char *wl_unsupported(const wl_bss_t *b)
{
    for (unsigned i = 0; i < b->nrates; i++)
        if (b->rates[i] == 0xff || b->rates[i] == 0xfe)
            return "the network requires HT or VHT (802.11n/ac only)";
    if (!strcmp(b->security, "WPA3"))
        return "WPA3 only (SAE) is not supported";
    if (!strcmp(b->security, "EAP"))
        return "enterprise networks (802.1X) are not supported";
    if (!strcmp(b->security, "WPA"))
        return "WPA1 (TKIP) is not supported";
    if (!strcmp(b->security, "WEP"))
        return "WEP is not supported";
    if (b->rsn && !b->rsn_ccmp)
        return "the network has no CCMP (AES) for its stations";
    if (b->rsn && b->rsn_group != 4 && b->rsn_group != 2)
        return "the network's group cipher is not CCMP or TKIP";
    return 0;
}

unsigned wl_auth_req(uint8_t *out, const uint8_t bssid[6], const uint8_t mac[6])
{
    unsigned n = mgmt_hdr(out, WL_FC_AUTH, bssid, mac);
    put16(out + n, 0);                              /* open system */
    put16(out + n + 2, 1);                          /* transaction 1 */
    put16(out + n + 4, 0);
    return n + 6;
}

int wl_auth_resp(const uint8_t *f, uint32_t len, const uint8_t bssid[6], const uint8_t mac[6],
                 uint16_t *status)
{
    if (!from_ap(f, len, WL_FC_AUTH, bssid, mac, 30) || le16(f + 24) != 0 || le16(f + 26) != 2)
        return -1;
    *status = le16(f + 28);
    return 0;
}

unsigned wl_assoc_req(uint8_t *out, const wl_bss_t *b, const uint8_t mac[6],
                      const uint8_t *rsn, unsigned rsn_len)
{
    unsigned n = mgmt_hdr(out, WL_FC_ASSOC_REQ, b->bssid, mac);
    unsigned cap = 0x0001 | (b->capab & (0x0010 | 0x0020 | 0x0400));    /* ESS, privacy, short preamble, slot */
    put16(out + n, cap);
    put16(out + n + 2, 10);                         /* listen interval */
    n += 4;
    out[n++] = 0;
    out[n++] = b->ssid_len;
    memcpy(out + n, b->ssid_raw, b->ssid_len);
    n += b->ssid_len;
    uint8_t r[16];
    unsigned nr = 0;
    for (unsigned i = 0; i < b->nrates; i++)
        if (b->rates[i] != 0xff && b->rates[i] != 0xfe)
            r[nr++] = b->rates[i];
    unsigned first = nr > 8 ? 8 : nr;
    out[n++] = 1;
    out[n++] = (uint8_t)first;
    memcpy(out + n, r, first);
    n += first;
    if (nr > first) {
        out[n++] = 50;
        out[n++] = (uint8_t)(nr - first);
        memcpy(out + n, r + first, nr - first);
        n += nr - first;
    }
    if (rsn_len) {
        memcpy(out + n, rsn, rsn_len);
        n += rsn_len;
    }
    return n;
}

int wl_assoc_resp(const uint8_t *f, uint32_t len, const uint8_t bssid[6], const uint8_t mac[6],
                  uint16_t *status, uint16_t *aid)
{
    if (!from_ap(f, len, WL_FC_ASSOC_RESP, bssid, mac, 30) &&
        !from_ap(f, len, 0x0030, bssid, mac, 30))   /* reassociation response */
        return -1;
    *status = le16(f + 26);
    *aid = le16(f + 28) & 0x3fff;
    return 0;
}

int wl_deauth(const uint8_t *f, uint32_t len, const uint8_t bssid[6], const uint8_t mac[6],
              uint16_t *reason)
{
    if (!from_ap(f, len, WL_FC_DEAUTH, bssid, mac, 26) &&
        !from_ap(f, len, WL_FC_DISASSOC, bssid, mac, 26))
        return -1;
    *reason = le16(f + 24);
    return 0;
}

uint32_t wl_rate_mask(const wl_bss_t *b)
{
    static const uint8_t units[12] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
    uint32_t m = 0;
    for (unsigned i = 0; i < b->nrates; i++)
        for (unsigned k = 0; k < 12; k++)
            if ((b->rates[i] & 0x7f) == units[k])
                m |= 1u << k;
    return m;
}

/* --- data --- */

static const uint8_t llc[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

unsigned wl_from_eth(uint8_t *out, const uint8_t *eth, unsigned len, const uint8_t bssid[6],
                     uint16_t seq, int protect, uint64_t pn, uint8_t keyid)
{
    if (len < 14)
        return 0;
    memset(out, 0, 24);
    out[0] = 0x08;                                  /* data */
    out[1] = protect ? 0x41 : 0x01;                 /* ToDS, protected */
    memcpy(out + 4, bssid, 6);
    memcpy(out + 10, eth + 6, 6);                   /* SA: us */
    memcpy(out + 16, eth, 6);                       /* DA */
    put16(out + 22, (unsigned)(seq & 0xfff) << 4);
    unsigned n = 24;
    if (protect) {
        out[n++] = (uint8_t)pn;
        out[n++] = (uint8_t)(pn >> 8);
        out[n++] = 0;
        out[n++] = (uint8_t)(0x20 | (keyid & 3) << 6);   /* ExtIV */
        for (int i = 2; i < 6; i++)
            out[n++] = (uint8_t)(pn >> (8 * i));
    }
    memcpy(out + n, llc, 6);
    n += 6;
    memcpy(out + n, eth + 12, len - 12);            /* type, payload */
    return n + len - 12;
}

int wl_to_eth(const uint8_t *f, uint32_t len, const uint8_t bssid[6], const uint8_t mac[6],
              unsigned head, unsigned tail, uint8_t *out, unsigned max)
{
    if (len < 24 || (f[0] & 0x0c) != 0x08 || (f[1] & 3) != 2 || memcmp(f + 10, bssid, 6) != 0)
        return -1;
    unsigned hdr = 24;
    if (f[0] & 0x80) {                              /* QoS data: QoS control, HT control */
        hdr += 2;
        if (f[1] & 0x80)
            hdr += 4;
    }
    if (f[0] & 0x40)                                /* null data: nothing inside */
        return 0;
    const uint8_t *sa = f + 16;
    if (!memcmp(sa, mac, 6))                        /* our broadcast, sent back */
        return -1;
    unsigned at = hdr + head;
    if (len < at + 8 + tail)
        return -1;
    const uint8_t *p = f + at;
    if (memcmp(p, llc, 5) != 0 || (p[5] != 0x00 && p[5] != 0xf8))
        return -1;
    unsigned n = 12 + (len - at - 6 - tail);
    if (n > max)
        return -1;
    memcpy(out, f + 4, 6);                          /* DA */
    memcpy(out + 6, sa, 6);
    memcpy(out + 12, p + 6, n - 12);                /* type, payload */
    return (int)n;
}
