/*
 * Bluetooth on the Pi Zero W (BCM43438): up to four HID game controllers
 * (the DualShock 4 is the reference), one per player. Classic Bluetooth
 * only:
 *
 *   chip up      power, 32 kHz clock, firmware patch, event mask, SSP on
 *   pairing      inquiry, Create Connection, SSP "Just Works" (no MITM),
 *                encryption, L2CAP channels 0x11 (control) 0x13 (interrupt);
 *                the link key goes to bm33/config.txt as bt_pad<player>
 *   reconnect    page scan on: a pad connects to us (PS button), we answer
 *                the Link Key Request with its saved key, it opens the
 *                L2CAP channels
 *   input        HID reports (0xA1 ...) on the interrupt channel -> hid layer,
 *                as the buttons of that pad's player
 *   light        an output report sets the pad's light to the player colour
 *
 * Each pad is an ACL link of its own, with its own L2CAP channels (channel
 * ids are per link, so every link uses the same local ones).
 * No SDP: the report format of the supported pad is known.
 */
#include "bt.h"
#include "btuart.h"
#include "hci.h"
#include "drivers/board.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/crc32.h"
#include "lib/printf.h"
#include "usb/hid.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BT_FAST_BAUD 921600u       /* UART speed after the firmware patch */
#define BT_ON_GPIO   45             /* BT_REG_ON of the BCM43438 on the Zero W */
#define LPO_GPIO     43             /* GPCLK2: 32.768 kHz sleep clock */

#define CM_GP2CTL    (PERIPHERAL_BASE + 0x101080)
#define CM_GP2DIV    (PERIPHERAL_BASE + 0x101084)
#define CM_PASSWD    0x5A000000u
#define CM_ENAB      (1u << 4)
#define CM_BUSY      (1u << 7)

#define PSM_SDP           0x01
#define PSM_HID_CONTROL   0x11
#define PSM_HID_INTERRUPT 0x13
#define CID_SIGNALING     0x0001
#define CID_CONTROL       0x0040    /* our local channel ids (per link) */
#define CID_INTERRUPT     0x0041
#define CID_SDP           0x0042

/* a pad that came back and opened no HID channel after this long gets them
 * opened by us (some pads ask SDP first and then wait for the host) */
#define HOST_OPEN_MS      1000

/* L2CAP signaling codes */
#define L2_CMD_REJECT   0x01
#define L2_CONN_REQ     0x02
#define L2_CONN_RSP     0x03
#define L2_CONF_REQ     0x04
#define L2_CONF_RSP     0x05
#define L2_DISC_REQ     0x06
#define L2_DISC_RSP     0x07
#define L2_ECHO_REQ     0x08
#define L2_ECHO_RSP     0x09
#define L2_INFO_REQ     0x0A
#define L2_INFO_RSP     0x0B

/* DualShock 4 output report 0x11 over Bluetooth: 78 bytes, CRC-32 of the
 * 0xA2 header and the first 74 bytes in the last four. */
#define DS4_OUT_LEN     78
#define DS4_POLL_MS     8           /* report every 8 ms: four pads fit the UART */

typedef struct {
    uint16_t lcid, rcid;
    int requested, open;
    int conf_in, conf_out;          /* their config accepted / ours accepted */
} chan_t;

typedef struct {
    int used;                       /* connecting or connected */
    int connected;                  /* ACL link up */
    uint16_t handle;
    uint8_t addr[6];                /* peer */
    int auth_done, auth_status;
    int enc;
    chan_t ctrl, intr, sdp;
    uint8_t sig_id;
    uint32_t since;                 /* timer_ticks() of Connection Complete */
    int host_step;                  /* HOST_OPEN_MS fallback: 0 idle, 1 auth, 2 encrypt, 3 channels */
    int announced;
    int slot;                       /* player - 1, or -1 before it has a key */
} link_t;

static struct {
    int started;
    link_t link[BT_PADS];
    int have_key[BT_PADS];
    uint8_t key_addr[BT_PADS][6], key[BT_PADS][16];
    int legacy_key;                 /* slot 0 came from the old "bt_pad" key */
    int pairing;
    int disc_reason;                /* last Disconnection Complete, -1 none */
} bt;

/* The player colours, also used by the games (dim: the light bar is bright) */
static const uint8_t led_rgb[BT_PADS][3] = {
    { 0x00, 0x20, 0x80 },           /* 1 blue */
    { 0x80, 0x08, 0x00 },           /* 2 red */
    { 0x00, 0x80, 0x10 },           /* 3 green */
    { 0x80, 0x00, 0x50 },           /* 4 pink */
};

static int tracing(const link_t *l)
{
    return bt.pairing || (l && l->connected && !l->announced);
}

/* ---------------------------------------------------------------- helpers */

/* While a pad is being paired or is coming back, every event and L2CAP
 * signal is shown (grey): a photo of the screen tells what the pad did. */
static void trace(const link_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void trace(const link_t *l, const char *fmt, ...)
{
    if (!tracing(l))
        return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    kprintf("\x1b[90mbt:   %s\x1b[0m\n", buf);
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void addr_str(char *out, const uint8_t *a)
{
    ksnprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[5], a[4], a[3], a[2], a[1], a[0]);
}

static int parse_hex(const char *s, uint8_t *out, int n, char sep)
{
    for (int i = 0; i < n; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = *s++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return -1;
        }
        out[i] = (uint8_t)v;
        if (sep && i + 1 < n && *s++ != sep)
            return -1;
    }
    return 0;
}

static void key_name(char *out, int slot)
{
    ksnprintf(out, 12, "bt_pad%d", slot + 1);
}

