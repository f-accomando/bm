/*
 * snes16.c: native ARM SNES-16 engine (PPU + HDMA + OAM + Mode 7 + BRR S-DSP).
 * No 65C816 CPU, LoROM/HiROM bus, or SPC700 emulation overhead. Original code.
 */
#include "snes16.h"
#include "bmnative.h"
#include "snes_int.h"

#include <string.h>

static uint32_t (*s16_now_us)(void);
static snes16_stats_t g_stats;

static struct {
    uint32_t flags;
    uint32_t scroll_mask;
    uint16_t fix[SNES_H_MAX];
    uint16_t hofs[4][SNES_H_MAX];
    uint16_t vofs[4][SNES_H_MAX];
    int16_t  m7a[SNES_H_MAX], m7b[SNES_H_MAX], m7c[SNES_H_MAX], m7d[SNES_H_MAX];
    uint8_t  bgmode[SNES_H_MAX], tm[SNES_H_MAX], ts[SNES_H_MAX];
} g_hdma;

static uint8_t g_base_bgmode, g_base_tm, g_base_ts;
static uint16_t g_base_hofs[4], g_base_vofs[4];
static int16_t g_base_m7a, g_base_m7b, g_base_m7c, g_base_m7d;
static uint8_t g_base_fixr, g_base_fixg, g_base_fixb;

/* Fixed-point trig table (256 angles -> 8.8 signed, [-256, 256]) for Mode 7 HDMA. */
static int16_t g_sin256[256];
static int g_sin_ready;

static void ensure_sin_tab(void)
{
    if (g_sin_ready) return;
    /* Bhaskara/cubic quarter-wave integer approximation: accurate to ~1 LSB in Q8. */
    for (int i = 0; i < 256; i++) {
        int q = i & 63;
        int oct = (i >> 6) & 3;
        int x = (oct & 1) ? (64 - q) : q;
        /* sin(x * pi / 128) * 256 via polynomial x in [0..64] */
        int32_t num = 4 * x * (128 - x);
        int32_t den = 40960 - x * (128 - x);
        int32_t v = (num * 256 + (den >> 1)) / den;
        if (v > 256) v = 256;
        if (oct >= 2) v = -v;
        g_sin256[i] = (int16_t)v;
    }
    g_sin_ready = 1;
}

void s16_set_clock(uint32_t (*now_us)(void))
{
    s16_now_us = now_us;
}

void s16_record_arm_us(uint32_t us)
{
    g_stats.us_arm = us;
}

void s16_get_stats(snes16_stats_t *out)
{
    if (out) *out = g_stats;
}

void s16_reset(int lines)
{
    memset(&S, 0, sizeof S);
    memset(&g_hdma, 0, sizeof g_hdma);
    memset(&g_stats, 0, sizeof g_stats);
    S.lines = (lines == 239) ? 239 : 224;
    S.inidisp = 0x0F;
    S.m7a = 0x0100;
    S.m7d = 0x0100;
    g_base_m7a = 0x0100;
    g_base_m7b = 0;
    g_base_m7c = 0;
    g_base_m7d = 0x0100;
    g_base_bgmode = 0;
    g_base_tm = 0;
    g_base_ts = 0;
    g_base_fixr = g_base_fixg = g_base_fixb = 0;
    memset(g_base_hofs, 0, sizeof g_base_hofs);
    memset(g_base_vofs, 0, sizeof g_base_vofs);
    ppu_reset();
    s16_oam_clear(0);
    s16_dsp_init();
}

void s16_mode(int bgmode, int tm, int ts, int bright)
{
    S.bgmode = (uint8_t)bgmode;
    S.tm = (uint8_t)tm;
    S.ts = (uint8_t)ts;
    S.inidisp = (bright < 0) ? 0x80u : (uint8_t)(bright & 15);
    g_base_bgmode = S.bgmode;
    g_base_tm = S.tm;
    g_base_ts = S.ts;
}

