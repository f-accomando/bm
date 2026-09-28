/*
 * HID devices: boot-protocol keyboards (Italian or US layout), generic
 * HID gamepads (report descriptor parsed for buttons, X/Y and hat switch)
 * and Xbox 360 wired controllers.
 */
#include "hid.h"
#include "usb.h"
#include "drivers/timer.h"

#include <string.h>

/* ---------------------------------------------------------------- keyboard */

#define MOD_CTRL    0x11
#define MOD_SHIFT   0x22
#define MOD_ALTGR   0x44            /* either Alt: Option on Apple keyboards, left or right */

/* layouts: [usage] = { normal, shift, altgr }; 0 = nothing. Accented
 * letters use code page 437, the console font. */
typedef struct { uint8_t n, s, a; } keydef_t;

static const keydef_t layout_it[0x65] = {
    [0x1E] = {'1', '!', 0},   [0x1F] = {'2', '"', 0},    [0x20] = {'3', 0x9C, 0},  /* £ */
    [0x21] = {'4', '$', 0},   [0x22] = {'5', '%', 0},    [0x23] = {'6', '&', 0},
    [0x24] = {'7', '/', '{'}, [0x25] = {'8', '(', '['},  [0x26] = {'9', ')', ']'},
    [0x27] = {'0', '=', '}'},
    [0x2C] = {' ', ' ', ' '},
    [0x2D] = {'\'', '?', '`'}, [0x2E] = {0x8D, '^', '~'},                         /* ì */
    [0x2F] = {0x8A, 0x82, '['}, [0x30] = {'+', '*', ']'},                          /* è é */
    [0x31] = {0x97, 0x15, 0}, [0x32] = {0x97, 0x15, 0},                            /* ù § */
    [0x33] = {0x95, 0x87, '@'}, [0x34] = {0x85, 0xF8, '#'},                        /* ò ç à ° */
    [0x35] = {'\\', '|', 0},  [0x36] = {',', ';', 0},    [0x37] = {'.', ':', 0},
    [0x38] = {'-', '_', 0},   [0x64] = {'<', '>', 0},
};

static const keydef_t layout_us[0x65] = {
    [0x1E] = {'1', '!', 0}, [0x1F] = {'2', '@', 0}, [0x20] = {'3', '#', 0}, [0x21] = {'4', '$', 0},
    [0x22] = {'5', '%', 0}, [0x23] = {'6', '^', 0}, [0x24] = {'7', '&', 0}, [0x25] = {'8', '*', 0},
    [0x26] = {'9', '(', 0}, [0x27] = {'0', ')', 0}, [0x2C] = {' ', ' ', 0},
    [0x2D] = {'-', '_', 0}, [0x2E] = {'=', '+', 0}, [0x2F] = {'[', '{', 0}, [0x30] = {']', '}', 0},
    [0x31] = {'\\', '|', 0}, [0x32] = {'\\', '|', 0}, [0x33] = {';', ':', 0}, [0x34] = {'\'', '"', 0},
    [0x35] = {'`', '~', 0}, [0x36] = {',', '<', 0}, [0x37] = {'.', '>', 0}, [0x38] = {'/', '?', 0},
    [0x64] = {'\\', '|', 0},
};

static const keydef_t *layout = layout_it;

static uint8_t prev_keys[8];
static uint8_t queue[64];
static unsigned q_head, q_tail;
static uint32_t kbd_buttons, pad_buttons, bt_buttons;
/* Buttons seen pressed since the last hid_buttons(): a press and release
 * that both arrive between two frames (a quick tap, or a backlog of
 * reports processed at once) still count for one frame. */
static uint32_t latched, latched_pad;   /* short presses, all / pads only */
static int text_mode;                   /* editors: navigation keys as codes, Esc stays */
static int bt_ps_held;
static int quit_edge;
static int caps;

/* key repeat for text */
static uint8_t rep_usage, rep_mods;
static uint32_t rep_next;

void hid_set_layout(const char *name)
{
    layout = (name && name[0] == 'u' && name[1] == 's') ? layout_us : layout_it;
}

const char *hid_layout(void)
{
    return layout == layout_us ? "us" : "it";
}

static void push(uint8_t c)
{
    if ((q_head + 1) % sizeof queue != q_tail) {
        queue[q_head] = c;
        q_head = (q_head + 1) % sizeof queue;
    }
}