/* "bt_pad1=00:1f:e2:bf:d7:dd <32 hex digits of the link key>"; the key of
 * older versions, "bt_pad", is player 1 when bt_pad1 is not there. */
static int parse_key(const char *v, int slot)
{
    uint8_t a[6];
    if (!v || parse_hex(v, a, 6, ':') || v[17] != ' ' || parse_hex(v + 18, bt.key[slot], 16, 0))
        return 0;
    for (int i = 0; i < 6; i++)
        bt.key_addr[slot][i] = a[5 - i];        /* written most significant first */
    return 1;
}

static void load_keys(void)
{
    char name[12];
    for (int s = 0; s < BT_PADS; s++) {
        key_name(name, s);
        bt.have_key[s] = parse_key(config_get(name), s);
    }
    bt.legacy_key = 0;
    if (!bt.have_key[0] && parse_key(config_get("bt_pad"), 0))
        bt.have_key[0] = bt.legacy_key = 1;
}

static int nkeys(void)
{
    int n = 0;
    for (int s = 0; s < BT_PADS; s++)
        n += bt.have_key[s];
    return n;
}

static int key_slot(const uint8_t *addr)
{
    for (int s = 0; s < BT_PADS; s++)
        if (bt.have_key[s] && memcmp(bt.key_addr[s], addr, 6) == 0)
            return s;
    return -1;
}

static void key_line(char *v, int slot)
{
    char a[18];
    addr_str(a, bt.key_addr[slot]);
    int n = ksnprintf(v, 72, "%s ", a);
    for (int i = 0; i < 16; i++)
        n += ksnprintf(v + n, 72 - (unsigned)n, "%02x", bt.key[slot][i]);
}

static int slot_connected(int s);

/* The slot a newly paired pad takes: its own if it was paired before, else
 * the first free one, else the first whose pad is not connected. */
static int new_slot(const uint8_t *addr)
{
    int s = key_slot(addr);
    if (s >= 0)
        return s;
    for (s = 0; s < BT_PADS; s++)
        if (!bt.have_key[s])
            return s;
    for (s = 0; s < BT_PADS; s++)
        if (!slot_connected(s)) {
            kprintf("bt: all %d player slots are taken: the new pad replaces player %d\n",
                    BT_PADS, s + 1);
            return s;
        }
    return -1;
}

static int save_key(const uint8_t *addr, const uint8_t *key)
{
    int s = new_slot(addr);
    if (s < 0)
        return -1;
    memcpy(bt.key_addr[s], addr, 6);
    memcpy(bt.key[s], key, 16);
    bt.have_key[s] = 1;
    char name[12], v[72];
    if (bt.legacy_key) {                        /* the old key becomes bt_pad1 */
        if (s != 0) {
            key_line(v, 0);
            config_set("bt_pad1", v);
        }
        config_unset("bt_pad");
        bt.legacy_key = 0;
    }
    key_name(name, s);
    key_line(v, s);
    config_set(name, v);
    config_save();
    return s;
}

/* ---------------------------------------------------------------- links */

static link_t *link_by_handle(uint16_t h)
{
    for (int i = 0; i < BT_PADS; i++)
        if (bt.link[i].used && bt.link[i].connected && bt.link[i].handle == h)
            return &bt.link[i];
    return NULL;
}

static link_t *link_by_addr(const uint8_t *a)
{
    for (int i = 0; i < BT_PADS; i++)
        if (bt.link[i].used && memcmp(bt.link[i].addr, a, 6) == 0)
            return &bt.link[i];
    return NULL;
}

static void reset_link(link_t *l)
{
    if (l->slot >= 0)
        hid_bt_clear(l->slot);
    l->connected = l->auth_done = l->enc = l->announced = 0;
    memset(&l->ctrl, 0, sizeof l->ctrl);
    memset(&l->intr, 0, sizeof l->intr);
    memset(&l->sdp, 0, sizeof l->sdp);
    l->ctrl.lcid = CID_CONTROL;
    l->intr.lcid = CID_INTERRUPT;
    l->sdp.lcid = CID_SDP;
    l->host_step = 0;
}

/* A link for this address: the existing one, or a free entry. */
static link_t *link_open(const uint8_t *addr)
{
    link_t *l = link_by_addr(addr);
    if (l)
        return l;
    for (int i = 0; i < BT_PADS; i++)
        if (!bt.link[i].used) {
            l = &bt.link[i];
            memset(l, 0, sizeof *l);
            l->used = 1;
            memcpy(l->addr, addr, 6);
            l->slot = key_slot(addr);
            reset_link(l);
            return l;
        }
    return NULL;
}

static void link_close(link_t *l)
{
    reset_link(l);
    l->used = 0;
}

static int slot_connected(int s)
{
    for (int i = 0; i < BT_PADS; i++)
        if (bt.link[i].used && bt.link[i].slot == s && bt.link[i].ctrl.open && bt.link[i].intr.open)
            return 1;
    return 0;
}

/* ---------------------------------------------------------------- chip */

/* The 32.768 kHz sleep clock of the BCM43438, shared by Bluetooth and WiFi. */
const char *bcm43438_lpo_clock(void)
{
    gpio_set_function(LPO_GPIO, GPIO_ALT0);
    if (mmio_read(CM_GP2CTL) & CM_ENAB)
        return "already on";
    mmio_write(CM_GP2CTL, CM_PASSWD | (mmio_read(CM_GP2CTL) & ~CM_ENAB & 0xFFFFFFu));
    uint32_t t0 = timer_ticks();
    while ((mmio_read(CM_GP2CTL) & CM_BUSY) && timer_ticks() - t0 < 10000)
        ;
    /* 19.2 MHz oscillator / 585.9375 = 32768 Hz (MASH 1) */
    mmio_write(CM_GP2DIV, CM_PASSWD | 585u << 12 | 3840u);
    mmio_write(CM_GP2CTL, CM_PASSWD | 1u << 9 | 1u);
    mmio_write(CM_GP2CTL, CM_PASSWD | 1u << 9 | 1u | CM_ENAB);
    return "started";
}

