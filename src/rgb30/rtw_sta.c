/*
 * wifi/wifi.h on the RGB30: the RTL8821CS (rtw_*.c) behind the same calls
 * the Pi's Broadcom driver offers. Done: the module powered, the SDIO card
 * set up, the chip on, its firmware running, the MAC address read from the
 * efuse, the MAC and radio set up (rtw_init.c), and the scan: on each
 * 2.4 GHz channel a probe request, then the beacons and probe responses
 * heard (rtw_frame.c). Joining comes next (the station's 802.11 logic and
 * the WPA2 handshake in software: the firmware has no supplicant).
 */
#ifdef PLAT_RK3566
#include "wifi/wifi.h"
#include "rtw.h"
#include "rtw_phy.h"
#include "rtw_frame.h"
#include "rk_sdio.h"
#include "rk_wlbt.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/printf.h"
#include "drivers/timer.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int started;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kprintf("wifi: ");
    kvlog(fmt, ap);
    kprintf("\n");
    va_end(ap);
}

static void fail(const char *what)
{
    kprintf("\x1b[91mwifi: %s\x1b[0m\n", what);
}

int wifi_probe(void)
{
    say("powering the RTL8821CS (GPIO0_A0, 32 kHz from the RK817, WL_REG_ON GPIO4_A2)");
    wlbt_wifi_reset();
    if (sdio_init() != 0) {
        char m[120];
        ksnprintf(m, sizeof m, "SDIO (sdmmc2): %s", sdio_error());
        fail(m);
        return -1;
    }
    say("SDIO card ready (sdmmc2, 4-bit, function 1, 512-byte blocks)");
    return 0;
}

int wifi_start(void)
{
    if (started)
        return 0;
    if (wifi_probe() != 0)
        return -1;
    fat_entry_t e;
    uint8_t *fw = NULL;
    size_t len = 0;
    if (config_find_file("rtw8821c_fw.bin", &e) != 0 || fat_load(&e, &fw, &len) != 0) {
        fail("bm/rtw8821c_fw.bin not on the SD card (make TARGET=rgb30 firmware image)");
        return -1;
    }
    char err[120];
    int r = rtw_chip_start(fw, len, err, sizeof err);
    free(fw);
    if (r) {
        fail(err);
        if (rtw_io_errors())
            say("%d SDIO errors on the way", rtw_io_errors());
        return -1;
    }
    say("chip cut %u, firmware %u.%u.%u running, RFE %u, package %u", rtw.cut, rtw.fw_major,
        rtw.fw_minor, rtw.fw_patch, rtw.rfe, rtw.pkg);
    say("MAC address %02x:%02x:%02x:%02x:%02x:%02x", rtw.mac[0], rtw.mac[1], rtw.mac[2],
        rtw.mac[3], rtw.mac[4], rtw.mac[5]);
    if (rtw.rfe != 0 && rtw.rfe != 2 && rtw.rfe != 4 && rtw.rfe != 6)
        say("RFE type %u is not one rtw88 knows", rtw.rfe);
    if (rtw_init_radio(err, sizeof err) != 0) {
        fail(err);
        return -1;
    }
    if (rtw_io_errors())
        say("%d SDIO errors so far", rtw_io_errors());
    say("radio on (2.4 GHz, channel 1)");
    started = 1;
    return 0;
}

/* ---------------------------------------------------------------- RX, TX */

static uint8_t rxbuf[32768] __attribute__((aligned(64)));
static unsigned rx_bad, rx_errors, rx_big;

/* Reads what the chip has received (up to 8 FIFO reads) and hands each
 * packet to cb. Returns the packets. */
static int rx_poll(rtw_rx_cb cb, void *ctx)
{
    int total = 0;
    for (int k = 0; k < 8; k++) {
        uint32_t len = rtw_rx_len();
        if (len == 0 || len == 0xffffffffu)
            break;
        if (len > sizeof rxbuf) {
            rx_big++;
            rtw_rx_discard(len);
            continue;
        }
        if (rtw_read_port(rxbuf, len) != 0) {
            rx_errors++;
            break;
        }
        int n = rtw_rx_walk(rxbuf, len, rtw.rfe, cb, ctx);
        if (n < 0) {
            rx_bad++;
            n = -1 - n;
        }
        total += n;
    }
    return total;
}

static uint8_t txbuf[TX_DESC_SIZE + 2048] __attribute__((aligned(64)));

/* A management frame (no FCS) on the management queue, at 1 Mb/s with
 * the hardware's sequence numbers, as rtw88 sends them on 2.4 GHz. */
static int tx_mgmt(const uint8_t *frame, unsigned len)
{
    if (len > sizeof txbuf - TX_DESC_SIZE)
        return -1;
    unsigned pages = (TX_DESC_SIZE + len + 127) / 128;
    for (int i = 0; rtw_free_pages(FIFO_EXTRA) < (int)pages; i++) {
        if (i == 50)
            return -1;
        timer_delay_us(200);
    }
    memcpy(txbuf + TX_DESC_SIZE, frame, len);
    rtw_txinfo_t t = { 0 };
    t.pkt_size = len;
    t.offset = TX_DESC_SIZE;
    t.qsel = QSEL_MGMT;
    t.rate = 0;                                     /* 1M */
    t.rate_id = 8;                                  /* B_20M */
    t.use_rate = t.dis_rate_fb = 1;
    t.dis_qselseq = t.en_hwseq = 1;
    t.bmc = frame[4] & 1;
    rtw_fill_txdesc(txbuf, &t);
    return rtw_write_port(FIFO_EXTRA, txbuf, TX_DESC_SIZE + len);
}

