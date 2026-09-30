/*
 * Minimal USB host stack: the device on the root port or, behind a hub,
 * the device on each hub port (the Pi 1 B always has a hub: the LAN951x,
 * with the Ethernet controller on port 1). Enumerated and configured at
 * boot; one HID device is used (keyboards with the boot protocol, generic
 * HID gamepads, Xbox 360 wired controllers), the Ethernet goes to
 * smsc95xx.c. Polled from the main loop (usb_poll).
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

/* An enumerated device (address set, configuration selected). */
typedef struct {
    uint8_t addr, speed;
    uint8_t hub_addr, hub_port;     /* transaction translator (split), 0 = none */
    uint8_t port;                   /* port of its hub, 0 = root port */
    uint8_t cls;                    /* device class, 9 = hub */
    uint16_t mps0, vid, pid;
    char name[48];
} usb_dev_t;

/* Initialises the controller and enumerates what is attached (through the
 * hub, if there is one). Returns the kind of the HID device in use
 * (USB_NONE if nothing is attached, USB_OTHER if nothing usable). */
int usb_init(void);
const usb_info_t *usb_info(void);
/* A few lines on the console: hub, Ethernet, the device in use. */
void usb_print(void);

/* Transfer counters and the last report, one line. */
void usb_diag(char *buf, unsigned size);
/* Polls for `seconds` showing usb_diag live on the console. */
void usb_live_test(uint32_t seconds);

/* Polls the HID endpoint (rate-limited to its bInterval). Call often. */
void usb_poll(void);

/* For device drivers. A control request with a data stage of len bytes
 * (at most 512): the bytes transferred, or < 0 (enum dwc2_result). */
int usb_control(usb_dev_t *d, uint8_t req_type, uint8_t req, uint16_t value,
                uint16_t index, void *data, uint16_t len);
/* A bulk transfer on endpoint `ep` (mps: its max packet size; toggle: its
 * data toggle, PID_DATA0 after the configuration). buf: 4-byte aligned,
 * and for IN whole cache lines. 0 or < 0 (enum dwc2_result). */
int usb_bulk(usb_dev_t *d, uint8_t ep, int in, uint16_t mps, uint8_t *toggle,
             void *buf, uint32_t len, uint32_t *actual, uint32_t timeout_ms);

#endif
