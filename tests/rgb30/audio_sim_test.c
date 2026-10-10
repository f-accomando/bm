/*
 * The RGB30's sound driver (src/rgb30/rk_audio.c) on a simulated chip:
 * the CRU, the GRFs, the I2S1 controller with its FIFO draining at the
 * frame rate its clocks give, and the RK817's codec registers on I2C.
 * Checked against Linux's values (rockchip_i2s_tdm.c, rk817_codec.c,
 * rk8xx-core.c, clk-rk3568.c): the 12.288 MHz MCLK from the GPLL, 48 kHz
 * frames, the pins and the I/O voltage, the codec's headphone path powered
 * in DAPM's order, every frame of audio_render32() out once and in order
 * (16 bits: left in the low half of the word, right in the high; 24: a
 * word a sample in its low 24 bits; 32: a whole word a sample, the codec
 * set to 24), no underrun, the width changed while it plays (sound_depth
 * from Settings: audio_out_idle()), and the power down.
 *
 *   build/rgb30-host/audio_sim_test [GPLL MHz] [bits at the start] [FIFO words]
 *                                   (1188 or 1200; 16, 24 or 32; 32)
 */
#include "rk_audio.h"
#include "audio/audio.h"
#include "audio/audio_out.h"
#include "kernel/irq.h"
#include "lib/printf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define CRU     0xfdd20000u
#define GRF     0xfdc60000u
#define PMU_GRF 0xfdc20000u
#define I2S1    0xfe410000u

/* ---- the chip -------------------------------------------------------- */

static uint32_t cru[0x400], grf[0x400], pmugrf[0x100], i2s[0x10];
static int seen_grf504, seen_grf508;
static int txcr_while_running;          /* the format changed with the transfer on */

static unsigned DEPTH = 32;            /* the TX FIFO's words (a third argument: another) */
static uint32_t fifo[64];
static unsigned fifo_n, overflows;
static uint32_t intsr;
static uint64_t now_ns, next_frame_ns;

static uint32_t out_words[48000 * 4];
static unsigned n_out, underruns;

/* FIFO words a frame: 1 at 16 bits with HWT 0 (L | R), else a word a sample */
static unsigned words_per_frame(void)
{
    return (i2s[0] & 0x1f) == 15 && !(i2s[0] >> 14 & 1) ? 1 : 2;
}

/* the frame rate the configured clocks give, as the chip would */
static double frame_hz(void)
{
    uint32_t c0 = cru[0x40 / 4], c1 = cru[0x44 / 4];
    double gpll = 24e6 / (c1 & 0x3f) * (c0 & 0xfff) / (c0 >> 12 & 7) / (c1 >> 6 & 7);
    uint32_t sel15 = cru[0x13c / 4], sel16 = cru[0x140 / 4];
    if ((sel15 >> 8 & 3) != 0 || (sel15 >> 10 & 3) != 1)
        return 0;                       /* not GPLL through the fraction */
    double mclk = gpll / ((sel15 & 0x7f) + 1) * (sel16 >> 16) / (sel16 & 0xffff);
    double bclk = mclk / ((i2s[0x38 / 4] & 0xff) + 1);
    return bclk / ((i2s[0x08 / 4] & 0xff) + 1);
}

static irq_fn handler;
static int irq_on;

static void run_hardware(void)
{
    static int busy;                    /* the handler's own register accesses */
    if (busy)
        return;
    if (!(i2s[0x1c / 4] & 1)) {
        next_frame_ns = now_ns;
        return;
    }
    double hz = frame_hz();
    if (hz <= 0)
        return;
    uint64_t period = (uint64_t)(1e9 / hz);
    busy = 1;
    while (next_frame_ns <= now_ns) {
        unsigned per = words_per_frame();
        if (fifo_n >= per) {
            for (unsigned k = 0; k < per; k++) {
                if (n_out < sizeof out_words / sizeof out_words[0])
                    out_words[n_out++] = fifo[0];
                memmove(fifo, fifo + 1, --fifo_n * sizeof fifo[0]);
            }
        } else {
            underruns++;
            intsr |= 2;
        }
        next_frame_ns += period;
        uint32_t intcr = i2s[0x14 / 4];
        unsigned tft = (intcr >> 4 & 0x1f) + 1;
        if (fifo_n <= tft && (intcr & 1))
            intsr |= 1;
        else
            intsr &= ~1u;
        if ((intsr & 1) && irq_on && handler) {
            handler(0);
            if (!(fifo_n <= tft))
                intsr &= ~1u;
        }
    }
    busy = 0;
}