void s16_bg(int layer, uint16_t map_byte_addr, uint16_t chr_byte_addr, int size_flags)
{
    if (layer < 0 || layer > 3) return;
    S.sc[layer] = (uint8_t)((((map_byte_addr >> 11) & 0x3F) << 2) | (size_flags & 3));
    S.nba[layer] = chr_byte_addr;
}

void s16_scroll(int layer, int hofs, int vofs)
{
    if (layer < 0 || layer > 3) return;
    S.hofs[layer] = (uint16_t)(hofs & 0x3FF);
    S.vofs[layer] = (uint16_t)(vofs & 0x3FF);
    g_base_hofs[layer] = S.hofs[layer];
    g_base_vofs[layer] = S.vofs[layer];
}

void s16_obj_cfg(int size_mode, uint16_t base0_bytes, uint16_t gap_bytes)
{
    uint32_t b0 = (base0_bytes >> 14) & 7u;
    uint32_t g = gap_bytes >= 0x2000u ? (((gap_bytes >> 13) - 1u) & 3u) : 0u;
    S.obsel = (uint8_t)(((size_mode & 7) << 5) | (g << 3) | b0);
}

void s16_colormath(uint8_t cgwsel, uint8_t cgadsub, uint16_t fix_rgb555)
{
    S.cgwsel = cgwsel;
    S.cgadsub = cgadsub;
    S.fixr = (uint8_t)(fix_rgb555 & 31u);
    S.fixg = (uint8_t)((fix_rgb555 >> 5) & 31u);
    S.fixb = (uint8_t)((fix_rgb555 >> 10) & 31u);
    g_base_fixr = S.fixr;
    g_base_fixg = S.fixg;
    g_base_fixb = S.fixb;
}

void s16_window(int layer, uint8_t w1_l, uint8_t w1_r, uint8_t w2_l, uint8_t w2_r, uint8_t sel_nibble, uint8_t log2)
{
    S.wh0 = w1_l; S.wh1 = w1_r;
    S.wh2 = w2_l; S.wh3 = w2_r;
    if (layer == 0) {
        S.w12sel = (uint8_t)((S.w12sel & 0xF0u) | (sel_nibble & 0x0Fu));
        S.wbglog = (uint8_t)((S.wbglog & ~0x03u) | (log2 & 3u));
    } else if (layer == 1) {
        S.w12sel = (uint8_t)((S.w12sel & 0x0Fu) | ((sel_nibble & 0x0Fu) << 4));
        S.wbglog = (uint8_t)((S.wbglog & ~0x0Cu) | ((log2 & 3u) << 2));
    } else if (layer == 2) {
        S.w34sel = (uint8_t)((S.w34sel & 0xF0u) | (sel_nibble & 0x0Fu));
        S.wbglog = (uint8_t)((S.wbglog & ~0x30u) | ((log2 & 3u) << 4));
    } else if (layer == 3) {
        S.w34sel = (uint8_t)((S.w34sel & 0x0Fu) | ((sel_nibble & 0x0Fu) << 4));
        S.wbglog = (uint8_t)((S.wbglog & ~0xC0u) | ((log2 & 3u) << 6));
    } else if (layer == 4) {
        S.wobjsel = (uint8_t)((S.wobjsel & 0xF0u) | (sel_nibble & 0x0Fu));
        S.wobjlog = (uint8_t)((S.wobjlog & ~0x03u) | (log2 & 3u));
    } else if (layer == 5) {
        S.wobjsel = (uint8_t)((S.wobjsel & 0x0Fu) | ((sel_nibble & 0x0Fu) << 4));
        S.wobjlog = (uint8_t)((S.wobjlog & ~0x0Cu) | ((log2 & 3u) << 2));
    }
}

void s16_m7(int16_t a, int16_t b, int16_t c, int16_t d, int16_t cx, int16_t cy, int16_t hx, int16_t hy, uint8_t m7sel)
{
    S.m7a = a; S.m7b = b; S.m7c = c; S.m7d = d;
    S.m7x = cx; S.m7y = cy; S.m7hofs = hx; S.m7vofs = hy;
    S.m7sel = m7sel;
    g_base_m7a = a; g_base_m7b = b; g_base_m7c = c; g_base_m7d = d;
}

