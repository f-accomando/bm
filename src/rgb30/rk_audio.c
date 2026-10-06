/*
 * The RGB30's sound, the output of audio.c (audio_out.h): the RK3566's
 * I2S1 (the 8-channel TDM controller used as plain I2S, 2 channels) sends
 * 48 kHz 16-bit stereo to the codec inside the RK817 PMIC, whose
 * headphone amplifier drives the jack and the speaker: the console moves
 * the sound between them by itself when headphones go in, and the
 * headphones are mono (Linux's rk3566-powkiddy-rk2023.dtsi).
 *
 * The values are Linux's: rockchip_i2s_tdm.c (the I2S, clocks shared with
 * TX: "rockchip,trcm-sync-tx-only"), rk817_codec.c and rk8xx-core.c (the
 * codec's registers and the order its DAPM powers the headphone path),
 * clk-rk3568.c (the 12.288 MHz MCLK, 256 fs, from the GPLL through the
 * fractional divider), rk3568-pinctrl.dtsi (i2s1m0 pins) and io-domain.c
 * (vccio1 at 3.3 V, the codec's I/O). No DMA: the I2S FIFO raises its
 * "TX FIFO at the threshold" interrupt (GIC SPI 53) and the handler fills
 * it from a chunk that audio_render() makes, as the Pi's DMA interrupt.
 */
#ifdef PLAT_RK3566
#include "rk_audio.h"
#include "rk_pmic.h"
#include "rk_gpio.h"
#include "io.h"
#include "audio/audio.h"
#include "audio/audio_out.h"
#include "drivers/timer.h"
#include "kernel/irq.h"
#include "lib/printf.h"

#define CRU         0xfdd20000u
#define GRF         0xfdc60000u
#define PMU_GRF     0xfdc20000u
#define I2S1        0xfe410000u
#define IRQ_I2S1    (32 + 53)

#define CRU_GPLL    0x40                /* PLL_CON(16): CON0, CON1, CON2 */
#define CRU_MODE    0xc0                /* GPLL: bits 7:6, 1 = normal */
#define CLKSEL(n)   (0x100 + 4 * (n))
#define GATE(n)     (0x300 + 4 * (n))

/* I2S TDM controller */
#define I2S_TXCR    0x00
#define I2S_CKR     0x08
#define I2S_TXFIFOLR 0x0c
#define I2S_DMACR   0x10
#define I2S_INTCR   0x14
#define I2S_INTSR   0x18
#define I2S_XFER    0x1c
#define I2S_CLR     0x20
#define I2S_TXDR    0x24
#define I2S_CLKDIV  0x38

#define TXCR_I2S16  0x7200000fu         /* reset value: I2S, 16 bits, 2 channels, HWT 0 (a word = L | R) */
#define CKR_TXONLY  (1u << 28)          /* RX on TX's clocks (TRCM) */
#define INTCR_TXEIE (1u << 0)
#define INTCR_TXUIC (1u << 2)
#define INTCR_TFT(n) ((uint32_t)((n) - 1) << 4)
#define INTSR_TXUI  (1u << 1)

#define MCLK_HZ     (256u * AUDIO_RATE)  /* 12.288 MHz */
#define BCLK_DIV    4                   /* 3.072 MHz: 64 bit clocks a frame */
#define LRCK_DIV    64
#define FIFO_MAX    63                  /* probed at start: the words the TX FIFO takes (6-bit level) */
#define THRESHOLD   16

static int16_t pcm[AUDIO_CHUNK];
static unsigned pos = AUDIO_CHUNK;      /* next sample of pcm[] to send */
static unsigned fifo_words;
static volatile uint32_t irqs, chunks, underruns, words;
static uint32_t gpll_hz, mclk_hz, frac_n, frac_d;
static int codec_errors, running;

static inline void w(uint32_t off, uint32_t v) { writel(I2S1 + off, v); }
static inline uint32_t r(uint32_t off)         { return readl(I2S1 + off); }

/* ---- clocks ------------------------------------------------------------ */

