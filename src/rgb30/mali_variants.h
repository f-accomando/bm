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

/* A frame lives in a set of the GPU memory (M41 phase 4: two sets, so the ARM builds frame n+1 while the GPU draws frame n).
 * Set s starts at MALI_SET_BASE(s) (mali.h); inside it: */
#define SO_FJOB         0x0000      /* the Fragment job */
#define SO_FBD          0x0100      /* its framebuffer descriptor */
#define SO_SAMPLES      0x0200
#define SO_HEAPD        0x0800      /* Tiler Heap */
#define SO_TCTX         0x0880      /* Tiler Context */
#define SO_VRSD         0x0a00      /* the vertex shader's Renderer State */
#define SO_LS           0x0b00      /* Local Storage */
#define SO_VIEWPORT     0x0b40
#define SO_ATTR         0x0bc0
#define SO_VATTR        0x0c40      /* the position's varying attribute */
#define SO_VS           0x8000      /* BM_VS */
#define SO_VS3          0x8400      /* BM_VS3 (0x1a0 bytes; 0x8100 would run into SO_FS) */
#define SO_FS           0x8200
#define SO_HEAP         0x80000
#define SO_DRAWS        0x40000
#define SO_SURF         MALI_SET_SURFACE
#define HEAP_SIZE       0x80000
#define MEM_DRAW_SIZE   0x800
#define D_VJOB          0x000       /* Vertex job */
#define D_TJOB          0x100       /* Tiler job */
#define D_FRSD          0x300       /* the fragment shader's Renderer State, then its Blend */
#define D_ABUFS         0x380       /* attribute buffers: the draw's vertices, a continuation, an end */
#define D_VBUFS         0x3c0       /* varying buffers: general, the draw's positions, point size, an end */
#define D_UBO           0x400       /* the fragment shader's uniform buffer descriptor */
#define D_COLOUR        0x440       /* the draw's colour: the buffer's content */
#define D_VUBO          0x480       /* the vertex shader's uniform buffer descriptor (a draw with a matrix) */
#define D_VRSD          0x500       /* its vertex Renderer State */
#define TRI             96          /* the square's side in pixels */
#define HIERARCHY_MASK  0xee        /* Mesa's default_mask[6]: 6 levels, the one over the whole frame */

/* the triangle's blocks in the GPU's memory (offsets from mem): set 0, draw 0 */
#define MV_VJOB         (MALI_SET_BASE(0) + SO_DRAWS + D_VJOB)
#define MV_TJOB         (MALI_SET_BASE(0) + SO_DRAWS + D_TJOB)
#define MV_VBUFS        (MALI_SET_BASE(0) + SO_DRAWS + D_VBUFS)
#define MV_HEAPD        (MALI_SET_BASE(0) + SO_HEAPD)
#define MV_TCTX         (MALI_SET_BASE(0) + SO_TCTX)
#define MV_VRSD         (MALI_SET_BASE(0) + SO_VRSD)
#define MV_HEAP         (MALI_SET_BASE(0) + SO_HEAP)

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
    uint32_t block;         /* offset (from the memory's start) of the block the bits are in */
    uint16_t bit;           /* first bit, from the start of the block */
    uint8_t nbits;
    uint64_t val;
    uint8_t flags;
} mv_table[MV_COUNT] = {
    /* the vertex job's next pointer (job header, byte 24) is 0 */
    { "vertex-only",   MV_VJOB,  24 * 8,           64,  0,             MV_VERTEX_ONLY },
    /* the tiler job's first dependency (job header, byte 20, 16 bits) is 0 */
    { "tiler-only",    MV_TJOB,  20 * 8,           16,  0,             MV_TILER_ONLY },
    /* the tiler heap's bottom (Tiler Heap word 4) is the base + 64 */
    { "heap-bottom64", MV_HEAPD, 4 * 32,           64,  MV_HEAP + 64, MV_PA },
    /* varying buffer entry 0 is a copy of entry 1 (the positions) */
    { "vary0-copy",    MV_VBUFS, 0,                128, 0,             MV_COPY16 },
    /* the Tiler Context's hierarchy mask (word 2, 13 bits) */
    { "mask-ff",       MV_TCTX,  2 * 32,           13,  0xff,          0 },
    /* the Tiler Job's Primitive (at byte 40) word 0, bits 16-17: low and high depth cull */
    { "no-depth-cull", MV_TJOB,  40 * 8 + 16,      2,   0,             0 },
    /* the Tiler Job's Draw (at byte 128) word 14: the renderer state is the vertex shader's */
    { "tiler-vrsd",    MV_TJOB,  128 * 8 + 14 * 32, 64, MV_VRSD,      MV_PA },
    /* the Primitive's word 0, bit 15: first provoking vertex */
    { "provoke-0",     MV_TJOB,  40 * 8 + 15,      1,   0,             0 },
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
 * them, into hw->mem (hw->mem_pa: its address); the fields of hw (screen) set the framebuffer's address */
void mali_tri_build(const mali_hw_t *hw);

#endif
