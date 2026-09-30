/* ACT LED: GPIO 47 active low on the Pi Zero / Zero W, GPIO 16 active low
 * on the first Pi 1 A/B, GPIO 47 active high on the Pi 1 A+/B+ (board.c). */
#include "led.h"
#include "board.h"
#include "gpio.h"
#include "timer.h"

static uint8_t led_pin, led_high;

void led_init(void)
{
    const board_t *b = board();
    led_pin = b->led_pin;
    led_high = b->led_active_high;
    if (led_pin)
        gpio_set_function(led_pin, GPIO_OUTPUT);
    led_set(0);
}

void led_set(int on)
{
    if (led_pin)
        gpio_write(led_pin, led_high ? on : !on);
}

void led_blink_forever(unsigned on_ms, unsigned off_ms)
{
    for (;;) {
        led_set(1);
        timer_delay_ms(on_ms);
        led_set(0);
        timer_delay_ms(off_ms);
    }
}

void led_blink_code(unsigned n)
{
    for (;;) {
        for (unsigned i = 0; i < n; i++) {
            led_set(1);
            timer_delay_ms(200);
            led_set(0);
            timer_delay_ms(300);
        }
        timer_delay_ms(1500);
    }
}
