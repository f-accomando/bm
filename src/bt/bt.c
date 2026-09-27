/*
 * Bluetooth on the Pi Zero W (BCM43438): one HID game controller (the
 * DualShock 4 is the reference). Classic Bluetooth only:
 *
 *   chip up      power, 32 kHz clock, firmware patch, event mask, SSP on
 *   pairing      inquiry, Create Connection, SSP "Just Works" (no MITM),
 *                encryption, L2CAP channels 0x11 (control) 0x13 (interrupt);
 *                the link key goes to bm33/config.txt
 *   reconnect    page scan on: the pad connects to us (PS button), we answer
 *                the Link Key Request with the saved key, it opens the
 *                L2CAP channels
 *   input        HID reports (0xA1 ...) on the interrupt channel -> hid layer
 *
 * No SDP: the report format of the supported pad is known.
 */
#include "bt.h"
#include "btuart.h"
#include "hci.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "lib/printf.h"
#include "usb/hid.h"

#include <stdlib.h>
#include <string.h>

#define BT_ON_GPIO   45             /* BT_REG_ON of the BCM43438 on the Zero W */
#define LPO_GPIO     43             /* GPCLK2: 32.768 kHz sleep clock */

#define CM_GP2CTL    (PERIPHERAL_BASE + 0x101080)
#define CM_GP2DIV    (PERIPHERAL_BASE + 0x101084)
#define CM_PASSWD    0x5A000000u
#define CM_ENAB      (1u << 4)
#define CM_BUSY      (1u << 7)

#define PSM_HID_CONTROL   0x11
#define PSM_HID_INTERRUPT 0x13
#define CID_SIGNALING     0x0001
#define CID_CONTROL       0x0040    /* our local channel ids */
#define CID_INTERRUPT     0x0041

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

typedef struct {
    uint16_t lcid, rcid;
    int requested, open;
    int conf_in, conf_out;          /* their config accepted / ours accepted */
} chan_t;

static struct {
    int started;
    int connected;                  /* ACL link up */
    uint16_t handle;
    uint8_t addr[6];                /* peer */
    int auth_done, auth_status;
    int enc;
    chan_t ctrl, intr;
    uint8_t sig_id;
    int have_key;
    uint8_t key_addr[6], key[16];   /* the paired pad */
    int announced;
} bt;

/* ---------------------------------------------------------------- helpers */

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

/* "bt_pad=00:1f:e2:bf:d7:dd <32 hex digits of the link key>" */
static void load_key(void)
{
    const char *v = config_get("bt_pad");
    uint8_t a[6];
    bt.have_key = 0;
    if (!v || parse_hex(v, a, 6, ':') || v[17] != ' ' || parse_hex(v + 18, bt.key, 16, 0))
        return;
    for (int i = 0; i < 6; i++)
        bt.key_addr[i] = a[5 - i];              /* written most significant first */
    bt.have_key = 1;
}

static void save_key(const uint8_t *addr, const uint8_t *key)
{
    char v[72], a[18];
    addr_str(a, addr);
    int n = ksnprintf(v, sizeof v, "%s ", a);
    for (int i = 0; i < 16; i++)
        n += ksnprintf(v + n, sizeof v - (unsigned)n, "%02x", key[i]);
    config_set("bt_pad", v);
    config_save();
    memcpy(bt.key_addr, addr, 6);
    memcpy(bt.key, key, 16);
    bt.have_key = 1;
}

/* ---------------------------------------------------------------- chip */

static const char *lpo_clock(void)
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
    kprintf("bt: the serial console moves to the mini UART (same pins, same speed)\n");
    uart_use_mini();
    kprintf("bt: 32 kHz clock %s\n", lpo_clock());
    btuart_init(115200);

    int r = reset();
    if (r != 0) {
        kprintf("bt: no answer to HCI reset, power-cycling the chip (GPIO%d)\n", BT_ON_GPIO);
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

    /* events we handle (SSP ones are not in the default mask), SSP on,
     * our name and class (console), page scan so the pad can come back */
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
    kprintf("bt: ready, address %s, HCI %u, LMP subversion %04x\n", as, v[0], v[6] | v[7] << 8);
    bt.started = 1;
    load_key();
    if (bt.have_key) {
        addr_str(as, bt.key_addr);
        kprintf("bt: paired pad %s: press its PS button to connect\n", as);
    }
    return 0;
}

