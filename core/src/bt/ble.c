/*
 * Bluetooth LE keyboards and mice (HID over GATT), e.g. the Logitech MX
 * Keys and MX Master: one of each, connected at the same time.
 *
 *   pairing     'K' (keyboard) or 'O' (mouse, M32) in the monitor: active
 *               scan for one in pairing mode, LE connection, SMP pairing
 *               as initiator. For a keyboard we show a 6-digit code and
 *               its owner types it (Passkey Entry); a mouse cannot type:
 *               Just Works. LE Secure Connections (P-256, f4/f5/f6) when
 *               the device supports it, else LE legacy pairing (c1/s1).
 *               Its LTK (and its IRK, if it hides its address) go to
 *               bm/config.txt as bt_kbd and bt_kbd_key (bt_mouse and
 *               bt_mouse_key).
 *   reconnect   while a paired device is away we scan (passively, one
 *               scan for both); when it advertises (a key pressed, the
 *               mouse moved), its address or one that its IRK resolves, we
 *               connect and encrypt with the saved LTK
 *   input       GATT: the HID service, its characteristics and
 *               descriptors, the report map (which report is the keyboard
 *               or the mouse, where its fields are); notifications turned
 *               on for that input report -> hid_ble_keyboard() or
 *               hid_mouse_report()
 *
 * One LE link per device; one connection is made at a time. ATT MTU 23
 * (the default): long values are read in pieces. Our GATT server has
 * nothing: requests get "not found" answers.
 */
#include "ble.h"
#include "smp_crypto.h"
#include "drivers/rng.h"
#include "drivers/timer.h"
#include "kernel/config.h"
#include "lib/printf.h"
#include "usb/hid.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LE_SET_EVENT_MASK       0x2001
#define LE_READ_BUFFER_SIZE     0x2002
#define LE_SET_SCAN_PARAMS      0x200B
#define LE_SET_SCAN_ENABLE      0x200C
#define LE_CREATE_CONN          0x200D
#define LE_CREATE_CONN_CANCEL   0x200E
#define LE_CONN_UPDATE          0x2013
#define LE_START_ENCRYPTION     0x2019
#define HCI_DISCONNECT          0x0406
#define HCI_READ_BUFFER_SIZE    0x1005

#define CID_ATT     0x0004
#define CID_LE_SIG  0x0005
#define CID_SMP     0x0006

#define ATT_MTU     23

/* SMP codes */
#define SMP_PAIR_REQ    0x01
#define SMP_PAIR_RSP    0x02
#define SMP_CONFIRM     0x03
#define SMP_RANDOM      0x04
#define SMP_FAILED      0x05
#define SMP_ENC_INFO    0x06
#define SMP_MASTER_ID   0x07
#define SMP_ID_INFO     0x08
#define SMP_ID_ADDR     0x09
#define SMP_SIGN_INFO   0x0A
#define SMP_SEC_REQ     0x0B
#define SMP_PUBLIC_KEY  0x0C
#define SMP_DHKEY_CHECK 0x0D

/* our side of the pairing: we show a code (DisplayOnly), bonding, MITM,
 * Secure Connections; we want the keyboard's LTK and IRK */
#define OUR_IO          0x00
#define OUR_AUTH        0x0D
#define OUR_KEYS_IN     0x00
#define OUR_KEYS_WANT   0x03

enum { IDLE, SCANNING, CONNECTING, CONNECTED };                 /* link */
enum { SMP_NONE, SMP_WAIT_RSP, SMP_WAIT_PK, SMP_WAIT_CONFIRM, SMP_WAIT_RANDOM,
       SMP_WAIT_CHECK, SMP_WAIT_ENC, SMP_WAIT_KEYS, SMP_DONE, SMP_FAILED_ };
enum { G_NONE, G_SERVICE, G_CHARS, G_DESCS, G_REFS, G_MAP, G_PROTO, G_CCCD, G_READY, G_FAILED };

#define MAX_CHARS   24
#define MAX_DESCS   32

typedef struct { uint16_t decl, value, uuid; uint8_t props; uint16_t cccd, ref; uint8_t id, type; } gchar_t;

typedef struct {
    uint8_t addr[6], type;
    uint8_t irk[16];
    int have_irk;
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
} bond_t;

/* a keyboard or a mouse */
typedef struct {
    int kind;                           /* LE_KBD, LE_MOUSE */
    int have_bond;
    bond_t bond;
    int bond_bad;                       /* it refused our key: wait for K / O */

    int state;
    uint32_t since;                     /* timer_ticks() of the last state change */
    uint16_t handle;
    uint8_t peer[6], peer_type;
    int enc;
    int announced;

    /* L2CAP reassembly */
    uint8_t rx[160];
    uint16_t rx_len, rx_want;

    /* pairing */
    int smp;
    int sc;                             /* LE Secure Connections */
    int passkey_method;
    uint32_t passkey;
    uint8_t preq[7], pres[7];
    uint8_t key_size;
    uint8_t tk[16], mrand[16], sconfirm[16], srand[16];
    uint8_t priv[32], pub[64], peer_pub[64], dhkey[32], mackey[16], ltk[16];
    int round;                          /* passkey bit being checked (SC) */
    uint8_t keys_left;                  /* key distribution PDUs still expected */
    bond_t nb;                          /* the bond being made */
    int smp_reason;

    /* GATT client */
    int g;
    uint8_t att_req;                    /* opcode of the request we wait for, 0 none */
    uint32_t att_sent;
    uint16_t svc_start, svc_end, cursor;
    gchar_t ch[MAX_CHARS];
    int nch;
    uint16_t desc_handle[MAX_DESCS], desc_uuid[MAX_DESCS];
    int ndesc;
    int ref_i;
    uint16_t map_handle;
    uint8_t map[1024];
    uint16_t map_len;
    hid_kbd_layout_t layout;            /* the keyboard's report */
    hid_mouse_layout_t mlayout;         /* the mouse's, without its report ID */
    uint16_t kbd_value, kbd_cccd;       /* the input report: value and notifications */
    int boot;                           /* boot protocol fallback */
    uint16_t proto_handle;
    char name[32];
} le_dev_t;

static struct {
    int ready;                          /* ble_init done */
    uint8_t own[6];
    uint16_t acl_len;                   /* LE ACL packet size of the chip */
    int pairing;                        /* a device is being paired (le points at it) */
    int scan_on;                        /* passive scan for the paired ones */
    le_dev_t dev[LE_DEVS];
} lg;

static le_dev_t *le = &lg.dev[LE_KBD];  /* the device being handled */

static const char *kind_name(void)
{
    return le->kind == LE_MOUSE ? "mouse" : "keyboard";
}

static le_dev_t *pd;                    /* the device being paired */

/* this device is the one being paired (the other one goes on as usual) */
static int pairing_me(void)
{
    return lg.pairing && le == pd;
}

/* ---------------------------------------------------------------- helpers */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void addr_str(char *out, const uint8_t *a)
{
    ksnprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[5], a[4], a[3], a[2], a[1], a[0]);
}

static void hex_str(char *out, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++)
        ksnprintf(out + 2 * i, 3, "%02x", b[i]);
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

