/*
 * RTL8821C MAC and radio set-up, ported from rtw88 (mac.c, phy.c,
 * rtw8821c.c, coex.c, fw.c, sec.c; GPL-2.0 OR BSD-3-Clause, used under
 * BSD-3-Clause, Copyright(c) 2018-2019 Realtek Corporation): TX/RX queues
 * and FIFO pages, the H2C queue, the 8821C MAC registers, the BB/AGC/RF
 * tables with their conditions (SDIO, chip cut, package, RFE type), the
 * WiFi-only antenna path, the security engine, channel switching and the
 * TX power from the efuse.
 */
#ifdef PLAT_RK3566
#include "rtw.h"
#include "rtw_phy.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

#define RFREG_MASK  0xfffffu

static uint32_t ch_param[3];
static int rfe_btg;
static uint16_t h2c_seq;
static unsigned h2c_box;

/* --- RF (path A): writes through the 3-wire SIPI port, reads direct --- */

uint32_t rtw_read_rf(uint32_t addr, uint32_t mask)
{
    uint32_t v = rtw_r32(0x2800 + ((addr & 0xff) << 2)) & mask;
    return mask ? v >> __builtin_ctz(mask) : v;
}

void rtw_write_rf(uint32_t addr, uint32_t mask, uint32_t data)
{
    addr &= 0xff;
    mask &= RFREG_MASK;
    if (mask != RFREG_MASK) {
        uint32_t old = rtw_read_rf(addr, RFREG_MASK);
        data = (old & ~mask) | ((data << __builtin_ctz(mask)) & mask);
    }
    rtw_w32(0xc90, ((addr << 20) | (data & 0xfffff)) & 0x0fffffff);
    timer_delay_us(13);
}

/* --- the conditional tables --- */

static uint32_t drv_cut, drv_pkg, drv_rfe;

static int check_positive(uint32_t c)
{
    uint32_t rfe = c & 0xff, intf = (c >> 8) & 0xf, pkg = (c >> 12) & 0xf, cut = (c >> 24) & 0xf;
    if (cut && cut != drv_cut)
        return 0;
    if (pkg && pkg != drv_pkg)
        return 0;
    if (intf && intf != 4)                          /* SDIO */
        return 0;
    return rfe == drv_rfe;
}

enum { CFG_MAC, CFG_BB, CFG_AGC, CFG_RF };

static unsigned load_table(const uint32_t *t, unsigned n, int kind)
{
    int matched = 1, skipped = 0;
    uint32_t pos = 0;
    unsigned writes = 0;
    for (unsigned i = 0; i + 1 < n; i += 2) {
        uint32_t a = t[i], d = t[i + 1];
        if (a & (1u << 31)) {
            switch ((a >> 28) & 3) {
            case 3: matched = 1; skipped = 0; break;            /* ENDIF */
            case 2: matched = !skipped; break;                  /* ELSE */
            default: pos = a; break;                            /* IF, ELIF */
            }
        } else if (a & (1u << 30)) {
            if (!skipped) {
                matched = skipped = check_positive(pos);
            } else {
                matched = 0;
            }
        } else if (matched) {
            writes++;
            switch (kind) {
            case CFG_MAC:
                rtw_w8(a, (uint8_t)d);
                break;
            case CFG_BB:
                if (a == 0xfe) timer_delay_ms(50);
                else if (a == 0xfd) timer_delay_ms(5);
                else if (a == 0xfc) timer_delay_ms(1);
                else if (a == 0xfb) timer_delay_us(50);
                else if (a == 0xfa) timer_delay_us(5);
                else if (a == 0xf9) timer_delay_us(1);
                else rtw_w32(a, d);
                break;
            case CFG_AGC:
                rtw_w32(a, d);
                break;
            case CFG_RF:
                if (a == 0xffe) timer_delay_ms(50);
                else if (a == 0xfe) timer_delay_us(100);
                else {
                    rtw_write_rf(a, RFREG_MASK, d);
                    timer_delay_us(1);
                }
                break;
            }
        }
    }
    return writes;
}

/* --- MAC --- */

