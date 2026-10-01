/*
 * RK3566 GPIO (the "v2" controller, hiword-mask writes) and pin muxing.
 * Pins are named by bank and index: GPIO3_C2 = rk_pin(3, 'C', 2).
 */
#ifndef RK_GPIO_H
#define RK_GPIO_H

#include <stdint.h>

#define rk_pin(bank, group, n)  (((bank) << 5) | (((group) - 'A') << 3) | (n))

enum { RK_PULL_NONE = 0, RK_PULL_UP = 1, RK_PULL_DOWN = 2 };

/* iomux function (0 = GPIO) and pull */
void rk_pin_mux(unsigned pin, unsigned func);
void rk_pin_pull(unsigned pin, unsigned pull);

void rk_gpio_output(unsigned pin, int value);
void rk_gpio_input(unsigned pin);
void rk_gpio_set(unsigned pin, int value);
int  rk_gpio_get(unsigned pin);

#endif
