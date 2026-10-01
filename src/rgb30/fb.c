/*
 * drivers/fb.h on the AArch64 build: framebuffers in a fixed region at the
 * top of RAM (plat.h), mapped non-cacheable like the Pi's GPU memory, so
 * whatever is drawn reaches the display controller without cache flushes.
 * Pages are allocated one after the other; fb_init frees and starts over.
 */
#include "drivers/fb.h"
#include "drivers/timer.h"
#include "plat.h"
#include "arch/mmu.h"

void mmu_set_uncached(uintptr_t start, uintptr_t end);

static int region_ready;

int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers)
{
    return fb_init_depth(fb, width, height, buffers, 32);
}

int fb_init_depth(framebuffer_t *fb, uint32_t width, uint32_t height,
                  uint32_t buffers, uint32_t depth)
{
    if (!region_ready) {
        mmu_set_uncached(PLAT_FB_START, PLAT_FB_END);
        region_ready = 1;
    }
    if (buffers < 1 || buffers > 3)
        buffers = 1;
    if (depth != 16)
        depth = 32;
    uint32_t pitch = width * (depth / 8);    /* the display wants it packed */
    if (pitch & 3)
        return -2;
    uint32_t page = (pitch * height + 4095) & ~4095u;
    if ((uint64_t)page * buffers > PLAT_FB_END - PLAT_FB_START)
        return -1;
    fb->width = width;
    fb->height = height;
    fb->pitch = pitch;
    fb->is_rgb = 0;                     /* XRGB8888: B in the low byte */
    fb->mem = (uint8_t *)(uintptr_t)PLAT_FB_START;
    fb->base = fb->mem;
    fb->size = page * buffers;
    fb->buffers = buffers;
    fb->shown = 0;
    fb->vsync = -1;
    fb->depth = depth;
    for (uint32_t *p = (uint32_t *)fb->mem, *e = (uint32_t *)(fb->mem + fb->size); p < e; p++)
        *p = 0;
    return plat_display_init(width, height, depth, (uintptr_t)fb->mem) == 0 ? 0 : -3;
}

static void fill32(uint32_t *p, uint32_t v, uint32_t n)
{
    while (n--)
        *p++ = v;
}

void fb_fill_rect(framebuffer_t *fb, uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h, uint32_t color)
{
    if (x >= fb->width || y >= fb->height)
        return;
    if (w > fb->width - x)
        w = fb->width - x;
    if (h > fb->height - y)
        h = fb->height - y;
    for (uint32_t row = y; row < y + h; row++)
        fill32((uint32_t *)(fb->base + row * fb->pitch) + x, color, w);
}

static uint8_t *page_addr(framebuffer_t *fb, uint32_t i)
{
    return fb->mem + i * (fb->size / fb->buffers);
}

void fb_show(framebuffer_t *fb, uint32_t index)
{
    if (index >= fb->buffers)
        index = 0;
    plat_display_show((uintptr_t)page_addr(fb, index));
    fb->shown = index;
    fb->base = page_addr(fb, index);
}

int fb_flip(framebuffer_t *fb)
{
    if (fb->buffers < 2)
        return 0;
    uint32_t drawn = (uint32_t)(fb->base - fb->mem) / (fb->size / fb->buffers);
    plat_display_show((uintptr_t)page_addr(fb, drawn));
    fb->shown = drawn;
    fb->vsync = plat_display_wait_vsync();
    fb->base = page_addr(fb, (drawn + 1) % fb->buffers);
    return fb->vsync;
}

int fb_vsync_probe(uint32_t *us, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t t0 = timer_ticks();
        if (!plat_display_wait_vsync())
            return -1;
        us[i] = timer_ticks() - t0;
    }
    return 0;
}
