/*
 * SD card on the BCM2835 EMMC controller (Arasan SDHCI, 0x20300000).
 * Polled PIO, 4-bit bus at 25 MHz, read and write.
 *
 * The Arasan block may lose a register write that follows another one
 * within two SD clock cycles, so every write is followed by a short delay
 * (6 us at 400 kHz during identification, 1 us afterwards).
 */
#include "sd.h"
#include "gpio.h"
#include "mmio.h"
#include "prop.h"
#include "timer.h"

#include <string.h>

#define EMMC            (PERIPHERAL_BASE + 0x300000)
#define ARG2            (EMMC + 0x00)
#define BLKSIZECNT      (EMMC + 0x04)
#define ARG1            (EMMC + 0x08)
#define CMDTM           (EMMC + 0x0C)
#define RESP0           (EMMC + 0x10)
#define RESP1           (EMMC + 0x14)
#define RESP2           (EMMC + 0x18)
#define RESP3           (EMMC + 0x1C)
#define DATA            (EMMC + 0x20)
#define STATUS          (EMMC + 0x24)
#define CONTROL0        (EMMC + 0x28)
#define CONTROL1        (EMMC + 0x2C)
#define INTERRUPT       (EMMC + 0x30)
#define IRPT_MASK       (EMMC + 0x34)
#define IRPT_EN         (EMMC + 0x38)
#define CONTROL2        (EMMC + 0x3C)

/* CMDTM */
#define TM_BLKCNT_EN    (1u << 1)
#define TM_AUTO_CMD12   (1u << 2)
#define TM_DAT_READ     (1u << 4)
#define TM_MULTI        (1u << 5)
#define RSP_NONE        (0u << 16)
#define RSP_136         (1u << 16)
#define RSP_48          (2u << 16)
#define RSP_48B         (3u << 16)
#define CMD_CRCCHK      (1u << 19)
#define CMD_IXCHK       (1u << 20)
#define CMD_ISDATA      (1u << 21)

#define R1   (RSP_48 | CMD_CRCCHK | CMD_IXCHK)
#define R1B  (RSP_48B | CMD_CRCCHK | CMD_IXCHK)
#define R2   (RSP_136 | CMD_CRCCHK)
#define R3   (RSP_48)
#define R6   R1
#define R7   R1

/* STATUS */
#define ST_CMD_INHIBIT  (1u << 0)
#define ST_DAT_INHIBIT  (1u << 1)

/* CONTROL0 */
#define C0_DWIDTH4      (1u << 1)

/* CONTROL1 */
#define C1_CLK_INTLEN   (1u << 0)
#define C1_CLK_STABLE   (1u << 1)
#define C1_CLK_EN       (1u << 2)
#define C1_TOUNIT_MAX   (0xEu << 16)
#define C1_SRST_HC      (1u << 24)
#define C1_SRST_CMD     (1u << 25)
#define C1_SRST_DATA    (1u << 26)

/* INTERRUPT */
#define INT_CMD_DONE    (1u << 0)
#define INT_DATA_DONE   (1u << 1)
#define INT_WRITE_RDY   (1u << 4)
#define INT_READ_RDY    (1u << 5)
#define INT_ERR         (1u << 15)
#define INT_CTO         (1u << 16)
#define INT_ERR_MASK    0xFFFF8000u

static uint32_t base_clock, write_delay = 6;
static uint32_t rca, blocks;
static int hc, ready;
static const char *err = "not initialised";

static void wr(uint32_t reg, uint32_t v)
{
    mmio_write(reg, v);
    timer_delay_us(write_delay);
}

static int wait_bits(uint32_t reg, uint32_t mask, int set, uint32_t timeout_us)
{
    uint32_t t0 = timer_ticks();
    while (((mmio_read(reg) & mask) != 0) != set)
        if (timer_ticks() - t0 > timeout_us)
            return -1;
    return 0;
}

static void reset_line(uint32_t bit)
{
    wr(CONTROL1, mmio_read(CONTROL1) | bit);
    wait_bits(CONTROL1, bit, 0, 100000);
}

