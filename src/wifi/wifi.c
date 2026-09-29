/*
 * WiFi of the Pi Zero W: the BCM43430 behind the SDIO host (sdio.c).
 *
 * Bring-up as in Plan 9's ether4330 and Linux's brcmfmac: the chip's
 * backplane is reached through function 1 (a 32 KiB window), the cores are
 * found in the enumeration ROM, the firmware and its board settings (NVRAM
 * text) are written into the chip's RAM and its ARM core is started; then
 * frames (SDPCM) go through function 2, and the control channel carries
 * BCDC ioctls ("ver", "cur_etheraddr", "clmload"...).
 *
 * Every step is printed: without a serial cable a photo of the screen must
 * tell how far it got.
 */
#include "wifi.h"
#include "sdio.h"
#include "bt/bt.h"
#include "drivers/sd.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "kernel/config.h"
#include "kernel/input.h"
#include "lib/printf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* function 1 registers of the SDIO core (brcmfmac sdio.h) */
#define SB_ADDR_LOW     0x1000A         /* backplane window, bits 15..8 */
#define SB_ADDR_MID     0x1000B         /* bits 23..16 */
#define SB_ADDR_HIGH    0x1000C         /* bits 31..24 */
#define WATERMARK       0x10008
#define FRAME_CTRL      0x1000D
#define CHIP_CLK_CSR    0x1000E
#define PULLUPS         0x1000F
#define RFRM_CNT        0x1001B

#define CLK_FORCE_ALP   0x01
#define CLK_FORCE_HT    0x02
#define CLK_REQ_ALP     0x08
#define CLK_REQ_HT      0x10
#define CLK_NO_HW_REQ   0x20
#define CLK_ALP_AVAIL   0x40
#define CLK_HT_AVAIL    0x80

#define ENUM_BASE       0x18000000u     /* chipcommon: chip id at 0, EROM pointer at 0xFC */
#define SB_WINDOW       0x8000u
#define SB_32BIT        0x8000u         /* 4-byte access flag in the function 1 address */

/* AI wrapper registers of a core */
#define IOCTRL          0x408
#define RESETCTRL       0x800

/* cores */
#define CORE_CHIPCOMMON 0x800
#define CORE_SOCRAM     0x80E
#define CORE_D11        0x812
#define CORE_SDIOD      0x829
#define CORE_ARM_CM3    0x82A

/* SDIO core registers (sdregs) */
#define SD_INTSTATUS    0x20
#define SD_INTMASK      0x24
#define SD_TOSBMAILBOXDATA 0x48
#define INT_FC_CHANGE   (1u << 5)
#define INT_FRAME       (1u << 6)
#define INT_MAILBOX     (1u << 7)

/* socram core */
#define SOCRAM_COREINFO 0x00
#define SOCRAM_BANKIDX  0x10
#define SOCRAM_BANKINFO 0x40
#define SOCRAM_BANKPDA  0x44

/* ioctls */
#define WLC_UP          2
#define WLC_SET_INFRA   20
#define WLC_SET_AUTH    22
#define WLC_SET_SSID    26
#define WLC_SET_WSEC_PMK 268
#define WLC_SET_PM      86
#define WLC_GET_VAR     262
#define WLC_SET_VAR     263

#define FW_FILE   "/bm33/brcmfmac43430-sdio.bin"
#define NVRAM_FILE "/bm33/brcmfmac43430-sdio.txt"
#define CLM_FILE  "/bm33/brcmfmac43430-sdio.clm_blob"

static struct {
    uint32_t window;
    uint32_t chipcommon, armctl, armregs, d11ctl, socramctl, socramregs, sdregs;
    int armcore, sdiorev, socramrev;
    uint32_t ramsize;
    uint8_t txseq, credit;
    uint16_t reqid;
    int up;                             /* firmware running, control channel works */
    uint8_t mac[6];
    char version[128];
} w;

static void say(const char *step, int ok, uint32_t value)
{
    char v[16] = "";
    if (value)
        ksnprintf(v, sizeof v, " %08lx", value);
    if (ok)
        kprintf("wifi: %s%s\n", step, v);
    else
        kprintf("\x1b[91mwifi: %s%s\x1b[0m\n", step, v);
}

static int fail(const char *what)
{
    kprintf("\x1b[91mwifi: %s (%s)\x1b[0m\n", what, sdio_error());
    return -1;
}

/* ---------------------------------------------------------------- backplane */

static int set_window(uint32_t addr)
{
    uint32_t win = addr & ~(SB_WINDOW - 1);
    if (win == w.window)
        return 0;
    if (sdio_write8(1, SB_ADDR_LOW, (uint8_t)(win >> 8 & 0x80)) ||
        sdio_write8(1, SB_ADDR_MID, (uint8_t)(win >> 16)) ||
        sdio_write8(1, SB_ADDR_HIGH, (uint8_t)(win >> 24))) {
        w.window = 0xFFFFFFFFu;
        return -1;
    }
    w.window = win;
    return 0;
}

