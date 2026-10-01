/*
 * The RGB30's WiFi station on the PC: rtw_sta.c, rtw_init.c, rtw_io.c,
 * rtw_frame.c, wpa.c and lwIP (src/net/net.c), unchanged, on a simulated
 * RTL8821C reached through SDIO (MAC registers, TX FIFOs and descriptors,
 * the RX FIFO with aggregated packets, RF registers, security CAM,
 * firmware mailbox and H2C packets) and two simulated access points:
 * "CasaRossi" (WPA2-PSK, channel 6, a router with DHCP behind it) and
 * "Ospiti" (open, channel 11). The AP's side of WPA2 is computed here.
 *
 * Checked: the scan; a wrong password (deauthentication, reason 15); the
 * 4-way handshake, the keys in the CAM, the firmware told; DHCP, ARP and a
 * ping over "CCMP" (descriptor SEC_TYPE, CCMP header, packet numbers); a
 * group rekey; a deauthentication; an open network; beacon loss.
 *
 * build/rgb30-host/wifi_sim_test [-v] (-v: the console output too)
 */
#include "rtw.h"
#include "rtw_frame.h"
#include "rk_sdio.h"
#include "wpa.h"
#include "wifi/wifi.h"
#include "net/net.h"
#include "kernel/config.h"
#include "fs/fat.h"
#include "drivers/timer.h"
#include "drivers/rng.h"
#include "lib/printf.h"

#include "mbedtls/aes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, verbose;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const uint8_t sta_mac[6] = { 0x00, 0xe0, 0x4c, 0x88, 0x21, 0xc0 };
static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* ---------------------------------------------------------------- console, clock */

static char logbuf[1 << 18];
static size_t loglen;

void uart_putc(char c)
{
    if (loglen + 1 < sizeof logbuf) {
        logbuf[loglen++] = c;
        logbuf[loglen] = 0;
    }
    if (verbose)
        putchar(c);
}

static int logged(const char *s, size_t from) { return strstr(logbuf + from, s) != NULL; }

static uint64_t now_us;
static void sim_tick(void);

uint32_t timer_ticks(void) { return (uint32_t)now_us; }
void timer_delay_us(uint32_t us)
{
    now_us += us;
    sim_tick();
}
void timer_delay_ms(uint32_t ms) { timer_delay_us(ms * 1000u); }

/* ---------------------------------------------------------------- what the driver needs */

rtw_chip_t rtw;
static int chip_starts;

int rtw_chip_start(const uint8_t *fw, size_t len, char *err, unsigned errlen)
{
    (void)fw; (void)len; (void)err; (void)errlen;
    chip_starts++;
    rtw.powered = rtw.fw_running = 1;
    rtw.cut = 2;
    rtw.rfe = 0;
    rtw.fw_major = 24;
    rtw.fw_minor = 11;
    memcpy(rtw.mac, sta_mac, 6);
    memset(rtw.efuse + 0x10, 0x2d, 11);             /* TX power indexes */
    rtw.efuse[0x10 + 11] = 0;
    rtw_set_poweron(1);
    return 0;
}

void wlbt_wifi_reset(void) {}
int sdio_init(void) { return 0; }
int sdio_ready(void) { return 1; }
const char *sdio_error(void) { return ""; }

int rng_read(void *buf, size_t len)
{
    static uint8_t x = 0x5a;
    for (size_t i = 0; i < len; i++)
        ((uint8_t *)buf)[i] = x += 37;
    return 0;
}
void rng_start(void) {}

static struct { char key[32], val[80]; } cfg[8];
const char *config_get(const char *key)
{
    for (int i = 0; i < 8; i++)
        if (!strcmp(cfg[i].key, key))
            return cfg[i].val;
    return NULL;
}
void config_set(const char *key, const char *value)
{
    for (int i = 0; i < 8; i++)
        if (!cfg[i].key[0] || !strcmp(cfg[i].key, key)) {
            snprintf(cfg[i].key, sizeof cfg[i].key, "%s", key);
            snprintf(cfg[i].val, sizeof cfg[i].val, "%s", value);
            return;
        }
}
void config_save(void) {}
int config_find_file(const char *name, fat_entry_t *e)
{
    (void)name;
    memset(e, 0, sizeof *e);
    return 0;
}
int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    (void)e;
    *data = malloc(16);
    *len = 16;
    return 0;
}

/* net.c's other partners */
int netcon_start(void) { return -1; }
void netcon_poll(void) {}
const char *netcon_password(void) { return ""; }
int netxfer_start(void) { return -1; }
void netxfer_poll(void) {}
const unsigned char *eth_mac(void) { return sta_mac; }
int eth_linked(void) { return 0; }
void eth_poll(void) {}
int eth_recv(void *buf, int max) { (void)buf; (void)max; return 0; }
int eth_send(const void *frame, int len) { (void)frame; (void)len; return -1; }

/* ---------------------------------------------------------------- the chip */

static uint8_t reg[0x10000];                        /* MAC registers */
static uint8_t local_reg[0x1000];                   /* SDIO local registers */
static uint32_t rf[256];
static uint32_t cam[32][8];
static struct { uint32_t w0, w1; } h2c[256];
static int nh2c, iqks, h2c_pkts;

