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
    CHECK(hid_quit_pressed() == 1);
    drain();
    hid_report(USB_KEYBOARD, rel, 9);
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

    printf("hid: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