static int mac_init(char *err, unsigned errlen)
{
    /* queue mapping (SDIO: VO, VI normal; BE, BK low; MG extra; HI high) */
    rtw_w16(REG_TXDMA_PQ_MAP, 0xc5a0);
    rtw_w8(REG_CR, 0);
    rtw_w8(REG_CR, 0xff);
    rtw_w32(REG_H2CQ_CSR, 1u << 31);
    (void)rtw_local_r32(SDIO_FREE_TXPG);
    rtw_local_w32(SDIO_TX_CTRL, 0);
    /* FIFO pages: 512, reserved 52 from page 460 */
    rtw_w16(REG_FIFOPAGE_INFO_1, 16);
    rtw_w16(0x234, 16);
    rtw_w16(0x238, 16);
    rtw_w16(0x23c, 14);
    rtw_w16(0x240, 397);
    rtw_set32(REG_RQPN_CTRL_2, 1u << 31);
    rtw_w16(REG_FIFOPAGE_CTRL_2, 460);
    rtw_set8(0x420 + 2, 1u << 4);                   /* EN_WR_FREE_TAIL */
    rtw_w16(0x424, 460);
    rtw_w16(REG_FIFOPAGE_CTRL_2 + 2, 460);
    rtw_w16(0x456, 460);
    rtw_w32(0x11c, 16384 - 256 - 1);                /* RXFF boundary */
    rtw_set8(0x208, 1);                             /* auto LLT */
    if (!rtw_poll32(0x208, 1, 0)) {
        ksnprintf(err, errlen, "LLT init does not finish");
        return -1;
    }
    rtw_w8(REG_CR + 3, 0);
    /* H2C queue at page 500 (0xfa00), 1024 bytes */
    rtw_w32(0x244, (rtw_r32(0x244) & 0xfffc0000u) | 0xfa00);
    rtw_w32(0x24c, (rtw_r32(0x24c) & 0xfffc0000u) | 0xfa00);
    rtw_w32(0x248, (rtw_r32(0x248) & 0xfffc0000u) | 0xfe00);
    rtw_w8(0x254, (uint8_t)((rtw_r8(0x254) & 0xfc) | 0x01));
    rtw_w8(0x254, (uint8_t)((rtw_r8(0x254) & 0xfb) | 0x04));
    rtw_w8(0x20c + 1, (uint8_t)((rtw_r8(0x20c + 1) & 0x7f) | 0x80));
    uint32_t wp = rtw_r32(0x10d4) & 0x3ffff, rp = rtw_r32(0x10d0) & 0x3ffff;
    uint32_t free = wp >= rp ? 1024 - (wp - rp) : rp - wp;
    if (free != 1024) {
        ksnprintf(err, errlen, "H2C queue mismatch (%lu bytes free)", free);
        return -1;
    }
    /* 8821C MAC: protocol, EDCA, beacon, WMAC (rtw8821c_mac_init) */
    rtw_w8(0x455, 0x70);
    rtw_set8(0x45e, 1u << 2);
    uint16_t pre = 0x1e4 | (1u << 11);
    rtw_w8(0x4e5, (uint8_t)pre);
    rtw_w8(0x4e5 + 1, (uint8_t)(pre >> 8));
    rtw_w32(0x4c8, 0xff | 0x08 << 8 | 0x20 << 16 | 0x20u << 24);
    rtw_w16(0x4cc + 2, 0x01 | 0x08 << 8);
    rtw_w8(0x1448, 0x06);
    rtw_w8(0x1448 + 2, 0x06);
    rtw_w8(0x144c, 0x06);
    rtw_w8(0x144c + 2, 0x06);
    rtw_set8(0x480, 1u << 5);
    rtw_clr8(0x5b4, 0x70);
    rtw_w16(0x522, 0);
    rtw_w8(0x51b, 0x09);
    rtw_w8(0x512, 0x19);
    rtw_w32(0x514, 0x0a | 0x0e << 8 | 0x10 << 16 | 0x10u << 24);
    rtw_w16(0x500 + 2, 0x186);
    rtw_w16(0x504 + 2, 0x3bc);
    rtw_w32(0x544, 0x05 | 0x1b << 16);
    rtw_w16(0x55e, 0x30 | 0x30 << 8);
    rtw_set8(REG_BCN_CTRL, 1u << 3);
    rtw_w32(0x540, 0x04 | 0x064 << 8);
    rtw_w8(0x558, 0x04);
    rtw_w8(0x559, 0x02);
    rtw_clr8(0x520 + 1, 1u << 4);
    rtw_w16(0x6a0, 0xffff);
    rtw_w16(0x6a2, 0x0fff);
    rtw_w16(0x6a4, 0xffff);
    rtw_w32(0x608, 0xe400220eu);
    rtw_w8(0x60c, 12288 >> 9);
    rtw_w8(0x604 + 2, 0x30);
    rtw_w8(0x604 + 1, 0x30);
    rtw_w8(0x639, 0x40);
    rtw_set8(0x66c, 1u << 1);
    rtw_set8(0x718, 1u << 6);
    rtw_w32(0x7d0 + 8, 0xb0810041u);
    rtw_w8(0x7d0 + 4, 0x98);
    /* RX driver info (PHY status), SDIO interface */
    rtw_w8(0x60f, 4);
    rtw_w8(0x114 + 1, (uint8_t)((rtw_r8(0x114 + 1) & 0xf0) | 0x0f));
    rtw_set32(0x608, 1u << 28);
    rtw_clr32(0x7d0 + 4, (1u << 8) | (1u << 9));
    (void)rtw_local_r32(SDIO_FREE_TXPG);
    rtw_local_w32(SDIO_TX_CTRL, rtw_local_r32(SDIO_TX_CTRL) & 0xfff8);
    return 0;
}

