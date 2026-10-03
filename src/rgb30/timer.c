/*
 * ARM generic timer: the counter (24 MHz on the RK3566, 62.5 MHz in QEMU)
 * gives the 1 MHz timer_ticks() of drivers/timer.h, and the EL1 physical
 * timer (PPI 14, INTID 30) the periodic tick of kernel/tick.h.
 */
#include "drivers/timer.h"
#include "kernel/tick.h"
#include "kernel/irq.h"
#include "plat.h"
#include "a64.h"

static uint64_t cnt_hz;

static inline uint64_t counter(void)
{
    isb();
    return read_sysreg(cntpct_el0);
}

static uint64_t freq(void)
{
    if (!cnt_hz)
        cnt_hz = read_sysreg(cntfrq_el0);
    return cnt_hz;
}

uint64_t timer_us64(void)
{
    uint64_t c = counter(), f = freq();
    return (c / f) * 1000000u + (c % f) * 1000000u / f;
}

uint32_t timer_ticks(void)
{
    return (uint32_t)timer_us64();
}

void timer_delay_us(uint32_t us)
{
    uint64_t end = counter() + (uint64_t)us * freq() / 1000000u;
    while (counter() < end)
        ;
}

void timer_delay_ms(uint32_t ms)
{
    timer_delay_us(ms * 1000u);
}

/* --- tick --- */

static uint32_t hz;
static uint64_t period, next;
static volatile uint32_t ticks;
static volatile uint32_t late;
static void (*tick_hook)(uint32_t);

static void tick_irq(void *arg)
{
    (void)arg;
    uint64_t now = counter();
    do {
        next += period;
        ticks++;
    } while (now >= next && ++late);
    write_sysreg(cntp_cval_el0, next);
    if (tick_hook)
        tick_hook(ticks);
}

void tick_init(uint32_t rate_hz)
{
    hz = rate_hz;
    period = freq() / rate_hz;
    ticks = 0;
    next = counter() + period;
    irq_register(IRQ_TIMER_PPI, tick_irq, 0);
    write_sysreg(cntp_cval_el0, next);
    write_sysreg(cntp_ctl_el0, 1);          /* enabled, not masked */
    irq_enable(IRQ_TIMER_PPI);
}

uint32_t tick_hz(void)      { return hz; }
uint32_t tick_count(void)   { return ticks; }
uint32_t tick_late(void)    { return late; }

uint32_t tick_ms(void)
{
    return (uint32_t)((uint64_t)ticks * 1000u / hz);
}

void tick_set_hook(void (*hook)(uint32_t))
{
    tick_hook = hook;
}
