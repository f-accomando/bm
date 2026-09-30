/*
 * Bluetooth on the Pi Zero W (BCM43438), M12 and M16: up to four HID game
 * controllers (DualShock 4), one per player, paired once and then
 * reconnected with the PS button.
 */
#ifndef BT_H
#define BT_H

#define BT_PADS 4                   /* players with a Bluetooth pad */

/* Moves the console to the mini UART, starts the chip (firmware patch
 * from the SD card), turns on page scan for the paired pads. Returns 0. */
int bt_start(void);

/* Searches for `seconds`; pairs the first game controller found as the
 * next free player (bt_pad1..bt_pad4 in bm/config.txt). */
void bt_scan(unsigned seconds);

/* Looks for a Bluetooth LE keyboard in pairing mode for `seconds` and
 * pairs it: a code is shown, to type on the keyboard (bt_kbd and
 * bt_kbd_key in bm/config.txt). Afterwards it comes back by itself. */
void bt_pair_keyboard(unsigned seconds);

/* 1 while the Bluetooth keyboard is connected; 1 once one is paired. */
int bt_keyboard(void);
int bt_keyboard_paired(void);

/* Processes what the chip sent (connections, HID reports). Call often;
 * does nothing until bt_start. */
void bt_poll(void);

/* Forgets all pads and the keyboard (links dropped, keys removed from
 * bm/config.txt). Returns how many keys there were. */
int bt_forget_all(void);

/* 1 once a pad or a keyboard has been paired (bt_pad, bt_kbd keys in
 * bm/config.txt). */
int bt_paired(void);

/* 1 while at least one pad has its HID channels open. */
int bt_connected(void);

/* Players with a connected pad: bit n = player n + 1. */
unsigned bt_pads(void);

/* Address of player slot+1's pad ("" if none); 1 if it is connected. */
int bt_pad_addr(int slot, char out[18]);

/* Starts the chip's 32.768 kHz sleep clock (GPCLK2 on GPIO43), needed by
 * both its Bluetooth and its WiFi half. */
const char *bcm43438_lpo_clock(void);

#endif