static uint32_t rd32(const uint8_t *r, uint32_t a)
{
    return r[a] | r[a + 1] << 8 | r[a + 2] << 16 | (uint32_t)r[a + 3] << 24;
}

static uint8_t mac_read8(uint32_t a)
{
    a &= 0xffff;
    uint32_t base = a & ~3u, v;
    if (base == 0x1700)
        v = rd32(reg, base) | 1u << 29;             /* LTE coex: ready */
    else if (base == 0x1cc)
        v = 0;                                      /* mailboxes free */
    else if (base >= 0x2800 && base < 0x2c00)
        v = rf[(base - 0x2800) / 4];
    else
        return reg[a];
    return (uint8_t)(v >> (8 * (a & 3)));
}

static void mac_hook(uint32_t base)
{
    uint32_t v = rd32(reg, base);
    if (base == 0x208)
        reg[0x208] &= (uint8_t)~1;                  /* LLT done */
    if (base == 0xc90)
        rf[(v >> 20) & 0xff] = v & 0xfffff;
    if (base == 0x670 && (v & (1u << 16)))
        cam[(v & 0xff) >> 3][v & 7] = rd32(reg, 0x674);
    if (base >= 0x1d0 && base <= 0x1dc && nh2c < 256) {
        h2c[nh2c].w0 = v;
        h2c[nh2c].w1 = rd32(reg, 0x1f0 + (base - 0x1d0));
        nh2c++;
    }
}

static void mac_write8(uint32_t a, uint8_t v)
{
    a &= 0xffff;
    reg[a] = v;
    mac_hook(a & ~3u);
}

/* --- RX: packets waiting, handed over aggregated (as RX DMA does) --- */

typedef struct {
    uint8_t data[1700];
    unsigned len;
    int rssi;
    uint8_t enc;
} rxpkt_t;

#define RXQ 64
static rxpkt_t rxq[RXQ];
static unsigned rxq_n, rx_dropped;
static uint8_t agg[32768];
static uint32_t agg_len;

static unsigned sta_channel(void) { return rf[0x18] & 0xff; }

