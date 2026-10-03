#include "fb.h"
#include "mbox.h"
#include "arch/cache.h"
#include "mmio.h"
#include "prop.h"
#include "timer.h"
#include "dma.h"

#define TAG_ALLOCATE_BUFFER   0x00040001u
#define TAG_GET_PITCH         0x00040008u
#define TAG_SET_PHYS_WH       0x00048003u
#define TAG_SET_VIRT_WH       0x00048004u
#define TAG_SET_DEPTH         0x00048005u
#define TAG_SET_PIXEL_ORDER   0x00048006u
#define TAG_SET_VIRT_OFFSET   0x00048009u
#define TAG_WAIT_FOR_VSYNC    0x0004000Eu

static volatile uint32_t __attribute__((aligned(CACHE_LINE))) msg[48];   /* whole cache lines */

int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t buffers)
{
    return fb_init_depth(fb, width, height, buffers, 32);
}

int fb_init_depth(framebuffer_t *fb, uint32_t width, uint32_t height,
                  uint32_t buffers, uint32_t depth)
{
    int i = 0;

    msg[i++] = 0;                       /* total size, patched below */
    msg[i++] = MBOX_REQUEST;

    msg[i++] = TAG_SET_PHYS_WH;  msg[i++] = 8; msg[i++] = 0;
    msg[i++] = width;            msg[i++] = height;

    if (buffers < 1 || buffers > 3)
        buffers = 1;
    const int virt = i + 3;
    msg[i++] = TAG_SET_VIRT_WH;  msg[i++] = 8; msg[i++] = 0;
    msg[i++] = width;            msg[i++] = height * buffers;

    msg[i++] = TAG_SET_VIRT_OFFSET; msg[i++] = 8; msg[i++] = 0;
    msg[i++] = 0;                msg[i++] = 0;

    const int depth_idx = i + 3;
    msg[i++] = TAG_SET_DEPTH;    msg[i++] = 4; msg[i++] = 0;
    msg[i++] = depth;

    const int porder = i + 3;
    msg[i++] = TAG_SET_PIXEL_ORDER; msg[i++] = 4; msg[i++] = 0;
    msg[i++] = 1;                       /* request RGB */

    const int alloc = i + 3;
    msg[i++] = TAG_ALLOCATE_BUFFER; msg[i++] = 8; msg[i++] = 0;
    msg[i++] = 4096;                    /* alignment in, address out */
    msg[i++] = 0;                       /* size out */

    const int pitch = i + 3;
    msg[i++] = TAG_GET_PITCH;    msg[i++] = 4; msg[i++] = 0;
    msg[i++] = 0;

    msg[i++] = MBOX_TAG_LAST;
    msg[0] = (uint32_t)i * 4;

    if (!mbox_call(MBOX_CH_PROP, msg))
        return -1;
    if (msg[5] == 0 || msg[6] == 0 || msg[alloc] == 0 || msg[pitch] == 0)
        return -2;

    fb->width  = msg[5];
    fb->height = msg[6];
    fb->pitch  = msg[pitch];
    fb->is_rgb = msg[porder];
    fb->mem    = (uint8_t *)BUS_TO_ARM(msg[alloc]);
    fb->base   = fb->mem;
    fb->size   = msg[alloc + 1];
    fb->buffers = msg[virt + 1] / fb->height;
    if (fb->buffers > buffers) fb->buffers = buffers;
    if (fb->buffers < 1) fb->buffers = 1;
    fb->shown  = 0;
    fb->vsync  = -1;
    fb->depth  = msg[depth_idx];
    fb->bus    = msg[alloc];
    dma_map_region((uint32_t)fb->mem, fb->size, msg[alloc]);
    if (fb->depth != depth)
        return -3;
    return 0;
}

/* Plain (non-volatile) stores: the framebuffer is normal memory, so the
 * compiler may use STM bursts, which the write buffer merges. */
static void fill32(uint32_t *p, uint32_t v, uint32_t n)
{
    while (n >= 8) {
        p[0] = v; p[1] = v; p[2] = v; p[3] = v;
        p[4] = v; p[5] = v; p[6] = v; p[7] = v;
        p += 8;
        n -= 8;
    }
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

static void set_offset(uint32_t y)
{
    uint32_t v[2] = { 0, y };
    prop_query(TAG_SET_VIRT_OFFSET, v, 2);
}

void fb_show(framebuffer_t *fb, uint32_t index)
{
    if (index >= fb->buffers)
        index = 0;
    set_offset(index * fb->height);
    fb->shown = index;
    fb->base = fb->mem + index * fb->height * fb->pitch;
}

int fb_flip(framebuffer_t *fb)
{
    if (fb->buffers < 2)
        return 0;

    uint32_t drawn = (uint32_t)(fb->base - fb->mem) / (fb->height * fb->pitch);
    set_offset(drawn * fb->height);
    fb->shown = drawn;

    if (fb->vsync != 0) {
        /* Emulators (QEMU) may accept the tag but return at once. Two
         * vblanks less than 8 ms apart cannot happen on a real display
         * (<= 120 Hz): after a few of those, fall back to timer pacing. */
        static uint32_t last, fast;
        uint32_t v[1] = { 0 };
        int ok = prop_query(TAG_WAIT_FOR_VSYNC, v, 1) == 0;
        uint32_t now = timer_ticks();
        if (ok && fb->vsync == 1 && now - last < 8000)
            ok = ++fast < 3;
        last = now;
        fb->vsync = ok;
    }

    /* with three pages the next one was on screen two flips ago: even if
     * the firmware applies the new offset only at the next vertical blank,
     * nothing is drawn into a page still being scanned out */
    fb->base = fb->mem + ((drawn + 1) % fb->buffers) * fb->height * fb->pitch;
    return fb->vsync;
}

int fb_vsync_probe(uint32_t *us, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v[1] = { 0 };
        uint32_t t0 = timer_ticks();
        if (prop_query(TAG_WAIT_FOR_VSYNC, v, 1) != 0)
            return -1;
        us[i] = timer_ticks() - t0;
    }
    return 0;
}