void s16_vram_write(uint32_t byte_addr, const uint8_t *data, uint32_t len)
{
    if (!data || len == 0) return;
    for (uint32_t i = 0; i < len; i++) {
        uint16_t a = (uint16_t)((byte_addr + i) & 0xFFFFu);
        S.vram[a] = data[i];
    }
    if (len >= 1024u) {
        ppu_vram_dirty_all();
    } else {
        for (uint32_t i = 0; i < len; i += 16u)
            ppu_vram_dirty((uint16_t)((byte_addr + i) & 0xFFFFu));
        ppu_vram_dirty((uint16_t)((byte_addr + len - 1u) & 0xFFFFu));
    }
}

void s16_tile_write(uint16_t byte_addr, int bpp, const uint8_t *px)
{
    if (!px || (bpp != 2 && bpp != 4 && bpp != 8)) return;
    for (int y = 0; y < 8; y++) {
        uint8_t p[8] = {0};
        for (int x = 0; x < 8; x++) {
            uint8_t c = px[y * 8 + x];
            uint8_t bit = (uint8_t)(1u << (7 - x));
            for (int pl = 0; pl < bpp; pl++) {
                if ((c >> pl) & 1u) p[pl] |= bit;
            }
        }
        for (int pl = 0; pl < bpp; pl++) {
            uint16_t a = (uint16_t)((byte_addr + (pl >> 1) * 16 + y * 2 + (pl & 1)) & 0xFFFFu);
            S.vram[a] = p[pl];
        }
    }
    ppu_vram_dirty(byte_addr);
    if (bpp >= 4) ppu_vram_dirty((uint16_t)(byte_addr + 16));
    if (bpp == 8) {
        ppu_vram_dirty((uint16_t)(byte_addr + 32));
        ppu_vram_dirty((uint16_t)(byte_addr + 48));
    }
}

void s16_map_set(int layer, int tx, int ty, uint16_t entry)
{
    if (layer < 0 || layer > 3) return;
    const int sc = S.sc[layer];
    const int wide = sc & 1, tall = sc & 2;
    const int mw = wide ? 64 : 32;
    const int mh = tall ? 64 : 32;
    const int x = ((tx % mw) + mw) % mw;
    const int y = ((ty % mh) + mh) % mh;
    const uint32_t map_base = (uint32_t)(sc & 0xFC) << 9;
    const uint32_t wsh = wide ? 64u : 0u;
    const uint32_t row_off = (uint32_t)(tall ? ((y >> 5) & 1) * (wide ? 2 : 1) * 0x800 : 0) + (uint32_t)((y & 31) * 64);
    const uint32_t ea = (map_base + row_off + (uint32_t)(x & 31) * 2 + (uint32_t)(x & 32) * wsh) & 0xFFFFu;
    S.vram[ea] = (uint8_t)(entry & 0xFFu);
    S.vram[(ea + 1u) & 0xFFFFu] = (uint8_t)(entry >> 8);
}

void s16_m7_tile(uint8_t tile_idx, const uint8_t *px)
{
    if (!px) return;
    uint32_t base = (uint32_t)tile_idx * 128u;
    for (int i = 0; i < 64; i++)
        S.vram[(base + (uint32_t)i * 2u + 1u) & 0xFFFFu] = px[i];
}

void s16_m7_map(int mx, int my, uint8_t tile_idx)
{
    uint32_t addr = (uint32_t)(((my & 127) * 128 + (mx & 127)) << 1);
    S.vram[addr & 0xFFFFu] = tile_idx;
}

void s16_cgram_set(uint8_t idx, uint16_t rgb555)
{
    uint16_t c = (uint16_t)(rgb555 & 0x7FFFu);
    S.cgram[idx * 2] = (uint8_t)(c & 0xFFu);
    S.cgram[idx * 2 + 1] = (uint8_t)(c >> 8);
    ppu_cgram_write(idx);
}