static void trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* while pairing or connecting, every step is shown (grey): a photo of the
 * screen tells how far the keyboard or the mouse got */
static void trace(const char *fmt, ...)
{
    if (!pairing_me() && le->announced)
        return;
    char buf[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    kprintf("\x1b[90mbt:   %s\x1b[0m\n", buf);
}

static int rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    return rng_read(buf, len) == 0 ? 0 : -1;
}

static void set_state(int s)
{
    le->state = s;
    le->since = timer_ticks();
}

static uint32_t since_ms(void)
{
    return (timer_ticks() - le->since) / 1000u;
}

/* ---------------------------------------------------------------- bond */

/* the keys in bm/config.txt: bt_kbd and bt_kbd_key, bt_mouse and bt_mouse_key */
static const char *key_addr(int kind) { return kind == LE_MOUSE ? "bt_mouse" : "bt_kbd"; }
static const char *key_ltk(int kind) { return kind == LE_MOUSE ? "bt_mouse_key" : "bt_kbd_key"; }

/* bt_kbd=<address> <type> <irk or -> ; bt_kbd_key=<ltk> <ediv> <rand> */
static void load_bond(void)
{
    const char *a = config_get(key_addr(le->kind)), *k = config_get(key_ltk(le->kind));
    bond_t *b = &le->bond;
    le->have_bond = 0;
    if (!a || !k || strlen(a) < 21 || strlen(k) < 54)
        return;
    uint8_t ad[6];
    if (parse_hex(a, ad, 6, ':') || a[17] != ' ' || (a[18] != '0' && a[18] != '1') || a[19] != ' ')
        return;
    for (int i = 0; i < 6; i++)
        b->addr[i] = ad[5 - i];
    b->type = (uint8_t)(a[18] - '0');
    b->have_irk = a[20] != '-' && parse_hex(a + 20, b->irk, 16, 0) == 0;
    uint8_t e[2];
    if (parse_hex(k, b->ltk, 16, 0) || k[32] != ' ' || parse_hex(k + 33, e, 2, 0) || k[37] != ' ' ||
        parse_hex(k + 38, b->rand, 8, 0))
        return;
    b->ediv = (uint16_t)(e[0] << 8 | e[1]);
    le->have_bond = 1;
}

static void save_bond(const bond_t *b)
{
    char v[72], as[18];
    int n;
    addr_str(as, b->addr);
    n = ksnprintf(v, sizeof v, "%s %u ", as, b->type);
    if (b->have_irk)
        hex_str(v + n, b->irk, 16);
    else
        ksnprintf(v + n, sizeof v - (unsigned)n, "-");
    config_set(key_addr(le->kind), v);
    hex_str(v, b->ltk, 16);
    ksnprintf(v + 32, sizeof v - 32, " %04x ", b->ediv);
    hex_str(v + 38, b->rand, 8);
    config_set(key_ltk(le->kind), v);
    config_save();
    le->bond = *b;
    le->have_bond = 1;
    le->bond_bad = 0;
}

int ble_forget(void)
{
    int had = 0;
    for (int k = 0; k < LE_DEVS; k++) {
        le_dev_t *d = &lg.dev[k];
        had += d->have_bond || config_get(key_addr(k)) != NULL;
        if (d->state == CONNECTED) {
            uint8_t p[3];
            put16(p, d->handle);
            p[2] = 0x13;
            hci_send(HCI_DISCONNECT, p, 3);
        }
        d->have_bond = 0;
        config_unset(key_addr(k));
        config_unset(key_ltk(k));
    }
    return had;
}

int ble_paired(int kind)
{
    return kind >= 0 && kind < LE_DEVS && config_get(key_addr(kind)) != NULL;
}

int ble_connected(int kind)
{
    return kind >= 0 && kind < LE_DEVS && lg.dev[kind].state == CONNECTED && lg.dev[kind].g == G_READY;
}

/* ---------------------------------------------------------------- sending */

/* One L2CAP frame on the LE link, in pieces the chip's buffers take. */
static void l2_send(uint16_t cid, const uint8_t *data, uint16_t len)
{
    uint8_t f[4 + 80];
    if (le->state != CONNECTED || len > 80)
        return;
    put16(f, len);
    put16(f + 2, cid);
    memcpy(f + 4, data, len);
    uint16_t total = (uint16_t)(len + 4), off = 0, step = lg.acl_len ? lg.acl_len : 27;
    while (off < total) {
        uint16_t n = (uint16_t)(total - off < step ? total - off : step);
        hci_acl_send_pb(le->handle, off == 0 ? 0 : 1, f + off, n);
        off = (uint16_t)(off + n);
    }
}

static void smp_send(const uint8_t *p, uint16_t len) { l2_send(CID_SMP, p, len); }

static void smp_fail(uint8_t reason, const char *why)
{
    uint8_t p[2] = { SMP_FAILED, reason };
    smp_send(p, 2);
    kprintf("\x1b[91mbt: %s pairing failed: %s\x1b[0m\n", kind_name(), why);
    le->smp = SMP_FAILED_;
    le->smp_reason = reason;
}

static void att_send(const uint8_t *p, uint16_t len)
{
    l2_send(CID_ATT, p, len);
    if (p[0] != 0x01 && p[0] != 0x03 && p[0] != 0x13 && p[0] != 0x1E && p[0] != 0x52) {
        le->att_req = p[0];              /* a request: its answer is awaited */
        le->att_sent = timer_ticks();
    }
}

static void disconnect(uint8_t reason)
{
    if (le->state != CONNECTED)
        return;
    uint8_t d[3];
    put16(d, le->handle);
    d[2] = reason;
    hci_send(HCI_DISCONNECT, d, 3);
}

/* ---------------------------------------------------------------- scanning */

static void scan(int on, int active)
{
    uint8_t p[7];
    lg.scan_on = on;
    if (on) {
        p[0] = (uint8_t)active;
        put16(p + 1, active ? 0x0030 : 0x00A0);         /* interval: 30 ms / 100 ms */
        put16(p + 3, active ? 0x0030 : 0x0030);         /* window: 30 ms */
        p[5] = 0;                                       /* our public address */
        p[6] = 0;                                       /* everyone */
        hci_send(LE_SET_SCAN_PARAMS, p, 7);
    }
    p[0] = (uint8_t)on;
    p[1] = 1;                                           /* filter duplicates */
    hci_send(LE_SET_SCAN_ENABLE, p, 2);
}

static void connect_to(const uint8_t *addr, uint8_t type)
{
    uint8_t p[25];
    scan(0, 0);
    put16(p, 0x0030);                   /* scan interval */
    put16(p + 2, 0x0030);               /* window */
    p[4] = 0;                           /* this address, not the white list */
    p[5] = type;
    memcpy(p + 6, addr, 6);
    p[12] = 0;                          /* our public address */
    put16(p + 13, 0x0006);              /* connection interval 7.5 .. 15 ms */
    put16(p + 15, 0x000C);
    put16(p + 17, 0);                   /* latency */
    put16(p + 19, 0x00C8);              /* supervision timeout 2 s */
    put16(p + 21, 0);
    put16(p + 23, 0);
    memcpy(le->peer, addr, 6);
    le->peer_type = type;
    hci_send(LE_CREATE_CONN, p, 25);
    set_state(CONNECTING);
}

