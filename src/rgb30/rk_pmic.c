/*
 * RK817 PMIC on I2C0 (0xfdd40000, address 0x20): power off, battery,
 * charger, power key, the 32 kHz clock of the WiFi chip. Polled I2C
 * (Rockchip i2c-rk3x controller), 100 kHz.
 */
#ifdef PLAT_RK3566
#include "rk_pmic.h"
#include "io.h"
#include "drivers/timer.h"

#define I2C0        0xfdd40000u
#define PMUCRU      0xfdd00000u
#define I2C_CON     0x00
#define I2C_CLKDIV  0x04
#define I2C_MRXADDR 0x08
#define I2C_MRXRADDR 0x0c
#define I2C_MTXCNT  0x10
#define I2C_MRXCNT  0x14
#define I2C_IEN     0x18
#define I2C_IPD     0x1c
#define I2C_TXDATA0 0x100
#define I2C_RXDATA0 0x200

#define CON_EN      (1u << 0)
#define CON_TRX     (1u << 1)
#define CON_START   (1u << 3)
#define CON_STOP    (1u << 4)
#define CON_LASTACK (1u << 5)
#define IPD_MBTF    (1u << 2)
#define IPD_MBRF    (1u << 3)
#define IPD_START   (1u << 4)
#define IPD_STOP    (1u << 5)
#define IPD_NAK     (1u << 6)

#define RK817       0x20

static int ready;

static inline void w(uint32_t off, uint32_t v) { writel(I2C0 + off, v); }
static inline uint32_t r(uint32_t off)         { return readl(I2C0 + off); }

static int wait_ipd(uint32_t bit)
{
    uint32_t t0 = timer_ticks();
    for (;;) {
        uint32_t ipd = r(I2C_IPD);
        if (ipd & IPD_NAK)
            return -1;
        if (ipd & bit)
            return 0;
        if (timer_ticks() - t0 > 20000)
            return -2;
    }
}

static void i2c_init(void)
{
    /* clocks on; i2c clock = PPLL (200 MHz) or GPLL / (div + 1) */
    writel(PMUCRU + 0x184, 0x00030000u);
    uint32_t sel2 = readl(PMUCRU + 0x108), sel3 = readl(PMUCRU + 0x10c);
    uint32_t src = (sel2 & (1u << 15)) ? 1188000000u : 200000000u;
    uint32_t clk = src / ((sel3 & 0x7f) + 1);
    uint32_t div = (clk + 8 * 100000 - 1) / (8 * 100000);
    div = div > 2 ? div - 2 : 0;
    w(I2C_CLKDIV, ((div - div / 2) << 16) | (div / 2));
    ready = 1;
}

static int start(uint32_t con)
{
    w(I2C_IPD, 0x7f);
    w(I2C_CON, con | CON_EN | CON_START);
    w(I2C_IEN, IPD_START);
    int e = wait_ipd(IPD_START);
    w(I2C_IPD, IPD_START);
    w(I2C_CON, con | CON_EN);
    return e;
}

static void stop(void)
{
    w(I2C_IPD, 0x7f);
    w(I2C_CON, CON_EN | CON_STOP);
    w(I2C_IEN, IPD_STOP);
    wait_ipd(IPD_STOP);
    w(I2C_IPD, IPD_STOP);
    w(I2C_CON, 0);
}

int rk817_write(uint8_t reg, uint8_t val)
{
    if (!ready)
        i2c_init();
    int e = start(0);
    if (!e) {
        w(I2C_TXDATA0, ((uint32_t)val << 16) | ((uint32_t)reg << 8) | (RK817 << 1));
        w(I2C_CON, CON_EN);
        w(I2C_IEN, IPD_MBTF | IPD_NAK);
        w(I2C_MTXCNT, 3);
        e = wait_ipd(IPD_MBTF);
    }
    stop();
    return e;
}

int rk817_read(uint8_t reg)
{
    if (!ready)
        i2c_init();
    int e = start(CON_TRX);
    int v = -1;
    if (!e) {
        w(I2C_MRXADDR, (RK817 << 1) | (1u << 24));
        w(I2C_MRXRADDR, reg | (1u << 24));
        w(I2C_CON, CON_EN | CON_TRX | CON_LASTACK);
        w(I2C_IEN, IPD_MBRF | IPD_NAK);
        w(I2C_MRXCNT, 1);
        if (wait_ipd(IPD_MBRF) == 0)
            v = (int)(r(I2C_RXDATA0) & 0xff);
    }
    stop();
    return v;
}

int rk817_present(void)
{
    int msb = rk817_read(0xed), lsb = rk817_read(0xee);
    return msb >= 0 && lsb >= 0 && (((msb << 8) | lsb) & 0xfff0) == 0x8170;
}

void rk817_power_off(void)
{
    int v = rk817_read(0xf4);                   /* SYS_CFG3: bit 0 DEV_OFF */
    rk817_write(0xf4, (uint8_t)((v < 0 ? 0 : v) | 1));
}

void rk817_clk32k_wifi(int on)
{
    int v = rk817_read(0xf2);                   /* SYS_CFG1: bit 7 CLK32KOUT2_EN */
    if (v >= 0)
        rk817_write(0xf2, (uint8_t)(on ? (v | 0x80) : (v & ~0x80)));
}

static int be16(uint8_t hi)
{
    int a = rk817_read(hi), b = rk817_read((uint8_t)(hi + 1));
    return a < 0 || b < 0 ? -1 : (a << 8) | b;
}

int rk817_battery_mv(void)
{
    static int k, b, calibrated;
    if (!calibrated) {
        rk817_write(0x50, 0xfc);                /* gauge ADCs on */
        int c0 = be16(0x93), c1 = be16(0x95);
        if (c0 < 0 || c1 <= c0)
            return -1;
        k = (4025 - 2300) * 1000 / (c1 - c0);
        b = 4025 - k * c1 / 1000;
        calibrated = 1;
    }
    int raw = be16(0x78);
    return raw < 0 ? -1 : k * raw / 1000 + b;
}

int rk817_charge_state(void)
{
    int v = rk817_read(0xeb);
    return v < 0 ? -1 : (v >> 4) & 7;
}

int rk817_plugged(void)
{
    int v = rk817_read(0xf0);
    return v < 0 ? -1 : (v >> 6) & 1;
}

int rk817_power_key(void)
{
    int v = rk817_read(0xf8);                   /* INT_STS0: bit 0 press, bit 1 release */
    if (v <= 0)
        return 0;
    rk817_write(0xf8, (uint8_t)(v & 3));
    return v & 1;
}
#endif