static int bp_read32(uint32_t addr, uint32_t *v)
{
    uint8_t b[4];
    if (set_window(addr) || sdio_rw_block(0, 1, (addr & (SB_WINDOW - 1)) | SB_32BIT, 1, b, 4))
        return -1;
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

static int bp_write32(uint32_t addr, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    if (set_window(addr))
        return -1;
    return sdio_rw_block(1, 1, (addr & (SB_WINDOW - 1)) | SB_32BIT, 1, b, 4);
}

/* Chip memory <-> buf; len a multiple of 4. */
static int bp_mem(int write, uint32_t addr, uint8_t *buf, uint32_t len)
{
    while (len) {
        uint32_t off = addr & (SB_WINDOW - 1);
        uint32_t n = SB_WINDOW - off;
        if (n > len) n = len;
        if (n > SDIO_F1_BLOCK) n = SDIO_F1_BLOCK;
        if (set_window(addr) || sdio_rw_block(write, 1, off | SB_32BIT, 1, buf, n))
            return -1;
        addr += n;
        buf += n;
        len -= n;
    }
    return 0;
}

static int core_disable(uint32_t regs, uint32_t pre, uint32_t ioctl)
{
    uint32_t v;
    if (bp_read32(regs + RESETCTRL, &v))
        return -1;
    if (v & 1) {                                /* already in reset */
        bp_write32(regs + IOCTRL, 3 | ioctl);
        return bp_read32(regs + IOCTRL, &v);
    }
    bp_write32(regs + IOCTRL, 3 | pre);
    bp_read32(regs + IOCTRL, &v);
    bp_write32(regs + RESETCTRL, 1);
    timer_delay_us(10);
    uint32_t t0 = timer_ticks();
    while (bp_read32(regs + RESETCTRL, &v) == 0 && !(v & 1))
        if (timer_ticks() - t0 > 100000)
            return -1;
    bp_write32(regs + IOCTRL, 3 | ioctl);
    return bp_read32(regs + IOCTRL, &v);
}

static int core_reset(uint32_t regs, uint32_t pre, uint32_t ioctl)
{
    uint32_t v;
    if (core_disable(regs, pre, ioctl))
        return -1;
    uint32_t t0 = timer_ticks();
    while (bp_read32(regs + RESETCTRL, &v) == 0 && (v & 1)) {
        bp_write32(regs + RESETCTRL, 0);
        timer_delay_us(40);
        if (timer_ticks() - t0 > 100000)
            return -1;
    }
    bp_write32(regs + IOCTRL, 1 | ioctl);
    return bp_read32(regs + IOCTRL, &v);
}

/* The enumeration ROM: component entries, then address entries (the
 * wrapper ones have bits 6-7 set). */
static int core_scan(uint32_t erom)
{
    static uint8_t buf[512];
    if (bp_mem(0, erom, buf, sizeof buf))
        return -1;
    int core = 0, rev = 0;
    for (unsigned i = 0; i + 8 <= sizeof buf; i += 4) {
        switch (buf[i] & 0xF) {
        case 0xF:                               /* end */
            return 0;
        case 0x1:                               /* component: two words */
            if ((buf[i + 4] & 0xF) != 0x1)
                break;
            core = (buf[i + 1] | buf[i + 2] << 8) & 0xFFF;
            i += 4;
            rev = buf[i + 3];
            break;
        case 0x5: {                             /* address */
            uint32_t addr = (uint32_t)buf[i + 1] << 8 | (uint32_t)buf[i + 2] << 16 |
                            (uint32_t)buf[i + 3] << 24;
            int wrap = (buf[i] & 0xC0) != 0;
            addr &= ~0xFFFu;
            switch (core) {
            case CORE_CHIPCOMMON:
                if (!wrap) w.chipcommon = addr;
                break;
            case CORE_ARM_CM3:
                w.armcore = core;
                if (wrap) { if (!w.armctl) w.armctl = addr; }
                else if (!w.armregs) w.armregs = addr;
                break;
            case CORE_SOCRAM:
                if (wrap) w.socramctl = addr;
                else if (!w.socramregs) w.socramregs = addr;
                w.socramrev = rev;
                break;
            case CORE_SDIOD:
                if (!wrap) w.sdregs = addr;
                w.sdiorev = rev;
                break;
            case CORE_D11:
                if (wrap) w.d11ctl = addr;
                break;
            }
            break;
        }
        }
    }
    return 0;
}

static int ram_scan(void)
{
    uint32_t info, bank;
    if (core_reset(w.socramctl, 0, 0) || bp_read32(w.socramregs + SOCRAM_COREINFO, &info))
        return -1;
    int banks = (int)(info >> 4 & 0xF);
    w.ramsize = 0;
    for (int i = 0; i < banks; i++) {
        bp_write32(w.socramregs + SOCRAM_BANKIDX, (uint32_t)i);
        if (bp_read32(w.socramregs + SOCRAM_BANKINFO, &bank))
            return -1;
        w.ramsize += 8192 * ((bank & 0x3F) + 1);
    }
    /* 43430: bank 3 stays powered (Plan 9, brcmfmac) */
    bp_write32(w.socramregs + SOCRAM_BANKIDX, 3);
    bp_write32(w.socramregs + SOCRAM_BANKPDA, 0);
    return 0;
}

/* ---------------------------------------------------------------- files */

/* What the SD card has in /bm33 (when a firmware file is missing). */
static void list_bm33(void)
{
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, "/bm33") != 0) {
        kprintf("wifi: there is no /bm33 folder on the SD card\n");
        return;
    }
    kprintf("wifi: /bm33 on the SD card has:");
    int n = 0;
    while (fat_readdir(&d, &e) == 1) {
        kprintf("%s %s", n % 3 ? "," : "\n     ", e.name);
        n++;
    }
    kprintf("%s\n", n ? "" : " nothing");
}

static uint8_t *load_file(const char *path, size_t *len)
{
    fat_entry_t e;
    uint8_t *data = NULL;
    if (fat_find(path, &e) != 0 || fat_load(&e, &data, len) != 0)
        return NULL;
    return data;
}

/* The NVRAM text as the firmware wants it: "key=value" strings, each ended
 * by a zero, comments and blank lines dropped, padded to 4 bytes. */
static uint32_t condense_nvram(const uint8_t *in, size_t len, uint8_t *out)
{
    uint32_t n = 0;
    size_t i = 0;
    while (i < len) {
        size_t s = i, e;
        while (i < len && in[i] != '\n') i++;
        e = i++;
        while (s < e && (in[s] == ' ' || in[s] == '\t' || in[s] == '\r')) s++;
        while (e > s && (in[e - 1] == ' ' || in[e - 1] == '\t' || in[e - 1] == '\r')) e--;
        if (s == e || in[s] == '#')
            continue;
        memcpy(out + n, in + s, e - s);
        n += (uint32_t)(e - s);
        out[n++] = 0;
    }
    out[n++] = 0;
    while (n & 3)
        out[n++] = 0;
    return n;
}

