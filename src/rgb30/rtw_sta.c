/*
 * wifi/wifi.h on the RGB30: the RTL8821CS (rtw_*.c) behind the same calls
 * the Pi's Broadcom driver offers. Done: the module powered, the SDIO card
 * set up, the chip on, its firmware running, the MAC address read from the
 * efuse, the MAC and radio set up (rtw_init.c); the scan (on each 2.4 GHz
 * channel a probe request, then the beacons and probe responses heard);
 * joining: open system authentication, association, the WPA2-PSK
 * handshake in software (wpa.c: the firmware has no supplicant), the keys
 * in the chip's CAM (it does CCMP); then data frames <-> Ethernet frames
 * for lwIP (rtw_frame.c), the link watched through the AP's beacons.
 */
#ifdef PLAT_RK3566
#include "wifi/wifi.h"
#include "rtw.h"
#include "rtw_phy.h"
#include "rtw_frame.h"
#include "wpa.h"
#include "rk_sdio.h"
#include "rk_wlbt.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "kernel/fiber.h"
#include "lib/printf.h"
#include "drivers/timer.h"
#include "drivers/rng.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int started;
/* a scan or a join under way: it may be paused in a fiber (wifi_auto.c,
 * the menu goes on meanwhile), and wifi_poll must not cut in */
static int busy;

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

/* One frame (no FCS) after its descriptor in txbuf, on fifo once the
 * queue has room. */
static int tx_send(uint32_t fifo, unsigned len, rtw_txinfo_t *t)
{
    unsigned pages = (TX_DESC_SIZE + len + 127) / 128;
    for (int i = 0; rtw_free_pages(fifo) < (int)pages; i++) {
        if (i == 100)
            return -1;
        timer_delay_us(200);
    }
    t->pkt_size = len;
    t->offset = TX_DESC_SIZE;
    rtw_fill_txdesc(txbuf, t);
    return rtw_write_port(fifo, txbuf, TX_DESC_SIZE + len);
}

/* A management frame on the management queue, at 1 Mb/s with the
 * hardware's sequence numbers, as rtw88 sends them on 2.4 GHz. */
static int tx_mgmt(const uint8_t *frame, unsigned len)
{
    if (len > sizeof txbuf - TX_DESC_SIZE)
        return -1;
    memcpy(txbuf + TX_DESC_SIZE, frame, len);
    rtw_txinfo_t t = { 0 };
    t.qsel = QSEL_MGMT;
    t.rate = 0;                                     /* 1M */
    t.rate_id = 8;                                  /* B_20M */
    t.use_rate = t.dis_rate_fb = 1;
    t.dis_qselseq = t.en_hwseq = 1;
    t.bmc = frame[4] & 1;
    return tx_send(FIFO_EXTRA, len, &t);
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

static void leave(const char *why);

/* every channel; verbose: the frames per channel and the TX check */
static void scan(int verbose)
{
    nnets = 0;
    rx_bad = rx_errors = rx_big = 0;
    scan_t s;
    memset(&s, 0, sizeof s);
    unsigned per_ch[SCAN_CHANNELS + 1] = { 0 }, tx_fail = 0;
    uint8_t probe[128];
    unsigned plen = wl_probe_req(probe, rtw.mac, NULL);
    rtw_rx_all_bss(1);
    rtw_set_igi(0x1c);
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
        while (timer_ticks() - t0 < DWELL_US && !fiber_cancelled())
            if (rx_poll(on_scan_packet, &s) == 0) {
                timer_delay_us(500);
                fiber_slice();                      /* in a fiber: the menu goes on */
            }
        per_ch[ch] = s.frames - before;
    }
    rtw_rx_all_bss(0);
    rtw_set_channel(1);
    qsort(nets, (size_t)nnets, sizeof *nets, cmp_rssi);
    if (!verbose)
        return;

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
}

int wifi_scan(void)
{
    if (!started) {
        kprintf("wifi: not started\n");
        return -1;
    }
    leave("scanning");
    kprintf("wifi: scanning channels 1-%d...\n", SCAN_CHANNELS);
    busy++;
    scan(1);
    busy--;
    kprintf("wifi: %d network%s\n", nnets, nnets == 1 ? "" : "s");
    for (int i = 0; i < nnets; i++)
        kprintf("  %2d  %4d dBm  ch %2u  %-4s  %s\n", i + 1, nets[i].rssi, nets[i].channel,
                nets[i].security, nets[i].ssid[0] ? nets[i].ssid : "(hidden)");
    return nnets;
}

