/*
 * drivers/fb.h on the AArch64 build: framebuffers in the display and GPU
 * memory at the top of RAM (plat.h), mapped non-cacheable by mmu.c like
 * the Pi's GPU memory, so whatever the CPU draws reaches the display
 * controller (and a GPU) as it is, and the other way round. The layout of
 * every page is one a GPU can render into (display.h). fb_init frees the
 * pages and starts over.
 */
#include "display.h"
#include "drivers/timer.h"
#include "kernel/config.h"

#include <string.h>

static plat_mode_t shown_mode;

/* the menu's and the console's screens: as big as the panel (360x360:
 * every pixel a 2x2 square) */
int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers)
{
    return fb_init_mode(fb, width, height, buffers, 32, FB_FILL, 0);
}

int fb_init_depth(framebuffer_t *fb, uint32_t width, uint32_t height,
                  uint32_t buffers, uint32_t depth)
{
    return fb_init_mode(fb, width, height, buffers, depth, 1, 0);
}

int fb_init_mode(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers,
                 uint32_t depth, uint32_t scale, int smooth)
{
    if (buffers < 1 || buffers > 3)
        buffers = 1;
    if (depth != 16)
        depth = 32;
    if (width < 16 || height < 16 || width > 4096 || height > 4096)
        return -2;
    uint32_t out_w, out_h;
    if (scale == FB_FILL) {             /* as big as fits, the same shape */
        if ((uint64_t)width * PLAT_PANEL_H <= (uint64_t)height * PLAT_PANEL_W) {
            out_h = PLAT_PANEL_H;
            out_w = width * PLAT_PANEL_H / height;
        } else {
            out_w = PLAT_PANEL_W;
            out_h = height * PLAT_PANEL_W / width;
        }
    } else {
        if (scale > 8)
            scale = 8;
        out_w = width * scale;
        out_h = height * scale;
    }
    uint32_t pitch = (width * (depth / 8) + FB_ROW_ALIGN - 1) & ~(FB_ROW_ALIGN - 1);
    uint32_t rows = (height + FB_TILE - 1) & ~(FB_TILE - 1);
    uint32_t page = (pitch * rows + FB_PAGE_ALIGN - 1) & ~(FB_PAGE_ALIGN - 1);
    if ((uint64_t)page * buffers > PLAT_FB_END - PLAT_FB_START)
        return -1;
    fb->width = width;
    fb->height = height;
    fb->pitch = pitch;
    fb->is_rgb = 0;                     /* XRGB8888: B in the low byte */
    fb->mem = (uint8_t *)(uintptr_t)PLAT_FB_START;
    fb->bus = (uint32_t)PLAT_FB_START;  /* identity map: the physical address */
    fb->base = fb->mem;
    fb->size = page * buffers;
    fb->buffers = buffers;
    fb->shown = 0;
    fb->vsync = -1;
    fb->depth = depth;
    for (uint64_t *p = (uint64_t *)fb->mem, *e = (uint64_t *)(fb->mem + fb->size); p < e; p++)
        *p = 0;
    plat_mode_t m = { width, height, depth, pitch, scale, out_w, out_h, smooth };
    if (plat_display_init(&m, (uintptr_t)fb->mem) != 0)
        return -3;
    shown_mode = m;
    return 0;
}

const plat_mode_t *fb_mode(void)
{
    return &shown_mode;
}

int fb_init_game(framebuffer_t *fb, int w, int h)
{
    const char *s = config_get("bm_scale"), *sm = config_get("bm_smooth");
    uint32_t scale = FB_FILL;
    if (s && !strcmp(s, "int")) {
        scale = PLAT_PANEL_W / (uint32_t)w < PLAT_PANEL_H / (uint32_t)h ? PLAT_PANEL_W / (uint32_t)w
                                                                        : PLAT_PANEL_H / (uint32_t)h;
        if (scale == 0)
            scale = 1;
    }
    return fb_init_mode(fb, (uint32_t)w, (uint32_t)h, 3, 16, scale, sm && sm[0] == '1');
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
