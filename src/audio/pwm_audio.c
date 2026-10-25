/*
 * BCM2835 PWM analog stereo audio (3.5mm jack on Raspberry Pi 1 Model B/B+).
 * Driven by DMA paced by DREQ 5 (PWM DREQ) into PWM FIFO at 48 kHz.
 * Pinout: GPIO 40 (PWM0, Left) and GPIO 45 (PWM1, Right) in ALT0 mode.
 * Pins 0-27 on the GPIO header are untouched (no conflict with LCD hats).
 */
#include "audio.h"
#include "audio_out.h"
#include "pwm_audio.h"
#include "arch/cache.h"
#include "drivers/mmio.h"
#include "drivers/dma.h"
#include "drivers/gpio.h"
#include "drivers/board.h"
#include "drivers/timer.h"
#include "drivers/watchdog.h"
#include "kernel/irq.h"
#include "lib/printf.h"

#include <string.h>

#define CM_BASE         (PERIPHERAL_BASE + 0x101000)
#define CM_PWMCTL       (CM_BASE + 0x0A0)
#define CM_PWMDIV       (CM_BASE + 0x0A4)
#define CM_PASSWD       0x5A000000u
#define CM_CTL_BUSY     (1u << 7)
#define CM_CTL_KILL     (1u << 5)
#define CM_CTL_ENAB     (1u << 4)
#define CM_CTL_SRC_OSC  1u              /* 19.2 MHz oscillator */
#define CM_CTL_SRC_PLLD 6u              /* PLLD 500 MHz (250 MHz carrier with div=2) */

#define PWM_BASE        (PERIPHERAL_BASE + 0x20C000)
#define PWM_CTL         (PWM_BASE + 0x00)
#define PWM_STA         (PWM_BASE + 0x04)
#define PWM_DMAC        (PWM_BASE + 0x08)
#define PWM_RNG1        (PWM_BASE + 0x10)
#define PWM_DAT1        (PWM_BASE + 0x14)
#define PWM_FIF1        (PWM_BASE + 0x18)
#define PWM_RNG2        (PWM_BASE + 0x20)
#define PWM_DAT2        (PWM_BASE + 0x24)
#define PWM_FIF1_BUS    0x7E20C018u

#define PWM_CTL_PWEN1   (1u << 0)
#define PWM_CTL_USEF1   (1u << 5)
#define PWM_CTL_CLRF1   (1u << 6)
#define PWM_CTL_MSEN1   (1u << 7)
#define PWM_CTL_PWEN2   (1u << 8)
#define PWM_CTL_USEF2   (1u << 13)
#define PWM_CTL_MSEN2   (1u << 15)

#define PWM_RANGE       5208u           /* 250 MHz / 48000 Hz = 5208 */
#define DREQ_PWM        5
#define IRQ_DMA(c)      (16 + (c))

#define DMA_BASE        (PERIPHERAL_BASE + 0x007000)
#define DMA_CS(ch)      (DMA_BASE + (ch) * 0x100 + 0x00)
#define DMA_CONBLK_AD(ch) (DMA_BASE + (ch) * 0x100 + 0x04)
#define DMA_ENABLE      (DMA_BASE + 0xFF0)
#define DMA_INT_STATUS  (DMA_BASE + 0xFE0)

#define CS_RESET        (1u << 31)
#define CS_WAIT_WRITES  (1u << 28)
#define CS_PANIC_PRIORITY(p) ((uint32_t)(p) << 20)
#define CS_PRIORITY(p)  ((uint32_t)(p) << 16)
#define CS_ERROR        (1u << 8)
#define CS_INT          (1u << 2)
#define CS_END          (1u << 1)
#define CS_ACTIVE       (1u << 0)

#define TI_INTEN        (1u << 0)
#define TI_WAIT_RESP    (1u << 3)
#define TI_DEST_DREQ    (1u << 6)
#define TI_SRC_INC      (1u << 8)
#define TI_SRC_WIDTH    (1u << 9)
#define TI_PERMAP(dreq) ((uint32_t)(dreq) << 16)

