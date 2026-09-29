/*
 * SD card on the BCM2835 SDHOST controller (0x20202000), polled PIO, 4-bit
 * bus at 25 MHz. This is the controller Linux uses for the card: it leaves
 * the Arasan one (EMMC) free for the WiFi chip on GPIO34-39.
 *
 * Register use follows Linux drivers/mmc/host/bcm2835.c: no automatic
 * CMD12 (sent by hand after multi-block transfers), a 16-word FIFO whose
 * fill level is in SDEDM, long responses not shifted (the CRC byte is
 * included, unlike the Arasan controller).
 */
#include "sd_backend.h"
#include "gpio.h"
#include "mmio.h"
#include "prop.h"
#include "timer.h"

#include <string.h>

#define SDHOST          (PERIPHERAL_BASE + 0x202000)
#define SDCMD           (SDHOST + 0x00)
#define SDARG           (SDHOST + 0x04)
#define SDTOUT          (SDHOST + 0x08)
#define SDCDIV          (SDHOST + 0x0C)
#define SDRSP0          (SDHOST + 0x10)
#define SDRSP1          (SDHOST + 0x14)
#define SDRSP2          (SDHOST + 0x18)
#define SDRSP3          (SDHOST + 0x1C)
#define SDHSTS          (SDHOST + 0x20)
#define SDVDD           (SDHOST + 0x30)
#define SDEDM           (SDHOST + 0x34)
#define SDHCFG          (SDHOST + 0x38)
#define SDHBCT          (SDHOST + 0x3C)
#define SDDATA          (SDHOST + 0x40)
#define SDHBLC          (SDHOST + 0x50)

/* SDCMD */
#define CMD_NEW         0x8000u
#define CMD_FAIL        0x4000u
#define CMD_NO_RESP     0x0400u
#define CMD_LONG_RESP   0x0200u
#define CMD_WRITE       0x0080u
#define CMD_READ        0x0040u

/* SDHSTS */
#define HSTS_REW_TIMEOUT 0x80u
#define HSTS_CMD_TIMEOUT 0x40u
#define HSTS_CRC16       0x20u
#define HSTS_CRC7        0x10u
#define HSTS_FIFO_ERR    0x08u
#define HSTS_ERRORS      0xF8u
#define HSTS_CLEAR       0x7F8u

/* SDHCFG */
#define HCFG_SLOW_CARD  (1u << 3)
#define HCFG_WIDE_EXT   (1u << 2)
#define HCFG_WIDE_INT   (1u << 1)

/* SDEDM */
#define EDM_FIFO_LEVEL(e) (((e) >> 4) & 0x1F)
#define EDM_THRESHOLDS  ((0x1Fu << 14) | (0x1Fu << 9))
#define EDM_THRESH_4    ((4u << 14) | (4u << 9))

#define EDM_FSM(e)      ((e) & 0xF)
#define EDM_FORCE_DATA  (1u << 19)
#define FSM_IDENT       0x0
#define FSM_DATA        0x1
#define FSM_READWAIT    0x4
#define FSM_WRITESTART1 0xA

#define FIFO_WORDS      16

/* command response kinds (our own flags, turned into SDCMD bits) */
#define R_NONE  0
#define R_SHORT 1
#define R_LONG  2

static uint32_t core_clock;
static uint32_t rca, blocks;
static int hc, ready;
static uint32_t hcfg;
static const char *err = "not initialised";

static void set_clock(uint32_t hz)
{
    /* f = core / (div + 2) */
    uint32_t div = (core_clock + hz - 1) / hz;
    div = div < 2 ? 0 : div - 2;
    if (div > 0x7FF) div = 0x7FF;
    mmio_write(SDCDIV, div);
    uint32_t actual = core_clock / (div + 2);
    mmio_write(SDTOUT, actual / 2);             /* about half a second */
}

/* Sends a command; resp gets the response in the Arasan layout (long
 * responses: bits 127..8, shifted down by 8), so card_size() is shared. */
static int cmd_x(uint32_t idx, int kind, uint32_t data_flags, uint32_t arg, uint32_t *resp)
{
    uint32_t t0 = timer_ticks();
    while (mmio_read(SDCMD) & CMD_NEW)
        if (timer_ticks() - t0 > 500000) {
            err = "controller busy";
            return -1;
        }
    uint32_t st = mmio_read(SDHSTS);
    if (st & HSTS_ERRORS)
        mmio_write(SDHSTS, st);
    mmio_write(SDARG, arg);
    uint32_t c = (idx & 0x3F) | data_flags | CMD_NEW;
    if (kind == R_NONE) c |= CMD_NO_RESP;
    else if (kind == R_LONG) c |= CMD_LONG_RESP;
    mmio_write(SDCMD, c);

    t0 = timer_ticks();
    uint32_t v;
    while ((v = mmio_read(SDCMD)) & CMD_NEW)
        if (timer_ticks() - t0 > 500000) {
            err = "command timeout";
            return -1;
        }
    if (v & CMD_FAIL) {
        st = mmio_read(SDHSTS);
        err = st & HSTS_CMD_TIMEOUT ? "no response" : "command error";
        mmio_write(SDHSTS, HSTS_CLEAR);
        return -1;
    }
    if (resp && kind == R_SHORT) {
        resp[0] = mmio_read(SDRSP0);
        resp[1] = resp[2] = resp[3] = 0;
    } else if (resp && kind == R_LONG) {
        uint32_t r[4] = { mmio_read(SDRSP0), mmio_read(SDRSP1), mmio_read(SDRSP2), mmio_read(SDRSP3) };
        for (int i = 0; i < 4; i++)
            resp[i] = r[i] >> 8 | (i < 3 ? r[i + 1] << 24 : 0);
    }
    return 0;
}

