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
/* The IPv4 address (network order), 0 if none. */
uint32_t net_ip(void);
/* "192.168.1.23", or "-" */
const char *net_ip_text(void);

#endif