#define AUDIO_CHUNK     256             /* stereo frames per buffer */

typedef struct {
    uint32_t ti, src, dst, len, stride, next, pad[2];
} __attribute__((aligned(32))) dma_cb_t;

static uint32_t pwm_bufs[2][AUDIO_CHUNK * 2] __attribute__((aligned(CACHE_LINE)));
static int16_t pcm_temp[AUDIO_CHUNK * 2];
static dma_cb_t cbs[2] __attribute__((aligned(32)));
static unsigned dma_ch;
static volatile uint32_t chunks, errors;
static const char **out_status;

static int pwm_fail(const char *why)
{
    if (out_status)
        *out_status = why;
    return -1;
}

static void pwm_fill(unsigned b)
{
    audio_render(pcm_temp, AUDIO_CHUNK);
    for (unsigned i = 0; i < AUDIO_CHUNK; i++) {
        int32_t l = pcm_temp[2 * i];
        int32_t r = pcm_temp[2 * i + 1];
        /* Map signed 16-bit [-32768, 32767] to [0, PWM_RANGE] */
        uint32_t val_l = (uint32_t)(((int64_t)(l + 32768) * (PWM_RANGE - 1)) / 65535);
        uint32_t val_r = (uint32_t)(((int64_t)(r + 32768) * (PWM_RANGE - 1)) / 65535);
        pwm_bufs[b][2 * i] = val_l;
        pwm_bufs[b][2 * i + 1] = val_r;
    }
    dcache_clean_range(pwm_bufs[b], sizeof pwm_bufs[b]);
}

static void pwm_dma_irq(void *arg)
{
    (void)arg;
    dmb();
    mmio_write(DMA_INT_STATUS, 1u << dma_ch);
    uint32_t cs = mmio_read(DMA_CS(dma_ch));
    mmio_write(DMA_CS(dma_ch), cs);
    if (cs & CS_ERROR) {
        errors++;
        dmb();
        return;
    }
    uint32_t cur = mmio_read(DMA_CONBLK_AD(dma_ch));
    dmb();
    pwm_fill(cur == ARM_TO_BUS(&cbs[1]) ? 0 : 1);
    chunks++;
    dmb();
}