/* --- PHY --- */

static void phy_init(unsigned *writes)
{
    uint8_t v = rtw_r8(REG_SYS_FUNC_EN) | (1u << 6);
    rtw_w8(REG_SYS_FUNC_EN, v);
    v |= 3;
    rtw_w8(REG_SYS_FUNC_EN, v);
    v &= (uint8_t)~3;
    rtw_w8(REG_SYS_FUNC_EN, v);
    v |= 3;
    rtw_w8(REG_SYS_FUNC_EN, v);
    rtw_w8(REG_RF_CTRL, 7);
    timer_delay_us(10);
    rtw_w8(REG_WLRF1 + 3, 7);
    timer_delay_us(10);
    rtw_clr32(0x808, (1u << 28) | (1u << 29));

    drv_cut = rtw.cut ? rtw.cut : 15;
    drv_pkg = rtw.pkg ? rtw.pkg : 15;
    drv_rfe = rtw.rfe;
    *writes = load_table(rtw8821c_mac, rtw8821c_mac_len, CFG_MAC);
    *writes += load_table(rtw8821c_bb, rtw8821c_bb_len, CFG_BB);
    *writes += load_table(rtw8821c_agc, rtw8821c_agc_len, CFG_AGC);
    if (rtw.rfe == 2 || rtw.rfe == 4)
        *writes += load_table(rtw8821c_agc_btg_type2, rtw8821c_agc_btg_type2_len, CFG_AGC);
    *writes += load_table(rtw8821c_rf_a, rtw8821c_rf_a_len, CFG_RF);

    uint32_t cap = rtw.xtal & 0x3f;
    rtw_w32_mask(REG_AFE_XTAL_CTRL, 0x7e000000u, cap);
    rtw_w32_mask(REG_AFE_PLL_CTRL, 0x7eu, cap);
    rtw_clr32(0xa2c, (1u << 18) | (1u << 22));
    rtw_set32(0x808, (1u << 28) | (1u << 29));
    ch_param[0] = rtw_r32(0xa24);
    ch_param[1] = rtw_r32(0xa28);
    ch_param[2] = rtw_r32(0xaac);
}

/* WiFi only on the antenna (coex.c COEX_SET_ANT_WONLY, 8821C switch) */
static void wifi_only_antenna(void)
{
    /* GNT_BT software low, GNT_WL software high (LTE coex register 0x38) */
    if (rtw_poll32(0x1700, 1u << 29, 1u << 29)) {
        rtw_w32(0x1700, 0x800f0000u | 0x38);
        uint32_t v = rtw_r32(0x1708);
        v = (v & ~0xff00u) | 0x7700;
        if (rtw_poll32(0x1700, 1u << 29, 1u << 29)) {
            rtw_w32(0x1704, v);
            rtw_w32(0x1700, 0xc00f0000u | 0x38);
        }
    }
    rtw_set8(0x73, 1u << 2);                        /* path owner: WLAN */
    rtw_clr32(REG_LED_CFG, 1u << 23);
    rtw_set32(REG_LED_CFG, 1u << 24);
    rtw_w8(0xcb4, 0x77);
    rtw_w32_mask(0xcb4, 0xf0000000u, 2);
    rtw_set8(0x67, (1u << 5) | (1u << 4));
    rtw_w16(0xaa, 0x8000 | 0x2 | 0x1);              /* scoreboard: WLAN active, on */
    rtw_w32(0x6c0, 0x55555555u);                    /* coex table */
    rtw_w32(0x6c4, 0x55555555u);
}