/* usage + modifiers -> byte, or 0 */
static uint8_t translate(uint8_t u, uint8_t mods)
{
    int shift = (mods & MOD_SHIFT) != 0, ctrl = (mods & MOD_CTRL) != 0, altgr = (mods & MOD_ALTGR) != 0;
    if (u >= 0x04 && u <= 0x1D) {
        char c = (char)('a' + u - 0x04);
        if (ctrl) return (uint8_t)(c - 'a' + 1);
        if (shift ^ caps) c = (char)(c - 32);
        return (uint8_t)c;
    }
    if (text_mode)
        switch (u) {
        case 0x52: return HID_KEY_UP;
        case 0x51: return HID_KEY_DOWN;
        case 0x50: return HID_KEY_LEFT;
        case 0x4F: return HID_KEY_RIGHT;
        case 0x4A: return HID_KEY_HOME;
        case 0x4D: return HID_KEY_END;
        case 0x4B: return HID_KEY_PGUP;
        case 0x4E: return HID_KEY_PGDN;
        case 0x4C: return HID_KEY_DEL;
        case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E:
            return (uint8_t)(HID_KEY_F1 + (u - 0x3A));
        }
    switch (u) {
    case 0x28: case 0x58: return '\r';          /* Enter, keypad Enter */
    case 0x2A: return 0x7F;                     /* Backspace */
    case 0x2B: return '\t';
    }
    if (u >= 0x59 && u <= 0x62) return (uint8_t)("1234567890"[u - 0x59]);   /* keypad */
    if (u < sizeof layout_it / sizeof *layout_it) {
        const keydef_t *k = &layout[u];
        return altgr ? k->a : shift ? k->s : k->n;
    }
    return 0;
}

static uint32_t key_button(uint8_t u)
{
    switch (u) {
    case 0x50: case 0x04: return HID_LEFT;      /* left arrow, A */
    case 0x4F: case 0x07: return HID_RIGHT;     /* right arrow, D */
    case 0x52: case 0x1A: return HID_UP;        /* up arrow, W */
    case 0x51: case 0x16: return HID_DOWN;      /* down arrow, S */
    case 0x2C: case 0x1D: case 0x0D: return HID_A;  /* space, Z, J */
    case 0x1B: case 0x0E: return HID_B;         /* X, K */
    case 0x06: case 0x0F: return HID_X;         /* C, L */
    case 0x19: case 0x0C: return HID_Y;         /* V, I */
    case 0x28: return HID_START;                /* Enter */
    case 0x2B: return HID_SELECT;               /* Tab */
    }
    return 0;
}

static uint8_t kbd_report_id;

void hid_keyboard_attach(uint8_t report_id)
{
    memset(prev_keys, 0, sizeof prev_keys);
    kbd_buttons = 0;
    kbd_report_id = report_id;
}

int hid_is_keyboard(const uint8_t *d, uint32_t len, uint8_t *report_id)
{
    uint32_t usage_page = 0, usage = 0;
    int in_kbd = 0, depth = 0, found = 0;
    *report_id = 0;
    for (uint32_t i = 0; i < len;) {
        uint8_t prefix = d[i];
        if (prefix == 0xFE) {                   /* long item */
            if (i + 1 >= len) break;
            i += 3 + d[i + 1];
            continue;
        }
        uint32_t sz = prefix & 3;
        if (sz == 3) sz = 4;
        if (i + 1 + sz > len) break;
        uint32_t v = 0;
        for (uint32_t k = 0; k < sz; k++) v |= (uint32_t)d[i + 1 + k] << (8 * k);
        i += 1 + sz;
        switch (prefix & 0xFC) {
        case 0x04: usage_page = v; break;
        case 0x08: usage = sz == 4 ? v & 0xFFFF : v; break;
        case 0xA0:                              /* Collection */
            depth++;
            if (depth == 1 && v == 1 && usage_page == 0x01 && usage == 0x06) {
                in_kbd = 1;
                found = 1;
            }
            break;
        case 0xC0:                              /* End Collection */
            if (depth > 0 && --depth == 0)
                in_kbd = 0;
            break;
        case 0x84:                              /* Report ID */
            if (in_kbd && !*report_id)
                *report_id = (uint8_t)v;
            break;
        }
        if ((prefix & 0x0C) == 0x00)            /* main item: clears local usage */
            usage = 0;
    }
    return found;
}

