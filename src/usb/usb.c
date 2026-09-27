#include "usb.h"
#include "dwc2.h"
#include "hid.h"
#include "arch/cache.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <string.h>

#define CH_CONTROL  0
#define CH_INTR     1
#define ADDR        1

#define DESC_DEVICE     1
#define DESC_CONFIG     2
#define DESC_STRING     3
#define DESC_INTERFACE  4
#define DESC_ENDPOINT   5
#define DESC_HID_REPORT 0x22

static usb_info_t info;
static int ds4;
static uint8_t dev_speed;
static uint16_t mps0 = 8;
static uint8_t addr;

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

static int control(uint8_t req_type, uint8_t req, uint16_t value, uint16_t index,
                   void *data, uint16_t len)
{
    static uint8_t setup[8] __attribute__((aligned(CACHE_LINE)));
    uint8_t toggle;
    dwc2_pipe_t p = { addr, 0, 0, EP_CONTROL, mps0, dev_speed, &toggle };
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

static int get_descriptor(uint8_t type, uint8_t index, uint16_t lang, void *buf, uint16_t len)
{
    return control(0x80, 6, (uint16_t)(type << 8 | index), lang, buf, len);
}

static void read_string(uint8_t index, char *out, int max)
{
    uint8_t s[64];
    out[0] = '\0';
    if (!index || get_descriptor(DESC_STRING, index, 0x0409, s, sizeof s) < 2)
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

void usb_print(void)
{
    static const char *const kinds[] = { "nothing attached", "keyboard", "gamepad",
                                         "Xbox 360 controller", "device (not used)" };
    static const char *const speeds[] = { "high", "full", "low" };
    if (info.kind == USB_NONE) {
        kprintf("usb: nothing attached\n");
        return;
    }
    kprintf("usb: %s %04x:%04x '%s', %s speed", kinds[info.kind], info.vid, info.pid,
            info.name[0] ? info.name : "?", info.speed < 3 ? speeds[info.speed] : "?");
    if (info.kind == USB_KEYBOARD)
        kprintf(", layout %s", hid_layout());
    if (ds4)
        kprintf(" (DualShock 4)");
    kprintf("\n");
}

int usb_init(void)
{
    memset(&info, 0, sizeof info);
    ds4 = 0;
    memset(&hid_ep, 0, sizeof hid_ep);
    memset(&st, 0, sizeof st);
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
    dev_speed = (uint8_t)sp;
    info.speed = dev_speed;
    addr = 0;
    mps0 = dev_speed == USB_SPEED_LOW ? 8 : 64;

    uint8_t d[18];
    int r = get_descriptor(DESC_DEVICE, 0, 0, d, 8);
    if (r < 8) {
        mps0 = 8;                               /* retry with the smallest size */
        r = get_descriptor(DESC_DEVICE, 0, 0, d, 8);
        if (r < 8) {
            kprintf("usb: no answer from the device (%d)\n", r);
            return USB_NONE;
        }
    }
    mps0 = d[7] ? d[7] : 8;
    if (control(0x00, 5, ADDR, 0, NULL, 0) < 0) {  /* SET_ADDRESS */
        kprintf("usb: SET_ADDRESS failed\n");
        return USB_NONE;
    }
    timer_delay_ms(10);
    addr = ADDR;
    if (get_descriptor(DESC_DEVICE, 0, 0, d, 18) < 18) {
        kprintf("usb: device descriptor failed\n");
        return USB_NONE;
    }
    info.vid = (uint16_t)(d[8] | d[9] << 8);
    info.pid = (uint16_t)(d[10] | d[11] << 8);
    uint8_t dev_class = d[4];
    read_string(d[15], info.name, sizeof info.name);
    if (!info.name[0])
        read_string(d[14], info.name, sizeof info.name);

    uint8_t c[256];
    if (get_descriptor(DESC_CONFIG, 0, 0, c, 9) < 9)
        return USB_OTHER;
    uint16_t total = (uint16_t)(c[2] | c[3] << 8);
    if (total > sizeof c) total = sizeof c;
    if (get_descriptor(DESC_CONFIG, 0, 0, c, total) < total)
        return USB_OTHER;
    uint8_t config_value = c[5];

    /* every interface with an interrupt IN endpoint is a candidate */
    struct cand { uint8_t iface, cls, sub, proto, ep, interval; uint16_t mps, rlen; } cand[8];
    int nc = 0, have_ep = 0;
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
    if (control(0x00, 9, config_value, 0, NULL, 0) < 0) {  /* SET_CONFIGURATION */
        kprintf("usb: SET_CONFIGURATION failed\n");
        return USB_OTHER;
    }

    /* Pick one: a keyboard (boot interface, or a HID report descriptor with a
     * keyboard collection), else an Xbox 360 pad, else a HID gamepad. */
    static uint8_t rd[512];
    int kind = USB_OTHER, best = -1, best_score = 0;
    uint8_t kbd_id = 0;
    for (int n = 0; n < nc; n++) {
        struct cand *k = &cand[n];
        int score = 0, rl = -1;
        uint8_t id = 0;
        if (!k->ep)
            continue;
        if (k->cls == 3 && k->rlen) {
            uint16_t want = k->rlen > sizeof rd ? sizeof rd : k->rlen;
            rl = control(0x81, 6, DESC_HID_REPORT << 8, k->iface, rd, want);
        }
        int kbd_desc = k->cls == 3 && rl > 0 && hid_is_keyboard(rd, (uint32_t)rl, &id);
        if (k->cls == 3 && k->sub == 1 && k->proto == 2) {
            score = 0;                                  /* boot mouse: not used */
        } else if (k->cls == 3 && ((k->sub == 1 && k->proto == 1) || kbd_desc)) {
            score = 4;
        } else if (k->cls == 0xFF && k->sub == 0x5D && k->proto == 0x01) {
            score = 3;
        } else if (k->cls == 3 && info.vid == 0x054C) {
            score = 2;                                  /* Sony pad: fixed report layout */
        } else if (k->cls == 3 && rl > 0 && hid_gamepad_attach(rd, (uint32_t)rl) == 0) {
            score = 2;
        }
        kprintf("usb: if%u class %02x/%02x/%02x ep %02x mps %u every %u%s, report desc %d%s\n",
                k->iface, k->cls, k->sub, k->proto, k->ep | 0x80, k->mps, k->interval,
                dev_speed == USB_SPEED_HIGH ? "uf" : "ms", rl,
                score == 4 ? (id ? " keyboard (report id)" : " keyboard") : score == 3 ? " xbox" :
                score == 2 ? " gamepad" : "");
        if (score > best_score) {
            best_score = score;
            best = n;
            kbd_id = id;
        }
    }
    if (best >= 0) {
        struct cand *k = &cand[best];
        hid_ep.ep = k->ep;
        hid_ep.mps = k->mps;
        hid_ep.interval = k->interval;
        hid_ep.iface = k->iface;
        hid_ep.active = 1;
        if (best_score == 4) {
            kind = USB_KEYBOARD;
            if (k->sub == 1)
                control(0x21, 0x0B, 0, k->iface, NULL, 0);  /* SET_PROTOCOL boot */
            control(0x21, 0x0A, 0, k->iface, NULL, 0);      /* SET_IDLE 0 */
            hid_keyboard_attach(kbd_id);
        } else if (best_score == 3) {
            kind = USB_XBOX360;
            hid_xbox360_attach();
        } else if (info.vid == 0x054C && (info.pid == 0x05C4 || info.pid == 0x09CC ||
                                          info.pid == 0x0BA0)) {
            kind = USB_GAMEPAD;                 /* DualShock 4 (or its USB dongle) */
            ds4 = 1;
            control(0x21, 0x0A, 0, k->iface, NULL, 0);
            hid_ds4_attach();
        } else {
            kind = USB_GAMEPAD;
            int rl = k->rlen ? control(0x81, 6, DESC_HID_REPORT << 8, k->iface, rd,
                                       k->rlen > sizeof rd ? sizeof rd : k->rlen) : -1;
            control(0x21, 0x0A, 0, k->iface, NULL, 0);
            if (rl <= 0 || hid_gamepad_attach(rd, (uint32_t)rl) != 0)
                kind = USB_OTHER;
        }
    }
    (void)dev_class;
    if (kind == USB_OTHER)
        hid_ep.active = 0;
    if (hid_ep.mps > sizeof report)
        hid_ep.mps = sizeof report;
    hid_ep.toggle = PID_DATA0;
    info.kind = kind;
    return kind;
}

void usb_poll(void)
{
    if (!hid_ep.active)
        return;
    uint32_t now = timer_ticks();
    /* bInterval: milliseconds (low/full speed) or 2^(n-1) x 125 us (high speed) */
    uint32_t iv = hid_ep.interval ? hid_ep.interval : 1;
    uint32_t period = dev_speed == USB_SPEED_HIGH
        ? (1u << ((iv > 16 ? 16 : iv) - 1)) * 125u
        : iv * 1000u;
    if (period < 4000) period = 4000;       /* polled from the main loop anyway */
    if (now - hid_ep.last_us < period)
        return;
    hid_ep.last_us = now;

    dwc2_pipe_t p = { addr, hid_ep.ep, 1, EP_INTERRUPT, hid_ep.mps, dev_speed, &hid_ep.toggle };
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
