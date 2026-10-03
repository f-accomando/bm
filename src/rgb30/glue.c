/*
 * The Pi's driver APIs on the AArch64 build, on top of plat.h: serial
 * console (drivers/uart.h), LED (drivers/led.h), watchdog and reboot
 * (drivers/watchdog.h).
 */
#include "drivers/uart.h"
#include "drivers/led.h"
#include "drivers/watchdog.h"
#include "drivers/timer.h"
#include "plat.h"

void uart_init(void)            { plat_uart_init(); }
void uart_use_mini(void)        { }
int  uart_is_mini(void)         { return 0; }
uint32_t uart_clock(void)       { return 0; }
void uart_putc(char c)          { plat_uart_putc(c); }

void uart_puts(const char *s)
{
    while (*s)
        plat_uart_putc(*s++);
}

void uart_write(const void *buf, uint32_t len)
{
    const char *p = buf;
    while (len--)
        plat_uart_putc(*p++);
}

static int pending = -1;

int uart_rx_ready(void)
{
    if (pending < 0)
        pending = plat_uart_getc();
    return pending >= 0;
}

char uart_getc(void)
{
    while (!uart_rx_ready())
        ;
    char c = (char)pending;
    pending = -1;
    return c;
}

int uart_getc_timeout(uint32_t timeout_us, char *c)
{
    uint32_t t0 = timer_ticks();
    while (!uart_rx_ready())
        if (timer_ticks() - t0 >= timeout_us)
            return 0;
    *c = uart_getc();
    return 1;
}

void uart_flush(void)
{
}

/* --- LED: the green status LED --- */

void led_init(void)             { }
void led_init_board(void)       { }
void led_set(int on)            { plat_led(on, -1); }

void led_blink_forever(unsigned on_ms, unsigned off_ms)
{
    for (;;) {
        plat_led(-1, 1);
        timer_delay_ms(on_ms);
        plat_led(-1, 0);
        timer_delay_ms(off_ms);
    }
}

/* the red LED: n short blinks, a pause, forever */
void led_blink_code(unsigned n)
{
    plat_led(0, 0);
    for (;;) {
        for (unsigned i = 0; i < n; i++) {
            plat_led(-1, 1);
            timer_delay_ms(200);
            plat_led(-1, 0);
            timer_delay_ms(300);
        }
        timer_delay_ms(1500);
    }
}

/* --- watchdog: none yet (the RK3566 has one: later) --- */

void watchdog_reboot(void)      { plat_reset(); }
int  watchdog_arm(uint32_t ms)  { (void)ms; return -1; }
void watchdog_pet(void)         { }
void watchdog_stop(void)        { }

void watchdog_probe_values(uint32_t *loaded, uint32_t *left)
{
    *loaded = *left = 0;
}
