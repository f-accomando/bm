/*
 * H5 transport and Realtek set-up on the PC (make TARGET=rgb30 test-bt):
 * src/bt/h5.c and rtlbt.c talk to a simulated RTL8821CS through two byte
 * queues standing in for the UART. The simulated controller answers SYNC
 * and CONFIG, acknowledges reliable packets (dropping some, to exercise
 * retransmission), answers HCI commands and collects the firmware pieces,
 * which must rebuild exactly the image of the real firmware file.
 *   h5_test [rtl8821cs_fw.bin rtl8821cs_config.bin]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bt/h5.h"
#include "bt/hci.h"
#include "bt/rtlbt.h"
#include "bt/btuart.h"

/* --- the simulated clock and UART --- */

static uint64_t now_us;
uint32_t timer_ticks(void) { return (uint32_t)now_us; }
void timer_delay_us(uint32_t us) { now_us += us; }
void timer_delay_ms(uint32_t ms) { now_us += ms * 1000ull; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

#define QSZ (1 << 16)
static uint8_t to_ctl[QSZ], to_host[QSZ];
static unsigned c_head, c_tail, h_head, h_tail;

static void controller_poll(void);

void btuart_init(uint32_t baud) { (void)baud; }
void btuart_set_baud(uint32_t baud) { (void)baud; }
void btuart_drain(void) { h_tail = h_head; }

void btuart_write(const void *buf, uint32_t len)
{
    const uint8_t *p = buf;
    while (len--) {
        to_ctl[c_head] = *p++;
        c_head = (c_head + 1) % QSZ;
    }
    controller_poll();
}

int btuart_ready(void)
{
    controller_poll();
    return h_tail != h_head;
}

int btuart_read(uint32_t timeout_us)
{
    uint64_t end = now_us + timeout_us;
    for (;;) {
        controller_poll();
        if (h_tail != h_head) {
            uint8_t c = to_host[h_tail];
            h_tail = (h_tail + 1) % QSZ;
            return c;
        }
        if (now_us >= end)
            return -1;
        now_us += 100;
    }
}

/* --- the simulated controller --- */

static struct {
    int synced, configured;
    uint8_t rx_expect, tx_seq;
    uint8_t frame[4200];
    unsigned flen;
    int in_frame, esc;
    unsigned drop_every, received;      /* drop every Nth reliable packet */
    uint8_t *fw;                        /* rebuilt from 0xFC20 */
    size_t fwlen;
    int last_index, done;
    unsigned commands;
} c;

static void c_put(const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (p[i] == 0xc0) { to_host[h_head] = 0xdb; h_head = (h_head + 1) % QSZ; to_host[h_head] = 0xdc; }
        else if (p[i] == 0xdb) { to_host[h_head] = 0xdb; h_head = (h_head + 1) % QSZ; to_host[h_head] = 0xdd; }
        else to_host[h_head] = p[i];
        h_head = (h_head + 1) % QSZ;
    }
}

static void c_send(int reliable, uint8_t type, const uint8_t *data, unsigned len, int with_crc)
{
    uint8_t h[4];
    h[0] = (uint8_t)((reliable ? 0x80 : 0) | (with_crc ? 0x40 : 0) | (c.rx_expect & 7) << 3 |
                     (reliable ? c.tx_seq : 0));
    if (reliable)
        c.tx_seq = (c.tx_seq + 1) & 7;
    h[1] = (uint8_t)(type | (len & 15) << 4);
    h[2] = (uint8_t)(len >> 4);
    h[3] = (uint8_t)~(h[0] + h[1] + h[2]);
    to_host[h_head] = 0xc0; h_head = (h_head + 1) % QSZ;
    c_put(h, 4);
    c_put(data, len);
    if (with_crc) {
        uint8_t all[4200];
        memcpy(all, h, 4);
        memcpy(all + 4, data, len);
        uint16_t crc = h5_crc(all, 4 + len);
        uint8_t b[2] = { (uint8_t)(crc >> 8), (uint8_t)crc };
        c_put(b, 2);
    }
    to_host[h_head] = 0xc0; h_head = (h_head + 1) % QSZ;
}

