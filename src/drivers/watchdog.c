#include "watchdog.h"
#include "mmio.h"
#include "timer.h"

#define PM_BASE         (PERIPHERAL_BASE + 0x100000)
#define PM_RSTC         (PM_BASE + 0x1C)
#define PM_WDOG         (PM_BASE + 0x24)
#define PM_PASSWORD     0x5A000000u
#define PM_RSTC_WRCFG_CLR       0xFFFFFFCFu
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u

static uint32_t wd_ticks;               /* 0 = off */

static void wd_write(uint32_t ticks)
{
    dmb();
    mmio_write(PM_WDOG, PM_PASSWORD | (ticks & 0xFFFFF));
    mmio_write(PM_RSTC, PM_PASSWORD
               | (mmio_read(PM_RSTC) & PM_RSTC_WRCFG_CLR)
               | PM_RSTC_WRCFG_FULL_RESET);
    dmb();
}

int watchdog_arm(uint32_t ms)
{
    if (ms > 15000)
        ms = 15000;
    uint32_t t = ms * 65536u / 1000u;           /* the timer counts 1/65536 s */
    /* The real timer counts down as soon as it is loaded; an emulator that
     * resets at once on RSTC (QEMU) does not, and is left alone. */
    dmb();
    mmio_write(PM_WDOG, PM_PASSWORD | t);
    timer_delay_ms(20);
    uint32_t left = mmio_read(PM_WDOG) & 0xFFFFF;
    dmb();
    if (left == 0 || left >= t)
        return -1;
    wd_ticks = t;
    wd_write(wd_ticks);
    return 0;
}

void watchdog_pet(void)
{
    if (wd_ticks)
        wd_write(wd_ticks);
}

void watchdog_stop(void)
{
    wd_ticks = 0;
    dmb();
    mmio_write(PM_RSTC, PM_PASSWORD | 0x102);   /* RESET: watchdog off */
    dmb();
}

void watchdog_reboot(void)
{
    dmb();
    mmio_write(PM_WDOG, PM_PASSWORD | 10);      /* ~150 us */
    mmio_write(PM_RSTC, PM_PASSWORD
               | (mmio_read(PM_RSTC) & PM_RSTC_WRCFG_CLR)
               | PM_RSTC_WRCFG_FULL_RESET);
    for (;;)
        __asm__ volatile("wfi");
}
