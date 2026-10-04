/*
 * HID devices: boot-protocol keyboards (Italian or US layout), generic
 * HID gamepads (report descriptor parsed for buttons, X/Y and hat switch)
 * and Xbox 360 wired controllers; mice (M32) on USB and Bluetooth.
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

/* the keyboards: [KBD_USB] and [KBD_BLE] (Bluetooth LE), each its own player */
enum { KBD_USB, KBD_BLE, KBDS };
static uint8_t prev_keys[KBDS][8];
static uint8_t queue[64];
static unsigned q_head, q_tail;
static uint32_t kbd_buttons[KBDS], pad_buttons;
static uint32_t bt_buttons[HID_PLAYERS];        /* Bluetooth pads, by player */
/* Buttons seen pressed since the last read: a press and release that both
 * arrive between two frames (a quick tap, or a backlog of reports processed
 * at once) still count for one frame. */
static uint32_t latched_kbd[KBDS], latched_pad, bt_latched[HID_PLAYERS];
static int text_mode;                   /* editors: navigation keys as codes, Esc stays */
static int bt_ps_held[HID_PLAYERS];
static int8_t bt_axis[HID_PLAYERS][2], pad_axis[2];     /* left stick, -127..127 */
static int bt_analog[HID_PLAYERS], pad_analog;
static int8_t bt_axis2[HID_PLAYERS][2], pad_axis2[2];   /* right stick (the pointer) */
static int bt_analog2[HID_PLAYERS], pad_analog2;
static int quit_edge;
static int last_source;                 /* HID_SOURCE_*: what pressed something last */
static int caps;
/* the pointer's buttons on the pads (M32), seen pressed since its last read */
#define PTR_BITS (HID_L2 | HID_R2 | HID_L3 | HID_R3)
static uint32_t ptr_latch;

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
        case 0x3F: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45:
            return (uint8_t)(HID_KEY_F6 + (u - 0x3F));
        }
    switch (u) {
    case 0x28: case 0x58: return '\r';          /* Enter, keypad Enter */
    case 0x2A: return 0x7F;                     /* Backspace */
    case 0x2B: return '\t';
    }
    if (u >= 0x59 && u <= 0x62) return (uint8_t)("1234567890"[u - 0x59]);   /* keypad */
    switch (u) {                                /* keypad operators */
    case 0x54: return '/';
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x63: return '.';
    }
    if (u < sizeof layout_it / sizeof *layout_it) {
        const keydef_t *k = &layout[u];
        return altgr ? k->a : shift ? k->s : k->n;
    }
    return 0;
}

static uint32_t key_button(uint8_t u)
{
    switch (u) {
    case 0x50: case 0x04: case 0x5C: return HID_LEFT;   /* left arrow, A, keypad 4 */
    case 0x4F: case 0x07: case 0x5E: return HID_RIGHT;  /* right arrow, D, keypad 6 */
    case 0x52: case 0x1A: case 0x60: return HID_UP;     /* up arrow, W, keypad 8 */
    case 0x51: case 0x16: case 0x5A: return HID_DOWN;   /* down arrow, S, keypad 2 */
    case 0x2C: case 0x1D: case 0x0D: return HID_A;  /* space, Z, J */
    case 0x1B: case 0x0E: return HID_B;         /* X, K */
    case 0x06: case 0x0F: return HID_X;         /* C, L */
    case 0x19: case 0x0C: return HID_Y;         /* V, I */
    case 0x28: return HID_START;                /* Enter */
    case 0x2B: return HID_SELECT;               /* Tab */
    case 0x14: case 0x4B: return HID_L1;        /* Q, PgUp */
    case 0x08: case 0x4E: return HID_R1;        /* E, PgDn */
    }
    return 0;
}

static uint8_t kbd_report_id;

void hid_keyboard_attach(uint8_t report_id)
{
    memset(prev_keys[KBD_USB], 0, sizeof prev_keys[KBD_USB]);
    kbd_buttons[KBD_USB] = 0;
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

static void keyboard_boot(int k, const uint8_t *r);

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
    if (len >= 8)
        keyboard_boot(KBD_USB, r);
}

/* A boot-format report of keyboard k: modifiers, reserved, six key usages.
 * Both keyboards type the same text; their game buttons stay apart. */