static void rx_push(const uint8_t *f, unsigned len, int rssi, uint8_t enc)
{
    if (rxq_n == RXQ) {
        rx_dropped++;
        return;
    }
    rxpkt_t *p = &rxq[rxq_n++];
    memcpy(p->data, f, len);
    p->len = len;
    p->rssi = rssi;
    p->enc = enc;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static void build_agg(void)
{
    if (agg_len || !rxq_n)
        return;
    unsigned take = 0;
    while (take < rxq_n && take < 6) {
        rxpkt_t *p = &rxq[take];
        uint8_t *d = agg + agg_len;
        if (agg_len + 24 + 32 + p->len + 4 + 8 > sizeof agg)
            break;
        memset(d, 0, 24 + 32);
        put32(d, (p->len + 4) | 4u << 16 | (uint32_t)p->enc << 20 | 1u << 26);
        put32(d + 12, 4);                           /* rate 6M */
        d[24] = 1;                                  /* PHY status page 1 */
        d[25] = (uint8_t)(p->rssi + 110);
        memcpy(d + 56, p->data, p->len);
        put32(d + 56 + p->len, 0xdeadbeef);         /* FCS */
        agg_len += (56 + p->len + 4 + 7) & ~7u;
        take++;
    }
    memmove(rxq, rxq + take, (rxq_n - take) * sizeof *rxq);
    rxq_n -= take;
}

static uint32_t local_read32(uint32_t off)
{
    if (off == 0x1c) {
        build_agg();
        return agg_len;
    }
    if (off == 0x20 || off == 0x24 || off == 0x28)
        return 0x00400040;                          /* free pages, public 0x40 */
    return rd32(local_reg, off);
}

/* --- TX: what the driver sends --- */

typedef struct {
    uint32_t fifo;
    uint8_t qsel, rate, use_rate, sec_type, en_hwseq, rate_id;
    uint16_t seq;
} txinfo_t;

static unsigned tx_mgmt_frames, tx_data_frames, bad_desc;
static void air(const uint8_t *f, unsigned len, const txinfo_t *t);

static void tx_fifo(uint32_t fifo, const uint8_t *d, uint32_t len)
{
    uint16_t x = 0;
    for (int i = 0; i < 16; i++)
        x ^= (uint16_t)(d[2 * i] | d[2 * i + 1] << 8);
    uint32_t w0 = rd32(d, 0), w1 = rd32(d, 4), w3 = rd32(d, 12), w4 = rd32(d, 16),
             w8 = rd32(d, 32), w9 = rd32(d, 36);
    unsigned size = w0 & 0xffff, offset = (w0 >> 16) & 0xff;
    txinfo_t t = { fifo, (uint8_t)((w1 >> 8) & 0x1f), (uint8_t)(w4 & 0x7f), (uint8_t)((w3 >> 8) & 1),
                   (uint8_t)((w1 >> 22) & 3), (uint8_t)((w8 >> 15) & 1), (uint8_t)((w1 >> 16) & 0x1f),
                   (uint16_t)((w9 >> 12) & 0xfff) };
    if (x != 0 || !(w0 & (1u << 26))) {
        bad_desc++;
        return;
    }
    if (t.qsel == 0x13) {                           /* H2C packet, no offset */
        CHECK(fifo == 0x8000 && offset == 0);
        h2c_pkts++;
        uint16_t sub = (uint16_t)(d[48 + 2] | d[48 + 3] << 8);
        if (sub == 0x0e) {
            iqks++;
            rf[0x08] = 0xabcde;                     /* IQK done at once */
        }
        return;
    }
    if (offset != 48 || 48 + size > len) {
        bad_desc++;
        return;
    }
    if (t.qsel == 0x12) {
        CHECK(fifo == 0xe000 && t.use_rate && t.en_hwseq);
        tx_mgmt_frames++;
    } else if (t.qsel == 0) {
        CHECK(fifo == 0xc000 && !t.en_hwseq);
        tx_data_frames++;
    } else {
        bad_desc++;
        return;
    }
    air(d + 48, size, &t);
}

/* --- the SDIO bus --- */

static uint8_t bus_read8(uint32_t a)
{
    if (a >= 0x10000)
        return mac_read8(a);
    if (a < 0x1000)
        return (uint8_t)(local_read32(a & ~3u) >> (8 * (a & 3)));
    return 0;
}

int sdio_read8(uint32_t addr) { return bus_read8(addr); }

int sdio_write8(uint32_t addr, uint8_t v)
{
    if (addr >= 0x10000)
        mac_write8(addr, v);
    else if (addr < 0x1000)
        local_reg[addr] = v;
    return 0;
}

int sdio_cmd52(int write, unsigned fn, uint32_t addr, uint8_t data)
{
    (void)fn;
    if (write)
        return sdio_write8(addr, data);
    return sdio_read8(addr);
}

int sdio_cmd53(int write, uint32_t addr, void *buf, uint32_t len)
{
    uint8_t *b = buf;
    if (addr >= 0x10000) {
        for (uint32_t i = 0; i < len; i++) {
            if (write)
                reg[(addr + i) & 0xffff] = b[i];
            else
                b[i] = mac_read8(addr + i);
        }
        if (write)
            for (uint32_t i = 0; i < len; i += 4)
                mac_hook((addr + i) & 0xfffc);
        return 0;
    }
    if (addr >= 0x8000) {
        if (write) {
            tx_fifo(addr & 0xe000, b, len);
            return 0;
        }
        CHECK((addr & 0xe000) == 0xe000);           /* the RX FIFO */
        uint32_t n = agg_len < len ? agg_len : len;
        memcpy(b, agg, n);
        if (len > n)
            memset(b + n, 0, len - n);
        agg_len = 0;
        return 0;
    }
    for (uint32_t i = 0; i < len; i += 4) {
        if (write)
            put32(local_reg + addr + i, rd32(b, i));
        else
            put32(b + i, local_read32(addr + i));
    }
    return 0;
}

/* ---------------------------------------------------------------- the access points */

static void aes_wrap(const uint8_t kek[16], const uint8_t *p, unsigned len, uint8_t *out)
{
    unsigned n = len / 8;
    uint8_t a[8], b[16];
    memset(a, 0xa6, 8);
    memcpy(out + 8, p, len);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, kek, 128);
    for (unsigned j = 0; j < 6; j++)
        for (unsigned i = 1; i <= n; i++) {
            memcpy(b, a, 8);
            memcpy(b + 8, out + 8 * i, 8);
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, b, b);
            uint64_t t = (uint64_t)n * j + i;
            memcpy(a, b, 8);
            for (int k = 0; k < 8; k++)
                a[7 - k] ^= (uint8_t)(t >> (8 * k));
            memcpy(out + 8 * i, b + 8, 8);
        }
    memcpy(out, a, 8);
    mbedtls_aes_free(&aes);
}

typedef struct {
    const char *ssid;
    uint8_t bssid[6];
    unsigned ch;
    int wpa2, beaconing, rssi;
    char pass[64];
    uint64_t next_beacon;
    enum { IDLE, AUTHED, ASSOCED, M1_SENT, M3_SENT, KEYS } state;
    uint8_t pmk[32], ptk[48], anonce[32], snonce[32], gtk[16], gtk_id;
    uint64_t replay, m1_time, last_pn;
    int m1_tries, probe_resps, m2_ok, m2_bad, m4_ok, g2_ok, plain_data, bad_data, seq;
} ap_t;

static ap_t ap[2];
static uint32_t sta_rcr(void) { return rd32(reg, 0x608); }

