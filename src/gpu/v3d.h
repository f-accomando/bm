/*
 * VideoCore IV 3D unit (V3D), without an operating system (M30).
 *
 * The V3D draws triangles in tiles of 64x64 pixels: a binning control
 * list (thread 0) sorts the triangles into one list per tile, a rendering
 * control list (thread 1) then draws every tile in the chip (colour, 24-bit
 * depth) and stores it to memory. The fragments are coloured by small QPU
 * programs (shaders); the vertices come already shaded from the ARM ("NV"
 * shader state: screen x, y in 12.4, z, 1/w, varyings).
 *
 * Addresses given to the V3D are bus addresses: v3d_bus() of a buffer in
 * ARM memory (the L2-cached alias, as for the DMA); the data cache must
 * be cleaned before a job (v3d_run does it for the whole cache).
 * Reference: VideoCore IV 3D Architecture Reference Guide (Broadcom), the
 * vc4 driver of Linux, the bare metal examples of Peter Lemon and kumaashi.
 */
#ifndef V3D_H
#define V3D_H

#include <stddef.h>
#include <stdint.h>

#define V3D_TILE 64                 /* tile size without multisampling */

/* Powers the V3D on (mailbox) and checks that it answers. 0, or -1 with
 * the reason in v3d_status() (QEMU has no V3D). */
int v3d_init(void);
const char *v3d_status(void);
uint32_t v3d_ident(int i);          /* IDENT0..2 */

/* bus address of ARM memory, for the V3D */
uint32_t v3d_bus(const void *p);

/* Memory the binner takes when the tile lists outgrow their allocation
 * (written before every binning job; 0, 0: none). */
void v3d_set_overflow(uint32_t bus, uint32_t size);

/* Runs a job: the binning list [bin, bin_end) if bin_end != bin, then the
 * rendering list [rnd, rnd_end). Waits at most timeout_us for each.
 * 0, or -1 (timeout or error: the registers are kept for v3d_dump). The
 * times of the two parts in *bin_us, *rnd_us when not NULL. */
int v3d_run(uint32_t bin, uint32_t bin_end, uint32_t rnd, uint32_t rnd_end, uint32_t timeout_us,
            uint32_t *bin_us, uint32_t *rnd_us);

/* Registers of the last failed job (or now), one line each. */
void v3d_dump(char *buf, size_t n);

/* ---------------------------------------------------------------- lists */

typedef struct {
    uint8_t *p, *start, *end;
    int overflow;
} v3d_cl_t;

void v3d_cl_init(v3d_cl_t *cl, void *buf, size_t size);
void v3d_cl_u8(v3d_cl_t *cl, uint8_t v);
void v3d_cl_u16(v3d_cl_t *cl, uint16_t v);
void v3d_cl_u32(v3d_cl_t *cl, uint32_t v);
void v3d_cl_f32(v3d_cl_t *cl, float v);

/* control list packets (the ones bm uses) */
#define V3D_HALT                        0
#define V3D_NOP                         1
#define V3D_FLUSH                       4
#define V3D_FLUSH_ALL                   5
#define V3D_START_TILE_BINNING          6
#define V3D_INCREMENT_SEMAPHORE         7
#define V3D_WAIT_ON_SEMAPHORE           8
#define V3D_BRANCH_TO_SUB_LIST          17
#define V3D_STORE_MS_TILE_BUFFER        24
#define V3D_STORE_MS_TILE_BUFFER_EOF    25
#define V3D_STORE_TILE_BUFFER_GENERAL   28
#define V3D_VERTEX_ARRAY_PRIMITIVES     33
#define V3D_NV_SHADER_STATE             65
#define V3D_CONFIGURATION_BITS          96
#define V3D_CLIP_WINDOW                 102
#define V3D_VIEWPORT_OFFSET             103
#define V3D_TILE_BINNING_MODE_CONFIG    112
#define V3D_TILE_RENDERING_MODE_CONFIG  113
#define V3D_CLEAR_COLORS                114
#define V3D_TILE_COORDINATES            115

/* TILE_RENDERING_MODE_CONFIG flags: colour format of the frame in memory */
#define V3D_RENDER_BGR565_DITHER        (0u << 2)
#define V3D_RENDER_RGBA8888             (1u << 2)
#define V3D_RENDER_BGR565               (2u << 2)

/* CONFIGURATION_BITS: first byte, then the 16-bit rest */
#define V3D_CFG_FRONT                   0x01    /* draw front-facing triangles */
#define V3D_CFG_BACK                    0x02    /* draw back-facing triangles */
#define V3D_CFG_CW                      0x04    /* clockwise is front */
#define V3D_CFG_MSAA4                   0x40
#define V3D_CFG_DEPTH(f)                ((uint16_t)((f) << 4))  /* 0 never .. 3 LE .. 7 always */
#define V3D_CFG_Z_UPDATE                0x0080
#define V3D_CFG_EARLY_Z                 0x0100
#define V3D_CFG_EARLY_Z_UPDATE          0x0200

/* TILE_BINNING_MODE_CONFIG flags */
#define V3D_BIN_AUTO_INIT_TSDA          0x04

#endif
