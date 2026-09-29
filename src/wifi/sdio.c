/*
 * SDIO host on the Arasan (EMMC) controller, wired to the WiFi half of the
 * BCM43438 on GPIO34-39 (ALT3), polled. The SD card is on SDHOST (sd.c).
 */
#include "sdio.h"
#include "drivers/gpio.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/timer.h"

#include <string.h>

#define EMMC            (PERIPHERAL_BASE + 0x300000)
#define BLKSIZECNT      (EMMC + 0x04)
#define ARG1            (EMMC + 0x08)
#define CMDTM           (EMMC + 0x0C)
#define RESP0           (EMMC + 0x10)
#define DATA            (EMMC + 0x20)
#define STATUS          (EMMC + 0x24)
#define CONTROL0        (EMMC + 0x28)
#define CONTROL1        (EMMC + 0x2C)
#define INTERRUPT       (EMMC + 0x30)
#define IRPT_MASK       (EMMC + 0x34)
#define IRPT_EN         (EMMC + 0x38)
#define CONTROL2        (EMMC + 0x3C)

#define TM_BLKCNT_EN    (1u << 1)
#define TM_DAT_READ     (1u << 4)
#define TM_MULTI        (1u << 5)
#define RSP_NONE        (0u << 16)
#define RSP_48          (2u << 16)
#define RSP_48B         (3u << 16)
#define CMD_CRCCHK      (1u << 19)
#define CMD_IXCHK       (1u << 20)
#define CMD_ISDATA      (1u << 21)

#define R1   (RSP_48 | CMD_CRCCHK | CMD_IXCHK)
#define R1B  (RSP_48B | CMD_CRCCHK | CMD_IXCHK)
#define R4   (RSP_48)                           /* no CRC, no index */
#define R5   R1
#define R6   R1

#define ST_CMD_INHIBIT  (1u << 0)
#define ST_DAT_INHIBIT  (1u << 1)
#define C0_DWIDTH4      (1u << 1)
#define C0_HCTL_HS_EN   (1u << 2)
#define C1_CLK_INTLEN   (1u << 0)
#define C1_CLK_STABLE   (1u << 1)
#define C1_CLK_EN       (1u << 2)
#define C1_TOUNIT_MAX   (0xEu << 16)
#define C1_SRST_HC      (1u << 24)
#define C1_SRST_CMD     (1u << 25)
#define C1_SRST_DATA    (1u << 26)
#define INT_CMD_DONE    (1u << 0)
#define INT_DATA_DONE   (1u << 1)
#define INT_WRITE_RDY   (1u << 4)
#define INT_READ_RDY    (1u << 5)
#define INT_ERR         (1u << 15)
#define INT_CTO         (1u << 16)
#define INT_ERR_MASK    0xFFFF8000u

#define WL_ON_GPIO      41              /* WL_REG_ON of the BCM43438 on the Zero W */

static uint32_t base_clock, write_delay = 6, rca;
static const char *err = "not started";

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
        if (timer_ticks() - t0 > 200000) {
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
    if (resp)
        *resp = mmio_read(RESP0);
    if ((flags & RSP_48B) == RSP_48B && !(flags & CMD_ISDATA)) {
        t0 = timer_ticks();
        while (!((irq = mmio_read(INTERRUPT)) & (INT_DATA_DONE | INT_ERR)))
            if (timer_ticks() - t0 > 500000)
                break;
        wr(INTERRUPT, INT_DATA_DONE);
    }
    return 0;
}

int sdio_rw_byte(int write, unsigned fn, uint32_t addr, uint8_t in, uint8_t *out)
{
    /* CMD52 IO_RW_DIRECT: R/W, function, RAW, address, data */
    uint32_t arg = (write ? 1u << 31 : 0) | (fn & 7) << 28 | (addr & 0x1FFFF) << 9 | in;
    uint32_t r;
    if (cmd(52, R5, arg, &r))
        return -1;
    if (r & 0xCB00) {                           /* COM_CRC, ILLEGAL, ERROR, FUNCTION, OUT_OF_RANGE */
        err = "CMD52 error flags";
        return -1;
    }
    if (out)
        *out = (uint8_t)r;
    return 0;
}

int sdio_read8(unsigned fn, uint32_t addr, uint8_t *out)
{
    return sdio_rw_byte(0, fn, addr, 0, out);
}

int sdio_write8(unsigned fn, uint32_t addr, uint8_t v)
{
    return sdio_rw_byte(1, fn, addr, v, NULL);
}

