/*
 * WiFi of the Pi Zero W (BCM43438 over SDIO), M18. First step: bring the
 * chip up and identify it, every step on screen.
 */
#ifndef WIFI_H
#define WIFI_H

/* Powers and identifies the WiFi chip; prints each step. 0 if it answered. */
int wifi_probe(void);

/* wifi_probe, then the firmware from the SD card (bm/brcmfmac43430-sdio.bin
 * and .txt, .clm_blob if present) into the chip, started; prints the
 * firmware version and the MAC address. 0 once the firmware answers. */
int wifi_start(void);

/* Scans every channel and lists the networks (signal, channel, security,
 * name). Returns how many, -1 if WiFi is not started. */
int wifi_scan(void);

/* After wifi_scan: joins the network saved in bm/config.txt (wifi_ssid,
 * wifi_psk) if it is in range, else asks which one and its password and
 * saves them. WPA2-PSK, WPA-PSK or open; the firmware does the handshake. */
int wifi_connect(void);

/* Joins the network saved in bm/config.txt (a scan first, for its
 * security; at boot and from wifi_auto.c); -1 if none is saved or the join
 * fails. Inside a fiber its waits give the CPU back (fiber_slice). */
int wifi_connect_saved(void);

/* 1 once wifi_start has brought the radio up (not again until a restart). */
int  wifi_up(void);
/* Before a restart: the network is told we leave, so that the access point
 * does not hold the old association when the console comes back. */
void wifi_leave(void);

/* The data path, for the network stack (src/net). */
int  wifi_linked(void);                 /* joined, link up */
const unsigned char *wifi_mac(void);
/* Reads the frames waiting in the chip: data frames are queued, a lost
 * link is noted. Cheap enough for the input loops. */
void wifi_poll(void);
/* The next queued Ethernet frame into buf; its length, 0 if none. */
int  wifi_recv(void *buf, int max);
/* Sends one Ethernet frame (14-byte header included); 0 or -1. */
int  wifi_send(const void *eth, int len);

#endif
