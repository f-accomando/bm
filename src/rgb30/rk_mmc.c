/*
 * DesignWare MSHC, polled PIO (see rk_mmc.h). Register layout from U-Boot's
 * dwmmc.h and Linux's dw_mmc.h; data FIFO at 0x200 (version >= 2.40a).
 */
#ifdef PLAT_RK3566
#include "rk_mmc.h"
#include "io.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#define CTRL        0x000
#define PWREN       0x004
#define CLKDIV      0x008
#define CLKSRC      0x00c
#define CLKENA      0x010
#define TMOUT       0x014
#define CTYPE       0x018
#define BLKSIZ      0x01c
#define BYTCNT      0x020
#define INTMASK     0x024
#define CMDARG      0x028
#define CMD         0x02c
#define RESP0       0x030
#define RESP1       0x034
#define RESP2       0x038
#define RESP3       0x03c
#define RINTSTS     0x044
#define STATUS      0x048
#define FIFOTH      0x04c
#define CDETECT     0x050
#define UHS_REG     0x074
#define BMOD        0x080
#define IDINTEN     0x090
#define DATA        0x200

#define INT_RE      (1u << 1)
#define INT_CDONE   (1u << 2)
#define INT_DTO     (1u << 3)
#define INT_TXDR    (1u << 4)
#define INT_RXDR    (1u << 5)
#define INT_RCRC    (1u << 6)
#define INT_RTO     (1u << 8)
#define INT_DATA_ERR ((1u << 7) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 13) | (1u << 15))

#define C_WAITPRV   (1u << 13)
#define C_UPDCLK    (1u << 21)
#define C_HOLD      (1u << 29)
#define C_START     (1u << 31)

static inline uint32_t rd(dwmmc_t *h, uint32_t off)         { return readl(h->base + off); }
static inline void wr(dwmmc_t *h, uint32_t off, uint32_t v) { writel(h->base + off, v); }

static int fail(dwmmc_t *h, const char *what, uint32_t v)
{
    ksnprintf(h->err, sizeof h->err, "%s (%08lx)", what, v);
    return -1;
}

static int wait_clear(dwmmc_t *h, uint32_t off, uint32_t mask, uint32_t us)
{
    uint32_t t0 = timer_ticks();
    while (rd(h, off) & mask)
        if (timer_ticks() - t0 > us)
            return -1;
    return 0;
}

int dwmmc_reset(dwmmc_t *h)
{
    wr(h, PWREN, 1);
    wr(h, CTRL, (rd(h, CTRL) & ~((1u << 4) | (1u << 5) | (1u << 25))) | 7);
    if (wait_clear(h, CTRL, 7, 100000))
        return fail(h, "controller reset", rd(h, CTRL));
    wr(h, BMOD, 1);                                 /* IDMAC reset, then off */
    wr(h, BMOD, 0);
    wr(h, IDINTEN, 0);
    wr(h, RINTSTS, 0xffffffffu);
    wr(h, INTMASK, 0);
    wr(h, TMOUT, 0xffffffffu);
    wr(h, FIFOTH, 0x207f0080u);                     /* MSIZE 8, RX 127, TX 128 (256-word FIFO) */
    wr(h, CTYPE, 0);
    wr(h, UHS_REG, 0);
    h->err[0] = 0;
    return 0;
}

static int clock_cmd(dwmmc_t *h)
{
    wr(h, CMD, C_START | C_UPDCLK | C_WAITPRV);
    return wait_clear(h, CMD, C_START, 100000);
}

int dwmmc_clock(dwmmc_t *h, uint32_t div)
{
    wr(h, CLKENA, 0);
    wr(h, CLKSRC, 0);
    if (clock_cmd(h))
        return fail(h, "clock update", rd(h, CMD));
    wr(h, CLKDIV, div);
    if (clock_cmd(h))
        return fail(h, "clock update", rd(h, CMD));
    wr(h, CLKENA, 1);
    if (clock_cmd(h))
        return fail(h, "clock update", rd(h, CMD));
    return 0;
}

void dwmmc_bus_width(dwmmc_t *h, int four_bits)
{
    wr(h, CTYPE, four_bits ? 1 : 0);
}

int dwmmc_busy(dwmmc_t *h)
{
    return (rd(h, STATUS) >> 9) & 1;
}

int dwmmc_card_present(dwmmc_t *h)
{
    return !(rd(h, CDETECT) & 1);
}

int dwmmc_cmd(dwmmc_t *h, uint32_t idx, uint32_t arg, uint32_t flags, uint32_t *resp)
{
    if (!(flags & MMC_INIT) && wait_clear(h, STATUS, 1u << 9, 500000))
        return fail(h, "card busy", rd(h, STATUS));
    if (!(flags & MMC_DATA))
        wr(h, RINTSTS, 0xffffffffu);
    wr(h, CMDARG, arg);
    wr(h, CMD, C_START | C_HOLD | (idx == 12 ? 0 : C_WAITPRV) | flags | idx);
    uint32_t t0 = timer_ticks(), st;
    while (!((st = rd(h, RINTSTS)) & INT_CDONE))
        if (timer_ticks() - t0 > 500000)
            return fail(h, "command timeout", idx);
    wr(h, RINTSTS, INT_CDONE | INT_RE | INT_RCRC | INT_RTO);
    if (st & INT_RTO)
        return -2;
    if (st & INT_RE)
        return fail(h, "response error", idx);
    if ((flags & MMC_CRC) && (st & INT_RCRC))
        return fail(h, "response CRC", idx);
    if (resp) {
        if (flags & MMC_LONG) {
            resp[0] = rd(h, RESP3); resp[1] = rd(h, RESP2);
            resp[2] = rd(h, RESP1); resp[3] = rd(h, RESP0);
        } else {
            resp[0] = rd(h, RESP0);
        }
    }
    return 0;
}

void dwmmc_data_setup(dwmmc_t *h, uint32_t blksz, uint32_t bytes)
{
    wr(h, CTRL, rd(h, CTRL) | 2);                   /* FIFO reset */
    wait_clear(h, CTRL, 2, 100000);
    wr(h, RINTSTS, 0xffffffffu);
    wr(h, BLKSIZ, blksz);
    wr(h, BYTCNT, bytes);
}

int dwmmc_data(dwmmc_t *h, int write, uint32_t *buf, uint32_t len)
{
    uint32_t words = len / 4, done = 0, t0 = timer_ticks();
    for (;;) {
        uint32_t st = rd(h, RINTSTS);
        if (st & INT_DATA_ERR)
            return fail(h, write ? "write error" : "read error", st);
        if (!write && (st & (INT_RXDR | INT_DTO))) {
            uint32_t n = (rd(h, STATUS) >> 17) & 0x1fff;
            for (; n && done < words; n--)
                buf[done++] = rd(h, DATA);
            wr(h, RINTSTS, INT_RXDR);
        }
        if (write && (st & INT_TXDR)) {
            uint32_t n = 256 - ((rd(h, STATUS) >> 17) & 0x1fff);
            for (; n && done < words; n--)
                wr(h, DATA, buf[done++]);
            wr(h, RINTSTS, INT_TXDR);
        }
        if ((st & INT_DTO) && (write || done >= words)) {
            wr(h, RINTSTS, INT_DTO);
            break;
        }
        if (timer_ticks() - t0 > 2000000)
            return fail(h, "data timeout", st);
    }
    if (write && wait_clear(h, STATUS, 1u << 9, 1000000))
        return fail(h, "write busy", rd(h, STATUS));
    return 0;
}
#endif
