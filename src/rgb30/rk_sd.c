/*
 * drivers/sd.h on the RGB30: the boot slot, SDMMC0 (Synopsys DesignWare
 * MSHC at 0xfe2b0000), polled PIO, 4-bit bus. U-Boot used the controller
 * with its DMA (IDMAC): everything is reset first.
 *
 * Clock: the CRU gives the controller 24 MHz, which it halves internally
 * ("CLKGEN_DIV" in Linux's dw_mmc-rockchip.c): 12 MHz in, /30 = 400 kHz for
 * the identification, bypass = 12 MHz for the data (what U-Boot runs a
 * default-speed card at, with the phases it leaves).
 */
#ifdef PLAT_RK3566
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "io.h"
#include "lib/printf.h"

#define SDMMC0      0xfe2b0000u
#define CRU         0xfdd20000u

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
#define VERID       0x06c
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
#define INT_DCRC    (1u << 7)
#define INT_RTO     (1u << 8)
#define INT_DRTO    (1u << 9)
#define INT_HTO     (1u << 10)
#define INT_FRUN    (1u << 11)
#define INT_SBE     (1u << 13)
#define INT_EBE     (1u << 15)
#define INT_DATA_ERR (INT_DCRC | INT_DRTO | INT_HTO | INT_FRUN | INT_SBE | INT_EBE)

#define C_RESP      (1u << 6)
#define C_LONG      (1u << 7)
#define C_CRC       (1u << 8)
#define C_DATA      (1u << 9)
#define C_WRITE     (1u << 10)
#define C_AUTOSTOP  (1u << 12)
#define C_WAITPRV   (1u << 13)
#define C_INIT      (1u << 15)
#define C_UPDCLK    (1u << 21)
#define C_HOLD      (1u << 29)
#define C_START     (1u << 31)

/* response kinds */
#define R_NONE  0
#define R1      (C_RESP | C_CRC)
#define R2      (C_RESP | C_LONG | C_CRC)
#define R3      (C_RESP)

static uint32_t rca, blocks;
static int hc;
static char err[64] = "not initialised";

static inline uint32_t rd(uint32_t off)          { return readl(SDMMC0 + off); }
static inline void wr(uint32_t off, uint32_t v)  { writel(SDMMC0 + off, v); }

static int fail(const char *what, uint32_t v)
{
    ksnprintf(err, sizeof err, "%s (%08lx)", what, v);
    return -1;
}

static int wait_clear(uint32_t off, uint32_t mask, uint32_t us)
{
    uint32_t t0 = timer_ticks();
    while (rd(off) & mask)
        if (timer_ticks() - t0 > us)
            return -1;
    return 0;
}

static int clock_cmd(void)
{
    wr(CMD, C_START | C_UPDCLK | C_WAITPRV);
    return wait_clear(CMD, C_START, 100000);
}

static int set_clock(uint32_t div)
{
    wr(CLKENA, 0);
    wr(CLKSRC, 0);
    if (clock_cmd())
        return -1;
    wr(CLKDIV, div);
    if (clock_cmd())
        return -1;
    wr(CLKENA, 1);
    return clock_cmd();
}

/* Sends a command; for data commands the caller set BLKSIZ/BYTCNT. */
static int send(uint32_t idx, uint32_t arg, uint32_t flags, uint32_t *resp)
{
    if (wait_clear(STATUS, 1u << 9, 500000))            /* card busy on DAT0 */
        return fail("card busy", rd(STATUS));
    wr(RINTSTS, 0xffffffffu);
    wr(CMDARG, arg);
    wr(CMD, C_START | C_HOLD | (idx == 12 ? 0 : C_WAITPRV) | flags | idx);
    uint32_t t0 = timer_ticks(), st;
    while (!((st = rd(RINTSTS)) & INT_CDONE))
        if (timer_ticks() - t0 > 500000)
            return fail("command timeout", idx);
    if (st & INT_RTO)
        return -2;                                      /* no response */
    if (st & INT_RE)
        return fail("response error", idx);
    if ((flags & C_CRC) && (st & INT_RCRC))
        return fail("response CRC", idx);
    if (resp) {
        if (flags & C_LONG) {
            resp[0] = rd(RESP3); resp[1] = rd(RESP2);
            resp[2] = rd(RESP1); resp[3] = rd(RESP0);
        } else {
            resp[0] = rd(RESP0);
        }
    }
    return 0;
}

static int app_cmd(uint32_t idx, uint32_t arg, uint32_t flags, uint32_t *resp)
{
    int r = send(55, rca << 16, R1, 0);
    return r ? r : send(idx, arg, flags, resp);
}

static void reset_fifo(void)
{
    wr(CTRL, rd(CTRL) | 2);
    wait_clear(CTRL, 2, 100000);
}