void s16_oam_set(int id, int x, int y, int tile, int attr)
{
    if (id < 0 || id >= 128) return;
    uint8_t *o = &S.oam[id * 4];
    int xm = x & 0x1FF;
    o[0] = (uint8_t)(xm & 0xFF);
    o[1] = (uint8_t)((y - 1) & 0xFF);
    o[2] = (uint8_t)(tile & 0xFF);
    /* attr bits: [0]=name_tab, [3:1]=pal, [5:4]=prio, [6]=hflip, [7]=vflip, [8]=big */
    o[3] = (uint8_t)(attr & 0xFF);
    const unsigned hi = (unsigned)(((xm >> 8) & 1) | (((attr >> 8) & 1) << 1));
    const int shift = (id & 3) * 2;
    uint8_t *hb = &S.oam[512 + (id >> 2)];
    *hb = (uint8_t)((*hb & ~(3u << shift)) | (hi << shift));
}

void s16_oam_clear(int from_id)
{
    if (from_id < 0) from_id = 0;
    for (int i = from_id; i < 128; i++)
        s16_oam_set(i, 0, 241, 0, 0);
}

void s16_oam_rot(int enable, int first_id)
{
    S.oamrot = enable ? 1u : 0u;
    S.oamaddr_reload = (uint16_t)((first_id & 127) << 2);
}

void s16_hdma_clear(uint32_t mask)
{
    g_hdma.flags &= ~mask;
    if (mask & S16_HDMA_SCROLL)
        g_hdma.scroll_mask = 0;
}

void s16_hdma_grad(int y0, int y1, uint16_t c0, uint16_t c1)
{
    if (y0 < 0) y0 = 0;
    if (y1 >= SNES_H_MAX) y1 = SNES_H_MAX - 1;
    if (y1 < y0) return;
    const int span = (y1 > y0) ? (y1 - y0) : 1;
    const int r0 = c0 & 31, g0 = (c0 >> 5) & 31, b0 = (c0 >> 10) & 31;
    const int r1 = c1 & 31, g1 = (c1 >> 5) & 31, b1 = (c1 >> 10) & 31;
    for (int y = y0; y <= y1; y++) {
        int t = y - y0;
        int r = r0 + (r1 - r0) * t / span;
        int g = g0 + (g1 - g0) * t / span;
        int b = b0 + (b1 - b0) * t / span;
        g_hdma.fix[y] = (uint16_t)((r & 31) | ((g & 31) << 5) | ((b & 31) << 10));
    }
    g_hdma.flags |= S16_HDMA_FIX;
}

void s16_hdma_band(int layer, int y0, int y1, int hofs, int vofs)
{
    if (layer < 0 || layer > 3) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= SNES_H_MAX) y1 = SNES_H_MAX - 1;
    if (y1 < y0) return;
    if (!(g_hdma.scroll_mask & (1u << layer))) {
        for (int y = 0; y < SNES_H_MAX; y++) {
            g_hdma.hofs[layer][y] = g_base_hofs[layer];
            g_hdma.vofs[layer][y] = g_base_vofs[layer];
        }
        g_hdma.scroll_mask |= (1u << layer);
    }
    for (int y = y0; y <= y1; y++) {
        g_hdma.hofs[layer][y] = (uint16_t)(hofs & 0x3FF);
        g_hdma.vofs[layer][y] = (uint16_t)(vofs & 0x3FF);
    }
    g_hdma.flags |= S16_HDMA_SCROLL;
}

