/*
 * drivers/sd.h on the RGB30: the boot slot, SDMMC0 (DesignWare MSHC at
 * 0xfe2b0000, rk_mmc.c), polled PIO, 4-bit bus. U-Boot used the controller
 * with its DMA (IDMAC): everything is reset first. 400 kHz for the
 * identification, then 12 MHz (what U-Boot runs a default-speed card at,
 * with the clock phases it leaves).
 */
#ifdef PLAT_RK3566
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "rk_mmc.h"
#include "rk_gpio.h"
#include "io.h"
#include "lib/printf.h"

#define CRU         0xfdd20000u

static dwmmc_t host = { .base = 0xfe2b0000u };
static uint32_t rca, blocks;
static int hc;
static char err[80] = "not initialised";

static int failf(const char *what, int r)
{
    ksnprintf(err, sizeof err, "%s: %s", what, r == -2 ? "no answer" : host.err);
    return -1;
}

static int app_cmd(uint32_t idx, uint32_t arg, uint32_t flags, uint32_t *resp)
{
    int r = dwmmc_cmd(&host, 55, rca << 16, MMC_R1, 0);
    return r ? r : dwmmc_cmd(&host, idx, arg, flags, resp);
}

int sd_init(void)
{
    uint32_t r[4];
    int e;
    rca = blocks = 0;
    hc = 0;
    /* clocks on, source 24 MHz (CLKSEL_CON30[10:8] = 0) */
    writel(CRU + 0x33c, 0x00030000u);
    writel(CRU + 0x178, 0x07000000u);
    /* card detect: GPIO0_A4 as sdmmc0_det (U-Boot leaves it so; made
     * sure). If it still says empty, the card is asked anyway. */
    rk_pin_mux(rk_pin(0, 'A', 4), 1);
    int present = dwmmc_card_present(&host);
    if (dwmmc_reset(&host))
        return failf("reset", -1);
    if (dwmmc_clock(&host, 15))                 /* 400 kHz */
        return failf("clock", -1);
    timer_delay_ms(2);

    dwmmc_cmd(&host, 0, 0, MMC_INIT, 0);
    int v2 = dwmmc_cmd(&host, 8, 0x1aa, MMC_R1, r) == 0 && (r[0] & 0xfff) == 0x1aa;
    uint32_t t0 = timer_ticks();
    do {
        e = app_cmd(41, (v2 ? 0x40000000u : 0) | 0x00ff8000u, MMC_R3, r);
        if (e && !present && !v2) {
            ksnprintf(err, sizeof err, "no card in the slot");
            return -1;
        }
        if (e)
            return failf("ACMD41", e);
        if (timer_ticks() - t0 > 1500000) {
            ksnprintf(err, sizeof err, "card stays busy (%08lx)", r[0]);
            return -1;
        }
        if (!(r[0] & 0x80000000u))
            timer_delay_ms(10);
    } while (!(r[0] & 0x80000000u));
    hc = (r[0] >> 30) & 1;
    if ((e = dwmmc_cmd(&host, 2, 0, MMC_R2, r)))
        return failf("CMD2", e);
    if ((e = dwmmc_cmd(&host, 3, 0, MMC_R1, r)))
        return failf("CMD3", e);
    rca = r[0] >> 16;
    if ((e = dwmmc_cmd(&host, 9, rca << 16, MMC_R2, r)))
        return failf("CMD9", e);
    /* r[0] = CSD bits 127..96 ... (U-Boot's layout) */
    if ((r[0] >> 30) == 1) {                    /* CSD 2.0 */
        uint32_t c_size = ((r[1] & 0x3f) << 16) | (r[2] >> 16);
        blocks = (c_size + 1) * 1024;
    } else {
        uint32_t read_bl_len = (r[1] >> 16) & 0xf;
        uint32_t c_size = ((r[1] & 0x3ff) << 2) | (r[2] >> 30);
        uint32_t mult = (r[2] >> 15) & 7;
        blocks = (uint32_t)((((uint64_t)c_size + 1) << (mult + 2 + read_bl_len)) >> 9);
    }
    if ((e = dwmmc_cmd(&host, 7, rca << 16, MMC_R1, r)))
        return failf("CMD7", e);
    if ((e = app_cmd(6, 2, MMC_R1, r)))
        return failf("ACMD6", e);
    dwmmc_bus_width(&host, 1);
    if (!hc && (e = dwmmc_cmd(&host, 16, 512, MMC_R1, r)))
        return failf("CMD16", e);
    if (dwmmc_clock(&host, 0))                  /* 12 MHz */
        return failf("clock", -1);
    err[0] = 0;
    return 0;
}

/* multi-block transfers end with a CMD12 sent here, as U-Boot and Linux
 * do (not the controller's auto-stop), then the card's busy */
static int stop(void)
{
    uint32_t r;
    int e = dwmmc_cmd(&host, 12, 0, MMC_R1 | MMC_STOP, &r);
    uint32_t t0 = timer_ticks();
    while (dwmmc_busy(&host))
        if (timer_ticks() - t0 > 1000000) {
            ksnprintf(host.err, sizeof host.err, "card busy after CMD12");
            return -1;
        }
    return e;
}

static int xfer(int write, uint32_t lba, uint32_t count, uint32_t *buf)
{
    dwmmc_data_setup(&host, 512, count * 512);
    uint32_t idx = write ? (count > 1 ? 25 : 24) : (count > 1 ? 18 : 17);
    uint32_t flags = MMC_R1 | MMC_DATA | (write ? MMC_WRITE : 0);
    int e = dwmmc_cmd(&host, idx, hc ? lba : lba * 512, flags, 0);
    if (e)
        return failf(write ? "write command" : "read command", e);
    if (dwmmc_data(&host, write, buf, count * 512)) {
        int r = failf(write ? "write" : "read", -1);
        if (count > 1)
            stop();
        return r;
    }
    if (count > 1 && (e = stop()) != 0)
        return failf("stop (CMD12)", e);
    return 0;
}

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!rca)
        return -1;
    static uint32_t tmp[128];
    uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 64 ? 64 : count;
        if (((uintptr_t)p & 3) == 0) {
            if (xfer(0, lba, n, (uint32_t *)p))
                return -1;
        } else {
            n = 1;                              /* unaligned buffer: bounce */
            if (xfer(0, lba, 1, tmp))
                return -1;
            for (int i = 0; i < 512; i++)
                p[i] = ((uint8_t *)tmp)[i];
        }
        lba += n; count -= n; p += n * 512;
    }
    return 0;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (!rca)
        return -1;
    static uint32_t tmp[128];
    const uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 64 ? 64 : count;
        if (((uintptr_t)p & 3) == 0) {
            if (xfer(1, lba, n, (uint32_t *)(uintptr_t)p))
                return -1;
        } else {
            n = 1;
            for (int i = 0; i < 512; i++)
                ((uint8_t *)tmp)[i] = p[i];
            if (xfer(1, lba, 1, tmp))
                return -1;
        }
        lba += n; count -= n; p += n * 512;
    }
    return 0;
}

uint32_t sd_blocks(void)        { return blocks; }
int sd_is_hc(void)              { return hc; }
const char *sd_error(void)      { return err; }
const char *sd_controller(void) { return "sdmmc0"; }
#endif