static void switch_rf_set(int btg)
{
    rtw_set32(0x1080, 1u << 16);
    rtw_set32(0x000, 1u << 26);
    uint32_t reg = rtw_r32(0xcb8);
    if (btg) {
        reg |= 1u << 16;
        reg &= ~((1u << 18) | (1u << 20) | (1u << 22) | (1u << 21) | (1u << 23));
        rtw_w32_mask(0xa84, 0x00ff0000u, 0x0e);
        rtw_w32_mask(0xa80, 0x0000ffffu, 0xfc84);
    } else {
        reg |= (1u << 20) | (1u << 22) | (1u << 21);
        reg &= ~((1u << 16) | (1u << 18) | (1u << 23));
        rtw_w32_mask(0xa84, 0x00ff0000u, 0x12);
        rtw_w32_mask(0xa80, 0x0000ffffu, 0x7532);
    }
    rtw_w32(0xcb8, reg);
}

void rtw_set_channel(unsigned ch)
{
    /* BB */
    rtw_w32_mask(0x808, 1u << 28, 1);
    rtw_w32_mask(0x454, 1u << 7, 0);
    rtw_w32_mask(0xa80, 1u << 18, 0);
    rtw_w32_mask(0x814, 0x0000fc00u, 15);
    rtw_w32_mask(0xc1c, 0xf00u, 0);
    rtw_w32_mask(0x860, 0x1ffe0000u, 0x96a);
    if (ch == 14) {
        rtw_w32(0xa24, 0x0000b81cu);
        rtw_w32_mask(0xa28, 0xffffu, 0);
        rtw_w32(0xaac, 0x00003667u);
    } else {
        rtw_w32(0xa24, ch_param[0]);
        rtw_w32_mask(0xa28, 0xffffu, ch_param[1] & 0xffff);
        rtw_w32(0xaac, ch_param[2]);
    }
    uint32_t v = rtw_r32(0x8ac);
    rtw_w32(0x8ac, (v & 0xffcffc00u) | 0x10010000u);       /* 20 MHz */
    rtw_w32_mask(0x8c4, 1u << 30, 1);
    /* BB swing */
    static const uint32_t swing[4] = { 0x200, 0x16a, 0x101, 0x0b6 };
    unsigned s = rtw.swing_2g > 9 ? 0 : rtw.swing_2g;
    rtw_w32_mask(0xc1c, 0xffe00000u, swing[s / 3]);
    /* MAC (20 MHz) */
    rtw_w8(0x483, 0);
    rtw_w32_mask(0x668, (1u << 7) | (1u << 8), 0);
    rtw_w32_mask(0x024, (1u << 20) | (1u << 21), 0);
    rtw_w8(0x55c, 80);
    rtw_w8(0x638, 80);
    rtw_clr8(0x454, 1u << 7);
    /* RF */
    uint32_t rf18 = rtw_read_rf(0x18, RFREG_MASK);
    rf18 &= ~((1u << 16) | (1u << 9) | (1u << 8) | 0xffu | (1u << 18) | (1u << 17) |
              (1u << 11) | (1u << 10));
    rf18 |= (ch & 0xff) | (1u << 11) | (1u << 10);
    switch_rf_set(rfe_btg);
    rtw_write_rf(0xdf, 1u << 6, 1);
    rtw_write_rf(0x64, 0xf, 0xf);
    rtw_write_rf(0x18, RFREG_MASK, rf18);
    rtw_write_rf(0xb8, 1u << 19, 0);
    rtw_write_rf(0xb8, 1u << 19, 1);
    /* RX DFIR, 20 MHz */
    rtw_w32_mask(0x948, (1u << 29) | (1u << 28), 2);
    rtw_w32_mask(0x94c, (1u << 29) | (1u << 28), 2);
    rtw_w32_mask(0xc20, 1u << 31, 1);
    rtw_w32_mask(0x8f0, 1u << 31, 0);
    rtw_set_tx_power(ch);
}

