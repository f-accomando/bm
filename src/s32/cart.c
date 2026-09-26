#include "s32.h"

#include <string.h>

#include "lib/crc32.h"

#define HDR_SIZE 264

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int fail(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) {
        strncpy(err, msg, errlen - 1);
        err[errlen - 1] = '\0';
    }
    return -1;
}

static int in_bounds(uint32_t off, uint64_t size, size_t len)
{
    return (uint64_t)off + size <= len;
}

int s32_cart_parse(const uint8_t *d, size_t len, s32_cart_t *c,
                   char *err, size_t errlen)
{
    memset(c, 0, sizeof *c);
    if (len < HDR_SIZE)
        return fail(err, errlen, "file too short");
    if (memcmp(d, "S32CART1", 8) != 0)
        return fail(err, errlen, "bad magic");
    if (d[8] != 1)
        return fail(err, errlen, "unsupported version");
    if (crc32(d + HDR_SIZE, (uint32_t)(len - HDR_SIZE)) != rd32(d + 168))
        return fail(err, errlen, "CRC mismatch");

    memcpy(c->title, d + 28, 64);
    memcpy(c->author, d + 92, 32);
    c->code_type = d[9];

    uint32_t code_off = rd32(d + 128), code_size = rd32(d + 132);
    uint32_t stage_count = rd16(d + 136), stage_size = rd32(d + 140), stage_off = rd32(d + 144);
    uint32_t gfx_count = rd16(d + 148), gfx_size = rd32(d + 152), gfx_off = rd32(d + 156);
    uint32_t cgram_off = rd32(d + 160);

    if (!in_bounds(code_off, code_size, len) || code_size > S32_MEM_SIZE - S32_LOAD_ADDR)
        return fail(err, errlen, "bad code section");
    if (stage_count && (stage_size != S32_TILEMAP_BYTES ||
                        !in_bounds(stage_off, (uint64_t)stage_count * stage_size, len)))
        return fail(err, errlen, "bad stage banks");
    if (gfx_count && (gfx_size != S32_GFX_BANK_BYTES ||
                      !in_bounds(gfx_off, (uint64_t)gfx_count * gfx_size, len)))
        return fail(err, errlen, "bad graphics banks");
    if (d[164] && !in_bounds(cgram_off, S32_CGRAM_BYTES, len))
        return fail(err, errlen, "bad cgram");

    c->code = d + code_off;
    c->code_size = code_size;
    c->stages = stage_count ? d + stage_off : NULL;
    c->stage_count = stage_count;
    c->gfx = gfx_count ? d + gfx_off : NULL;
    c->gfx_count = gfx_count;
    c->cgram = d[164] ? d + cgram_off : NULL;
    return 0;
}
