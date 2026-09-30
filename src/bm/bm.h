/*
 * Native bm cartridge format (.bm), version 1. Little endian.
 *
 *   0   char[8]  magic "BMCART" and two zero bytes ("BM33CART" in files
 *                made before the project was renamed: still read)
 *   8   u16      version (1)
 *   10  u16      header size (128)
 *   12  u16      width  (640 or 320)
 *   14  u16      height (360 or 180)
 *   16  u8       pixel format (1 = RGB565; 2 = XRGB8888, reserved)
 *   17  u8       section count
 *   18  u16      reserved (0)
 *   20  u32      CRC-32 of everything after the header
 *   24  char[48] title
 *   72  char[32] author
 *   104 ...      reserved (0) up to 128
 *   128 section table: count x { u32 type, u32 offset, u32 size, u32 reserved }
 *
 * Section types:
 *   1 LUA    main script, UTF-8 source
 *   2 SHEET  u16 w, u16 h, then w*h RGBA8888 pixels (alpha < 128 = transparent)
 *   3 MAP    u16 w, u16 h, then w*h u16 cells (sprite index, 0 = empty)
 *   4 COVER  u16 w, u16 h, then w*h RGBA8888: the picture printed on the
 *            cartridge in the menu (mkbm.py makes it 128x80); first in the
 *            file, so the menu can read it without the rest
 *   5 SHEET8 the sheet with at most 256 colours, much smaller for big
 *            sprites: u16 w, u16 h, u16 colours (1..256), u16 reserved (0),
 *            the palette (colours x RGBA8888, alpha < 128 = transparent), then
 *            the w*h palette indices, row by row, as runs: a byte t < 128 is
 *            followed by t+1 indices; t >= 128 by one index, repeated t-126
 *            times. A cartridge has SHEET or SHEET8, not both.
 * Graphics are stored independently of the screen format and converted when
 * the cartridge is loaded, so the same file works if 32-bit output is added.
 */
#ifndef BM_H
#define BM_H

#include <stddef.h>
#include <stdint.h>

#define BM_HEADER_SIZE     128
#define BM_FMT_RGB565      1
#define BM_FMT_XRGB8888    2

#define BM_SEC_LUA         1
#define BM_SEC_SHEET       2
#define BM_SEC_MAP         3
#define BM_SEC_COVER       4
#define BM_SEC_SHEET8      5
#define BM_SHEET_MAX       4096            /* width and height of a sheet */
#define BM_COVER_W         128
#define BM_COVER_H         80

typedef struct {
    char title[49];
    char author[33];
    uint16_t width, height;
    uint8_t pixel_format;
    const char *lua;
    uint32_t lua_size;
    const uint8_t *sheet_rgba;
    const uint8_t *sheet8;          /* SHEET8 section (from its header), or NULL */
    uint32_t sheet8_size;
    uint16_t sheet_w, sheet_h;
    const uint8_t *map_cells;       /* little-endian u16 cells */
    uint16_t map_w, map_h;
    const uint8_t *cover_rgba;      /* NULL if the cartridge has no cover */
    uint16_t cover_w, cover_h;
} bm_cart_t;

int bm_parse(const uint8_t *data, size_t len, bm_cart_t *c, char *err, size_t errlen);

/* Unpacks a SHEET8 section: set(x, y, rgba) for every pixel. Returns 0, or
 * -1 if the data is broken (bm_parse has already checked it). */
int bm_sheet8_unpack(const bm_cart_t *c, void (*set)(void *ctx, int x, int y, const uint8_t rgba[4]),
                      void *ctx);

/* 1 if these first 8 bytes are the magic of a .bm cartridge. */
int bm_is_cart(const void *head8);

#endif
