/*
 * The Mali triangle's vertex and tiler jobs, tried in 8 variants (see mali.c).
 * A variant is one patch of the built descriptors: the bits of one block of
 * the GPU's memory are cleared, then set to a value. Pure data and bit
 * work, no hardware: tests/rgb30/mali_test.c checks every patch.
 */
#ifndef RGB30_MALI_VARIANTS_H
#define RGB30_MALI_VARIANTS_H

#include <stdint.h>
#include <string.h>

#include "mali.h"

/* the triangle's blocks in the GPU's memory (offsets from mem) */
#define MEM_VJOB        0x4000
#define MEM_TJOB        0x4100
#define MEM_HEAPD       0x4300      /* Tiler Heap */
#define MEM_TCTX        0x4380      /* Tiler Context */
#define MEM_VRSD        0x4500      /* the vertex shader's Renderer State */
#define MEM_FRSD        0x4580      /* the fragment shader's, then its Blend */
#define MEM_LS          0x4600      /* Local Storage */
#define MEM_VIEWPORT    0x4640
#define MEM_ABUFS       0x4680      /* attribute buffers: the vertices, a continuation, an end */
#define MEM_ATTR        0x46c0
#define MEM_VBUFS       0x4700      /* varying buffers: general, position, point size, an end */
#define MEM_VATTR       0x4740      /* the position's varying attribute */
#define MEM_UBO         0x4780      /* the fragment shader's uniform buffer descriptor */
#define MEM_PUSH        0x47c0      /* its pushed words */
#define MEM_COLOUR      0x47e0      /* the draw's colour: the buffer's content */
#define MEM_VS          0x4800
#define MEM_FS          0x4900
#define MEM_VERTS       0x5000      /* three positions (x y z w floats) */
#define MEM_POSITIONS   0x5100      /* what the vertex job wrote */
#define MEM_HEAP        0x100000
#define HEAP_SIZE       0x100000
#define TRI             96          /* the square's side in pixels */
#define HIERARCHY_MASK  0xee        /* Mesa's default_mask[6]: 6 levels, the one over the whole frame */

/* flags of a patch */
enum {
    MV_PA = 1,              /* val is an offset in the GPU's memory: the patch adds the memory's address */
    MV_COPY16 = 2,          /* the 16 bytes at block + 16 are copied to block (val is not used) */
    MV_VERTEX_ONLY = 4,     /* run the vertex job alone (an isolation run: it never counts as the one that works) */
    MV_TILER_ONLY = 8       /* the CPU writes the positions, the tiler job runs alone (an isolation run) */
};

#define MV_COUNT 8
#define MV_ISOLATE (MV_VERTEX_ONLY | MV_TILER_ONLY)

/* bit positions: a descriptor's word w, bit b is bit 32 * w + b of its block */
static const struct mv_patch {
    const char *name;
    uint16_t block;         /* MEM_* offset of the block the bits are in */
    uint16_t bit;           /* first bit, from the start of the block */
    uint8_t nbits;
    uint64_t val;
    uint8_t flags;
} mv_table[MV_COUNT] = {
    /* the vertex job's next pointer (job header, byte 24) is 0 */
    { "vertex-only",   MEM_VJOB,  24 * 8,           64,  0,             MV_VERTEX_ONLY },
    /* the tiler job's first dependency (job header, byte 20, 16 bits) is 0 */
    { "tiler-only",    MEM_TJOB,  20 * 8,           16,  0,             MV_TILER_ONLY },
    /* the tiler heap's bottom (Tiler Heap word 4) is the base + 64 */
    { "heap-bottom64", MEM_HEAPD, 4 * 32,           64,  MEM_HEAP + 64, MV_PA },
    /* varying buffer entry 0 is a copy of entry 1 (the positions) */
    { "vary0-copy",    MEM_VBUFS, 0,                128, 0,             MV_COPY16 },
    /* the Tiler Context's hierarchy mask (word 2, 13 bits) */
    { "mask-ff",       MEM_TCTX,  2 * 32,           13,  0xff,          0 },
    /* the Tiler Job's Primitive (at byte 40) word 0, bits 16-17: low and high depth cull */
    { "no-depth-cull", MEM_TJOB,  40 * 8 + 16,      2,   0,             0 },
    /* the Tiler Job's Draw (at byte 128) word 14: the renderer state is the vertex shader's */
    { "tiler-vrsd",    MEM_TJOB,  128 * 8 + 14 * 32, 64, MEM_VRSD,      MV_PA },
    /* the Primitive's word 0, bit 15: first provoking vertex */
    { "provoke-0",     MEM_TJOB,  40 * 8 + 15,      1,   0,             0 },
};

/* clear the bits [start, start + n) of the block at p, then set them to the low n bits of v */
static inline void mv_put(uint8_t *p, unsigned start, unsigned n, uint64_t v)
{
    for (unsigned i = 0; i < n; i++) {
        const unsigned b = start + i;
        p[b >> 3] = (uint8_t)(p[b >> 3] & ~(1u << (b & 7)));
        if (i < 64 && (v >> i & 1))
            p[b >> 3] |= (uint8_t)(1u << (b & 7));
    }
}

/* patch variant n into the GPU's memory mem (mem_pa: its address); safe to apply twice */
static inline void mv_apply(uint8_t *mem, uint64_t mem_pa, int n)
{
    const struct mv_patch *t = &mv_table[n];
    uint8_t *blk = mem + t->block;
    if (t->flags & MV_COPY16) {
        memcpy(blk, blk + 16, 16);
        return;
    }
    mv_put(blk, t->bit, t->nbits, t->val + ((t->flags & MV_PA) ? mem_pa : 0));
}

/* the vertex and tiler jobs, the tables they use and the fragment job's descriptor, as the base path builds
 * them, into mem (mem_pa: its address); the fields of hw (screen) set the framebuffer's address */
void mali_tri_build(const mali_hw_t *hw, uint8_t *mem, uint64_t mem_pa);

#endif
