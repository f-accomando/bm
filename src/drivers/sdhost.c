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
#include "lib/printf.h"

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

#define WAIT_US         500000  /* a wait for the controller or the card */
#define SLOW_READ_US    50000   /* a read that waited longer has a line in the log (64 blocks: ~3 ms) */

static uint32_t core_clock;
static uint32_t rca, blocks;
static int hc, ready;
static uint32_t hcfg;
static const char *err = "not initialised";
/* since boot: transfers tried again, waits that ran out (sd_counters) */
static uint32_t n_retries, n_timeouts;

/* What the last failed try saw, for its line in the log (2026-10-11: the
 * Pi's reads time out now and then and work at the retry; the counters say
 * how often, this says which wait and in what state). */
typedef struct {
    const char *what;           /* err then: the wait that ran out, or the error */
    uint32_t us;                /* how far into the try */
    uint32_t done, words;       /* data words moved, of */
    uint32_t edm, hsts;         /* SDEDM, SDHSTS then (before anything is cleared) */
    uint32_t cmd_r1;            /* the card's answer to the read or write command, ~0 if none */
    uint32_t r1;                /* its status (CMD13) right after, before the stop; ~0 if none */
} fail_t;
static fail_t fail;
static uint32_t n_lines;        /* lines in the log so far */

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
 * responses: bits 127..8, shifted down by 8), so card_size() is shared.
 * wait_us: how long the controller may take (before and after). */