/* ---------------------------------------------------------------- the link */

#define RXQ_SLOTS   16
#define RXQ_SIZE    1600
#define CAM_PAIRWISE 4
#define BEACON_LOSS_US 8000000u

static uint8_t rxq[RXQ_SLOTS][RXQ_SIZE];
static uint16_t rxq_len[RXQ_SLOTS];
static unsigned rxq_head, rxq_tail;

static struct {
    int associated, linked, protect;
    wl_bss_t bss;
    uint8_t rsn[22];
    unsigned rate_id, max_rate;
    uint64_t pn;                    /* our next CCMP packet number */
    uint16_t seq;                   /* our next data sequence number */
    uint32_t last_beacon, last_igi;
    uint32_t last_poll;             /* wifi_poll's last call: beacons count only while we listen */
    int rssi;
    wpa_t wpa;
    int eapol_msgs, eapol_err;
    /* while joining */
    int got_auth, got_assoc, deauth;
    uint16_t status, aid, reason;
    unsigned dropped;               /* protected frames the chip did not decrypt */
} lk;

static void queue_eth(const uint8_t *eth, int n)
{
    if (rxq_head - rxq_tail >= RXQ_SLOTS)
        return;                                     /* full: lwIP is slow, dropped */
    unsigned k = rxq_head % RXQ_SLOTS;
    memcpy(rxq[k], eth, (size_t)n);
    rxq_len[k] = (uint16_t)n;
    rxq_head++;
}

/* An Ethernet frame to the AP: CCMP once the keys are in; EAPOL at a
 * fixed 1 Mb/s, the rest at the rate the firmware picks. */
static int tx_data(const uint8_t *eth, unsigned len, int fixed_rate)
{
    if (len < 14 || len > RXQ_SIZE)
        return -1;
    unsigned n = wl_from_eth(txbuf + TX_DESC_SIZE, eth, len, lk.bss.bssid, lk.seq, lk.protect,
                             lk.pn, 0);
    rtw_txinfo_t t = { 0 };
    t.qsel = 0;                                     /* TID 0: best effort */
    t.macid = 0;
    t.rate_id = (uint8_t)lk.rate_id;
    t.rate = fixed_rate ? 0 : (uint8_t)lk.max_rate;
    t.use_rate = t.dis_rate_fb = fixed_rate ? 1 : 0;
    t.seq = lk.seq;
    t.sec_type = lk.protect ? 3 : 0;
    lk.seq = (lk.seq + 1) & 0xfff;
    if (lk.protect)
        lk.pn++;
    return tx_send(FIFO_LOW, n, &t);
}