static int cmd(uint32_t idx, int kind, uint32_t arg, uint32_t *resp)
{
    return cmd_x(idx, kind, 0, arg, resp);
}

static int acmd(uint32_t idx, int kind, uint32_t arg, uint32_t *resp)
{
    if (cmd(55, R_SHORT, rca << 16, NULL))
        return -1;
    return cmd(idx, kind, arg, resp);
}

/* CMD13 until the card is ready for data in the transfer state (after
 * select, writes and stops: we do not use the controller's busy wait). */
static int wait_card_ready(uint32_t timeout_us)
{
    uint32_t r[4], t0 = timer_ticks();
    for (;;) {
        if (cmd(13, R_SHORT, rca << 16, r) == 0 && (r[0] & (1u << 8)) && ((r[0] >> 9) & 15) == 4)
            return 0;
        if (timer_ticks() - t0 > timeout_us) {
            err = "card busy";
            return -1;
        }
    }
}

static void card_size(const uint32_t *r)
{
    uint32_t structure = r[3] >> 22 & 3;
    if (structure == 1) {
        uint32_t csize = r[1] >> 8 & 0x3FFFFF;
        blocks = (csize + 1) * 1024;
    } else {
        uint32_t csize = (r[1] >> 22) | (r[2] & 3) << 10;
        uint32_t mult = r[1] >> 7 & 7;
        uint32_t bl_len = r[2] >> 8 & 15;
        blocks = (csize + 1) << (mult + 2 + bl_len - 9);
    }
}