static unsigned beacon_body(const ap_t *a, uint8_t *f, uint16_t kind, const uint8_t *to)
{
    static const uint8_t rates[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
    static const uint8_t xrates[] = { 0x30, 0x48, 0x60, 0x6c };
    static const uint8_t rsn[] = { 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4,
                                   1, 0, 0x00, 0x0f, 0xac, 2, 0x0c, 0 };
    memset(f, 0, 36);
    f[0] = (uint8_t)kind;
    memcpy(f + 4, to, 6);
    memcpy(f + 10, a->bssid, 6);
    memcpy(f + 16, a->bssid, 6);
    f[32] = 100;
    f[34] = a->wpa2 ? 0x31 : 0x21;                  /* ESS, privacy, short preamble */
    f[35] = 0x04;                                   /* short slot */
    unsigned n = 36, l = (unsigned)strlen(a->ssid);
    f[n++] = 0; f[n++] = (uint8_t)l; memcpy(f + n, a->ssid, l); n += l;
    f[n++] = 1; f[n++] = 8; memcpy(f + n, rates, 8); n += 8;
    f[n++] = 3; f[n++] = 1; f[n++] = (uint8_t)a->ch;
    if (a->wpa2) {
        f[n++] = 48; f[n++] = sizeof rsn; memcpy(f + n, rsn, sizeof rsn); n += sizeof rsn;
    }
    f[n++] = 50; f[n++] = 4; memcpy(f + n, xrates, 4); n += 4;
    return n;
}

/* a frame from the AP, if the station's radio is on its channel */
static void ap_send(const ap_t *a, const uint8_t *f, unsigned len, uint8_t enc)
{
    if (sta_channel() != a->ch)
        return;
    if (wl_kind(f) == WL_FC_BEACON && (sta_rcr() & (1u << 7)) && memcmp(reg + 0x618, a->bssid, 6))
        return;                                     /* CBSSID_BCN: beacons of our BSS only */
    if (memcmp(f + 4, bcast, 6) && memcmp(f + 4, reg + 0x610, 6))
        return;                                     /* not to us */
    rx_push(f, len, a->rssi, enc);
}

static void ap_mgmt(ap_t *a, uint16_t kind, const uint8_t *body, unsigned blen)
{
    uint8_t f[64];
    memset(f, 0, 24);
    f[0] = (uint8_t)kind;
    memcpy(f + 4, sta_mac, 6);
    memcpy(f + 10, a->bssid, 6);
    memcpy(f + 16, a->bssid, 6);
    memcpy(f + 24, body, blen);
    ap_send(a, f, 24 + blen, 0);
}

static void ap_deauth(ap_t *a, unsigned reason)
{
    uint8_t b[2] = { (uint8_t)reason, 0 };
    ap_mgmt(a, WL_FC_DEAUTH, b, 2);
    a->state = IDLE;
}

/* an Ethernet frame (to the station or to everyone) as a data frame from the AP */
static void ap_data(ap_t *a, const uint8_t *eth, unsigned len, int protect)
{
    uint8_t f[1700];
    unsigned n = 0;
    memset(f, 0, 24);
    f[0] = 0x08;
    f[1] = protect ? 0x42 : 0x02;
    memcpy(f + 4, eth, 6);                          /* DA */
    memcpy(f + 10, a->bssid, 6);
    memcpy(f + 16, eth + 6, 6);                     /* SA */
    f[22] = (uint8_t)(a->seq << 4);
    f[23] = (uint8_t)(a->seq >> 4);
    a->seq = (a->seq + 1) & 0xfff;
    n = 24;
    if (protect) {
        memset(f + n, 0, 8);
        f[n + 3] = 0x20;
        n += 8;
    }
    memcpy(f + n, "\xaa\xaa\x03\x00\x00\x00", 6);
    n += 6;
    memcpy(f + n, eth + 12, len - 12);
    n += len - 12;
    if (protect) {
        memset(f + n, 0x3c, 8);                     /* the MIC the chip leaves in */
        n += 8;
    }
    ap_send(a, f, n, protect ? 4 : 0);
}

static unsigned eapol_key(uint8_t *e, unsigned info, unsigned keylen, uint64_t replay,
                          const uint8_t *nonce, const uint8_t *data, unsigned dlen, const uint8_t *kck)
{
    unsigned len = 99 + dlen;
    memset(e, 0, 99);
    e[0] = 2; e[1] = 3;
    e[2] = (uint8_t)((len - 4) >> 8); e[3] = (uint8_t)(len - 4);
    e[4] = 2;
    e[5] = (uint8_t)(info >> 8); e[6] = (uint8_t)info;
    e[7] = (uint8_t)(keylen >> 8); e[8] = (uint8_t)keylen;
    for (int i = 0; i < 8; i++)
        e[9 + i] = (uint8_t)(replay >> (56 - 8 * i));
    if (nonce)
        memcpy(e + 17, nonce, 32);
    e[97] = (uint8_t)(dlen >> 8); e[98] = (uint8_t)dlen;
    if (dlen)
        memcpy(e + 99, data, dlen);
    if (kck) {
        uint8_t h[20];
        wpa_hmac_sha1(kck, 16, e, len, h);
        memcpy(e + 81, h, 16);
    }
    return len;
}

static void ap_eapol(ap_t *a, const uint8_t *e, unsigned len, int protect)
{
    uint8_t eth[300];
    memcpy(eth, sta_mac, 6);
    memcpy(eth + 6, a->bssid, 6);
    eth[12] = 0x88; eth[13] = 0x8e;
    memcpy(eth + 14, e, len);
    ap_data(a, eth, 14 + len, protect);
}

static void ap_send_m1(ap_t *a)
{
    uint8_t e[128];
    unsigned n = eapol_key(e, 0x008a, 16, ++a->replay, a->anonce, NULL, 0, NULL);
    ap_eapol(a, e, n, 0);
    a->state = M1_SENT;
    a->m1_time = now_us;
    a->m1_tries++;
}

/* key data: our RSN IE, the GTK KDE, padding; wrapped with the KEK */
static unsigned gtk_data(const ap_t *a, uint8_t *out)
{
    static const uint8_t rsn[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4,
                                   1, 0, 0x00, 0x0f, 0xac, 2, 0x0c, 0 };
    uint8_t p[96];
    unsigned n = 0;
    memcpy(p, rsn, sizeof rsn);
    n = sizeof rsn;
    uint8_t kde[8] = { 0xdd, 22, 0x00, 0x0f, 0xac, 0x01, a->gtk_id, 0 };
    memcpy(p + n, kde, 8);
    memcpy(p + n + 8, a->gtk, 16);
    n += 24;
    if (n % 8) {
        p[n++] = 0xdd;
        while (n % 8)
            p[n++] = 0;
    }
    aes_wrap(a->ptk + 16, p, n, out);
    return n + 8;
}

static int mic_ok(const ap_t *a, const uint8_t *e, unsigned len)
{
    uint8_t tmp[300], h[20];
    if (len > sizeof tmp)
        return 0;
    memcpy(tmp, e, len);
    memset(tmp + 81, 0, 16);
    wpa_hmac_sha1(a->ptk, 16, tmp, len, h);
    return memcmp(h, e + 81, 16) == 0;
}

static void ap_rekey(ap_t *a)
{
    for (int i = 0; i < 16; i++)
        a->gtk[i] = (uint8_t)(0x40 + i);
    a->gtk_id = 2;
    uint8_t kd[128], e[256];
    unsigned kl = gtk_data(a, kd);
    unsigned n = eapol_key(e, 0x1382, 16, ++a->replay, NULL, kd, kl, a->ptk);
    ap_eapol(a, e, n, 1);
}

/* --- the router behind "CasaRossi": ARP, DHCP, ping (as tests/net/test_ethnet.c) --- */

static const uint8_t router_mac[6] = { 0x02, 0, 0, 0, 0, 1 };
static const uint8_t router_ip[4] = { 192, 168, 1, 1 }, our_ip[4] = { 192, 168, 1, 50 };
static int discovers, requests, arp_asks, echo_replies;

static uint16_t csum(const uint8_t *p, int len)
{
    uint32_t s = 0;
    for (int i = 0; i + 1 < len; i += 2)
        s += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (len & 1)
        s += (uint32_t)(p[len - 1] << 8);
    while (s >> 16)
        s = (s & 0xffff) + (s >> 16);
    return (uint16_t)~s;
}

static uint8_t rf_buf[1600];

static int ip_frame(const uint8_t *dst_mac, const uint8_t *dst_ip, uint8_t proto, int payload)
{
    uint8_t *f = rf_buf;
    memcpy(f, dst_mac, 6);
    memcpy(f + 6, router_mac, 6);
    f[12] = 0x08; f[13] = 0x00;
    uint8_t *ip = f + 14;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (uint8_t)((20 + payload) >> 8); ip[3] = (uint8_t)(20 + payload);
    ip[8] = 64;
    ip[9] = proto;
    memcpy(ip + 12, router_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    uint16_t c = csum(ip, 20);
    ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)c;
    return 34;
}

static void dhcp_reply(ap_t *a, const uint8_t *req, uint8_t type)
{
    static const uint8_t all[4] = { 255, 255, 255, 255 };
    int len = 8 + 236 + 4 + 3 + 6 + 6 + 6 + 6 + 1;
    int o = ip_frame(req + 28, all, 17, len);
    uint8_t *u = rf_buf + o;
    memset(u, 0, (size_t)len);
    u[1] = 67; u[3] = 68;
    u[4] = (uint8_t)(len >> 8); u[5] = (uint8_t)len;
    uint8_t *b = u + 8;
    b[0] = 2; b[1] = 1; b[2] = 6;
    memcpy(b + 4, req + 4, 4);
    memcpy(b + 16, our_ip, 4);
    memcpy(b + 28, req + 28, 16);
    uint8_t *p = b + 236;
    p[0] = 99; p[1] = 130; p[2] = 83; p[3] = 99;
    p += 4;
    *p++ = 53; *p++ = 1; *p++ = type;
    *p++ = 54; *p++ = 4; memcpy(p, router_ip, 4); p += 4;
    *p++ = 51; *p++ = 4; *p++ = 0; *p++ = 0; *p++ = 0x0e; *p++ = 0x10;
    *p++ = 1; *p++ = 4; *p++ = 255; *p++ = 255; *p++ = 255; *p++ = 0;
    *p++ = 3; *p++ = 4; memcpy(p, router_ip, 4); p += 4;
    *p++ = 255;
    ap_data(a, rf_buf, (unsigned)(o + len), a->wpa2);
}

static void router(ap_t *a, const uint8_t *e, unsigned len)
{
    if (e[12] == 0x08 && e[13] == 0x06) {           /* ARP */
        const uint8_t *r = e + 14;
        if (r[7] == 1 && !memcmp(r + 24, router_ip, 4)) {
            arp_asks++;
            uint8_t *f = rf_buf;
            memcpy(f, r + 8, 6);
            memcpy(f + 6, router_mac, 6);
            f[12] = 0x08; f[13] = 0x06;
            memcpy(f + 14, r, 6);
            f[20] = 0; f[21] = 2;
            memcpy(f + 22, router_mac, 6);
            memcpy(f + 28, router_ip, 4);
            memcpy(f + 32, r + 8, 10);
            ap_data(a, f, 42, a->wpa2);
        }
        return;
    }
    if (e[12] != 0x08 || e[13] != 0x00)
        return;
    const uint8_t *ip = e + 14;
    int ihl = (ip[0] & 15) * 4;
    CHECK(csum(ip, ihl) == 0);
    if (ip[9] == 17 && ip[ihl + 3] == 67) {
        const uint8_t *b = ip + ihl + 8;
        uint8_t type = 0;
        for (const uint8_t *p = b + 240; p < e + len && *p != 255; p += p[0] ? p[1] + 2 : 1)
            if (p[0] == 53)
                type = p[2];
        if (type == 1) {
            discovers++;
            dhcp_reply(a, b, 2);
        } else if (type == 3) {
            requests++;
            dhcp_reply(a, b, 5);
        }
    } else if (ip[9] == 1 && ip[ihl] == 0 && !memcmp(ip + 16, router_ip, 4)) {
        const uint8_t *icmp = ip + ihl;
        int ilen = (ip[2] << 8 | ip[3]) - ihl;
        CHECK(csum(icmp, ilen) == 0);
        CHECK(icmp[4] == 0x12 && icmp[5] == 0x34 && ilen == 8 + 32 && icmp[8 + 31] == 'p' + 31);
        echo_replies++;
    }
}

static void ping(ap_t *a)
{
    int o = ip_frame(sta_mac, our_ip, 1, 8 + 32);
    uint8_t *icmp = rf_buf + o;
    memset(icmp, 0, 8);
    icmp[0] = 8;
    icmp[4] = 0x12; icmp[5] = 0x34; icmp[7] = 7;
    for (int i = 0; i < 32; i++)
        icmp[8 + i] = (uint8_t)('p' + i);
    uint16_t c = csum(icmp, 40);
    icmp[2] = (uint8_t)(c >> 8); icmp[3] = (uint8_t)c;
    ap_data(a, rf_buf, (unsigned)(o + 40), a->wpa2);
}

/* --- what the AP hears --- */

static void ap_hear_eapol(ap_t *a, const uint8_t *e, unsigned len, int protect)
{
    unsigned info = (unsigned)(e[5] << 8 | e[6]);
    if (info == 0x010a && a->state == M1_SENT) {    /* message 2 */
        memcpy(a->snonce, e + 17, 32);
        uint8_t d[76];
        int lo = memcmp(a->bssid, sta_mac, 6) < 0;
        memcpy(d, lo ? a->bssid : sta_mac, 6);
        memcpy(d + 6, lo ? sta_mac : a->bssid, 6);
        lo = memcmp(a->anonce, a->snonce, 32) < 0;
        memcpy(d + 12, lo ? a->anonce : a->snonce, 32);
        memcpy(d + 44, lo ? a->snonce : a->anonce, 32);
        wpa_prf(a->pmk, 32, "Pairwise key expansion", d, 76, a->ptk, 48);
        if (!mic_ok(a, e, len)) {
            a->m2_bad++;                            /* another password: M1 again later */
            return;
        }
        a->m2_ok++;
        CHECK(e[97] == 0 && e[98] == 22 && e[99] == 48);   /* our RSN IE */
        uint8_t kd[128], m3[256];
        unsigned kl = gtk_data(a, kd);
        unsigned n = eapol_key(m3, 0x13ca, 16, ++a->replay, a->anonce, kd, kl, a->ptk);
        ap_eapol(a, m3, n, 0);
        a->state = M3_SENT;
    } else if (info == 0x030a && a->state == M3_SENT) {
        CHECK(mic_ok(a, e, len) && !protect);
        a->m4_ok++;
        a->state = KEYS;
    } else if (info == 0x0302 && a->state == KEYS) {
        CHECK(mic_ok(a, e, len) && protect);
        a->g2_ok++;
    }
}

static void ap_hear(ap_t *a, const uint8_t *f, unsigned len, const txinfo_t *t)
{
    uint16_t kind = wl_kind(f);
    if (kind == WL_FC_PROBE_REQ) {
        uint8_t r[256];
        unsigned n = beacon_body(a, r, WL_FC_PROBE_RESP, sta_mac);
        a->probe_resps++;
        ap_send(a, r, n, 0);
        return;
    }
    if (memcmp(f + 4, a->bssid, 6))
        return;
    if (kind == WL_FC_AUTH) {
        uint8_t b[6] = { 0, 0, 2, 0, 0, 0 };
        a->state = AUTHED;
        ap_mgmt(a, WL_FC_AUTH, b, 6);
    } else if (kind == WL_FC_ASSOC_REQ && a->state >= AUTHED) {
        int has_rsn = 0;
        for (unsigned i = 28; i + 2 <= len; i += 2u + f[i + 1])
            if (f[i] == 48)
                has_rsn = 1;
        CHECK(has_rsn == a->wpa2);
        uint8_t b[6] = { 0x31, 0x04, 0, 0, 0x01, 0xc0 };    /* status 0, AID 1 */
        ap_mgmt(a, WL_FC_ASSOC_RESP, b, 6);
        a->state = ASSOCED;
        a->m1_tries = 0;
        a->last_pn = 0;
        if (a->wpa2)
            ap_send_m1(a);
    } else if (kind == WL_FC_DEAUTH) {
        a->state = IDLE;
    } else if ((f[0] & 0x0c) == 0x08 && (f[1] & 3) == 1 && a->state >= ASSOCED) {
        int protect = (f[1] & 0x40) != 0;
        unsigned at = 24;
        if (protect) {
            CHECK(t->sec_type == 3);
            uint64_t pn = (uint64_t)f[24] | (uint64_t)f[25] << 8 | (uint64_t)f[28] << 16 |
                          (uint64_t)f[29] << 24 | (uint64_t)f[30] << 32 | (uint64_t)f[31] << 40;
            CHECK(f[27] == 0x20 && pn > a->last_pn);
            a->last_pn = pn;
            at += 8;
        } else {
            CHECK(t->sec_type == 0);
        }
        CHECK(t->seq == (((f[22] | f[23] << 8) >> 4) & 0xfff));
        if (len < at + 8 || memcmp(f + at, "\xaa\xaa\x03\x00\x00\x00", 6))
            return;
        uint8_t eth[1600];
        memcpy(eth, f + 16, 6);
        memcpy(eth + 6, f + 10, 6);
        memcpy(eth + 12, f + at + 6, len - at - 6);
        unsigned elen = 12 + len - at - 6;
        if (eth[12] == 0x88 && eth[13] == 0x8e) {
            CHECK(t->use_rate && t->rate == 0);     /* EAPOL at 1 Mb/s */
            ap_hear_eapol(a, eth + 14, elen - 14, protect);
            return;
        }
        if (a->wpa2 && !protect) {
            a->bad_data++;
            return;
        }
        if (!a->wpa2)
            a->plain_data++;
        CHECK(!t->use_rate);                        /* the firmware's rate adaptation */
        router(a, eth, elen);
    }
}

static void air(const uint8_t *f, unsigned len, const txinfo_t *t)
{
    for (int i = 0; i < 2; i++)
        if (sta_channel() == ap[i].ch)
            ap_hear(&ap[i], f, len, t);
}

static void sim_tick(void)
{
    for (int i = 0; i < 2; i++) {
        ap_t *a = &ap[i];
        while (a->next_beacon <= now_us) {
            if (a->beaconing && now_us - a->next_beacon < 200000) {
                uint8_t f[256];
                unsigned n = beacon_body(a, f, WL_FC_BEACON, bcast);
                ap_send(a, f, n, 0);
            }
            a->next_beacon += 102400;
        }
        if (a->state == M1_SENT && now_us - a->m1_time > 1000000) {
            if (a->m1_tries < 4)
                ap_send_m1(a);
            else
                ap_deauth(a, 15);                   /* 4-way handshake timeout */
        }
    }
}

/* ---------------------------------------------------------------- the test */

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < ms; i++) {
        timer_delay_us(1000);
        net_poll();
    }
}

