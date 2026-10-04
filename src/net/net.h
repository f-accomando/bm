/*
 * The network stack (lwIP, polled) on top of a data path: the WiFi chip
 * (Pi Zero W, M18) or the Ethernet of the Pi 1 B (LAN951x over USB).
 */
#ifndef NET_H
#define NET_H

#include <stdint.h>

/* A network interface's data path. */
typedef struct {
    char name[3];                           /* lwIP interface name: "wl", "en" */
    const unsigned char *(*mac)(void);
    int  (*linked)(void);                   /* link up */
    void (*poll)(void);                     /* cheap: called from the input loops */
    int  (*recv)(void *buf, int max);       /* the next frame, 0 if none */
    int  (*send)(const void *frame, int len);
    int  keeps_dhcp;                        /* a lost link keeps the lease (cable) */
} net_link_t;

extern const net_link_t net_wifi, net_eth;

/* The interface up on this data path, DHCP started (after wifi_connect;
 * the Ethernet may still be without link: DHCP waits for it). The address
 * is printed when the network gives it ("net: IP ..."). */
int  net_start(const net_link_t *link);
/* Asks for an address and waits up to `ms` for it; 0 once bound. */
int  net_wait_ip(uint32_t ms);
/* Moves frames and lwIP's timers; called from the input loops, cheap when
 * WiFi is off (at most once per millisecond otherwise). */
void net_poll(void);
/* One round of the network (frames, timers) and a short pause: for code
 * that waits for an answer (stream.c). Inside a fiber (fiber.h) the pause
 * is the rest of the menu's frame instead; -1 if the fiber was cancelled:
 * stop waiting. */
int  net_wait_step(void);
/* Seconds since 1970 (UTC) from the network (SNTP), 0 until known. */
unsigned long net_time(void);
/* "2026-09-29 18:04 UTC", or "unknown" */
const char *net_time_text(void);
/* The IPv4 address (network order), 0 if none. */
uint32_t net_ip(void);
/* "192.168.1.23", or "-" */
const char *net_ip_text(void);
/* The kind of link the console is on, for the menu's icon: the WiFi of the
 * Pi Zero W or the Ethernet cable of the Pi 1 B / B+, with its link up;
 * NET_LINK_NONE when the network is not started or the link is down. */
#define NET_LINK_NONE     0
#define NET_LINK_WIFI     1
#define NET_LINK_ETHERNET 2
int net_link_kind(void);

#endif
