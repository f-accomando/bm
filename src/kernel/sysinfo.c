#include "sysinfo.h"
#include "drivers/prop.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "lib/printf.h"

extern char __kernel_end[];

void sysinfo_print(void)
{
    uint32_t v[2];

    v[0] = 0;
    if (prop_query(PROP_GET_BOARD_REVISION, v, 1) == 0)
        kprintf("board revision : %08lx\n", v[0]);

    v[0] = v[1] = 0;
    if (prop_query(PROP_GET_ARM_MEMORY, v, 2) == 0)
        kprintf("ARM memory     : %08lx + %lu MiB\n", v[0], v[1] >> 20);

    v[0] = v[1] = 0;
    if (prop_query(PROP_GET_VC_MEMORY, v, 2) == 0)
        kprintf("GPU memory     : %08lx + %lu MiB\n", v[0], v[1] >> 20);

    kprintf("ARM clock      : %lu MHz\n", prop_clock_rate(CLOCK_ARM) / 1000000);
    kprintf("core clock     : %lu MHz\n", prop_clock_rate(CLOCK_CORE) / 1000000);
    kprintf("UART clock     : %lu Hz, %lu baud\n", uart_clock(), (uint32_t)UART_BAUD);

    v[0] = 0; v[1] = 0;
    if (prop_query(PROP_GET_TEMPERATURE, v, 2) == 0 && v[1])
        kprintf("SoC temp       : %lu.%lu C\n", v[1] / 1000, (v[1] % 1000) / 100);

    kprintf("kernel end     : %p\n", (void *)__kernel_end);
    kprintf("uptime         : %lu ms\n", timer_ticks() / 1000);
}