static void tick(uint64_t ns)
{
    now_ns += ns;
    run_hardware();
}

static void hiword(uint32_t *reg, uint32_t v)
{
    uint32_t mask = v >> 16;
    *reg = (*reg & ~mask) | (v & mask);
}

uint32_t readl(uintptr_t a)
{
    tick(50);
    if (a >= I2S1 && a < I2S1 + 0x40) {
        switch (a - I2S1) {
        case 0x0c: return fifo_n;
        case 0x18: return intsr;
        case 0x20: return 0;            /* the clear is done at once */
        default:   return i2s[(a - I2S1) / 4];
        }
    }
    if (a >= CRU && a < CRU + 0x1000)
        return cru[(a - CRU) / 4];
    if (a >= GRF && a < GRF + 0x1000)
        return grf[(a - GRF) / 4];
    if (a >= PMU_GRF && a < PMU_GRF + 0x400)
        return pmugrf[(a - PMU_GRF) / 4];
    printf("FAIL: read of %08lx\n", (unsigned long)a);
    fails++;
    return 0;
}

void writel(uintptr_t a, uint32_t v)
{
    tick(50);
    if (a >= I2S1 && a < I2S1 + 0x40) {
        uint32_t off = (uint32_t)(a - I2S1);
        if (off == 0x24) {
            if (fifo_n < DEPTH)
                fifo[fifo_n++] = v;
            else
                overflows++;
        } else if (off == 0x14 && (v & 4)) {
            intsr &= ~2u;               /* TXUIC */
            i2s[off / 4] = v & ~4u;
        } else if (off == 0x20) {
            if (v & 1)
                fifo_n = 0;             /* TXC: the TX logic and its FIFO cleared */
        } else {
            if (off == 0 && (i2s[0x1c / 4] & 1))
                txcr_while_running++;
            i2s[off / 4] = v;
        }
        return;
    }
    if (a >= CRU && a < CRU + 0x1000) {
        uint32_t off = (uint32_t)(a - CRU);
        if (off == 0x140)
            cru[off / 4] = v;           /* the fraction: a whole word */
        else
            hiword(&cru[off / 4], v);
        return;
    }
    if (a >= GRF && a < GRF + 0x1000) {
        if (a - GRF == 0x504) seen_grf504 = v;
        if (a - GRF == 0x508) seen_grf508 = v;
        hiword(&grf[(a - GRF) / 4], v);
        return;
    }
    if (a >= PMU_GRF && a < PMU_GRF + 0x400) {
        hiword(&pmugrf[(a - PMU_GRF) / 4], v);
        return;
    }
    printf("FAIL: write of %08lx\n", (unsigned long)a);
    fails++;
}

/* the RK817: its registers, and the order of the codec's writes */
static uint8_t pmic[256];
static struct { uint8_t reg, val; } wlog[512];
static unsigned n_wlog;

int rk817_present(void) { return 1; }
int rk817_read(uint8_t reg) { tick(100000); return pmic[reg]; }
int rk817_write(uint8_t reg, uint8_t val)
{
    tick(100000);                       /* 100 us an I2C transfer */
    pmic[reg] = val;
    if (n_wlog < sizeof wlog / sizeof wlog[0]) {
        wlog[n_wlog].reg = reg;
        wlog[n_wlog].val = val;
        n_wlog++;
    }
    return 0;
}

/* the last write of reg before index `before` (-1: none) */
static int written(uint8_t reg, unsigned before)
{
    int v = -1;
    for (unsigned i = 0; i < before && i < n_wlog; i++)
        if (wlog[i].reg == reg)
            v = wlog[i].val;
    return v;
}

static unsigned first_write(uint8_t reg, uint8_t mask, uint8_t want)
{
    for (unsigned i = 0; i < n_wlog; i++)
        if (wlog[i].reg == reg && (wlog[i].val & mask) == want)
            return i;
    return ~0u;
}

static unsigned muxed[8], n_muxed, pulled;
void rk_pin_mux(unsigned pin, unsigned func)
{
    CHECK(func == 1);
    if (n_muxed < 8)
        muxed[n_muxed++] = pin;
}
void rk_pin_pull(unsigned pin, unsigned pull) { (void)pin; pulled += pull == 0; }