static int sign4(unsigned v)
{
    return (v & 8) ? (int)v - 16 : (int)v;
}

void rtw_set_tx_power(unsigned ch)
{
    const uint8_t *e = rtw.efuse + 0x10;            /* path A, 2.4 GHz */
    unsigned group = ch <= 2 ? 0 : ch <= 5 ? 1 : ch <= 8 ? 2 : ch <= 11 ? 3 : 4;
    unsigned cck_group = ch == 14 ? 5 : group;
    int ofdm = sign4(e[11] & 0xf), bw20 = sign4(e[11] >> 4);
    uint8_t idx[20];
    for (unsigned r = 0; r < 20; r++) {
        int p = r <= 3 ? e[cck_group] : e[6 + group];
        if (r >= 4 && r <= 11)
            p += ofdm;
        if (r >= 12)
            p += bw20;
        if (p < 0) p = 0;
        if (p > 0x3f) p = 0x3f;
        idx[r] = (uint8_t)p;
    }
    for (unsigned r = 0; r < 20; r += 4)
        rtw_w32(0x1d00 + r, idx[r] | idx[r + 1] << 8 | idx[r + 2] << 16 | (uint32_t)idx[r + 3] << 24);
}

/* --- firmware commands --- */

int rtw_h2c_box(const uint8_t cmd[8])
{
    unsigned box = h2c_box;
    for (int i = 0; i < 30; i++) {
        if (!((rtw_r8(0x1cc) >> box) & 1))
            goto free;
        timer_delay_us(100);
    }
    return -1;
free:
    rtw_w32(0x1f0 + 4 * box, cmd[4] | cmd[5] << 8 | cmd[6] << 16 | (uint32_t)cmd[7] << 24);
    rtw_w32(0x1d0 + 4 * box, cmd[0] | cmd[1] << 8 | cmd[2] << 16 | (uint32_t)cmd[3] << 24);
    h2c_box = (h2c_box + 1) & 3;
    return 0;
}

static uint8_t h2c_buf[TX_DESC_SIZE + 32] __attribute__((aligned(64)));

int rtw_h2c_pkt(uint16_t sub_id, const uint8_t *payload, unsigned len)
{
    if (len > 24)
        return -1;
    uint8_t *p = h2c_buf + TX_DESC_SIZE;
    memset(p, 0, 32);
    p[0] = 0x01;                                    /* category */
    p[1] = 0xff;                                    /* id */
    p[2] = (uint8_t)sub_id;
    p[3] = (uint8_t)(sub_id >> 8);
    p[4] = (uint8_t)(8 + len);
    p[5] = 0;
    p[6] = (uint8_t)h2c_seq;
    p[7] = (uint8_t)(h2c_seq >> 8);
    h2c_seq++;
    memcpy(p + 8, payload, len);
    rtw_txinfo_t t = { 0 };
    t.pkt_size = 32;
    t.qsel = QSEL_H2C;
    rtw_fill_txdesc(h2c_buf, &t);
    return rtw_write_port(FIFO_HIGH, h2c_buf, TX_DESC_SIZE + 32);
}

void rtw_rx_all_bss(int all)
{
    uint32_t rcr = 0xf410400eu;                     /* FCS, MIC, ICV, PHY status, ..., AB, AM, APM */
    if (!all)
        rcr |= 1u << 7;                             /* only beacons of our BSS */
    rtw_w32(0x608, rcr);
}

int rtw_init_radio(char *err, unsigned errlen)
{
    rfe_btg = rtw.rfe == 2 || rtw.rfe == 4 || rtw.rfe == 7 || rtw.rfe == 10 ||
              rtw.rfe == 12 || rtw.rfe == 15;
    if (mac_init(err, errlen))
        return -1;
    unsigned writes;
    phy_init(&writes);
    kprintf("wifi: radio tables loaded (%u writes, RFE %u%s)\n", writes, rtw.rfe,
            rfe_btg ? ", antenna shared with Bluetooth" : "");
    /* HCI start: RX aggregation (as rtw88 on SDIO), interrupts masked (we poll) */
    rtw_set32(0x280, 1u << 29);
    rtw_set8(REG_TXDMA_PQ_MAP, 1u << 2);
    rtw_w16(0x280, 0xff | 0x01 << 8);
    rtw_set8(0x290, 1u << 1);
    rtw_local_w32(SDIO_HIMR, 0);
    /* general info: FW TX boundary 508 - 460; PHYDM info: RFE, 1T1R, cut */
    uint8_t gi[4] = { 0, 0, 48, 0 };
    rtw_h2c_pkt(0x0d, gi, 4);
    uint8_t pi[4] = { rtw.rfe, 4, (uint8_t)rtw.cut, 0x11 };
    rtw_h2c_pkt(0x11, pi, 4);
    wifi_only_antenna();
    /* security engine on, our address, RX filter */
    rtw_set32(REG_CR, 1u << 9);
    rtw_set8(0x680, 0xcf);
    for (int i = 0; i < 6; i++)
        rtw_w8(0x610 + i, rtw.mac[i]);
    rtw_rx_all_bss(1);
    rtw_set_igi(0x1c);                              /* for scanning: most coverage */
    rtw_set_channel(1);
    return 0;
}

