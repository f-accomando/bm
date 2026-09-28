/*
 * Native bm33 cartridge format (.b33), version 1. Little endian.
 *
 *   0   char[8]  magic "BM33CART"
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
 *            cartridge in the menu (mkb33.py makes it 128x80); first in the
 *            file, so the menu can read it without the rest
 * Graphics are stored independently of the screen format and converted when
 * the cartridge is loaded, so the same file works if 32-bit output is added.
 */
#ifndef B33_H
#define B33_H

#include <stddef.h>
#include <stdint.h>

#define B33_HEADER_SIZE     128
#define B33_FMT_RGB565      1
#define B33_FMT_XRGB8888    2

#define B33_SEC_LUA         1
#define B33_SEC_SHEET       2
#define B33_SEC_MAP         3
#define B33_SEC_COVER       4
#define B33_COVER_W         128
#define B33_COVER_H         80

typedef struct {
    char title[49];
    char author[33];
    uint16_t width, height;
    uint8_t pixel_format;
    const char *lua;
    uint32_t lua_size;
    const uint8_t *sheet_rgba;
    uint16_t sheet_w, sheet_h;
    const uint8_t *map_cells;       /* little-endian u16 cells */
    uint16_t map_w, map_h;
    const uint8_t *cover_rgba;      /* NULL if the cartridge has no cover */
    uint16_t cover_w, cover_h;
} b33_cart_t;

int b33_parse(const uint8_t *data, size_t len, b33_cart_t *c, char *err, size_t errlen);

#endif