static int set_clock(uint32_t hz)
{
    if (wait_bits(STATUS, ST_CMD_INHIBIT | ST_DAT_INHIBIT, 0, 100000))
        return -1;
    uint32_t c1 = mmio_read(CONTROL1) & ~C1_CLK_EN;
    wr(CONTROL1, c1);
    timer_delay_us(10);

    /* SDHCI v3 10-bit divider: f = base / (2 * div), div 0 = base */
    uint32_t div = (base_clock + 2 * hz - 1) / (2 * hz);
    if (div > 0x3FF) div = 0x3FF;
    c1 &= ~0xFFE0u;
    c1 |= (div & 0xFF) << 8 | (div >> 8 & 3) << 6 | C1_CLK_INTLEN;
    wr(CONTROL1, c1);
    if (wait_bits(CONTROL1, C1_CLK_STABLE, 1, 100000))
        return -1;
    wr(CONTROL1, c1 | C1_CLK_EN);
    timer_delay_us(100);
    return 0;
}

/* Sends a command; resp gets RESP0..3 (may be NULL). */
static int cmd(uint32_t idx, uint32_t flags, uint32_t arg, uint32_t *resp)
{
    uint32_t inhibit = ST_CMD_INHIBIT | ((flags & (CMD_ISDATA | RSP_48B)) ? ST_DAT_INHIBIT : 0);
    if (wait_bits(STATUS, inhibit, 0, 500000)) {
        err = "controller busy";
        return -1;
    }
    wr(INTERRUPT, 0xFFFFFFFFu);
    wr(ARG1, arg);
    wr(CMDTM, idx << 24 | flags);
    uint32_t t0 = timer_ticks(), irq;
    while (!((irq = mmio_read(INTERRUPT)) & (INT_CMD_DONE | INT_ERR))) {
        if (timer_ticks() - t0 > 500000) {
            err = "command timeout";
            reset_line(C1_SRST_CMD);
            return -1;
        }
    }
    if (irq & INT_ERR_MASK) {
        err = irq & INT_CTO ? "no response" : "command error";
        wr(INTERRUPT, 0xFFFFFFFFu);
        reset_line(C1_SRST_CMD);
        return -1;
    }
    wr(INTERRUPT, INT_CMD_DONE);
    if (resp) {
        resp[0] = mmio_read(RESP0);
        resp[1] = mmio_read(RESP1);
        resp[2] = mmio_read(RESP2);
        resp[3] = mmio_read(RESP3);
    }
    if ((flags & RSP_48B) == RSP_48B && !(flags & CMD_ISDATA)) {
        t0 = timer_ticks();
        while (!((irq = mmio_read(INTERRUPT)) & (INT_DATA_DONE | INT_ERR)))
            if (timer_ticks() - t0 > 500000)
                break;
        wr(INTERRUPT, INT_DATA_DONE);
    }
    return 0;
}

static int acmd(uint32_t idx, uint32_t flags, uint32_t arg, uint32_t *resp)
{
    if (cmd(55, R1, rca << 16, NULL))
        return -1;
    return cmd(idx, flags, arg, resp);
}

static void card_size(const uint32_t *r)
{
    /* R2 in RESP0..3 = CSD bits 127..8 shifted down by 8 */
    uint32_t structure = r[3] >> 22 & 3;
    if (structure == 1) {                       /* CSD 2.0: (C_SIZE + 1) x 512 KiB */
        uint32_t csize = r[1] >> 8 & 0x3FFFFF;
        blocks = (csize + 1) * 1024;
    } else {                                    /* CSD 1.0 */
        uint32_t csize = (r[1] >> 22) | (r[2] & 3) << 10;
        uint32_t mult = r[1] >> 7 & 7;
        uint32_t bl_len = r[2] >> 8 & 15;
        blocks = (csize + 1) << (mult + 2 + bl_len - 9);
    }
}

