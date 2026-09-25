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

#endif
