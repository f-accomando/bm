#ifndef MONITOR_H
#define MONITOR_H

/* Single-key debug monitor on the serial console. Never returns. */
void monitor_run(void) __attribute__((noreturn));

#endif
