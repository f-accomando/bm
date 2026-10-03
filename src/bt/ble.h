/*
 * Bluetooth Low Energy keyboards and mice (HID over GATT), next to the
 * classic Bluetooth pads of bt.c: one keyboard and one mouse. Used by bt.c
 * only; the monitor calls bt_*.
 */
#ifndef BLE_H
#define BLE_H

#include "hci.h"

#include <stdint.h>

enum { LE_KBD, LE_MOUSE, LE_DEVS };

/* Chip set up for LE (event mask, buffer size), the saved keyboard and
 * mouse loaded and looked for. addr: our public address. */
void ble_init(const uint8_t addr[6]);

/* An event or ACL packet for the LE link: 1 if it was ours. */
int ble_event(const hci_pkt_t *p);
int ble_acl(const hci_pkt_t *p);

/* Timers: connecting, answers that do not come, looking for the paired
 * devices. */
void ble_poll(void);

/* Looks for a keyboard (LE_KBD) or a mouse (LE_MOUSE) in pairing mode for
 * `seconds` and pairs it (for a keyboard, the code to type on it is
 * shown). 0 when it works, -2 when none was found, -1 on other failures. */
int ble_pair(int kind, unsigned seconds);

/* Forgets the keyboard and the mouse (keys removed from bm/config.txt):
 * how many there were. */
int ble_forget(void);

/* 1 once one of that kind has been paired / while it is connected and
 * its reports come. */
int ble_paired(int kind);
int ble_connected(int kind);

/* bt.c: one packet from the chip, handled; -1 if none came in timeout_us. */
int bt_pump(uint32_t timeout_us);

#endif
