/*
 * The RGB30's sound driver (src/rgb30/rk_audio.c) on a simulated chip:
 * the CRU, the GRFs, the I2S1 controller with its FIFO draining at the
 * frame rate its clocks give, and the RK817's codec registers on I2C.
 * Checked against Linux's values (rockchip_i2s_tdm.c, rk817_codec.c,
 * rk8xx-core.c, clk-rk3568.c): the 12.288 MHz MCLK from the GPLL, 48 kHz
 * frames, the pins and the I/O voltage, the codec's headphone path powered
 * in DAPM's order, every sample of audio_render() out once and in order,
 * no underrun, and the power down.
 *
 *   build/rgb30-host/audio_sim_test [GPLL MHz]     (1188 or 1200)
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

#define DEPTH 32
static uint32_t fifo[DEPTH];
static unsigned fifo_n, overflows;
static uint32_t intsr;
static uint64_t now_ns, next_frame_ns;

static uint32_t out_words[48000 * 2];
static unsigned n_out, underruns;

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
        if (fifo_n) {
            if (n_out < sizeof out_words / sizeof out_words[0])
                out_words[n_out++] = fifo[0];
            memmove(fifo, fifo + 1, --fifo_n * sizeof fifo[0]);
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
        } else {
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

/* audio.c: a ramp, so every sample is known */
static int16_t next_sample;
static unsigned rendered;
void audio_render(int16_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        out[i] = next_sample++;
    rendered += n;
}

/* ---- the test --------------------------------------------------------- */

int main(int argc, char **argv)
{
    unsigned mhz = argc > 1 ? (unsigned)atoi(argv[1]) : 1188;
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

    /* the I2S: master, I2S 16 bits 2 channels, TX-only clocks, 64 BCLK a frame */
    CHECK(i2s[0] == 0x7200000f);
    CHECK((i2s[0x08 / 4] >> 28 & 3) == 1 && !(i2s[0x08 / 4] >> 27 & 1));
    CHECK((i2s[0x08 / 4] & 0xff) == 63 && (i2s[0x08 / 4] >> 8 & 0xff) == 63);
    CHECK((i2s[0x38 / 4] & 0xff) == 3);
    CHECK(i2s[0x10 / 4] == 0);                      /* no DMA */

    /* the codec: Linux's pre-init, then its probe, then the headphone path */
    unsigned lpt = first_write(0x14, 0xff, 0x40);
    CHECK(lpt != ~0u && written(0x2f, lpt) == 0x07 && written(0x38, lpt) == 0xa0);
    CHECK(pmic[0x43] == 0x58 && pmic[0x44] == 0x2d && pmic[0x45] == 0x0c && pmic[0x46] == 0xa5);
    CHECK(pmic[0x48] == 0x00);                      /* slave: the SoC gives the clocks */
    CHECK(pmic[0x4b] == 0x0f && pmic[0x4e] == 0x0f);
    CHECK(pmic[0x17] == 0x40 && pmic[0x47] == 0x00 && pmic[0x4c] == 0x20 && pmic[0x15] == 0x0f);
    CHECK(pmic[0x2f] == 0x04 && pmic[0x38] == 0xa0 && pmic[0x3f] == 0x11 && pmic[0x3d] == 0x80);
    CHECK(pmic[0x31] == 0x00 && pmic[0x32] == 0x00);
    unsigned hp_on = first_write(0x3d, 0x60, 0x00), dac_on = first_write(0x2f, 0x03, 0x00);
    unsigned cp_on = first_write(0x3f, 0x10, 0x10), ldo_on = first_write(0x17, 0x40, 0x40);
    CHECK(ldo_on < cp_on && cp_on < hp_on && hp_on < dac_on);  /* supplies first, the DACs last */

    /* the FIFO's size was found by filling it (the extra words dropped) */
    CHECK(overflows == 63 - DEPTH);
    /* one second of sound: every sample once, in order, L = R, no underrun */
    unsigned out0 = n_out, under0 = underruns, over0 = overflows;
    uint64_t t0 = now_ns;
    for (int i = 0; i < 1000; i++)
        tick(1000000);
    unsigned got = n_out - out0;
    double secs = (double)(now_ns - t0) / 1e9;     /* a little more: the handler's accesses take time too */
    printf("audio_sim: %u frames in %.4f s, %u underruns, %u overflows, %u rendered\n",
           got, secs, underruns - under0, overflows, rendered);
    CHECK(got > 48000 * secs - 2 && got < 48000 * secs + 2);
    CHECK(underruns == under0 && overflows == over0);
    int ordered = 1, stereo = 1;
    for (unsigned i = out0 + 1; i < n_out; i++)
        if ((uint16_t)(out_words[i] - out_words[i - 1]) != 0x0001 &&
            !((out_words[i - 1] & 0xffff) == 0 && (out_words[i] & 0xffff) == 0))
            ordered = 0;
    for (unsigned i = 0; i < n_out; i++)
        if ((out_words[i] & 0xffff) != out_words[i] >> 16)
            stereo = 0;
    CHECK(ordered);
    CHECK(stereo);
    audio_out_print();

    /* the power down: muted, the headphone stages and the DACs off, stopped */
    rk_audio_off();
    CHECK((pmic[0x38] & 1) == 1 && (pmic[0x3d] & 0x60) == 0x60 && (pmic[0x2f] & 3) == 3);
    CHECK(i2s[0x1c / 4] == 0 && !irq_on);

    printf("audio_sim: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