static void power_cycle(void)
{
    gpio_set_function(BT_ON_GPIO, GPIO_OUTPUT);
    gpio_write(BT_ON_GPIO, 0);
    timer_delay_ms(20);
    gpio_write(BT_ON_GPIO, 1);
    timer_delay_ms(250);
}

static int reset(void)
{
    btuart_drain();
    hci_flush();
    return hci_cmd(HCI_RESET, NULL, 0, NULL, 0, 1000000);
}

/* The .hcd file is a list of HCI commands: opcode (2), length (1), data. */
static int load_patch(void)
{
    static const char *const paths[] = { "/bm33/BCM43430A1.hcd", "/BCM43430A1.hcd" };
    fat_entry_t e;
    uint8_t *data = NULL;
    size_t len = 0;
    for (unsigned i = 0; i < 2 && !data; i++)
        if (fat_find(paths[i], &e) == 0)
            fat_load(&e, &data, &len);
    if (!data) {
        kprintf("bt: BCM43430A1.hcd not on the SD card (make firmware; make sdcard puts it\n"
                "    in bm33/): the chip runs its ROM firmware\n");
        return -1;
    }
    if (hci_cmd(HCI_BCM_DOWNLOAD_MINI, NULL, 0, NULL, 0, 1000000) != 0) {
        kprintf("bt: the chip refused the firmware download\n");
        free(data);
        return -1;
    }
    timer_delay_ms(50);
    unsigned records = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        uint16_t op = (uint16_t)(data[i] | data[i + 1] << 8);
        uint8_t n = data[i + 2];
        if (i + 3 + n > len)
            break;
        if (hci_cmd(op, data + i + 3, n, NULL, 0, 1000000) != 0) {
            kprintf("bt: firmware record %u (opcode %04x) failed\n", records, op);
            free(data);
            return -1;
        }
        records++;
        i += 3u + n;
    }
    free(data);
    timer_delay_ms(250);                        /* the chip restarts, at 115200 */
    btuart_set_baud(115200);
    kprintf("bt: firmware patch loaded (%u records, %u bytes)\n", records, (unsigned)len);
    return 0;
}

int bt_start(void)
{
    if (bt.started)
        return 0;
    if (!board()->wireless) {
        /* moving the console UART would take the serial pins away */
        kprintf("bt: no Bluetooth on the %s\n", board()->name);
        return -1;
    }
    kprintf("bt: the serial console moves to the mini UART (same pins, same speed)\n");
    uart_use_mini();
    kprintf("bt: 32 kHz clock %s\n", bcm43438_lpo_clock());
    /* always from a clean state: after a warm reboot the chip keeps its
     * speed, its links and its flow control from before */
    kprintf("bt: power-cycling the chip (GPIO%d)\n", BT_ON_GPIO);
    power_cycle();
    btuart_init(115200);

    int r = reset();
    if (r != 0) {
        kprintf("bt: no answer to HCI reset, power-cycling the chip again (GPIO%d)\n", BT_ON_GPIO);
        power_cycle();
        btuart_init(115200);
        r = reset();
    }
    if (r != 0) {
        kprintf("\x1b[91mbt: the chip does not answer (%d)\x1b[0m\n", r);
        return -1;
    }
    if (load_patch() == 0 && reset() != 0) {
        kprintf("\x1b[91mbt: no answer after the firmware patch\x1b[0m\n");
        return -1;
    }

    uint8_t v[8] = { 0 }, a[6] = { 0 };
    hci_cmd(HCI_READ_LOCAL_VERSION, NULL, 0, v, sizeof v, 500000);
    hci_cmd(HCI_READ_BD_ADDR, NULL, 0, a, sizeof a, 500000);

    /* 115200 baud carries ~550 small packets a second; a DS4 sends more,
     * and the chip queued them (over a second of lag on the Pi). */
    uint8_t baud[6] = { 0, 0 };
    uint32_t fast = BT_FAST_BAUD;
    memcpy(baud + 2, &fast, 4);
    if (hci_cmd(HCI_BCM_UPDATE_BAUD, baud, 6, NULL, 0, 500000) == 0) {
        timer_delay_ms(10);
        btuart_set_baud(fast);
        timer_delay_ms(10);
        if (hci_cmd(HCI_READ_BD_ADDR, NULL, 0, a, sizeof a, 500000) != 0) {
            kprintf("\x1b[91mbt: no answer at %lu baud\x1b[0m\n", fast);
            return -1;
        }
    } else {
        kprintf("bt: the chip stays at 115200 baud (input may lag)\n");
        fast = 115200;
    }

    /* events we handle (SSP ones are not in the default mask), SSP on,
     * our name and class (console), page scan so the pads can come back */
    static const uint8_t mask[8] = { 0xFF, 0xFF, 0xFB, 0xFF, 0x07, 0xF8, 0xBF, 0x3D };
    hci_cmd(HCI_SET_EVENT_MASK, mask, 8, NULL, 0, 500000);
    uint8_t one = 1;
    hci_cmd(HCI_WRITE_SSP_MODE, &one, 1, NULL, 0, 500000);
    static uint8_t name[248];
    memset(name, 0, sizeof name);
    memcpy(name, "bm33", 4);
    hci_cmd(HCI_WRITE_LOCAL_NAME, name, sizeof name, NULL, 0, 500000);
    static const uint8_t cod[3] = { 0x14, 0x09, 0x00 };     /* toy / game */
    hci_cmd(HCI_WRITE_CLASS, cod, 3, NULL, 0, 500000);
    uint8_t scan = 0x02;                                    /* page scan */
    hci_cmd(HCI_WRITE_SCAN_ENABLE, &scan, 1, NULL, 0, 500000);

    char as[18];
    addr_str(as, a);
    kprintf("bt: ready, address %s, HCI %u, LMP subversion %04x, %lu baud\n", as, v[0],
            v[6] | v[7] << 8, fast);
    bt.started = 1;
    for (int i = 0; i < BT_PADS; i++)
        bt.link[i].used = 0;
    load_keys();
    for (int s = 0; s < BT_PADS; s++)
        if (bt.have_key[s]) {
            addr_str(as, bt.key_addr[s]);
            kprintf("bt: paired pad %s (player %d): press its PS button to connect\n", as, s + 1);
        }
    return 0;
}

