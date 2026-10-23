/* Games and projects: the editable copy of a game and the build of a
 * project (project.h). */
#include "project.h"
#include "bm.h"
#include "lint.h"

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

/* "/carts/Il mio gioco.bm" -> dir "/carts", base "ilmiogio": what an 8.3
 * name can hold of it, in lower case */
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
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            base[n++] = c;
    }
    if (!n)
        n = ksnprintf(base, 9, "project");
    base[n] = 0;
}

/* candidate k of a name: base.ext, then base1.ext ... base9.ext (the
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

const char *bm_games_dir(void)
{
#ifdef BM_RGB30
    return "/bm";
#else
    return "/carts";
#endif
}

int bm_project_path(const char *name, char *out, size_t n)
{
    char dir[64], base[9];
    split(name, dir, sizeof dir, base);
    candidate(BM_PROJECTS_DIR, base, ".bme", 0, out, n);
    return 0;
}

static int in_projects_dir(const char *dir)
{
    return strcasecmp(dir, BM_PROJECTS_DIR) == 0;
}

int bm_copy_name(const char *game, char *out, size_t n)
{
    char dir[64], base[9];
    split(game, dir, sizeof dir, base);
    for (int k = 0; k < 10; k++) {
        candidate(BM_PROJECTS_DIR, base, ".bme", k, out, n);
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
    const unsigned f = (head[18] | head[19] << 8) & ~(BM_FLAG_PROJECT | BM_FLAG_B16);
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
    bm_set_built_from(data, NULL);
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
    if (in_projects_dir(dir))           /* a project in /bme: the game goes to the games folder */
        ksnprintf(dir, sizeof dir, "%s", bm_games_dir());
    const char *slash = strrchr(project, '/');
    ksnprintf(from, sizeof from, "%s", slash ? slash + 1 : project);
    for (char *p = from; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + 32);
    set_flags(data, 0);
    bm_set_built_from(data, from);
    /* the game it built before, else the first free name */
    int k, found = 0;
    for (k = 0; k < 10 && !found; k++) {
        candidate(dir, base, ".bm", k, out, n);
        found = built_from(out, from);
    }
    for (k = 0; k < 10 && !found; k++) {
        candidate(dir, base, ".bm", k, out, n);
        found = !exists(out);
    }
    int r = found ? store(out, data, len, err, en) : say(err, en, "no free name for the game");
    free(data);
    return r;
}

/* one lint warning of the .b16 export: on the serial console, like the other build messages */
static void lint_say(void *ctx, int line, const char *msg)
{
    if (line)
        kprintf("bm: warning: %s:%d: %s\n", (const char *)ctx, line, msg);
    else
        kprintf("bm: warning: %s: %s\n", (const char *)ctx, msg);
}

int bm_build_b16(const char *src, char *out, size_t n, char *err, size_t en)
{
    if (!bm_is_project(src) && !ends_ci(src, ".bm"))
        return say(err, en, "only a project (.bme) or game (.bm) is exported to .b16");
    size_t len;
    uint8_t *data = load(src, &len, err, en);
    if (!data)
        return -1;
    char dir[64], base[9], from[17];
    split(src, dir, sizeof dir, base);
    if (in_projects_dir(dir))
        ksnprintf(dir, sizeof dir, "%s", bm_games_dir());
#ifdef BM_RGB30
    else if (strcasecmp(dir, "/carts") == 0 && exists("/bm"))
        ksnprintf(dir, sizeof dir, "/bm");
#endif
    const char *slash = strrchr(src, '/');
    ksnprintf(from, sizeof from, "%s", slash ? slash + 1 : src);
    for (char *p = from; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p = (char)(*p + 32);
    set_flags(data, 0);
    data[18] |= BM_FLAG_B16;
    unsigned w = data[12] | data[13] << 8, h = data[14] | data[15] << 8;
    if (w != h) {
        data[12] = 360 & 0xff; data[13] = 360 >> 8;
        data[14] = 360 & 0xff; data[15] = 360 >> 8;
    }
    bm_set_built_from(data, from);
    bm_b16_lint(data, len, lint_say, from);     /* warnings only: the build goes on */
    int k, found = 0;
    for (k = 0; k < 10 && !found; k++) {
        candidate(dir, base, ".b16", k, out, n);
        found = built_from(out, from);
    }
    for (k = 0; k < 10 && !found; k++) {
        candidate(dir, base, ".b16", k, out, n);
        found = !exists(out);
    }
    int r = found ? store(out, data, len, err, en) : say(err, en, "no free name for the .b16");
    free(data);
    return r;
}
