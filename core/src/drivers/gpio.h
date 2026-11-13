#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

enum gpio_func {
    GPIO_INPUT = 0, GPIO_OUTPUT = 1,
    GPIO_ALT0 = 4, GPIO_ALT1 = 5, GPIO_ALT2 = 6,
    GPIO_ALT3 = 7, GPIO_ALT4 = 3, GPIO_ALT5 = 2,
};

enum gpio_pull { GPIO_PULL_NONE = 0, GPIO_PULL_DOWN = 1, GPIO_PULL_UP = 2 };

void gpio_set_function(unsigned pin, enum gpio_func fn);
void gpio_set_pull(unsigned pin, enum gpio_pull pull);
void gpio_write(unsigned pin, int high);
int  gpio_read(unsigned pin);

#endif
