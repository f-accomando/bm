#include "dma.h"
#include "mmio.h"
#include "prop.h"
#include "timer.h"
#include "arch/cache.h"

#define DMA_BASE            (PERIPHERAL_BASE + 0x007000)
#define DMA_CS(c)           (DMA_BASE + (c) * 0x100 + 0x00)
#define DMA_CONBLK_AD(c)    (DMA_BASE + (c) * 0x100 + 0x04)
#define DMA_DEBUG(c)        (DMA_BASE + (c) * 0x100 + 0x20)
#define DMA_ENABLE          (DMA_BASE + 0xFF0)

#define CS_ACTIVE           (1u << 0)
#define CS_END              (1u << 1)
#define CS_INT              (1u << 2)
#define CS_ERROR            (1u << 8)
#define CS_PRIORITY(p)      ((uint32_t)(p) << 16)
#define CS_PANIC_PRIORITY(p) ((uint32_t)(p) << 20)
#define CS_WAIT_WRITES      (1u << 28)
#define CS_RESET            (1u << 31)
#define DEBUG_LITE          (1u << 28)

#define TI_WAIT_RESP        (1u << 3)
#define TI_DEST_INC         (1u << 4)
#define TI_DEST_WIDTH       (1u << 5)       /* 128-bit writes */
#define TI_SRC_INC          (1u << 8)
#define TI_SRC_WIDTH        (1u << 9)       /* 128-bit reads */
#define TI_BURST(n)         ((uint32_t)(n) << 12)
#define PROP_GET_DMA_CHANNELS 0x00060001u

typedef struct {
    uint32_t ti, src, dst, len, stride, next, pad[2];
} dma_cb_t;

static dma_cb_t cb __attribute__((aligned(32)));
static uint32_t pattern[8] __attribute__((aligned(32)));
static uint32_t claimed;
static int ch = -1;
static int pending;
static uint32_t region_arm, region_len, region_bus;

void dma_map_region(uint32_t arm_start, uint32_t len, uint32_t bus_start)
{
    region_arm = arm_start;
    region_len = len;
    region_bus = bus_start;
}

static uint32_t bus(const void *p)
{
    uint32_t a = (uint32_t)p;
    if (a - region_arm < region_len)
        return region_bus + (a - region_arm);
    return ARM_TO_BUS(a);
}

int dma_channel_claim(const uint8_t *pref, unsigned n)
{
    static uint32_t mask;
    if (!mask) {
        uint32_t v[1] = { 0 };
        mask = prop_query(PROP_GET_DMA_CHANNELS, v, 1) == 0 && v[0] ? v[0] : 0x7F35;
    }
    for (unsigned i = 0; i < n; i++) {
        uint32_t bit = 1u << pref[i];
        if ((mask & bit) && !(claimed & bit)) {
            claimed |= bit;
            return pref[i];
        }
    }
    return -1;
}

int dma_init(void)
{
    if (ch >= 0)
        return 0;
    static const uint8_t full[] = { 4, 2, 0, 5, 1, 3, 6 };
    ch = dma_channel_claim(full, sizeof full);
    if (ch < 0)
        return -1;
    dmb();
    if (mmio_read(DMA_DEBUG(ch)) & DEBUG_LITE) {    /* should not happen */
        ch = -1;
        return -1;
    }
    mmio_write(DMA_ENABLE, mmio_read(DMA_ENABLE) | 1u << ch);
    mmio_write(DMA_CS(ch), CS_RESET);
    timer_delay_us(10);
    mmio_write(DMA_CS(ch), CS_INT | CS_END);
    dmb();
    return 0;
}

int dma_ready(void)
{
    return ch >= 0;
}

int dma_channel(void)
{
    return ch;
}

int dma_busy(void)
{
    if (!pending)
        return 0;
    dmb();
    uint32_t cs = mmio_read(DMA_CS(ch));
    dmb();
    if (cs & CS_ACTIVE)
        return 1;
    pending = 0;
    return 0;
}

void dma_wait(void)
{
    if (!pending)
        return;
    uint32_t t0 = timer_ticks();
    while (dma_busy())
        if (timer_ticks() - t0 > 100000) {       /* 100 ms: a stuck transfer */
            mmio_write(DMA_CS(ch), CS_RESET);
            pending = 0;
            break;
        }
    dmb();
}

static void start(uint32_t ti, uint32_t src, void *dst, uint32_t len)
{
    dma_wait();
    cb.ti = ti | TI_WAIT_RESP | TI_BURST(8);
    cb.src = src;
    cb.dst = bus(dst);
    cb.len = len;
    cb.stride = 0;
    cb.next = 0;
    dcache_clean_range(&cb, sizeof cb);
    dmb();
    mmio_write(DMA_CS(ch), CS_INT | CS_END);
    mmio_write(DMA_CONBLK_AD(ch), ARM_TO_BUS(&cb));
    mmio_write(DMA_CS(ch), CS_WAIT_WRITES | CS_PANIC_PRIORITY(15) | CS_PRIORITY(8) | CS_ACTIVE);
    dmb();
    pending = 1;
}

void dma_fill(void *dst, uint32_t value, uint32_t len)
{
    if (!len)
        return;
    dma_wait();
    for (int i = 0; i < 8; i++)
        pattern[i] = value;
    dcache_clean_range(pattern, sizeof pattern);
    /* the source does not move: the same 16 bytes are read again and again */
    uint32_t wide = ((uint32_t)dst | len) & 15 ? 0 : TI_DEST_WIDTH;
    start(TI_DEST_INC | wide | TI_SRC_WIDTH, ARM_TO_BUS(pattern), dst, len);
}

void dma_copy(void *dst, const void *src, uint32_t len)
{
    if (!len)
        return;
    uint32_t wide = ((uint32_t)dst | (uint32_t)src | len) & 15 ? 0 : TI_DEST_WIDTH | TI_SRC_WIDTH;
    start(TI_DEST_INC | TI_SRC_INC | wide, bus(src), dst, len);
}
