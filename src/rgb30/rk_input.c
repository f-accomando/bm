/*
 * The RGB30's controls (rk3566-powkiddy-rk2023.dtsi): every button is a
 * GPIO3 pin pulled up, low when pressed; the two sticks share SARADC
 * channel 3 through an analog mux (select GPIO0_B6/B7, active low, enable
 * GPIO0_B5 low), 100 us to settle, 10-bit samples.
 */
#ifdef PLAT_RK3566
#include "plat.h"
#include "pad.h"
#include "rk_gpio.h"
#include "io.h"
#include "drivers/timer.h"

static const struct { uint8_t group, n; uint32_t button; } keys[] = {
    { 'C', 2, PAD_A },      /* EAST */
    { 'C', 3, PAD_B },      /* SOUTH */
    { 'C', 0, PAD_X },      /* NORTH */
    { 'C', 1, PAD_Y },      /* WEST */
    { 'A', 3, PAD_UP },
    { 'A', 4, PAD_DOWN },
    { 'A', 6, PAD_LEFT },
    { 'A', 5, PAD_RIGHT },
    { 'B', 1, PAD_L1 },     /* TL */
    { 'B', 2, PAD_L2 },     /* TL2 */
    { 'B', 3, PAD_R1 },     /* TR */
    { 'B', 4, PAD_R2 },     /* TR2 */
    { 'B', 6, PAD_SELECT },
    { 'B', 5, PAD_START },
    { 'A', 1, PAD_L3 },     /* THUMBL */
    { 'A', 2, PAD_R3 },     /* THUMBR */
    { 'A', 7, PAD_VOLUP },
    { 'B', 0, PAD_VOLDN },
};
#define NKEYS (sizeof keys / sizeof keys[0])

static int ready;

static void input_init(void)
{
    for (unsigned i = 0; i < NKEYS; i++) {
        unsigned pin = rk_pin(3, keys[i].group, keys[i].n);
        rk_pin_pull(pin, RK_PULL_UP);
        rk_gpio_input(pin);
    }
    writel(0xfdd20368u, 0x01800000u);           /* pclk_saradc, clk_saradc on (CLKGATE_CON26) */
    rk_gpio_output(rk_pin(0, 'B', 5), 0);       /* mux enabled */
    rk_gpio_output(rk_pin(0, 'B', 6), 1);       /* channel 0 (active low) */
    rk_gpio_output(rk_pin(0, 'B', 7), 1);
    ready = 1;
}

uint32_t plat_buttons(void)
{
    if (!ready)
        input_init();
    uint32_t port3 = readl(0xfe760000u + 0x70);
    uint32_t held = 0;
    for (unsigned i = 0; i < NKEYS; i++) {
        unsigned bit = (keys[i].group - 'A') * 8 + keys[i].n;
        if (!((port3 >> bit) & 1))
            held |= keys[i].button;
    }
    return held;
}

/* --- sticks --- */

#define SARADC          0xfe720000u
#define SARADC_DATA     (SARADC + 0x00)
#define SARADC_CTRL     (SARADC + 0x08)
#define SARADC_DLY      (SARADC + 0x0c)
#define CTRL_IRQ_STATUS (1u << 6)
#define CTRL_IRQ_ENABLE (1u << 5)
#define CTRL_POWER      (1u << 3)

static int adc_read(unsigned chn)
{
    writel(SARADC_DLY, 8);
    writel(SARADC_CTRL, CTRL_POWER | CTRL_IRQ_ENABLE | (chn & 7));
    uint32_t t0 = timer_ticks();
    while (!(readl(SARADC_CTRL) & CTRL_IRQ_STATUS))
        if (timer_ticks() - t0 > 2000) {
            writel(SARADC_CTRL, 0);
            return -1;
        }
    int v = (int)(readl(SARADC_DATA) & 0x3ff);
    writel(SARADC_CTRL, 0);
    return v;
}

/* mux channel: 0 left x, 1 right x, 2 left y, 3 right y. Raw values:
 * left x 1023 at the left, right x 15; left y 1023 at the top, right y 15.
 * The y axes are the other way round from the dts (tried on the console,
 * 2026-10-03); reversed ones are turned so that left and up are negative. */
static int16_t axis(unsigned mux, int inverted)
{
    rk_gpio_set(rk_pin(0, 'B', 6), !(mux & 1));
    rk_gpio_set(rk_pin(0, 'B', 7), !(mux & 2));
    timer_delay_us(100);
    int v = adc_read(3);
    if (v < 0)
        return 0;
    int c = (v - 519) * 64;                 /* centre of 15..1023 */
    if (inverted)
        c = -c;
    if (c > 32767) c = 32767;
    if (c < -32768) c = -32768;
    return (int16_t)c;
}

void plat_sticks(int16_t axes[4])
{
    if (!ready)
        input_init();
    axes[0] = axis(0, 1);
    axes[1] = axis(2, 1);
    axes[2] = axis(1, 0);
    axes[3] = axis(3, 0);
}
#endif
