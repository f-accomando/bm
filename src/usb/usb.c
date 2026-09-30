#include "usb.h"
#include "dwc2.h"
#include "hid.h"
#include "smsc95xx.h"
#include "arch/cache.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

#define CH_CONTROL  0
#define CH_INTR     1
#define CH_BULK     2

#define DESC_DEVICE     1
#define DESC_CONFIG     2
#define DESC_STRING     3
#define DESC_INTERFACE  4
#define DESC_ENDPOINT   5
#define DESC_HUB        0x29
#define DESC_HID_REPORT 0x22

#define CLASS_HUB       9
#define MAX_DEVS        8

/* hub ports: GET_STATUS bits (status, then changes in the high half) */
#define PORT_CONNECTION (1u << 0)
#define PORT_ENABLE     (1u << 1)
#define PORT_RESET      (1u << 4)
#define PORT_LOW_SPEED  (1u << 9)
#define PORT_HIGH_SPEED (1u << 10)
#define C_PORT_RESET    (1u << 20)
#define FEAT_PORT_RESET        4
#define FEAT_PORT_POWER        8
#define FEAT_C_PORT_CONNECTION 16
#define FEAT_C_PORT_RESET      20

static usb_dev_t devs[MAX_DEVS];
static int ndevs;
static usb_dev_t *hub;              /* the hub on the root port, if any */
static int hub_ports;
static usb_dev_t *hid_dev;          /* the HID device in use */
static usb_info_t info;
static int ds4;

/* interrupt IN endpoint of the HID interface */
static struct {
    uint8_t ep, iface, interval;
    uint16_t mps;
    uint8_t toggle;
    uint32_t last_us;
    int active;
} hid_ep;

static struct { uint32_t ok, nak, err, tmo; uint8_t last[8]; uint32_t last_len; } st;

static uint8_t dma[512] __attribute__((aligned(CACHE_LINE)));
static uint8_t report[64] __attribute__((aligned(CACHE_LINE)));

int usb_control(usb_dev_t *d, uint8_t req_type, uint8_t req, uint16_t value, uint16_t index,
                void *data, uint16_t len)
{
    static uint8_t setup[8] __attribute__((aligned(CACHE_LINE)));
    uint8_t toggle;
    dwc2_pipe_t p = { d->addr, 0, 0, EP_CONTROL, d->mps0, d->speed, &toggle,
                      d->hub_addr, d->hub_port };
    if (len > sizeof dma)
        return XFER_ERROR;
    setup[0] = req_type; setup[1] = req;
    setup[2] = (uint8_t)value; setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index; setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)len; setup[7] = (uint8_t)(len >> 8);

    int r = dwc2_transfer(CH_CONTROL, &p, PID_SETUP, setup, 8, NULL, 500);
    if (r != XFER_OK)
        return r;
    int in = req_type & 0x80;
    uint32_t got = 0;
    if (len) {
        p.in = in ? 1 : 0;
        if (!in)
            memcpy(dma, data, len);
        r = dwc2_transfer(CH_CONTROL, &p, PID_DATA1, dma, len, &got, 500);
        if (r != XFER_OK)
            return r;
        if (in)
            memcpy(data, dma, got);
    }
    p.in = (len && in) ? 0 : 1;                 /* status stage: opposite direction */
    r = dwc2_transfer(CH_CONTROL, &p, PID_DATA1, dma, 0, NULL, 500);
    return r == XFER_OK ? (int)got : r;
}

int usb_bulk(usb_dev_t *d, uint8_t ep, int in, uint16_t mps, uint8_t *toggle,
             void *buf, uint32_t len, uint32_t *actual, uint32_t timeout_ms)
{
    dwc2_pipe_t p = { d->addr, ep, in ? 1 : 0, EP_BULK, mps, d->speed, toggle,
                      d->hub_addr, d->hub_port };
    return dwc2_transfer(CH_BULK, &p, *toggle, buf, len, actual, timeout_ms);
}