/* ---------------------------------------------------------------- frames */

/* Function 2 is a FIFO seen at 0x8000 with the window on chipcommon. */
static int f2_rw(int write, uint8_t *buf, uint32_t len)
{
    if (set_window(ENUM_BASE))
        return -1;
    while (len) {
        uint32_t n = len > SDIO_F2_BLOCK ? SDIO_F2_BLOCK : len;
        if (sdio_rw_block(write, 2, SB_32BIT, 0, buf, n))
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static uint8_t frame[2048];

/* Reads one frame into `frame`; its length, 0 if none, -1 on error. */
static int read_frame(void)
{
    if (f2_rw(0, frame, 12))
        return -1;
    uint32_t len = frame[0] | frame[1] << 8, chk = frame[2] | frame[3] << 8;
    if (len == 0 && chk == 0)
        return 0;
    if ((len ^ 0xFFFF) != chk || len < 12 || len > sizeof frame) {
        /* out of step: stop the frame and drain it (Plan 9) */
        sdio_write8(1, FRAME_CTRL, 1);
        uint8_t c = 1;
        for (int k = 0; k < 100 && sdio_read8(1, RFRM_CNT, &c) == 0 && c; k++)
            ;
        return -1;
    }
    if (len > 12 && f2_rw(0, frame + 12, (len - 12 + 3) & ~3u))
        return -1;
    w.credit = frame[9];
    return (int)len;
}

/* ---------------------------------------------------------------- data */

/* Ethernet frames received on the data channel (2), queued until the
 * network stack (net.c) takes them: frames also arrive while an ioctl or a
 * join waits for its answer, and lwIP must not be entered from there. */
#define RXQ_SLOTS 8
#define RXQ_SIZE  1536
static uint8_t rxq[RXQ_SLOTS][RXQ_SIZE];
static uint16_t rxq_len[RXQ_SLOTS];
static unsigned rxq_head, rxq_tail;
static int joined;
static char joined_ssid[33];

static int parse_event(int n, uint32_t *status, const uint8_t **data, uint32_t *dlen);

/* A frame that is not the answer being waited for: data is queued, a lost
 * link is noted. */
static void dispatch(int n)
{
    unsigned chan = frame[5] & 0xF;
    uint32_t off = frame[7];
    if (chan == 2) {
        if (off + 4 > (uint32_t)n)
            return;
        uint32_t d = off + 4 + frame[off + 3] * 4u;         /* BDC header */
        if (d + 14 > (uint32_t)n || (uint32_t)n - d > RXQ_SIZE)
            return;
        if (rxq_head - rxq_tail >= RXQ_SLOTS)
            return;                             /* full: dropped */
        unsigned k = rxq_head++ % RXQ_SLOTS;
        rxq_len[k] = (uint16_t)(n - d);
        memcpy(rxq[k], frame + d, (uint32_t)n - d);
    } else if (chan == 1 && joined) {
        uint32_t status, dlen;
        const uint8_t *data;
        int type = parse_event(n, &status, &data, &dlen);
        const uint8_t *ev = data - 48;
        if ((type == 16 && !(ev[3] & 1)) || type == 5 || type == 6 || type == 11 || type == 12) {
            joined = 0;
            kprintf("\x1b[91mwifi: link to \"%s\" lost (event %d)\x1b[0m\n", joined_ssid, type);
        }
    }
}

/* Sends an ioctl on the control channel and waits for its answer; out gets
 * up to out_len bytes of the answer's data. */
static int ioctl_(uint32_t cmd, int set, const uint8_t *data, uint32_t len,
                  uint8_t *out, uint32_t out_len, uint32_t timeout_ms)
{
    static uint8_t tx[2048];
    uint32_t total = 12 + 16 + len;
    if (total > sizeof tx)
        return -1;
    memset(tx, 0, 12 + 16);
    tx[0] = (uint8_t)total; tx[1] = (uint8_t)(total >> 8);
    tx[2] = (uint8_t)~total; tx[3] = (uint8_t)(~total >> 8);
    tx[4] = w.txseq++;
    tx[5] = 0;                                  /* channel 0: control */
    tx[7] = 12;                                 /* data offset */
    uint8_t *c = tx + 12;
    uint16_t id = ++w.reqid;
    c[0] = (uint8_t)cmd; c[1] = (uint8_t)(cmd >> 8); c[2] = (uint8_t)(cmd >> 16); c[3] = (uint8_t)(cmd >> 24);
    c[4] = (uint8_t)len; c[5] = (uint8_t)(len >> 8); c[6] = (uint8_t)(len >> 16); c[7] = (uint8_t)(len >> 24);
    c[8] = set ? 2 : 0;
    c[10] = (uint8_t)id; c[11] = (uint8_t)(id >> 8);
    if (len)
        memcpy(tx + 28, data, len);
    if (f2_rw(1, tx, (total + 3) & ~3u))
        return -1;
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < timeout_ms * 1000u) {
        int n = read_frame();
        if (n <= 0) {
            timer_delay_us(200);
            continue;
        }
        if ((frame[5] & 0xF) != 0) {
            dispatch(n);                        /* events and data */
            continue;
        }
        uint32_t off = frame[7];
        if (off + 16 > (uint32_t)n)
            continue;
        const uint8_t *r = frame + off;
        if ((r[10] | r[11] << 8) != id)
            continue;
        uint32_t status = r[12] | r[13] << 8 | (uint32_t)r[14] << 16 | (uint32_t)r[15] << 24;
        if (out) {
            uint32_t have = (uint32_t)n - off - 16;
            memcpy(out, r + 16, have < out_len ? have : out_len);
        }
        return status ? -(int)(status & 0xFFFF) - 2 : 0;
    }
    return -1;                                  /* no answer */
}

static int get_var(const char *name, uint8_t *out, uint32_t out_len)
{
    static uint8_t buf[512];
    uint32_t n = (uint32_t)strlen(name) + 1;
    uint32_t len = out_len > n ? out_len : n;
    if (len > sizeof buf)
        return -1;
    memset(buf, 0, len);
    memcpy(buf, name, n);
    return ioctl_(WLC_GET_VAR, 0, buf, (len + 3) & ~3u, out, out_len, 500);
}

static int set_var(const char *name, const uint8_t *val, uint32_t val_len)
{
    static uint8_t buf[1600];
    uint32_t n = (uint32_t)strlen(name) + 1;
    if (n + val_len > sizeof buf)
        return -1;
    memcpy(buf, name, n);
    memcpy(buf + n, val, val_len);
    return ioctl_(WLC_SET_VAR, 1, buf, (n + val_len + 3) & ~3u, NULL, 0, 1000);
}

static int set_int_var(const char *name, int32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return set_var(name, b, 4);
}

static int ioctl_int(uint32_t cmd, int32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return ioctl_(cmd, 1, b, 4, NULL, 0, 1000);
}

/* The regulatory data (CLM blob), in chunks of 1400 bytes ("clmload"). */
static int clm_load(const uint8_t *blob, size_t len)
{
    static uint8_t chunk[12 + 1400];
    size_t off = 0;
    while (off < len) {
        uint32_t n = len - off > 1400 ? 1400 : (uint32_t)(len - off);
        uint16_t flag = 1u << 12;               /* handler version 1 */
        if (off == 0) flag |= 0x0002;           /* begin */
        if (off + n == len) flag |= 0x0004;     /* end */
        chunk[0] = (uint8_t)flag; chunk[1] = (uint8_t)(flag >> 8);
        chunk[2] = 2; chunk[3] = 0;             /* type: CLM */
        chunk[4] = (uint8_t)n; chunk[5] = (uint8_t)(n >> 8); chunk[6] = 0; chunk[7] = 0;
        memset(chunk + 8, 0, 4);                /* crc: not checked */
        memcpy(chunk + 12, blob + off, n);
        int r = set_var("clmload", chunk, 12 + n);
        if (r)
            return r;
        off += n;
    }
    return 0;
}

/* ---------------------------------------------------------------- bring-up */

static int identify(void)
{
    if (sdio_write8(0, 0x02, 0x02)) {           /* function 1 (backplane) on */
        say("enable function 1 failed", 0, 0);
        return -1;
    }
    uint8_t rdy = 0;
    uint32_t t0 = timer_ticks();
    while (sdio_read8(0, 0x03, &rdy) == 0 && !(rdy & 0x02))
        if (timer_ticks() - t0 > 500000)
            break;
    if (!(rdy & 0x02)) {
        say("function 1 not ready", 0, rdy);
        return -1;
    }
    say("function 1 ready", 1, 0);

    uint8_t csr = 0;
    sdio_write8(1, CHIP_CLK_CSR, CLK_NO_HW_REQ | CLK_REQ_ALP);
    t0 = timer_ticks();
    while (sdio_read8(1, CHIP_CLK_CSR, &csr) == 0 && !(csr & CLK_ALP_AVAIL))
        if (timer_ticks() - t0 > 500000)
            break;
    if (!(csr & CLK_ALP_AVAIL)) {
        say("ALP clock not available, CSR", 0, csr);
        return -1;
    }
    sdio_write8(1, CHIP_CLK_CSR, CLK_NO_HW_REQ | CLK_FORCE_ALP);
    say("backplane clock (ALP) on", 1, 0);

    uint32_t id = 0;
    if (bp_read32(ENUM_BASE, &id))
        return fail("chip id read failed");
    uint32_t chip = id & 0xFFFF, rev = id >> 16 & 0xF;
    kprintf("wifi: chip %lu (%04lx) rev %lu%s\n", chip, chip, rev,
            chip == 43430 ? ": BCM43430/43438, ok" : "");
    return chip == 43430 ? 0 : -1;
}

int wifi_probe(void)
{
    memset(&w, 0, sizeof w);
    w.window = 0xFFFFFFFFu;
    if (strcmp(sd_controller(), "emmc") == 0) {
        kprintf("\x1b[91mwifi: the SD card is on the Arasan controller (SDHOST failed): "
                "no controller left for WiFi\x1b[0m\n");
        return -1;
    }
    kprintf("wifi: 32 kHz clock %s\n", bcm43438_lpo_clock());
    if (sdio_init(say) != 0) {
        kprintf("\x1b[91mwifi: stopped (%s)\x1b[0m\n", sdio_error());
        return -1;
    }
    return identify();
}

int wifi_start(void)
{
    if (wifi_probe() != 0)
        return -1;

    /* the cores */
    uint32_t erom = 0;
    if (bp_read32(ENUM_BASE + 0xFC, &erom) || core_scan(erom))
        return fail("reading the core list failed");
    kprintf("wifi: cores: ARM %05lx/%05lx, RAM %05lx/%05lx, SDIO %05lx (rev %d), D11 %05lx\n",
            w.armregs >> 12, w.armctl >> 12, w.socramregs >> 12, w.socramctl >> 12,
            w.sdregs >> 12, w.sdiorev, w.d11ctl >> 12);
    if (!w.armctl || !w.socramctl || !w.socramregs || !w.sdregs || !w.d11ctl) {
        say("a core is missing", 0, 0);
        return -1;
    }
    if (core_disable(w.armctl, 0, 0) || core_reset(w.d11ctl, 8 | 4, 4) || ram_scan())
        return fail("core reset failed");
    kprintf("wifi: chip RAM %lu KiB\n", w.ramsize / 1024);
    if (w.ramsize < 256 * 1024 || w.ramsize > 1024 * 1024) {
        say("unexpected RAM size", 0, w.ramsize);
        return -1;
    }
    uint8_t csr = 0;
    sdio_write8(1, CHIP_CLK_CSR, 0);
    timer_delay_us(10);
    sdio_write8(1, CHIP_CLK_CSR, CLK_NO_HW_REQ | CLK_REQ_ALP);
    uint32_t t0 = timer_ticks();
    while (sdio_read8(1, CHIP_CLK_CSR, &csr) == 0 && !(csr & (CLK_ALP_AVAIL | CLK_HT_AVAIL)))
        if (timer_ticks() - t0 > 100000)
            break;
    sdio_write8(1, CHIP_CLK_CSR, CLK_NO_HW_REQ | CLK_FORCE_ALP);
    timer_delay_us(65);
    sdio_write8(1, PULLUPS, 0);
    bp_write32(w.chipcommon + 0x58, 0);         /* GPIO pull-ups, pull-downs off */
    bp_write32(w.chipcommon + 0x5C, 0);

    /* firmware and NVRAM from the SD card */
    size_t fw_len = 0, nv_len = 0;
    uint8_t *fw = load_file(FW_FILE, &fw_len);
    if (!fw) {
        kprintf("\x1b[91mwifi: %s not on the SD card (make firmware; make sdcard)\x1b[0m\n", FW_FILE);
        list_bm33();
        return -1;
    }
    uint8_t *nv = load_file(NVRAM_FILE, &nv_len);
    if (!nv) {
        free(fw);
        kprintf("\x1b[91mwifi: %s not on the SD card (make firmware; make sdcard)\x1b[0m\n", NVRAM_FILE);
        list_bm33();
        return -1;
    }
    uint8_t *nvram = malloc(nv_len + 8);
    uint32_t nvram_len = nvram ? condense_nvram(nv, nv_len, nvram) : 0;
    free(nv);
    uint32_t fw_pad = (uint32_t)(fw_len + 3) & ~3u;
    if (!nvram || fw_pad + nvram_len + 4 > w.ramsize) {
        free(fw); free(nvram);
        say("firmware too big for the chip RAM", 0, (uint32_t)fw_len);
        return -1;
    }
    uint8_t *fwp = realloc(fw, fw_pad);
    if (!fwp) { free(fw); free(nvram); return -1; }
    fw = fwp;
    memset(fw + fw_len, 0, fw_pad - fw_len);

    uint8_t zero[4] = { 0, 0, 0, 0 };
    bp_mem(1, w.ramsize - 4, zero, 4);
    t0 = timer_ticks();
    int err = bp_mem(1, 0, fw, fw_pad);
    uint32_t ms = (timer_ticks() - t0) / 1000;
    if (!err) {                                 /* read back the start and the end */
        static uint8_t chk[512];
        err = bp_mem(0, 0, chk, 512) || memcmp(chk, fw, 512) ||
              bp_mem(0, fw_pad - 512, chk, 512) || memcmp(chk, fw + fw_pad - 512, 512);
    }
    free(fw);
    if (err) {
        free(nvram);
        return fail("firmware upload failed or reads back wrong");
    }
    kprintf("wifi: firmware %lu KiB written and checked in %lu ms\n", (uint32_t)fw_len / 1024, ms);
    uint32_t nv_at = w.ramsize - 4 - nvram_len;
    err = bp_mem(1, nv_at, nvram, nvram_len);
    free(nvram);
    uint32_t words = nvram_len / 4, token = (~words << 16) | (words & 0xFFFF);
    uint8_t tb[4] = { (uint8_t)token, (uint8_t)(token >> 8), (uint8_t)(token >> 16), (uint8_t)(token >> 24) };
    if (err || bp_mem(1, w.ramsize - 4, tb, 4))
        return fail("NVRAM upload failed");
    kprintf("wifi: board settings (NVRAM) %lu bytes\n", nvram_len);

    /* start the chip's ARM, then the fast clock and function 2 */
    if (core_reset(w.armctl, 0, 0))
        return fail("ARM start failed");
    say("chip ARM started", 1, 0);
    sdio_write8(1, CHIP_CLK_CSR, 0);
    timer_delay_ms(1);
    sdio_write8(1, CHIP_CLK_CSR, CLK_REQ_HT);
    t0 = timer_ticks();
    csr = 0;
    while (sdio_read8(1, CHIP_CLK_CSR, &csr) == 0 && !(csr & CLK_HT_AVAIL))
        if (timer_ticks() - t0 > 3000000)
            break;
    if (!(csr & CLK_HT_AVAIL)) {
        say("fast clock (HT) not available: the firmware did not start, CSR", 0, csr);
        return -1;
    }
    sdio_write8(1, CHIP_CLK_CSR, csr | CLK_FORCE_HT);
    say("fast clock (HT) on", 1, 0);
    bp_write32(w.sdregs + SD_INTSTATUS, 0xFFFFFFFFu);
    bp_write32(w.sdregs + SD_TOSBMAILBOXDATA, 4u << 16);   /* SDPCM protocol version 4 (brcmfmac) */
    sdio_write8(0, 0x02, 0x06);                 /* functions 1 and 2 */
    uint8_t rdy = 0;
    t0 = timer_ticks();
    while (sdio_read8(0, 0x03, &rdy) == 0 && !(rdy & 0x04))
        if (timer_ticks() - t0 > 1000000)
            break;
    if (!(rdy & 0x04)) {
        say("function 2 not ready", 0, rdy);
        return -1;
    }
    bp_write32(w.sdregs + SD_INTMASK, INT_FRAME | INT_MAILBOX | INT_FC_CHANGE);
    sdio_write8(1, WATERMARK, 8);
    say("function 2 ready", 1, 0);

    /* the control channel: firmware version, MAC address */
    int r = -1;
    for (int tries = 0; tries < 10 && r != 0; tries++) {
        memset(w.version, 0, sizeof w.version);
        r = get_var("ver", (uint8_t *)w.version, sizeof w.version - 1);
        if (r)
            timer_delay_ms(100);
    }
    if (r) {
        say("no answer from the firmware (ioctl \"ver\")", 0, (uint32_t)-r);
        return -1;
    }
    for (char *p = w.version; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';
    kprintf("wifi: firmware: %s\n", w.version);
    if (get_var("cur_etheraddr", w.mac, 6) == 0)
        kprintf("wifi: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                w.mac[0], w.mac[1], w.mac[2], w.mac[3], w.mac[4], w.mac[5]);
    size_t clm_len = 0;
    uint8_t *clm = load_file(CLM_FILE, &clm_len);
    if (clm) {
        r = clm_load(clm, clm_len);
        free(clm);
        if (r)
            say("regulatory data (CLM) refused", 0, (uint32_t)-r);
        else
            kprintf("wifi: regulatory data (CLM) %lu bytes loaded\n", (uint32_t)clm_len);
    } else {
        kprintf("wifi: no %s (optional with older firmware)\n", CLM_FILE);
    }
    /* radio on, no power saving (the chip answers at once) */
    set_int_var("bus:txglom", 0);
    set_int_var("bus:rxglom", 0);               /* one frame per transfer */
    set_int_var("mpc", 0);
    ioctl_int(WLC_SET_PM, 0);
    uint8_t mask[16];
    memset(mask, 0, sizeof mask);
    static const int events[] = { 0, 3, 5, 6, 11, 12, 16, 46, 69 };
    for (unsigned i = 0; i < sizeof events / sizeof *events; i++)
        mask[events[i] / 8] |= (uint8_t)(1u << (events[i] % 8));
    set_var("event_msgs", mask, sizeof mask);
    r = ioctl_int(WLC_UP, 0);
    if (r) {
        say("radio did not come up (WLC_UP)", 0, (uint32_t)-r);
        return -1;
    }
    w.up = 1;
    kprintf("wifi: radio up\n");
    return 0;
}

/* ---------------------------------------------------------------- scan */

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int rssi, channel;
    const char *security;
} net_t;

#define MAX_NETS 32
static net_t nets[MAX_NETS];
static int nnets;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* WPA2 / WPA / WEP / open, from the capability bits and the IEs */
static const char *security_of(const uint8_t *bss, uint32_t len)
{
    uint16_t cap = le16(bss + 16);
    uint32_t ie_off = le16(bss + 116), ie_len = le32(bss + 120);
    int wpa = 0, rsn = 0;
    if (ie_off + ie_len <= len) {
        const uint8_t *ie = bss + ie_off, *end = ie + ie_len;
        while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
            if (ie[0] == 48)
                rsn = 1;
            else if (ie[0] == 221 && ie[1] >= 4 && ie[2] == 0x00 && ie[3] == 0x50 &&
                     ie[4] == 0xF2 && ie[5] == 0x01)
                wpa = 1;
            ie += 2 + ie[1];
        }
    }
    return rsn ? "WPA2" : wpa ? "WPA" : (cap & 0x10) ? "WEP" : "open";
}

static void add_bss(const uint8_t *bss, uint32_t len)
{
    if (len < 128)
        return;
    net_t n;
    memset(&n, 0, sizeof n);
    uint8_t sl = bss[18] > 32 ? 32 : bss[18];
    memcpy(n.ssid, bss + 19, sl);
    for (int i = 0; i < sl; i++)
        if ((uint8_t)n.ssid[i] < 32 || (uint8_t)n.ssid[i] > 126) n.ssid[i] = '?';
    memcpy(n.bssid, bss + 8, 6);
    n.rssi = (int16_t)le16(bss + 78);
    n.channel = bss[88] ? bss[88] : (le16(bss + 72) & 0xFF);
    n.security = security_of(bss, len);
    for (int i = 0; i < nnets; i++)
        if (memcmp(nets[i].bssid, n.bssid, 6) == 0) {
            if (n.rssi > nets[i].rssi) nets[i].rssi = n.rssi;
            return;
        }
    if (nnets < MAX_NETS)
        nets[nnets++] = n;
}

/* An event frame (channel 1): BDC header, Ethernet header, Broadcom
 * event header (big endian), then the data. Returns the event type and
 * status, the data in *data / *dlen; -1 if it is not an event. */
static int parse_event(int n, uint32_t *status, const uint8_t **data, uint32_t *dlen)
{
    uint32_t off = frame[7];
    if (off + 4 > (uint32_t)n)
        return -1;
    const uint8_t *p = frame + off;
    const uint8_t *eth = p + 4 + p[3] * 4u;
    if (eth + 14 + 10 + 48 > frame + n || eth[12] != 0x88 || eth[13] != 0x6C)
        return -1;
    const uint8_t *ev = eth + 14 + 10;
    *status = be32(ev + 8);
    *dlen = be32(ev + 20);                     /* version, flags, type, status, reason, auth, datalen */
    *data = ev + 48;
    if (*data + *dlen > frame + n)
        *dlen = (uint32_t)(frame + n - *data);
    return (int)be32(ev + 4);
}

static int cmp_rssi(const void *a, const void *b)
{
    return ((const net_t *)b)->rssi - ((const net_t *)a)->rssi;
}

int wifi_scan(void)
{
    if (!w.up) {
        kprintf("wifi: not started (W first)\n");
        return -1;
    }
    /* escan: version 1, action start, sync id, then the scan parameters:
     * any SSID, broadcast BSSID, any BSS type, active, defaults */
    static uint8_t p[8 + 64];
    memset(p, 0, sizeof p);
    p[0] = 1;                                   /* version */
    p[4] = 1;                                   /* action: start */
    p[6] = 0x34; p[7] = 0x12;                   /* sync id */
    uint8_t *sp = p + 8;
    memset(sp + 36, 0xFF, 6);                   /* BSSID: any */
    sp[42] = 2;                                 /* BSS type: any */
    sp[43] = 0;                                 /* active scan */
    for (int i = 0; i < 4; i++)                 /* probes, times: firmware defaults */
        memset(sp + 44 + i * 4, 0xFF, 4);
    nnets = 0;
    int r = set_var("escan", p, sizeof p);
    if (r) {
        say("scan refused", 0, (uint32_t)-r);
        return -1;
    }
    kprintf("wifi: scanning...\n");
    uint32_t t0 = timer_ticks();
    int done = 0;
    while (!done && timer_ticks() - t0 < 8000000u) {
        int n = read_frame();
        if (n <= 0) {
            timer_delay_us(500);
            continue;
        }
        if ((frame[5] & 0xF) != 1) {
            dispatch(n);
            continue;
        }
        uint32_t status, dlen;
        const uint8_t *d;
        int type = parse_event(n, &status, &d, &dlen);
        if (type != 69)                         /* E_ESCAN_RESULT */
            continue;
        if (status == 8 && dlen >= 12 + 128) {  /* partial: one or more BSS */
            uint16_t count = le16(d + 10);
            const uint8_t *bss = d + 12, *end = d + dlen;
            for (int i = 0; i < count && bss + 8 <= end; i++) {
                uint32_t len = le32(bss + 4);
                if (len < 128 || bss + len > end)
                    break;
                add_bss(bss, len);
                bss += len;
            }
        } else if (status != 8) {
            done = 1;                           /* 0: finished; others: aborted */
            if (status != 0)
                kprintf("wifi: scan ended with status %lu\n", status);
        }
    }
    qsort(nets, (size_t)nnets, sizeof *nets, cmp_rssi);
    kprintf("wifi: %d network%s%s\n", nnets, nnets == 1 ? "" : "s", done ? "" : " (scan timed out)");
    for (int i = 0; i < nnets; i++)
        kprintf("  %2d  %4d dBm  ch %2d  %-4s  %s\n", i + 1, nets[i].rssi, nets[i].channel,
                nets[i].security, nets[i].ssid[0] ? nets[i].ssid : "(hidden)");
    return nnets;
}

/* ---------------------------------------------------------------- join */

/* Joins `ssid`: open, or WPA/WPA2 with `psk` (8-63 characters); the
 * firmware does the 4-way handshake ("sup_wpa"). Waits for the events. */
static int join(const char *ssid, const char *psk, const char *security)
{
    int open_net = strcmp(security, "open") == 0;
    int wpa2 = strcmp(security, "WPA2") == 0;
    if (strcmp(security, "WEP") == 0) {
        kprintf("\x1b[91mwifi: WEP networks are not supported\x1b[0m\n");
        return -1;
    }
    size_t pl = psk ? strlen(psk) : 0;
    if (!open_net && (pl < 8 || pl > 63)) {
        kprintf("\x1b[91mwifi: the password must be 8 to 63 characters\x1b[0m\n");
        return -1;
    }
    joined = 0;
    ioctl_int(WLC_SET_INFRA, 1);
    ioctl_int(WLC_SET_AUTH, 0);                 /* open system; WPA on top */
    set_int_var("wsec", open_net ? 0 : wpa2 ? 4 : 6);          /* AES, or TKIP+AES */
    set_int_var("wpa_auth", open_net ? 0 : wpa2 ? 0x80 : 0x04); /* WPA2-PSK / WPA-PSK */
    set_int_var("sup_wpa", open_net ? 0 : 1);
    if (!open_net) {
        static uint8_t pmk[2 + 2 + 129];
        memset(pmk, 0, sizeof pmk);
        pmk[0] = (uint8_t)pl;
        pmk[2] = 1;                             /* a passphrase, not a key */
        memcpy(pmk + 4, psk, pl);
        timer_delay_ms(2);                      /* sup_wpa takes a moment (brcmfmac) */
        int r = ioctl_(WLC_SET_WSEC_PMK, 1, pmk, (sizeof pmk + 3) & ~3u, NULL, 0, 1000);
        if (r) {
            say("password refused by the firmware (WSEC_PMK)", 0, (uint32_t)-r);
            return -1;
        }
    }
    uint8_t sl = (uint8_t)strlen(ssid);
    if (sl > 32) sl = 32;
    uint8_t sb[36];
    memset(sb, 0, sizeof sb);
    sb[0] = sl;
    memcpy(sb + 4, ssid, sl);
    kprintf("wifi: joining \"%s\" (%s)...\n", ssid, security);
    int r = ioctl_(WLC_SET_SSID, 1, sb, sizeof sb, NULL, 0, 1000);
    if (r) {
        say("join refused (SET_SSID)", 0, (uint32_t)-r);
        return -1;
    }
    /* events: SET_SSID (0) says associated or not, PSK_SUP (46) status 6
     * says the keys are in, LINK (16) flag 1 the link; DEAUTH / DISASSOC
     * (5, 6, 11, 12) or a bad status: failed */
    int assoc = 0, keyed = open_net, link = 0;
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < 15000000u) {
        int n = read_frame();
        if (n <= 0) {
            timer_delay_us(500);
            continue;
        }
        if ((frame[5] & 0xF) != 1)
            continue;
        uint32_t status, dlen;
        const uint8_t *d;
        int type = parse_event(n, &status, &d, &dlen);
        if (type < 0)
            continue;
        uint32_t off = frame[7];
        const uint8_t *ev = frame + off + 4 + frame[off + 3] * 4u + 14 + 10;
        uint16_t flags = (uint16_t)(ev[2] << 8 | ev[3]);
        switch (type) {
        case 0:                                 /* SET_SSID */
            if (status != 0) {
                kprintf("\x1b[91mwifi: could not join (status %lu%s)\x1b[0m\n", status,
                        status == 3 ? ": network not found" : status == 1 ? ": failed" : "");
                return -1;
            }
            assoc = 1;
            kprintf("wifi: associated\n");
            break;
        case 46:                                /* PSK_SUP */
            if (status == 6) {
                keyed = 1;
                kprintf("wifi: password accepted, keys set\n");
            } else if (status == 7) {           /* WLC_SUP_TIMEOUT; others are progress */
                kprintf("\x1b[91mwifi: the password was not accepted (status %lu)\x1b[0m\n", status);
                return -1;
            }
            break;
        case 16:                                /* LINK */
            link = flags & 1;
            if (!link && assoc) {
                kprintf("\x1b[91mwifi: link lost (wrong password?)\x1b[0m\n");
                return -1;
            }
            break;
        case 5: case 6: case 11: case 12:       /* deauth / disassoc */
            kprintf("\x1b[91mwifi: the access point sent us away (event %d, wrong password?)\x1b[0m\n",
                    type);
            return -1;
        }
        if (assoc && keyed && link) {
            joined = 1;
            snprintf(joined_ssid, sizeof joined_ssid, "%s", ssid);
            kprintf("\x1b[92mwifi: connected to \"%s\"\x1b[0m \n", ssid);
            return 0;
        }
    }
    kprintf("\x1b[91mwifi: no answer from the network in 15 s\x1b[0m\n");
    return -1;
}

