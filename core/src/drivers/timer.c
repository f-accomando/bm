#include "timer.h"
#include "mmio.h"

#define SYSTIMER_CLO (PERIPHERAL_BASE + 0x3004)

uint32_t timer_ticks(void)
{
    return mmio_read(SYSTIMER_CLO);
}

void timer_delay_us(uint32_t us)
{
    uint32_t start = timer_ticks();
    while (timer_ticks() - start < us)
        ;
}

void timer_delay_ms(uint32_t ms)
{
    while (ms--)
        timer_delay_us(1000);
}
