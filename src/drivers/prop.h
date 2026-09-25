#ifndef PROP_H
#define PROP_H

#include <stdint.h>

#define PROP_GET_BOARD_REVISION 0x00010002u
#define PROP_GET_BOARD_SERIAL   0x00010004u
#define PROP_GET_ARM_MEMORY     0x00010005u
#define PROP_GET_VC_MEMORY      0x00010006u
#define PROP_GET_CLOCK_RATE     0x00030002u
#define PROP_GET_TEMPERATURE    0x00030006u

#define CLOCK_UART  2
#define CLOCK_ARM   3
#define CLOCK_CORE  4

/* Runs a single property tag. vals[0..n-1] is the request value buffer and
 * receives the response. Returns 0 on success. */
int prop_query(uint32_t tag, uint32_t *vals, unsigned n);

/* Convenience: rate in Hz of the given clock id, 0 on failure. */
uint32_t prop_clock_rate(uint32_t clock_id);

#endif
