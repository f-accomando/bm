/*
 * The BCM2835 hardware random number generator (M19: TLS keys).
 */
#ifndef RNG_H
#define RNG_H

#include <stddef.h>
#include <stdint.h>

/* Starts the generator. Its first numbers come only after a warm-up, so
 * the kernel starts it at boot; rng_read starts it too if needed. */
void rng_start(void);

/* Fills buf; 0, or -1 if the generator gives nothing (then the caller
 * must not make keys). The first word may wait for the warm-up. */
int rng_read(void *buf, size_t len);

#endif