/* PLL_CON0..2 of the RK3568 family (Linux clk-pll.c, rk3036 layout) */
static uint32_t gpll_rate(void)
{
    if ((readl(CRU + CRU_MODE) >> 6 & 3) != 1)
        return 24000000;                /* slow mode: the oscillator */
    uint32_t c0 = readl(CRU + CRU_GPLL), c1 = readl(CRU + CRU_GPLL + 4), c2 = readl(CRU + CRU_GPLL + 8);
    uint32_t fb = c0 & 0xfff, post1 = c0 >> 12 & 7, ref = c1 & 0x3f, post2 = c1 >> 6 & 7;
    if (!ref || !post1 || !post2)
        return 0;
    uint64_t hz = 24000000ull * fb;
    if (!(c1 >> 12 & 1))                /* DSMPD 0: the fraction counts */
        hz += (24000000ull * (c2 & 0xffffff)) >> 24;
    return (uint32_t)(hz / ref / post1 / post2);
}

/* Linux lib/math/rational.c: best n/d with n <= maxn, d <= maxd. */
static void rational(uint32_t gn, uint32_t gd, uint32_t maxn, uint32_t maxd,
                     uint32_t *bn, uint32_t *bd)
{
    uint32_t n = gn, d = gd, n0 = 0, d1 = 0, n1 = 1, d0 = 1;
    while (d) {
        uint32_t dp = d, a = n / d;
        d = n % d;
        n = dp;
        uint32_t n2 = n0 + a * n1, d2 = d0 + a * d1;
        if (n2 > maxn || d2 > maxd) {
            uint32_t t1 = (maxn - n0) / n1, t2 = (maxd - d0) / d1;
            uint32_t t = t1 < t2 ? t1 : t2;
            if (2 * t > a || (2 * t == a && d0 * dp > d1 * d)) {
                n1 = n0 + t * n1;
                d1 = d0 + t * d1;
            }
            break;
        }
        n0 = n1; n1 = n2;
        d0 = d1; d1 = d2;
    }
    *bn = n1;
    *bd = d1;
}

/* MCLK_I2S1_8CH_TX = GPLL through the fractional divider, out on the
 * i2s1m0_mclk pin as I2S1_MCLKOUT_TX (the RK817's mclk) */
static int clocks(void)
{
    gpll_hz = gpll_rate();
    if (gpll_hz < 20 * MCLK_HZ)
        return -1;
    rational(MCLK_HZ, gpll_hz, 0xffff, 0xffff, &frac_n, &frac_d);
    mclk_hz = (uint32_t)((uint64_t)gpll_hz * frac_n / frac_d);
    writel(CRU + GATE(5), HIWORD(1u << 11, 0));         /* hclk_i2s1_8ch */
    /* CLKSEL15: src = GPLL / 1, the TX clock from the fraction, MCLKOUT from TX */
    writel(CRU + CLKSEL(15), HIWORD(0x7fu | 3u << 8 | 3u << 10 | 1u << 15, 1u << 10));
    writel(CRU + CLKSEL(16), frac_n << 16 | frac_d);
    writel(CRU + GATE(6), HIWORD(0xfu << 8, 0));        /* src, frac, mclk_tx, mclkout_tx */
    /* SYS_GRF: I2S1's MCLK out from TX, the pin an output */
    writel(GRF + 0x504, HIWORD(1u << 5, 1u << 5));
    writel(GRF + 0x508, HIWORD(1u << 1, 1u << 1));
    return 0;
}

/* vccio1 (the codec's I/O, vccio_acodec) at 3.3 V, as io-domain.c sets
 * it; then the pins: MCLK, SCLK TX, LRCK TX, SDO0 */
static void pins(void)
{
    writel(PMU_GRF + 0x140, 1u << 17);                 /* IO_VSEL0 bit 1: not 1.8 V */
    writel(PMU_GRF + 0x144, 1u << 17 | 1u << 1);       /* IO_VSEL1 bit 1: 3.3 V */
    static const unsigned p[] = { rk_pin(1, 'A', 2), rk_pin(1, 'A', 3), rk_pin(1, 'A', 5), rk_pin(1, 'A', 7) };
    for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++) {
        rk_pin_mux(p[i], 1);
        rk_pin_pull(p[i], RK_PULL_NONE);
    }
}

/* ---- the RK817's codec ------------------------------------------------- */

static void cw(uint8_t reg, uint8_t val)
{
    if (rk817_write(reg, val) != 0)
        codec_errors++;
}

static void cupd(uint8_t reg, uint8_t mask, uint8_t val)
{
    int v = rk817_read(reg);
    if (v < 0) {
        codec_errors++;
        return;
    }
    cw(reg, (uint8_t)((v & ~mask) | val));
}

