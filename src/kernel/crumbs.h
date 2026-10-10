/*
 * Breadcrumbs for freezes without a serial cable: what the console was
 * doing is kept in a small RAM area that the kernel never clears. The
 * watchdog restarts a frozen Pi; the SDRAM keeps its contents through the
 * reset, so the next boot can say on screen what was running.
 */
#ifndef CRUMBS_H
#define CRUMBS_H

#include <stddef.h>
#include <stdint.h>

void crumbs_boot(void);             /* reports a previous freeze, starts a new record */
void crumb(const char *what, const char *name);  /* what is running now (name may be NULL) */
void crumb_frame(uint32_t frame);   /* progress inside it */
void crumb_tick(uint32_t ms);       /* from the timer: uptime of the record */
void crumbs_clean_exit(void);       /* before an intentional reboot */
void crumb_irq(int irq);            /* IRQ being handled, -1 = none */
void crumbs_print(void);            /* "while: ..." for crash screens */
void crumbs_crashed(void);          /* the crash screen is done: the next boot says so */
uint32_t crumbs_uptime_ms(void);    /* of this boot, as the timer last said */
/* The boot before ended in a crash or a freeze: its kind ("crash",
 * "freeze") and text with the last lines printed, once, for a report;
 * NULL if it ended well (or the power went). */
const char *crumbs_last(const char **kind, size_t *len);
/* The last lines printed kept in RAM (4 KiB), oldest first, into out;
 * returns the length. Before crumbs_boot(): the run before's. */
size_t crumbs_ring_copy(char *out, size_t size);
/* Before crumbs_boot(): how the run before ended, one line. */
const char *crumbs_prev_state(void);

#endif
