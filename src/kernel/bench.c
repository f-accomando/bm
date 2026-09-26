#include "bench.h"
#include "gfx/console.h"
#include "lib/crc32.h"
#include "lib/printf.h"
#include "drivers/timer.h"

#include <stdlib.h>
#include <string.h>

#define MEM_SIZE    (1u << 20)
#define CRC_SIZE    (64u << 10)
#define FLOAT_ITERS 100000

static volatile uint32_t sink;
static volatile float fsink;

static uint32_t since(uint32_t t0)
{
    return timer_ticks() - t0;
}

void bench_run(bench_t *b, framebuffer_t *fb, const char *label)
{
    uint8_t *src = malloc(MEM_SIZE), *dst = malloc(MEM_SIZE);
    uint32_t t;

    memset(b, 0, sizeof *b);
    b->label = label;

    console_suspend(1);
    t = timer_ticks();
    fb_fill_rect(fb, 0, 0, fb->width, fb->height, fb_color(fb, 0, 0, 0));
    b->fill_us = since(t);
    console_suspend(0);

    if (src && dst) {
        t = timer_ticks();
        memset(src, 0x5A, MEM_SIZE);
        b->memset_us = since(t);

        t = timer_ticks();
        memcpy(dst, src, MEM_SIZE);
        b->memcpy_us = since(t);

        t = timer_ticks();
        sink = crc32(dst, CRC_SIZE);
        b->crc_us = since(t);
    }
    free(dst);
    free(src);

    float acc = 0.0f, k = 1.0001f;
    t = timer_ticks();
    for (int i = 0; i < FLOAT_ITERS; i++)
        acc = acc * k + 0.5f;
    b->float_us = since(t);
    fsink = acc;
}

static void row(const char *name, const bench_t *b, int n, size_t off)
{
    kprintf("%-15s", name);
    for (int i = 0; i < n; i++)
        kprintf("%15lu", *(const uint32_t *)((const char *)&b[i] + off));
    kprintf("\n");
}

void bench_print(const bench_t *b, int n)
{
    kprintf("\x1b[1m%-15s", "benchmark (us)");
    for (int i = 0; i < n; i++)
        kprintf("%15s", b[i].label);
    kprintf("\x1b[0m\n");
    row("fill 640x360", b, n, offsetof(bench_t, fill_us));
    row("memset 1 MiB", b, n, offsetof(bench_t, memset_us));
    row("memcpy 1 MiB", b, n, offsetof(bench_t, memcpy_us));
    row("crc32 64 KiB", b, n, offsetof(bench_t, crc_us));
    row("float 100k", b, n, offsetof(bench_t, float_us));
}