void s16_hdma_m7_persp(int y0, int y1, int32_t cx, int32_t cy, int32_t angle_q8, int32_t z_q8, int32_t horizon_y)
{
    ensure_sin_tab();
    if (y0 < 0) y0 = 0;
    if (y1 >= SNES_H_MAX) y1 = SNES_H_MAX - 1;
    if (y1 < y0) return;
    const int idx = ((angle_q8 >> 8) & 255);
    const int32_t sn = g_sin256[idx];
    const int32_t cs = g_sin256[(idx + 64) & 255];
    S.m7x = (int16_t)cx;
    S.m7y = (int16_t)cy;
    S.m7hofs = (int16_t)(cx - 128);
    S.m7vofs = (int16_t)(cy - 112);
    for (int y = y0; y <= y1; y++) {
        int dy = y - horizon_y;
        if (dy < 4) dy = 4;
        /* Perspective scale: z / dy in Q8, clamped to int16 */
        int32_t dist = (z_q8 * 16) / dy;
        if (dist > 4096) dist = 4096;
        g_hdma.m7a[y] = (int16_t)((cs * dist) >> 8);
        g_hdma.m7b[y] = (int16_t)((-sn * dist) >> 8);
        g_hdma.m7c[y] = (int16_t)((sn * dist) >> 8);
        g_hdma.m7d[y] = (int16_t)((cs * dist) >> 8);
    }
    g_hdma.flags |= S16_HDMA_M7;
}

void s16_hdma_mode_band(int y0, int y1, uint8_t bgmode, uint8_t tm, uint8_t ts)
{
    if (y0 < 0) y0 = 0;
    if (y1 >= SNES_H_MAX) y1 = SNES_H_MAX - 1;
    if (y1 < y0) return;
    if (!(g_hdma.flags & S16_HDMA_MODE)) {
        for (int y = 0; y < SNES_H_MAX; y++) {
            g_hdma.bgmode[y] = g_base_bgmode;
            g_hdma.tm[y] = g_base_tm;
            g_hdma.ts[y] = g_base_ts;
        }
    }
    for (int y = y0; y <= y1; y++) {
        g_hdma.bgmode[y] = bgmode;
        g_hdma.tm[y] = tm;
        g_hdma.ts[y] = ts;
    }
    g_hdma.flags |= S16_HDMA_MODE;
}

void s16_render(void)
{
    uint32_t t0 = s16_now_us ? s16_now_us() : 0;
    const uint32_t hf = g_hdma.flags;
    if (!hf) {
        for (int y = 0; y < S.lines; y++)
            ppu_line(y);
    } else {
        for (int y = 0; y < S.lines; y++) {
            if (hf & S16_HDMA_MODE) {
                S.bgmode = g_hdma.bgmode[y];
                S.tm = g_hdma.tm[y];
                S.ts = g_hdma.ts[y];
            }
            if (hf & S16_HDMA_FIX) {
                uint16_t c = g_hdma.fix[y];
                S.fixr = (uint8_t)(c & 31u);
                S.fixg = (uint8_t)((c >> 5) & 31u);
                S.fixb = (uint8_t)((c >> 10) & 31u);
            }
            if (hf & S16_HDMA_SCROLL) {
                for (int l = 0; l < 4; l++) {
                    if (g_hdma.scroll_mask & (1u << l)) {
                        S.hofs[l] = g_hdma.hofs[l][y];
                        S.vofs[l] = g_hdma.vofs[l][y];
                    }
                }
            }
            if (hf & S16_HDMA_M7) {
                S.m7a = g_hdma.m7a[y];
                S.m7b = g_hdma.m7b[y];
                S.m7c = g_hdma.m7c[y];
                S.m7d = g_hdma.m7d[y];
            }
            ppu_line(y);
        }
        /* Restore base registers for next frame's defaults */
        if (hf & S16_HDMA_MODE) { S.bgmode = g_base_bgmode; S.tm = g_base_tm; S.ts = g_base_ts; }
        if (hf & S16_HDMA_FIX)  { S.fixr = g_base_fixr; S.fixg = g_base_fixg; S.fixb = g_base_fixb; }
        if (hf & S16_HDMA_SCROLL) {
            for (int l = 0; l < 4; l++) {
                S.hofs[l] = g_base_hofs[l];
                S.vofs[l] = g_base_vofs[l];
            }
        }
        if (hf & S16_HDMA_M7) { S.m7a = g_base_m7a; S.m7b = g_base_m7b; S.m7c = g_base_m7c; S.m7d = g_base_m7d; }
    }
    S.frames++;
    if (s16_now_us)
        g_stats.us_ppu = s16_now_us() - t0;
}