int sdhost_init(void)
{
    uint32_t r[4];
    ready = 0;
    rca = 0;

    /* SD pins on SDHOST: GPIO48 CLK, 49 CMD, 50-53 DAT (ALT0) */
    for (unsigned pin = 48; pin <= 53; pin++) {
        gpio_set_function(pin, GPIO_ALT0);
        gpio_set_pull(pin, pin == 48 ? GPIO_PULL_NONE : GPIO_PULL_UP);
    }
    core_clock = prop_clock_rate(CLOCK_CORE);
    if (!core_clock)
        core_clock = 250000000;

    /* reset, as Linux does */
    mmio_write(SDVDD, 0);
    mmio_write(SDCMD, 0);
    mmio_write(SDARG, 0);
    mmio_write(SDTOUT, 0xF00000);
    mmio_write(SDCDIV, 0);
    mmio_write(SDHSTS, HSTS_CLEAR);
    mmio_write(SDHCFG, 0);
    mmio_write(SDHBCT, 0);
    mmio_write(SDHBLC, 0);
    uint32_t edm = mmio_read(SDEDM);
    mmio_write(SDEDM, (edm & ~EDM_THRESHOLDS) | EDM_THRESH_4);
    timer_delay_ms(20);
    mmio_write(SDVDD, 1);
    timer_delay_ms(20);
    hcfg = HCFG_SLOW_CARD | HCFG_WIDE_INT;
    mmio_write(SDHCFG, hcfg);
    set_clock(400000);
    timer_delay_ms(2);

    if (cmd(0, R_NONE, 0, NULL)) {
        err = "no card";
        return -1;
    }
    int v2 = cmd(8, R_SHORT, 0x1AA, r) == 0 && (r[0] & 0xFFF) == 0x1AA;

    uint32_t t0 = timer_ticks();
    for (;;) {
        if (acmd(41, R_SHORT, (v2 ? 0x40000000u : 0) | 0x00FF8000u, r)) {
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

    if (cmd(2, R_LONG, 0, r) || cmd(3, R_SHORT, 0, r)) {
        err = "identification failed";
        return -1;
    }
    rca = r[0] >> 16;
    blocks = 0;
    if (cmd(9, R_LONG, rca << 16, r) == 0)
        card_size(r);
    if (cmd(7, R_SHORT, rca << 16, r) || wait_card_ready(500000)) {
        err = "select failed";
        return -1;
    }
    set_clock(25000000);
    if (acmd(6, R_SHORT, 2, r) == 0) {           /* 4-bit bus */
        hcfg |= HCFG_WIDE_EXT;
        mmio_write(SDHCFG, hcfg);
    }
    if (!hc && cmd(16, R_SHORT, 512, r)) {
        err = "set block length failed";
        return -1;
    }
    ready = 1;
    err = "ok";
    return 0;
}

/* The controller's state machine back to idle after a transfer (Linux
 * bcm2835_wait_transfer_complete): only then may CMD12 be sent, or the
 * last block of a write would be cut. */
static int wait_transfer_complete(int write)
{
    const uint32_t alt_idle = write ? FSM_WRITESTART1 : FSM_READWAIT;
    uint32_t t0 = timer_ticks();
    for (;;) {
        uint32_t edm = mmio_read(SDEDM), fsm = EDM_FSM(edm);
        if (fsm == FSM_IDENT || fsm == FSM_DATA)
            return 0;
        if (fsm == alt_idle) {
            mmio_write(SDEDM, edm | EDM_FORCE_DATA);
            return 0;
        }
        if (timer_ticks() - t0 > 500000) {
            err = write ? "write not completed" : "read not completed";
            return -1;
        }
    }
}

static int stop(void)
{
    cmd(12, R_SHORT, 0, NULL);
    return wait_card_ready(1000000);
}

static int read_blocks(uint32_t lba, uint32_t count, uint8_t *buf)
{
    mmio_write(SDHBCT, 512);
    mmio_write(SDHBLC, count);
    if (cmd_x(count > 1 ? 18 : 17, R_SHORT, CMD_READ, hc ? lba : lba * 512, NULL))
        return -1;
    uint32_t words = count * 128, done = 0;
    uint32_t t0 = timer_ticks();
    while (done < words) {
        uint32_t level = EDM_FIFO_LEVEL(mmio_read(SDEDM));
        if (!level) {
            uint32_t st = mmio_read(SDHSTS);
            if (st & (HSTS_CRC16 | HSTS_FIFO_ERR | HSTS_REW_TIMEOUT)) {
                err = "read error";
                goto fail;
            }
            if (timer_ticks() - t0 > 500000) {
                err = "read timeout";
                goto fail;
            }
            continue;
        }
        if (level > words - done)
            level = words - done;
        for (; level; level--, done++) {
            uint32_t w = mmio_read(SDDATA);
            memcpy(buf + done * 4, &w, 4);
        }
        t0 = timer_ticks();
    }
    if (wait_transfer_complete(0))
        goto fail;
    if (count > 1 && stop())
        return -1;
    mmio_write(SDHSTS, HSTS_CLEAR);
    return 0;
fail:
    mmio_write(SDHSTS, HSTS_CLEAR);
    stop();
    return -1;
}

static int write_blocks(uint32_t lba, uint32_t count, const uint8_t *buf)
{
    mmio_write(SDHBCT, 512);
    mmio_write(SDHBLC, count);
    if (cmd_x(count > 1 ? 25 : 24, R_SHORT, CMD_WRITE, hc ? lba : lba * 512, NULL))
        return -1;
    uint32_t words = count * 128, done = 0;
    uint32_t t0 = timer_ticks();
    while (done < words) {
        uint32_t level = EDM_FIFO_LEVEL(mmio_read(SDEDM));
        uint32_t space = level < FIFO_WORDS ? FIFO_WORDS - level : 0;
        if (!space) {
            if (mmio_read(SDHSTS) & (HSTS_CRC16 | HSTS_FIFO_ERR | HSTS_REW_TIMEOUT)) {
                err = "write error";
                goto fail;
            }
            if (timer_ticks() - t0 > 500000) {
                err = "write timeout";
                goto fail;
            }
            continue;
        }
        if (space > words - done)
            space = words - done;
        for (; space; space--, done++) {
            uint32_t w;
            memcpy(&w, buf + done * 4, 4);
            mmio_write(SDDATA, w);
        }
        t0 = timer_ticks();
    }
    /* the FIFO drains to the card, then the card programs the blocks */
    t0 = timer_ticks();
    while (EDM_FIFO_LEVEL(mmio_read(SDEDM)))
        if (timer_ticks() - t0 > 500000) {
            err = "write not completed";
            goto fail;
        }
    if (wait_transfer_complete(1))
        goto fail;
    if (mmio_read(SDHSTS) & (HSTS_CRC16 | HSTS_FIFO_ERR)) {
        err = "write error";
        goto fail;
    }
    if (count > 1 ? stop() : wait_card_ready(1000000))
        return -1;
    mmio_write(SDHSTS, HSTS_CLEAR);
    return 0;
fail:
    mmio_write(SDHSTS, HSTS_CLEAR);
    stop();
    return -1;
}

int sdhost_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!ready) {
        err = "not initialised";
        return -1;
    }
    uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 128 ? 128 : count;
        if (read_blocks(lba, n, p) && read_blocks(lba, n, p))     /* one retry */
            return -1;
        lba += n;
        count -= n;
        p += n * 512;
    }
    return 0;
}

int sdhost_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (!ready) {
        err = "not initialised";
        return -1;
    }
    const uint8_t *p = buf;
    while (count) {
        uint32_t n = count > 128 ? 128 : count;
        if (write_blocks(lba, n, p) && write_blocks(lba, n, p))
            return -1;
        lba += n;
        count -= n;
        p += n * 512;
    }
    return 0;
}

uint32_t sdhost_blocks(void) { return blocks; }
int sdhost_is_hc(void) { return hc; }
const char *sdhost_error(void) { return err; }