/* A line typed on the keyboard or the serial port; Esc cancels. */
static int read_line(char *buf, int max, int secret)
{
    int n = 0;
    for (;;) {
        char c = input_getc();
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            kprintf("\n");
            return n;
        }
        if (c == 0x1B) {
            kprintf("  (cancelled)\n");
            return -1;
        }
        if ((c == 0x7F || c == 0x08) && n > 0) {
            n--;
            kprintf("\b \b");
            continue;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127 && n < max - 1) {
            buf[n++] = c;
            kprintf("%c", secret ? '*' : c);
        }
    }
}

int wifi_connect(void)
{
    if (!w.up || nnets <= 0)
        return -1;
    /* the saved network, if it is in range */
    const char *ssid = config_get("wifi_ssid"), *psk = config_get("wifi_psk");
    if (ssid && ssid[0])
        for (int i = 0; i < nnets; i++)
            if (strcmp(nets[i].ssid, ssid) == 0) {
                kprintf("wifi: saved network \"%s\" is in range\n", ssid);
                return join(ssid, psk, nets[i].security);
            }
    kprintf("wifi: number of the network to join (Enter or Esc: none): ");
    char line[72];
    if (read_line(line, 4, 0) <= 0)
        return -1;
    int k = atoi(line);
    if (k < 1 || k > nnets || !nets[k - 1].ssid[0]) {
        kprintf("wifi: no network %s\n", line);
        return -1;
    }
    const net_t *nt = &nets[k - 1];
    line[0] = 0;
    if (strcmp(nt->security, "open") != 0) {
        kprintf("wifi: password for \"%s\": ", nt->ssid);
        if (read_line(line, 64, 1) < 0)
            return -1;
    }
    if (join(nt->ssid, line, nt->security) != 0)
        return -1;
    /* remembered for next time (plain text on the SD card) */
    config_set("wifi_ssid", nt->ssid);
    config_set("wifi_psk", line);
    config_save();
    kprintf("wifi: saved in bm33/config.txt (W reconnects by itself)\n");
    return 0;
}

