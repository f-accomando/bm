/*
 * The BCM2835 hardware random number generator (M19: TLS keys).
 */
#ifndef RNG_H
#define RNG_H

#include <stddef.h>
#include <stdint.h>

/* Fills buf; 0, or -1 if the generator gives nothing (then the caller
 * must not make keys). The first call starts it. */
int rng_read(void *buf, size_t len);

#endif
