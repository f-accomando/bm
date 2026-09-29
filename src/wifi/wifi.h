/*
 * WiFi of the Pi Zero W (BCM43438 over SDIO), M18. First step: bring the
 * chip up and identify it, every step on screen.
 */
#ifndef WIFI_H
#define WIFI_H

/* Powers and identifies the WiFi chip; prints each step. 0 if it answered. */
int wifi_probe(void);

/* wifi_probe, then the firmware from the SD card (bm33/brcmfmac43430-sdio.bin
 * and .txt, .clm_blob if present) into the chip, started; prints the
 * firmware version and the MAC address. 0 once the firmware answers. */
int wifi_start(void);

#endif
