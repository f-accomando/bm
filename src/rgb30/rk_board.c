/*
 * RGB30 board bits: the status LEDs (green GPIO0_C5, red GPIO0_C6; in
 * Linux they are PWM6 / PWM7 LEDs, here plain GPIO outputs, high = on).
 */
#ifdef PLAT_RK3566
#include "plat.h"
#include "rk_gpio.h"

#define LED_GREEN   rk_pin(0, 'C', 5)
#define LED_RED     rk_pin(0, 'C', 6)

void plat_led(int green, int red)
{
    static int ready;
    if (!ready) {
        rk_gpio_output(LED_GREEN, 0);
        rk_gpio_output(LED_RED, 0);
        ready = 1;
    }
    if (green >= 0)
        rk_gpio_set(LED_GREEN, green);
    if (red >= 0)
        rk_gpio_set(LED_RED, red);
}
#endif
