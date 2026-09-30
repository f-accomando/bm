/*
 * Synopsys DesignWare USB 2.0 OTG controller (DWC2) of the BCM2835, host
 * mode, buffer-DMA, polled (no interrupts). One root port; devices behind
 * a hub too: low/full-speed ones behind a high-speed hub (the LAN951x of
 * the Pi 1 B, or any USB 2.0 hub) through its transaction translator
 * (split transactions).
 */
#ifndef DWC2_H
#define DWC2_H

#include <stdint.h>

enum usb_speed { USB_SPEED_HIGH = 0, USB_SPEED_FULL = 1, USB_SPEED_LOW = 2 };
enum usb_ep_type { EP_CONTROL = 0, EP_ISO = 1, EP_BULK = 2, EP_INTERRUPT = 3 };
enum usb_pid { PID_DATA0 = 0, PID_DATA2 = 1, PID_DATA1 = 2, PID_SETUP = 3 };

enum dwc2_result {
    XFER_OK = 0,
    XFER_NAK = -1,          /* interrupt endpoint had nothing to say */
    XFER_STALL = -2,
    XFER_ERROR = -3,
    XFER_TIMEOUT = -4,
};

typedef struct {
    uint8_t addr;           /* device address */
    uint8_t ep;             /* endpoint number */
    uint8_t in;             /* 1 = device to host */
    uint8_t type;           /* enum usb_ep_type */
    uint16_t mps;           /* max packet size */
    uint8_t speed;          /* enum usb_speed */
    uint8_t *toggle;        /* data toggle to use / update (PID_DATA0/1) */
    uint8_t hub_addr;       /* low/full-speed device behind a high-speed hub: */
    uint8_t hub_port;       /* its address and port (split transactions), else 0 */
} dwc2_pipe_t;

int  dwc2_init(void);                       /* power, reset, host mode */
int  dwc2_port_connected(void);
int  dwc2_port_reset(enum usb_speed *speed); /* 0 = device enabled */
uint32_t dwc2_frame(void);

/* Blocking transfer on channel `ch`. buf must be 4-byte aligned. For
 * interrupt pipes a NAK returns XFER_NAK at once; for others it retries
 * until timeout_ms (the controller retries NAKs by itself: a bulk IN with
 * no data gives up after timeout_ms). *actual receives the bytes
 * transferred. Split transactions go one packet at a time. */
int dwc2_transfer(int ch, const dwc2_pipe_t *p, uint8_t pid, void *buf,
                  uint32_t len, uint32_t *actual, uint32_t timeout_ms);

#endif