static const uint32_t *find_h2c(uint8_t id, int last)
{
    const uint32_t *r = NULL;
    for (int i = 0; i < nh2c; i++)
        if ((h2c[i].w0 & 0xff) == id) {
            r = &h2c[i].w0;
            if (!last)
                break;
        }
    return r;
}

static int cam_has(unsigned idx, unsigned keyid, unsigned type, int group, const uint8_t *mac,
                   const uint8_t *key)
{
    const uint32_t *w = cam[idx];
    uint32_t w0 = (keyid & 3) | type << 2 | (uint32_t)group << 6 | 1u << 15 |
                  (uint32_t)mac[0] << 16 | (uint32_t)mac[1] << 24;
    if (w[0] != w0)
        return 0;
    if (w[1] != (mac[2] | mac[3] << 8 | mac[4] << 16 | (uint32_t)mac[5] << 24))
        return 0;
    for (int i = 0; i < 4; i++)
        if (w[2 + i] != rd32(key, 4 * i))
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    ap[0] = (ap_t){ .ssid = "CasaRossi", .bssid = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 }, .ch = 6,
                    .wpa2 = 1, .beaconing = 1, .rssi = -48, .pass = "bm-console-2026" };
    ap[1] = (ap_t){ .ssid = "Ospiti", .bssid = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01 }, .ch = 11,
                    .beaconing = 1, .rssi = -67, .next_beacon = 40000 };
    wpa_pmk(ap[0].pass, (const uint8_t *)ap[0].ssid, 9, ap[0].pmk);
    for (int i = 0; i < 32; i++)
        ap[0].anonce[i] = (uint8_t)(0x10 + i);
    for (int i = 0; i < 16; i++)
        ap[0].gtk[i] = (uint8_t)(0x80 + i);
    ap[0].gtk_id = 1;
    /* the wrap used by the AP against wpa.c's unwrap */
    {
        uint8_t k[16] = { 1 }, p[24], c[32], back[24];
        for (int i = 0; i < 24; i++) p[i] = (uint8_t)(i * 7);
        aes_wrap(k, p, 24, c);
        CHECK(wpa_unwrap(k, c, 32, back) == 0 && !memcmp(back, p, 24));
    }

    /* start: the chip, the radio */
    CHECK(wifi_start() == 0 && chip_starts == 1);
    CHECK(logged("radio on", 0) && logged("radio tables loaded", 0));
    CHECK(!memcmp(reg + 0x610, sta_mac, 6));

    /* scan */
    size_t mark = loglen;
    CHECK(wifi_scan() == 2);
    CHECK(logged("CasaRossi", mark) && logged("Ospiti", mark));
    CHECK(logged("ch  6  WPA2  CasaRossi", mark) && logged("ch 11  open  Ospiti", mark));
    CHECK(logged("-48 dBm", mark));
    CHECK(logged("probe responses to our MAC: transmitting works", mark));
    CHECK(ap[0].probe_resps == 1 && ap[1].probe_resps == 1 && bad_desc == 0);

    /* no network saved */
    mark = loglen;
    CHECK(wifi_connect() == -1 && logged("no network saved", mark));

    /* a wrong password: our message 2 has a bad MIC, the AP gives up */
    config_set("wifi_ssid", "CasaRossi");
    config_set("wifi_psk", "sbagliata!");
    mark = loglen;
    CHECK(wifi_connect() == -1);
    CHECK(logged("authenticated", mark) && logged("associated, AID 1", mark));
    CHECK(logged("reason 15", mark) && logged("wrong password", mark));
    CHECK(ap[0].m2_bad >= 2 && ap[0].m2_ok == 0 && !wifi_linked());
    CHECK(!(cam[4][0] & (1u << 15)));

    /* the right password */
    config_set("wifi_psk", "bm-console-2026");
    nh2c = 0;
    mark = loglen;
    int iq0 = iqks;
    CHECK(wifi_connect() == 0 && wifi_linked());
    CHECK(iqks == iq0 + 1 && logged("IQ calibration done", mark));
    CHECK(logged("WPA2 keys in", mark) && ap[0].m2_ok == 1 && ap[0].m4_ok == 1 && ap[0].state == KEYS);
    CHECK(cam_has(4, 0, 4, 0, ap[0].bssid, ap[0].ptk + 32));
    CHECK(cam_has(1, 1, 4, 1, bcast, ap[0].gtk));
    CHECK(!memcmp(reg + 0x618, ap[0].bssid, 6));
    CHECK(((rd32(reg, 0x100) >> 16) & 3) == 2 && (rd32(reg, 0x6a8) & 0x7ff) == 1);
    const uint32_t *ra = find_h2c(0x40, 1), *ms = find_h2c(0x01, 1);
    CHECK(ra && ((ra[0] >> 16) & 0x1f) == 6 && ra[1] == 0xfff);
    CHECK(ms && ((ms[0] >> 8) & 1) == 1);
    CHECK((rd32(reg, 0x608) & (1u << 7)) != 0);     /* beacons of our BSS only */

    /* DHCP, ARP and a ping over the protected link */
    mark = loglen;
    CHECK(net_start(&net_wifi) == 0);
    for (int i = 0; i < 100 && !net_ip(); i++)
        run_ms(100);
    CHECK(net_ip() != 0 && logged("net: IP 192.168.1.50", mark));
    CHECK(discovers >= 1 && requests >= 1 && ap[0].bad_data == 0);
    ping(&ap[0]);
    run_ms(200);
    CHECK(echo_replies == 1);

    /* group rekey */
    ap_rekey(&ap[0]);
    run_ms(50);
    CHECK(ap[0].g2_ok == 1 && cam_has(2, 2, 4, 1, bcast, ap[0].gtk) && wifi_linked());
    ping(&ap[0]);
    run_ms(200);
    CHECK(echo_replies == 2);

    /* the link stays up over a minute of beacons */
    run_ms(60000);
    CHECK(wifi_linked() && rx_dropped == 0);

    /* the AP sends us away */
    mark = loglen;
    ap_deauth(&ap[0], 3);
    run_ms(20);
    CHECK(!wifi_linked() && logged("link lost: the network sent us away (reason 3", mark));
    CHECK(!(cam[4][0] & (1u << 15)) && !(cam[1][0] & (1u << 15)));
    ms = find_h2c(0x01, 1);
    CHECK(ms && ((ms[0] >> 8) & 1) == 0);

    /* an open network; then it goes quiet (beacon loss) */
    config_set("wifi_ssid", "Ospiti");
    mark = loglen;
    CHECK(wifi_connect() == 0 && wifi_linked() && !logged("WPA2", mark));
    uint8_t eth[60];
    memset(eth, 0, sizeof eth);
    memcpy(eth, router_mac, 6);
    memcpy(eth + 6, sta_mac, 6);
    eth[12] = 0x08; eth[13] = 0x00;
    eth[14] = 0x45;
    uint16_t c = csum(eth + 14, 20);
    eth[24] = (uint8_t)(c >> 8); eth[25] = (uint8_t)c;
    CHECK(wifi_send(eth, 60) == 0 && ap[1].plain_data == 1);
    ap[1].beaconing = 0;
    mark = loglen;
    run_ms(9000);
    CHECK(!wifi_linked() && logged("no beacon from the network for 8 s", mark));
    CHECK(bad_desc == 0);

    if (fails) {
        printf("wifi_sim_test: %d failures (-v: the console)\n", fails);
        return 1;
    }
    printf("wifi_sim_test: ok\n");
    return 0;
}