/* rk8xx-core.c's rk817_pre_init_reg (the codec's part), in its order */
static const uint8_t pre_init[][2] = {
    { 0x12, 0x03 }, { 0x13, 0x00 }, { 0x14, 0x00 }, { 0x15, 0x00 }, { 0x16, 0x00 }, { 0x17, 0x06 },
    { 0x18, 0xc8 }, { 0x19, 0x00 }, { 0x1a, 0x00 }, { 0x1b, 0x00 }, { 0x1e, 0x00 }, { 0x1f, 0x00 },
    { 0x20, 0x00 }, { 0x21, 0x00 }, { 0x22, 0x00 }, { 0x23, 0xff }, { 0x24, 0xff }, { 0x27, 0x70 },
    { 0x28, 0x00 }, { 0x29, 0x66 }, { 0x2a, 0x00 }, { 0x2b, 0x00 }, { 0x2c, 0x00 }, { 0x2d, 0x00 },
    { 0x2e, 0x00 }, { 0x2f, 0x07 }, { 0x30, 0x82 }, { 0x31, 0x00 }, { 0x32, 0x00 }, { 0x35, 0x00 },
    { 0x36, 0x00 }, { 0x37, 0x00 }, { 0x38, 0xa0 }, { 0x39, 0xff }, { 0x3a, 0xff }, { 0x3b, 0x00 },
    { 0x3c, 0x00 }, { 0x3d, 0xe0 }, { 0x3e, 0x1f }, { 0x3f, 0x09 }, { 0x40, 0x69 }, { 0x41, 0x44 },
    { 0x42, 0x04 }, { 0x43, 0x00 }, { 0x44, 0x30 }, { 0x45, 0x19 }, { 0x46, 0x65 }, { 0x47, 0x01 },
    { 0x48, 0x01 }, { 0x49, 0x00 }, { 0x4a, 0x00 }, { 0x4b, 0x17 }, { 0x4c, 0x00 }, { 0x4d, 0x00 },
    { 0x4e, 0x17 }, { 0x4f, 0x00 },
};

static void codec_start(void)
{
    for (unsigned i = 0; i < sizeof pre_init / sizeof pre_init[0]; i++)
        cw(pre_init[i][0], pre_init[i][1]);
    /* rk817_probe: reset, rk817_init, the PLL (the vendor's values) */
    cw(0x14, 0x40);             /* DTOP_LPT_SRST */
    cw(0x30, 0x02);             /* DDAC_POPD_DACST */
    cw(0x35, 0x02);             /* DDAC_SR_LMT0 */
    cw(0x1e, 0x02);             /* DADC_SR_ACL0 */
    cw(0x13, 0xf4);             /* DTOP_VUCTIME */
    cw(0x43, 0x58);             /* APLL_CFG1..4 */
    cw(0x44, 0x2d);
    cw(0x45, 0x0c);
    cw(0x46, 0xa5);
    /* set_fmt, hw_params: the SoC gives the clocks; 16-bit words */
    cupd(0x48, 0x01, 0x00);     /* DI2S_CKM: slave */
    cw(0x4b, 0x0f);             /* DI2S_RXCR2 */
    cw(0x4e, 0x0f);             /* DI2S_TXCR2 */
}

/* the headphone path up, supplies first as DAPM does */
static void codec_play(void)
{
    cupd(0x17, 0x46, 0x40);     /* AREF_RTCFG1: LDO on, IBIAS and VAvg buffer on */
    cupd(0x47, 0x01, 0x00);     /* APLL_CFG5: the PLL on */
    cupd(0x4c, 0x20, 0x20);     /* DI2S_RXCMD_TSD: transfer start */
    cupd(0x15, 0x0f, 0x0f);     /* DTOP_DIGEN_CLKE: DAC clock, I2S RX clock, both channels */
    cupd(0x2f, 0x08, 0x00);     /* ADAC_CFG1: DAC bias */
    cupd(0x38, 0x01, 0x00);     /* DDAC_MUTE_MIXCTL: not muted */
    cupd(0x3f, 0x18, 0x10);     /* AHP_CP: charge pump on, its discharge LDO off */
    cupd(0x3d, 0x60, 0x00);     /* AHP_CFG0: output stage and pre-amplifier */
    cupd(0x2f, 0x03, 0x00);     /* ADAC_CFG1: DAC L and R */
    cw(0x31, 0x00);             /* DDAC_VOLL, VOLR: 0 dB (the volume is audio.c's) */
    cw(0x32, 0x00);
}

