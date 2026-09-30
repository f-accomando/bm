/*
 * Bluetooth Low Energy keyboards (HID over GATT), next to the classic
 * Bluetooth pads of bt.c. Used by bt.c only; the monitor calls bt_*.
 */
#ifndef BLE_H
#define BLE_H

#include "hci.h"

#include <stdint.h>

/* Chip set up for LE (event mask, buffer size), the saved keyboard loaded
 * and looked for. addr: our public address. */
void ble_init(const uint8_t addr[6]);

/* An event or ACL packet for the LE link: 1 if it was ours. */
int ble_event(const hci_pkt_t *p);
int ble_acl(const hci_pkt_t *p);

/* Timers: connecting, answers that do not come, looking for the keyboard. */
void ble_poll(void);

/* Looks for a keyboard in pairing mode for `seconds` and pairs it (the
 * code to type on it is shown). 0 when it works. */
int ble_pair(unsigned seconds);

/* Forgets the keyboard (key removed from bm33/config.txt): 1 if there was one. */
int ble_forget(void);

/* 1 once a keyboard has been paired / while it is connected and typing. */
int ble_paired(void);
int ble_connected(void);

/* bt.c: one packet from the chip, handled; -1 if none came in timeout_us. */
int bt_pump(uint32_t timeout_us);

#endif
