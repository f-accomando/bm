#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* Full SoC reset through the PM watchdog; the firmware boots again from SD. */
void watchdog_reboot(void) __attribute__((noreturn));

/* Freeze guard: the SoC resets if watchdog_pet() is not called within ms
 * (up to 15 s). watchdog_stop() turns it off (crash screens stay). */
int  watchdog_arm(uint32_t ms);         /* -1: no working watchdog (emulator) */
void watchdog_pet(void);
void watchdog_stop(void);

#endif
