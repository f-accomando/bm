#ifndef TICK_H
#define TICK_H

#include <stdint.h>

/* Periodic system tick from system timer compare 1 (IRQ 1). */
void     tick_init(uint32_t hz);
uint32_t tick_hz(void);
uint32_t tick_count(void);      /* ticks since tick_init */
uint32_t tick_ms(void);         /* milliseconds since tick_init */
uint32_t tick_late(void);       /* ticks that fired late (>1 period) */

/* Optional per-tick callback, runs in IRQ context: keep it short. */
void tick_set_hook(void (*hook)(uint32_t tick));

#endif