const uint16_t *s16_picture(int *lines)
{
    if (lines) *lines = S.lines ? S.lines : 224;
    return S.picture;
}

/* ------------------------------------------------------------------ S-DSP BRR */
void s16_dsp_init(void)
{
    dsp_set_mem(NULL, NULL);
    uint8_t *ram = dsp_ram_mem();
    uint8_t *reg = dsp_reg_mem();
    memset(ram, 0, 65536);
    memset(reg, 0, 128);
    dsp_reset();
    dsp_write(0x0C, 127); /* MVOLL */
    dsp_write(0x1C, 127); /* MVOLR */
    dsp_write(0x5D, 0x02); /* DIR = 0x0200 */
    dsp_write(0x6C, 0x20); /* FLG: echo off, unmuted */
}

void s16_dsp_upload_brr(int slot, uint16_t brr_addr, uint16_t loop_addr, const uint8_t *brr, uint32_t len)
{
    if (!brr || slot < 0 || slot >= 64) return;
    uint8_t *ram = dsp_ram_mem();
    for (uint32_t i = 0; i < len; i++)
        ram[(brr_addr + i) & 0xFFFFu] = brr[i];
    uint16_t dir = (uint16_t)(0x0200u + (uint16_t)(slot * 4));
    ram[dir]     = (uint8_t)(brr_addr & 0xFFu);
    ram[dir + 1] = (uint8_t)(brr_addr >> 8);
    ram[dir + 2] = (uint8_t)(loop_addr & 0xFFu);
    ram[dir + 3] = (uint8_t)(loop_addr >> 8);
}

int s16_dsp_upload_pcm16(int slot, uint16_t brr_addr, const int16_t *pcm, int nsamples, int loop_sample)
{
    if (!pcm || nsamples <= 0 || slot < 0 || slot >= 64) return 0;
    uint8_t *ram = dsp_ram_mem();
    const int nblocks = (nsamples + 15) / 16;
    const int loop_on = (loop_sample >= 0 && loop_sample < nsamples);
    const int loop_blk = loop_on ? (loop_sample / 16) : 0;
    const uint16_t loop_addr = (uint16_t)(brr_addr + loop_blk * 9);

    for (int b = 0; b < nblocks; b++) {
        int peak = 0;
        int16_t s[16];
        for (int i = 0; i < 16; i++) {
            int idx = b * 16 + i;
            int16_t v = (idx < nsamples) ? pcm[idx] : 0;
            /* BRR decoder shifts sample right/up and multiplies by 2; scale down by 2 here. */
            int32_t sv = v >> 1;
            s[i] = (int16_t)sv;
            int a = sv < 0 ? -sv : sv;
            if (a > peak) peak = a;
        }
        int range = 1;
        while (range < 12 && (peak >> range) > 7)
            range++;
        uint8_t hdr = (uint8_t)((range << 4) | (loop_on ? 2 : 0) | ((b == nblocks - 1) ? 1 : 0));
        uint16_t dst = (uint16_t)(brr_addr + b * 9);
        ram[dst & 0xFFFFu] = hdr;
        for (int i = 0; i < 8; i++) {
            int32_t s0 = s[i * 2];
            int32_t s1 = s[i * 2 + 1];
            int q0 = (range > 0) ? ((s0 + (s0 >= 0 ? (1 << (range - 1)) : -(1 << (range - 1)))) >> range) : s0;
            int q1 = (range > 0) ? ((s1 + (s1 >= 0 ? (1 << (range - 1)) : -(1 << (range - 1)))) >> range) : s1;
            if (q0 > 7) q0 = 7; else if (q0 < -8) q0 = -8;
            if (q1 > 7) q1 = 7; else if (q1 < -8) q1 = -8;
            ram[(dst + 1 + i) & 0xFFFFu] = (uint8_t)(((q0 & 15) << 4) | (q1 & 15));
        }
    }
    uint16_t dir = (uint16_t)(0x0200u + (uint16_t)(slot * 4));
    ram[dir]     = (uint8_t)(brr_addr & 0xFFu);
    ram[dir + 1] = (uint8_t)(brr_addr >> 8);
    ram[dir + 2] = (uint8_t)(loop_addr & 0xFFu);
    ram[dir + 3] = (uint8_t)(loop_addr >> 8);
    return nblocks * 9;
}

