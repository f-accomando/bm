#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

uint32_t timer_ticks(void);        /* free-running 1 MHz counter */
void     timer_delay_us(uint32_t us);
void     timer_delay_ms(uint32_t ms);

#endif