int bt_paired(void)
{
    return config_get("bt_pad") != NULL;
}

/* ---------------------------------------------------------------- L2CAP */

static void l2cap_send(uint16_t cid, const uint8_t *data, uint16_t len)
{
    static uint8_t buf[80];
    if (len + 4u > sizeof buf)
        return;
    put16(buf, len);
    put16(buf + 2, cid);
    memcpy(buf + 4, data, len);
    hci_acl_send(bt.handle, buf, (uint16_t)(len + 4));
}

static void sig_send(uint8_t code, uint8_t id, const uint8_t *data, uint16_t len)
{
    uint8_t buf[64];
    buf[0] = code;
    buf[1] = id;
    put16(buf + 2, len);
    memcpy(buf + 4, data, len);
    l2cap_send(CID_SIGNALING, buf, (uint16_t)(len + 4));
}

static void send_config(chan_t *c)
{
    uint8_t p[8];
    put16(p, c->rcid);
    put16(p + 2, 0);                            /* flags */
    p[4] = 0x01;                                /* option: MTU */
    p[5] = 2;
    put16(p + 6, 672);
    sig_send(L2_CONF_REQ, ++bt.sig_id, p, 8);
}

static void l2cap_connect(chan_t *c, uint16_t psm)
{
    uint8_t p[4];
    put16(p, psm);
    put16(p + 2, c->lcid);
    c->requested = 1;
    sig_send(L2_CONN_REQ, ++bt.sig_id, p, 4);
}

static chan_t *by_lcid(uint16_t lcid)
{
    return lcid == bt.ctrl.lcid ? &bt.ctrl : lcid == bt.intr.lcid ? &bt.intr : NULL;
}

static void check_open(chan_t *c)
{
    c->open = c->conf_in && c->conf_out;
    if (bt.ctrl.open && bt.intr.open && !bt.announced) {
        char as[18];
        addr_str(as, bt.addr);
        kprintf("bt: controller %s connected\n", as);
        bt.announced = 1;
    }
}

static void handle_signaling(const uint8_t *s, uint16_t len)
{
    while (len >= 4) {
        uint8_t code = s[0], id = s[1];
        uint16_t n = get16(s + 2);
        const uint8_t *p = s + 4;
        if (n + 4u > len)
            return;
        switch (code) {
        case L2_CONN_REQ: {                     /* the pad opens a channel (reconnect) */
            uint16_t psm = get16(p), scid = get16(p + 2);
            chan_t *c = psm == PSM_HID_CONTROL ? &bt.ctrl : psm == PSM_HID_INTERRUPT ? &bt.intr : NULL;
            uint8_t r[8];
            put16(r, c ? c->lcid : 0);
            put16(r + 2, scid);
            put16(r + 4, c ? 0 : 2);            /* success / PSM not supported */
            put16(r + 6, 0);
            sig_send(L2_CONN_RSP, id, r, 8);
            if (c) {
                c->rcid = scid;
                c->conf_in = c->conf_out = c->open = 0;
                send_config(c);
            }
            break;
        }
        case L2_CONN_RSP: {
            uint16_t dcid = get16(p), scid = get16(p + 2), result = get16(p + 4);
            chan_t *c = by_lcid(scid);
            if (c && result == 0) {
                c->rcid = dcid;
                send_config(c);
            } else if (c && result != 1) {      /* 1 = pending */
                kprintf("bt: channel refused (result %u)\n", result);
                c->requested = 0;
            }
            break;
        }
        case L2_CONF_REQ: {
            chan_t *c = by_lcid(get16(p));
            uint8_t r[6];
            put16(r, c ? c->rcid : 0);
            put16(r + 2, 0);
            put16(r + 4, 0);                    /* success: we take their options */
            sig_send(L2_CONF_RSP, id, r, 6);
            if (c) {
                c->conf_in = 1;
                check_open(c);
            }
            break;
        }
        case L2_CONF_RSP: {
            chan_t *c = by_lcid(get16(p));
            if (c && get16(p + 4) == 0) {
                c->conf_out = 1;
                check_open(c);
            }
            break;
        }
        case L2_DISC_REQ: {
            uint8_t r[4];
            memcpy(r, p, 4);
            sig_send(L2_DISC_RSP, id, r, 4);
            chan_t *c = by_lcid(get16(p));
            if (c)
                c->open = c->conf_in = c->conf_out = c->requested = 0;
            break;
        }
        case L2_ECHO_REQ:
            sig_send(L2_ECHO_RSP, id, NULL, 0);
            break;
        case L2_INFO_REQ: {
            uint8_t r[4];
            memcpy(r, p, 2);
            put16(r + 2, 1);                    /* not supported */
            sig_send(L2_INFO_RSP, id, r, 4);
            break;
        }
        default:
            break;
        }
        s += n + 4u;
        len = (uint16_t)(len - (n + 4u));
    }
}