/* Forgets every paired pad: the links are dropped and the keys removed
 * from bm33/config.txt; each pad must be paired again with T. */
int bt_forget_all(void)
{
    int n = 0;
    for (int i = 0; i < BT_PADS; i++) {
        link_t *l = &bt.link[i];
        if (!l->used)
            continue;
        if (l->connected && bt.started) {
            uint8_t d[3];
            put16(d, l->handle);
            d[2] = 0x13;                        /* remote user terminated */
            hci_send(0x0406, d, 3);             /* Disconnect */
        }
        link_close(l);
    }
    char name[12];
    for (int s = 0; s < BT_PADS; s++) {
        key_name(name, s);
        if (bt.have_key[s] || config_get(name))
            n++;
        bt.have_key[s] = 0;
        hid_bt_clear(s);
        config_unset(name);
    }
    if (config_get("bt_pad"))
        n++;
    config_unset("bt_pad");
    bt.legacy_key = 0;
    config_save();
    return n;
}

int bt_paired(void)
{
    char name[12];
    for (int s = 0; s < BT_PADS; s++) {
        key_name(name, s);
        if (config_get(name))
            return 1;
    }
    return config_get("bt_pad") != NULL;
}

/* ---------------------------------------------------------------- L2CAP */

static void l2cap_send(link_t *l, uint16_t cid, const uint8_t *data, uint16_t len)
{
    static uint8_t buf[96];
    if (len + 4u > sizeof buf)
        return;
    put16(buf, len);
    put16(buf + 2, cid);
    memcpy(buf + 4, data, len);
    hci_acl_send(l->handle, buf, (uint16_t)(len + 4));
}

static void sig_send(link_t *l, uint8_t code, uint8_t id, const uint8_t *data, uint16_t len)
{
    uint8_t buf[64];
    buf[0] = code;
    buf[1] = id;
    put16(buf + 2, len);
    memcpy(buf + 4, data, len);
    l2cap_send(l, CID_SIGNALING, buf, (uint16_t)(len + 4));
}

static void send_config(link_t *l, chan_t *c)
{
    uint8_t p[8];
    put16(p, c->rcid);
    put16(p + 2, 0);                            /* flags */
    p[4] = 0x01;                                /* option: MTU */
    p[5] = 2;
    put16(p + 6, 672);
    sig_send(l, L2_CONF_REQ, ++l->sig_id, p, 8);
}

static void l2cap_connect(link_t *l, chan_t *c, uint16_t psm)
{
    uint8_t p[4];
    put16(p, psm);
    put16(p + 2, c->lcid);
    c->requested = 1;
    sig_send(l, L2_CONN_REQ, ++l->sig_id, p, 4);
}

static chan_t *by_lcid(link_t *l, uint16_t lcid)
{
    return lcid == l->ctrl.lcid ? &l->ctrl : lcid == l->intr.lcid ? &l->intr :
           lcid == l->sdp.lcid ? &l->sdp : NULL;
}

/* The DualShock 4 light in the player colour. Also switches the pad to
 * its full report (0x11), at DS4_POLL_MS. */
static void send_light(link_t *l)
{
    uint8_t p[1 + DS4_OUT_LEN];
    if (l->slot < 0 || !l->intr.open)
        return;
    memset(p, 0, sizeof p);
    p[0] = 0xA2;                                /* DATA | Output */
    uint8_t *r = p + 1;
    r[0] = 0x11;
    r[1] = 0xC0 | DS4_POLL_MS;                  /* HID + CRC, report interval */
    r[3] = 0x07;                                /* rumble, light, flash */
    r[8] = led_rgb[l->slot][0];
    r[9] = led_rgb[l->slot][1];
    r[10] = led_rgb[l->slot][2];
    uint32_t crc = crc32(p, 1 + DS4_OUT_LEN - 4);
    r[74] = (uint8_t)crc;
    r[75] = (uint8_t)(crc >> 8);
    r[76] = (uint8_t)(crc >> 16);
    r[77] = (uint8_t)(crc >> 24);
    l2cap_send(l, l->intr.rcid, p, sizeof p);
}

static void check_open(link_t *l, chan_t *c)
{
    c->open = c->conf_in && c->conf_out;
    if (l->ctrl.open && l->intr.open && !l->announced) {
        char as[18];
        addr_str(as, l->addr);
        if (l->slot < 0)
            l->slot = key_slot(l->addr);
        if (l->slot >= 0)
            kprintf("bt: controller %s connected (player %d)\n", as, l->slot + 1);
        else
            kprintf("bt: controller %s connected (no player: not paired)\n", as);
        l->announced = 1;
        send_light(l);
    }
}

