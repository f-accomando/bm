/*
 * Bluetooth on the Pi Zero W (BCM43438), M12: one HID game controller
 * (DualShock 4), paired once and then reconnected with the PS button.
 */
#ifndef BT_H
#define BT_H

/* Moves the console to the mini UART, starts the chip (firmware patch
 * from the SD card), turns on page scan for a paired pad. Returns 0. */
int bt_start(void);

/* Searches for `seconds`; pairs with the first game controller found. */
void bt_scan(unsigned seconds);

/* Processes what the chip sent (connections, HID reports). Call often;
 * does nothing until bt_start. */
void bt_poll(void);

/* 1 once a pad has been paired (bt_pad in bm33/config.txt). */
int bt_paired(void);

/* 1 while the pad's HID channels are open. */
int bt_connected(void);

#endif