static int get_descriptor(usb_dev_t *d, uint8_t type, uint8_t index, uint16_t lang,
                          void *buf, uint16_t len)
{
    return usb_control(d, 0x80, 6, (uint16_t)(type << 8 | index), lang, buf, len);
}

static void read_string(usb_dev_t *d, uint8_t index, char *out, int max)
{
    uint8_t s[64];
    out[0] = '\0';
    if (!index || get_descriptor(d, DESC_STRING, index, 0x0409, s, sizeof s) < 2)
        return;
    int n = 0;
    for (int i = 2; i + 1 < s[0] && i < (int)sizeof s && n < max - 1; i += 2)
        out[n++] = (s[i] >= 32 && s[i] < 127 && !s[i + 1]) ? (char)s[i] : '?';
    out[n] = '\0';
}

const usb_info_t *usb_info(void)
{
    return &info;
}

static const char *const speeds[] = { "high", "full", "low" };

void usb_print(void)
{
    static const char *const kinds[] = { "nothing attached", "keyboard", "gamepad",
                                         "Xbox 360 controller", "device (not used)" };
    if (hub)
        kprintf("usb: hub %04x:%04x, %d ports, %s speed\n", hub->vid, hub->pid, hub_ports,
                hub->speed < 3 ? speeds[hub->speed] : "?");
    if (eth_present())
        eth_print();
    if (info.kind == USB_NONE) {
        kprintf(hub ? "usb: no keyboard or gamepad on the hub\n" : "usb: nothing attached\n");
        return;
    }
    kprintf("usb: %s %04x:%04x '%s', %s speed", kinds[info.kind], info.vid, info.pid,
            info.name[0] ? info.name : "?", info.speed < 3 ? speeds[info.speed] : "?");
    if (info.kind == USB_KEYBOARD)
        kprintf(", layout %s", hid_layout());
    if (ds4)
        kprintf(" (DualShock 4)");
    if (hid_dev && hid_dev->port)
        kprintf(" (hub port %u%s)", hid_dev->port, hid_dev->hub_addr ? ", split" : "");
    kprintf("\n");
}

/* A device at address 0 (its port just reset) gets the next address, its
 * descriptors are read and its first configuration selected. cfg receives
 * the configuration descriptor (*cfg_len bytes, 0 if unreadable). */
static usb_dev_t *enumerate(uint8_t speed, const usb_dev_t *parent, uint8_t port,
                            uint8_t *cfg, uint16_t cfg_size, uint16_t *cfg_len)
{
    char where[16] = "";
    if (port)
        ksnprintf(where, sizeof where, "port %u: ", port);
    *cfg_len = 0;
    if (ndevs >= MAX_DEVS) {
        kprintf("usb: %stoo many devices\n", where);
        return NULL;
    }
    usb_dev_t *d = &devs[ndevs];
    memset(d, 0, sizeof *d);
    d->speed = speed;
    d->port = port;
    if (parent) {
        if (parent->speed == USB_SPEED_HIGH && speed != USB_SPEED_HIGH) {
            d->hub_addr = parent->addr;         /* the hub translates: split */
            d->hub_port = port;
        } else {
            d->hub_addr = parent->hub_addr;
            d->hub_port = parent->hub_port;
        }
    }
    d->mps0 = speed == USB_SPEED_LOW ? 8 : 64;

    uint8_t dd[18];
    int r = get_descriptor(d, DESC_DEVICE, 0, 0, dd, 8);
    if (r < 8) {
        d->mps0 = 8;                            /* retry with the smallest size */
        r = get_descriptor(d, DESC_DEVICE, 0, 0, dd, 8);
        if (r < 8) {
            kprintf("usb: %sno answer from the device (%d)\n", where, r);
            return NULL;
        }
    }
    d->mps0 = dd[7] ? dd[7] : 8;
    uint8_t addr = (uint8_t)(ndevs + 1);
    if (usb_control(d, 0x00, 5, addr, 0, NULL, 0) < 0) {  /* SET_ADDRESS */
        kprintf("usb: %sSET_ADDRESS failed\n", where);
        return NULL;
    }
    timer_delay_ms(10);
    d->addr = addr;
    if (get_descriptor(d, DESC_DEVICE, 0, 0, dd, 18) < 18) {
        kprintf("usb: %sdevice descriptor failed\n", where);
        return NULL;
    }
    ndevs++;
    d->cls = dd[4];
    d->vid = (uint16_t)(dd[8] | dd[9] << 8);
    d->pid = (uint16_t)(dd[10] | dd[11] << 8);
    read_string(d, dd[15], d->name, sizeof d->name);
    if (!d->name[0])
        read_string(d, dd[14], d->name, sizeof d->name);

    if (get_descriptor(d, DESC_CONFIG, 0, 0, cfg, 9) < 9)
        return d;
    uint16_t total = (uint16_t)(cfg[2] | cfg[3] << 8);
    if (total > cfg_size) total = cfg_size;
    if (get_descriptor(d, DESC_CONFIG, 0, 0, cfg, total) < total)
        return d;
    if (usb_control(d, 0x00, 9, cfg[5], 0, NULL, 0) < 0) {  /* SET_CONFIGURATION */
        kprintf("usb: %sSET_CONFIGURATION failed\n", where);
        return d;
    }
    *cfg_len = total;
    return d;
}

