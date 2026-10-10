/*
 * IEC 60958 (S/PDIF) subframes, the sample format of the BCM2835 HDMI
 * audio FIFO (MAI): a 24-bit sample in bits 4..27, the channel-status bit
 * in bit 30, even parity in bit 31, and the block-start preamble in bits
 * 0..3 on the two subframes of frame 0. A block is 192 frames.
 */
#ifndef IEC958_H
#define IEC958_H

#include <stdint.h>

#define IEC958_FRAMES_PER_BLOCK 192
#define IEC958_B_PREAMBLE       0x0Fu

typedef struct {
    uint8_t status[5];      /* channel-status bytes, bit 0 first */
    unsigned frame;         /* position in the block, 0..191 */
} iec958_t;

/* Consumer, PCM, 24-bit samples at `rate` (48000, 44100 or 32000). */
void iec958_init(iec958_t *e, uint32_t rate);

/* One 24-bit sample -> its subframe in frame `frame` of the block. */
uint32_t iec958_subframe(const iec958_t *e, int32_t sample24, unsigned frame);

/* n stereo frames (2n samples, left first) -> 2n subframes, continuing
 * the block position. 16-bit samples (the low 8 bits of the word 0) ... */
void iec958_encode(iec958_t *e, const int16_t *in, uint32_t *out, unsigned n);
/* ... or 32-bit words aligned to the left (audio_render32): their top 24
 * bits, all the subframe carries (round them to 24 first: the low byte is
 * dropped). */
void iec958_encode32(iec958_t *e, const int32_t *in, uint32_t *out, unsigned n);

#endif