/* The saved device: its address, or a private one its IRK makes. */
static int is_ours(const uint8_t *addr, uint8_t type)
{
    if (!le->have_bond)
        return 0;
    if (type == le->bond.type && memcmp(addr, le->bond.addr, 6) == 0)
        return 1;
    return type == 1 && le->bond.have_irk && smp_resolves(le->bond.irk, addr);
}

/* ---------------------------------------------------------------- pairing */

static void start_encryption(const uint8_t ltk[16], uint16_t ediv, const uint8_t rand[8])
{
    uint8_t p[28];
    put16(p, le->handle);
    memcpy(p + 2, rand, 8);
    put16(p + 10, ediv);
    memcpy(p + 12, ltk, 16);
    hci_send(LE_START_ENCRYPTION, p, 28);
}

static void mask_key(uint8_t k[16])
{
    for (int i = le->key_size; i < 16; i++)
        k[i] = 0;
}

static void our_a(uint8_t a[7])       /* address and type, as f5/f6 want them */
{
    memcpy(a, lg.own, 6);
    a[6] = 0;
}

static void peer_a(uint8_t a[7])
{
    memcpy(a, le->peer, 6);
    a[6] = le->peer_type;
}

static int count_keys(uint8_t dist)
{
    int n = 0;
    if ((dist & 1) && !le->sc) n += 2;           /* LTK + EDIV/Rand (legacy only) */
    if (dist & 2) n += 2;                       /* IRK + identity address */
    if (dist & 4) n += 1;                       /* CSRK */
    return n;
}

static void smp_start(void)
{
    uint8_t p[7] = { SMP_PAIR_REQ, OUR_IO, 0, OUR_AUTH, 16, OUR_KEYS_IN, OUR_KEYS_WANT };
    memcpy(le->preq, p, 7);
    memset(&le->nb, 0, sizeof le->nb);
    memcpy(le->nb.addr, le->peer, 6);
    le->nb.type = le->peer_type;
    le->smp = SMP_WAIT_RSP;
    smp_send(p, 7);
    trace("-> pairing request");
}

static void show_passkey(void)
{
    uint32_t r = 0;
    if (rng_read(&r, sizeof r) != 0)
        r = timer_ticks() * 2654435761u;
    le->passkey = r % 1000000u;
    kprintf("\n\x1b[93mbt: type this code on the keyboard, then Enter:\x1b[0m\n"
            "\x1b[97m\n        %06lu\n\n\x1b[0m", (unsigned long)le->passkey);
}

/* SC, passkey: the confirm value of the current round */
static void sc_send_confirm(void)
{
    uint8_t p[17], z;
    rng_read(le->mrand, 16);
    z = le->passkey_method ? (uint8_t)(0x80 | ((le->passkey >> le->round) & 1)) : 0;
    p[0] = SMP_CONFIRM;
    smp_f4(le->pub, le->peer_pub, le->mrand, z, p + 1);
    smp_send(p, 17);
    le->smp = SMP_WAIT_CONFIRM;
}

static void send_random(void)
{
    uint8_t p[17];
    p[0] = SMP_RANDOM;
    memcpy(p + 1, le->mrand, 16);
    smp_send(p, 17);
    le->smp = SMP_WAIT_RANDOM;
}

static void sc_dhkey_check(void)
{
    uint8_t a[7], b[7], r[16], io[3], p[17];
    our_a(a);
    peer_a(b);
    smp_f5(le->dhkey, le->mrand, le->srand, a, b, le->mackey, le->ltk);
    memset(r, 0, 16);
    if (le->passkey_method) {
        r[0] = (uint8_t)le->passkey;
        r[1] = (uint8_t)(le->passkey >> 8);
        r[2] = (uint8_t)(le->passkey >> 16);
    }
    memcpy(io, le->preq + 1, 3);
    p[0] = SMP_DHKEY_CHECK;
    smp_f6(le->mackey, le->mrand, le->srand, r, io, a, b, p + 1);
    smp_send(p, 17);
    le->smp = SMP_WAIT_CHECK;
}

static void pairing_encrypted(void)
{
    le->keys_left = (uint8_t)count_keys(le->pres[6]);
    if (le->sc) {
        memcpy(le->nb.ltk, le->ltk, 16);
        le->nb.ediv = 0;
        memset(le->nb.rand, 0, 8);
    }
    le->smp = le->keys_left ? SMP_WAIT_KEYS : SMP_DONE;
}

