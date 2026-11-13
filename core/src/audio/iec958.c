#include "iec958.h"

void iec958_init(iec958_t *e, uint32_t rate)
{
    uint8_t fs = 2, orig = 13;                  /* 48 kHz */
    if (rate == 44100) { fs = 0; orig = 15; }
    else if (rate == 32000) { fs = 3; orig = 12; }
    e->status[0] = 0x04;        /* consumer, PCM, no copyright, no pre-emphasis */
    e->status[1] = 0;           /* category: general */
    e->status[2] = 0;           /* source and channel number: not given */
    e->status[3] = fs;          /* sampling frequency */
    e->status[4] = (uint8_t)(0x0B | orig << 4); /* 24-bit words, original frequency */
    e->frame = 0;
}

uint32_t iec958_subframe(const iec958_t *e, int32_t sample24, unsigned frame)
{
    uint32_t w = ((uint32_t)sample24 & 0xFFFFFFu) << 4;
    if (frame < 40 && (e->status[frame / 8] >> (frame % 8) & 1))
        w |= 0x40000000u;
    if (__builtin_parity(w))
        w |= 0x80000000u;                       /* even parity over bits 4..31 */
    if (frame == 0)
        w |= IEC958_B_PREAMBLE;
    return w;
}

void iec958_encode(iec958_t *e, const int16_t *in, uint32_t *out, unsigned n)
{
    unsigned frame = e->frame;
    for (unsigned i = 0; i < n; i++) {
        out[2 * i] = iec958_subframe(e, (int32_t)in[2 * i] * 256, frame);
        out[2 * i + 1] = iec958_subframe(e, (int32_t)in[2 * i + 1] * 256, frame);
        if (++frame == IEC958_FRAMES_PER_BLOCK)
            frame = 0;
    }
    e->frame = frame;
}

void iec958_encode32(iec958_t *e, const int32_t *in, uint32_t *out, unsigned n)
{
    unsigned frame = e->frame;
    for (unsigned i = 0; i < n; i++) {
        out[2 * i] = iec958_subframe(e, in[2 * i] >> 8, frame);
        out[2 * i + 1] = iec958_subframe(e, in[2 * i + 1] >> 8, frame);
        if (++frame == IEC958_FRAMES_PER_BLOCK)
            frame = 0;
    }
    e->frame = frame;
}