/* ---- the FIFO ---------------------------------------------------------- */

static void feed(void)
{
    uint32_t level = r(I2S_TXFIFOLR) & 0x3f;
    while (level < fifo_words) {
        if (pos >= AUDIO_CHUNK) {
            audio_render(pcm, AUDIO_CHUNK);
            pos = 0;
            chunks++;
        }
        uint16_t s = (uint16_t)pcm[pos++];
        w(I2S_TXDR, (uint32_t)s << 16 | s);             /* left low, right high */
        level++;
        words++;
    }
}

static void i2s_irq(void *arg)
{
    (void)arg;
    if (r(I2S_INTSR) & INTSR_TXUI) {
        underruns++;
        w(I2S_INTCR, r(I2S_INTCR) | INTCR_TXUIC);
    }
    feed();
    irqs++;
}

static const char **out_status;

static int fail(const char *why)
{
    *out_status = why;
    return -1;
}

int audio_out_start(const char **status)
{
    out_status = status;
    if (!rk817_present())
        return fail("the RK817 does not answer on I2C");
    if (clocks() != 0)
        return fail("cannot make the 12.288 MHz MCLK (GPLL)");
    pins();
    timer_delay_us(100);                    /* MCLK running before the codec */
    codec_start();

    /* the I2S: stopped and cleared, then master, I2S, 16 bits */
    w(I2S_XFER, 0);
    w(I2S_CLR, 3);
    for (int i = 0; i < 100 && (r(I2S_CLR) & 3); i++)
        timer_delay_us(15);
    w(I2S_DMACR, 0);
    w(I2S_TXCR, TXCR_I2S16);
    w(I2S_CKR, CKR_TXONLY | (LRCK_DIV - 1u) << 8 | (LRCK_DIV - 1u));
    w(I2S_CLKDIV, (BCLK_DIV - 1u) << 8 | (BCLK_DIV - 1u));

    /* the FIFO's size: silence until it takes no more (it does not move yet) */
    for (int i = 0; i < FIFO_MAX; i++)
        w(I2S_TXDR, 0);
    fifo_words = r(I2S_TXFIFOLR) & 0x3f;
    if (fifo_words < THRESHOLD + 4)
        return fail("the I2S FIFO does not take data");
    w(I2S_INTCR, INTCR_TFT(THRESHOLD) | INTCR_TXUIC | INTCR_TXEIE);
    irq_register(IRQ_I2S1, i2s_irq, 0);
    irq_enable(IRQ_I2S1);
    w(I2S_XFER, 3);                         /* TX and RX (on TX's clocks), as Linux */
    codec_play();

    /* the FIFO must be moving: two chunks take 10.7 ms */
    uint32_t t0 = timer_ticks();
    while (chunks < 2 && timer_ticks() - t0 < 100000)
        ;
    if (chunks < 2) {
        irq_disable(IRQ_I2S1);
        w(I2S_INTCR, 0);
        w(I2S_XFER, 0);
        return fail(irqs ? "the I2S FIFO does not drain (no bit clock?)" : "no interrupt from the I2S");
    }
    running = 1;
    *status = codec_errors ? "RK817 codec, 48 kHz (I2C errors)" : "RK817 codec, 48 kHz";
    return 0;
}

void audio_out_print(void)
{
    kprintf("       I2S1: %lu interrupts, %lu chunks, %lu underruns, FIFO %u words\n",
            irqs, chunks, underruns, fifo_words);
    kprintf("       MCLK %lu Hz (GPLL %lu Hz x %lu/%lu), codec I2C errors %d\n",
            mclk_hz, gpll_hz, frac_n, frac_d, codec_errors);
}

void audio_out_idle(void)
{
}

/* Before a restart or the power off: the headphone amplifier off first
 * (no pop), then the I2S */
void rk_audio_off(void)
{
    if (!running)
        return;
    running = 0;
    cupd(0x38, 0x01, 0x01);                 /* DAC muted */
    cupd(0x3d, 0x60, 0x60);                 /* headphone stages off */
    cupd(0x2f, 0x03, 0x03);                 /* DACs off */
    irq_disable(IRQ_I2S1);
    w(I2S_INTCR, 0);
    w(I2S_XFER, 0);
}

#endif