/* The best HID interface of a device. score: 4 keyboard, 3 Xbox 360 pad,
 * 2 gamepad, 0 nothing usable. */
typedef struct {
    int score;
    uint8_t iface, sub, ep, interval, kbd_id;
    uint16_t mps, rlen;
} hid_pick_t;

static uint8_t rd[512];

static void hid_probe(usb_dev_t *d, const uint8_t *c, uint16_t total, hid_pick_t *best)
{
    /* every interface with an interrupt IN endpoint is a candidate */
    struct cand { uint8_t iface, cls, sub, proto, ep, interval; uint16_t mps, rlen; } cand[8];
    int nc = 0, have_ep = 0;
    char where[16] = "";
    if (d->port)
        ksnprintf(where, sizeof where, "port %u: ", d->port);
    memset(best, 0, sizeof *best);
    for (int i = 0; i + 1 < total && c[i]; i += c[i]) {
        if (c[i + 1] == DESC_INTERFACE && i + 8 < total && nc < 8) {
            cand[nc] = (struct cand){ c[i + 2], c[i + 5], c[i + 6], c[i + 7], 0, 0, 0, 0 };
            have_ep = 0;
            nc++;
        } else if (c[i + 1] == 0x21 && i + 8 < total && nc) {
            cand[nc - 1].rlen = (uint16_t)(c[i + 7] | c[i + 8] << 8);
        } else if (c[i + 1] == DESC_ENDPOINT && i + 6 < total && nc && !have_ep &&
                   (c[i + 2] & 0x80) && (c[i + 3] & 3) == EP_INTERRUPT) {
            cand[nc - 1].ep = c[i + 2] & 0x0F;
            cand[nc - 1].mps = (uint16_t)((c[i + 4] | c[i + 5] << 8) & 0x7FF);
            cand[nc - 1].interval = c[i + 6] ? c[i + 6] : 10;
            have_ep = 1;
        }
    }

    /* Pick one: a keyboard (boot interface, or a HID report descriptor with a
     * keyboard collection), else an Xbox 360 pad, else a HID gamepad. */
    for (int n = 0; n < nc; n++) {
        struct cand *k = &cand[n];
        int score = 0, rl = -1;
        uint8_t id = 0;
        if (!k->ep)
            continue;
        if (k->cls == 3 && k->rlen) {
            uint16_t want = k->rlen > sizeof rd ? sizeof rd : k->rlen;
            rl = usb_control(d, 0x81, 6, DESC_HID_REPORT << 8, k->iface, rd, want);
        }
        int kbd_desc = k->cls == 3 && rl > 0 && hid_is_keyboard(rd, (uint32_t)rl, &id);
        if (k->cls == 3 && k->sub == 1 && k->proto == 2) {
            score = 0;                                  /* boot mouse: not used */
        } else if (k->cls == 3 && ((k->sub == 1 && k->proto == 1) || kbd_desc)) {
            score = 4;
        } else if (k->cls == 0xFF && k->sub == 0x5D && k->proto == 0x01) {
            score = 3;
        } else if (k->cls == 3 && d->vid == 0x054C) {
            score = 2;                                  /* Sony pad: fixed report layout */
        } else if (k->cls == 3 && rl > 0 && hid_gamepad_attach(rd, (uint32_t)rl) == 0) {
            score = 2;
        }
        kprintf("usb: %sif%u class %02x/%02x/%02x ep %02x mps %u every %u%s, report desc %d%s\n",
                where, k->iface, k->cls, k->sub, k->proto, k->ep | 0x80,
                k->mps, k->interval, d->speed == USB_SPEED_HIGH ? "uf" : "ms", rl,
                score == 4 ? (id ? " keyboard (report id)" : " keyboard") : score == 3 ? " xbox" :
                score == 2 ? " gamepad" : "");
        if (score > best->score) {
            best->score = score;
            best->iface = k->iface;
            best->sub = k->sub;
            best->ep = k->ep;
            best->interval = k->interval;
            best->mps = k->mps;
            best->rlen = k->rlen;
            best->kbd_id = id;
        }
    }
}

