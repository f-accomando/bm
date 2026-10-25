#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* Full SoC reset through the PM watchdog; the firmware boots again from SD. */
void watchdog_reboot(void) __attribute__((noreturn));
void watchdog_before_reboot(void (*fn)(void));
/* The same reset, but the firmware halts (Settings > Shut down): off until
 * the power is plugged again. The RGB30: the PMIC turns it off. */
void watchdog_halt(void) __attribute__((noreturn));

/* Freeze guard: the SoC resets if watchdog_pet() is not called within ms
 * (up to 15 s). watchdog_stop() turns it off (crash screens stay). */
int  watchdog_arm(uint32_t ms);         /* -1: no working watchdog (emulator) */
/* 1 under an emulator: the board serial is no number there. QEMU has no PWM
 * block (a write is a data abort). */
int  watchdog_emulated(void);
/* What the last watchdog_arm loaded and read back 20 ms later. */
void watchdog_probe_values(uint32_t *loaded, uint32_t *left);
void watchdog_pet(void);
void watchdog_stop(void);

/* Main-loop guard. The 1 kHz timer IRQ calls watchdog_pet_irq(), not watchdog_pet():
 * an IRQ that pets for ever would hide a hang of the main code (the Pi froze for
 * hours with a timer that still ran). The main code calls watchdog_progress() each
 * time it reaches a poll, a frame, a flip or a new crumb. Once it has called it,
 * watchdog_pet_irq() pets only while the last call is at most WATCHDOG_STALL_US
 * old. After that the SoC resets within the armed time and the next boot reports
 * a freeze with the crumb that was set. Before the first call it always pets
 * (boot has no poll). */
#define WATCHDOG_STALL_US 20000000u
void watchdog_progress(void);
void watchdog_pet_irq(void);

/* The decision, for the tests: mark 0 = no call yet. */
static inline int watchdog_fresh(uint32_t now_us, uint32_t mark_us, uint32_t limit_us)
{
    return mark_us == 0 || (uint32_t)(now_us - mark_us) <= limit_us;
}

#endif