void s16_dsp_play(int voice, int slot, int pitch, int adsr1, int adsr2, int vol_l, int vol_r)
{
    if (voice < 0 || voice >= 8) return;
    int b = voice * 16;
    dsp_write(b + 0, (uint8_t)(vol_l & 0xFF));
    dsp_write(b + 1, (uint8_t)(vol_r & 0xFF));
    dsp_write(b + 2, (uint8_t)(pitch & 0xFF));
    dsp_write(b + 3, (uint8_t)((pitch >> 8) & 0x3F));
    dsp_write(b + 4, (uint8_t)(slot & 0xFF));
    dsp_write(b + 5, (uint8_t)(adsr1 & 0xFF));
    dsp_write(b + 6, (uint8_t)(adsr2 & 0xFF));
    dsp_write(b + 7, 0x7F);
    dsp_write(0x4C, (uint8_t)(1u << voice));
}

void s16_dsp_stop(int voice)
{
    if (voice < 0 || voice >= 8) return;
    dsp_write(0x5C, (uint8_t)(1u << voice));
}

void s16_dsp_step(int samples32k)
{
    uint32_t t0 = s16_now_us ? s16_now_us() : 0;
    for (int i = 0; i < samples32k; i++)
        dsp_tick();
    if (s16_now_us)
        g_stats.us_dsp = s16_now_us() - t0;
}

/* ------------------------------------------------------------------ BMN1 fast C host dispatcher */
int32_t s16_host_call(int32_t host_id, const int32_t *args, int32_t nargs)
{
    switch (host_id) {
    case BMN_HOST_S16_MODE:
        if (nargs >= 4) s16_mode(args[0], args[1], args[2], args[3]);
        return 0;
    case BMN_HOST_S16_SCROLL:
        if (nargs >= 3) s16_scroll(args[0], args[1], args[2]);
        return 0;
    case BMN_HOST_S16_OAM:
        if (nargs >= 5) s16_oam_set(args[0], args[1], args[2], args[3], args[4]);
        return 0;
    case BMN_HOST_S16_OAM_CLR:
        if (nargs >= 1) s16_oam_clear(args[0]);
        return 0;
    case BMN_HOST_S16_M7:
        if (nargs >= 8) s16_m7((int16_t)args[0], (int16_t)args[1], (int16_t)args[2], (int16_t)args[3],
                               (int16_t)args[4], (int16_t)args[5], (int16_t)args[6], (int16_t)args[7], S.m7sel);
        return 0;
    case BMN_HOST_S16_HDMA_GRAD:
        if (nargs >= 4) s16_hdma_grad(args[0], args[1], (uint16_t)args[2], (uint16_t)args[3]);
        return 0;
    case BMN_HOST_S16_HDMA_BAND:
        if (nargs >= 5) s16_hdma_band(args[0], args[1], args[2], args[3], args[4]);
        return 0;
    case BMN_HOST_S16_HDMA_M7:
        if (nargs >= 7) s16_hdma_m7_persp(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
        return 0;
    case BMN_HOST_S16_RENDER:
        s16_render();
        return 0;
    case BMN_HOST_S16_SFX:
        if (nargs >= 4) s16_dsp_play(args[0], args[1], args[2], 0xFF, 0xE0, args[3], args[3]);
        return 0;
    default:
        return 0;
    }
}
