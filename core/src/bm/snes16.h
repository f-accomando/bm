/*
 * snes16.h: native ARM SNES-16 PPU + S-DSP engine (no 65C816/SPC700 emulation).
 * Reuses snes_ppu.c (ARMv6 SIMD32 + snes_ppu_arm.S) and snes_dsp.c directly.
 */
#ifndef SNES16_H
#define SNES16_H

#include <stddef.h>
#include <stdint.h>

#include "snes.h"

#define S16_HDMA_FIX    (1u << 0)
#define S16_HDMA_SCROLL (1u << 1)
#define S16_HDMA_M7     (1u << 2)
#define S16_HDMA_MODE   (1u << 3)

typedef struct {
    uint32_t us_ppu;
    uint32_t us_dsp;
    uint32_t us_arm;
} snes16_stats_t;

void s16_reset(int lines);
void s16_set_clock(uint32_t (*now_us)(void));
void s16_record_arm_us(uint32_t us);
void s16_get_stats(snes16_stats_t *out);

/* PPU configuration */
void s16_mode(int bgmode, int tm, int ts, int bright);
void s16_bg(int layer, uint16_t map_byte_addr, uint16_t chr_byte_addr, int size_flags);
void s16_scroll(int layer, int hofs, int vofs);
void s16_obj_cfg(int size_mode, uint16_t base0_bytes, uint16_t gap_bytes);
void s16_colormath(uint8_t cgwsel, uint8_t cgadsub, uint16_t fix_rgb555);
void s16_window(int layer, uint8_t w1_l, uint8_t w1_r, uint8_t w2_l, uint8_t w2_r, uint8_t sel_nibble, uint8_t log2);
void s16_m7(int16_t a, int16_t b, int16_t c, int16_t d, int16_t cx, int16_t cy, int16_t hx, int16_t hy, uint8_t m7sel);

/* VRAM / CGRAM / OAM direct writers */
void s16_vram_write(uint32_t byte_addr, const uint8_t *data, uint32_t len);
void s16_tile_write(uint16_t byte_addr, int bpp, const uint8_t *chunky_8x8);
void s16_map_set(int layer, int tx, int ty, uint16_t entry);
void s16_m7_tile(uint8_t tile_idx, const uint8_t *chunky_8x8);
void s16_m7_map(int mx, int my, uint8_t tile_idx);
void s16_cgram_set(uint8_t idx, uint16_t rgb555);
void s16_oam_set(int id, int x, int y, int tile, int attr);
void s16_oam_clear(int from_id);
void s16_oam_rot(int enable, int first_id);

/* Native scanline HDMA tables */
void s16_hdma_clear(uint32_t mask);
void s16_hdma_grad(int y0, int y1, uint16_t c0_rgb555, uint16_t c1_rgb555);
void s16_hdma_band(int layer, int y0, int y1, int hofs, int vofs);
void s16_hdma_m7_persp(int y0, int y1, int32_t cx, int32_t cy, int32_t angle_q8, int32_t z_q8, int32_t horizon_y);
void s16_hdma_mode_band(int y0, int y1, uint8_t bgmode, uint8_t tm, uint8_t ts);

/* Render frame into S.picture */
void s16_render(void);
const uint16_t *s16_picture(int *lines);

/* Direct S-DSP BRR audio (no SPC700) */
void s16_dsp_init(void);
int s16_dsp_upload_pcm16(int slot, uint16_t brr_addr, const int16_t *pcm, int nsamples, int loop_sample);
void s16_dsp_upload_brr(int slot, uint16_t brr_addr, uint16_t loop_addr, const uint8_t *brr, uint32_t len);
void s16_dsp_play(int voice, int slot, int pitch, int adsr1, int adsr2, int vol_l, int vol_r);
void s16_dsp_stop(int voice);
void s16_dsp_step(int samples32k);

/* Fast C dispatcher for BMN1 native ARMv6 CALL_HOST */
int32_t s16_host_call(int32_t host_id, const int32_t *args, int32_t nargs);

#endif
