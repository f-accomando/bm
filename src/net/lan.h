/*
 * Games between consoles on the home network (M24, part of the Market).
 * While its Market tab is open (or the "Send to a nearby console" panel),
 * a console says hello every 2 s with a UDP broadcast on port 3335 and
 * listens on TCP port 3336. Another console sends it one of its games:
 * the receiver's player is asked first; the bytes must have the SHA-256
 * the sender announced, and market.c says whether they are a game of the
 * Market's catalog (checked) or not (from a friend only).
 *
 * TCP 3336, sender -> receiver: "BMLX", 1, then length-prefixed (one
 * byte) sender name, title and author, the size (4 bytes, little endian)
 * and the SHA-256 (32 bytes). Receiver -> sender: "OK" (accepted), "NO"
 * (refused or no answer in 60 s), "BZ" (busy), "BH" (bad header), "BS"
 * (too big). After OK the size bytes, then "OK" (kept) or "BD" (damaged).
 * Over lwIP's raw API, polled: portable (make test-lan, on loopback).
 */
#ifndef LAN_H
#define LAN_H

#include <stddef.h>
#include <stdint.h>

#define LAN_HELLO_PORT 3335
#define LAN_PORT       3336
#define LAN_MAX_PEERS  8
#define LAN_MAX_SIZE   (8u << 20)

typedef struct {
    uint32_t ip;                /* network order */
    char name[24];
    uint32_t seen;              /* timer_ticks() of its last hello */
} lan_peer_t;

typedef struct {
    char from[24];
    uint32_t ip;
    char title[49], author[33];
    uint32_t size;
    uint8_t sha256[32];
} lan_offer_t;

/* Hello and the server on, with this console's name; 0, or -1. */
int  lan_start(const char *name);
/* Everything off: a transfer in progress is dropped. */
void lan_stop(void);
int  lan_running(void);
/* Each frame while on: the hello every 2 s, peers silent for 7 s leave. */
void lan_poll(void);
/* The consoles heard lately (at most max). */
int  lan_peers(lan_peer_t *out, int max);

/* 1 and the offer, while one waits for the player's answer. */
int  lan_offer(lan_offer_t *o);
void lan_answer(int yes);
/* Bytes received of the accepted game, or -1 if none is coming. */
long lan_receiving(void);
/* 1 once the accepted game arrived with its SHA-256: the buffer is the
 * caller's (free it). */
int  lan_take(uint8_t **data, size_t *len, lan_offer_t *o);

/* Sends a game to the console at ip (network order) and waits for its
 * player's answer (blocking, net_wait_step). 0, or -1 with err. */
int  lan_send(uint32_t ip, const char *from, const char *title, const char *author,
              const uint8_t *data, size_t len, void (*progress)(const char *), char *err, size_t err_len);

/* The hello datagram ("BMHI 1 <name>"): written into buf (its length),
 * and read back (0 and the name, or -1). */
int  lan_hello(char *buf, size_t n, const char *name);
int  lan_parse_hello(const char *buf, size_t n, char *name, size_t nn);

#endif