int pwm_audio_start(const char **status)
{
    out_status = status;

    /* Pi 1 Model B/B+ only: Pi Zero models have no jack and use GPIO 45 for BT_REG_ON */
    if (!board_is_pi1())
        return pwm_fail("board has no 3.5mm jack (Pi 1 only)");

    /* QEMU has no PWM block: a write to it is a data abort */
    if (watchdog_emulated())
        return pwm_fail("no PWM audio in an emulator");

    /* Claim DMA channel */
    static const uint8_t pref[] = { 5, 4, 2, 0 };
    int c = dma_channel_claim(pref, sizeof pref);
    if (c < 0)
        return pwm_fail("no free DMA channel for PWM audio");
    dma_ch = (unsigned)c;

    /* 1. Setup GPIO 40 and 45 for PWM0 / PWM1 (Audio Jack).
     * Pins 0-27 on the GPIO header are untouched (no LCD conflict). */
    gpio_set_function(40, GPIO_ALT0);
    gpio_set_function(45, GPIO_ALT0);
    gpio_set_pull(40, GPIO_PULL_NONE);
    gpio_set_pull(45, GPIO_PULL_NONE);

    /* 2. Setup Clock Manager for PWM (PLLD 500 MHz, div=2 -> 250 MHz) */
    dmb();
    mmio_write(CM_PWMCTL, CM_PASSWD | CM_CTL_KILL);
    timer_delay_us(10);
    while (mmio_read(CM_PWMCTL) & CM_CTL_BUSY)
        ;
    mmio_write(CM_PWMDIV, CM_PASSWD | (2u << 12));  /* divisor = 2 */
    timer_delay_us(10);
    mmio_write(CM_PWMCTL, CM_PASSWD | CM_CTL_SRC_PLLD);
    timer_delay_us(10);
    mmio_write(CM_PWMCTL, mmio_read(CM_PWMCTL) | CM_PASSWD | CM_CTL_ENAB);
    timer_delay_us(2000);

    /* 3. Setup PWM controller: Range 5208, Stereo FIFO, Mark-Space mode, enable DMA */
    mmio_write(PWM_CTL, 0);
    timer_delay_us(10);
    mmio_write(PWM_RNG1, PWM_RANGE);
    mmio_write(PWM_RNG2, PWM_RANGE);
    mmio_write(PWM_CTL, PWM_CTL_PWEN1 | PWM_CTL_USEF1 |
                        PWM_CTL_PWEN2 | PWM_CTL_USEF2 |
                        PWM_CTL_MSEN1 | PWM_CTL_MSEN2 |
                        PWM_CTL_CLRF1);
    timer_delay_us(2000);
    mmio_write(PWM_DMAC, (1u << 31) | (7u << 8) | (7u << 0));
    timer_delay_us(10);

    /* 4. Pre-fill ring buffers */
    pwm_fill(0);
    pwm_fill(1);
    for (unsigned i = 0; i < 2; i++) {
        cbs[i].ti = TI_PERMAP(DREQ_PWM) | TI_SRC_WIDTH | TI_SRC_INC | TI_DEST_DREQ |
                    TI_WAIT_RESP | TI_INTEN;
        cbs[i].src = ARM_TO_BUS(pwm_bufs[i]);
        cbs[i].dst = PWM_FIF1_BUS;
        cbs[i].len = sizeof pwm_bufs[i];
        cbs[i].stride = 0;
        cbs[i].next = ARM_TO_BUS(&cbs[i ^ 1]);
        cbs[i].pad[0] = cbs[i].pad[1] = 0;
    }
    dcache_clean_range(cbs, sizeof cbs);

    /* 5. Start DMA */
    dmb();
    mmio_write(DMA_ENABLE, mmio_read(DMA_ENABLE) | 1u << dma_ch);
    mmio_write(DMA_CS(dma_ch), CS_RESET);
    timer_delay_us(10);
    while (mmio_read(DMA_CS(dma_ch)) & CS_RESET)
        ;
    mmio_write(DMA_CS(dma_ch), CS_INT | CS_END);
    mmio_write(DMA_INT_STATUS, 1u << dma_ch);
    dmb();
    irq_register(IRQ_DMA(dma_ch), pwm_dma_irq, 0);
    irq_enable(IRQ_DMA(dma_ch));
    dmb();
    mmio_write(DMA_CONBLK_AD(dma_ch), ARM_TO_BUS(&cbs[0]));
    mmio_write(DMA_CS(dma_ch), CS_WAIT_WRITES | CS_PANIC_PRIORITY(15) | CS_PRIORITY(1) | CS_ACTIVE);
    dmb();

    /* 6. Verify DMA ring is moving */
    uint32_t t0 = timer_ticks();
    while (chunks < 2 && timer_ticks() - t0 < 100000)
        ;
    if (chunks < 2) {
        irq_disable(IRQ_DMA(dma_ch));
        mmio_write(DMA_CS(dma_ch), CS_RESET);
        mmio_write(PWM_CTL, 0);
        dmb();
        return pwm_fail("PWM audio FIFO does not take data (DMA stalled)");
    }
    *status = "PWM audio 48 kHz (jack 3.5mm, GPIO 40/45)";
    return 0;
}

void pwm_audio_idle(void)
{
}

void pwm_audio_print(void)
{
    dmb();
    kprintf("       PWM DMA channel %u, %lu chunks, %lu errors\n", dma_ch, chunks, errors);
}
