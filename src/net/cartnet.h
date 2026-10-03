/*
 * UDP for the cartridges (M31.5: Overbit's matches on the network): a few
 * sockets, the packets that arrive queued until the game reads them (lwIP
 * calls back from net_poll, in the main loop). bmhost has the same
 * functions on the PC's sockets (tests/host/hostnet.c), so two bmhost can
 * play together.
 */
#ifndef CARTNET_H
#define CARTNET_H

#include <stdint.h>

#define CARTNET_SOCKETS 2
#define CARTNET_QUEUE   48          /* packets waiting on a socket */
#define CARTNET_MAX     1024        /* bytes in a packet, at most */
#define CARTNET_BROADCAST 0xFFFFFFFFu

/* A socket on `port` (0: any), from 0; -1 without network or a free one. */
int  cartnet_open(uint16_t port);
/* The port a socket listens on (useful with port 0). */
uint16_t cartnet_port(int s);
/* Sends `len` bytes to ip (host order; CARTNET_BROADCAST: the LAN):port.
 * 0 when handed to the network. */
int  cartnet_send(int s, uint32_t ip, uint16_t port, const void *data, int len);
/* The next packet: its length (into buf, at most max), and where it came
 * from; -1 if none waits. */
int  cartnet_recv(int s, void *buf, int max, uint32_t *ip, uint16_t *port);
void cartnet_close(int s);
/* Every socket closed (a cartridge ends). */
void cartnet_reset(void);
/* The console's address (host order), 0 without network. */
uint32_t cartnet_ip(void);
/* A name to an address: the address once known (host order), 0 while it is
 * being looked up (ask again next frame), CARTNET_BROADCAST if it fails. */
uint32_t cartnet_resolve(const char *name);

#endif