/* ---- the rest of the kernel ------------------------------------------- */

uint32_t timer_ticks(void) { tick(1000); return (uint32_t)(now_ns / 1000); }
void timer_delay_us(uint32_t us) { tick((uint64_t)us * 1000); }
void irq_register(unsigned irq, irq_fn fn, void *arg) { CHECK(irq == 32 + 53); (void)arg; handler = fn; }
void irq_enable(unsigned irq) { CHECK(irq == 32 + 53); irq_on = 1; }
void irq_disable(unsigned irq) { (void)irq; irq_on = 0; }
void uart_putc(char c) { putchar(c); }

/* audio.c: a ramp on the left, the same plus an offset on the right, so
 * every sample is known and the channels told apart; at 32 bits the low
 * byte is not 0, to see it go through. sound_depth is sim_depth. */
#define STEP32  0x101u
#define LOW32   0x5au
static uint32_t next_frame;
static unsigned rendered, render_bits, sim_depth = 16;
static uint64_t render_ns;              /* the time a chunk takes to make (the FIFO drains meanwhile) */
unsigned audio_depth(void) { return sim_depth; }

/* from just under the middle: the sign changes after 256 frames */
static uint32_t left_of(uint32_t f, unsigned bits)
{
    return bits == 16 ? (f + 0x7f00) & 0xffff : bits == 24 ? (f + 0x7fff00) & 0xffffff : f * STEP32 + LOW32;
}

static uint32_t next_left(uint32_t l, unsigned bits)
{
    return bits == 16 ? (l + 1) & 0xffff : bits == 24 ? (l + 1) & 0xffffff : l + STEP32;
}

static uint32_t right_of(uint32_t left, unsigned bits)
{
    return bits == 16 ? (left + 0x4000) & 0xffff : bits == 24 ? (left + 0x4000) & 0xffffff : left + 0x40000000u;
}

void audio_render32(int32_t *out, unsigned n, unsigned bits)
{
    unsigned sh = 32 - bits;
    for (unsigned i = 0; i < n; i++) {
        uint32_t l = left_of(next_frame, bits);
        out[2 * i] = (int32_t)(l << sh);
        out[2 * i + 1] = (int32_t)(right_of(l, bits) << sh);
        next_frame++;
    }
    rendered += n;
    render_bits = bits;
    tick(render_ns);
}

/* ---- the test --------------------------------------------------------- */

static uint8_t codec_vdw(unsigned bits)
{
    return bits == 16 ? 0x0f : 0x17;    /* rk817_codec.c: 16 bits, or 24 for S24_LE and S32_LE */
}

/* The words out since `from` as frames of `bits`: each the one before + 1
 * (past the silence the FIFO was measured with), right = left + offset,
 * at 24 bits the top byte the sign of the low 24 */
static void check_stream(unsigned from, unsigned bits)
{
    unsigned per = bits == 16 ? 1 : 2;
    int ordered = 1, stereo = 1, extended = 1, have = 0, frames = 0;
    uint32_t prev = 0;
    for (unsigned i = from; i + per <= n_out; i += per) {
        uint32_t l, r;
        if (bits == 16) {
            l = out_words[i] & 0xffff;
            r = out_words[i] >> 16;
        } else {
            l = out_words[i];
            r = out_words[i + 1];
        }
        if (bits == 24) {
            if ((uint32_t)((int32_t)(l << 8) >> 8) != l || (uint32_t)((int32_t)(r << 8) >> 8) != r)
                extended = 0;
            l &= 0xffffff;
            r &= 0xffffff;
        }
        if (!l && !r) {
            have = 0;
            continue;
        }
        if (r != right_of(l, bits))
            stereo = 0;
        if (have && l != next_left(prev, bits))
            ordered = 0;
        prev = l;
        have = 1;
        frames++;
    }
    CHECK(ordered);
    CHECK(stereo);
    CHECK(extended);
    CHECK(frames > 1000);
}

