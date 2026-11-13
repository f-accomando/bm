#ifndef MBOX_H
#define MBOX_H

#include <stdint.h>

#define MBOX_CH_PROP        8

#define MBOX_REQUEST        0x00000000u
#define MBOX_RESPONSE_OK    0x80000000u
#define MBOX_TAG_LAST       0x00000000u

/* Sends a message buffer (must be 16-byte aligned) on the given channel and
 * waits for the reply. Returns 1 on success. */
int mbox_call(uint8_t channel, volatile uint32_t *buf);

/* The firmware (the VideoCore) can stop answering for good: every call then waits MBOX_TIMEOUT_US and
 * fails, the screen cannot change mode or flip, and the Pi looks frozen with the network alive (the
 * freeze of 26 Oct 2026, 3 hours after boot). After MBOX_DEAD_TIMEOUTS timeouts in a row the hook runs,
 * once. A reply, even a refusal, starts the count again. */
#define MBOX_DEAD_TIMEOUTS 3
static inline int mbox_is_dead(unsigned timeouts_in_a_row)
{
    return timeouts_in_a_row >= MBOX_DEAD_TIMEOUTS;
}
void mbox_set_dead_hook(void (*fn)(void));

/* A call that gets no answer prints one line (the channel, the first tag, which wait gave up, the status register, how
 * long ago the last answer came) and runs this hook, so the kernel can add what else it knows (the V3D). Once for
 * each run of timeouts (30 Oct 2026: the second firmware stop, at the video mode change of an app). */
void mbox_set_diag_hook(void (*fn)(void));

#endif
