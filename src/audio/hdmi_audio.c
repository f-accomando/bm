/*
 * The Pi's output of audio.c (audio_out.h): HDMI audio on the BCM2835,
 * after Circle's hdmisoundbasedevice.cpp
 * (R. Stange) and Linux vc4_hdmi.c: the HDMI block packs IEC 958 subframes
 * written into the MAI FIFO (HD block) into HDMI audio packets. The
 * firmware has already set up the video mode; here we only add the audio
 * clock regeneration (N/CTS), the audio InfoFrame and the FIFO, then a DMA
 * channel paced by the HDMI DREQ plays two buffers in a ring.
 */
#include "audio.h"
#include "audio_out.h"
#include "iec958.h"
#include "arch/cache.h"
#include "drivers/mmio.h"
#include "drivers/prop.h"
#include "drivers/dma.h"
#include "drivers/timer.h"
#include "kernel/irq.h"
#include "lib/printf.h"

#include <string.h>

#define HDMI_BASE   (PERIPHERAL_BASE + 0x902000)
#define HD_BASE     (PERIPHERAL_BASE + 0x808000)
#define CM_BASE     (PERIPHERAL_BASE + 0x101000)
#define DMA_BASE    (PERIPHERAL_BASE + 0x007000)

#define HDMI_MAI_CHANNEL_MAP    (HDMI_BASE + 0x090)
#define HDMI_MAI_CONFIG         (HDMI_BASE + 0x094)
#define HDMI_AUDIO_PACKET_CFG   (HDMI_BASE + 0x09C)
#define HDMI_RAM_PACKET_CFG     (HDMI_BASE + 0x0A0)
#define HDMI_RAM_PACKET_STATUS  (HDMI_BASE + 0x0A4)
#define HDMI_CRP_CFG            (HDMI_BASE + 0x0A8)
#define HDMI_CTS0               (HDMI_BASE + 0x0AC)
#define HDMI_CTS1               (HDMI_BASE + 0x0B0)
#define HDMI_TX_PHY_CTL0        (HDMI_BASE + 0x2C4)
#define HDMI_RAM_AUDIO(i)       (HDMI_BASE + 0x490 + 4 * (i))

#define RAM_PACKET_AUDIO        (1u << 4)       /* audio InfoFrame slot */
#define RAM_PACKET_ENABLE       (1u << 16)      /* HDMI (not DVI) mode */
#define TX_PHY_RNG_PWRDN        (1u << 25)

#define MAI_CTL                 (HD_BASE + 0x14)
#define MAI_THR                 (HD_BASE + 0x18)
#define MAI_FMT                 (HD_BASE + 0x1C)
#define MAI_DATA                (HD_BASE + 0x20)
#define MAI_SMP                 (HD_BASE + 0x2C)
#define MAI_DATA_BUS            0x7E808020u

#define MAI_CTL_RESET           (1u << 0)
#define MAI_CTL_ERRF            (1u << 1)
#define MAI_CTL_ERRE            (1u << 2)
#define MAI_CTL_ENABLE          (1u << 3)
#define MAI_CTL_CHNUM(n)        ((uint32_t)(n) << 4)
#define MAI_CTL_FLUSH           (1u << 9)
#define MAI_CTL_WHOLSMP         (1u << 12)
#define MAI_CTL_CHALIGN         (1u << 13)
#define MAI_CTL_DLATE           (1u << 15)

#define CM_HSMDIV               (CM_BASE + 0x08C)
#define A2W_PLLH_ANA0           (CM_BASE + 0x1070)
#define A2W_PLLH_CTRLR          (CM_BASE + 0x1960)
#define A2W_PLLH_FRACR          (CM_BASE + 0x1A60)

#define DMA_CS(c)               (DMA_BASE + (c) * 0x100 + 0x00)
#define DMA_CONBLK_AD(c)        (DMA_BASE + (c) * 0x100 + 0x04)
#define DMA_DEBUG(c)            (DMA_BASE + (c) * 0x100 + 0x20)
#define DMA_INT_STATUS          (DMA_BASE + 0xFE0)
#define DMA_ENABLE              (DMA_BASE + 0xFF0)
#define CS_ACTIVE               (1u << 0)
#define CS_END                  (1u << 1)
#define CS_INT                  (1u << 2)
#define CS_ERROR                (1u << 8)
#define CS_PRIORITY(p)          ((uint32_t)(p) << 16)
#define CS_PANIC_PRIORITY(p)    ((uint32_t)(p) << 20)
#define CS_WAIT_WRITES          (1u << 28)
#define CS_ABORT                (1u << 30)
#define CS_RESET                (1u << 31)
#define TI_INTEN                (1u << 0)
#define TI_WAIT_RESP            (1u << 3)
#define TI_DEST_DREQ            (1u << 6)
#define TI_SRC_INC              (1u << 8)
#define TI_SRC_WIDTH            (1u << 9)
#define TI_PERMAP(p)            ((uint32_t)(p) << 16)
#define DREQ_HDMI               17
#define IRQ_DMA(c)              (16 + (c))