static void handle_smp(const uint8_t *d, uint16_t len)
{
    if (len < 1)
        return;
    uint8_t code = d[0];
    trace("<- SMP %02x (%u bytes)", code, len);
    if (code == SMP_FAILED) {
        kprintf("\x1b[91mbt: the %s stopped the pairing (reason %02x%s)\x1b[0m\n", kind_name(), len > 1 ? d[1] : 0,
                len > 1 && d[1] == 0x01 ? ": wrong code?" : len > 1 && d[1] == 0x04 ? ": wrong code" : "");
        le->smp = SMP_FAILED_;
        le->smp_reason = len > 1 ? d[1] : 0;
        return;
    }
    if (code == SMP_SEC_REQ) {
        if (le->have_bond && !le->enc && !pairing_me())
            start_encryption(le->bond.ltk, le->bond.ediv, le->bond.rand);
        else if (pairing_me() && le->smp == SMP_NONE)
            smp_start();
        return;
    }
    switch (le->smp) {
    case SMP_WAIT_RSP: {
        if (code != SMP_PAIR_RSP || len < 7)
            return smp_fail(0x08, "unexpected answer");
        memcpy(le->pres, d, 7);
        le->key_size = d[4] < 16 ? d[4] : 16;
        if (le->key_size < 7)
            return smp_fail(0x06, "key too short");
        le->sc = (d[3] & 0x08) && (OUR_AUTH & 0x08);
        /* the keyboard can type (KeyboardOnly, KeyboardDisplay): it types
         * our code; anything else (a mouse): no code (Just Works) */
        le->passkey_method = ((d[3] | OUR_AUTH) & 0x04) && (d[1] == 0x02 || d[1] == 0x04);
        kprintf("bt: pairing, %s, %s\n", le->sc ? "LE Secure Connections" : "LE legacy",
                le->passkey_method ? "the keyboard types a code" : "no code (Just Works)");
        if (le->passkey_method)
            show_passkey();
        if (le->sc) {
            if (smp_p256_keypair(le->priv, le->pub, rng, NULL) != 0)
                return smp_fail(0x08, "no key pair (random numbers)");
            uint8_t p[65];
            p[0] = SMP_PUBLIC_KEY;
            memcpy(p + 1, le->pub, 64);
            smp_send(p, 65);
            le->smp = SMP_WAIT_PK;
        } else {
            memset(le->tk, 0, 16);
            if (le->passkey_method) {
                le->tk[0] = (uint8_t)le->passkey;
                le->tk[1] = (uint8_t)(le->passkey >> 8);
                le->tk[2] = (uint8_t)(le->passkey >> 16);
            }
            rng_read(le->mrand, 16);
            uint8_t p[17];
            p[0] = SMP_CONFIRM;
            smp_c1(le->tk, le->mrand, le->preq, le->pres, 0, lg.own, le->peer_type, le->peer, p + 1);
            smp_send(p, 17);
            le->smp = SMP_WAIT_CONFIRM;
        }
        break;
    }
    case SMP_WAIT_PK:
        if (code != SMP_PUBLIC_KEY || len < 65)
            return smp_fail(0x08, "no public key");
        memcpy(le->peer_pub, d + 1, 64);
        if (memcmp(le->peer_pub, le->pub, 64) == 0 ||
            smp_p256_dhkey(le->priv, le->peer_pub, le->dhkey, rng, NULL) != 0)
            return smp_fail(0x0B, "bad public key");         /* DHKey check failed */
        le->round = 0;
        if (le->passkey_method) {
            sc_send_confirm();
        } else {
            rng_read(le->mrand, 16);
            le->smp = SMP_WAIT_CONFIRM;       /* Just Works: the device confirms first */
        }
        break;
    case SMP_WAIT_CONFIRM:
        if (code != SMP_CONFIRM || len < 17)
            return smp_fail(0x08, "no confirm value");
        memcpy(le->sconfirm, d + 1, 16);
        send_random();
        break;
    case SMP_WAIT_RANDOM: {
        if (code != SMP_RANDOM || len < 17)
            return smp_fail(0x08, "no random value");
        memcpy(le->srand, d + 1, 16);
        uint8_t c[16];
        if (le->sc) {
            uint8_t z = le->passkey_method ? (uint8_t)(0x80 | ((le->passkey >> le->round) & 1)) : 0;
            smp_f4(le->peer_pub, le->pub, le->srand, z, c);
            if (memcmp(c, le->sconfirm, 16) != 0)
                return smp_fail(0x04, "the confirm value does not match (wrong code?)");
            if (le->passkey_method && ++le->round < 20)
                return sc_send_confirm();
            sc_dhkey_check();
        } else {
            smp_c1(le->tk, le->srand, le->preq, le->pres, 0, lg.own, le->peer_type, le->peer, c);
            if (memcmp(c, le->sconfirm, 16) != 0)
                return smp_fail(0x04, "the confirm value does not match (wrong code?)");
            uint8_t stk[16], zero[8] = { 0 };
            smp_s1(le->tk, le->srand, le->mrand, stk);
            mask_key(stk);
            start_encryption(stk, 0, zero);
            le->smp = SMP_WAIT_ENC;
        }
        break;
    }
    case SMP_WAIT_CHECK: {
        if (code != SMP_DHKEY_CHECK || len < 17)
            return smp_fail(0x08, "no DHKey check");
        uint8_t a[7], b[7], r[16], io[3], e[16];
        our_a(a);
        peer_a(b);
        memset(r, 0, 16);
        if (le->passkey_method) {
            r[0] = (uint8_t)le->passkey;
            r[1] = (uint8_t)(le->passkey >> 8);
            r[2] = (uint8_t)(le->passkey >> 16);
        }
        memcpy(io, le->pres + 1, 3);
        smp_f6(le->mackey, le->srand, le->mrand, r, io, b, a, e);
        if (memcmp(e, d + 1, 16) != 0)
            return smp_fail(0x0B, "DHKey check failed");
        mask_key(le->ltk);
        uint8_t zero[8] = { 0 };
        start_encryption(le->ltk, 0, zero);
        le->smp = SMP_WAIT_ENC;
        break;
    }
    case SMP_WAIT_KEYS:
        switch (code) {
        case SMP_ENC_INFO: if (len >= 17) memcpy(le->nb.ltk, d + 1, 16); break;
        case SMP_MASTER_ID:
            if (len >= 11) {
                le->nb.ediv = get16(d + 1);
                memcpy(le->nb.rand, d + 3, 8);
            }
            break;
        case SMP_ID_INFO:
            if (len >= 17) {
                memcpy(le->nb.irk, d + 1, 16);
                le->nb.have_irk = 1;
            }
            break;
        case SMP_ID_ADDR:
            if (len >= 8) {
                le->nb.type = d[1];
                memcpy(le->nb.addr, d + 2, 6);
            }
            break;
        case SMP_SIGN_INFO: break;
        default: return;
        }
        if (le->keys_left && --le->keys_left == 0)
            le->smp = SMP_DONE;
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- GATT client */

static void gatt_fail(const char *why)
{
    kprintf("\x1b[91mbt: %s: %s\x1b[0m\n", kind_name(), why);
    le->g = G_FAILED;
    disconnect(0x13);
}

static void find_service(void)
{
    uint8_t p[9] = { 0x06, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28, 0x12, 0x18 };  /* HID service */
    le->g = G_SERVICE;
    att_send(p, 9);
}

static void read_by_type(uint16_t start, uint16_t end, uint16_t type)
{
    uint8_t p[7] = { 0x08 };
    put16(p + 1, start);
    put16(p + 3, end);
    put16(p + 5, type);
    att_send(p, 7);
}

static void find_info(uint16_t start, uint16_t end)
{
    uint8_t p[5] = { 0x04 };
    put16(p + 1, start);
    put16(p + 3, end);
    att_send(p, 5);
}

static void read_handle(uint16_t h)
{
    uint8_t p[3] = { 0x0A };
    put16(p + 1, h);
    att_send(p, 3);
}

static void read_blob(uint16_t h, uint16_t off)
{
    uint8_t p[5] = { 0x0C };
    put16(p + 1, h);
    put16(p + 3, off);
    att_send(p, 5);
}

static void write_handle(uint16_t h, const uint8_t *v, uint16_t n)
{
    uint8_t p[8] = { 0x12 };
    put16(p + 1, h);
    memcpy(p + 3, v, n);
    att_send(p, (uint16_t)(3 + n));
}

static gchar_t *char_by_uuid(uint16_t uuid)
{
    for (int i = 0; i < le->nch; i++)
        if (le->ch[i].uuid == uuid)
            return &le->ch[i];
    return NULL;
}

/* descriptors belong to the characteristic just before them */
static void assign_descriptors(void)
{
    for (int k = 0; k < le->ndesc; k++) {
        gchar_t *owner = NULL;
        for (int i = 0; i < le->nch; i++)
            if (le->ch[i].value < le->desc_handle[k] && (!owner || le->ch[i].value > owner->value))
                owner = &le->ch[i];
        if (!owner)
            continue;
        if (le->desc_uuid[k] == 0x2902)
            owner->cccd = le->desc_handle[k];
        else if (le->desc_uuid[k] == 0x2908)
            owner->ref = le->desc_handle[k];
    }
}

/* the next Report characteristic whose Report Reference is to be read */
static int next_ref(int from)
{
    for (int i = from; i < le->nch; i++)
        if (le->ch[i].uuid == 0x2A4D && le->ch[i].ref)
            return i;
    return -1;
}

static void subscribe(void)
{
    uint8_t on[2] = { 0x01, 0x00 };
    if (!le->kbd_cccd)
        return gatt_fail(le->kind == LE_MOUSE ? "its mouse report cannot notify" :
                                                "its keyboard report cannot notify");
    le->g = G_CCCD;
    write_handle(le->kbd_cccd, on, 2);
}

/* Protocol Mode = boot (Write Command: no answer) */
static void boot_protocol(void)
{
    gchar_t *pm = char_by_uuid(0x2A4E);
    le->boot = 1;
    if (pm) {
        le->g = G_PROTO;
        uint8_t p[4] = { 0x52 };
        put16(p + 1, pm->value);
        p[3] = 0;
        att_send(p, 4);
    }
}

/* the input Report characteristic with this report ID, that can notify */
static void find_report(uint8_t id)
{
    for (int i = 0; i < le->nch; i++) {
        gchar_t *c = &le->ch[i];
        if (c->uuid == 0x2A4D && (c->props & 0x10) && c->type == 1 && (c->id == id || !c->ref)) {
            le->kbd_value = c->value;
            le->kbd_cccd = c->cccd;
            return;
        }
    }
}

/* the report map is read: which report is the mouse (M32): buttons, X, Y,
 * wheel; else the boot mouse report, in boot protocol */
static void choose_mouse_report(void)
{
    int ok = hid_mouse_layout(le->map, le->map_len, &le->mlayout);
    trace("report map %u bytes: mouse %s, report ID %u", le->map_len, ok ? "found" : "not found",
          le->mlayout.id);
    if (ok)
        find_report(le->mlayout.id);
    le->mlayout.id = 0;                         /* notifications have no report ID */
    if (!le->kbd_value) {
        gchar_t *b = char_by_uuid(0x2A33);
        if (!b)
            return gatt_fail("no mouse report found");
        hid_mouse_boot_layout(&le->mlayout, 0);
        le->kbd_value = b->value;
        le->kbd_cccd = b->cccd;
        boot_protocol();
    }
    subscribe();
}

/* the report map is read: which report is the keyboard */
static void choose_report(void)
{
    if (le->kind == LE_MOUSE)
        return choose_mouse_report();
    int ok = hid_kbd_layout(le->map, le->map_len, &le->layout);
    trace("report map %u bytes: keyboard %s, report ID %u", le->map_len, ok ? "found" : "not found",
          le->layout.id);
    if (ok)
        find_report(le->layout.id);
    if (!le->kbd_value) {                        /* the boot keyboard report, in boot protocol */
        gchar_t *b = char_by_uuid(0x2A22);
        if (!b)
            return gatt_fail("no keyboard report found");
        static const uint8_t boot_map[] = {     /* the boot report: mods, reserved, 6 keys */
            0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
            0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
            0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
            0x81, 0x00, 0xC0 };
        hid_kbd_layout(boot_map, sizeof boot_map, &le->layout);
        le->kbd_value = b->value;
        le->kbd_cccd = b->cccd;
        boot_protocol();
    }
    subscribe();
}

static void gatt_next_after_descs(void)
{
    assign_descriptors();
    le->ref_i = next_ref(0);
    if (le->ref_i >= 0) {
        le->g = G_REFS;
        read_handle(le->ch[le->ref_i].ref);
        return;
    }
    gchar_t *m = char_by_uuid(0x2A4B);
    if (!m)
        return gatt_fail("no report map");
    le->map_handle = m->value;
    le->map_len = 0;
    le->g = G_MAP;
    read_handle(le->map_handle);
}

static void handle_att_response(const uint8_t *d, uint16_t len)
{
    uint8_t op = d[0];
    int err = op == 0x01;
    uint8_t ecode = err && len >= 5 ? d[4] : 0;
    le->att_req = 0;
    switch (le->g) {
    case G_SERVICE:
        if (op != 0x07 || len < 5)
            return gatt_fail(le->kind == LE_MOUSE ? "no HID service (is it a mouse?)" :
                                                    "no HID service (is it a keyboard?)");
        le->svc_start = get16(d + 1);
        le->svc_end = get16(d + 3);
        trace("HID service %04x-%04x", le->svc_start, le->svc_end);
        le->nch = 0;
        le->cursor = le->svc_start;
        le->g = G_CHARS;
        read_by_type(le->cursor, le->svc_end, 0x2803);
        break;
    case G_CHARS:
        if (op == 0x09 && len >= 2) {
            uint8_t n = d[1];
            uint16_t last = le->cursor;
            for (uint16_t i = 2; n >= 7 && i + n <= len; i = (uint16_t)(i + n)) {
                const uint8_t *e = d + i;
                if (le->nch < MAX_CHARS) {
                    gchar_t *c = &le->ch[le->nch++];
                    memset(c, 0, sizeof *c);
                    c->decl = get16(e);
                    c->props = e[2];
                    c->value = get16(e + 3);
                    c->uuid = n == 7 ? get16(e + 5) : 0;       /* 128-bit ones: not HID */
                }
                last = get16(e);
            }
            if (last < le->svc_end) {
                le->cursor = (uint16_t)(last + 1);
                read_by_type(le->cursor, le->svc_end, 0x2803);
                return;
            }
        } else if (!err || ecode != 0x0A) {
            return gatt_fail("characteristics not readable");
        }
        trace("%d characteristics", le->nch);
        le->ndesc = 0;
        le->cursor = le->svc_start;
        le->g = G_DESCS;
        find_info(le->cursor, le->svc_end);
        break;
    case G_DESCS:
        if (op == 0x05 && len >= 2) {
            uint16_t last = le->cursor;
            int step = d[1] == 1 ? 4 : 18;
            for (uint16_t i = 2; i + step <= len; i = (uint16_t)(i + step)) {
                last = get16(d + i);
                if (d[1] == 1 && le->ndesc < MAX_DESCS) {
                    uint16_t u = get16(d + i + 2);
                    if (u == 0x2902 || u == 0x2908) {
                        le->desc_handle[le->ndesc] = last;
                        le->desc_uuid[le->ndesc++] = u;
                    }
                }
            }
            if (last < le->svc_end) {
                le->cursor = (uint16_t)(last + 1);
                find_info(le->cursor, le->svc_end);
                return;
            }
        } else if (!err || ecode != 0x0A) {
            return gatt_fail("descriptors not readable");
        }
        gatt_next_after_descs();
        break;
    case G_REFS: {
        gchar_t *c = &le->ch[le->ref_i];
        if (op == 0x0B && len >= 3) {
            c->id = d[1];
            c->type = d[2];
        } else if (err && (ecode == 0x05 || ecode == 0x0F)) {
            return gatt_fail("not allowed to read it (encryption)");
        }
        le->ref_i = next_ref(le->ref_i + 1);
        if (le->ref_i >= 0) {
            read_handle(le->ch[le->ref_i].ref);
            return;
        }
        gchar_t *m = char_by_uuid(0x2A4B);
        if (!m)
            return gatt_fail("no report map");
        le->map_handle = m->value;
        le->map_len = 0;
        le->g = G_MAP;
        read_handle(le->map_handle);
        break;
    }
    case G_MAP:
        if ((op == 0x0B || op == 0x0D) && len >= 1) {
            uint16_t n = (uint16_t)(len - 1);
            if (le->map_len + n > sizeof le->map)
                n = (uint16_t)(sizeof le->map - le->map_len);
            memcpy(le->map + le->map_len, d + 1, n);
            le->map_len = (uint16_t)(le->map_len + n);
            if (len - 1 == ATT_MTU - 1 && le->map_len < sizeof le->map) {
                read_blob(le->map_handle, le->map_len);
                return;
            }
        } else if (!(err && (ecode == 0x07 || ecode == 0x0B))) {   /* end of the value */
            return gatt_fail("report map not readable");
        }
        choose_report();
        break;
    case G_CCCD:
        if (op != 0x13)
            return gatt_fail("notifications refused");
        le->g = G_READY;
        {
            char as[18];
            addr_str(as, le->peer);
            kprintf("\x1b[92mbt: %s %s connected%s%s\x1b[0m\n", kind_name(),
                    le->name[0] ? le->name : as, le->boot ? " (boot protocol)" : "",
                    le->kind == LE_MOUSE ? ", it moves the pointer" : ", ready to type");
        }
        le->announced = 1;
        break;
    default:
        break;
    }
}

/* The device asks our (empty) GATT server something: polite refusals. */
static void att_server(const uint8_t *d, uint16_t len)
{
    uint8_t r[5];
    switch (d[0]) {
    case 0x02:                                  /* Exchange MTU */
        r[0] = 0x03;
        put16(r + 1, ATT_MTU);
        l2_send(CID_ATT, r, 3);
        return;
    case 0x1D:                                  /* indication: confirm it */
        r[0] = 0x1E;
        l2_send(CID_ATT, r, 1);
        return;
    case 0x04: case 0x06: case 0x08: case 0x10:
        r[4] = 0x0A;                            /* attribute not found */
        break;
    case 0x0A: case 0x0C: case 0x0E: case 0x12: case 0x16: case 0x18:
        r[4] = 0x01;                            /* invalid handle */
        break;
    default:
        r[4] = 0x06;                            /* request not supported */
        break;
    }
    if (d[0] & 0x40)                            /* commands get no answer */
        return;
    r[0] = 0x01;
    r[1] = d[0];
    put16(r + 2, len >= 3 ? get16(d + 1) : 0);
    l2_send(CID_ATT, r, 5);
}

static void handle_att(const uint8_t *d, uint16_t len)
{
    if (len < 1)
        return;
    uint8_t op = d[0];
    if (op == 0x1B) {                           /* Handle Value Notification */
        if (le->g == G_READY && len >= 3 && get16(d + 1) == le->kbd_value) {
            if (le->kind == LE_MOUSE)
                hid_mouse_report(HID_MOUSE_BLE, &le->mlayout, d + 3, (uint32_t)(len - 3));
            else
                hid_ble_keyboard(&le->layout, d + 3, (uint32_t)(len - 3));
        }
        return;
    }
    int response = op == 0x01 || op == 0x03 || op == 0x05 || op == 0x07 || op == 0x09 ||
                   op == 0x0B || op == 0x0D || op == 0x0F || op == 0x11 || op == 0x13 || op == 0x17 ||
                   op == 0x19;
    if (response) {
        if (le->att_req)
            handle_att_response(d, len);
        return;
    }
    att_server(d, len);
}

static void gatt_start(void)
{
    le->g = G_NONE;
    le->nch = le->ndesc = 0;
    le->kbd_value = le->kbd_cccd = 0;
    le->boot = 0;
    find_service();
}

/* ---------------------------------------------------------------- L2CAP LE */

static void handle_le_sig(const uint8_t *d, uint16_t len)
{
    if (len < 4)
        return;
    uint8_t code = d[0], id = d[1];
    if (code == 0x12 && len >= 12) {            /* Connection Parameter Update Request */
        uint8_t r[6] = { 0x13, id, 2, 0, 0, 0 };       /* accepted */
        l2_send(CID_LE_SIG, r, 6);
        uint8_t p[14];
        put16(p, le->handle);
        memcpy(p + 2, d + 4, 8);                /* interval min, max, latency, timeout */
        put16(p + 10, 0);
        put16(p + 12, 0);
        hci_send(LE_CONN_UPDATE, p, 14);
        trace("connection parameters: interval %u..%u, latency %u", get16(d + 4), get16(d + 6), get16(d + 8));
    } else if (!(code & 1) && code != 0x16) {  /* another request: not understood */
        uint8_t r[6] = { 0x01, id, 2, 0, 0, 0 };
        l2_send(CID_LE_SIG, r, 6);
    }
}

/* the connected device with this handle, or NULL */
static le_dev_t *by_handle(uint16_t h)
{
    for (int k = 0; k < LE_DEVS; k++)
        if (lg.dev[k].state == CONNECTED && lg.dev[k].handle == h)
            return &lg.dev[k];
    return NULL;
}

static int acl(const hci_pkt_t *p)
{
    if (p->len < 4 || !(le = by_handle(get16(p->data) & 0x0FFF)))
        return 0;
    uint8_t pb = (p->data[1] >> 4) & 3;
    uint16_t n = get16(p->data + 2);
    const uint8_t *d = p->data + 4;
    if (n + 4u > p->len)
        return 1;
    if (pb != 1) {                              /* start of a frame */
        le->rx_len = 0;
        le->rx_want = n >= 2 ? (uint16_t)(get16(d) + 4) : 0;
    }
    if (le->rx_len + n > sizeof le->rx) {
        le->rx_len = le->rx_want = 0;
        return 1;
    }
    memcpy(le->rx + le->rx_len, d, n);
    le->rx_len = (uint16_t)(le->rx_len + n);
    if (!le->rx_want || le->rx_len < le->rx_want)
        return 1;
    uint16_t cid = get16(le->rx + 2), l2len = (uint16_t)(le->rx_want - 4);
    le->rx_len = le->rx_want = 0;
    if (cid == CID_SMP)
        handle_smp(le->rx + 4, l2len);
    else if (cid == CID_ATT)
        handle_att(le->rx + 4, l2len);
    else if (cid == CID_LE_SIG)
        handle_le_sig(le->rx + 4, l2len);
    return 1;
}

/* ---------------------------------------------------------------- events */

/* AD structures: flags, 16-bit service UUIDs, name, appearance */
typedef struct { int hid, keyboard, mouse, discoverable; char name[32]; } adv_t;

static void parse_adv(const uint8_t *d, uint8_t n, adv_t *a)
{
    for (int i = 0; i + 1 < n;) {
        uint8_t l = d[i], t = d[i + 1];
        if (l == 0 || i + 1 + l > n)
            break;
        const uint8_t *v = d + i + 2;
        int vl = l - 1;
        if (t == 0x01 && vl >= 1)
            a->discoverable = (v[0] & 0x03) != 0;
        else if ((t == 0x02 || t == 0x03) && vl >= 2)
            for (int k = 0; k + 1 < vl; k += 2)
                a->hid |= get16(v + k) == 0x1812;
        else if ((t == 0x08 || t == 0x09) && vl > 0) {
            int m = vl < 31 ? vl : 31;
            memcpy(a->name, v, (size_t)m);
            a->name[m] = 0;
        } else if (t == 0x19 && vl >= 2) {
            a->keyboard = get16(v) == 0x03C1;
            a->mouse = get16(v) == 0x03C2;
        }
        i += 1 + l;
    }
}

/* pairing: the device found by the scan, the other devices seen */
static struct { int found; uint8_t addr[6], type; char name[32]; uint32_t at; } cand;
static uint8_t seen[8][6];
static int nseen;

/* a paired device that advertises: connect to it (one at a time) */
static int come_back(const uint8_t *addr, uint8_t type)
{
    for (int k = 0; k < LE_DEVS; k++)
        if (lg.dev[k].state == CONNECTING)
            return 0;
    for (int k = 0; k < LE_DEVS; k++) {
        le = &lg.dev[k];
        if (le->state == SCANNING && is_ours(addr, type)) {
            char as[18];
            addr_str(as, addr);
            trace("%s %s is back", kind_name(), as);
            connect_to(addr, type);
            return 1;
        }
    }
    return 0;
}

/* the other paired device's address: not a candidate */
static int other_ours(const uint8_t *addr, uint8_t type)
{
    int r = 0;
    for (int k = 0; k < LE_DEVS; k++)
        if (&lg.dev[k] != pd) {
            le = &lg.dev[k];
            r |= is_ours(addr, type);
        }
    le = pd;
    return r;
}

static void adv_report(const uint8_t *e, uint8_t len)
{
    /* reports one after the other: type, address type, address, data, RSSI */
    uint8_t n = e[0];
    const uint8_t *r = e + 1;
    const uint8_t *end = e + len;
    for (int i = 0; i < n && r + 9 <= end; i++) {
        uint8_t evt = r[0], type = r[1], dl = r[8];
        const uint8_t *addr = r + 2, *data = r + 9;
        if (data + dl + 1 > end)
            break;
        r = data + dl + 1;
        if (!lg.pairing) {
            if (evt <= 1 && come_back(addr, type))
                return;
            continue;
        }
        le = pd;
        adv_t a;
        memset(&a, 0, sizeof a);
        parse_adv(data, dl, &a);
        if (cand.found && memcmp(cand.addr, addr, 6) == 0) {
            if (a.name[0] && !cand.name[0])
                memcpy(cand.name, a.name, sizeof cand.name);
            continue;
        }
        int known = 0;
        for (int k = 0; k < nseen; k++)
            known |= memcmp(seen[k], addr, 6) == 0;
        if (!known && nseen < 8) {
            memcpy(seen[nseen++], addr, 6);
            char as[18];
            addr_str(as, addr);
            trace("seen %s%s%s%s", as, a.name[0] ? " " : "", a.name,
                  a.hid || a.keyboard ? " (HID)" : "");
        }
        int kind_ok = pd->kind == LE_MOUSE ? a.mouse || (a.hid && !a.keyboard)
                                           : (a.hid || a.keyboard) && !a.mouse;
        if (!cand.found && kind_ok && (a.discoverable || evt == 4) && !other_ours(addr, type)) {
            cand.found = 1;
            memcpy(cand.addr, addr, 6);
            cand.type = type;
            memcpy(cand.name, a.name, sizeof cand.name);
            cand.at = timer_ticks();
            char as[18];
            addr_str(as, addr);
            kprintf("bt: found %s %s%s%s (%s address)\n", kind_name(), as, a.name[0] ? " " : "", a.name,
                    type ? "random" : "public");
        }
    }
}

static void connected(const uint8_t *e)
{
    /* status, handle, role, peer type, peer address, interval, latency, timeout, accuracy */
    if (e[0] != 0) {
        trace("connection failed (%02x)", e[0]);
        set_state(IDLE);
        return;
    }
    le->handle = get16(e + 1) & 0x0FFF;
    le->peer_type = e[4];
    memcpy(le->peer, e + 5, 6);
    le->enc = 0;
    le->announced = 0;
    le->rx_len = le->rx_want = 0;
    le->g = G_NONE;
    le->att_req = 0;
    set_state(CONNECTED);
    trace("LE connection %03x, interval %u", le->handle, get16(e + 11));
    if (pairing_me())
        smp_start();
    else if (le->have_bond)
        start_encryption(le->bond.ltk, le->bond.ediv, le->bond.rand);
}

static void link_down(uint8_t reason)
{
    if (le->announced) {
        kprintf("bt: %s disconnected after %u ms: %s (reason %02x)\n", kind_name(), (unsigned)since_ms(),
                hci_reason(reason), reason);
    } else if (!pairing_me()) {
        trace("%s link lost after %u ms: %s (reason %02x)", kind_name(), (unsigned)since_ms(),
              hci_reason(reason), reason);
    }
    if (le->kind == LE_MOUSE)
        hid_mouse_clear(HID_MOUSE_BLE);
    else
        hid_ble_keyboard_clear();
    le->enc = 0;
    le->announced = 0;
    le->g = G_NONE;
    le->att_req = 0;
    if (pairing_me() && le->smp != SMP_DONE && le->smp != SMP_FAILED_)
        le->smp = SMP_FAILED_;
    set_state(IDLE);
}

static int event(const hci_pkt_t *p)
{
    uint8_t code = p->data[0], plen = p->data[1];
    const uint8_t *e = p->data + 2;
    if (code == 0x3E && plen >= 1) {            /* LE Meta */
        switch (e[0]) {
        case 0x01:                              /* the connection being made */
            for (int k = 0; k < LE_DEVS; k++)
                if (lg.dev[k].state == CONNECTING) {
                    le = &lg.dev[k];
                    connected(e + 1);
                    break;
                }
            break;
        case 0x02: adv_report(e + 1, (uint8_t)(plen - 1)); break;
        default: break;
        }
        return 1;
    }
    if (plen < 3 || !(le = by_handle(get16(e + 1) & 0x0FFF)))
        return 0;
    switch (code) {
    case 0x05:                                  /* Disconnection Complete */
        link_down(plen >= 4 ? e[3] : 0);
        return 1;
    case 0x08:                                  /* Encryption Change */
    case 0x30:                                  /* Encryption Key Refresh Complete */
        if (e[0] != 0) {
            if (pairing_me()) {
                kprintf("\x1b[91mbt: encryption failed (%02x)\x1b[0m\n", e[0]);
                le->smp = SMP_FAILED_;
            } else {
                kprintf("\x1b[91mbt: the %s does not know this console anymore (%02x):\n"
                        "    pair it again with %c\x1b[0m\n", kind_name(), e[0],
                        le->kind == LE_MOUSE ? 'O' : 'K');
                le->bond_bad = 1;
            }
            disconnect(0x05);
            return 1;
        }
        if (code == 0x08 && !e[3])
            return 1;
        le->enc = 1;
        trace("encrypted");
        if (pairing_me() && le->smp == SMP_WAIT_ENC)
            pairing_encrypted();
        else if (!pairing_me() && le->g == G_NONE)
            gatt_start();
        return 1;
    default:
        return 0;
    }
}

/* The entry points from bt.c handle one device each (le); the device
 * being worked on before (a pairing) is kept. */
int ble_event(const hci_pkt_t *p)
{
    le_dev_t *keep = le;
    int r = event(p);
    le = keep;
    return r;
}

int ble_acl(const hci_pkt_t *p)
{
    le_dev_t *keep = le;
    int r = acl(p);
    le = keep;
    return r;
}

/* ---------------------------------------------------------------- polling */

static void poll_dev(void)
{
    switch (le->state) {
    case IDLE:
        if (le->have_bond && !le->bond_bad && !lg.pairing)
            set_state(SCANNING);                /* wait for it to come back */
        break;
    case CONNECTING:
        if (since_ms() > 8000) {
            hci_send(LE_CREATE_CONN_CANCEL, NULL, 0);
            trace("the %s did not answer", kind_name());
            set_state(IDLE);
        }
        break;
    case CONNECTED:
        if (le->att_req && timer_ticks() - le->att_sent > 10000000u)
            gatt_fail("no answer");
        else if (!pairing_me() && !le->enc && since_ms() > 10000)
            disconnect(0x13);
        break;
    default:
        break;
    }
}

void ble_poll(void)
{
    if (!lg.ready)
        return;
    le_dev_t *keep = le;
    int connecting = 0, waiting = 0;
    for (int k = 0; k < LE_DEVS; k++) {
        le = &lg.dev[k];
        poll_dev();
        connecting |= le->state == CONNECTING;
        waiting |= le->state == SCANNING;
    }
    /* one passive scan for the devices that are away, off while a
     * connection is being made (pairing scans by itself) */
    int want = waiting && !connecting;
    if (!lg.pairing && want != lg.scan_on)
        scan(want, 0);
    le = keep;
}

/* ---------------------------------------------------------------- setup, pairing */

void ble_init(const uint8_t addr[6])
{
    static const uint8_t mask[8] = { 0x1F, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t b[3] = { 0 };
    memcpy(lg.own, addr, 6);
    hci_cmd(LE_SET_EVENT_MASK, mask, 8, NULL, 0, 500000);
    hci_cmd(LE_READ_BUFFER_SIZE, NULL, 0, b, 3, 500000);
    lg.acl_len = get16(b);
    if (!lg.acl_len) {                          /* shared with BR/EDR */
        uint8_t r[7] = { 0 };
        hci_cmd(HCI_READ_BUFFER_SIZE, NULL, 0, r, 7, 500000);
        lg.acl_len = get16(r);
    }
    if (!lg.acl_len || lg.acl_len > 251)
        lg.acl_len = 27;
    for (int k = 0; k < LE_DEVS; k++) {
        le = &lg.dev[k];
        le->kind = k;
        load_bond();
        set_state(IDLE);
        if (le->have_bond) {
            char as[18];
            addr_str(as, le->bond.addr);
            kprintf(k == LE_MOUSE ? "bt: paired mouse %s: move it or click to connect\n"
                                  : "bt: paired keyboard %s: press a key on it to connect\n", as);
        }
    }
    le = &lg.dev[LE_KBD];
    lg.ready = 1;
}

/* Runs the stack until cond() holds or ms pass; 0 if it holds. */
static int wait_until(int (*cond)(void), uint32_t ms)
{
    uint32_t t0 = timer_ticks();
    while (!cond()) {
        if (timer_ticks() - t0 > ms * 1000u)
            return -1;
        bt_pump(20000);
        ble_poll();
    }
    return 0;
}

static int c_candidate(void) { return cand.found && timer_ticks() - cand.at > 1500000u; }
static int c_connected(void) { return le->state == CONNECTED || le->state == IDLE; }
static int c_smp_end(void) { return le->smp == SMP_DONE || le->smp == SMP_FAILED_ || le->state != CONNECTED; }
static int c_gatt_end(void) { return le->g == G_READY || le->g == G_FAILED || le->state != CONNECTED; }

int ble_pair(int kind, unsigned seconds)
{
    if (!lg.ready || kind < 0 || kind >= LE_DEVS)
        return -1;
    pd = le = &lg.dev[kind];
    if (le->state == CONNECTED) {
        disconnect(0x13);
        wait_until(c_connected, 2000);
    }
    for (int k = 0; k < LE_DEVS; k++)           /* one connection at a time: ours */
        if (lg.dev[k].state == CONNECTING) {
            hci_send(LE_CREATE_CONN_CANCEL, NULL, 0);
            le = &lg.dev[k];
            set_state(IDLE);
        }
    le = pd;
    lg.pairing = 1;
    memset(&cand, 0, sizeof cand);
    nseen = 0;
    if (kind == LE_MOUSE)
        kprintf("bt: looking for a mouse for %u s (MX mice: hold the Easy-Switch button 3 s,\n"
                "    until its light blinks fast)\n", seconds);
    else
        kprintf("bt: looking for a keyboard for %u s (MX Keys: hold an Easy-Switch key 3 s,\n"
                "    until its light blinks fast)\n", seconds);
    scan(0, 0);
    scan(1, 1);
    set_state(SCANNING);
    int r = -1;
    if (wait_until(c_candidate, seconds * 1000u + 1500) != 0) {
        scan(0, 0);
        if (cand.found) {
            kprintf("bt: the %s disappeared\n", kind_name());
        } else {
            kprintf("bt: no %s in pairing mode found\n", kind_name());
            r = -2;
        }
        set_state(IDLE);
        goto out;
    }
    memcpy(le->name, cand.name, sizeof le->name);
    kprintf("bt: pairing with %s...\n", cand.name[0] ? cand.name : kind == LE_MOUSE ? "the mouse" : "the keyboard");
    le->smp = SMP_NONE;
    connect_to(cand.addr, cand.type);
    if (wait_until(c_connected, 9000) != 0 || le->state != CONNECTED) {
        if (le->state == CONNECTING)
            hci_send(LE_CREATE_CONN_CANCEL, NULL, 0);
        kprintf("\x1b[91mbt: cannot connect to the %s (is it still blinking?)\x1b[0m\n", kind_name());
        set_state(IDLE);
        goto out;
    }
    if (wait_until(c_smp_end, 90000) != 0 || le->smp != SMP_DONE) {
        if (le->smp != SMP_FAILED_)
            kprintf("\x1b[91mbt: pairing did not finish (%s)\x1b[0m\n",
                    le->state != CONNECTED ? (kind == LE_MOUSE ? "the mouse disconnected" :
                                              "the keyboard disconnected") : "timeout");
        disconnect(0x05);
        goto out;
    }
    save_bond(&le->nb);
    {
        char as[18];
        addr_str(as, le->nb.addr);
        kprintf("bt: %s %s paired (%s%s)\n", kind_name(), as, le->sc ? "Secure Connections" : "legacy",
                le->nb.have_irk ? ", private address" : "");
    }
    gatt_start();
    if (wait_until(c_gatt_end, 20000) != 0 || le->g != G_READY) {
        if (le->g != G_FAILED)
            kprintf("\x1b[91mbt: the %s's HID service did not answer\x1b[0m\n", kind_name());
        goto out;
    }
    r = 0;
out:
    lg.pairing = 0;
    pd = NULL;
    le = &lg.dev[LE_KBD];
    return r;
}