static void handle_signaling(link_t *l, const uint8_t *s, uint16_t len)
{
    while (len >= 4) {
        uint8_t code = s[0], id = s[1];
        uint16_t n = get16(s + 2);
        const uint8_t *p = s + 4;
        if (n + 4u > len)
            return;
        trace(l, "<- L2CAP %s (%02x) %02x %02x %02x %02x %02x %02x",
              code == L2_CONN_REQ ? "connect request" : code == L2_CONN_RSP ? "connect response" :
              code == L2_CONF_REQ ? "config request" : code == L2_CONF_RSP ? "config response" :
              code == L2_DISC_REQ ? "disconnect" : "signal", code,
              n > 0 ? p[0] : 0, n > 1 ? p[1] : 0, n > 2 ? p[2] : 0, n > 3 ? p[3] : 0,
              n > 4 ? p[4] : 0, n > 5 ? p[5] : 0);
        switch (code) {
        case L2_CONN_REQ: {                     /* the pad opens a channel (reconnect) */
            uint16_t psm = get16(p), scid = get16(p + 2);
            chan_t *c = psm == PSM_HID_CONTROL ? &l->ctrl : psm == PSM_HID_INTERRUPT ? &l->intr :
                        psm == PSM_SDP ? &l->sdp : NULL;
            uint8_t r[8];
            put16(r, c ? c->lcid : 0);
            put16(r + 2, scid);
            put16(r + 4, c ? 0 : 2);            /* success / PSM not supported */
            put16(r + 6, 0);
            sig_send(l, L2_CONN_RSP, id, r, 8);
            if (c) {
                c->rcid = scid;
                c->conf_in = c->conf_out = c->open = 0;
                send_config(l, c);
            }
            break;
        }
        case L2_CONN_RSP: {
            uint16_t dcid = get16(p), scid = get16(p + 2), result = get16(p + 4);
            chan_t *c = by_lcid(l, scid);
            if (c && result == 0) {
                c->rcid = dcid;
                send_config(l, c);
            } else if (c && result != 1) {      /* 1 = pending */
                kprintf("bt: channel refused (result %u)\n", result);
                c->requested = 0;
                if (l->host_step == 3)
                    l->host_step = 4;           /* do not insist: the pad decides */
            }
            break;
        }
        case L2_CONF_REQ: {
            chan_t *c = by_lcid(l, get16(p));
            uint8_t r[6];
            put16(r, c ? c->rcid : 0);
            put16(r + 2, 0);
            put16(r + 4, 0);                    /* success: we take their options */
            sig_send(l, L2_CONF_RSP, id, r, 6);
            if (c) {
                c->conf_in = 1;
                check_open(l, c);
            }
            break;
        }
        case L2_CONF_RSP: {
            chan_t *c = by_lcid(l, get16(p));
            if (c && get16(p + 4) == 0) {
                c->conf_out = 1;
                check_open(l, c);
            }
            break;
        }
        case L2_DISC_REQ: {
            uint8_t r[4];
            memcpy(r, p, 4);
            sig_send(l, L2_DISC_RSP, id, r, 4);
            chan_t *c = by_lcid(l, get16(p));
            if (c)
                c->open = c->conf_in = c->conf_out = c->requested = 0;
            break;
        }
        case L2_ECHO_REQ:
            sig_send(l, L2_ECHO_RSP, id, NULL, 0);
            break;
        case L2_INFO_REQ: {
            uint8_t r[4];
            memcpy(r, p, 2);
            put16(r + 2, 1);                    /* not supported */
            sig_send(l, L2_INFO_RSP, id, r, 4);
            break;
        }
        default:
            break;
        }
        s += n + 4u;
        len = (uint16_t)(len - (n + 4u));
    }
}

/* SDP server with no records: some pads ask the host before opening the
 * HID channels, and give up when the channel is refused. Every search gets
 * a valid, empty answer. PDU: id, transaction (BE), parameter length (BE). */
static void sdp_reply(link_t *l, const uint8_t *d, uint16_t len)
{
    if (len < 5 || !l->sdp.open)
        return;
    uint8_t r[12];
    uint16_t n;
    r[1] = d[1];
    r[2] = d[2];
    switch (d[0]) {
    case 0x02:                                  /* Service Search -> none */
        r[0] = 0x03;
        r[5] = 0; r[6] = 0;                     /* total records */
        r[7] = 0; r[8] = 0;                     /* records in this answer */
        r[9] = 0;                               /* no continuation */
        n = 5;
        break;
    case 0x04:                                  /* Service Attribute */
    case 0x06:                                  /* Service Search Attribute */
        r[0] = (uint8_t)(d[0] + 1);
        r[5] = 0; r[6] = 2;                     /* attribute list byte count */
        r[7] = 0x35; r[8] = 0x00;               /* empty data element sequence */
        r[9] = 0;
        n = 5;
        break;
    default:
        r[0] = 0x01;                            /* Error Response */
        r[5] = 0; r[6] = 0x03;                  /* invalid request syntax */
        n = 2;
        break;
    }
    r[3] = (uint8_t)(n >> 8);
    r[4] = (uint8_t)n;
    trace(l, "SDP request %02x answered (no records)", d[0]);
    l2cap_send(l, l->sdp.rcid, r, (uint16_t)(5 + n));
}

