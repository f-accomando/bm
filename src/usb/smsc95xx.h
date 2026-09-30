/*
 * Ethernet of the Pi 1 B / B+: the SMSC (Microchip) LAN9512 / LAN9514, a
 * USB hub with a 10/100 Ethernet controller on its port 1 (USB id
 * 0424:ec00), set up the way Linux's smsc95xx driver does. One frame per
 * bulk transfer; with nothing waiting the chip answers an empty packet
 * (HW_CFG.BIR), so a poll never waits. The board has no EEPROM: the MAC
 * address comes from the firmware.
 */
#ifndef SMSC95XX_H
#define SMSC95XX_H

#include "usb.h"

/* 1 for the USB ids this driver takes (LAN9512/9514 and the LAN9500 family). */
int  eth_match(uint16_t vid, uint16_t pid);
/* Resets and configures the controller (the device is configured already;
 * cfg: its configuration descriptor). 0 once it can send and receive. */
int  eth_attach(usb_dev_t *d, const uint8_t *cfg, uint16_t cfg_len);
/* Forgets the device (before the USB bus is enumerated again). */
void eth_detach(void);
int  eth_present(void);
/* One line: chip, MAC address, link. */
void eth_print(void);
/* Counters and registers, for the monitor ('E'). */
void eth_diag(void);

/* The data path, for the network stack (src/net). */
const unsigned char *eth_mac(void);
int  eth_linked(void);                  /* cable in, link up */
/* Link state from the PHY (at most every 500 ms; prints the changes). */
void eth_poll(void);
/* The next received Ethernet frame (without its CRC) into buf; its length,
 * 0 if none. */
int  eth_recv(void *buf, int max);
/* Sends one Ethernet frame (14-byte header included); 0 or -1. */
int  eth_send(const void *frame, int len);

#endif