/* ---------------------------------------------------------------- scan */

#define MAX_NETS 32
static wl_bss_t nets[MAX_NETS];
static int nnets;

typedef struct {
    unsigned ch, frames, crc, c2h, probe_resp_to_us;
} scan_t;

static void add_bss(const wl_bss_t *b)
{
    for (int i = 0; i < nnets; i++)
        if (memcmp(nets[i].bssid, b->bssid, 6) == 0) {
            int rssi = nets[i].rssi > b->rssi ? nets[i].rssi : b->rssi;
            if (!nets[i].ssid[0] && b->ssid[0])     /* a hidden network's probe response */
                nets[i] = *b;
            nets[i].rssi = rssi;
            return;
        }
    if (nnets < MAX_NETS)
        nets[nnets++] = *b;
}

static void on_scan_packet(const rtw_rxpkt_t *p, void *v)
{
    scan_t *s = v;
    if (p->c2h) {
        s->c2h++;
        return;
    }
    if (p->crc_err) {
        s->crc++;
        return;
    }
    s->frames++;
    if (p->len < 24)
        return;
    uint16_t kind = wl_kind(p->data);
    if (kind == WL_FC_PROBE_RESP && memcmp(p->data + 4, rtw.mac, 6) == 0)
        s->probe_resp_to_us++;
    wl_bss_t b;
    if (wl_parse_bss(p->data, p->len, p->rssi, &b) != 0)
        return;
    if (!b.channel)
        b.channel = (uint8_t)s->ch;
    add_bss(&b);
}

static int cmp_rssi(const void *a, const void *b)
{
    return ((const wl_bss_t *)b)->rssi - ((const wl_bss_t *)a)->rssi;
}

#define SCAN_CHANNELS   13
#define DWELL_US        120000u

int wifi_scan(void)
{
    if (!started) {
        kprintf("wifi: not started\n");
        return -1;
    }
    kprintf("wifi: scanning channels 1-%d...\n", SCAN_CHANNELS);
    nnets = 0;
    rx_bad = rx_errors = rx_big = 0;
    scan_t s;
    memset(&s, 0, sizeof s);
    unsigned per_ch[SCAN_CHANNELS + 1] = { 0 }, tx_fail = 0;
    uint8_t probe[128];
    unsigned plen = wl_probe_req(probe, rtw.mac, NULL);
    rtw_rx_all_bss(1);
    while (rx_poll(on_scan_packet, &s) > 0)        /* what came in before */
        ;
    memset(&s, 0, sizeof s);
    for (unsigned ch = 1; ch <= SCAN_CHANNELS; ch++) {
        rtw_set_channel(ch);
        s.ch = ch;
        unsigned before = s.frames;
        if (tx_mgmt(probe, plen) != 0)
            tx_fail++;
        uint32_t t0 = timer_ticks();
        while (timer_ticks() - t0 < DWELL_US)
            if (rx_poll(on_scan_packet, &s) == 0)
                timer_delay_us(500);
        per_ch[ch] = s.frames - before;
    }
    rtw_rx_all_bss(0);
    rtw_set_channel(1);

    char line[128];
    int n = ksnprintf(line, sizeof line, "wifi: frames per channel:");
    for (unsigned ch = 1; ch <= SCAN_CHANNELS && n < (int)sizeof line - 8; ch++)
        n += ksnprintf(line + n, sizeof line - (unsigned)n, " %u", per_ch[ch]);
    kprintf("%s\n", line);
    if (s.crc || s.c2h || rx_bad || rx_errors || rx_big || tx_fail)
        say("%u with bad CRC, %u from the firmware, %u bad buffers, %u read errors, %u probes not sent",
            s.crc, s.c2h, rx_bad, rx_errors + rx_big, tx_fail);
    if (s.probe_resp_to_us)
        say("%u probe responses to our MAC: transmitting works", s.probe_resp_to_us);
    else
        say("no probe response to our MAC (transmitting not proven)");

    qsort(nets, (size_t)nnets, sizeof *nets, cmp_rssi);
    kprintf("wifi: %d network%s\n", nnets, nnets == 1 ? "" : "s");
    for (int i = 0; i < nnets; i++)
        kprintf("  %2d  %4d dBm  ch %2u  %-4s  %s\n", i + 1, nets[i].rssi, nets[i].channel,
                nets[i].security, nets[i].ssid[0] ? nets[i].ssid : "(hidden)");
    return nnets;
}

int wifi_connect(void)
{
    say("joining a network is not in the RGB30 port yet");
    return -1;
}

int wifi_connect_saved(void)                { return -1; }
int wifi_linked(void)                       { return 0; }
const unsigned char *wifi_mac(void)         { return rtw.mac; }
void wifi_poll(void)                        { }
int wifi_recv(void *buf, int max)           { (void)buf; (void)max; return 0; }
int wifi_send(const void *eth, int len)     { (void)eth; (void)len; return -1; }
#endif
