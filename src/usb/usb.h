/*
 * Minimal USB host stack: one device on the root port, enumerated and
 * configured; HID keyboards (boot protocol), generic HID gamepads and
 * Xbox 360 wired controllers. Polled from the main loop (usb_poll).
 */
#ifndef USB_H
#define USB_H

#include <stdint.h>

enum usb_kind { USB_NONE, USB_KEYBOARD, USB_GAMEPAD, USB_XBOX360, USB_OTHER };

typedef struct {
    int kind;
    uint16_t vid, pid;
    uint8_t speed;
    char name[48];
} usb_info_t;

/* Initialises the controller and enumerates the device on the port.
 * Returns the kind of device found (USB_NONE if nothing is attached). */
int usb_init(void);
const usb_info_t *usb_info(void);
/* One line on the console: what is attached. */
void usb_print(void);

/* Transfer counters and the last report, one line. */
void usb_diag(char *buf, unsigned size);
/* Polls for `seconds` showing usb_diag live on the console. */
void usb_live_test(uint32_t seconds);

/* Polls the HID endpoint (rate-limited to its bInterval). Call often. */
void usb_poll(void);

#endif
