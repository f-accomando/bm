#include "tick.h"
#include "irq.h"
#include "drivers/mmio.h"

#define ST_CS   (PERIPHERAL_BASE + 0x3000)
#define ST_CLO  (PERIPHERAL_BASE + 0x3004)
#define ST_C1   (PERIPHERAL_BASE + 0x3010)
#define ST_M1   (1u << 1)

static uint32_t period_us;
static uint32_t hz;
static uint32_t next;
static volatile uint32_t ticks;
static volatile uint32_t late;
static void (*tick_hook)(uint32_t);

static void tick_irq(void *arg)
{
    (void)arg;
    mmio_write(ST_CS, ST_M1);           /* acknowledge */

    /* Advance by whole periods, so the average rate stays exact even if
     * this handler runs late; count skipped periods. */
    uint32_t now = mmio_read(ST_CLO);
    do {
        next += period_us;
        ticks++;
    } while ((int32_t)(now - next) >= 0 && ++late);
    mmio_write(ST_C1, next);
    /* If the counter got past `next` while it was being written, the
     * compare would match again only after the 32-bit wrap (71 minutes):
     * catch up until the compare value is really in the future. */
    while ((int32_t)(mmio_read(ST_CLO) - next) >= 0) {
        next += period_us;
        ticks++;
        late++;
        mmio_write(ST_C1, next);
    }

    if (tick_hook)
        tick_hook(ticks);
}

void tick_init(uint32_t rate_hz)
{
    hz = rate_hz;
    period_us = 1000000u / rate_hz;
    ticks = 0;
    irq_register(IRQ_TIMER1, tick_irq, 0);
    next = mmio_read(ST_CLO) + period_us;
    mmio_write(ST_C1, next);
    mmio_write(ST_CS, ST_M1);
    irq_enable(IRQ_TIMER1);
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