/* ms of sound at `bits`: every frame once (48 kHz), in order, no underrun */
static void play(unsigned bits, int ms, const char *what)
{
    n_out = 0;
    unsigned under0 = underruns, over0 = overflows;
    uint64_t t0 = now_ns;
    for (int i = 0; i < ms; i++)
        tick(1000000);
    unsigned per = bits == 16 ? 1 : 2, got = n_out / per;
    double secs = (double)(now_ns - t0) / 1e9;     /* a little more: the handler's accesses take time too */
    printf("audio_sim: %s, %u-bit: %u frames in %.4f s, %u underruns, %u overflows, %u rendered\n",
           what, bits, got, secs, underruns - under0, overflows, rendered);
    CHECK(got > 48000 * secs - 2 && got < 48000 * secs + 2);
    CHECK(underruns == under0 && overflows == over0);
    CHECK(render_bits == bits);
    CHECK(n_out % per == 0);
    check_stream(0, bits);
}

int main(int argc, char **argv)
{
    unsigned mhz = argc > 1 ? (unsigned)atoi(argv[1]) : 1188;
    unsigned bits0 = argc > 2 ? (unsigned)atoi(argv[2]) : 16;
    if (bits0 != 24 && bits0 != 32)
        bits0 = 16;
    sim_depth = bits0;
    if (argc > 3 && atoi(argv[3]) >= 24 && atoi(argv[3]) <= 63)
        DEPTH = (unsigned)atoi(argv[3]);    /* an odd one: whole frames all the same */
    /* the GPLL as U-Boot leaves it: 24 MHz / 2 * fbdiv, normal mode */
    cru[0x40 / 4] = 1u << 12 | (mhz / 12);
    cru[0x44 / 4] = 1u << 12 | 1u << 6 | 2;
    cru[0xc0 / 4] = 1u << 6;
    cru[0x13c / 4] = 0x0000;            /* CLKSEL15 as after reset: something else */
    i2s[0] = 0x7200000f;
    i2s[0x08 / 4] = 0x00001f1f;
    i2s[0x38 / 4] = 0x0707;
    memset(pmic, 0x5a, sizeof pmic);    /* the codec as nobody knows it */

    const char *status = NULL;
    int r = audio_out_start(&status);
    printf("audio_sim: GPLL %u MHz: %s (%s)\n", mhz, r == 0 ? "started" : "failed", status);
    CHECK(r == 0);

    /* clocks: GPLL / 1 through the fraction, MCLK out from TX, gates open */
    uint32_t sel15 = cru[0x13c / 4], sel16 = cru[0x140 / 4];
    double mclk = mhz * 1e6 * (sel16 >> 16) / (sel16 & 0xffff);
    printf("audio_sim: MCLK %.1f Hz (%u/%u), %.3f frames/s\n", mclk, sel16 >> 16, sel16 & 0xffff, frame_hz());
    CHECK((sel15 & 0x7f) == 0 && (sel15 >> 8 & 3) == 0 && (sel15 >> 10 & 3) == 1 && !(sel15 >> 15 & 1));
    CHECK(mclk > 12288000 * 0.9999 && mclk < 12288000 * 1.0001);
    CHECK(frame_hz() > 47995 && frame_hz() < 48005);
    CHECK((cru[0x318 / 4] & 0xf00) == 0);           /* GATE6: src, frac, mclk_tx, mclkout_tx */
    CHECK((cru[0x314 / 4] & (1u << 11)) == 0);      /* GATE5: hclk_i2s1 */
    CHECK(seen_grf504 == (int)(1u << 21 | 1u << 5)); /* MCLK out from TX */
    CHECK(seen_grf508 == (int)(1u << 17 | 1u << 1)); /* MCLK TX output enable */
    CHECK((pmugrf[0x140 / 4] & 2) == 0 && (pmugrf[0x144 / 4] & 2) == 2);   /* vccio1 3.3 V */
    /* pins GPIO1_A2 A3 A5 A7, function 1, no pull */
    CHECK(n_muxed == 4 && pulled == 4);
    unsigned want_pins[] = { 1u << 5 | 2, 1u << 5 | 3, 1u << 5 | 5, 1u << 5 | 7 };
    for (unsigned i = 0; i < 4 && i < n_muxed; i++)
        CHECK(muxed[i] == want_pins[i]);

    /* the I2S: master, I2S, the width asked (16: the reset value), 2
     * channels, TX-only clocks, 64 BCLK a frame (32-bit slots) */
    CHECK(i2s[0] == (0x72000000u | (bits0 - 1)));
    CHECK(txcr_while_running == 0);
    CHECK((i2s[0x08 / 4] >> 28 & 3) == 1 && !(i2s[0x08 / 4] >> 27 & 1));
    CHECK((i2s[0x08 / 4] & 0xff) == 63 && (i2s[0x08 / 4] >> 8 & 0xff) == 63);
    CHECK((i2s[0x38 / 4] & 0xff) == 3);
    CHECK(i2s[0x10 / 4] == 0);                      /* no DMA */

    /* the codec: Linux's pre-init, then its probe, then the headphone path */
    unsigned lpt = first_write(0x14, 0xff, 0x40);
    CHECK(lpt != ~0u && written(0x2f, lpt) == 0x07 && written(0x38, lpt) == 0xa0);
    CHECK(pmic[0x43] == 0x58 && pmic[0x44] == 0x2d && pmic[0x45] == 0x0c && pmic[0x46] == 0xa5);
    CHECK(pmic[0x48] == 0x00);                      /* slave: the SoC gives the clocks */
    CHECK(pmic[0x4b] == codec_vdw(bits0) && pmic[0x4e] == codec_vdw(bits0));
    CHECK(pmic[0x17] == 0x40 && pmic[0x47] == 0x00 && pmic[0x4c] == 0x20 && pmic[0x15] == 0x0f);
    CHECK(pmic[0x2f] == 0x04 && pmic[0x38] == 0xa0 && pmic[0x3f] == 0x11 && pmic[0x3d] == 0x80);
    CHECK(pmic[0x31] == 0x00 && pmic[0x32] == 0x00);
    unsigned hp_on = first_write(0x3d, 0x60, 0x00), dac_on = first_write(0x2f, 0x03, 0x00);
    unsigned cp_on = first_write(0x3f, 0x10, 0x10), ldo_on = first_write(0x17, 0x40, 0x40);
    CHECK(ldo_on < cp_on && cp_on < hp_on && hp_on < dac_on);  /* supplies first, the DACs last */

    /* the FIFO's size was found by filling it (the extra words dropped) */
    CHECK(overflows == 63 - DEPTH);
    /* one second of sound: every frame once, in order, left and right in their places, no underrun */
    play(bits0, 1000, "started");
    audio_out_print();

    /* sound_depth changed in Settings while it plays: the next
     * audio_out_idle() mutes the DAC, sets the I2S and the codec to the new
     * width, starts again and unmutes; the other two widths, then back */
    static const unsigned widths[3] = { 16, 24, 32 };
    unsigned at = bits0 == 16 ? 0 : bits0 == 24 ? 1 : 2;
    for (int k = 1; k <= 3; k++) {
        unsigned b = widths[(at + (unsigned)k) % 3];
        sim_depth = b;
        unsigned w0 = n_wlog;
        audio_out_idle();
        CHECK(i2s[0] == (0x72000000u | (b - 1)));
        CHECK(txcr_while_running == 0);
        CHECK(pmic[0x4b] == codec_vdw(b) && pmic[0x4e] == codec_vdw(b));
        int muted = 0;
        for (unsigned i = w0; i < n_wlog; i++)
            if (wlog[i].reg == 0x38 && (wlog[i].val & 1))
                muted = 1;
        CHECK(muted && (pmic[0x38] & 1) == 0);      /* muted meanwhile, then not */
        CHECK(i2s[0x1c / 4] == 3 && irq_on);
        play(b, 300, "changed");
        w0 = n_wlog;
        audio_out_idle();                           /* the same width: nothing to do */
        CHECK(n_wlog == w0 && i2s[0] == (0x72000000u | (b - 1)));
    }
    /* a slow chunk (250 us: the Pi's synthesizer with every voice on) is
     * made with the FIFO full: 32 words, 16 frames (333 us) at 24 and 32
     * bits, so it does not run dry */
    render_ns = 250000;
    for (unsigned b = 24; b <= 32; b += 8) {
        sim_depth = b;
        audio_out_idle();
        play(b, 300, "250 us a chunk");
    }
    render_ns = 0;
    audio_out_print();

    /* the power down: muted, the headphone stages and the DACs off, stopped */
    rk_audio_off();
    CHECK((pmic[0x38] & 1) == 1 && (pmic[0x3d] & 0x60) == 0x60 && (pmic[0x2f] & 3) == 3);
    CHECK(i2s[0x1c / 4] == 0 && !irq_on);

    printf("audio_sim: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