/* ---------------------------------------------------------------- data API */

int wifi_linked(void)
{
    return w.up && joined;
}

const unsigned char *wifi_mac(void)
{
    return w.mac;
}

void wifi_poll(void)
{
    if (!w.up)
        return;
    for (int k = 0; k < 8; k++) {
        int n = read_frame();
        if (n <= 0)
            return;
        dispatch(n);
    }
}

int wifi_recv(void *buf, int max)
{
    if (rxq_tail == rxq_head)
        return 0;
    unsigned k = rxq_tail++ % RXQ_SLOTS;
    int n = rxq_len[k] < max ? rxq_len[k] : max;
    memcpy(buf, rxq[k], (size_t)n);
    return n;
}

int wifi_send(const void *eth, int len)
{
    static uint8_t tx[12 + 4 + RXQ_SIZE + 4];
    if (!wifi_linked() || len < 14 || len > RXQ_SIZE)
        return -1;
    /* flow control: the firmware grants sequence numbers up to `credit`
     * (brcmfmac data_ok); a frame read refreshes it */
    uint32_t t0 = timer_ticks();
    while ((uint8_t)(w.credit - w.txseq) == 0 || ((uint8_t)(w.credit - w.txseq) & 0x80)) {
        if (timer_ticks() - t0 > 20000u)
            return -1;
        int n = read_frame();
        if (n > 0)
            dispatch(n);
        else
            timer_delay_us(100);
    }
    uint32_t total = 12 + 4 + (uint32_t)len;
    memset(tx, 0, 16);
    tx[0] = (uint8_t)total; tx[1] = (uint8_t)(total >> 8);
    tx[2] = (uint8_t)~total; tx[3] = (uint8_t)(~total >> 8);
    tx[4] = w.txseq++;
    tx[5] = 2;                                  /* channel 2: data */
    tx[7] = 12;                                 /* data offset */
    tx[12] = 0x20;                              /* BDC version 2, priority 0 */
    memcpy(tx + 16, eth, (size_t)len);
    return f2_rw(1, tx, (total + 3) & ~3u);
}