static void handle_acl(const hci_pkt_t *p)
{
    if (p->len < 8)
        return;
    link_t *l = link_by_handle(get16(p->data) & 0x0FFF);
    if (!l)
        return;
    uint16_t l2len = get16(p->data + 4), cid = get16(p->data + 6);
    const uint8_t *d = p->data + 8;
    if (l2len + 8u > p->len)
        return;                                 /* fragments are not expected */
    if (cid == CID_SIGNALING) {
        handle_signaling(l, d, l2len);
    } else if (cid == l->sdp.lcid) {
        sdp_reply(l, d, l2len);
    } else if (cid == l->intr.lcid && l2len >= 2 && d[0] == 0xA1 && l->slot >= 0) {
        hid_bt_report(l->slot, d + 1, l2len - 1u);   /* DATA | Input, then report ID */
    }
}

/* ---------------------------------------------------------------- events */

static const char *event_name(uint8_t code)
{
    switch (code) {
    case 0x03: return "connection complete";
    case 0x04: return "connection request";
    case 0x05: return "disconnected";
    case 0x06: return "authentication complete";
    case 0x08: return "encryption change";
    case 0x12: return "role change";
    case 0x16: return "PIN code request";
    case 0x17: return "link key request";
    case 0x18: return "link key (paired)";
    case 0x1B: return "max slots change";
    case 0x20: return "page scan mode change";
    case 0x31: return "IO capability request";
    case 0x32: return "IO capability response";
    case 0x33: return "user confirmation request";
    case 0x36: return "simple pairing complete";
    default: return "event";
    }
}

/* The link an event is about: by handle (bytes 1-2 after the status) or by
 * address (first 6 bytes), depending on the event. */
static link_t *event_link(uint8_t code, const uint8_t *e)
{
    switch (code) {
    case 0x03: return link_by_addr(e + 3);
    case 0x05: case 0x06: case 0x08: return link_by_handle(get16(e + 1) & 0x0FFF);
    case 0x04: case 0x16: case 0x17: case 0x18: case 0x31: case 0x32: case 0x33:
        return link_by_addr(e);
    case 0x36: return link_by_addr(e + 1);
    default: return NULL;
    }
}

static void handle_event(const hci_pkt_t *p)
{
    const uint8_t code = p->data[0];
    const uint8_t *e = p->data + 2;
    uint8_t r[23];
    link_t *l = event_link(code, e);
    trace(l, "<- %s (%02x) %02x %02x %02x %02x", event_name(code), code,
          p->data[1] > 0 ? e[0] : 0, p->data[1] > 1 ? e[1] : 0,
          p->data[1] > 2 ? e[2] : 0, p->data[1] > 3 ? e[3] : 0);
    switch (code) {
    case 0x03:                                  /* Connection Complete */
        if (e[0] == 0) {
            if (!l)
                l = link_open(e + 3);
            if (l) {
                reset_link(l);
                l->connected = 1;
                l->handle = get16(e + 1) & 0x0FFF;
                l->since = timer_ticks();
            }
        } else {
            kprintf("bt: connection failed (status %02x)\n", e[0]);
            if (l && !l->connected)
                link_close(l);
        }
        break;
    case 0x04: {                                /* Connection Request: a pad is back */
        int ours = key_slot(e) >= 0;
        memcpy(r, e, 6);
        if ((ours || nkeys() == 0) && link_open(e)) {
            r[6] = 0x00;                        /* become master */
            hci_send(HCI_ACCEPT_CONNECTION, r, 7);
        } else {
            r[6] = 0x0F;                        /* reject: unacceptable address */
            hci_send(0x040A, r, 7);
        }
        break;
    }
    case 0x05:                                  /* Disconnection Complete */
        if (l) {
            if (l->announced) {
                char as[18];
                addr_str(as, l->addr);
                kprintf("bt: controller %s disconnected (player %d)\n", as, l->slot + 1);
            }
            link_close(l);
            bt.disc_reason = e[3];
        }
        break;
    case 0x06:                                  /* Authentication Complete */
        if (l) {
            l->auth_done = 1;
            l->auth_status = e[0];
        }
        break;
    case 0x08:                                  /* Encryption Change */
        if (l)
            l->enc = e[0] == 0 && e[3];
        break;
    case 0x16:                                  /* PIN Code Request (legacy pads) */
        memcpy(r, e, 6);
        r[6] = 4;
        memset(r + 7, 0, 16);
        memcpy(r + 7, "0000", 4);
        hci_send(HCI_PIN_CODE_REPLY, r, 23);
        break;
    case 0x17: {                                /* Link Key Request */
        int s = key_slot(e);
        memcpy(r, e, 6);
        if (s >= 0) {
            memcpy(r + 6, bt.key[s], 16);
            hci_send(HCI_LINK_KEY_REPLY, r, 22);
        } else {
            hci_send(HCI_LINK_KEY_NEG_REPLY, r, 6);
        }
        break;
    }
    case 0x18: {                                /* Link Key Notification: paired */
        int s = save_key(e, e + 6);
        if (l)
            l->slot = s;
        break;
    }
    case 0x31:                                  /* IO Capability Request */
        memcpy(r, e, 6);
        r[6] = 0x03;                            /* NoInputNoOutput: Just Works */
        r[7] = 0x00;                            /* no OOB data */
        r[8] = 0x04;                            /* general bonding, no MITM */
        hci_send(HCI_IO_CAP_REPLY, r, 9);
        break;
    case 0x33:                                  /* User Confirmation Request */
        memcpy(r, e, 6);
        hci_send(HCI_USER_CONFIRM_REPLY, r, 6);
        break;
    default:
        break;
    }
}

