/* Host tests for src/usb/hid.c: keyboards in boot and report protocol. */
#include "usb/hid.h"
#include "usb/usb.h"

#include <stdio.h>
#include <string.h>

uint32_t timer_ticks(void) { return 0; }

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* keyboard application collection with report ID 1, then consumer keys (ID 3) */
static const uint8_t desc_ids[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

/* boot-style keyboard without report IDs */
static const uint8_t desc_boot[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0,
};

/* a gamepad: not a keyboard */
static const uint8_t desc_pad[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0,
};

static void drain(void) { while (hid_getc() >= 0) ; hid_quit_pressed(); }

int main(void)
{
    uint8_t id = 99;
    CHECK(hid_last_source() == HID_SOURCE_NONE);
    CHECK(hid_is_keyboard(desc_ids, sizeof desc_ids, &id) == 1 && id == 1);
    CHECK(hid_is_keyboard(desc_boot, sizeof desc_boot, &id) == 1 && id == 0);
    CHECK(hid_is_keyboard(desc_pad, sizeof desc_pad, &id) == 0);

    /* boot protocol: 8 bytes */
    hid_keyboard_attach(0);
    const uint8_t h[8] = { 0, 0, 0x0B };                /* 'h' */
    const uint8_t up[8] = { 0 };
    hid_report(USB_KEYBOARD, h, 8);
    hid_report(USB_KEYBOARD, up, 8);
    CHECK(hid_getc() == 'h');
    CHECK(hid_getc() == -1);
    CHECK(hid_last_source() == HID_SOURCE_KEYBOARD);      /* the buttons shown on screen */

    /* report protocol with ID 1 (a keyboard that ignored SET_PROTOCOL) */
    hid_keyboard_attach(1);
    const uint8_t shift_a[9] = { 1, 0x02, 0, 0x04 };    /* 'A' */
    const uint8_t rel[9] = { 1 };
    hid_report(USB_KEYBOARD, shift_a, 9);
    hid_report(USB_KEYBOARD, rel, 9);
    CHECK(hid_getc() == 'A');
    CHECK(hid_getc() == -1);
    const uint8_t media[9] = { 3, 0xE9, 0 };            /* other report ID: ignored */
    hid_report(USB_KEYBOARD, media, 9);
    CHECK(hid_getc() == -1);
    const uint8_t esc[10] = { 1, 0, 0, 0x29 };          /* longer report, Esc */
    hid_report(USB_KEYBOARD, esc, 10);
    CHECK(hid_quit_pressed() == HID_QUIT_KEY);
    drain();
    hid_report(USB_KEYBOARD, rel, 9);
    const uint8_t kq[9] = { 1, 0, 0, 0x14 };           /* Q = L1: the menu's tabs */
    hid_buttons();                                      /* the earlier presses */
    hid_report(USB_KEYBOARD, kq, 9);
    CHECK(hid_buttons() == HID_L1);
    hid_report(USB_KEYBOARD, rel, 9);
    drain();
    hid_buttons();
    const uint8_t arrow[9] = { 1, 0, 0, 0x4F };         /* right arrow = game button */
    hid_report(USB_KEYBOARD, arrow, 9);
    CHECK(hid_buttons() & HID_RIGHT);
    hid_report(USB_KEYBOARD, rel, 9);
    CHECK(!(hid_buttons() & HID_RIGHT));
    /* the same keyboard back in boot protocol (8 bytes): still understood */
    hid_report(USB_KEYBOARD, h, 8);
    hid_report(USB_KEYBOARD, up, 8);
    CHECK(hid_getc() == 'h');

    /* Italian layout: ';' key = o grave (CP437 0x95), AltGr+it = '@' */
    hid_keyboard_attach(0);
    const uint8_t ograve[8] = { 0, 0, 0x33 };
    hid_report(USB_KEYBOARD, ograve, 8);
    hid_report(USB_KEYBOARD, up, 8);
    CHECK(hid_getc() == 0x95);
    const uint8_t at[8] = { 0x40, 0, 0x33 };
    hid_report(USB_KEYBOARD, at, 8);
    hid_report(USB_KEYBOARD, up, 8);
    CHECK(hid_getc() == '@');

    /* text mode (editors): F5, F6 and F12 as key codes (M30: F6 opens the
     * assistant); out of text mode they type nothing */
    hid_text_mode(1);
    const uint8_t f5[8] = { 0, 0, 0x3E }, f6[8] = { 0, 0, 0x3F }, f12[8] = { 0, 0, 0x45 };
    hid_report(USB_KEYBOARD, f5, 8);
    hid_report(USB_KEYBOARD, up, 8);
    hid_report(USB_KEYBOARD, f6, 8);
    hid_report(USB_KEYBOARD, up, 8);
    hid_report(USB_KEYBOARD, f12, 8);
    hid_report(USB_KEYBOARD, up, 8);
    CHECK(hid_getc() == HID_KEY_F1 + 4);
    CHECK(hid_getc() == HID_KEY_F6);
    CHECK(hid_getc() == HID_KEY_F6 + 6);
    hid_text_mode(0);
    hid_report(USB_KEYBOARD, f6, 8);
    hid_report(USB_KEYBOARD, up, 8);
    CHECK(hid_getc() == -1);

    /* DualShock 4: USB report 0x01, Bluetooth report 0x11 (2 more bytes) */
    hid_ds4_attach();
    uint8_t usb[64] = { 0x01, 128, 128, 128, 128, 0x08 };     /* centred, hat none */
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_buttons() == 0);
    CHECK(hid_last_source() == HID_SOURCE_KEYBOARD);           /* nothing pressed yet */
    usb[5] = 0x20 | 2;                                         /* cross + hat right */
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_buttons() == (HID_A | HID_RIGHT));
    CHECK(hid_last_source() == HID_SOURCE_DS4);
    usb[5] = 0x40 | 8; usb[2] = 10;                            /* circle + stick up */
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_buttons() == (HID_B | HID_UP));
    usb[2] = 128; usb[5] = 8; usb[6] = 0x01;                   /* L1, then R1 */
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_buttons() == HID_L1);
    usb[6] = 0x02;
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_buttons() == HID_R1);
    usb[6] = 0x20 | 0x10;                                      /* options + share */
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_buttons() == (HID_START | HID_SELECT));
    CHECK(hid_quit_pressed() == HID_QUIT_KEY);                 /* Start+Select */
    usb[6] = 0; usb[7] = 1;                                    /* PS button: home */
    hid_report(USB_GAMEPAD, usb, 64);
    CHECK(hid_quit_pressed() == HID_QUIT_PS);
    hid_report(USB_GAMEPAD, usb, 64);                          /* held: once only */
    CHECK(hid_quit_pressed() == 0);
    uint8_t bt[78] = { 0x11, 0xC0, 0x00, 128, 128, 128, 128, 0x10 | 6 };  /* square + left */
    hid_report(USB_GAMEPAD, bt, 78);
    CHECK(hid_buttons() == (HID_X | HID_LEFT));
    bt[7] = 0x80 | 8;                                          /* triangle */
    hid_report(USB_GAMEPAD, bt, 78);
    CHECK(hid_buttons() == HID_Y);

    /* press and release between two reads: seen once, then gone */
    hid_ds4_attach();
    uint8_t tap[64] = { 0x01, 128, 128, 128, 128, 0x08 | 0x20 };
    hid_report(USB_GAMEPAD, tap, 64);
    tap[5] = 0x08;
    hid_report(USB_GAMEPAD, tap, 64);
    CHECK(hid_buttons() == HID_A);
    CHECK(hid_buttons() == 0);

    /* M16: Bluetooth pads by player, the USB gamepad as the local player */
    uint32_t pl[HID_PLAYERS];
    uint8_t p1[11] = { 0x01, 128, 128, 128, 128, 0x08 | 0x20 };   /* player 1: cross */
    uint8_t p3[11] = { 0x11, 0xC0, 0x00, 0, 255, 128, 128, 0x08 };  /* player 3: stick left+down */
    hid_bt_report(0, p1, sizeof p1);
    hid_bt_report(2, p3, sizeof p3);
    tap[5] = 0x08 | 0x40;                                      /* USB pad: circle */
    hid_report(USB_GAMEPAD, tap, 64);
    uint32_t any = hid_players(pl, 0, 1, -1);                      /* USB = player 2 */
    CHECK(pl[0] == HID_A && pl[1] == HID_B && pl[2] == (HID_LEFT | HID_DOWN) && pl[3] == 0);
    CHECK(any == (HID_A | HID_B | HID_LEFT | HID_DOWN));
    int8_t xy[2];
    CHECK(hid_stick(2, xy) == 1 && xy[0] == -127 && xy[1] == 127);
    CHECK(hid_stick(1, xy) == 0);
    p1[5] = 0x08;                                              /* released before the read: */
    hid_bt_report(0, p1, sizeof p1);
    hid_bt_report(0, p1, sizeof p1);
    CHECK(hid_players(pl, 0, 1, -1) != 0 && pl[0] == 0);           /* the earlier read took it */
    hid_bt_clear(2);
    tap[5] = 0x08;
    hid_report(USB_GAMEPAD, tap, 64);
    hid_players(pl, 0, 1, -1);
    CHECK(hid_players(pl, 0, -1, -1) == 0 && pl[2] == 0);
    p1[5] = 0x08; p1[7] = 1;                                   /* PS on a Bluetooth pad quits */
    hid_bt_report(0, p1, sizeof p1);
    CHECK(hid_quit_pressed() == HID_QUIT_PS);

    /* the Bluetooth LE keyboard is a player of its own; with ble < 0 it
     * plays with the USB one; in text mode both keyboards only type */
    hid_kbd_layout_t kl = { .id = 1, .nkeys = 6, .mods_bit = 0, .keys_bit = 16, .bitmap_bit = -1 };
    uint8_t lr[8] = { 0, 0, 0x07 };                            /* D: right */
    hid_players(pl, 0, 0, 1);
    hid_ble_keyboard(&kl, lr, sizeof lr);
    CHECK(hid_players(pl, 0, 0, 1) == HID_RIGHT && pl[1] == HID_RIGHT && pl[0] == 0);
    CHECK(hid_players(pl, 0, 0, -1) == HID_RIGHT && pl[0] == HID_RIGHT && pl[1] == 0);
    CHECK(hid_players(pl, 1, 0, 1) == 0);                      /* it types instead */
    memset(lr, 0, sizeof lr);
    hid_ble_keyboard(&kl, lr, sizeof lr);
    hid_players(pl, 0, 0, 1);
    CHECK(hid_players(pl, 0, 0, 1) == 0);
    drain();

    /* Xbox 360: LB / RB are L1 / R1, Guide is like PS (once per press) */
    hid_xbox360_attach();
    uint8_t xb[20] = { 0x00, 0x14 };
    xb[3] = 0x01 | 0x02;
    hid_report(USB_XBOX360, xb, sizeof xb);
    CHECK(hid_buttons() == (HID_L1 | HID_R1));
    CHECK(hid_last_source() == HID_SOURCE_PAD);
    xb[3] = 0;
    hid_report(USB_XBOX360, xb, sizeof xb);
    p1[7] = 0;
    hid_bt_report(1, p1, sizeof p1);                           /* a Bluetooth DS4 */
    p1[5] = 0x08 | 0x40;
    hid_bt_report(1, p1, sizeof p1);
    CHECK(hid_last_source() == HID_SOURCE_DS4);
    p1[5] = 0x08;
    hid_bt_report(1, p1, sizeof p1);
    xb[3] = 0x01 | 0x02;
    hid_report(USB_XBOX360, xb, sizeof xb);
    hid_buttons();
    hid_quit_pressed();
    xb[3] = 0x04;
    hid_report(USB_XBOX360, xb, sizeof xb);
    CHECK(hid_quit_pressed() == HID_QUIT_PS);
    hid_report(USB_XBOX360, xb, sizeof xb);
    CHECK(hid_quit_pressed() == 0);

    printf("hid: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