static int cmd_x(uint32_t idx, int kind, uint32_t data_flags, uint32_t arg, uint32_t *resp, uint32_t wait_us)
{
    uint32_t t0 = timer_ticks();
    while (mmio_read(SDCMD) & CMD_NEW)
        if (timer_ticks() - t0 > wait_us) {
            err = "controller busy";
            n_timeouts++;
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
        if (timer_ticks() - t0 > wait_us) {
            err = "command timeout";
            n_timeouts++;
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
    return cmd_x(idx, kind, 0, arg, resp, WAIT_US);
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
            n_timeouts++;
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
        if (timer_ticks() - t0 > WAIT_US) {
            err = write ? "write not completed" : "read not completed";
            n_timeouts++;
            return -1;
        }
    }
}

static int stop(void)
{
    cmd(12, R_SHORT, 0, NULL);
    return wait_card_ready(1000000);
}

/* ---------------------------------------------------------------- the log of a failed try */

/* the first 32 lines, then one in 32: each is a stall of the card already */
static int may_log(void)
{
    const uint32_t n = n_lines++;
    return n < 32 || n % 32 == 0;
}

static const char *fsm_name(uint32_t edm)
{
    static const char *const names[16] = {
        "ident", "data", "readdata", "writedata", "readwait", "readcrc", "writecrc", "writewait1",
        "powerdown", "powerup", "writestart1", "writestart2", "genpulses", "writewait2", "?", "startpowdown",
    };
    return names[EDM_FSM(edm)];
}

/* the card's state in an R1 status (bits 12..9) */
static const char *card_state(uint32_t r1)
{
    static const char *const names[16] = { "idle", "ready", "ident", "stby", "tran", "data", "rcv", "prg", "dis" };
    if (r1 == ~0u)
        return "no answer";
    const char *s = names[(r1 >> 9) & 15];
    return s ? s : "?";
}

static const char *hsts_name(uint32_t h)
{
    if (h & HSTS_REW_TIMEOUT)
        return "data timeout";
    if (h & HSTS_CRC16)
        return "data CRC";
    if (h & HSTS_FIFO_ERR)
        return "FIFO error";
    if (h & HSTS_CMD_TIMEOUT)
        return "command timeout";
    if (h & HSTS_CRC7)
        return "command CRC";
    return "none";
}

/* A try failed: what it saw, before anything is cleared. */
static void note_fail(uint32_t start, uint32_t done, uint32_t words, uint32_t cmd_r1)
{
    fail.what = err;
    fail.us = timer_ticks() - start;
    fail.done = done;
    fail.words = words;
    fail.edm = mmio_read(SDEDM);
    fail.hsts = mmio_read(SDHSTS);
    fail.cmd_r1 = cmd_r1;
    fail.r1 = ~0u;
}

/* The card's own status (CMD13) for the log, before the stop: "data" or
 * "rcv" if it is still sending or receiving, "tran" if it thinks it is
 * done or never started. A short wait; neither err nor the counters move. */
static void note_card(void)
{
    const char *const what = err;
    const uint32_t timeouts = n_timeouts;
    uint32_t r[4];
    fail.r1 = cmd_x(13, R_SHORT, 0, rca << 16, r, 10000) == 0 ? r[0] : ~0u;
    err = what;
    n_timeouts = timeouts;
}

/* One line for a transfer that failed: what its first try saw, and how the
 * others ended (2026-10-11: which wait runs out on the Pi, and why). */
static void log_fail(const char *op, uint32_t lba, uint32_t count, const fail_t *f, int ok, int tries, uint32_t us)
{
    if (!may_log())
        return;
    char last[48] = "";
    if (!ok || tries > 2)
        ksnprintf(last, sizeof last, ", the last: %s", fail.what);
    kprintf("sd: %s %lu at %lu: %s after %lu.%lu ms, %lu/%lu words; controller %s, fifo %lu, hsts %03lx (%s); "
            "card %s (%08lx, to the command %08lx); %s in %lu ms, %d tries%s\n", op, (unsigned long)count,
            (unsigned long)lba, f->what, (unsigned long)(f->us / 1000), (unsigned long)(f->us / 100 % 10),
            (unsigned long)f->done, (unsigned long)f->words, fsm_name(f->edm),
            (unsigned long)EDM_FIFO_LEVEL(f->edm), (unsigned long)(f->hsts & 0x7FF), hsts_name(f->hsts),
            card_state(f->r1), (unsigned long)f->r1, (unsigned long)f->cmd_r1, ok ? "then ok" : "failed",
            (unsigned long)(us / 1000), tries, last);
}

/* ---------------------------------------------------------------- transfers */

static int read_blocks(uint32_t lba, uint32_t count, uint8_t *buf)
{
    const uint32_t start = timer_ticks(), words = count * 128;
    uint32_t done = 0, waited = 0, r[4];        /* waited: the longest wait for data */
    r[0] = ~0u;
    mmio_write(SDHBCT, 512);
    mmio_write(SDHBLC, count);
    if (cmd_x(count > 1 ? 18 : 17, R_SHORT, CMD_READ, hc ? lba : lba * 512, r, WAIT_US)) {
        note_fail(start, 0, words, ~0u);
        return -1;
    }
    uint32_t t0 = timer_ticks();
    while (done < words) {
        uint32_t level = EDM_FIFO_LEVEL(mmio_read(SDEDM));
        if (!level) {
            uint32_t st = mmio_read(SDHSTS);
            if (st & (HSTS_CRC16 | HSTS_FIFO_ERR | HSTS_REW_TIMEOUT)) {
                err = "read error";
                n_timeouts += (st & HSTS_REW_TIMEOUT) != 0;    /* the controller's own (SDTOUT) */
                goto fail;
            }
            const uint32_t w = timer_ticks() - t0;
            if (w > WAIT_US) {
                err = "read timeout";
                n_timeouts++;
                goto fail;
            }
            if (w > waited)
                waited = w;
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
    const uint32_t data_end = timer_ticks();
    if (wait_transfer_complete(0))
        goto fail;
    /* the last block's CRC is checked after its data: seen only here (the
     * loop looks at the flags while it waits). A line for now, the data
     * kept as before: what the Pi says decides if it becomes a failure */
    const uint32_t end = mmio_read(SDHSTS);
    if (count > 1 && stop()) {
        note_fail(start, done, words, r[0]);
        return -1;
    }
    mmio_write(SDHSTS, HSTS_CLEAR);
    const uint32_t after = timer_ticks() - data_end;
    if ((end & (HSTS_CRC16 | HSTS_FIFO_ERR)) && may_log())
        kprintf("sd: read %lu at %lu: hsts %03lx (%s) at its end, the data kept\n", (unsigned long)count,
                (unsigned long)lba, (unsigned long)(end & 0x7FF), hsts_name(end));
    /* a slow card without a failure: its waits (not the time of the copy,
     * which in QEMU is long) */
    if ((waited > SLOW_READ_US || after > SLOW_READ_US) && may_log())
        kprintf("sd: read %lu at %lu ok but slow: %lu ms waiting for data, %lu ms after it\n",
                (unsigned long)count, (unsigned long)lba, (unsigned long)(waited / 1000),
                (unsigned long)(after / 1000));
    return 0;
fail:
    note_fail(start, done, words, r[0]);
    mmio_write(SDHSTS, HSTS_CLEAR);
    note_card();
    stop();
    return -1;
}

static int write_blocks(uint32_t lba, uint32_t count, const uint8_t *buf)
{
    const uint32_t start = timer_ticks(), words = count * 128;
    uint32_t done = 0, r[4];
    r[0] = ~0u;
    mmio_write(SDHBCT, 512);
    mmio_write(SDHBLC, count);
    if (cmd_x(count > 1 ? 25 : 24, R_SHORT, CMD_WRITE, hc ? lba : lba * 512, r, WAIT_US)) {
        note_fail(start, 0, words, ~0u);
        return -1;
    }
    uint32_t t0 = timer_ticks();
    while (done < words) {
        uint32_t level = EDM_FIFO_LEVEL(mmio_read(SDEDM));
        uint32_t space = level < FIFO_WORDS ? FIFO_WORDS - level : 0;
        if (!space) {
            const uint32_t st = mmio_read(SDHSTS);
            if (st & (HSTS_CRC16 | HSTS_FIFO_ERR | HSTS_REW_TIMEOUT)) {
                err = "write error";
                n_timeouts += (st & HSTS_REW_TIMEOUT) != 0;
                goto fail;
            }
            if (timer_ticks() - t0 > WAIT_US) {
                err = "write timeout";
                n_timeouts++;
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
        if (timer_ticks() - t0 > WAIT_US) {
            err = "write not completed";
            n_timeouts++;
            goto fail;
        }
    if (wait_transfer_complete(1))
        goto fail;
    if (mmio_read(SDHSTS) & (HSTS_CRC16 | HSTS_FIFO_ERR)) {
        err = "write error";
        goto fail;
    }
    if (count > 1 ? stop() : wait_card_ready(1000000)) {
        note_fail(start, done, words, r[0]);
        return -1;
    }
    mmio_write(SDHSTS, HSTS_CLEAR);
    return 0;
fail:
    note_fail(start, done, words, r[0]);
    mmio_write(SDHSTS, HSTS_CLEAR);
    note_card();
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
        if (read_blocks(lba, n, p)) {
            const fail_t first = fail;
            const uint32_t t0 = timer_ticks();
            n_retries++;                                        /* one retry */
            const int ok = read_blocks(lba, n, p) == 0;
            log_fail("read", lba, n, &first, ok, 2, timer_ticks() - t0);
            if (!ok)
                return -1;
        }
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
        if (write_blocks(lba, n, p)) {
            const fail_t first = fail;
            const uint32_t t0 = timer_ticks();
            n_retries++;
            const int ok = write_blocks(lba, n, p) == 0;
            log_fail("write", lba, n, &first, ok, 2, timer_ticks() - t0);
            if (!ok)
                return -1;
        }
        lba += n;
        count -= n;
        p += n * 512;
    }
    return 0;
}

uint32_t sdhost_blocks(void) { return blocks; }
int sdhost_is_hc(void) { return hc; }
const char *sdhost_error(void) { return err; }

void sdhost_counters(uint32_t *retries, uint32_t *timeouts)
{
    if (retries)
        *retries = n_retries;
    if (timeouts)
        *timeouts = n_timeouts;
}
