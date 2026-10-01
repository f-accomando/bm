#include "bm.h"

#include <stdlib.h>
#include <string.h>

#include "lib/crc32.h"

int bm_is_cart(const void *head8)
{
    return memcmp(head8, "BMCART\0\0", 8) == 0 || memcmp(head8, "BM33CART", 8) == 0;
}

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

/* Walks the runs of a SHEET8 section; with `set`, draws the pixels. Returns
 * 0 if the runs give exactly w*h valid indices. */
static int sheet8_walk(const uint8_t *p, uint32_t size, void (*set)(void *, int, int, const uint8_t *),
                       void *ctx)
{
    unsigned w = rd16(p), h = rd16(p + 2), ncol = rd16(p + 4);
    const uint8_t *pal = p + 8, *q = pal + ncol * 4, *end = p + size;
    uint32_t n = (uint32_t)w * h, i = 0;
    while (i < n) {
        if (q >= end)
            return -1;
        unsigned t = *q++, run, lit = t < 128;
        run = lit ? t + 1 : t - 126;
        if (i + run > n || q + (lit ? run : 1) > end)
            return -1;
        for (unsigned k = 0; k < run; k++) {
            unsigned idx = lit ? q[k] : q[0];
            if (idx >= ncol)
                return -1;
            if (set)
                set(ctx, (int)(i % w), (int)(i / w), pal + idx * 4);
            i++;
        }
        q += lit ? run : 1;
    }
    return q == end ? 0 : -1;
}

int bm_sheet8_unpack(const bm_cart_t *c, void (*set)(void *ctx, int x, int y, const uint8_t rgba[4]),
                      void *ctx)
{
    if (!c->sheet8)
        return -1;
    return sheet8_walk(c->sheet8, c->sheet8_size, set, ctx);
}

int bm_parse(const uint8_t *d, size_t len, bm_cart_t *c, char *err, size_t errlen)
{
    memset(c, 0, sizeof *c);
    if (len < BM_HEADER_SIZE || !bm_is_cart(d))
        return fail(err, errlen, "not a .bm cartridge");
    if (rd16(d + 8) != 1 || rd16(d + 10) != BM_HEADER_SIZE)
        return fail(err, errlen, "unsupported .bm version");
    if (crc32(d + BM_HEADER_SIZE, (uint32_t)(len - BM_HEADER_SIZE)) != rd32(d + 20))
        return fail(err, errlen, "CRC mismatch");

    c->width = rd16(d + 12);
    c->height = rd16(d + 14);
    c->pixel_format = d[16];
    if (!((c->width == 640 && c->height == 360) || (c->width == 320 && c->height == 180)))
        return fail(err, errlen, "resolution must be 640x360 or 320x180");
    if (c->pixel_format != BM_FMT_RGB565)
        return fail(err, errlen, "pixel format not supported (only RGB565)");
    memcpy(c->title, d + 24, 48);
    memcpy(c->author, d + 72, 32);

    unsigned count = d[17];
    if (BM_HEADER_SIZE + (uint64_t)count * 16 > len)
        return fail(err, errlen, "truncated section table");
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *e = d + BM_HEADER_SIZE + i * 16;
        uint32_t type = rd32(e), off = rd32(e + 4), size = rd32(e + 8);
        if ((uint64_t)off + size > len)
            return fail(err, errlen, "section out of bounds");
        const uint8_t *p = d + off;
        switch (type) {
        case BM_SEC_LUA:
            c->lua = (const char *)p;
            c->lua_size = size;
            break;
        case BM_SEC_SHEET:
            if (size < 4) return fail(err, errlen, "bad sheet");
            c->sheet_w = rd16(p);
            c->sheet_h = rd16(p + 2);
            if (!c->sheet_w || !c->sheet_h || c->sheet_w > BM_SHEET_MAX || c->sheet_h > BM_SHEET_MAX ||
                4 + (uint64_t)c->sheet_w * c->sheet_h * 4 != size)
                return fail(err, errlen, "bad sheet size");
            c->sheet_rgba = p + 4;
            break;
        case BM_SEC_SHEET8: {
            if (size < 12) return fail(err, errlen, "bad sheet");
            unsigned w = rd16(p), h = rd16(p + 2), ncol = rd16(p + 4);
            if (!w || !h || w > BM_SHEET_MAX || h > BM_SHEET_MAX || !ncol || ncol > 256 ||
                8 + ncol * 4 > size)
                return fail(err, errlen, "bad sheet size");
            if (sheet8_walk(p, size, NULL, NULL) != 0)
                return fail(err, errlen, "bad sheet data");
            c->sheet_w = (uint16_t)w;
            c->sheet_h = (uint16_t)h;
            c->sheet8 = p;
            c->sheet8_size = size;
            break;
        }
        case BM_SEC_MAP:
            if (size < 4) return fail(err, errlen, "bad map");
            c->map_w = rd16(p);
            c->map_h = rd16(p + 2);
            if (!c->map_w || !c->map_h || 4 + (uint64_t)c->map_w * c->map_h * 2 != size)
                return fail(err, errlen, "bad map size");
            c->map_cells = p + 4;
            break;
        case BM_SEC_COVER:
            if (size < 4) return fail(err, errlen, "bad cover");
            c->cover_w = rd16(p);
            c->cover_h = rd16(p + 2);
            if (!c->cover_w || !c->cover_h || c->cover_w > 512 || c->cover_h > 512 ||
                4 + (uint64_t)c->cover_w * c->cover_h * 4 != size)
                return fail(err, errlen, "bad cover size");
            c->cover_rgba = p + 4;
            break;
        case BM_SEC_AUDIO:
            if (size < 16 || memcmp(p, "BMAU", 4) != 0) return fail(err, errlen, "bad sound bank");
            c->audio = p;
            c->audio_size = size;
            break;
        default:
            break;      /* unknown sections are ignored (forward compatible) */
        }
    }
    if (!c->lua)
        return fail(err, errlen, "no Lua section");
    if (c->sheet_rgba && c->sheet8)
        return fail(err, errlen, "two sheets");
    return 0;
}

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