static void c_event_cc(uint16_t op, const uint8_t *ret, unsigned n)
{
    uint8_t e[260] = { 0x0e, (uint8_t)(3 + n), 1, (uint8_t)op, (uint8_t)(op >> 8) };
    memcpy(e + 5, ret, n);
    c_send(1, 4, e, 5 + n, c.commands % 3 == 0);    /* every third with a CRC */
}

static void c_command(const uint8_t *p, unsigned len)
{
    uint16_t op = (uint16_t)(p[0] | p[1] << 8);
    const uint8_t *par = p + 3;
    c.commands++;
    if (op == 0x1001) {                 /* local version: ROM 8821, rev 0xc, hci 8 */
        uint8_t r[9] = { 0, 8, 0x0c, 0, 8, 0x5d, 0, 0x21, 0x88 };
        c_event_cc(op, r, 9);
    } else if (op == 0xfc6d) {          /* ROM version 1 */
        uint8_t r[2] = { 0, 1 };
        c_event_cc(op, r, 2);
    } else if (op == 0xfc20) {
        int idx = par[0];
        unsigned n = len - 4;
        c.fw = realloc(c.fw, c.fwlen + n);
        memcpy(c.fw + c.fwlen, par + 1, n);
        c.fwlen += n;
        if (idx & 0x80)
            c.done = 1;
        c.last_index = idx;
        uint8_t r[2] = { 0, (uint8_t)idx };
        c_event_cc(op, r, 2);
    } else {
        uint8_t r[1] = { 0 };
        c_event_cc(op, r, 1);
    }
}

static void c_frame(void)
{
    uint8_t *f = c.frame;
    if (c.flen < 4 || (uint8_t)(f[0] + f[1] + f[2] + f[3]) != 0xff)
        return;
    unsigned len = (f[1] >> 4) | f[2] << 4, type = f[1] & 15;
    if (type == 15) {
        uint8_t resp[3];
        if (f[4] == 0x01) { resp[0] = 0x02; resp[1] = 0x7d; c_send(0, 15, resp, 2, 0); c.synced = 1; }
        else if (f[4] == 0x03) { resp[0] = 0x04; resp[1] = 0x7b; resp[2] = 0x14; c_send(0, 15, resp, 3, 0); c.configured = 1; }
        return;
    }
    if (f[0] & 0x80) {
        if ((f[0] & 7) != c.rx_expect)
            return;                     /* out of order: the host retransmits */
        c.received++;
        if (c.drop_every && c.received % c.drop_every == 0)
            return;                     /* "lost": no ack, no answer */
        c.rx_expect = (c.rx_expect + 1) & 7;
        if (type == 1)
            c_command(f + 4, len);
        else
            c_send(0, 0, NULL, 0, 0);   /* pure ack */
    }
}

static void controller_poll(void)
{
    while (c_tail != c_head) {
        uint8_t b = to_ctl[c_tail];
        c_tail = (c_tail + 1) % QSZ;
        if (b == 0xc0) {
            if (c.in_frame && c.flen)
                c_frame();
            c.in_frame = 1;
            c.flen = 0;
            c.esc = 0;
            continue;
        }
        if (c.esc) { b = b == 0xdc ? 0xc0 : 0xdb; c.esc = 0; }
        else if (b == 0xdb) { c.esc = 1; continue; }
        if (c.flen < sizeof c.frame)
            c.frame[c.flen++] = b;
    }
}

/* --- tests --- */

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*len);
    if (fread(d, 1, *len, f) != *len) { free(d); d = NULL; }
    fclose(f);
    return d;
}

