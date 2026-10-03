/*
 * The RTL8821CS module's power (rk3566-powkiddy-rk2023.dtsi): its 3.3 V
 * switch (vcc_wifi, GPIO0_A0), the 32 kHz sleep clock from the RK817
 * (CLKOUT2), and the WiFi reset WL_REG_ON (GPIO4_A2, low = reset, then
 * 200 ms after release, as Linux's mmc-pwrseq-simple). The pins of the
 * SDIO bus run at 1.8 V (vccio4): the PMU GRF is told so, as Linux's
 * io-domain driver does.
 */
#ifdef PLAT_RK3566
#include "rk_wlbt.h"
#include "rk_gpio.h"
#include "rk_pmic.h"
#include "io.h"
#include "drivers/timer.h"

#define VCC_WIFI    rk_pin(0, 'A', 0)
#define WL_REG_ON   rk_pin(4, 'A', 2)
#define PMUGRF      0xfdc20000u

static int powered;

void wlbt_power_on(void)
{
    if (powered)
        return;
    writel(PMUGRF + 0x140, 0x00100010u);        /* IO_VSEL0: vccio4 1.8 V */
    writel(PMUGRF + 0x144, 0x00100000u);        /* IO_VSEL1: not 3.3 V */
    rk_gpio_output(VCC_WIFI, 1);
    rk817_clk32k_wifi(1);
    rk_gpio_output(WL_REG_ON, 1);
    timer_delay_ms(200);
    powered = 1;
}

void wlbt_wifi_reset(void)
{
    wlbt_power_on();
    rk_gpio_set(WL_REG_ON, 0);
    timer_delay_ms(20);
    rk_gpio_set(WL_REG_ON, 1);
    timer_delay_ms(200);
}
#endif
