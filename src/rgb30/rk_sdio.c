/*
 * SDIO on SDMMC2 (0xfe000000, pins M0 GPIO3_C6..D3, 4-bit, 1.8 V I/O): the
 * WiFi side of the RTL8821CS. Card set-up (CMD5, CMD3, CMD7, 4-bit bus,
 * function 1 on with 512-byte blocks), then CMD52 and CMD53 on function 1
 * for rk_wifi.c. The module's power and reset are in rk_wlbt.c.
 */
#ifdef PLAT_RK3566
#include "rk_sdio.h"
#include "rk_mmc.h"
#include "rk_gpio.h"
#include "io.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#define SDMMC2      0xfe000000u
#define CRU         0xfdd20000u
#define GRF         0xfdc60000u
#define PMUGRF      0xfdc20000u

static dwmmc_t host = { .base = SDMMC2 };
static uint32_t rca;
static char err[80] = "not started";

const char *sdio_error(void)
{
    return err[0] ? err : host.err;
}

static int failf(const char *what, int r)
{
    ksnprintf(err, sizeof err, "%s: %s", what, r == -2 ? "no answer" : host.err);
    return -1;
}

/* CMD52: one byte of function fn at addr; returns the byte read (or
 * written back), -1 on error */
int sdio_cmd52(int write, unsigned fn, uint32_t addr, uint8_t data)
{
    uint32_t arg = (write ? 1u << 31 : 0) | (fn & 7) << 28 | (write ? 1u << 27 : 0) |
                   (addr & 0x1ffff) << 9 | data, r;
    if (dwmmc_cmd(&host, 52, arg, MMC_R1, &r) != 0)
        return -1;
    if (r & 0xcb00)                         /* R5 flags: CRC, illegal, error, function, range */
        return -1;
    return (int)(r & 0xff);
}

int sdio_read8(uint32_t addr)               { return sdio_cmd52(0, 1, addr, 0); }
int sdio_write8(uint32_t addr, uint8_t v)   { return sdio_cmd52(1, 1, addr, v) < 0 ? -1 : 0; }

/* CMD53 on function 1, incrementing address: len bytes (multiple of 4; up
 * to 512 in byte mode, else whole 512-byte blocks) */
int sdio_cmd53(int write, uint32_t addr, void *buf, uint32_t len)
{
    int block = len > 512;
    uint32_t count = block ? (len + 511) / 512 : len;
    uint32_t bytes = block ? count * 512 : len;
    uint32_t arg = (write ? 1u << 31 : 0) | 1u << 28 | (block ? 1u << 27 : 0) | 1u << 26 |
                   (addr & 0x1ffff) << 9 | (count & 0x1ff);
    dwmmc_data_setup(&host, block ? 512 : bytes, bytes);
    uint32_t r;
    if (dwmmc_cmd(&host, 53, arg, MMC_R1 | MMC_DATA | (write ? MMC_WRITE : 0), &r) != 0)
        return -1;
    if (r & 0xcb00)
        return -1;
    return dwmmc_data(&host, write, buf, bytes);
}

static void pins_and_clock(void)
{
    /* clocks: 24 MHz source (CLKSEL_CON32[10:8] = 0), gates open, reset pulse */
    writel(CRU + 0x180, 0x07000000u);
    writel(CRU + 0x344, 0x00030000u);
    writel(CRU + 0x438, 0x08000800u);
    timer_delay_us(10);
    writel(CRU + 0x438, 0x08000000u);
    /* pins M0: GPIO3_C6, C7 (D0, D1), D0..D3 (D2, D3, CMD, CLK), function 3, pull-up */
    rk_pin_mux(rk_pin(3, 'C', 6), 3);
    rk_pin_mux(rk_pin(3, 'C', 7), 3);
    for (unsigned n = 0; n < 4; n++)
        rk_pin_mux(rk_pin(3, 'D', n), 3);
    writel(GRF + 0x308, 0x40000000u);           /* route: SDMMC2 on M0 */
    for (unsigned n = 0; n < 4; n++)
        rk_pin_pull(rk_pin(3, 'D', n), RK_PULL_UP);
    rk_pin_pull(rk_pin(3, 'C', 6), RK_PULL_UP);
    rk_pin_pull(rk_pin(3, 'C', 7), RK_PULL_UP);
}

int sdio_init(void)
{
    uint32_t r;
    err[0] = 0;
    rca = 0;
    pins_and_clock();
    if (dwmmc_reset(&host))
        return failf("reset", -1);
    if (dwmmc_clock(&host, 15))                 /* 400 kHz */
        return failf("clock", -1);
    timer_delay_ms(2);

    dwmmc_cmd(&host, 0, 0, MMC_INIT, NULL);     /* harmless for SDIO-only cards */
    uint32_t ocr = 0;
    int e = dwmmc_cmd(&host, 5, 0, MMC_R3, &r);
    if (e)
        return failf("CMD5 (no SDIO card)", e);
    ocr = r & 0x00ff8000u;
    uint32_t t0 = timer_ticks();
    do {
        e = dwmmc_cmd(&host, 5, ocr ? ocr : 0x00ff8000u, MMC_R3, &r);
        if (e)
            return failf("CMD5", e);
        if (!(r & 0x80000000u))
            timer_delay_ms(10);
        if (timer_ticks() - t0 > 1000000) {
            ksnprintf(err, sizeof err, "CMD5: card stays busy (%08lx)", r);
            return -1;
        }
    } while (!(r & 0x80000000u));
    unsigned functions = (r >> 28) & 7;
    if ((e = dwmmc_cmd(&host, 3, 0, MMC_R1, &r)))
        return failf("CMD3", e);
    rca = r >> 16;
    if ((e = dwmmc_cmd(&host, 7, rca << 16, MMC_R1, &r)))
        return failf("CMD7", e);
    /* 4-bit bus (CCCR 0x07 bits 1:0 = 2) */
    int bic = sdio_cmd52(0, 0, 0x07, 0);
    if (bic < 0 || sdio_cmd52(1, 0, 0x07, (uint8_t)((bic & ~3) | 2)) < 0)
        return failf("bus width", -1);
    dwmmc_bus_width(&host, 1);
    /* function 1 on (CCCR 0x02 IOE1), wait for IOR1 (0x03) */
    if (sdio_cmd52(1, 0, 0x02, 0x02) < 0)
        return failf("enable function 1", -1);
    t0 = timer_ticks();
    while ((sdio_cmd52(0, 0, 0x03, 0) & 2) == 0)
        if (timer_ticks() - t0 > 500000) {
            ksnprintf(err, sizeof err, "function 1 not ready");
            return -1;
        }
    /* function 1 block size 512 (FBR1 0x110, 0x111) */
    if (sdio_cmd52(1, 0, 0x110, 0x00) < 0 || sdio_cmd52(1, 0, 0x111, 0x02) < 0)
        return failf("block size", -1);
    if (dwmmc_clock(&host, 0))                  /* 12 MHz */
        return failf("clock", -1);
    err[0] = 0;
    (void)functions;
    return 0;
}

/* vendor and device id from function 1's CIS would need the tuple walk:
 * the CCCR's common CIS pointer is enough to check the card answers */
int sdio_ready(void)
{
    return rca != 0;
}
#endif
