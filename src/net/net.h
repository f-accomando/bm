/*
 * The network stack (lwIP, polled) on top of the WiFi data path. M18.
 */
#ifndef NET_H
#define NET_H

#include <stdint.h>

/* After wifi_connect: the interface up, DHCP started. The address is
 * printed when the access point gives it ("net: IP ..."). */
int  net_start(void);
/* Asks for an address and waits up to `ms` for it; 0 once bound. */
int  net_wait_ip(uint32_t ms);
/* Moves frames and lwIP's timers; called from the input loops, cheap when
 * WiFi is off (at most once per millisecond otherwise). */
void net_poll(void);
/* One round of the network (frames, timers) and a short pause: for code
 * that waits for an answer (stream.c). */
void net_wait_step(void);
/* Seconds since 1970 (UTC) from the network (SNTP), 0 until known. */
unsigned long net_time(void);
/* "2026-09-29 18:04 UTC", or "unknown" */
const char *net_time_text(void);
/* The IPv4 address (network order), 0 if none. */
uint32_t net_ip(void);
/* "192.168.1.23", or "-" */
const char *net_ip_text(void);
/* The link the console is on. bm33 has no Ethernet interface yet (the Pi
 * Zero W has no port; a USB adapter would need its driver): Ethernet is
 * here for when it has one. */
#define NET_LINK_NONE     0
#define NET_LINK_WIFI     1
#define NET_LINK_ETHERNET 2
int net_link(void);

#endif
