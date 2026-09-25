#include "fb.h"
#include "mbox.h"
#include "mmio.h"

#define TAG_ALLOCATE_BUFFER   0x00040001u
#define TAG_GET_PITCH         0x00040008u
#define TAG_SET_PHYS_WH       0x00048003u
#define TAG_SET_VIRT_WH       0x00048004u
#define TAG_SET_DEPTH         0x00048005u
#define TAG_SET_PIXEL_ORDER   0x00048006u
#define TAG_SET_VIRT_OFFSET   0x00048009u

static volatile uint32_t __attribute__((aligned(16))) msg[36];

int fb_init(framebuffer_t *fb, uint32_t width, uint32_t height)
{
    int i = 0;

    msg[i++] = 0;                       /* total size, patched below */
    msg[i++] = MBOX_REQUEST;

    msg[i++] = TAG_SET_PHYS_WH;  msg[i++] = 8; msg[i++] = 0;
    msg[i++] = width;            msg[i++] = height;

    msg[i++] = TAG_SET_VIRT_WH;  msg[i++] = 8; msg[i++] = 0;
    msg[i++] = width;            msg[i++] = height;

    msg[i++] = TAG_SET_VIRT_OFFSET; msg[i++] = 8; msg[i++] = 0;
    msg[i++] = 0;                msg[i++] = 0;

    msg[i++] = TAG_SET_DEPTH;    msg[i++] = 4; msg[i++] = 0;
    msg[i++] = 32;

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
    fb->base   = (uint8_t *)BUS_TO_ARM(msg[alloc]);
    fb->size   = msg[alloc + 1];
    return 0;
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

    for (uint32_t row = y; row < y + h; row++) {
        volatile uint32_t *p = (volatile uint32_t *)(fb->base + row * fb->pitch) + x;
        for (uint32_t col = 0; col < w; col++)
            p[col] = color;
    }
}