uint8_t *bm_rewrite(const uint8_t *old, size_t oldlen, const char *lua, size_t lua_len,
                    const char *title, const char *author, int width, size_t *outlen)
{
    (void)oldlen;
    enum { MAXSEC = 32 };
    uint32_t type[MAXSEC], size[MAXSEC];
    const uint8_t *src[MAXSEC];
    unsigned n = 0, have_lua = 0, count = old ? old[17] : 0;
    for (unsigned i = 0; i < count && n < MAXSEC; i++) {
        const uint8_t *e = old + BM_HEADER_SIZE + i * 16;
        type[n] = rd32(e);
        if (type[n] == BM_SEC_LUA) {
            if (have_lua++) continue;           /* one code section */
            src[n] = (const uint8_t *)lua;
            size[n] = (uint32_t)lua_len;
        } else {
            src[n] = old + rd32(e + 4);
            size[n] = rd32(e + 8);
        }
        n++;
    }
    if (!have_lua && n < MAXSEC) {
        type[n] = BM_SEC_LUA;
        src[n] = (const uint8_t *)lua;
        size[n] = (uint32_t)lua_len;
        n++;
    }
    size_t total = BM_HEADER_SIZE + (size_t)n * 16;
    for (unsigned i = 0; i < n; i++)
        total += (size[i] + 3) & ~3u;
    uint8_t *buf = calloc(total, 1);
    if (!buf)
        return NULL;
    uint8_t *tab = buf + BM_HEADER_SIZE, *p = tab + n * 16;
    for (unsigned i = 0; i < n; i++, tab += 16) {
        wr32(tab, type[i]);
        wr32(tab + 4, (uint32_t)(p - buf));
        wr32(tab + 8, size[i]);
        memcpy(p, src[i], size[i]);
        p += (size[i] + 3) & ~3u;
    }
    memcpy(buf, "BMCART\0\0", 8);
    wr16(buf + 8, 1);
    wr16(buf + 10, BM_HEADER_SIZE);
    wr16(buf + 12, width == 320 ? 320 : 640);
    wr16(buf + 14, width == 320 ? 180 : 360);
    buf[16] = BM_FMT_RGB565;
    buf[17] = (uint8_t)n;
    strncpy((char *)buf + 24, title ? title : "", 47);
    strncpy((char *)buf + 72, author ? author : "", 31);
    wr32(buf + 20, crc32(buf + BM_HEADER_SIZE, (uint32_t)(total - BM_HEADER_SIZE)));
    *outlen = total;
    return buf;
}
