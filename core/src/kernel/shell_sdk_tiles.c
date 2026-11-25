/*
 * Shell SDK: the bytes of a ROM as tiles (see shell_sdk_tiles.h). Original code.
 */
#include "shell_sdk_tiles.h"

int sdk_tile_bytes(int fmt)
{
    return fmt == SDK_TILE_SNES4 ? 32 : fmt == SDK_TILE_SNES8 ? 64 : 16;
}

const char *sdk_tile_name(int fmt)
{
    return fmt == SDK_TILE_NES ? "NES 2bpp" : fmt == SDK_TILE_SNES2 ? "2bpp" : fmt == SDK_TILE_SNES4 ? "4bpp" : "8bpp";
}

int sdk_tile_colors(int fmt)
{
    return fmt == SDK_TILE_SNES4 ? 16 : fmt == SDK_TILE_SNES8 ? 256 : 4;
}

int sdk_tile_next(int fmt)
{
    return fmt == SDK_TILE_SNES4 ? SDK_TILE_SNES2 : fmt == SDK_TILE_SNES2 ? SDK_TILE_SNES8 :
           fmt == SDK_TILE_SNES8 ? SDK_TILE_SNES4 : SDK_TILE_NES;
}

void sdk_tile_decode(const uint8_t *data, size_t size, size_t off, int fmt, uint8_t pix[64])
{
    const int planes = fmt == SDK_TILE_SNES4 ? 4 : fmt == SDK_TILE_SNES8 ? 8 : 2;
    for (int y = 0; y < 8; y++) {
        uint8_t bits[8];
        for (int p = 0; p < planes; p++) {
            const size_t k = off + (size_t)(fmt == SDK_TILE_NES ? p * 8 + y : (p / 2) * 16 + y * 2 + (p & 1));
            bits[p] = k < size ? data[k] : 0;
        }
        for (int x = 0; x < 8; x++) {
            int v = 0;
            for (int p = 0; p < planes; p++)
                v |= ((bits[p] >> (7 - x)) & 1) << p;
            pix[y * 8 + x] = (uint8_t)v;
        }
    }
}

void sdk_tile_encode(const uint8_t pix[64], int fmt, uint8_t *out)
{
    const int planes = fmt == SDK_TILE_SNES4 ? 4 : fmt == SDK_TILE_SNES8 ? 8 : 2;
    for (int y = 0; y < 8; y++)
        for (int p = 0; p < planes; p++) {
            uint8_t bits = 0;
            for (int x = 0; x < 8; x++)
                bits |= (uint8_t)(((pix[y * 8 + x] >> p) & 1) << (7 - x));
            out[fmt == SDK_TILE_NES ? p * 8 + y : (p / 2) * 16 + y * 2 + (p & 1)] = bits;
        }
}

size_t sdk_tiles_start(const uint8_t *data, size_t size, const sdk_rom_t *info, int *fmt, int *chr)
{
    *chr = 0;
    if (info->kind == SDK_ROM_NES && size >= 16) {
        *fmt = SDK_TILE_NES;
        size_t start = 16 + (data[6] & 4 ? 512u : 0u);
        const size_t graphics = start + (size_t)data[4] * 16384;
        if (data[5] && graphics < size) {
            *chr = 1;
            start = graphics;
        }
        return start < size ? start : 0;
    }
    *fmt = SDK_TILE_SNES4;
    return size % 1024 == 512 ? 512 : 0;
}

int sdk_tiles_cell(int r, int c, int per, int tall)
{
    if (!tall)
        return r * per + c;
    const int pair = r >= 0 ? r / 2 : -((1 - r) / 2);     /* rounded down */
    return (pair * per + c) * 2 + (r - pair * 2);
}

size_t sdk_tiles_move(size_t off, size_t size, int fmt, int group, int rows, int move)
{
    const size_t tile = (size_t)sdk_tile_bytes(fmt);
    const size_t row = tile * (size_t)(group > 0 ? group : 1);
    const size_t page = row * (size_t)(rows > 0 ? rows : 1);
    const size_t last = size > page ? size - page : 0;      /* the last page ends with the file */
    switch (move) {
    case SDK_TILES_UP:
    case SDK_TILES_PGUP: {
        const size_t d = move == SDK_TILES_UP ? row : page;
        return off >= d ? off - d : off % tile;             /* the tiles stay where they were */
    }
    case SDK_TILES_DOWN:
    case SDK_TILES_PGDN: {
        const size_t d = move == SDK_TILES_DOWN ? row : page;
        const size_t end = sdk_tiles_move(off, size, fmt, group, rows, SDK_TILES_END);
        const size_t limit = end > off ? end : off;
        return off + d < limit ? off + d : limit;
    }
    case SDK_TILES_LEFT:
        return off ? off - 1 : 0;
    case SDK_TILES_RIGHT:
        return off + 1 < size ? off + 1 : off;
    case SDK_TILES_BACK:
        return off >= tile ? off - tile : off;
    case SDK_TILES_NEXT:
        return off + tile < size ? off + tile : off;
    default: {                                              /* SDK_TILES_END, in step with the tiles of off */
        const size_t phase = off % tile;
        return last + (phase + tile - last % tile) % tile;
    }
    }
}

static int clamp(int v, int n) { return v < 0 ? 0 : v >= n ? n - 1 : v; }

static void grid_select(sdk_grid_t *g, int select)
{
    if (select && !g->sel) {
        g->arow = g->row;
        g->acol = g->col;
    }
    g->sel = select != 0;
}

void sdk_grid_move(sdk_grid_t *g, int dr, int dc, int select, int h, int w)
{
    grid_select(g, select);
    g->row = clamp(g->row + dr, h);
    g->col = clamp(g->col + dc, w);
}

int sdk_grid_rect(const sdk_grid_t *g, int *r1, int *c1, int *r2, int *c2)
{
    const int ar = g->sel ? g->arow : g->row, ac = g->sel ? g->acol : g->col;
    *r1 = ar < g->row ? ar : g->row;
    *r2 = ar < g->row ? g->row : ar;
    *c1 = ac < g->col ? ac : g->col;
    *c2 = ac < g->col ? g->col : ac;
    return g->sel;
}

void sdk_tiles_cursor(sdk_grid_t *g, size_t *off, size_t size, int fmt, int per, int tall, int rows, int dr,
                      int dc, int select)
{
    const int high = tall ? 2 : 1;
    const size_t tile = (size_t)sdk_tile_bytes(fmt);
    const size_t row = tile * (size_t)(per * high);
    grid_select(g, select);
    g->col = clamp(g->col + dc, per);
    for (; dr < 0; dr++) {
        if (g->row > 0)
            g->row--;
        else if (*off >= row) {                             /* the row of the view above */
            *off -= row;
            g->row = high - 1;
            g->arow += high;
        } else if (*off >= tile) {                          /* the start of the file: the tiles move */
            *off %= tile;
            g->sel = 0;
        }
    }
    for (; dr > 0; dr--) {
        if (g->row < rows - 1) {
            if (*off + (size_t)sdk_tiles_cell(g->row + 1, 0, per, tall) * tile < size)
                g->row++;
        } else {
            const size_t end = sdk_tiles_move(*off, size, fmt, per * high, rows / high, SDK_TILES_END);
            if (*off + row <= end) {
                *off += row;
                g->row = rows - high > 0 ? rows - high : 0;
                g->arow -= high;
            }
        }
    }
}