int sd_init(void)
{
    uint32_t r[4];

    ready = 0;
    rca = 0;
    write_delay = 6;

    /* SD pins on the Arasan controller: GPIO48 CLK, 49 CMD, 50-53 DAT */
    for (unsigned pin = 48; pin <= 53; pin++) {
        gpio_set_function(pin, GPIO_ALT3);
        gpio_set_pull(pin, pin == 48 ? GPIO_PULL_NONE : GPIO_PULL_UP);
    }

    base_clock = prop_clock_rate(1);            /* EMMC clock */
    if (!base_clock)
        base_clock = 250000000;

    wr(CONTROL0, 0);
    wr(CONTROL2, 0);
    wr(CONTROL1, C1_SRST_HC);
    if (wait_bits(CONTROL1, C1_SRST_HC | C1_SRST_CMD | C1_SRST_DATA, 0, 100000)) {
        err = "controller reset failed";
        return -1;
    }
    wr(CONTROL1, C1_CLK_INTLEN | C1_TOUNIT_MAX);
    if (set_clock(400000)) {
        err = "clock not stable";
        return -1;
    }
    wr(IRPT_EN, 0);
    wr(IRPT_MASK, 0xFFFFFFFFu);
    wr(INTERRUPT, 0xFFFFFFFFu);
    timer_delay_ms(2);

    if (cmd(0, RSP_NONE, 0, NULL)) {            /* GO_IDLE_STATE */
        err = "no card";
        return -1;
    }
    int v2 = cmd(8, R7, 0x1AA, r) == 0 && (r[0] & 0xFFF) == 0x1AA;   /* SEND_IF_COND */

    uint32_t t0 = timer_ticks();
    for (;;) {                                  /* SD_SEND_OP_COND until ready */
        if (acmd(41, R3, (v2 ? 0x40000000u : 0) | 0x00FF8000u, r)) {
            err = "no card";
            return -1;
        }
        if (r[0] & 0x80000000u)
            break;
        if (timer_ticks() - t0 > 1000000) {
            err = "card not ready";
            return -1;
        }
        timer_delay_ms(10);
    }
    hc = (r[0] >> 30) & 1;

    if (cmd(2, R2, 0, r) || cmd(3, R6, 0, r)) { /* ALL_SEND_CID, SEND_RELATIVE_ADDR */
        err = "identification failed";
        return -1;
    }
    rca = r[0] >> 16;
    blocks = 0;
    if (cmd(9, R2, rca << 16, r) == 0)          /* SEND_CSD */
        card_size(r);
    if (cmd(7, R1B, rca << 16, r)) {            /* SELECT_CARD */
        err = "select failed";
        return -1;
    }

    if (set_clock(25000000)) {
        err = "clock not stable";
        return -1;
    }
    write_delay = 1;
    if (acmd(6, R1, 2, r) == 0)                 /* SET_BUS_WIDTH 4 */
        wr(CONTROL0, mmio_read(CONTROL0) | C0_DWIDTH4);
    if (!hc && cmd(16, R1, 512, r)) {           /* SET_BLOCKLEN */
        err = "set block length failed";
        return -1;
    }
    ready = 1;
    err = "ok";
    return 0;
}