static void keyboard_report(const uint8_t *r, uint32_t len)
{
    /* A keyboard that ignored SET_PROTOCOL(boot) sends report protocol:
     * [report id] mods reserved keys... ; other report IDs (media keys,
     * battery...) are not ours. */
    if (kbd_report_id && len >= 9) {
        if (r[0] != kbd_report_id)
            return;
        r++;
        len--;
    }
    if (len < 8 || r[2] == 1)                   /* roll-over error: ignore */
        return;
    uint8_t mods = r[0];
    uint32_t buttons = 0;
    for (int i = 2; i < 8; i++) {
        uint8_t u = r[i];
        if (!u) continue;
        buttons |= key_button(u);
        int was = 0;
        for (int j = 2; j < 8; j++) was |= prev_keys[j] == u;
        if (was) continue;
        /* new key */
        if (u == 0x29) {                                            /* Esc */
            if (!text_mode) quit_edge = 1;
            push(0x1B);
            continue;
        }
        if (u == 0x39) { caps = !caps; continue; }                  /* Caps Lock */
        uint8_t c = translate(u, mods);
        if (c) {
            push(c);
            rep_usage = u;
            rep_mods = mods;
            rep_next = timer_ticks() + 500000;
        }
    }
    /* stop repeating when the key is released */
    int held = 0;
    for (int i = 2; i < 8; i++) held |= r[i] == rep_usage;
    if (!held) rep_usage = 0;
    kbd_buttons = buttons;
    latched |= buttons;
    memcpy(prev_keys, r, 8);
}

int hid_getc(void)
{
    if (rep_usage && (int32_t)(timer_ticks() - rep_next) >= 0) {
        uint8_t c = translate(rep_usage, rep_mods);
        if (c) push(c);
        rep_next = timer_ticks() + 33000;       /* ~30 per second */
    }
    if (q_tail == q_head)
        return -1;
    uint8_t c = queue[q_tail];
    q_tail = (q_tail + 1) % sizeof queue;
    return c;
}

uint32_t hid_buttons(void)
{
    uint32_t b = kbd_buttons | pad_buttons | bt_buttons | latched;
    latched = latched_pad = 0;
    return b;
}

int hid_usage_held(uint8_t u)
{
    for (int i = 2; i < 8; i++)
        if (prev_keys[i] == u)
            return 1;
    return 0;
}

uint32_t hid_pad_buttons(void)
{
    uint32_t b = pad_buttons | bt_buttons | latched_pad;
    latched = latched_pad = 0;
    return b;
}

void hid_text_mode(int on)
{
    text_mode = on;
}

void hid_bt_report(const uint8_t *r, uint32_t len)
{
    int off = len && r[0] == 0x01 ? 1 : len && r[0] == 0x11 ? 3 : -1;
    if (off < 0 || len < (uint32_t)off + 7)
        return;
    int ps = 0;
    uint32_t b = hid_ds4_buttons(r + off, len - (uint32_t)off, &ps);
    if ((ps && !bt_ps_held) ||
        ((b & (HID_START | HID_SELECT)) == (HID_START | HID_SELECT) &&
         (bt_buttons & (HID_START | HID_SELECT)) != (HID_START | HID_SELECT)))
        quit_edge = 1;
    bt_ps_held = ps;
    bt_buttons = b;
    latched |= b;
    latched_pad |= b;
}

void hid_bt_clear(void)
{
    bt_buttons = 0;
    bt_ps_held = 0;
}

int hid_quit_pressed(void)
{
    int q = quit_edge;
    quit_edge = 0;
    return q;
}

/* ---------------------------------------------------------------- gamepad */

typedef struct { uint16_t bit, size; } field_t;

static struct {
    uint8_t report_id;          /* 0 = no report IDs */
    field_t buttons[16];
    int nbuttons;
    field_t x, y, hat;
    int32_t x_min, x_max, y_min, y_max, hat_min;
    int have_x, have_y, have_hat;
    int xbox;
    int ds4, ps_held;
} pad;

static uint32_t bits(const uint8_t *d, uint32_t len, field_t f)
{
    uint32_t v = 0;
    for (uint16_t i = 0; i < f.size && i < 32; i++) {
        uint32_t b = f.bit + i;
        if (b / 8 < len && (d[b / 8] >> (b % 8) & 1))
            v |= 1u << i;
    }
    return v;
}

static int32_t sign_extend(uint32_t v, int size)
{
    if (size < 32 && (v & (1u << (size - 1))))
        return (int32_t)(v | ~((1u << size) - 1));
    return (int32_t)v;
}

/* Minimal HID report descriptor parser: first application collection
 * with buttons / X / Y / hat in Input items. */
