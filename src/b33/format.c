#include "b33.h"

#include <string.h>

#include "lib/crc32.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int fail(char *err, size_t n, const char *msg)
{
    if (err && n) {
        strncpy(err, msg, n - 1);
        err[n - 1] = '\0';
    }
    return -1;
}

int b33_parse(const uint8_t *d, size_t len, b33_cart_t *c, char *err, size_t errlen)
{
    memset(c, 0, sizeof *c);
    if (len < B33_HEADER_SIZE || memcmp(d, "BM33CART", 8) != 0)
        return fail(err, errlen, "not a .b33 cartridge");
    if (rd16(d + 8) != 1 || rd16(d + 10) != B33_HEADER_SIZE)
        return fail(err, errlen, "unsupported .b33 version");
    if (crc32(d + B33_HEADER_SIZE, (uint32_t)(len - B33_HEADER_SIZE)) != rd32(d + 20))
        return fail(err, errlen, "CRC mismatch");

    c->width = rd16(d + 12);
    c->height = rd16(d + 14);
    c->pixel_format = d[16];
    if (!((c->width == 640 && c->height == 360) || (c->width == 320 && c->height == 180)))
        return fail(err, errlen, "resolution must be 640x360 or 320x180");
    if (c->pixel_format != B33_FMT_RGB565)
        return fail(err, errlen, "pixel format not supported (only RGB565)");
    memcpy(c->title, d + 24, 48);
    memcpy(c->author, d + 72, 32);

    unsigned count = d[17];
    if (B33_HEADER_SIZE + (uint64_t)count * 16 > len)
        return fail(err, errlen, "truncated section table");
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = d + B33_HEADER_SIZE + i * 16;
        uint32_t type = rd32(e), off = rd32(e + 4), size = rd32(e + 8);
        if ((uint64_t)off + size > len)
            return fail(err, errlen, "section out of bounds");
        const uint8_t *p = d + off;
        switch (type) {
        case B33_SEC_LUA:
            c->lua = (const char *)p;
            c->lua_size = size;
            break;
        case B33_SEC_SHEET:
            if (size < 4) return fail(err, errlen, "bad sheet");
            c->sheet_w = rd16(p);
            c->sheet_h = rd16(p + 2);
            if (!c->sheet_w || !c->sheet_h || c->sheet_w > 2048 || c->sheet_h > 2048 ||
                4 + (uint64_t)c->sheet_w * c->sheet_h * 4 != size)
                return fail(err, errlen, "bad sheet size");
            c->sheet_rgba = p + 4;
            break;
        case B33_SEC_MAP:
            if (size < 4) return fail(err, errlen, "bad map");
            c->map_w = rd16(p);
            c->map_h = rd16(p + 2);
            if (!c->map_w || !c->map_h || 4 + (uint64_t)c->map_w * c->map_h * 2 != size)
                return fail(err, errlen, "bad map size");
            c->map_cells = p + 4;
            break;
        case B33_SEC_COVER:
            if (size < 4) return fail(err, errlen, "bad cover");
            c->cover_w = rd16(p);
            c->cover_h = rd16(p + 2);
            if (!c->cover_w || !c->cover_h || c->cover_w > 512 || c->cover_h > 512 ||
                4 + (uint64_t)c->cover_w * c->cover_h * 4 != size)
                return fail(err, errlen, "bad cover size");
            c->cover_rgba = p + 4;
            break;
        default:
            break;      /* unknown sections are ignored (forward compatible) */
        }
    }
    if (!c->lua)
        return fail(err, errlen, "no Lua section");
    return 0;
}
