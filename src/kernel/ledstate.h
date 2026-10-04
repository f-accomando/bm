/*
 * The board's status LED (the user's decision, 2026-10-04): on and still
 * when nothing is wrong; a slow blink (a second on, a second off) for
 * anything else: starting up, a problem (no SD card, the power, the
 * screen), a new kernel or an update coming in. The reasons are bits: the
 * LED blinks while any of them is set. The Pi and the RGB30 alike (on the
 * RGB30 the green LED; its red one keeps saying where a hang is at boot).
 * The fatal errors keep their blink codes (led_blink_code).
 */
#ifndef LEDSTATE_H
#define LEDSTATE_H

#include <stdint.h>

#define LED_BOOT     0x01u      /* starting up, until the menu */
#define LED_NO_SD    0x02u      /* no SD card, or one that cannot be read */
#define LED_POWER    0x04u      /* the Pi's supply too low, the RGB30's battery low */
#define LED_DISPLAY  0x08u      /* the screen did not come up (the RGB30) */
#define LED_BUSY     0x10u      /* an update being installed */

void     ledstate_set(unsigned reason, int on);
unsigned ledstate(void);        /* the reasons set, and a kernel arriving (LED_BUSY) */
/* From the timer's tick (ms since boot): sets the LED. */
void     ledstate_tick(uint32_t ms);

#endif
