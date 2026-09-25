#ifndef WATCHDOG_H
#define WATCHDOG_H

/* Full SoC reset through the PM watchdog; the firmware boots again from SD. */
void watchdog_reboot(void) __attribute__((noreturn));

#endif