typedef struct {
    uint32_t ti, src, dst, len, stride, next, pad[2];
} dma_cb_t;

static dma_cb_t cbs[2] __attribute__((aligned(32)));
static uint32_t bufs[2][AUDIO_CHUNK * 2] __attribute__((aligned(32)));
static int16_t pcm[AUDIO_CHUNK];

static iec958_t iec;
static unsigned dma_ch;
static uint32_t hsm_rate, pixel_rate, cts_n, smp_n, smp_m;
static volatile uint32_t chunks, errors;

/* ---- clocks ------------------------------------------------------------ */

static uint32_t hsm_clock(void)
{
    /* PLLD (500 MHz) / CM_HSMDIV, a 12.12 divider with 4.8 bits in use */
    uint32_t div = mmio_read(CM_HSMDIV) >> 4 & 0xFFF;
    return div ? (uint32_t)((500000000ull << 8) / div) : 0;
}

static uint32_t pixel_clock(void)
{
    uint32_t ctrl = mmio_read(A2W_PLLH_CTRLR);
    uint32_t fdiv = mmio_read(A2W_PLLH_FRACR) & 0xFFFFF;
    uint32_t ndiv = ctrl & 0x3FF, pdiv = ctrl >> 12 & 7;
    if (mmio_read(A2W_PLLH_ANA0 + 4) & (1u << 11)) {
        ndiv *= 2;
        fdiv *= 2;
    }
    if (!pdiv)
        return 0;
    uint64_t rate = 19200000ull * (((uint64_t)ndiv << 20) + fdiv) / pdiv;
    return (uint32_t)((rate >> 20) / 10);
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

static int wait_bit(uint32_t reg, uint32_t mask, int set, uint32_t ms)
{
    uint32_t t0 = timer_ticks();
    while (!!(mmio_read(reg) & mask) != set)
        if (timer_ticks() - t0 > ms * 1000)
            return -1;
    return 0;
}

/* ---- the refill ------------------------------------------------------- */

static void fill(unsigned b)
{
    audio_render(pcm, AUDIO_CHUNK);
    iec958_encode(&iec, pcm, bufs[b], AUDIO_CHUNK);
    dcache_clean_range(bufs[b], sizeof bufs[b]);
}

static void dma_irq(void *arg)
{
    (void)arg;
    dmb();
    mmio_write(DMA_INT_STATUS, 1u << dma_ch);
    uint32_t cs = mmio_read(DMA_CS(dma_ch));
    mmio_write(DMA_CS(dma_ch), cs);                 /* clears INT and END */
    if (cs & CS_ERROR) {
        errors++;
        dmb();
        return;
    }
    /* the DMA already runs the next block: refill the one it just left */
    uint32_t cur = mmio_read(DMA_CONBLK_AD(dma_ch));
    dmb();
    fill(cur == ARM_TO_BUS(&cbs[1]) ? 0 : 1);
    chunks++;
    dmb();
}

/* ---- start ------------------------------------------------------------ */

static const char **out_status;

static int fail(const char *why)
{
    *out_status = why;
    return -1;
}

int audio_out_start(const char **status)
{
    out_status = status;
    iec958_init(&iec, AUDIO_RATE);

    dmb();
    if (!(mmio_read(HDMI_RAM_PACKET_CFG) & RAM_PACKET_ENABLE))
        return fail("no HDMI audio (DVI mode or no HDMI; hdmi_drive=2 in config.txt)");
    hsm_rate = hsm_clock();
    pixel_rate = pixel_clock();
    if (!hsm_rate || !pixel_rate || pixel_rate > 200000000)
        return fail("cannot read the HDMI clocks");

    static const uint8_t pref[] = { 5, 4, 2, 0 };
    int c = dma_channel_claim(pref, sizeof pref);
    if (c < 0)
        return fail("no free DMA channel");
    dma_ch = (unsigned)c;

    /* audio clock regeneration: N = 128 fs / 1000, CTS = pixel N / 128 fs */
    rational(hsm_rate, AUDIO_RATE, 0xFFFFFF, 256, &smp_n, &smp_m);
    cts_n = AUDIO_RATE * 128 / 1000;
    uint32_t cts = (uint32_t)((uint64_t)pixel_rate * cts_n / (AUDIO_RATE * 128));

    dmb();
    mmio_write(MAI_CTL, MAI_CTL_RESET | MAI_CTL_FLUSH | MAI_CTL_DLATE | MAI_CTL_ERRE | MAI_CTL_ERRF);
    mmio_write(MAI_SMP, smp_n << 8 | (smp_m - 1));
    mmio_write(MAI_FMT, 9u << 8 | 2u << 16);        /* 48 kHz, PCM */
    mmio_write(MAI_THR, 16u << 24 | 16u << 16 | 16u << 8 | 16u);
    mmio_write(HDMI_MAI_CONFIG, 1u << 27 | 1u << 26 | 3u);  /* format+bit reverse, 2 ch */
    mmio_write(HDMI_MAI_CHANNEL_MAP, 0x08);
    mmio_write(HDMI_AUDIO_PACKET_CFG, 1u << 29 | 1u << 24 | IEC958_B_PREAMBLE << 10 | 3u);
    mmio_write(HDMI_CRP_CFG, 1u << 24 | cts_n);
    mmio_write(HDMI_CTS0, cts);
    mmio_write(HDMI_CTS1, cts);

    /* audio InfoFrame: 2 channels, everything else "refer to stream" */
    mmio_write(HDMI_RAM_PACKET_CFG, mmio_read(HDMI_RAM_PACKET_CFG) & ~RAM_PACKET_AUDIO);
    if (wait_bit(HDMI_RAM_PACKET_STATUS, RAM_PACKET_AUDIO, 0, 100) != 0)
        return fail("HDMI InfoFrame slot busy");
    mmio_write(HDMI_RAM_AUDIO(0), 0x0A0184);
    mmio_write(HDMI_RAM_AUDIO(1), 0x0170);
    for (unsigned i = 2; i <= 8; i++)
        mmio_write(HDMI_RAM_AUDIO(i), 0);
    mmio_write(HDMI_RAM_PACKET_CFG, mmio_read(HDMI_RAM_PACKET_CFG) | RAM_PACKET_AUDIO);
    if (wait_bit(HDMI_RAM_PACKET_STATUS, RAM_PACKET_AUDIO, 1, 100) != 0)
        return fail("HDMI InfoFrame not accepted");
    dmb();

    /* two control blocks in a ring, each raising an interrupt when done */
    fill(0);
    fill(1);
    for (unsigned i = 0; i < 2; i++) {
        cbs[i].ti = TI_PERMAP(DREQ_HDMI) | TI_SRC_WIDTH | TI_SRC_INC | TI_DEST_DREQ |
                    TI_WAIT_RESP | TI_INTEN;
        cbs[i].src = ARM_TO_BUS(bufs[i]);
        cbs[i].dst = MAI_DATA_BUS;
        cbs[i].len = sizeof bufs[i];
        cbs[i].stride = 0;
        cbs[i].next = ARM_TO_BUS(&cbs[i ^ 1]);
        cbs[i].pad[0] = cbs[i].pad[1] = 0;
    }
    dcache_clean_range(cbs, sizeof cbs);

    dmb();
    mmio_write(DMA_ENABLE, mmio_read(DMA_ENABLE) | 1u << dma_ch);
    mmio_write(DMA_CS(dma_ch), CS_RESET);
    timer_delay_us(10);
    mmio_write(DMA_CS(dma_ch), CS_INT | CS_END);
    mmio_write(DMA_INT_STATUS, 1u << dma_ch);
    dmb();
    irq_register(IRQ_DMA(dma_ch), dma_irq, 0);
    irq_enable(IRQ_DMA(dma_ch));
    dmb();
    mmio_write(DMA_CONBLK_AD(dma_ch), ARM_TO_BUS(&cbs[0]));
    mmio_write(DMA_CS(dma_ch), CS_WAIT_WRITES | CS_PANIC_PRIORITY(15) | CS_PRIORITY(1) | CS_ACTIVE);
    dmb();

    mmio_write(HDMI_TX_PHY_CTL0, mmio_read(HDMI_TX_PHY_CTL0) & ~TX_PHY_RNG_PWRDN);
    mmio_write(MAI_CTL, MAI_CTL_CHNUM(2) | MAI_CTL_WHOLSMP | MAI_CTL_CHALIGN | MAI_CTL_ENABLE);
    dmb();

    /* the ring must be moving: two buffers take 10.7 ms */
    uint32_t t0 = timer_ticks();
    while (chunks < 2 && timer_ticks() - t0 < 100000)
        ;
    if (chunks < 2) {
        irq_disable(IRQ_DMA(dma_ch));
        mmio_write(DMA_CS(dma_ch), CS_RESET);
        mmio_write(MAI_CTL, MAI_CTL_DLATE | MAI_CTL_ERRE | MAI_CTL_ERRF);
        dmb();
        return fail("HDMI audio FIFO does not take data (DMA stalled)");
    }
    *status = "HDMI 48 kHz";
    return 0;
}

void audio_out_idle(void)
{
}

void audio_out_print(void)
{
    dmb();
    uint32_t ctl = mmio_read(MAI_CTL);
    dmb();
    kprintf("       DMA channel %u, %lu chunks, %lu errors\n", dma_ch, chunks, errors);
    kprintf("       HSM %lu Hz, pixel %lu Hz, N %lu, MAI ctl %08lx\n",
            hsm_rate, pixel_rate, cts_n, ctl);
}
