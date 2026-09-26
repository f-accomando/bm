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
    kprintf("\n");
}

int usb_init(void)
{
    memset(&info, 0, sizeof info);
    memset(&hid_ep, 0, sizeof hid_ep);
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

    /* first HID (or Xbox 360) interface with an interrupt IN endpoint */
    int kind = USB_OTHER, cur_class = -1, cur_sub = -1, cur_proto = -1, cur_iface = -1;
    uint16_t report_len = 0;
    for (int i = 0; i + 1 < total && c[i]; i += c[i]) {
        if (c[i + 1] == DESC_INTERFACE && i + 8 < total) {
            cur_iface = c[i + 2]; cur_class = c[i + 5]; cur_sub = c[i + 6]; cur_proto = c[i + 7];
        } else if (c[i + 1] == 0x21 && i + 8 < total && cur_class == 3 && !hid_ep.active) {
            report_len = (uint16_t)(c[i + 7] | c[i + 8] << 8);
        } else if (c[i + 1] == DESC_ENDPOINT && i + 6 < total && !hid_ep.active &&
                   (c[i + 2] & 0x80) && (c[i + 3] & 3) == EP_INTERRUPT) {
            int k = cur_class == 3 ? (cur_proto == 1 && cur_sub == 1 ? USB_KEYBOARD : USB_GAMEPAD)
                  : (cur_class == 0xFF && cur_sub == 0x5D && cur_proto == 0x01) ? USB_XBOX360 : -1;
            if (k < 0)
                continue;
            kind = k;
            hid_ep.ep = c[i + 2] & 0x0F;
            hid_ep.mps = (uint16_t)((c[i + 4] | c[i + 5] << 8) & 0x7FF);
            hid_ep.interval = c[i + 6] ? c[i + 6] : 10;
            hid_ep.iface = (uint8_t)cur_iface;
            hid_ep.active = 1;
        }
    }
    (void)dev_class;
    if (control(0x00, 9, config_value, 0, NULL, 0) < 0) {  /* SET_CONFIGURATION */
        kprintf("usb: SET_CONFIGURATION failed\n");
        return USB_OTHER;
    }

    if (kind == USB_KEYBOARD) {
        control(0x21, 0x0B, 0, hid_ep.iface, NULL, 0);      /* SET_PROTOCOL boot */
        control(0x21, 0x0A, 0, hid_ep.iface, NULL, 0);      /* SET_IDLE 0 */
        hid_keyboard_attach();
    } else if (kind == USB_GAMEPAD) {
        static uint8_t rd[512];
        if (report_len > sizeof rd) report_len = sizeof rd;
        int n = report_len ? control(0x81, 6, DESC_HID_REPORT << 8, hid_ep.iface, rd, report_len) : -1;
        control(0x21, 0x0A, 0, hid_ep.iface, NULL, 0);
        if (n <= 0 || hid_gamepad_attach(rd, (uint32_t)n) != 0)
            kind = USB_OTHER;
    } else if (kind == USB_XBOX360) {
        hid_xbox360_attach();
    }
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
    if (r == XFER_OK && got)
        hid_report(info.kind, report, got);
    else if (r == XFER_ERROR || r == XFER_TIMEOUT)
        hid_ep.toggle = PID_DATA0;
}
