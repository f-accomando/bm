/* ACT LED on Pi Zero / Zero W: GPIO 47, active low. */
#include "led.h"
#include "gpio.h"
#include "timer.h"

#define LED_PIN 47

void led_init(void)
{
    gpio_set_function(LED_PIN, GPIO_OUTPUT);
    led_set(0);
}

void led_set(int on)
{
    gpio_write(LED_PIN, !on);
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