static void dispatch(const hci_pkt_t *p)
{
    if (p->type == HCI_EVENT)
        handle_event(p);
    else if (p->type == HCI_ACL)
        handle_acl(p);
}

/* A paired pad that connected by itself but opened no HID channel: we
 * authenticate, encrypt and open the channels, as when pairing (without
 * waiting: the events arrive through bt_poll). */
static void host_open(link_t *l)
{
    if (!l->connected || l->announced || l->slot < 0 || bt.pairing)
        return;
    if (l->host_step == 0) {
        if (l->ctrl.rcid || l->intr.rcid || timer_ticks() - l->since < HOST_OPEN_MS * 1000u)
            return;                             /* the pad is opening them itself */
        trace(l, "no HID channel from the pad: opening them");
        l->host_step = 1;
    }
    uint8_t h[3];
    put16(h, l->handle);
    switch (l->host_step) {
    case 1:
        if (l->enc) {
            l->host_step = 3;
        } else if (!l->auth_done) {
            hci_send(HCI_AUTH_REQUESTED, h, 2);
            l->host_step = 2;
        } else {
            l->host_step = 2;
        }
        break;
    case 2:
        if (l->auth_done == 1 && l->auth_status == 0 && !l->enc) {
            h[2] = 1;
            hci_send(HCI_SET_CONN_ENCRYPTION, h, 3);
            l->auth_done = 2;                   /* sent: wait for Encryption Change */
        } else if (l->auth_done == 1 && l->auth_status != 0) {
            l->host_step = 4;                   /* give up: the pad decides */
        }
        if (l->enc)
            l->host_step = 3;
        break;
    case 3:
        if (!l->ctrl.requested && !l->ctrl.rcid)
            l2cap_connect(l, &l->ctrl, PSM_HID_CONTROL);
        else if (l->ctrl.open && !l->intr.requested && !l->intr.rcid)
            l2cap_connect(l, &l->intr, PSM_HID_INTERRUPT);
        break;
    default:
        break;
    }
}

void bt_poll(void)
{
    static hci_pkt_t p;
    if (!bt.started)
        return;
    for (int i = 0; i < BT_PADS; i++)
        if (bt.link[i].used)
            host_open(&bt.link[i]);
    /* everything that arrived since the last call: the buttons must be
     * the newest state, not a queue (bounded to 4 ms of work) */
    uint32_t t0 = timer_ticks();
    while (hci_pending() && timer_ticks() - t0 < 4000) {
        if (hci_recv(&p, 20000) != 0)
            break;
        dispatch(&p);
    }
}

/* Runs the stack until *flag is set or timeout_ms passes; 0 if set. -1 also
 * when link l drops (unless the flag is its "connected"). */
static int wait_for(link_t *l, const int *flag, uint32_t timeout_ms)
{
    static hci_pkt_t p;
    uint32_t t0 = timer_ticks();
    while (!*flag) {
        if (timer_ticks() - t0 > timeout_ms * 1000u)
            return -1;
        if (hci_recv(&p, 50000) == 0)
            dispatch(&p);
        if (l && (!l->used || (!l->connected && flag != &l->connected)))
            return -1;                          /* the link dropped */
    }
    return 0;
}

/* ---------------------------------------------------------------- scan, pair */

typedef struct { uint8_t addr[6], psrm; uint16_t clock; uint32_t cod; } found_t;

static const char *major_class(uint32_t cod)
{
    switch ((cod >> 8) & 0x1F) {
    case 1: return "computer";
    case 2: return "phone";
    case 4: return "audio";
    case 5: return (cod & 0xC0) == 0x40 ? "keyboard" : (cod & 0x0C) == 0x08 ? "gamepad" : "input";
    default: return "other";
    }
}

static int is_pad(uint32_t cod)
{
    return ((cod >> 8) & 0x1F) == 5 && (cod & 0x0C) == 0x08;
}

static int inquiry(unsigned seconds, found_t *list, int max)
{
    uint8_t p[5] = { 0x33, 0x8B, 0x9E, (uint8_t)((seconds * 100 + 127) / 128), 0 };  /* GIAC */
    if (hci_cmd(HCI_INQUIRY, p, sizeof p, NULL, 0, 1000000) != 0) {
        kprintf("bt: inquiry refused\n");
        return 0;
    }
    kprintf("bt: looking for devices for %u s (DS4: hold Share + PS until the light flashes)\n",
            seconds);
    static hci_pkt_t ev;
    int n = 0;
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < (seconds + 3) * 1000000u) {
        if (hci_recv(&ev, 500000) != 0)
            continue;
        if (ev.type != HCI_EVENT) {
            dispatch(&ev);
            continue;
        }
        uint8_t code = ev.data[0];
        if (code == 0x01)                       /* Inquiry Complete */
            break;
        if (code != 0x02 && code != 0x22 && code != 0x2F) {
            dispatch(&ev);
            continue;
        }
        /* one response per event in practice: address, page scan
         * repetition mode, reserved, class, clock offset */
        const uint8_t *r = ev.data + 3;
        found_t f;
        memcpy(f.addr, r, 6);
        f.psrm = r[6];
        const uint8_t *c = code == 0x02 ? r + 9 : r + 8;
        f.cod = (uint32_t)(c[0] | c[1] << 8 | c[2] << 16);
        f.clock = get16(c + 3);
        int dup = 0;
        for (int k = 0; k < n; k++)
            dup |= memcmp(list[k].addr, f.addr, 6) == 0;
        if (dup)
            continue;
        if (n < max)
            list[n++] = f;
        char as[18];
        addr_str(as, f.addr);
        kprintf("bt: found %s class %06lx (%s)\n", as, f.cod, major_class(f.cod));
    }
    kprintf("bt: %d device%s found\n", n, n == 1 ? "" : "s");
    return n;
}