/* Sets up the chosen interface; the kind of device it is. */
static int hid_attach(usb_dev_t *d, const hid_pick_t *k)
{
    int kind;
    hid_ep.ep = k->ep;
    hid_ep.mps = k->mps;
    hid_ep.interval = k->interval;
    hid_ep.iface = k->iface;
    hid_ep.active = 1;
    if (k->score == 4) {
        kind = USB_KEYBOARD;
        if (k->sub == 1)
            usb_control(d, 0x21, 0x0B, 0, k->iface, NULL, 0);  /* SET_PROTOCOL boot */
        usb_control(d, 0x21, 0x0A, 0, k->iface, NULL, 0);      /* SET_IDLE 0 */
        hid_keyboard_attach(k->kbd_id);
    } else if (k->score == 3) {
        kind = USB_XBOX360;
        hid_xbox360_attach();
    } else if (d->vid == 0x054C && (d->pid == 0x05C4 || d->pid == 0x09CC ||
                                    d->pid == 0x0BA0)) {
        kind = USB_GAMEPAD;                 /* DualShock 4 (or its USB dongle) */
        ds4 = 1;
        usb_control(d, 0x21, 0x0A, 0, k->iface, NULL, 0);
        hid_ds4_attach();
    } else {
        kind = USB_GAMEPAD;
        int rl = k->rlen ? usb_control(d, 0x81, 6, DESC_HID_REPORT << 8, k->iface, rd,
                                       k->rlen > sizeof rd ? sizeof rd : k->rlen) : -1;
        usb_control(d, 0x21, 0x0A, 0, k->iface, NULL, 0);
        if (rl <= 0 || hid_gamepad_attach(rd, (uint32_t)rl) != 0)
            kind = USB_OTHER;
    }
    if (kind == USB_OTHER)
        hid_ep.active = 0;
    return kind;
}

/* What a device is for: the Ethernet of the LAN951x, else a HID candidate
 * (the best one so far is kept in *best / *best_dev). */
static void probe(usb_dev_t *d, const uint8_t *cfg, uint16_t cfg_len,
                  hid_pick_t *best, usb_dev_t **best_dev, usb_dev_t **other)
{
    if (eth_match(d->vid, d->pid)) {
        eth_attach(d, cfg, cfg_len);
        return;
    }
    if (d->cls == CLASS_HUB) {
        kprintf("usb: port %u: a hub behind the hub, not used\n", d->port);
        return;
    }
    if (!*other)
        *other = d;
    if (!cfg_len)
        return;
    hid_pick_t k;
    hid_probe(d, cfg, cfg_len, &k);
    if (k.score > best->score) {
        *best = k;
        *best_dev = d;
    }
}

