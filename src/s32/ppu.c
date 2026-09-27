/*
 * s32 PPU (spec 7): background from the dense 8x8 tilemap with "covered"
 * tracking for big tiles, then sprites in OAM slot order. Mirrors lua32's
 * ppu.lua pixel for pixel. Output: 0x00RRGGBB, black where nothing drawn.
 */
#include "s32.h"

#include <string.h>

#define CELL        8
#define MAX_SPAN    8                   /* a 64 px tile spans 8 cells */
#define MAX_COLS    (S32_SCREEN_W / CELL + MAX_SPAN + 2)
#define MAX_ROWS    (S32_SCREEN_H / CELL + MAX_SPAN + 2)

static const uint8_t tile_sizes[4] = { 8, 16, 32, 64 };

static inline uint32_t mask(uint32_t a) { return a & S32_ADDR_MASK; }

static inline uint16_t rd16(const uint8_t *mem, uint32_t a)
{
    return (uint16_t)(mem[mask(a)] | mem[mask(a + 1)] << 8);
}

static inline uint32_t color(const uint8_t *mem, uint32_t pal_base, uint8_t idx)
{
    uint32_t c = mask(pal_base + idx * 3u);
    return (uint32_t)mem[c] << 16 | (uint32_t)mem[mask(c + 1)] << 8 | mem[mask(c + 2)];
}

/* Directory lookup: returns 0 for an undefined size class (spec: undefined). */
static int tile_info(const uint8_t *mem, uint16_t tile, uint32_t *pixels, int *size)
{
    uint32_t d = S32_DIR_BASE + tile * 4u;
    uint8_t cls = mem[d + 3];
    if (cls > 3)
        return 0;
    *pixels = S32_POOL_BASE + (mem[d] | mem[d + 1] << 8 | (uint32_t)mem[d + 2] << 16);
    *size = tile_sizes[cls];
    return 1;
}

/* Background tiles that are drawn this frame, in drawing order, after the
 * "covered" pass over the whole screen. Rendering can then go band by band
 * (a few rows at a time, in the cache) with the same result pixel for
 * pixel: every band draws the same tiles in the same order. */
typedef struct {
    int16_t x, y;               /* top-left on screen */
    uint8_t size;
    uint32_t pix, pal_base;
} bg_tile_t;

static bg_tile_t bg_tiles[MAX_ROWS * MAX_COLS];
static int bg_count;

static void collect_background(const s32_machine_t *m)
{
    static uint8_t covered[MAX_ROWS * MAX_COLS];
    const uint8_t *mem = m->mem;
    const int sx = m->scroll_x, sy = m->scroll_y;
    const int W = S32_SCREEN_W, H = S32_SCREEN_H;

    const int first_cx = sx / CELL - MAX_SPAN, first_cy = sy / CELL - MAX_SPAN;
    const int last_cx = (sx + W - 1) / CELL, last_cy = (sy + H - 1) / CELL;
    const int cols = last_cx - first_cx + 1;
    memset(covered, 0, sizeof covered);
    bg_count = 0;

    for (int cy = first_cy; cy <= last_cy; cy++) {
        const int crow = (cy - first_cy) * cols;
        for (int cx = first_cx; cx <= last_cx; cx++) {
            if (covered[crow + cx - first_cx])
                continue;
            int tmx = ((cx % S32_TILEMAP_W) + S32_TILEMAP_W) % S32_TILEMAP_W;
            int tmy = ((cy % S32_TILEMAP_H) + S32_TILEMAP_H) % S32_TILEMAP_H;
            uint16_t word = rd16(mem, S32_TILEMAP_BASE + (uint32_t)(tmy * S32_TILEMAP_W + tmx) * 2);
            uint16_t tile = word & 0x7FF;
            if (tile == 0)
                continue;

            uint32_t pix;
            int size;
            if (!tile_info(mem, tile, &pix, &size))
                continue;
            const int span = size / CELL;
            const int dy_max = span - 1 < last_cy - cy ? span - 1 : last_cy - cy;
            const int dx_max = span - 1 < last_cx - cx ? span - 1 : last_cx - cx;
            for (int dy = 0; dy <= dy_max; dy++)
                for (int dx = 0; dx <= dx_max; dx++)
                    covered[crow + dy * cols + (cx - first_cx) + dx] = 1;

            const int x = cx * CELL - sx, y = cy * CELL - sy;
            if (x + size <= 0 || y + size <= 0)
                continue;                       /* entirely off screen */
            bg_tiles[bg_count++] = (bg_tile_t){
                (int16_t)x, (int16_t)y, (uint8_t)size, pix,
                S32_CGRAM_BASE + ((word >> 11) & 7) * 256u * 3u,
            };
        }
    }
}