/* --- the link (port 0, MACID 0) --- */

void rtw_set_igi(uint8_t igi)
{
    rtw_w32_mask(0xc50, 0x7f, igi);
}

void rtw_set_bssid(const uint8_t bssid[6])
{
    for (int i = 0; i < 6; i++)
        rtw_w8(0x618 + i, bssid[i]);
}

void rtw_set_link(unsigned net_type, unsigned aid)
{
    rtw_w32_mask(REG_CR, 0x30000u, net_type);
    rtw_w32_mask(0x6a8, 0x7ffu, aid);
}

/* CAM entry idx: 8 dwords through 0x674 / 0x670, word 0 (valid) last */
void rtw_cam_write(unsigned idx, unsigned keyid, unsigned type, int group, const uint8_t mac[6],
                   const uint8_t key[16])
{
    for (int i = 7; i >= 0; i--) {
        uint32_t v;
        if (i == 0)
            v = (keyid & 3) | (type & 7) << 2 | (uint32_t)(group ? 1 : 0) << 6 | 1u << 15 |
                (uint32_t)mac[0] << 16 | (uint32_t)mac[1] << 24;
        else if (i == 1)
            v = mac[2] | mac[3] << 8 | mac[4] << 16 | (uint32_t)mac[5] << 24;
        else if (i >= 6)
            v = 0;
        else {
            const uint8_t *k = key + (i - 2) * 4;
            v = k[0] | k[1] << 8 | k[2] << 16 | (uint32_t)k[3] << 24;
        }
        rtw_w32(0x674, v);
        rtw_w32(0x670, 1u << 31 | 1u << 16 | ((idx << 3) + (unsigned)i));
    }
}

void rtw_cam_clear(unsigned idx)
{
    rtw_w32(0x674, 0);
    rtw_w32(0x670, 1u << 31 | 1u << 16 | idx << 3);
}

int rtw_media_status(int connect)
{
    uint8_t c[8] = { 0x01, (uint8_t)(connect ? 1 : 0), 0 /* MACID */, 0, 0, 0, 0, 0 };
    return rtw_h2c_box(c);
}

int rtw_ra_info(unsigned rate_id, uint32_t mask)
{
    /* MACID 0, init level 1, 20 MHz, no SGI/LDPC/VHT, mask update, dis_pt */
    uint32_t w0 = 0x40 | (rate_id & 0x1f) << 16 | 1u << 21 | 1u << 30;
    uint8_t c[8] = { (uint8_t)w0, (uint8_t)(w0 >> 8), (uint8_t)(w0 >> 16), (uint8_t)(w0 >> 24),
                     (uint8_t)mask, (uint8_t)(mask >> 8), (uint8_t)(mask >> 16), (uint8_t)(mask >> 24) };
    return rtw_h2c_box(c);
}

int rtw_iqk(unsigned *ms, uint32_t *fail_mask)
{
    uint8_t p[1] = { 0 };                           /* not clear, not segmented */
    if (rtw_h2c_pkt(0x0e, p, 1) != 0)
        return -1;
    unsigned n = 0;
    int done = 0;
    for (; n < 300; n++) {
        if (rtw_read_rf(0x08, RFREG_MASK) == 0xabcde) {
            done = 1;
            break;
        }
        timer_delay_ms(20);
    }
    rtw_write_rf(0x08, RFREG_MASK, 0);
    *ms = n * 20;
    *fail_mask = rtw_r32(0x1bf0) & 0xff;
    return done ? 0 : -1;
}
#endif