static int port_status(usb_dev_t *h, uint8_t port, uint32_t *s)
{
    uint8_t b[4];
    if (usb_control(h, 0xA3, 0, 0, port, b, 4) < 4)     /* GET_STATUS (port) */
        return -1;
    *s = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

/* Resets a hub port; 0 once the port is enabled (*s: its status). */
static int port_reset(usb_dev_t *h, uint8_t port, uint32_t *s)
{
    usb_control(h, 0x23, 3, FEAT_PORT_RESET, port, NULL, 0);    /* SET_FEATURE */
    for (int i = 0; i < 50; i++) {
        timer_delay_ms(10);
        if (port_status(h, port, s) != 0 || (*s & PORT_RESET))
            continue;
        if (*s & (C_PORT_RESET | PORT_ENABLE)) {
            usb_control(h, 0x23, 1, FEAT_C_PORT_RESET, port, NULL, 0);  /* CLEAR_FEATURE */
            timer_delay_ms(10);                 /* reset recovery */
            return (*s & PORT_ENABLE) ? 0 : -1;
        }
    }
    return -1;
}

/* Powers every port of the hub on the root port, then enumerates what is
 * attached, one port at a time (each device starts at address 0). */
static void hub_scan(usb_dev_t *h, uint8_t *cfg, uint16_t cfg_size,
                     hid_pick_t *best, usb_dev_t **best_dev, usb_dev_t **other)
{
    uint8_t hd[16];
    int r = usb_control(h, 0xA0, 6, DESC_HUB << 8, 0, hd, sizeof hd);
    if (r < 7) {
        kprintf("usb: hub descriptor failed (%d)\n", r);
        return;
    }
    hub_ports = hd[2] > 15 ? 15 : hd[2];
    for (int p = 1; p <= hub_ports; p++)
        usb_control(h, 0x23, 3, FEAT_PORT_POWER, (uint16_t)p, NULL, 0);
    timer_delay_ms(hd[5] * 2u + 100);           /* power good, then connect debounce */

    for (int p = 1; p <= hub_ports; p++) {
        uint32_t s;
        if (port_status(h, (uint8_t)p, &s) != 0 || !(s & PORT_CONNECTION))
            continue;
        usb_control(h, 0x23, 1, FEAT_C_PORT_CONNECTION, (uint16_t)p, NULL, 0);
        if (port_reset(h, (uint8_t)p, &s) != 0) {
            kprintf("usb: port %d: reset failed (status %08lx)\n", p, s);
            continue;
        }
        uint8_t sp = (s & PORT_LOW_SPEED) ? USB_SPEED_LOW :
                     (s & PORT_HIGH_SPEED) ? USB_SPEED_HIGH : USB_SPEED_FULL;
        uint16_t len;
        usb_dev_t *d = enumerate(sp, h, (uint8_t)p, cfg, cfg_size, &len);
        if (d)
            probe(d, cfg, len, best, best_dev, other);
    }
}

int usb_init(void)
{
    static uint8_t cfg[256];
    memset(&info, 0, sizeof info);
    ds4 = 0;
    memset(&hid_ep, 0, sizeof hid_ep);
    memset(&st, 0, sizeof st);
    ndevs = 0;
    hub = NULL;
    hub_ports = 0;
    hid_dev = NULL;
    eth_detach();
    if (dwc2_init() != 0)
        return USB_NONE;
    if (!dwc2_port_connected()) {
        timer_delay_ms(300);                    /* some devices are slow to show up */
        if (!dwc2_port_connected())
            return USB_NONE;
    }
    enum usb_speed sp;
    if (dwc2_port_reset(&sp) != 0) {
        kprintf("usb: port reset failed\n");
        return USB_NONE;
    }
    uint16_t len;
    usb_dev_t *root = enumerate((uint8_t)sp, NULL, 0, cfg, sizeof cfg, &len);
    if (!root)
        return USB_NONE;

    hid_pick_t best = { 0 };
    usb_dev_t *best_dev = NULL, *other = NULL;
    if (root->cls == CLASS_HUB) {
        hub = root;
        hub_scan(root, cfg, sizeof cfg, &best, &best_dev, &other);
    } else {
        probe(root, cfg, len, &best, &best_dev, &other);
    }

    int kind = USB_NONE;
    usb_dev_t *d = best_dev ? best_dev : other;
    if (best_dev) {
        hid_dev = best_dev;
        kind = hid_attach(best_dev, &best);
    } else if (other) {
        kind = USB_OTHER;
    }
    if (d) {
        info.vid = d->vid;
        info.pid = d->pid;
        info.speed = d->speed;
        memcpy(info.name, d->name, sizeof info.name);
    }
    if (hid_ep.mps > sizeof report)
        hid_ep.mps = sizeof report;
    hid_ep.toggle = PID_DATA0;
    info.kind = kind;
    return kind;
}

void usb_poll(void)
{
    if (!hid_ep.active || !hid_dev)
        return;
    uint32_t now = timer_ticks();
    /* bInterval: milliseconds (low/full speed) or 2^(n-1) x 125 us (high speed) */
    uint32_t iv = hid_ep.interval ? hid_ep.interval : 1;
    uint32_t period = hid_dev->speed == USB_SPEED_HIGH
        ? (1u << ((iv > 16 ? 16 : iv) - 1)) * 125u
        : iv * 1000u;
    if (period < 4000) period = 4000;       /* polled from the main loop anyway */
    if (now - hid_ep.last_us < period)
        return;
    hid_ep.last_us = now;

    dwc2_pipe_t p = { hid_dev->addr, hid_ep.ep, 1, EP_INTERRUPT, hid_ep.mps, hid_dev->speed,
                      &hid_ep.toggle, hid_dev->hub_addr, hid_dev->hub_port };
    uint32_t got = 0;
    int r = dwc2_transfer(CH_INTR, &p, hid_ep.toggle, report, hid_ep.mps, &got, 20);
    if (r == XFER_OK) {
        st.ok++;
        if (got) {
            st.last_len = got;
            memcpy(st.last, report, got < 8 ? got : 8);
            hid_report(info.kind, report, got);
        }
    } else if (r == XFER_NAK) {
        st.nak++;
    } else {
        if (r == XFER_TIMEOUT) st.tmo++; else st.err++;
        hid_ep.toggle = PID_DATA0;
    }
}

void usb_diag(char *buf, unsigned size)
{
    int n = ksnprintf(buf, size, "ok %lu nak %lu err %lu tmo %lu, last %lu:",
                      st.ok, st.nak, st.err, st.tmo, st.last_len);
    for (uint32_t i = 0; i < 8 && i < st.last_len && n + 4 < (int)size; i++)
        n += ksnprintf(buf + n, size - (unsigned)n, " %02x", st.last[i]);
}

void usb_live_test(uint32_t seconds)
{
    char line[96];
    if (!hid_ep.active)
        return;
    kprintf("usb test for %lu s: press some keys / buttons\n", seconds);
    uint32_t t0 = timer_ticks(), shown = 0;
    while (timer_ticks() - t0 < seconds * 1000000u) {
        usb_poll();
        if (timer_ticks() - shown >= 200000) {
            shown = timer_ticks();
            usb_diag(line, sizeof line);
            kprintf("\r  %s\x1b[K", line);
        }
    }
    kprintf("\n");
    while (hid_getc() >= 0)                     /* keys pressed here are not commands */
        ;
    hid_quit_pressed();
}