/* Rows y0..y1-1 of the screen into out (row y0 at out[0]). */
static void draw_background(const s32_machine_t *m, uint32_t *out, uint32_t stride, int y0, int y1)
{
    const uint8_t *mem = m->mem;
    const int W = S32_SCREEN_W;

    for (int t = 0; t < bg_count; t++) {
        const bg_tile_t *b = &bg_tiles[t];
        int ly0 = y0 - b->y > 0 ? y0 - b->y : 0;
        int ly1 = y1 - b->y < b->size ? y1 - b->y : b->size;
        for (int ly = ly0; ly < ly1; ly++) {
            uint32_t *row = out + (uint32_t)(b->y + ly - y0) * stride;
            uint32_t src = b->pix + (uint32_t)ly * b->size;
            for (int lx = 0; lx < b->size; lx++) {
                int ox = b->x + lx;
                if (ox < 0 || ox >= W)
                    continue;
                uint8_t idx = mem[mask(src + lx)];
                if (idx)
                    row[ox] = color(mem, b->pal_base, idx);
            }
        }
    }
}

static void draw_sprites(const s32_machine_t *m, uint32_t *out, uint32_t stride, int y0, int y1)
{
    const uint8_t *mem = m->mem;

    for (int i = 0; i < S32_OAM_SLOTS; i++) {
        const uint32_t base = S32_OAM_BASE + i * 8u;
        const uint16_t attr = rd16(mem, base + 6);
        if (!(attr & 1))
            continue;
        const int x = (int16_t)rd16(mem, base);
        const int y = (int16_t)rd16(mem, base + 2);
        const uint16_t word = rd16(mem, base + 4);
        const int flip_x = attr & 2, flip_y = attr & 4;

        uint32_t pix;
        int size;
        if (y >= y1 || y + 64 <= y0)            /* 64 = largest tile: skip early */
            continue;
        if (!tile_info(mem, word & 0x7FF, &pix, &size))
            continue;
        const uint32_t pal_base = S32_CGRAM_BASE + ((word >> 11) & 7) * 256u * 3u;

        int ly0 = y0 - y > 0 ? y0 - y : 0;
        int ly1 = y1 - y < size ? y1 - y : size;
        for (int ly = ly0; ly < ly1; ly++) {
            int ty = flip_y ? size - 1 - ly : ly;
            uint32_t *row = out + (uint32_t)(y + ly - y0) * stride;
            uint32_t src = pix + (uint32_t)ty * size;
            for (int lx = 0; lx < size; lx++) {
                int ox = x + lx;
                if (ox < 0 || ox >= S32_SCREEN_W)
                    continue;
                uint8_t idx = mem[mask(src + (flip_x ? size - 1 - lx : lx))];
                if (idx)
                    row[ox] = color(mem, pal_base, idx);
            }
        }
    }
}

void s32_render_begin(const s32_machine_t *m)
{
    collect_background(m);
}

void s32_render_rows(const s32_machine_t *m, uint32_t *out, uint32_t stride, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        memset(out + (uint32_t)(y - y0) * stride, 0, S32_SCREEN_W * 4);
    draw_background(m, out, stride, y0, y1);
    draw_sprites(m, out, stride, y0, y1);
}

void s32_render(const s32_machine_t *m, uint32_t *out, uint32_t stride)
{
    s32_render_begin(m);
    s32_render_rows(m, out, stride, 0, S32_SCREEN_H);
}