int hid_gamepad_attach(const uint8_t *d, uint32_t len)
{
    memset(&pad, 0, sizeof pad);
    uint32_t usage_page = 0, rsize = 0, rcount = 0, bitpos = 0;
    int32_t lmin = 0, lmax = 0;
    uint32_t usages[16], nusages = 0, umin = 0, umax = 0;
    int have_range = 0, report_id = -1;

    for (uint32_t i = 0; i < len;) {
        uint8_t prefix = d[i];
        if (prefix == 0xFE) {                   /* long item */
            if (i + 1 >= len) break;
            i += 3 + d[i + 1];
            continue;
        }
        uint32_t sz = prefix & 3;
        if (sz == 3) sz = 4;
        if (i + 1 + sz > len) break;
        uint32_t v = 0;
        for (uint32_t k = 0; k < sz; k++) v |= (uint32_t)d[i + 1 + k] << (8 * k);
        int32_t sv = sz ? sign_extend(v, (int)sz * 8) : 0;
        uint8_t tag = prefix & 0xFC;
        i += 1 + sz;

        switch (tag) {
        case 0x04: usage_page = v; break;                       /* Usage Page */
        case 0x14: lmin = sv; break;                            /* Logical Min */
        case 0x24: lmax = (lmin >= 0 && sz < 4) ? (int32_t)v : sv; break;
        case 0x74: rsize = v; break;                            /* Report Size */
        case 0x94: rcount = v; break;                           /* Report Count */
        case 0x84:                                              /* Report ID */
            if (report_id >= 0 && pad.nbuttons) goto done;      /* only the first report */
            report_id = (int)v; bitpos = 8; break;
        case 0x08: if (nusages < 16) usages[nusages++] = (sz == 4 ? v & 0xFFFF : v); break;
        case 0x18: umin = v; have_range = 1; break;
        case 0x28: umax = v; have_range = 1; break;
        case 0x80:                                              /* Input */
            if (!(v & 1)) {                                     /* data, not constant */
                for (uint32_t n = 0; n < rcount; n++) {
                    field_t f = { (uint16_t)(bitpos + n * rsize), (uint16_t)rsize };
                    uint32_t usage = have_range ? umin + n : (n < nusages ? usages[n] : (nusages ? usages[nusages - 1] : 0));
                    if (usage_page == 0x09 && pad.nbuttons < 16) {
                        pad.buttons[pad.nbuttons++] = f;
                    } else if (usage_page == 0x01 && usage == 0x30 && !pad.have_x) {
                        pad.x = f; pad.x_min = lmin; pad.x_max = lmax; pad.have_x = 1;
                    } else if (usage_page == 0x01 && usage == 0x31 && !pad.have_y) {
                        pad.y = f; pad.y_min = lmin; pad.y_max = lmax; pad.have_y = 1;
                    } else if (usage_page == 0x01 && usage == 0x39 && !pad.have_hat) {
                        pad.hat = f; pad.hat_min = lmin; pad.have_hat = 1;
                    }
                }
            }
            bitpos += rsize * rcount;
            /* local items are cleared after a main item */
            /* fall through */
        case 0x90: case 0xB0: case 0xA0: case 0xC0:
            nusages = 0; have_range = 0; umin = umax = 0;
            break;
        }
    }
done:
    pad.report_id = report_id > 0 ? (uint8_t)report_id : 0;
    pad_buttons = 0;
    return (pad.nbuttons || pad.have_hat || pad.have_x) ? 0 : -1;
}

void hid_xbox360_attach(void)
{
    memset(&pad, 0, sizeof pad);
    pad.xbox = 1;
    pad_buttons = 0;
}

void hid_ds4_attach(void)
{
    memset(&pad, 0, sizeof pad);
    pad.ds4 = 1;
    pad_buttons = 0;
}

/* DualShock 4: after the report ID, d[0..3] sticks (LX LY RX RY, 0..255,
 * 128 = centre), d[4] hat (low nibble, 8 = none) and square/cross/circle/
 * triangle (bits 4..7), d[5] L1 R1 L2 R2 share options L3 R3, d[6] bit 0 PS.
 * USB sends report 0x01 with d at byte 1; Bluetooth report 0x11 at byte 3. */
uint32_t hid_ds4_buttons(const uint8_t *d, uint32_t len, int *ps)
{
    uint32_t b = 0;
    if (len < 7)
        return 0;
    static const uint32_t dirs[8] = {
        HID_UP, HID_UP | HID_RIGHT, HID_RIGHT, HID_RIGHT | HID_DOWN,
        HID_DOWN, HID_DOWN | HID_LEFT, HID_LEFT, HID_LEFT | HID_UP,
    };
    if ((d[4] & 15) < 8) b |= dirs[d[4] & 15];
    if (d[0] < 64) b |= HID_LEFT;
    if (d[0] > 192) b |= HID_RIGHT;
    if (d[1] < 64) b |= HID_UP;
    if (d[1] > 192) b |= HID_DOWN;
    if (d[4] & 0x20) b |= HID_A;                /* cross */
    if (d[4] & 0x10) b |= HID_X;                /* square */
    if (d[4] & 0x40) b |= HID_B;                /* circle */
    if (d[4] & 0x80) b |= HID_Y;                /* triangle */
    if (d[5] & 0x20) b |= HID_START;            /* options */
    if (d[5] & 0x10) b |= HID_SELECT;           /* share */
    *ps = d[6] & 1;
    return b;
}

