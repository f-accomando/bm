#include "input.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "gfx/console.h"
#include "lib/printf.h"

static void update_uptime(void)
{
    char buf[24];
    uint32_t s = timer_ticks() / 1000000;
    ksnprintf(buf, sizeof buf, "up %02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
    console_set_status(0, buf);
}

char input_getc(void)
{
    uint32_t last = timer_ticks();
    char c;

    update_uptime();
    while (!uart_getc_timeout(10000, &c)) {
        if (timer_ticks() - last >= 1000000) {
            last += 1000000;
            update_uptime();
        }
    }
    return c;
}
