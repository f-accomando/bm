#ifndef PROP_H
#define PROP_H

#include <stdint.h>

#define PROP_GET_BOARD_REVISION 0x00010002u
#define PROP_GET_BOARD_SERIAL   0x00010004u
#define PROP_GET_ARM_MEMORY     0x00010005u
#define PROP_GET_VC_MEMORY      0x00010006u
#define PROP_GET_CLOCK_RATE     0x00030002u
#define PROP_GET_TEMPERATURE    0x00030006u
#define PROP_GET_MAX_CLOCK_RATE 0x00030004u
#define PROP_SET_CLOCK_RATE     0x00038002u
#define PROP_GET_THROTTLED      0x00030046u

#define CLOCK_UART  2
#define CLOCK_ARM   3
#define CLOCK_CORE  4
#define CLOCK_V3D   5
#define CLOCK_SDRAM 8

/* Runs a single property tag. vals[0..n-1] is the request value buffer and
 * receives the response. Returns 0 on success. */
int prop_query(uint32_t tag, uint32_t *vals, unsigned n);

/* Convenience: rate in Hz of the given clock id, 0 on failure. */
uint32_t prop_clock_rate(uint32_t clock_id);
/* The highest rate the firmware allows for the clock, 0 on failure. */
uint32_t prop_clock_max(uint32_t clock_id);

/* Raises a clock to the maximum the firmware allows (arm_freq in
 * config.txt); returns the new rate in Hz, 0 on failure. The firmware boots
 * the Pi Zero ARM at 700 MHz and only goes to 1 GHz when asked. */
uint32_t prop_clock_set_max(uint32_t clock_id);

#endif