static void handle_eapol(const uint8_t *e, unsigned len)
{
    static uint8_t out[14 + 160];
    unsigned olen;
    int r = wpa_rx(&lk.wpa, e, len, out + 14, &olen);
    if (r < 0) {
        lk.eapol_err = r;
        return;
    }
    lk.eapol_msgs++;
    if (r & WPA_SEND) {
        memcpy(out, lk.bss.bssid, 6);
        memcpy(out + 6, rtw.mac, 6);
        out[12] = 0x88;
        out[13] = 0x8e;
        tx_data(out, 14 + olen, 1);
    }
    if (r & WPA_SET_PTK) {
        rtw_cam_write(CAM_PAIRWISE, 0, RTW_CAM_AES, 0, lk.bss.bssid, lk.wpa.ptk + 32);
        lk.protect = 1;
        lk.pn = 1;
    }
    if (r & WPA_SET_GTK) {
        static const uint8_t all[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        unsigned type = lk.bss.rsn_group == 2 ? RTW_CAM_TKIP : RTW_CAM_AES;
        rtw_cam_write(lk.wpa.gtk_id, lk.wpa.gtk_id, type, 1, all, lk.wpa.gtk);
    }
    if (r & WPA_SET_PTK)
        lk.linked = 1;
}

static void on_link_packet(const rtw_rxpkt_t *p, void *ctx)
{
    (void)ctx;
    if (p->c2h || p->crc_err || p->icv_err || p->len < 24)
        return;
    const uint8_t *f = p->data;
    const uint8_t *bssid = lk.bss.bssid;
    if ((f[0] & 0x0c) == 0x00) {                    /* management */
        uint16_t kind = wl_kind(f), st, aid, reason;
        if (kind == WL_FC_BEACON) {
            if (!memcmp(f + 16, bssid, 6)) {
                lk.last_beacon = timer_ticks();
                if (p->rssi > -127)
                    lk.rssi = (lk.rssi * 3 + p->rssi) / 4;
            }
        } else if (wl_auth_resp(f, p->len, bssid, rtw.mac, &st) == 0) {
            lk.got_auth = 1;
            lk.status = st;
        } else if (wl_assoc_resp(f, p->len, bssid, rtw.mac, &st, &aid) == 0) {
            lk.got_assoc = 1;
            lk.status = st;
            lk.aid = aid;
        } else if (wl_deauth(f, p->len, bssid, rtw.mac, &reason) == 0) {
            lk.deauth = 1;
            lk.reason = reason;
        }
        return;
    }
    /* data: from the association response on (message 1 of the WPA2
     * handshake may come in the same read) */
    if ((f[0] & 0x0c) != 0x08 || !(lk.associated || (lk.got_assoc && !lk.status)))
        return;
    unsigned head = 0, tail = 0;
    if (f[1] & 0x40) {                              /* protected */
        if (!p->decrypted) {
            lk.dropped++;
            return;
        }
        head = 8;
        tail = p->enc == 4 ? 8 : 12;                /* CCMP MIC; TKIP Michael + ICV */
    }
    static uint8_t eth[RXQ_SIZE];
    int n = wl_to_eth(f, p->len, bssid, rtw.mac, head, tail, eth, sizeof eth);
    if (n <= 14)
        return;
    if (eth[12] == 0x88 && eth[13] == 0x8e)
        handle_eapol(eth + 14, (unsigned)n - 14);
    else if (lk.linked)
        queue_eth(eth, n);
}

/* drops the link: keys out, the firmware told, nothing more received */
static void leave(const char *why)
{
    if (!lk.associated)
        return;
    if (why) {
        uint8_t f[26];
        memset(f, 0, sizeof f);                     /* deauthentication, reason 3: leaving */
        f[0] = WL_FC_DEAUTH;
        memcpy(f + 4, lk.bss.bssid, 6);
        memcpy(f + 10, rtw.mac, 6);
        memcpy(f + 16, lk.bss.bssid, 6);
        f[24] = 3;
        tx_mgmt(f, sizeof f);
        say("left \"%s\" (%s)", lk.bss.ssid, why);
    }
    rtw_media_status(0);
    rtw_set_link(RTW_NET_NO_LINK, 0);
    for (unsigned i = 0; i <= CAM_PAIRWISE; i++)
        rtw_cam_clear(i);
    lk.associated = lk.linked = lk.protect = 0;
    rxq_head = rxq_tail = 0;
}

/* waits up to ms for *flag (or a deauthentication), reading what comes */
static int wait_for(int *flag, uint32_t ms)
{
    uint32_t t0 = timer_ticks();
    while (!*flag && !lk.deauth && timer_ticks() - t0 < ms * 1000u && !fiber_cancelled())
        if (rx_poll(on_link_packet, 0) == 0) {
            timer_delay_us(300);
            fiber_slice();                          /* in a fiber: the menu goes on */
        }
    return *flag;
}

static const char *status_text(unsigned st)
{
    switch (st) {
    case 1: return "unspecified failure";
    case 10: return "capabilities not supported";
    case 12: return "refused";
    case 17: return "the network is full";
    case 18: return "rates not supported";
    case 27: return "802.11n (HT) required";
    case 40: return "invalid information element";
    case 41: return "invalid group cipher";
    case 42: return "invalid pairwise cipher";
    case 43: return "invalid key management";
    case 53: return "the network asks for management frame protection";
    default: return "";
    }
}

static const char *reason_text(unsigned r)
{
    switch (r) {
    case 2: return "authentication no longer valid";
    case 3: return "the network is leaving";
    case 4: return "inactivity";
    case 6: case 7: return "not authenticated or associated";
    case 14: return "MIC failure";
    case 15: return "4-way handshake timeout: wrong password?";
    case 23: return "802.1X authentication failed: wrong password?";
    default: return "";
    }
}

static int join_failed(const char *fmt, unsigned v, const char *text)
{
    char m[120];
    int n = ksnprintf(m, sizeof m, fmt, v);
    if (text && text[0])
        ksnprintf(m + n, sizeof m - (unsigned)n, " (%s)", text);
    fail(m);
    leave(NULL);
    rtw_set_bssid((const uint8_t *)"\0\0\0\0\0\0");
    return -1;
}

static wl_bss_t *find_net(const char *ssid)
{
    for (int i = 0; i < nnets; i++)                 /* sorted: the strongest first */
        if (!strcmp(nets[i].ssid, ssid))
            return &nets[i];
    return NULL;
}

static int join_(const wl_bss_t *b, const char *psk);

/* Joins b: open, or WPA2-PSK with psk (8-63 characters). */
static int join(const wl_bss_t *b, const char *psk)
{
    busy++;
    int r = join_(b, psk);
    busy--;
    return r;
}

static int join_(const wl_bss_t *b, const char *psk)
{
    const char *why = wl_unsupported(b);
    if (why) {
        fail(why);
        return -1;
    }
    int wpa2 = b->rsn;
    unsigned plen = psk ? (unsigned)strlen(psk) : 0;
    if (wpa2 && (plen < 8 || plen > 63)) {
        fail("the password must be 8 to 63 characters");
        return -1;
    }
    leave("joining another network");
    memset(&lk, 0, sizeof lk);
    lk.bss = *b;
    kprintf("wifi: joining \"%s\" (%s, channel %u, %d dBm)...\n", b->ssid, b->security,
            b->channel, b->rssi);
    if (wpa2) {
        uint32_t t0 = timer_ticks();
        wpa_pmk(psk, b->ssid_raw, b->ssid_len, lk.wpa.pmk);
        say("PMK computed (%lu ms)", (timer_ticks() - t0) / 1000);
    }

    rtw_set_channel(b->channel);
    rtw_set_bssid(b->bssid);
    rtw_rx_all_bss(0);
    lk.rssi = b->rssi;
    int igi = b->rssi + 100 - 6;
    rtw_set_igi((uint8_t)(igi < 0x1c ? 0x1c : igi > 0x40 ? 0x40 : igi));
    unsigned ms;
    uint32_t mask;
    if (rtw_iqk(&ms, &mask) == 0)
        say("IQ calibration done (%u ms, fail mask %02lx)", ms, mask);
    else
        say("IQ calibration did not finish (%u ms)", ms);
    while (rx_poll(on_link_packet, 0) > 0)
        ;

    /* open system authentication */
    uint8_t f[256];
    unsigned n = wl_auth_req(f, b->bssid, rtw.mac);
    for (int i = 0; i < 3 && !lk.got_auth && !lk.deauth; i++) {
        if (tx_mgmt(f, n) != 0)
            say("authentication request not sent (TX queue full)");
        wait_for(&lk.got_auth, 300);
    }
    if (!lk.got_auth)
        return join_failed("no answer to authentication (3 tries)", 0, NULL);
    if (lk.status)
        return join_failed("authentication refused, status %u", lk.status, status_text(lk.status));
    say("authenticated");

    /* association; WPA2 ready before it, since the AP's message 1 follows
     * the association response at once */
    unsigned rsn_len = wpa2 ? wl_rsn_ie(lk.rsn, b->rsn_group) : 0;
    memcpy(lk.wpa.aa, b->bssid, 6);
    memcpy(lk.wpa.spa, rtw.mac, 6);
    rng_read(lk.wpa.snonce, 32);
    lk.wpa.ie = lk.rsn;
    lk.wpa.ie_len = rsn_len;
    uint32_t rates = wl_rate_mask(b);
    lk.rate_id = (rates & 0xff0) ? ((rates & 0xf) ? 6 : 7) : 8;       /* BG, G, B */
    lk.max_rate = (rates & 0xff0) ? 0x0b : 0x03;                       /* 54M, 11M */
    n = wl_assoc_req(f, b, rtw.mac, lk.rsn, rsn_len);
    for (int i = 0; i < 3 && !lk.got_assoc && !lk.deauth; i++) {
        tx_mgmt(f, n);
        wait_for(&lk.got_assoc, 500);
    }
    if (lk.deauth)
        return join_failed("the network sent us away, reason %u", lk.reason, reason_text(lk.reason));
    if (!lk.got_assoc)
        return join_failed("no answer to association (3 tries)", 0, NULL);
    if (lk.status)
        return join_failed("association refused, status %u", lk.status, status_text(lk.status));
    lk.associated = 1;
    lk.last_beacon = lk.last_igi = lk.last_poll = timer_ticks();
    rtw_set_link(RTW_NET_LINKED, lk.aid);
    rtw_ra_info(lk.rate_id, rates);
    rtw_media_status(1);
    say("associated, AID %u", lk.aid);
    if (!wpa2) {
        lk.linked = 1;
        return 0;
    }

    /* WPA2: the AP runs the 4-way handshake (it may be over already) */
    wait_for(&lk.linked, 6000);
    if (lk.linked) {
        say("WPA2 keys in (pairwise CCMP, group %s, key %u)",
            b->rsn_group == 2 ? "TKIP" : "CCMP", lk.wpa.gtk_id);
        return 0;
    }
    if (lk.deauth)
        return join_failed("the network sent us away, reason %u", lk.reason, reason_text(lk.reason));
    if (lk.eapol_err == WPA_ERR_MIC || lk.eapol_msgs > 1)
        return join_failed("password not accepted (%u handshake messages)", (unsigned)lk.eapol_msgs, NULL);
    if (!lk.eapol_msgs)
        return join_failed("the network did not start the WPA2 handshake", 0, NULL);
    return join_failed("WPA2 handshake not finished (error %u)", (unsigned)-lk.eapol_err, NULL);
}

int wifi_connect(void)
{
    if (!started)
        return -1;
    const char *ssid = config_get("wifi_ssid"), *psk = config_get("wifi_psk");
    if (!ssid || !ssid[0]) {
        fail("no network saved: write wifi_ssid=... and wifi_psk=... in bm/config.txt");
        return -1;
    }
    wl_bss_t *b = find_net(ssid);
    if (!b) {
        if (nnets == 0) {
            kprintf("wifi: looking for \"%s\"...\n", ssid);
            leave("scanning");
            busy++;
            scan(0);
            busy--;
            b = find_net(ssid);
        }
        if (!b) {
            char m[80];
            ksnprintf(m, sizeof m, "\"%s\" is not in range", ssid);
            fail(m);
            return -1;
        }
    }
    return join(b, psk);
}

int wifi_connect_saved(void)
{
    const char *ssid = config_get("wifi_ssid");
    if (!started || !ssid || !ssid[0])
        return -1;
    leave("scanning");
    busy++;
    scan(0);
    busy--;
    if (fiber_cancelled())                          /* wifi_auto_stop: no join started */
        return -1;
    return wifi_connect();
}

int wifi_linked(void)
{
    return started && lk.linked;
}

int wifi_up(void)
{
    return started;
}

/* before a restart: a deauthentication to the AP, so that it does not hold
 * the old association when the console comes back */
void wifi_leave(void)
{
    if (started && !busy)
        leave("restarting");
}

const unsigned char *wifi_mac(void)
{
    return rtw.mac;
}

void wifi_poll(void)
{
    if (!started || !lk.associated || busy)
        return;
    /* a long job that did not poll (an SD write, a cartridge loading): the
     * beacons missed meanwhile are ours, not the network's */
    uint32_t now = timer_ticks();
    if (now - lk.last_poll > 1000000u)
        lk.last_beacon = now;
    lk.last_poll = now;
    rx_poll(on_link_packet, 0);
    now = timer_ticks();
    if (lk.deauth) {
        char m[100];
        ksnprintf(m, sizeof m, "link lost: the network sent us away (reason %u%s%s)", lk.reason,
                  reason_text(lk.reason)[0] ? ", " : "", reason_text(lk.reason));
        fail(m);
        leave(NULL);
        return;
    }
    if (now - lk.last_beacon > BEACON_LOSS_US) {
        fail("link lost: no beacon from the network for 8 s");
        leave(NULL);
        return;
    }
    if (now - lk.last_igi > 2000000u) {             /* initial gain from the signal */
        lk.last_igi = now;
        int igi = lk.rssi + 100 - 6;
        rtw_set_igi((uint8_t)(igi < 0x1c ? 0x1c : igi > 0x40 ? 0x40 : igi));
    }
}

int wifi_recv(void *buf, int max)
{
    if (rxq_tail == rxq_head)
        return 0;
    unsigned k = rxq_tail++ % RXQ_SLOTS;
    int n = rxq_len[k] < max ? rxq_len[k] : max;
    memcpy(buf, rxq[k], (size_t)n);
    return n;
}

int wifi_send(const void *eth, int len)
{
    if (!wifi_linked() || len < 14)
        return -1;
    return tx_data(eth, (unsigned)len, 0);
}
#endif
