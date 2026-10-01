/*
 * nano8: a fantasy machine compatible with PICO-8 cartridges (.p8, .p8.png),
 * the core of the nano8 cartridge (carts/nano8). Not PICO-8 itself: our own
 * name and font; the same memory map, so the carts' peek and poke work.
 *
 * 64 KiB of RAM, as the carts see it:
 *   0x0000 sprite sheet (128x128, 4 bits per pixel, low nibble = left pixel)
 *   0x1000 sprites 128-255 / map rows 32-63
 *   0x2000 map rows 0-31 (128 cells per row)
 *   0x3000 sprite flags, 0x3100 music, 0x3200 sound effects
 *   0x4300 free, 0x5600 custom font, 0x5e00 persistent data (64 numbers)
 *   0x5f00 draw state (palettes, clip, pen, cursor, camera, fill pattern...)
 *   0x6000 screen (128x128, 4 bits per pixel), 0x8000 upper memory
 * Everything drawn goes into the RAM; n8_blit turns the screen into RGB565
 * through the display palette once per frame. Portable C: the host tests
 * build it as it is.
 */
#ifndef N8_H
#define N8_H

#include <stddef.h>
#include <stdint.h>

#define N8_RAM_SIZE     0x10000
#define N8_ROM_SIZE     0x4300      /* what reload() copies back */

#define N8_GFX          0x0000
#define N8_MAP          0x2000
#define N8_FLAGS        0x3000
#define N8_MUSIC        0x3100
#define N8_SFX          0x3200
#define N8_FONT         0x5600
#define N8_CARTDATA     0x5e00
#define N8_PAL_DRAW     0x5f00
#define N8_PAL_SCREEN   0x5f10
#define N8_CLIP         0x5f20
#define N8_PEN          0x5f25
#define N8_CURSOR       0x5f26
#define N8_CAMERA       0x5f28
#define N8_SCREEN_MODE  0x5f2c
#define N8_FILLP        0x5f31
#define N8_SCREEN       0x6000

typedef struct {
    uint8_t ram[N8_RAM_SIZE];
    uint8_t rom[N8_ROM_SIZE];
    uint32_t rng[2];
    /* print: the attributes that last for one print() call */
    int print_home_x, print_home_y;
    /* buttons: bits per player (0-7), held frames for btnp */
    uint8_t btn[8], btn_prev[8];
    uint16_t btn_frames[8][8];
    int fps;                        /* 30 or 60: btnp's repeat speed */
    /* the frame on show: the screen and the draw state (palettes, mode)
     * as they were at the last flip */
    uint8_t shown[0x2000];
    uint8_t shown_state[0x80];
} n8_t;

/* 16.16 fixed point, the way the carts' numbers are stored in memory and
 * combined by the bit operations. */
int32_t n8_fix(double v);
static inline double n8_unfix(int32_t f) { return f / 65536.0; }
/* A number as a coordinate: floor, clamped to -32768..32767. */
int n8_int(double v);

/* Power on: RAM cleared, the cart's ROM copied in, draw state reset. */
void n8_power(n8_t *m);
/* pal(), palt(), clip(), camera(0,0), fillp(0), cursor(0,0), color(6). */
void n8_reset_draw(n8_t *m);
void n8_reset_pal(n8_t *m);

/* The colour argument of a drawing call: < 0 = the pen; else it becomes
 * the pen (low nibble colour, high nibble the fill pattern's second colour). */
int n8_color(n8_t *m, int c);

void n8_cls(n8_t *m, int c);
void n8_pset(n8_t *m, int x, int y, int c);
int  n8_pget(const n8_t *m, int x, int y);
int  n8_sget(const n8_t *m, int x, int y);
void n8_sset(n8_t *m, int x, int y, int c);
void n8_line(n8_t *m, int x0, int y0, int x1, int y1, int c);
void n8_rect(n8_t *m, int x0, int y0, int x1, int y1, int c, int fill);
void n8_rrect(n8_t *m, int x, int y, int w, int h, int r, int c, int fill);
void n8_circ(n8_t *m, int x, int y, int r, int c, int fill);
void n8_oval(n8_t *m, int x0, int y0, int x1, int y1, int c, int fill);
/* w, h in pixels (spr's tiles x 8) */
void n8_spr(n8_t *m, int n, int x, int y, int w, int h, int fx, int fy);
void n8_sspr(n8_t *m, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, int fx, int fy);
void n8_map(n8_t *m, int cx, int cy, int sx, int sy, int cw, int ch, int layers);
/* map coordinates in 16.16 tiles */
void n8_tline(n8_t *m, int x0, int y0, int x1, int y1, int32_t mx, int32_t my, int32_t mdx, int32_t mdy,
              int layers);
int  n8_mget(const n8_t *m, int x, int y);
void n8_mset(n8_t *m, int x, int y, int v);
void n8_map_size(const n8_t *m, int *w, int *h);

/* print: text in P8SCII with the control codes; at_cursor: no x, y given.
 * c < 0: the pen. Returns the right-most x reached. */
int  n8_print(n8_t *m, const uint8_t *s, size_t len, int x, int y, int c, int at_cursor);

/* Memory: peek/poke wrap at 64 KiB. */
static inline int n8_peek(const n8_t *m, uint32_t a) { return m->ram[a & 0xFFFF]; }
void n8_poke(n8_t *m, uint32_t a, int v);
void n8_memcpy(n8_t *m, uint32_t dst, uint32_t src, int32_t len);
void n8_memset(n8_t *m, uint32_t dst, int v, int32_t len);

/* Random numbers: 0 <= r < limit (16.16), and the seed. */
int32_t n8_rnd(n8_t *m, int32_t limit);
void n8_srand(n8_t *m, int32_t seed);

/* Bit operations on 16.16 numbers. */
int32_t n8_shl(int32_t a, int n);
int32_t n8_shr(int32_t a, int n);
int32_t n8_lshr(int32_t a, int n);
int32_t n8_rotl(int32_t a, int n);
int32_t n8_rotr(int32_t a, int n);

/* Number to text the way the carts print it ("1.5", "0.3333", "-2");
 * flags as tostr(): 1 hex, 2 the raw 32-bit integer. */
int n8_tostr(double v, int flags, char *out, size_t n);
/* Text to number (tonum): decimal, 0x hex and 0b binary, with fractions;
 * flags 1 hex without 0x, 2 raw 32 bits. 0 if not a number. */
int n8_tonum(const char *s, size_t len, int flags, double *out);

/* Buttons for this frame: bits 0-5 (left right up down O X) and 6 (pause)
 * per player; btnp repeats after 15 frames, then every 4 (30 fps). */
void n8_buttons(n8_t *m, const uint8_t bits[8]);
int  n8_btn(const n8_t *m, int b, int p);
int  n8_btnp(const n8_t *m, int b, int p);

/* The frame is complete (flip): it becomes the one on show. */
void n8_present(n8_t *m);
/* The display: the frame on show through the display palette (and the
 * screen modes, the second palette by scanline) into RGB565, scaled to
 * dw x dh with the nearest pixel. */
void n8_blit(const n8_t *m, uint16_t *dst, uint32_t stride, int dw, int dh);
/* Colour i (0-15, 128-143) as 0xRRGGBB */
uint32_t n8_rgb(int i);

/* The built-in font (ours: 3x5 letters, 7x5 symbols). Returns the glyph
 * width in pixels (0: none); rows[5] gets the rows, bit 0 = left. */
int n8_glyph(int c, uint8_t rows[5]);

#endif