static void handle_acl(const hci_pkt_t *p)
{
    if (p->len < 8 || (get16(p->data) & 0x0FFF) != bt.handle)
        return;
    uint16_t l2len = get16(p->data + 4), cid = get16(p->data + 6);
    const uint8_t *d = p->data + 8;
    if (l2len + 8u > p->len)
        return;                                 /* fragments are not expected */
    if (cid == CID_SIGNALING) {
        handle_signaling(d, l2len);
    } else if (cid == bt.intr.lcid && l2len >= 2 && d[0] == 0xA1) {
        hid_bt_report(d + 1, l2len - 1u);       /* DATA | Input, then report ID */
    }
}

/* ---------------------------------------------------------------- events */

static void reset_link(void)
{
    bt.connected = bt.auth_done = bt.enc = bt.announced = 0;
    memset(&bt.ctrl, 0, sizeof bt.ctrl);
    memset(&bt.intr, 0, sizeof bt.intr);
    bt.ctrl.lcid = CID_CONTROL;
    bt.intr.lcid = CID_INTERRUPT;
    hid_bt_clear();
}

static void handle_event(const hci_pkt_t *p)
{
    const uint8_t *e = p->data + 2;
    uint8_t r[23];
    switch (p->data[0]) {
    case 0x03:                                  /* Connection Complete */
        if (e[0] == 0) {
            reset_link();
            bt.connected = 1;
            bt.handle = get16(e + 1) & 0x0FFF;
            memcpy(bt.addr, e + 3, 6);
        } else {
            kprintf("bt: connection failed (status %02x)\n", e[0]);
        }
        break;
    case 0x04: {                                /* Connection Request: the pad is back */
        int ours = bt.have_key && memcmp(e, bt.key_addr, 6) == 0;
        memcpy(r, e, 6);
        if (ours || !bt.have_key) {
            r[6] = 0x00;                        /* become master */
            hci_send(HCI_ACCEPT_CONNECTION, r, 7);
        } else {
            r[6] = 0x0F;                        /* reject: unacceptable address */
            hci_send(0x040A, r, 7);
        }
        break;
    }
    case 0x05:                                  /* Disconnection Complete */
        if (bt.connected && (get16(e + 1) & 0x0FFF) == bt.handle) {
            if (bt.announced)
                kprintf("bt: controller disconnected\n");
            reset_link();
        }
        break;
    case 0x06:                                  /* Authentication Complete */
        bt.auth_done = 1;
        bt.auth_status = e[0];
        break;
    case 0x08:                                  /* Encryption Change */
        bt.enc = e[0] == 0 && e[3];
        break;
    case 0x16:                                  /* PIN Code Request (legacy pads) */
        memcpy(r, e, 6);
        r[6] = 4;
        memset(r + 7, 0, 16);
        memcpy(r + 7, "0000", 4);
        hci_send(HCI_PIN_CODE_REPLY, r, 23);
        break;
    case 0x17:                                  /* Link Key Request */
        memcpy(r, e, 6);
        if (bt.have_key && memcmp(e, bt.key_addr, 6) == 0) {
            memcpy(r + 6, bt.key, 16);
            hci_send(HCI_LINK_KEY_REPLY, r, 22);
        } else {
            hci_send(HCI_LINK_KEY_NEG_REPLY, r, 6);
        }
        break;
    case 0x18:                                  /* Link Key Notification: paired */
        save_key(e, e + 6);
        break;
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

void bt_poll(void)
{
    static hci_pkt_t p;
    if (!bt.started)
        return;
    for (int n = 0; n < 16 && hci_pending(); n++) {
        if (hci_recv(&p, 20000) != 0)
            break;
        dispatch(&p);
    }
}

/* Runs the stack until *flag is set or timeout_ms passes; 0 if set. */
static int wait_for(const int *flag, uint32_t timeout_ms)
{
    static hci_pkt_t p;
    uint32_t t0 = timer_ticks();
    while (!*flag) {
        if (timer_ticks() - t0 > timeout_ms * 1000u)
            return -1;
        if (hci_recv(&p, 50000) == 0)
            dispatch(&p);
        if (!bt.connected && flag != &bt.connected)
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

static int pair(const found_t *f)
{
    char as[18];
    addr_str(as, f->addr);
    kprintf("bt: pairing with %s...\n", as);

    uint8_t p[13];
    memcpy(p, f->addr, 6);
    put16(p + 6, 0xCC18);                       /* DM1..DH5 */
    p[8] = f->psrm;
    p[9] = 0;
    put16(p + 10, f->clock | 0x8000);
    p[12] = 1;                                  /* allow role switch */
    if (hci_cmd(HCI_CREATE_CONNECTION, p, 13, NULL, 0, 1000000) != 0 ||
        wait_for(&bt.connected, 10000) != 0) {
        kprintf("\x1b[91mbt: cannot connect (is it still flashing?)\x1b[0m\n");
        return -1;
    }
    uint8_t h[3];
    put16(h, bt.handle);
    if (hci_cmd(HCI_AUTH_REQUESTED, h, 2, NULL, 0, 1000000) != 0 ||
        wait_for(&bt.auth_done, 20000) != 0 || bt.auth_status != 0) {
        kprintf("\x1b[91mbt: pairing failed (status %02x)\x1b[0m\n", bt.auth_status);
        return -1;
    }
    h[2] = 1;
    if (hci_cmd(HCI_SET_CONN_ENCRYPTION, h, 3, NULL, 0, 1000000) != 0 ||
        wait_for(&bt.enc, 10000) != 0) {
        kprintf("\x1b[91mbt: encryption failed\x1b[0m\n");
        return -1;
    }
    l2cap_connect(&bt.ctrl, PSM_HID_CONTROL);
    if (wait_for(&bt.ctrl.open, 10000) != 0) {
        kprintf("\x1b[91mbt: HID control channel not opened\x1b[0m\n");
        return -1;
    }
    l2cap_connect(&bt.intr, PSM_HID_INTERRUPT);
    if (wait_for(&bt.intr.open, 10000) != 0) {
        kprintf("\x1b[91mbt: HID interrupt channel not opened\x1b[0m\n");
        return -1;
    }
    kprintf("bt: paired; next time just press PS on the controller\n");
    return 0;
}

void bt_scan(unsigned seconds)
{
    if (!bt.started && bt_start() != 0)
        return;
    if (!bt.ctrl.lcid)
        reset_link();
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
    return bt.ctrl.open && bt.intr.open;
}
