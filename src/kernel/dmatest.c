#include "dmatest.h"
#include "arch/cache.h"
#include "drivers/dma.h"
#include "drivers/timer.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define BIG (640 * 360 * 2)             /* one RGB565 frame, 450 KiB */

static int failed;

static void step(const char *what)
{
    kprintf("  %-34s", what);
    timer_delay_ms(40);                 /* on the display before the DMA starts */
}

static void result(int ok, uint32_t cpu_us, uint32_t dma_us)
{
    if (!ok) {
        kprintf("\x1b[91mFAILED\x1b[0m\n");
        failed = 1;
    } else if (cpu_us || dma_us) {
        kprintf("ok  CPU %lu us, DMA %lu us\n", cpu_us, dma_us);
    } else {
        kprintf("ok\n");
    }
}

static int all(const uint8_t *p, uint8_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

void dma_test(framebuffer_t *fb)
{
    failed = 0;
    if (!dma_ready()) {
        kprintf("dma: no channel\n");
        return;
    }
    kprintf("DMA test, channel %d (if the Pi freezes, the last line is the culprit)\n",
            dma_channel());
    uint8_t *a = aligned_alloc(32, BIG), *b = aligned_alloc(32, BIG);
    if (!a || !b) {
        kprintf("dma: out of memory\n");
        free(a); free(b);
        return;
    }
    uint32_t t0, cpu, d;
    int ok;

    step("1 copy RAM -> RAM, 4 KiB");
    for (int i = 0; i < 4096; i++) a[i] = (uint8_t)(i * 7);
    memset(b, 0, 4096);
    dcache_clean_invalidate_all();
    dma_copy(b, a, 4096);
    ok = dma_wait() == 0 && memcmp(a, b, 4096) == 0;
    result(ok, 0, 0);
    if (!ok) goto out;

    step("2 fill RAM, 4 KiB");
    dcache_clean_invalidate_all();
    dma_fill(b, 0x5A5A5A5Au, 4096);
    ok = dma_wait() == 0 && all(b, 0x5A, 4096);
    result(ok, 0, 0);
    if (!ok) goto out;

    step("3 copy RAM -> RAM, 450 KiB");
    for (uint32_t i = 0; i < BIG; i++) a[i] = (uint8_t)(i * 13 + (i >> 9));
    t0 = timer_ticks(); memcpy(b, a, BIG); cpu = timer_ticks() - t0;
    memset(b, 0, BIG);
    t0 = timer_ticks();
    dcache_clean_invalidate_all();
    dma_copy(b, a, BIG);
    ok = dma_wait() == 0;
    d = timer_ticks() - t0;
    result(ok && memcmp(a, b, BIG) == 0, cpu, d);
    if (failed) goto out;

    step("4 fill RAM, 450 KiB");
    t0 = timer_ticks(); memset(b, 0, BIG); cpu = timer_ticks() - t0;
    t0 = timer_ticks();
    dcache_clean_invalidate_all();
    dma_fill(b, 0xA5A5A5A5u, BIG);
    ok = dma_wait() == 0;
    d = timer_ticks() - t0;
    result(ok && all(b, 0xA5, BIG), cpu, d);
    if (failed) goto out;

    /* the console's own page: a band of rows at the bottom of the screen */
    uint32_t rows = 48, bytes = rows * fb->pitch;
    uint8_t *band = fb->base + (fb->height - rows) * fb->pitch;
    step("5 fill screen (grey band)");
    t0 = timer_ticks(); memset(band, 0x30, bytes); cpu = timer_ticks() - t0;
    t0 = timer_ticks();
    dma_fill(band, 0xFF606060u, bytes);
    ok = dma_wait() == 0;
    d = timer_ticks() - t0;
    result(ok && band[bytes - 1] == 0xFF && band[0] == 0x60, cpu, d);
    if (failed) goto out;

    step("6 copy RAM -> screen (pattern)");
    t0 = timer_ticks(); memcpy(band, a, bytes); cpu = timer_ticks() - t0;
    dcache_clean_all();
    t0 = timer_ticks();
    dma_copy(band, a, bytes);
    ok = dma_wait() == 0;
    d = timer_ticks() - t0;
    result(ok && memcmp(band, a, bytes) == 0, cpu, d);
    memset(band, 0, bytes);

out:
    free(a);
    free(b);
    if (failed) {
        kprintf("DMA test \x1b[91mfailed\x1b[0m\n");
    } else {
        /* measured on the Pi: a whole 640x360 frame "via RAM" takes 14.06 ms
         * with the DMA copy against 11.85 ms with the CPU, so frames stay on
         * the CPU (b33_set_dma_frames exists for further experiments) */
        kprintf("DMA test passed\n");
    }
}
