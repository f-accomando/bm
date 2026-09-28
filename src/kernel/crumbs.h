/*
 * Breadcrumbs for freezes without a serial cable: what the console was
 * doing is kept in a small RAM area that the kernel never clears. The
 * watchdog restarts a frozen Pi; the SDRAM keeps its contents through the
 * reset, so the next boot can say on screen what was running.
 */
#ifndef CRUMBS_H
#define CRUMBS_H

#include <stdint.h>

void crumbs_boot(void);             /* reports a previous freeze, starts a new record */
void crumb(const char *what, const char *name);  /* what is running now (name may be NULL) */
void crumb_frame(uint32_t frame);   /* progress inside it */
void crumb_tick(uint32_t ms);       /* from the timer: uptime of the record */
void crumbs_clean_exit(void);       /* before an intentional reboot */
void crumb_irq(int irq);            /* IRQ being handled, -1 = none */
void crumbs_print(void);            /* "while: ..." for crash screens */

#endif