int main(int argc, char **argv)
{
    /* CRC: CRC-16/MCRF4XX of "123456789" is 0x6F91, bit-reversed 0x89F6 */
    CHECK(h5_crc((const uint8_t *)"123456789", 9) == 0x89f6, "crc %04x", h5_crc((const uint8_t *)"123456789", 9));

    CHECK(h5_open(2000) == 0, "link not established");
    CHECK(c.synced && c.configured, "sync %d config %d", c.synced, c.configured);
    CHECK(h5_peer_config() == 0x14, "peer config %d", h5_peer_config());
    hci_set_transport(&h5_transport);

    uint8_t v[8];
    CHECK(hci_cmd(0x1001, NULL, 0, v, sizeof v, 500000) == 0, "read local version");
    CHECK((v[6] | v[7] << 8) == 0x8821 && v[0] == 8, "version %02x lmp %04x", v[0], v[6] | v[7] << 8);

    /* lost packets: the host retransmits them */
    c.drop_every = 5;
    for (int i = 0; i < 12; i++)
        CHECK(hci_cmd(0x0c03, NULL, 0, NULL, 0, 2000000) == 0, "reset %d with losses", i);
    unsigned re, crc, bad;
    h5_stats(&re, &crc, &bad);
    CHECK(re >= 2, "retransmissions %u", re);
    CHECK(crc == 0 && bad == 0, "crc errors %u, bad headers %u", crc, bad);
    c.drop_every = 0;

    if (argc >= 3) {
        size_t fwlen, cfglen, imglen;
        uint8_t *fw = load(argv[1], &fwlen), *cfg = load(argv[2], &cfglen), *img;
        CHECK(fw && cfg, "cannot read %s / %s", argv[1], argv[2]);
        if (fw && cfg) {
            const char *err = "";
            uint32_t word, baud;
            int flow;
            CHECK(rtlbt_uart_config(cfg, cfglen, &word, &baud, &flow) == 0, "config");
            CHECK(word == 0x04928002u && baud == 1500000 && flow, "uart %08x %u %d", word, baud, flow);
            uint8_t rv[2];
            CHECK(hci_cmd(0xfc6d, NULL, 0, rv, 2, 500000) == 0 && rv[0] == 1, "rom version");
            CHECK(rtlbt_patch(fw, fwlen, cfg, cfglen, rv[0], &img, &imglen, &err) == 0, "patch: %s", err);
            CHECK(imglen == 36928 + 25, "image %zu bytes", imglen);
            /* the patch for ROM 1 starts at 0x4cc0; its last 4 bytes are the fw version */
            CHECK(memcmp(img, fw + 0x4cc0, 36928 - 4) == 0, "patch body");
            CHECK(img[36924] == 0x98 && img[36925] == 0xf0 && img[36926] == 0xb8 && img[36927] == 0x75,
                  "fw version %02x %02x %02x %02x", img[36924], img[36925], img[36926], img[36927]);
            CHECK(memcmp(img + 36928, cfg, cfglen) == 0, "config appended");
            unsigned frags;
            c.drop_every = 7;
            CHECK(rtlbt_download(img, imglen, &frags) == 0, "download stopped at %u", frags);
            CHECK(frags == imglen / 252 + 1, "fragments %u", frags);
            CHECK(c.done && c.fwlen == imglen && memcmp(c.fw, img, imglen) == 0,
                  "controller got %zu bytes (done %d)", c.fwlen, c.done);
            CHECK(c.last_index == (0x80 | ((frags - 1) % 0x7f == 0 ? 0x7f : 0)) || (c.last_index & 0x80),
                  "last index %02x", c.last_index);
            uint8_t bad_rom = 5;
            CHECK(rtlbt_patch(fw, fwlen, cfg, cfglen, bad_rom, &img, &imglen, &err) != 0, "rom 5 accepted");
        }
    } else {
        printf("(no firmware files: patch and download not tested)\n");
    }
    printf(fails ? "h5_test: %d failed\n" : "h5_test: ok\n", fails);
    return fails != 0;
}
