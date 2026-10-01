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
        p.decrypted = !((w0 >> 27) & 1) && ((w0 >> 20) & 7) != 0;
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