static uint32_t axis(int32_t v, int32_t lo, int32_t hi, uint32_t neg, uint32_t pos)
{
    int32_t range = hi - lo;
    if (range <= 0) return 0;
    if (v < lo + range / 4) return neg;
    if (v > hi - range / 4) return pos;
    return 0;
}

static void gamepad_report(const uint8_t *r, uint32_t len)
{
    uint32_t b = 0;
    if (pad.ds4) {
        int off = len && r[0] == 0x01 ? 1 : len && r[0] == 0x11 ? 3 : -1;
        if (off < 0 || len < (uint32_t)off + 7)
            return;
        int ps = 0;
        b = hid_ds4_buttons(r + off, len - (uint32_t)off, &ps);
        if (ps && !pad.ps_held)
            quit_edge = 1;                      /* the PS button leaves the game */
        pad.ps_held = ps;
    } else if (pad.xbox) {
        if (len < 10 || r[0] != 0x00) return;
        uint8_t d = r[2], k = r[3];
        if (d & 0x01) b |= HID_UP;
        if (d & 0x02) b |= HID_DOWN;
        if (d & 0x04) b |= HID_LEFT;
        if (d & 0x08) b |= HID_RIGHT;
        if (d & 0x10) b |= HID_START;
        if (d & 0x20) b |= HID_SELECT;
        if (k & 0x10) b |= HID_A;
        if (k & 0x20) b |= HID_B;
        if (k & 0x40) b |= HID_X;
        if (k & 0x80) b |= HID_Y;
        int16_t lx = (int16_t)(r[6] | r[7] << 8), ly = (int16_t)(r[8] | r[9] << 8);
        if (lx < -12000) b |= HID_LEFT;
        if (lx > 12000) b |= HID_RIGHT;
        if (ly > 12000) b |= HID_UP;
        if (ly < -12000) b |= HID_DOWN;
    } else {
        if (pad.report_id) {
            if (r[0] != pad.report_id) return;
        }
        for (int i = 0; i < pad.nbuttons; i++) {
            if (!bits(r, len, pad.buttons[i])) continue;
            switch (i) {
            case 0: b |= HID_A; break;
            case 1: b |= HID_B; break;
            case 2: b |= HID_X; break;
            case 3: b |= HID_Y; break;
            case 8: b |= HID_SELECT; break;
            case 9: b |= HID_START; break;
            default: if (i >= 4 && i < 8) b |= (i & 1) ? HID_B : HID_A;
            }
        }
        if (pad.have_x) {
            int32_t v = pad.x_min < 0 ? sign_extend(bits(r, len, pad.x), pad.x.size) : (int32_t)bits(r, len, pad.x);
            b |= axis(v, pad.x_min, pad.x_max, HID_LEFT, HID_RIGHT);
        }
        if (pad.have_y) {
            int32_t v = pad.y_min < 0 ? sign_extend(bits(r, len, pad.y), pad.y.size) : (int32_t)bits(r, len, pad.y);
            b |= axis(v, pad.y_min, pad.y_max, HID_UP, HID_DOWN);
        }
        if (pad.have_hat) {
            static const uint32_t dirs[8] = {
                HID_UP, HID_UP | HID_RIGHT, HID_RIGHT, HID_RIGHT | HID_DOWN,
                HID_DOWN, HID_DOWN | HID_LEFT, HID_LEFT, HID_LEFT | HID_UP,
            };
            int32_t h = (int32_t)bits(r, len, pad.hat) - pad.hat_min;
            if (h >= 0 && h < 8) b |= dirs[h];
        }
    }
    if ((b & (HID_START | HID_SELECT)) == (HID_START | HID_SELECT) &&
        (pad_buttons & (HID_START | HID_SELECT)) != (HID_START | HID_SELECT))
        quit_edge = 1;
    pad_buttons = b;
    latched |= b;
    latched_pad |= b;
}

void hid_report(int kind, const uint8_t *data, uint32_t len)
{
    if (kind == USB_KEYBOARD)
        keyboard_report(data, len);
    else if (kind == USB_GAMEPAD || kind == USB_XBOX360)
        gamepad_report(data, len);
}