int sdio_rw_block(int write, unsigned fn, uint32_t addr, int incr, void *buf, uint32_t len)
{
    /* CMD53 IO_RW_EXTENDED in byte mode, up to 512 bytes, 4-byte multiples */
    if (len == 0 || len > 512 || (len & 3)) {
        err = "CMD53 length";
        return -1;
    }
    wr(BLKSIZECNT, 1u << 16 | len);
    uint32_t arg = (write ? 1u << 31 : 0) | (fn & 7) << 28 | (incr ? 1u << 26 : 0) |
                   (addr & 0x1FFFF) << 9 | (len == 512 ? 0 : len);
    uint32_t flags = R5 | CMD_ISDATA | (write ? 0 : TM_DAT_READ);
    if (cmd(53, flags, arg, NULL))
        return -1;
    uint32_t want = write ? INT_WRITE_RDY : INT_READ_RDY, t0 = timer_ticks(), irq;
    while (!((irq = mmio_read(INTERRUPT)) & (want | INT_ERR)))
        if (timer_ticks() - t0 > 200000) {
            err = "CMD53 data timeout";
            goto fail;
        }
    if (irq & INT_ERR_MASK) {
        err = "CMD53 data error";
        goto fail;
    }
    wr(INTERRUPT, want);
    uint8_t *p = buf;
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t w;
        if (write) {
            memcpy(&w, p + i, 4);
            mmio_write(DATA, w);
        } else {
            w = mmio_read(DATA);
            memcpy(p + i, &w, 4);
        }
    }
    t0 = timer_ticks();
    while (!((irq = mmio_read(INTERRUPT)) & (INT_DATA_DONE | INT_ERR)))
        if (timer_ticks() - t0 > 200000) {
            err = "CMD53 not completed";
            goto fail;
        }
    wr(INTERRUPT, 0xFFFFFFFFu);
    return 0;
fail:
    wr(INTERRUPT, 0xFFFFFFFFu);
    reset_line(C1_SRST_CMD | C1_SRST_DATA);
    return -1;
}

/* Every step printed through `say` (the monitor shows them on screen). */
int sdio_init(void (*say)(const char *step, int ok, uint32_t value))
{
    uint32_t r = 0;
    rca = 0;
    write_delay = 6;

    for (unsigned pin = 34; pin <= 39; pin++) {
        gpio_set_function(pin, GPIO_ALT3);
        gpio_set_pull(pin, pin == 34 ? GPIO_PULL_NONE : GPIO_PULL_UP);
    }
    gpio_set_function(WL_ON_GPIO, GPIO_OUTPUT);
    gpio_write(WL_ON_GPIO, 0);
    timer_delay_ms(20);
    gpio_write(WL_ON_GPIO, 1);
    timer_delay_ms(150);
    say("power on (WL_REG_ON = GPIO41)", 1, 0);

    base_clock = prop_clock_rate(1);
    if (!base_clock)
        base_clock = 250000000;
    wr(CONTROL0, 0);
    wr(CONTROL2, 0);
    wr(CONTROL1, C1_SRST_HC);
    if (wait_bits(CONTROL1, C1_SRST_HC | C1_SRST_CMD | C1_SRST_DATA, 0, 100000)) {
        err = "controller reset failed";
        say(err, 0, 0);
        return -1;
    }
    wr(CONTROL1, C1_CLK_INTLEN | C1_TOUNIT_MAX);
    if (set_clock(400000)) {
        err = "clock not stable";
        say(err, 0, 0);
        return -1;
    }
    wr(IRPT_EN, 0);
    wr(IRPT_MASK, 0xFFFFFFFFu);
    wr(INTERRUPT, 0xFFFFFFFFu);
    timer_delay_ms(2);
    say("controller at 400 kHz, base clock (Hz)", 1, base_clock);

    cmd(0, RSP_NONE, 0, NULL);                  /* GO_IDLE_STATE */
    if (cmd(5, R4, 0, &r)) {                    /* IO_SEND_OP_COND: what it supports */
        say("CMD5: no answer from the WiFi chip", 0, 0);
        return -1;
    }
    say("CMD5 OCR", 1, r);
    uint32_t t0 = timer_ticks();
    do {
        if (cmd(5, R4, 0x00200000, &r)) {       /* 3.2-3.4 V */
            say("CMD5 with voltage: no answer", 0, 0);
            return -1;
        }
        if (timer_ticks() - t0 > 1000000) {
            err = "chip not ready";
            say("CMD5: chip never ready", 0, r);
            return -1;
        }
    } while (!(r & 0x80000000u));
    say("chip ready, functions", 1, r >> 28 & 7);

    if (cmd(3, R6, 0, &r)) {
        say("CMD3: no address", 0, 0);
        return -1;
    }
    rca = r >> 16;
    say("CMD3 address", 1, rca);
    if (cmd(7, R1B, rca << 16, &r)) {
        say("CMD7: select failed", 0, 0);
        return -1;
    }
    say("CMD7 selected", 1, r);

    uint8_t rev = 0, bus = 0;
    if (sdio_read8(0, 0x00, &rev)) {
        say("CCCR read failed", 0, 0);
        return -1;
    }
    say("CCCR/SDIO revision", 1, rev);
    /* 4-bit bus, then 25 MHz */
    if (sdio_read8(0, 0x07, &bus) == 0 && sdio_write8(0, 0x07, (uint8_t)((bus & ~3u) | 2)) == 0) {
        wr(CONTROL0, mmio_read(CONTROL0) | C0_DWIDTH4);
        say("4-bit bus", 1, 0);
    } else {
        say("4-bit bus refused (1-bit)", 0, 0);
    }
    if (set_clock(25000000) == 0)
        write_delay = 1;
    say("clock 25 MHz", 1, 0);
    return 0;
}

const char *sdio_error(void) { return err; }
