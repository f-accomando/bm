#include "bmz.h"
#include "png.h"
#include "lib/crc32.h"

#include <string.h>

static const struct { const char *ext, *dir; } kinds[] = {
    { ".bm", "/bm" },   { ".bme", "/bme" }, { ".bmm", "/bmm" }, { ".bmk", "/bmk" },
    { ".bmi", "/bmi" }, { ".bms", "/bms" }, { ".bmt", "/bmt" }, { ".bmc", "/bmc" },
};

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const char *bmz_dir_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return NULL;
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (!strcmp(dot, kinds[i].ext))
            return kinds[i].dir;
    return NULL;
}

/* 1 to 8 of a-z 0-9 _ -, a dot, a known extension; nothing after the NUL */
static int name_ok(const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot || dot == n || dot - n > 8 || !bmz_dir_of(n))
        return 0;
    for (const char *c = n; c < dot; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return 0;
    return 1;
}

int bmz_parse(const uint8_t *f, size_t len, bmz_info_t *info)
{
    if (len < BMZ_HEAD + 2)
        return BMZ_E_SHORT;
    if (memcmp(f, "BMZ1", 4))
        return BMZ_E_MAGIC;
    if ((f[4] | f[5] << 8) != 1 || (f[6] | f[7] << 8) != 0)
        return BMZ_E_VERSION;
    info->raw_len = le32(f + 8);
    info->crc = le32(f + 12);
    if (!info->raw_len || info->raw_len > BMZ_MAX_RAW)
        return BMZ_E_SIZE;
    memcpy(info->name, f + 16, 16);
    if (memchr(info->name, 0, 16) == NULL || !name_ok(info->name))
        return BMZ_E_NAME;
    for (const char *c = info->name + strlen(info->name); c < info->name + 16; c++)
        if (*c)
            return BMZ_E_NAME;
    info->dir = bmz_dir_of(info->name);
    info->zdata = f + BMZ_HEAD;
    info->zlen = len - BMZ_HEAD;
    return BMZ_OK;
}

/* the first bytes of a raw file say its kind: a cartridge or project, or a resource file (docs/BM-FORMAT.md) */
static int kind_ok(const bmz_info_t *info, const uint8_t *raw)
{
    const char *dot = strrchr(info->name, '.');
    const int cart = !strcmp(dot, ".bm") || !strcmp(dot, ".bme");
    if (info->raw_len < 16)
        return 0;
    if (cart)
        return !memcmp(raw, "BMCART\0\0", 8) || !memcmp(raw, "BM33CART", 8);
    return !memcmp(raw, "BMRES\0\0\0", 8);
}

int bmz_extract(const bmz_info_t *info, uint8_t *out)
{
    if (png_inflate(info->zdata, info->zlen, out, info->raw_len) != (long)info->raw_len)
        return BMZ_E_INFLATE;
    if (crc32(out, info->raw_len) != info->crc)
        return BMZ_E_CRC;
    if (!kind_ok(info, out))
        return BMZ_E_KIND;
    return BMZ_OK;
}

const char *bmz_error(int code)
{
    switch (code) {
    case BMZ_OK:        return "ok";
    case BMZ_E_SHORT:   return "too short";
    case BMZ_E_MAGIC:   return "not a .bmz file";
    case BMZ_E_VERSION: return "a .bmz version this console does not know";
    case BMZ_E_SIZE:    return "the size is zero or too big";
    case BMZ_E_NAME:    return "bad name inside (lowercase 8.3 with a known extension)";
    case BMZ_E_INFLATE: return "the compressed data is broken";
    case BMZ_E_CRC:     return "the CRC-32 does not match";
    case BMZ_E_KIND:    return "the content is not what the extension says";
    }
    return "error";
}