static void keyboard_boot(int k, const uint8_t *r)
{
    if (r[2] == 1)                              /* roll-over error: ignore */
        return;
    uint8_t mods = r[0];
    uint32_t buttons = 0;
    for (int i = 2; i < 8; i++) {
        uint8_t u = r[i];
        if (!u) continue;
        buttons |= key_button(u);
        int was = 0;
        for (int j = 2; j < 8; j++) was |= prev_keys[k][j] == u;
        if (was) continue;
        /* new key */
        last_source = HID_SOURCE_KEYBOARD;
        if (u == 0x29) {                                            /* Esc */
            /* the system's keys (src/kernel/syskeys.h): Ctrl+Shift+Esc the
             * monitor (as Start+Select), Ctrl+Esc back to bm's menu (as
             * PS), Esc alone back or a game's menu (as Start) */
            if ((mods & MOD_CTRL) && (mods & MOD_SHIFT))
                quit_edge |= HID_QUIT_KEY | HID_QUIT_MONITOR;
            else if (mods & MOD_CTRL)
                quit_edge |= HID_QUIT_PS;
            else {
                if (!text_mode)
                    quit_edge |= HID_QUIT_ESC;
                push(0x1B);
            }
            continue;
        }
        if (u == 0x39) { caps = !caps; continue; }                  /* Caps Lock */
        uint8_t c = translate(u, mods);
        if (c) {
            if (text_mode && c >= 1 && c <= 26 && (mods & MOD_SHIFT))
                push(HID_KEY_CTRL_SHIFT);       /* Ctrl+Shift+S: "^S" for keyp() */
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
    kbd_buttons[k] = buttons;
    latched_kbd[k] |= buttons;
    memcpy(prev_keys[k], r, 8);
}

/* ---------------------------------------------------------------- LE keyboard */

/* The keyboard input report of a Bluetooth LE keyboard, from its report
 * map: where the modifier byte and the key usages (an array of bytes, or a
 * bitmap) are. Offsets count per report ID, over the Input items only. */
int hid_kbd_layout(const uint8_t *d, uint32_t len, hid_kbd_layout_t *k)
{
    static uint16_t off[256];
    uint32_t page = 0, size = 0, count = 0, id = 0, umin = 0, umax = 0, usage = 0;
    int depth = 0, in_kbd = 0, found = 0;
    memset(off, 0, sizeof off);
    memset(k, 0, sizeof *k);
    k->mods_bit = k->keys_bit = k->bitmap_bit = -1;
    for (uint32_t i = 0; i < len;) {
        uint8_t prefix = d[i];
        if (prefix == 0xFE) {                   /* long item */
            if (i + 1 >= len) break;
            i += 3u + d[i + 1];
            continue;
        }
        uint32_t sz = prefix & 3;
        if (sz == 3) sz = 4;
        if (i + 1 + sz > len) break;
        uint32_t v = 0;
        for (uint32_t b = 0; b < sz; b++) v |= (uint32_t)d[i + 1 + b] << (8 * b);
        i += 1 + sz;
        switch (prefix & 0xFC) {
        case 0x04: page = v; break;             /* Usage Page */
        case 0x74: size = v; break;             /* Report Size */
        case 0x94: count = v; break;            /* Report Count */
        case 0x84: id = v & 0xFF; break;        /* Report ID */
        case 0x08: usage = v & 0xFFFF; break;   /* Usage */
        case 0x18: umin = v & 0xFFFF; break;    /* Usage Minimum */
        case 0x28: umax = v & 0xFFFF; break;    /* Usage Maximum */
        case 0xA0:                              /* Collection */
            depth++;
            if (depth == 1 && v == 1 && page == 0x01 && usage == 0x06 && !found) {
                in_kbd = 1;
                found = 1;
            }
            break;
        case 0xC0:                              /* End Collection */
            if (depth > 0 && --depth == 0)
                in_kbd = 0;
            break;
        case 0x80: {                            /* Input */
            uint32_t bits = size * count;
            if (in_kbd && !(v & 1) && page == 0x07) {
                if (!k->id)
                    k->id = (uint8_t)id;
                if (id == k->id) {
                    if ((v & 2) && umin == 0xE0 && size == 1 && k->mods_bit < 0) {
                        k->mods_bit = (int16_t)off[id];
                    } else if ((v & 2) && size == 1 && k->bitmap_bit < 0) {
                        k->bitmap_bit = (int16_t)off[id];
                        k->bitmap_min = (uint8_t)umin;
                        k->bitmap_n = (uint16_t)count;
                    } else if (!(v & 2) && size == 8 && k->keys_bit < 0) {
                        k->keys_bit = (int16_t)off[id];
                        k->nkeys = (uint8_t)(count > 32 ? 32 : count);
                    }
                }
            }
            off[id] = (uint16_t)(off[id] + bits);
            break;
        }
        }
        if ((prefix & 0x0C) == 0x00) {          /* main item: clears the local ones */
            usage = 0;
            if ((prefix & 0xFC) != 0xA0)
                umin = umax = 0;
        }
    }
    (void)umax;
    return found && (k->keys_bit >= 0 || k->bitmap_bit >= 0);
}

static int bit_at(const uint8_t *r, uint32_t len, uint32_t bit)
{
    return bit / 8 < len && (r[bit / 8] >> (bit % 8)) & 1;
}

static uint8_t byte_at(const uint8_t *r, uint32_t len, uint32_t bit)
{
    uint8_t v = 0;
    for (int b = 0; b < 8; b++)
        v |= (uint8_t)(bit_at(r, len, bit + (uint32_t)b) << b);
    return v;
}

void hid_ble_keyboard(const hid_kbd_layout_t *k, const uint8_t *r, uint32_t len)
{
    uint8_t boot[8] = { 0 };
    int n = 0;
    if (k->mods_bit >= 0)
        boot[0] = byte_at(r, len, (uint32_t)k->mods_bit);
    if (k->keys_bit >= 0)
        for (int i = 0; i < k->nkeys && n < 6; i++) {
            uint8_t u = byte_at(r, len, (uint32_t)k->keys_bit + 8u * (uint32_t)i);
            if (u == 1) {                       /* roll-over error */
                boot[2] = 1;
                break;
            }
            if (u >= 4)
                boot[2 + n++] = u;
        }
    if (k->bitmap_bit >= 0)
        for (int i = 0; i < k->bitmap_n && n < 6; i++) {
            unsigned u = k->bitmap_min + (unsigned)i;
            if (!bit_at(r, len, (uint32_t)k->bitmap_bit + (uint32_t)i))
                continue;
            if (u >= 0xE0 && u <= 0xE7)         /* modifiers inside the bitmap */
                boot[0] |= (uint8_t)(1u << (u - 0xE0));
            else if (u >= 4)
                boot[2 + n++] = (uint8_t)u;
        }
    keyboard_boot(KBD_BLE, boot);
}

void hid_ble_keyboard_clear(void)
{
    static const uint8_t none[8];
    keyboard_boot(KBD_BLE, none);
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

static uint32_t bt_all(void)
{
    uint32_t b = 0;
    for (int s = 0; s < HID_PLAYERS; s++)
        b |= bt_buttons[s] | bt_latched[s];
    return b;
}

static void clear_latches(void)
{
    latched_kbd[KBD_USB] = latched_kbd[KBD_BLE] = latched_pad = 0;
    for (int s = 0; s < HID_PLAYERS; s++)
        bt_latched[s] = 0;
}

uint32_t hid_buttons(void)
{
    uint32_t b = kbd_buttons[KBD_USB] | kbd_buttons[KBD_BLE] | latched_kbd[KBD_USB] |
                 latched_kbd[KBD_BLE] | pad_buttons | latched_pad | bt_all();
    clear_latches();
    return b;
}

uint32_t hid_players(uint32_t out[HID_PLAYERS], int text, int local, int ble)
{
    uint32_t mine = pad_buttons | latched_pad, le = 0, any;
    if (!text) {
        mine |= kbd_buttons[KBD_USB] | latched_kbd[KBD_USB];
        le = kbd_buttons[KBD_BLE] | latched_kbd[KBD_BLE];
    }
    any = mine | le;
    for (int s = 0; s < HID_PLAYERS; s++) {
        out[s] = bt_buttons[s] | bt_latched[s];
        any |= out[s];
    }
    if (ble < 0 || ble >= HID_PLAYERS)          /* no player of its own: with the USB one */
        ble = local;
    if (local >= 0 && local < HID_PLAYERS)
        out[local] |= mine;
    if (ble >= 0 && ble < HID_PLAYERS)
        out[ble] |= le;
    clear_latches();
    return any;
}

int hid_stick(int slot, int8_t xy[2])
{
    if (slot < 0) {
        xy[0] = pad_axis[0];
        xy[1] = pad_axis[1];
        return pad_analog;
    }
    if (slot >= HID_PLAYERS)
        return 0;
    xy[0] = bt_axis[slot][0];
    xy[1] = bt_axis[slot][1];
    return bt_analog[slot];
}

int hid_stick_r(int slot, int8_t xy[2])
{
    return hid_stick2(slot, xy);
}

int hid_stick2(int slot, int8_t xy[2])
{
    if (slot < 0) {
        xy[0] = pad_axis2[0];
        xy[1] = pad_axis2[1];
        return pad_analog2;
    }
    if (slot >= HID_PLAYERS)
        return 0;
    xy[0] = bt_axis2[slot][0];
    xy[1] = bt_axis2[slot][1];
    return bt_analog2[slot];
}

int hid_usage_held(uint8_t u)
{
    if (u >= 0xE0 && u <= 0xE7)                 /* modifiers: a bit of the first byte */
        return ((prev_keys[KBD_USB][0] | prev_keys[KBD_BLE][0]) >> (u - 0xE0)) & 1;
    for (int i = 2; i < 8; i++)
        if (prev_keys[KBD_USB][i] == u || prev_keys[KBD_BLE][i] == u)
            return 1;
    return 0;
}

int hid_keys_held(uint8_t *out, int max)
{
    int n = 0;
    uint8_t mods = prev_keys[KBD_USB][0] | prev_keys[KBD_BLE][0];
    for (int b = 0; b < 8 && n < max; b++)
        if (mods >> b & 1)
            out[n++] = (uint8_t)(0xE0 + b);
    for (int k = 0; k < KBDS; k++)
        for (int i = 2; i < 8 && n < max; i++) {
            uint8_t u = prev_keys[k][i];
            int dup = u < 4;                    /* 0 none, 1-3 errors */
            for (int j = 0; j < n && !dup; j++)
                dup = out[j] == u;
            if (!dup)
                out[n++] = u;
        }
    return n;
}

uint32_t hid_pointer_buttons(void)
{
    uint32_t b = pad_buttons | ptr_latch;
    for (int s = 0; s < HID_PLAYERS; s++)
        b |= bt_buttons[s];
    ptr_latch = 0;
    return b & PTR_BITS;
}

uint32_t hid_pad_buttons(void)
{
    uint32_t b = pad_buttons | latched_pad | bt_all();
    clear_latches();
    return b;
}

void hid_text_mode(int on)
{
    text_mode = on;
}

/* stick byte 0..255 (128 = centre) -> -127..127 */
static int8_t ds4_axis(uint8_t v)
{
    int a = (int)v - 128;
    return (int8_t)(a < -127 ? -127 : a);
}

void hid_bt_report(int slot, const uint8_t *r, uint32_t len)
{
    int off = len && r[0] == 0x01 ? 1 : len && r[0] == 0x11 ? 3 : -1;
    if (slot < 0 || slot >= HID_PLAYERS || off < 0 || len < (uint32_t)off + 7)
        return;
    int ps = 0;
    uint32_t b = hid_ds4_buttons(r + off, len - (uint32_t)off, &ps);
    if (ps && !bt_ps_held[slot])
        quit_edge |= HID_QUIT_PS;
    if ((b & (HID_START | HID_SELECT)) == (HID_START | HID_SELECT) &&
        (bt_buttons[slot] & (HID_START | HID_SELECT)) != (HID_START | HID_SELECT))
        quit_edge |= HID_QUIT_KEY | HID_QUIT_MONITOR;
    if ((b & ~bt_buttons[slot]) || (ps && !bt_ps_held[slot]))
        last_source = HID_SOURCE_DS4;
    bt_ps_held[slot] = ps;
    bt_buttons[slot] = b;
    bt_latched[slot] |= b;
    ptr_latch |= b & PTR_BITS;
    bt_axis[slot][0] = ds4_axis(r[off]);
    bt_axis[slot][1] = ds4_axis(r[off + 1]);
    bt_analog[slot] = 1;
    bt_axis2[slot][0] = ds4_axis(r[off + 2]);
    bt_axis2[slot][1] = ds4_axis(r[off + 3]);
    bt_analog2[slot] = 1;
}

void hid_bt_clear(int slot)
{
    if (slot < 0 || slot >= HID_PLAYERS)
        return;
    bt_buttons[slot] = 0;
    bt_ps_held[slot] = 0;
    bt_axis[slot][0] = bt_axis[slot][1] = 0;
    bt_analog[slot] = 0;
    bt_axis2[slot][0] = bt_axis2[slot][1] = 0;
    bt_analog2[slot] = 0;
}

int hid_last_source(void)
{
    return last_source;
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
    field_t x, y, hat, rx, ry;          /* rx, ry: the right stick */
    int32_t x_min, x_max, y_min, y_max, hat_min, rx_min, rx_max, ry_min, ry_max;
    int have_x, have_y, have_hat;
    int rx_usage, ry_usage;             /* Rx / Ry, else Z / Rz */
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
                    } else if (usage_page == 0x01 && (usage == 0x33 || (usage == 0x32 && pad.rx_usage != 0x33))) {
                        pad.rx = f; pad.rx_min = lmin; pad.rx_max = lmax; pad.rx_usage = (int)usage;
                    } else if (usage_page == 0x01 && (usage == 0x34 || (usage == 0x35 && pad.ry_usage != 0x34))) {
                        pad.ry = f; pad.ry_min = lmin; pad.ry_max = lmax; pad.ry_usage = (int)usage;
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
    pad_analog = pad.have_x && pad.have_y;
    /* a right stick: Rx and Ry, or Z and Rz (not a mix: Z alone is often
     * the triggers) */
    pad_analog2 = (pad.rx_usage == 0x33 && pad.ry_usage == 0x34) ||
                  (pad.rx_usage == 0x32 && pad.ry_usage == 0x35);
    pad_axis2[0] = pad_axis2[1] = 0;
    return (pad.nbuttons || pad.have_hat || pad.have_x) ? 0 : -1;
}

void hid_xbox360_attach(void)
{
    memset(&pad, 0, sizeof pad);
    pad.xbox = 1;
    pad_buttons = 0;
    pad_analog = pad_analog2 = 1;
}

void hid_ds4_attach(void)
{
    memset(&pad, 0, sizeof pad);
    pad.ds4 = 1;
    pad_buttons = 0;
    pad_analog = pad_analog2 = 1;
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
    if (d[5] & 0x01) b |= HID_L1;
    if (d[5] & 0x02) b |= HID_R1;
    if (d[5] & 0x04) b |= HID_L2;
    if (d[5] & 0x08) b |= HID_R2;
    if (d[5] & 0x40) b |= HID_L3;
    if (d[5] & 0x80) b |= HID_R3;
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

/* v in lo..hi -> -127..127 */
static int8_t norm_axis(int32_t v, int32_t lo, int32_t hi)
{
    if (hi <= lo)
        return 0;
    int32_t a = (int32_t)(((int64_t)(v - lo) * 254) / (hi - lo)) - 127;
    return (int8_t)(a < -127 ? -127 : a > 127 ? 127 : a);
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
        pad_axis[0] = ds4_axis(r[off]);
        pad_axis[1] = ds4_axis(r[off + 1]);
        pad_axis2[0] = ds4_axis(r[off + 2]);
        pad_axis2[1] = ds4_axis(r[off + 3]);
        if (ps && !pad.ps_held) {
            quit_edge |= HID_QUIT_PS;           /* the PS button leaves the game */
            last_source = HID_SOURCE_DS4;
        }
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
        if (d & 0x40) b |= HID_L3;              /* stick clicks */
        if (d & 0x80) b |= HID_R3;
        if (r[4] > 64) b |= HID_L2;             /* LT, RT: 0..255 */
        if (r[5] > 64) b |= HID_R2;
        if (k & 0x10) b |= HID_A;
        if (k & 0x20) b |= HID_B;
        if (k & 0x40) b |= HID_X;
        if (k & 0x80) b |= HID_Y;
        if (k & 0x01) b |= HID_L1;              /* LB */
        if (k & 0x02) b |= HID_R1;              /* RB */
        if ((k & 0x04) && !pad.ps_held)
            quit_edge |= HID_QUIT_PS;           /* Guide: like PS */
        pad.ps_held = k & 0x04;
        int16_t lx = (int16_t)(r[6] | r[7] << 8), ly = (int16_t)(r[8] | r[9] << 8);
        pad_axis[0] = (int8_t)(lx / 258);
        pad_axis[1] = (int8_t)(-(ly / 258));
        if (lx < -12000) b |= HID_LEFT;
        if (lx > 12000) b |= HID_RIGHT;
        if (ly > 12000) b |= HID_UP;
        if (ly < -12000) b |= HID_DOWN;
        if (len >= 14) {
            int16_t rx = (int16_t)(r[10] | r[11] << 8), ry = (int16_t)(r[12] | r[13] << 8);
            pad_axis2[0] = (int8_t)(rx / 258);
            pad_axis2[1] = (int8_t)(-(ry / 258));
        }
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
            case 4: b |= HID_L1; break;
            case 5: b |= HID_R1; break;
            case 8: b |= HID_SELECT; break;
            case 9: b |= HID_START; break;
            case 10: b |= HID_L3; break;
            case 11: b |= HID_R3; break;
            default: if (i >= 6 && i < 8) b |= ((i & 1) ? HID_B | HID_R2 : HID_A | HID_L2);
            }
        }
        if (pad.have_x) {
            int32_t v = pad.x_min < 0 ? sign_extend(bits(r, len, pad.x), pad.x.size) : (int32_t)bits(r, len, pad.x);
            b |= axis(v, pad.x_min, pad.x_max, HID_LEFT, HID_RIGHT);
            pad_axis[0] = norm_axis(v, pad.x_min, pad.x_max);
        }
        if (pad.have_y) {
            int32_t v = pad.y_min < 0 ? sign_extend(bits(r, len, pad.y), pad.y.size) : (int32_t)bits(r, len, pad.y);
            b |= axis(v, pad.y_min, pad.y_max, HID_UP, HID_DOWN);
            pad_axis[1] = norm_axis(v, pad.y_min, pad.y_max);
        }
        if (pad.have_hat) {
            static const uint32_t dirs[8] = {
                HID_UP, HID_UP | HID_RIGHT, HID_RIGHT, HID_RIGHT | HID_DOWN,
                HID_DOWN, HID_DOWN | HID_LEFT, HID_LEFT, HID_LEFT | HID_UP,
            };
            int32_t h = (int32_t)bits(r, len, pad.hat) - pad.hat_min;
            if (h >= 0 && h < 8) b |= dirs[h];
        }
        if (pad_analog2) {
            int32_t vx = (int32_t)bits(r, len, pad.rx), vy = (int32_t)bits(r, len, pad.ry);
            if (pad.rx_min < 0) vx = sign_extend((uint32_t)vx, pad.rx.size);
            if (pad.ry_min < 0) vy = sign_extend((uint32_t)vy, pad.ry.size);
            pad_axis2[0] = norm_axis(vx, pad.rx_min, pad.rx_max);
            pad_axis2[1] = norm_axis(vy, pad.ry_min, pad.ry_max);
        }
    }
    if ((b & (HID_START | HID_SELECT)) == (HID_START | HID_SELECT) &&
        (pad_buttons & (HID_START | HID_SELECT)) != (HID_START | HID_SELECT))
        quit_edge |= HID_QUIT_KEY | HID_QUIT_MONITOR;
    if (b & ~pad_buttons)
        last_source = pad.ds4 ? HID_SOURCE_DS4 : HID_SOURCE_PAD;
    pad_buttons = b;
    latched_pad |= b;
    ptr_latch |= b & PTR_BITS;
}

void hid_report(int kind, const uint8_t *data, uint32_t len)
{
    if (kind == USB_KEYBOARD)
        keyboard_report(data, len);
    else if (kind == USB_GAMEPAD || kind == USB_XBOX360)
        gamepad_report(data, len);
}

/* ---------------------------------------------------------------- mice */

/* Mouse report layout from a report descriptor: the first application
 * collection that is a mouse (or a pointer), its buttons, X, Y, wheel and
 * AC Pan. Offsets count per report ID, over the Input items only. */
int hid_mouse_layout(const uint8_t *d, uint32_t len, hid_mouse_layout_t *m)
{
    static uint16_t off[256];
    uint32_t page = 0, size = 0, count = 0, id = 0, umin = 0, nusages = 0;
    uint32_t usages[16];
    int32_t lmin = 0, lmax = 0;
    int depth = 0, in_mouse = 0, found = 0, have_id = 0, have_range = 0;
    memset(off, 0, sizeof off);
    memset(m, 0, sizeof *m);
    m->buttons_bit = m->x_bit = m->y_bit = m->wheel_bit = m->pan_bit = -1;
    for (uint32_t i = 0; i < len;) {
        uint8_t prefix = d[i];
        if (prefix == 0xFE) {                   /* long item */
            if (i + 1 >= len) break;
            i += 3u + d[i + 1];
            continue;
        }
        uint32_t sz = prefix & 3;
        if (sz == 3) sz = 4;
        if (i + 1 + sz > len) break;
        uint32_t v = 0;
        for (uint32_t b = 0; b < sz; b++) v |= (uint32_t)d[i + 1 + b] << (8 * b);
        int32_t sv = sz ? sign_extend(v, (int)sz * 8) : 0;
        i += 1 + sz;
        switch (prefix & 0xFC) {
        case 0x04: page = v; break;             /* Usage Page */
        case 0x14: lmin = sv; break;            /* Logical Minimum */
        case 0x24: lmax = (lmin >= 0 && sz < 4) ? (int32_t)v : sv; break;
        case 0x74: size = v; break;             /* Report Size */
        case 0x94: count = v; break;            /* Report Count */
        case 0x84: id = v & 0xFF; break;        /* Report ID */
        case 0x08:                              /* Usage */
            if (nusages < 16) usages[nusages++] = v & 0xFFFF;
            break;
        case 0x18: umin = v & 0xFFFF; have_range = 1; break;   /* Usage Minimum */
        case 0xA0:                              /* Collection */
            depth++;
            if (depth == 1 && v == 1 && page == 0x01 && nusages &&
                (usages[nusages - 1] == 0x02 || usages[nusages - 1] == 0x01) && !found) {
                in_mouse = 1;
                found = 1;
            }
            break;
        case 0xC0:                              /* End Collection */
            if (depth > 0 && --depth == 0)
                in_mouse = 0;
            break;
        case 0x80: {                            /* Input */
            if (in_mouse && !(v & 1) && (!have_id || id == m->id)) {
                for (uint32_t n = 0; n < count && size; n++) {
                    uint32_t u = have_range ? umin + n : n < nusages ? usages[n] :
                                 nusages ? usages[nusages - 1] : 0;
                    int16_t bit = (int16_t)(off[id] + n * size);
                    int field = 0;
                    if (page == 0x09 && m->buttons_bit < 0 && size == 1) {
                        m->buttons_bit = bit;
                        m->nbuttons = (uint8_t)(count > 8 ? 8 : count);
                        field = 1;
                        n = count;                  /* the whole array */
                    } else if (page == 0x01 && u == 0x30 && m->x_bit < 0 && size <= 32) {
                        m->x_bit = bit;
                        m->x_size = (uint8_t)size;
                        m->x_min = lmin;
                        m->x_max = lmax;
                        m->absolute = !(v & 4);
                        field = 1;
                    } else if (page == 0x01 && u == 0x31 && m->y_bit < 0 && size <= 32) {
                        m->y_bit = bit;
                        m->y_size = (uint8_t)size;
                        m->y_min = lmin;
                        m->y_max = lmax;
                        field = 1;
                    } else if (page == 0x01 && u == 0x38 && m->wheel_bit < 0 && size <= 32) {
                        m->wheel_bit = bit;
                        m->wheel_size = (uint8_t)size;
                        field = 1;
                    } else if (page == 0x0C && u == 0x238 && m->pan_bit < 0 && size <= 32) {
                        m->pan_bit = bit;
                        m->pan_size = (uint8_t)size;
                        field = 1;
                    }
                    if (field && !have_id) {
                        m->id = (uint8_t)id;
                        have_id = 1;
                    }
                }
            }
            off[id] = (uint16_t)(off[id] + size * count);
            break;
        }
        }
        if ((prefix & 0x0C) == 0x00) {          /* main item: clears the local ones */
            nusages = 0;
            have_range = 0;
            umin = 0;
        }
    }
    return found && m->x_bit >= 0 && m->y_bit >= 0;
}

void hid_mouse_boot_layout(hid_mouse_layout_t *m, uint8_t id)
{
    memset(m, 0, sizeof *m);
    m->id = id;
    m->nbuttons = 3;
    m->buttons_bit = 0;
    m->x_bit = 8;
    m->y_bit = 16;
    m->wheel_bit = 24;
    m->pan_bit = -1;
    m->x_size = m->y_size = m->wheel_size = 8;
    m->x_min = m->y_min = -127;
    m->x_max = m->y_max = 127;
}

static struct {
    int32_t dx, dy, wheel, pan;
    uint8_t held[HID_MICE], pressed;
    int abs;
    uint16_t ax, ay;
} mice;

/* a signed (relative) field, 0 when it is not in this report */
static int32_t mouse_field(const uint8_t *r, uint32_t len, int16_t bit, uint8_t size)
{
    if (bit < 0 || !size || (uint32_t)bit + size > len * 8)
        return 0;
    field_t f = { (uint16_t)bit, size };
    return sign_extend(bits(r, len, f), size);
}

/* an absolute field -> 0..65535 */
static uint16_t mouse_abs(const uint8_t *r, uint32_t len, int16_t bit, uint8_t size,
                          int32_t lo, int32_t hi)
{
    field_t f = { (uint16_t)bit, size };
    int32_t v = lo < 0 ? sign_extend(bits(r, len, f), size) : (int32_t)bits(r, len, f);
    if (hi <= lo)
        return 0;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (uint16_t)((int64_t)(v - lo) * 65535 / (hi - lo));
}

void hid_mouse_report(int src, const hid_mouse_layout_t *m, const uint8_t *r, uint32_t len)
{
    if (src < 0 || src >= HID_MICE)
        return;
    if (m->id) {
        if (!len || r[0] != m->id)
            return;
        r++;
        len--;
    }
    if (m->x_bit < 0 || (uint32_t)m->y_bit + m->y_size > len * 8)
        return;                                 /* too short: not a motion report */
    uint8_t b = 0;
    if (m->buttons_bit >= 0)
        for (int i = 0; i < m->nbuttons && i < 3; i++) {
            field_t f = { (uint16_t)(m->buttons_bit + i), 1 };
            if (bits(r, len, f))
                b |= (uint8_t)(1u << i);
        }
    mice.pressed |= (uint8_t)(b & ~mice.held[src]);
    mice.held[src] = b;
    if (m->absolute) {
        mice.ax = mouse_abs(r, len, m->x_bit, m->x_size, m->x_min, m->x_max);
        mice.ay = mouse_abs(r, len, m->y_bit, m->y_size, m->y_min, m->y_max);
        mice.abs = 1;
    } else {
        mice.dx += mouse_field(r, len, m->x_bit, m->x_size);
        mice.dy += mouse_field(r, len, m->y_bit, m->y_size);
    }
    mice.wheel += mouse_field(r, len, m->wheel_bit, m->wheel_size);
    mice.pan += mouse_field(r, len, m->pan_bit, m->pan_size);
}

void hid_mouse_clear(int src)
{
    if (src >= 0 && src < HID_MICE)
        mice.held[src] = 0;
}

void hid_mouse_take(hid_mouse_t *m)
{
    m->dx = mice.dx;
    m->dy = mice.dy;
    m->wheel = mice.wheel;
    m->pan = mice.pan;
    m->buttons = 0;
    for (int s = 0; s < HID_MICE; s++)
        m->buttons |= mice.held[s];
    m->pressed = mice.pressed;
    m->abs = mice.abs;
    m->ax = mice.ax;
    m->ay = mice.ay;
    mice.dx = mice.dy = mice.wheel = mice.pan = 0;
    mice.pressed = 0;
    mice.abs = 0;
}
