#ifndef HID_H
#define HID_H

#include <stdint.h>

/* Game buttons, same order as the .b33 btn() numbers */
#define HID_LEFT    (1u << 0)
#define HID_RIGHT   (1u << 1)
#define HID_UP      (1u << 2)
#define HID_DOWN    (1u << 3)
#define HID_A       (1u << 4)
#define HID_B       (1u << 5)
#define HID_START   (1u << 6)
#define HID_SELECT  (1u << 7)
#define HID_X       (1u << 8)       /* third and fourth face buttons: .b33 btn(6), btn(7) */
#define HID_Y       (1u << 9)

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
 * local < 0; with text != 0 the keyboard types and is left out). Clears the
 * short presses, like hid_buttons(). */
uint32_t hid_players(uint32_t out[HID_PLAYERS], int text, int local);
/* Left stick, -127..127 each (x right, y down), of the Bluetooth pad in
 * slot, or of the USB gamepad with slot -1. Returns 1 if the pad has an
 * analog stick (else xy is 0). */
int hid_stick(int slot, int8_t xy[2]);

/* Text input from the keyboard (layout applied): next byte or -1. */
int      hid_getc(void);
/* Buttons held now, from keyboard or gamepad. */
uint32_t hid_buttons(void);
/* The same without the keyboard (text mode: the keyboard types). */
uint32_t hid_pad_buttons(void);
/* Text mode (editors): hid_getc() also returns the navigation keys as the
 * codes below, and Esc no longer counts as "quit". */
void hid_text_mode(int on);
/* 1 while the key with this HID usage is held on the USB keyboard. */
int hid_usage_held(uint8_t usage);
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
/* 1 once per press of Esc (keyboard) or Start+Select (gamepad). */
int      hid_quit_pressed(void);

/* "it" (default) or "us" */
void hid_set_layout(const char *name);
const char *hid_layout(void);

#endif
