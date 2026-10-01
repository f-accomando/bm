/*
 * RK3566 GPIO and iomux (register layout from Linux's gpio-rockchip.c and
 * pinctrl-rockchip.c, RK3568 tables):
 *   GPIO0 0xfdd60000, GPIO1-4 0xfe740000 + (bank - 1) * 0x10000
 *   SWPORT_DR_L/H 0x00/0x04, SWPORT_DDR_L/H 0x08/0x0c, EXT_PORT 0x70
 *   iomux: 4 bits per pin, 8 bytes per group of 8 pins (two words);
 *          GPIO0 in PMU_GRF from 0x00, GPIO1-4 in GRF from (bank - 1) * 0x20
 *   pull:  2 bits per pin (1 up, 2 down), 4 bytes per group;
 *          GPIO0 in PMU_GRF from 0x20, GPIO1-4 in GRF from 0x80 + (bank - 1) * 0x10
 */
#ifdef PLAT_RK3566
#include "rk_gpio.h"
#include "io.h"

#define PMU_GRF     0xfdc20000u
#define GRF         0xfdc60000u

static uintptr_t gpio_base(unsigned bank)
{
    return bank == 0 ? 0xfdd60000u : 0xfe740000u + (bank - 1) * 0x10000u;
}

void rk_pin_mux(unsigned pin, unsigned func)
{
    unsigned bank = pin >> 5, idx = pin & 31, group = idx >> 3, n = idx & 7;
    uintptr_t reg = bank == 0 ? PMU_GRF + group * 8 : GRF + (bank - 1) * 0x20 + group * 8;
    reg += (n >> 2) * 4;
    unsigned shift = (n & 3) * 4;
    writel(reg, HIWORD(0xfu << shift, func << shift));
}

void rk_pin_pull(unsigned pin, unsigned pull)
{
    unsigned bank = pin >> 5, idx = pin & 31, group = idx >> 3, n = idx & 7;
    uintptr_t reg = bank == 0 ? PMU_GRF + 0x20 + group * 4 : GRF + 0x80 + (bank - 1) * 0x10 + group * 4;
    unsigned shift = n * 2;
    writel(reg, HIWORD(3u << shift, pull << shift));
}

static void hw_write(uintptr_t base, unsigned off_lo, unsigned idx, int v)
{
    uintptr_t reg = base + off_lo + (idx >= 16 ? 4 : 0);
    unsigned bit = idx & 15;
    writel(reg, HIWORD(1u << bit, (v ? 1u : 0u) << bit));
}

void rk_gpio_output(unsigned pin, int value)
{
    rk_pin_mux(pin, 0);
    rk_gpio_set(pin, value);
    hw_write(gpio_base(pin >> 5), 0x08, pin & 31, 1);
}

void rk_gpio_input(unsigned pin)
{
    rk_pin_mux(pin, 0);
    hw_write(gpio_base(pin >> 5), 0x08, pin & 31, 0);
}

void rk_gpio_set(unsigned pin, int value)
{
    hw_write(gpio_base(pin >> 5), 0x00, pin & 31, value);
}

int rk_gpio_get(unsigned pin)
{
    return (readl(gpio_base(pin >> 5) + 0x70) >> (pin & 31)) & 1;
}
#endif
