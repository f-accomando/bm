#ifndef GPU_ETC1_H
#define GPU_ETC1_H

#include <stdint.h>

/*
 * ETC1 (OES_compressed_ETC1_RGB8_texture): a block of 4x4 RGB texels in 64
 * bits, bit 63 the first bit of the format's block (its bytes big-endian).
 * bm3d 7.3: the opaque sheets of gpu3d_etc1, made when a sheet's
 * texture is made (gpu3d.c tex_get); the V3D emulator of the tests decodes
 * them with etc1_texel.
 */

/* the block of the 4x4 RGB565 texels at px (rows stride texels apart):
 * both modes (individual, differential), both splits; per half the table
 * and indices nearest in light. Integers only: the same bits everywhere.
 * swap_rb: red and blue swapped first (the TMU's order, learned by the
 * probe) */
uint64_t etc1_encode(const uint16_t *px, int stride, int swap_rb);

/* texel (x, y) of a block, 0..3 each, as 0xRRGGBB */
uint32_t etc1_texel(uint64_t b, int x, int y);

#endif
