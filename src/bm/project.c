/* Games and projects: the editable copy of a game and the build of a
 * project (project.h). */
#include "project.h"
#include "bm.h"

#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int ends_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    return n > m && strcasecmp(s + n - m, ext) == 0;
}

int bm_is_project(const char *path)
{
    return ends_ci(path, ".bme");
}

int bm_is_game(const char *path)
{
    return ends_ci(path, ".bm") || ends_ci(path, ".b16");
}

static int say(char *err, size_t en, const char *msg)
{
    if (err && en)
        ksnprintf(err, en, "%s", msg);
    return -1;
}

/* "/carts/Il mio gioco.bm" -> dir "/carts", base "ILMIOGIO": what an 8.3
 * name can hold of it, in upper case */
static void split(const char *path, char *dir, size_t dn, char base[9])
{
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    if (slash && slash > path) {
        size_t k = (size_t)(slash - path) < dn ? (size_t)(slash - path) : dn - 1;
        memcpy(dir, path, k);
        dir[k] = 0;
    } else {
        ksnprintf(dir, dn, slash ? "/" : "/carts");
    }
    int n = 0;
    for (const char *p = name; *p && *p != '.' && n < 8; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            base[n++] = c;
    }
    if (!n)
        n = ksnprintf(base, 9, "PROJECT");
    base[n] = 0;
}

/* candidate k of a name: BASE.EXT, then BASE1.EXT ... BASE9.EXT (the
 * eighth letter makes room for the digit) */
static void candidate(const char *dir, const char *base, const char *ext, int k, char *out, size_t n)
{
    char b[9];
    ksnprintf(b, sizeof b, "%s", base);
    if (k && strlen(b) > 7)
        b[7] = 0;
    if (!k)
        ksnprintf(out, n, "%s/%s%s", dir, b, ext);
    else
        ksnprintf(out, n, "%s/%s%d%s", dir, b, k, ext);
}

static int exists(const char *path)
{
    fat_entry_t e;
    return fat_find(path, &e) == 0;
}

int bm_copy_name(const char *game, char *out, size_t n)
{
    char dir[64], base[9];
    split(game, dir, sizeof dir, base);
    for (int k = 0; k < 10; k++) {
        candidate(dir, base, ".BME", k, out, n);
        if (!exists(out))
            return 0;
    }
    return -1;
}

/* the whole file, checked to be a cartridge */
static uint8_t *load(const char *path, size_t *len, char *err, size_t en)
{
    fat_entry_t e;
    uint8_t *data;
    if (fat_find(path, &e) != 0 || e.is_dir) {
        say(err, en, "no such file");
        return NULL;
    }
    if (fat_load(&e, &data, len) != 0) {
        say(err, en, fat_error());
        return NULL;
    }
    if (*len < BM_HEADER_SIZE || !bm_is_cart(data)) {
        free(data);
        say(err, en, "not a cartridge");
        return NULL;
    }
    return data;
}

/* a new file (8.3) or the content of one that is there */
static int store(const char *path, const uint8_t *data, size_t len, char *err, size_t en)
{
    if (exists(path))
        return fat_replace(path, data, len) == 0 ? 0 : say(err, en, fat_error());
    char dir[64], base[9];
    split(path, dir, sizeof dir, base);
    const char *slash = strrchr(path, '/');
    if (fat_mkdirs(dir) != 0 || fat_write_file(dir, slash ? slash + 1 : path, data, len) != 0)
        return say(err, en, fat_error());
    return 0;
}

static void set_flags(uint8_t *head, int project)
{
    const unsigned f = (head[18] | head[19] << 8) & ~BM_FLAG_PROJECT;
    const unsigned v = f | (project ? BM_FLAG_PROJECT : 0);
    head[18] = (uint8_t)v;
    head[19] = (uint8_t)(v >> 8);
}

int bm_make_copy(const char *game, const char *to, char *err, size_t en)
{
    size_t len;
    uint8_t *data = load(game, &len, err, en);
    if (!data)
        return -1;
    set_flags(data, 1);
    memset(data + BM_BUILT_FROM, 0, BM_HEADER_SIZE - BM_BUILT_FROM);
    const int r = store(to, data, len, err, en);
    free(data);
    return r;
}

/* the game at `path` was built from the project `from` (its 8.3 name) */
static int built_from(const char *path, const char *from)
{
    fat_entry_t e;
    uint8_t head[512];
    if (fat_find(path, &e) != 0 || e.is_dir || fat_read_head(&e, head) != 0)
        return 0;
    char was[17];
    memcpy(was, head + BM_BUILT_FROM, 16);
    was[16] = 0;
    return bm_is_cart(head) && was[0] && strcasecmp(was, from) == 0;
}

int bm_build(const char *project, char *out, size_t n, char *err, size_t en)
{
    if (!bm_is_project(project))
        return say(err, en, "only a project (.bme) is built");
    size_t len;
    uint8_t *data = load(project, &len, err, en);
    if (!data)
        return -1;
    char dir[64], base[9], from[17];
    split(project, dir, sizeof dir, base);
    const char *slash = strrchr(project, '/');
    ksnprintf(from, sizeof from, "%s", slash ? slash + 1 : project);
    for (char *p = from; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
    set_flags(data, 0);
    memset(data + BM_BUILT_FROM, 0, BM_HEADER_SIZE - BM_BUILT_FROM);
    memcpy(data + BM_BUILT_FROM, from, strlen(from));
    /* the game it built before, else the first free name */
    int k, found = 0;
    for (k = 0; k < 10 && !found; k++) {
        candidate(dir, base, ".BM", k, out, n);
        found = built_from(out, from);
    }
    for (k = 0; k < 10 && !found; k++) {
        candidate(dir, base, ".BM", k, out, n);
        found = !exists(out);
    }
    int r = found ? store(out, data, len, err, en) : say(err, en, "no free name for the game");
    free(data);
    return r;
}