static int read_blocks(uint32_t lba, uint32_t count, uint8_t *buf)
{
    wr(BLKSIZECNT, count << 16 | 512);
    uint32_t flags = R1 | CMD_ISDATA | TM_DAT_READ;
    if (count > 1)
        flags |= TM_MULTI | TM_BLKCNT_EN | TM_AUTO_CMD12;
    if (cmd(count > 1 ? 18 : 17, flags, hc ? lba : lba * 512, NULL))
        return -1;

    for (uint32_t b = 0; b < count; b++) {
        uint32_t t0 = timer_ticks(), irq;
        while (!((irq = mmio_read(INTERRUPT)) & (INT_READ_RDY | INT_ERR))) {
            if (timer_ticks() - t0 > 500000) {
                err = "read timeout";
                goto fail;
            }
        }
        if (irq & INT_ERR_MASK) {
            err = "read error";
            goto fail;
        }
        wr(INTERRUPT, INT_READ_RDY);
        if ((uint32_t)buf & 3) {
            for (int i = 0; i < 128; i++) {
                uint32_t w = mmio_read(DATA);
                memcpy(buf + i * 4, &w, 4);
            }
        } else {
            uint32_t *p = (uint32_t *)buf;
            for (int i = 0; i < 128; i++)
                p[i] = mmio_read(DATA);
        }
        buf += 512;
    }
    uint32_t t0 = timer_ticks();
    while (!(mmio_read(INTERRUPT) & (INT_DATA_DONE | INT_ERR)))
        if (timer_ticks() - t0 > 500000)
            break;
    wr(INTERRUPT, 0xFFFFFFFFu);
    return 0;

fail:
    wr(INTERRUPT, 0xFFFFFFFFu);
    reset_line(C1_SRST_CMD | C1_SRST_DATA);
    return -1;
}

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!ready) {
        err = "not initialised";
        return -1;
    }
    uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 128 ? 128 : count;
        if (read_blocks(lba, n, p)) {
            if (read_blocks(lba, n, p))         /* one retry */
                return -1;
        }
        lba += n;
        count -= n;
        p += n * 512;
    }
    return 0;
}

/* Waits until the card has programmed the data: CMD13 until "ready for
 * data" in the transfer state. */
static int wait_card_ready(void)
{
    uint32_t r[4], t0 = timer_ticks();
    for (;;) {
        if (cmd(13, R1, rca << 16, r) == 0 && (r[0] & (1u << 8)) && ((r[0] >> 9) & 15) == 4)
            return 0;
        if (timer_ticks() - t0 > 1000000) {
            err = "card busy after write";
            return -1;
        }
    }
}

static int write_blocks(uint32_t lba, uint32_t count, const uint8_t *buf)
{
    wr(BLKSIZECNT, count << 16 | 512);
    uint32_t flags = R1 | CMD_ISDATA;
    if (count > 1)
        flags |= TM_MULTI | TM_BLKCNT_EN | TM_AUTO_CMD12;
    if (cmd(count > 1 ? 25 : 24, flags, hc ? lba : lba * 512, NULL))
        return -1;

    for (uint32_t b = 0; b < count; b++) {
        uint32_t t0 = timer_ticks(), irq;
        while (!((irq = mmio_read(INTERRUPT)) & (INT_WRITE_RDY | INT_ERR))) {
            if (timer_ticks() - t0 > 500000) {
                err = "write timeout";
                goto fail;
            }
        }
        if (irq & INT_ERR_MASK) {
            err = "write error";
            goto fail;
        }
        wr(INTERRUPT, INT_WRITE_RDY);
        for (int i = 0; i < 128; i++) {
            uint32_t w;
            memcpy(&w, buf + i * 4, 4);
            mmio_write(DATA, w);
        }
        buf += 512;
    }
    uint32_t t0 = timer_ticks(), irq;
    while (!((irq = mmio_read(INTERRUPT)) & (INT_DATA_DONE | INT_ERR))) {
        if (timer_ticks() - t0 > 1000000) {
            err = "write not completed";
            goto fail;
        }
    }
    if (irq & INT_ERR_MASK) {
        err = "write error";
        goto fail;
    }
    wr(INTERRUPT, 0xFFFFFFFFu);
    return wait_card_ready();

fail:
    wr(INTERRUPT, 0xFFFFFFFFu);
    reset_line(C1_SRST_CMD | C1_SRST_DATA);
    if (count > 1)
        cmd(12, R1B, 0, NULL);                  /* STOP_TRANSMISSION */
    wait_card_ready();
    return -1;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (!ready) {
        err = "not initialised";
        return -1;
    }
    const uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 128 ? 128 : count;
        if (write_blocks(lba, n, p) && write_blocks(lba, n, p))   /* one retry */
            return -1;
        lba += n;
        count -= n;
        p += n * 512;
    }
    return 0;
}

uint32_t sd_blocks(void) { return blocks; }
int sd_is_hc(void) { return hc; }
const char *sd_error(void) { return err; }
