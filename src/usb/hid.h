#ifndef HID_H
#define HID_H

#include <stdint.h>

/* Game buttons, same order as the .bm btn() numbers */
#define HID_LEFT    (1u << 0)
#define HID_RIGHT   (1u << 1)
#define HID_UP      (1u << 2)
#define HID_DOWN    (1u << 3)
#define HID_A       (1u << 4)
#define HID_B       (1u << 5)
#define HID_START   (1u << 6)
#define HID_SELECT  (1u << 7)
#define HID_X       (1u << 8)       /* third and fourth face buttons: .bm btn(6), btn(7) */
#define HID_Y       (1u << 9)
#define HID_L1      (1u << 10)      /* shoulder buttons: the menu's tabs (not in games) */
#define HID_R1      (1u << 11)
#define HID_L2      (1u << 12)      /* triggers and stick clicks (M32): with the right */
#define HID_R2      (1u << 13)      /* stick as the pointer, R2 / R3 click and L2 is the */
#define HID_L3      (1u << 14)      /* right button; not in btn() */
#define HID_R3      (1u << 15)

/* report_id: the keyboard's report ID if the device may send report
 * protocol reports (first byte = ID), else 0. */
void hid_keyboard_attach(uint8_t report_id);
/* 1 if the report descriptor has a keyboard application collection;
 * *report_id gets its report ID (0 if none). */
int  hid_is_keyboard(const uint8_t *desc, uint32_t len, uint8_t *report_id);
int  hid_gamepad_attach(const uint8_t *report_desc, uint32_t len);
void hid_xbox360_attach(void);
/* Sony DualShock 4 (USB report 0x01, Bluetooth report 0x11). */
void hid_ds4_attach(void);
/* Buttons from the DS4 data that follows the report header (sticks
 * first); *ps = PS button. Shared by USB and Bluetooth. */
uint32_t hid_ds4_buttons(const uint8_t *d, uint32_t len, int *ps);
void hid_report(int kind, const uint8_t *data, uint32_t len);

#define HID_PLAYERS 4

/* DualShock 4 over Bluetooth, the pad of player slot+1: an input report as
 * it comes from the interrupt channel, after the 0xA1 header (r[0] = report
 * ID 0x01 or 0x11). Its buttons join hid_buttons(); clear on disconnection. */
void hid_bt_report(int slot, const uint8_t *r, uint32_t len);
void hid_bt_clear(int slot);

/* Buttons of each player (out[0] = player 1) and their OR: the Bluetooth
 * pads by player, the USB keyboard/gamepad as player local+1 (none if
 * local < 0), the Bluetooth LE keyboard as player ble+1 (with the USB one
 * if ble < 0); with text != 0 the keyboards type and are left out. Clears
 * the short presses, like hid_buttons(). */
uint32_t hid_players(uint32_t out[HID_PLAYERS], int text, int local, int ble);
/* Left stick, -127..127 each (x right, y down), of the Bluetooth pad in
 * slot, or of the USB gamepad with slot -1. Returns 1 if the pad has an
 * analog stick (else xy is 0). */
int hid_stick(int slot, int8_t xy[2]);
/* The right stick (M32: it moves the pointer), the same way; 0 for pads
 * without one. */
int hid_stick2(int slot, int8_t xy[2]);

/* ---- mice (M32): USB, Bluetooth classic and Bluetooth LE */

/* Where the fields of a mouse's input report are, from its report
 * descriptor (USB) or report map (LE): bit offsets after the report ID,
 * -1 when absent. absolute: X and Y are a position (a tablet, QEMU's
 * usb-tablet), in x_min..x_max / y_min..y_max. */
typedef struct {
    uint8_t id;                     /* report ID, 0 = none */
    uint8_t nbuttons, absolute;
    uint8_t x_size, y_size, wheel_size, pan_size;
    int16_t buttons_bit, x_bit, y_bit, wheel_bit, pan_bit;
    int32_t x_min, x_max, y_min, y_max;
} hid_mouse_layout_t;
/* 1 if the descriptor has a mouse (or tablet) with X and Y. */
int  hid_mouse_layout(const uint8_t *desc, uint32_t len, hid_mouse_layout_t *m);
/* The boot protocol report: buttons, X, Y and, when the report is longer,
 * the wheel. id: 2 on Bluetooth classic (A1 02 ...), 0 on USB. */
void hid_mouse_boot_layout(hid_mouse_layout_t *m, uint8_t id);

