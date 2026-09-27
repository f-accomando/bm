/*
 * Bluetooth on the Pi Zero W (BCM43438), M12. Step 1: bring the chip up
 * (power, 32 kHz clock, firmware patch) and look for devices.
 */
#ifndef BT_H
#define BT_H

/* Moves the console to the mini UART, starts the chip and loads the
 * firmware patch from the SD card. Prints what happens. Returns 0. */
int bt_start(void);

/* Inquiry for `seconds`: prints each device found (address, class). */
void bt_scan(unsigned seconds);

#endif