static int pair_steps(link_t *l, const found_t *f);

static int pair(const found_t *f)
{
    char as[18];
    addr_str(as, f->addr);
    link_t *l = link_by_addr(f->addr);
    if (l && l->connected) {
        kprintf("bt: %s is already connected\n", as);
        return -1;
    }
    if (!(l = link_open(f->addr))) {
        kprintf("bt: no free link for another controller\n");
        return -1;
    }
    kprintf("bt: pairing with %s...\n", as);
    bt.pairing = 1;
    int r = pair_steps(l, f);
    bt.pairing = 0;
    if (r != 0 && l->used && !l->connected)
        link_close(l);
    return r;
}

/* Why a step failed, in one line. */
static void fail(const link_t *l, const char *what, int cmd_status)
{
    if (cmd_status > 0)
        kprintf("\x1b[91mbt: %s: the chip refused the command (status %02x)\x1b[0m\n",
                what, cmd_status);
    else if (cmd_status < 0)
        kprintf("\x1b[91mbt: %s: no answer from the chip\x1b[0m\n", what);
    else if ((!l->used || !l->connected) && bt.disc_reason >= 0)
        kprintf("\x1b[91mbt: %s: the controller disconnected (reason %02x)\x1b[0m\n",
                what, bt.disc_reason);
    else
        kprintf("\x1b[91mbt: %s: timeout\x1b[0m\n", what);
}

/* Lets the link settle (role switch, features) for `ms`, handling events. */
static void settle(link_t *l, uint32_t ms)
{
    static int never;
    wait_for(l, &never, ms);
}

static int pair_steps(link_t *l, const found_t *f)
{
    uint8_t p[13];
    int st;
    bt.disc_reason = -1;
    memcpy(p, f->addr, 6);
    put16(p + 6, 0xCC18);                       /* DM1..DH5 */
    p[8] = f->psrm;
    p[9] = 0;
    put16(p + 10, f->clock | 0x8000);
    p[12] = 1;                                  /* allow role switch */
    if ((st = hci_cmd(HCI_CREATE_CONNECTION, p, 13, NULL, 0, 1000000)) != 0 ||
        wait_for(l, &l->connected, 10000) != 0) {
        fail(l, "cannot connect (is it still flashing?)", st);
        return -1;
    }
    settle(l, 300);
    uint8_t h[3];
    put16(h, l->handle);
    st = hci_cmd(HCI_AUTH_REQUESTED, h, 2, NULL, 0, 1000000);
    if (st > 0 && l->connected) {               /* busy with the link: once more */
        trace(l, "authentication request refused (%02x), retrying", st);
        settle(l, 700);
        st = hci_cmd(HCI_AUTH_REQUESTED, h, 2, NULL, 0, 1000000);
    }
    if (st != 0 || wait_for(l, &l->auth_done, 20000) != 0) {
        fail(l, "pairing failed", st);
        return -1;
    }
    if (l->auth_status != 0) {
        kprintf("\x1b[91mbt: pairing failed: authentication status %02x\x1b[0m\n",
                l->auth_status);
        return -1;
    }
    h[2] = 1;
    if ((st = hci_cmd(HCI_SET_CONN_ENCRYPTION, h, 3, NULL, 0, 1000000)) != 0 ||
        wait_for(l, &l->enc, 10000) != 0) {
        fail(l, "encryption failed", st);
        return -1;
    }
    l2cap_connect(l, &l->ctrl, PSM_HID_CONTROL);
    if (wait_for(l, &l->ctrl.open, 10000) != 0) {
        fail(l, "HID control channel not opened", 0);
        return -1;
    }
    l2cap_connect(l, &l->intr, PSM_HID_INTERRUPT);
    if (wait_for(l, &l->intr.open, 10000) != 0) {
        fail(l, "HID interrupt channel not opened", 0);
        return -1;
    }
    kprintf("bt: paired as player %d; next time just press PS on the controller\n", l->slot + 1);
    return 0;
}

void bt_scan(unsigned seconds)
{
    if (!bt.started && bt_start() != 0)
        return;
    int on = 0;
    for (int s = 0; s < BT_PADS; s++)
        on += slot_connected(s);
    if (on >= BT_PADS) {
        kprintf("bt: %d controllers are connected already\n", BT_PADS);
        return;
    }
    found_t list[8];
    int n = inquiry(seconds, list, 8);
    for (int i = 0; i < n; i++)
        if (is_pad(list[i].cod)) {
            pair(&list[i]);
            return;
        }
    if (n)
        kprintf("bt: no game controller among them\n");
}

int bt_connected(void)
{
    for (int s = 0; s < BT_PADS; s++)
        if (slot_connected(s))
            return 1;
    return 0;
}

unsigned bt_pads(void)
{
    unsigned m = 0;
    for (int s = 0; s < BT_PADS; s++)
        if (slot_connected(s))
            m |= 1u << s;
    return m;
}

int bt_pad_addr(int slot, char out[18])
{
    for (int i = 0; i < BT_PADS; i++)
        if (bt.link[i].used && bt.link[i].slot == slot) {
            addr_str(out, bt.link[i].addr);
            return 1;
        }
    if (slot >= 0 && slot < BT_PADS && bt.have_key[slot]) {
        addr_str(out, bt.key_addr[slot]);
        return 0;
    }
    out[0] = 0;
    return 0;
}
