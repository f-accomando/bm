#include "watchdog.h"
#include "mmio.h"

#define PM_BASE         (PERIPHERAL_BASE + 0x100000)
#define PM_RSTC         (PM_BASE + 0x1C)
#define PM_WDOG         (PM_BASE + 0x24)
#define PM_PASSWORD     0x5A000000u
#define PM_RSTC_WRCFG_CLR       0xFFFFFFCFu
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u

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