enum { HID_MOUSE_USB, HID_MOUSE_BT, HID_MOUSE_BLE, HID_MICE };
/* A report of the mouse on `src`: with its report ID first when the layout
 * has one (for LE notifications, pass a layout with id 0). */
void hid_mouse_report(int src, const hid_mouse_layout_t *m, const uint8_t *r, uint32_t len);
/* The mouse on `src` went away: its buttons are released. */
void hid_mouse_clear(int src);

typedef struct {
    int32_t dx, dy;                 /* relative motion since the last call (counts) */
    int32_t wheel, pan;             /* wheel steps: up / right positive */
    uint8_t buttons;                /* held on any mouse: bit 0 left, 1 right, 2 middle */
    uint8_t pressed;                /* pressed since the last call (a quick click counts) */
    int abs;                        /* a tablet reported: ax, ay its position, 0..65535 */
    uint16_t ax, ay;
} hid_mouse_t;
/* What the mice did since the last call (then cleared). */
void hid_mouse_take(hid_mouse_t *m);

/* A Bluetooth LE keyboard (HID over GATT). Its keyboard input report, as
 * found in the report map: report ID, bit offsets of the modifier byte and
 * of the keys (an array of nkeys usages, or a bitmap of bitmap_n usages
 * from bitmap_min); -1 when absent. */
typedef struct {
    uint8_t id, nkeys, bitmap_min;
    uint16_t bitmap_n;
    int16_t mods_bit, keys_bit, bitmap_bit;
} hid_kbd_layout_t;
/* 1 if the report map has a keyboard with keys we can read. */
int  hid_kbd_layout(const uint8_t *map, uint32_t len, hid_kbd_layout_t *k);
/* A report of that keyboard (without the report ID): works like the USB
 * keyboard (text, keys as buttons, Esc). Clear: all keys released. */
void hid_ble_keyboard(const hid_kbd_layout_t *k, const uint8_t *r, uint32_t len);
void hid_ble_keyboard_clear(void);

/* Text input from the keyboard (layout applied): next byte or -1. */
int      hid_getc(void);
/* Buttons held now, from keyboard or gamepad. */
uint32_t hid_buttons(void);
/* The same without the keyboard (text mode: the keyboard types). */
uint32_t hid_pad_buttons(void);
/* L2 R2 L3 R3 held on the pads, or pressed since the last call (the
 * pointer's buttons, M32); the other readers' presses are not taken. */
uint32_t hid_pointer_buttons(void);
/* What pressed a button or a key last, for the buttons shown on screen:
 * a keyboard (USB or Bluetooth), a DS4 (Bluetooth or USB) or another pad;
 * HID_SOURCE_NONE until something is pressed. */
enum { HID_SOURCE_NONE, HID_SOURCE_KEYBOARD, HID_SOURCE_DS4, HID_SOURCE_PAD };
int      hid_last_source(void);
/* Text mode (editors): hid_getc() also returns the navigation keys as the
 * codes below, and Esc no longer counts as "quit". */
void hid_text_mode(int on);
/* 1 while the key with this HID usage is held on a keyboard (USB or
 * Bluetooth); the modifiers are 0xE0-0xE7 (left Ctrl, Shift, Alt, GUI,
 * then the right ones). */
int hid_usage_held(uint8_t usage);
/* The usages held now on the keyboards (modifiers first); returns how many. */
int hid_keys_held(uint8_t *out, int max);
#define HID_KEY_UP      0xF0
#define HID_KEY_DOWN    0xF1
#define HID_KEY_LEFT    0xF2
#define HID_KEY_RIGHT   0xF3
#define HID_KEY_HOME    0xF4
#define HID_KEY_END     0xF5
#define HID_KEY_PGUP    0xF6
#define HID_KEY_PGDN    0xF7
#define HID_KEY_DEL     0xF8
#define HID_KEY_F1      0xF9            /* .. F5 = 0xFD */
#define HID_KEY_F6      0xE6            /* .. F12 = 0xEC (code page 437 Greek: never typed) */
/* Once per press, then cleared: HID_QUIT_KEY for Esc or Start+Select,
 * HID_QUIT_PS for the PS / Xbox Guide button (home: never the monitor).
 * Ctrl+Esc and Start+Select add HID_QUIT_MONITOR: from the menu, they go
 * to the monitor; Esc alone goes back there, like B. */
#define HID_QUIT_KEY     1
#define HID_QUIT_PS      2
#define HID_QUIT_MONITOR 4
int      hid_quit_pressed(void);

/* "it" (default) or "us" */
void hid_set_layout(const char *name);
const char *hid_layout(void);

#endif