int sd_init(void)
{
    uint32_t r[4];
    rca = blocks = 0;
    hc = 0;
    /* clocks on, source 24 MHz (CLKSEL_CON30[10:8] = 0) */
    writel(CRU + 0x33c, 0x00030000u);
    writel(CRU + 0x178, 0x07000000u);
    if (rd(CDETECT) & 1)
        return fail("no card in the slot", rd(CDETECT));

    /* controller, FIFO and DMA reset; no DMA, no interrupts */
    wr(PWREN, 1);
    wr(CTRL, (rd(CTRL) & ~((1u << 4) | (1u << 5) | (1u << 25))) | 7);
    if (wait_clear(CTRL, 7, 100000))
        return fail("controller reset", rd(CTRL));
    wr(BMOD, 1);
    wr(BMOD, 0);
    wr(IDINTEN, 0);
    wr(RINTSTS, 0xffffffffu);
    wr(INTMASK, 0);
    wr(TMOUT, 0xffffffffu);
    wr(FIFOTH, 0x207f0080u);
    wr(CTYPE, 0);
    wr(UHS_REG, 0);
    if (set_clock(15))                                  /* 400 kHz */
        return fail("clock", rd(CMD));
    timer_delay_ms(2);

    send(0, 0, C_INIT, 0);
    int v2 = send(8, 0x1aa, R1, r) == 0 && (r[0] & 0xfff) == 0x1aa;
    uint32_t t0 = timer_ticks();
    do {
        int e = app_cmd(41, (v2 ? 0x40000000u : 0) | 0x00ff8000u, R3, r);
        if (e && e != -2)
            return -1;
        if (e == -2)
            return fail("no answer to ACMD41", 0);
        if (timer_ticks() - t0 > 1500000)
            return fail("card stays busy", r[0]);
        if (!(r[0] & 0x80000000u))
            timer_delay_ms(10);
    } while (!(r[0] & 0x80000000u));
    hc = (r[0] >> 30) & 1;
    if (send(2, 0, R2, r))
        return fail("CMD2", 0);
    if (send(3, 0, R1, r))
        return fail("CMD3", 0);
    rca = r[0] >> 16;
    if (send(9, rca << 16, R2, r))
        return fail("CMD9", 0);
    /* r[0] = CSD bits 127..96 ... r[3] = bits 31..0 (CRC byte dropped:
     * the controller gives bits 127..8 shifted, U-Boot's layout) */
    if ((r[0] >> 30) == 1) {                            /* CSD 2.0 */
        uint32_t c_size = ((r[1] & 0x3f) << 16) | (r[2] >> 16);
        blocks = (c_size + 1) * 1024;
    } else {
        uint32_t read_bl_len = (r[1] >> 16) & 0xf;
        uint32_t c_size = ((r[1] & 0x3ff) << 2) | (r[2] >> 30);
        uint32_t mult = (r[2] >> 15) & 7;
        blocks = ((c_size + 1) << (mult + 2)) << read_bl_len >> 9;
    }
    if (send(7, rca << 16, R1, r))
        return fail("CMD7", 0);
    if (app_cmd(6, 2, R1, r))
        return fail("ACMD6", 0);
    wr(CTYPE, 1);
    if (!hc && send(16, 512, R1, r))
        return fail("CMD16", 0);
    if (set_clock(0))                                   /* 12 MHz */
        return fail("clock", rd(CMD));
    err[0] = 0;
    return 0;
}

static int data_xfer(int write, uint32_t lba, uint32_t count, uint32_t *buf)
{
    reset_fifo();
    wr(BLKSIZ, 512);
    wr(BYTCNT, count * 512);
    uint32_t idx = write ? (count > 1 ? 25 : 24) : (count > 1 ? 18 : 17);
    uint32_t flags = R1 | C_DATA | (write ? C_WRITE : 0) | (count > 1 ? C_AUTOSTOP : 0);
    if (send(idx, hc ? lba : lba * 512, flags, 0))
        return -1;
    uint32_t words = count * 128, done = 0, t0 = timer_ticks();
    for (;;) {
        uint32_t st = rd(RINTSTS);
        if (st & INT_DATA_ERR)
            return fail(write ? "write error" : "read error", st);
        if (!write && (st & (INT_RXDR | INT_DTO))) {
            uint32_t n = (rd(STATUS) >> 17) & 0x1fff;
            for (; n && done < words; n--)
                buf[done++] = rd(DATA);
            wr(RINTSTS, INT_RXDR);
        }
        if (write && (st & INT_TXDR)) {
            uint32_t n = 256 - ((rd(STATUS) >> 17) & 0x1fff);
            for (; n && done < words; n--)
                wr(DATA, buf[done++]);
            wr(RINTSTS, INT_TXDR);
        }
        if ((st & INT_DTO) && (write || done >= words))
            break;
        if (timer_ticks() - t0 > 2000000)
            return fail("data timeout", st);
    }
    if (write && wait_clear(STATUS, 1u << 9, 1000000))
        return fail("write busy", rd(STATUS));
    return 0;
}

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!rca)
        return -1;
    uint32_t tmp[128];
    uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 64 ? 64 : count;
        if (((uintptr_t)p & 3) == 0) {
            if (data_xfer(0, lba, n, (uint32_t *)p))
                return -1;
        } else {
            n = 1;                                      /* unaligned buffer: bounce */
            if (data_xfer(0, lba, 1, tmp))
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
    uint32_t tmp[128];
    const uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 64 ? 64 : count;
        if (((uintptr_t)p & 3) == 0) {
            if (data_xfer(1, lba, n, (uint32_t *)(uintptr_t)p))
                return -1;
        } else {
            n = 1;
            for (int i = 0; i < 512; i++)
                ((uint8_t *)tmp)[i] = p[i];
            if (data_xfer(1, lba, 1, tmp))
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
