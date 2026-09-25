#include "gpio.h"
#include "mmio.h"
#include "timer.h"

#define GPIO_BASE   (PERIPHERAL_BASE + 0x200000)
#define GPFSEL0     (GPIO_BASE + 0x00)
#define GPSET0      (GPIO_BASE + 0x1C)
#define GPCLR0      (GPIO_BASE + 0x28)
#define GPLEV0      (GPIO_BASE + 0x34)
#define GPPUD       (GPIO_BASE + 0x94)
#define GPPUDCLK0   (GPIO_BASE + 0x98)

void gpio_set_function(unsigned pin, enum gpio_func fn)
{
    uint32_t reg = GPFSEL0 + (pin / 10) * 4;
    unsigned shift = (pin % 10) * 3;
    uint32_t v = mmio_read(reg);
    v &= ~(7u << shift);
    v |= (uint32_t)fn << shift;
    mmio_write(reg, v);
}

/* BCM2835 pull-up/down sequence (datasheet p.101): set GPPUD, wait,
 * clock it into the pin, wait, then remove both. */
void gpio_set_pull(unsigned pin, enum gpio_pull pull)
{
    uint32_t clk = GPPUDCLK0 + (pin / 32) * 4;

    mmio_write(GPPUD, pull);
    timer_delay_us(5);
    mmio_write(clk, 1u << (pin % 32));
    timer_delay_us(5);
    mmio_write(GPPUD, 0);
    mmio_write(clk, 0);
}

void gpio_write(unsigned pin, int high)
{
    mmio_write((high ? GPSET0 : GPCLR0) + (pin / 32) * 4, 1u << (pin % 32));
}

int gpio_read(unsigned pin)
{
    return (mmio_read(GPLEV0 + (pin / 32) * 4) >> (pin % 32)) & 1;
}
