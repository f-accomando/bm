#ifndef STALLS_H
#define STALLS_H

#include "drivers/timer.h"

#include <stdint.h>

/* The long single calls that do not give the CPU back (multitasking stage 1,
 * a measurement: no behavior change). Each call is timed from timer_ticks()
 * at its start and end; nothing else runs in the hot path. menu_ui.c prints
 * the "menu stalls:" line from these counters and starts them again.
 * The definitions are weak: the host tests that link only tls.c need none. */
typedef struct {
    uint32_t n;                 /* calls since the last report */
    uint32_t last, worst;       /* us: the last call, the longest */
    uint32_t bytes;             /* bytes hashed (hash only) */
} stall_t;

extern stall_t stall_tls;       /* one TLS handshake (tls.c) */
extern stall_t stall_verify;    /* one release_verify: SHA-256 + ECDSA (update.c) */
extern stall_t stall_hash;      /* one release_check_file: SHA-256 of a buffer (update.c) */

__attribute__((weak)) stall_t stall_tls, stall_verify, stall_hash;

static inline void stall_note(stall_t *s, uint32_t t0, uint32_t bytes)
{
    const uint32_t us = timer_ticks() - t0;
    s->n++;
    s->last = us;
    s->bytes += bytes;
    if (us > s->worst)
        s->worst = us;
}

#endif
