/*
 * WiFi of the Pi Zero W (BCM43438 over SDIO), M18. First step: bring the
 * chip up and identify it, every step on screen.
 */
#ifndef WIFI_H
#define WIFI_H

/* Powers and identifies the WiFi chip; prints each step. 0 if it answered. */
int wifi_probe(void);

#endif
