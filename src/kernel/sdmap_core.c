/* The SD map engine: walks the card a few directory entries at a time, then counts the free clusters, and keeps the
 * numbers of the map (sdmap.h). No fiber and no kernel service: the host test (tests/fs/test_fat.c) runs it on an
 * image; sdmap.c (D09-5c) calls sdmap_core_step from a fiber. */
#include "kernel/sdmap.h"
#include "fs/fat.h"

#include <string.h>

#define DEPTH_MAX 6
#define PATH_MAX_LEN 64

typedef struct { fat_dir_t d; char path[PATH_MAX_LEN]; int depth; } frame_t;

static sdmap_t res;
static frame_t stack[DEPTH_MAX];
static int sp;
static int phase = -1;                  /* -1 not started, 0 the walk, 1 the FAT count, 2 done */
static unsigned seen;
static fat_free_t fst;

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b))
            return 0;
    return *a == *b;
}

/* a starts with prefix, case ignored */
static int istarts(const char *a, const char *prefix)
{
    for (; *prefix; a++, prefix++)
        if (!*a || lower(*a) != lower(*prefix))
            return 0;
    return 1;
}

static int iends(const char *a, const char *suffix)
{
    size_t la = strlen(a), ls = strlen(suffix);
    return la >= ls && ieq(a + la - ls, suffix);
}

/* the folder is `top` or lives under it ("/bm", "/bm/save") */
static int under(const char *folder, const char *top)
{
    size_t n = strlen(top);
    return istarts(folder, top) && (folder[n] == 0 || folder[n] == '/');
}

static int is_kernel_file(const char *folder, const char *name)
{
    if (strcmp(folder, "/") != 0)
        return 0;
    return (istarts(name, "kernel") && iends(name, ".img")) || (istarts(name, "start") && iends(name, ".elf")) ||
           (istarts(name, "fixup") && iends(name, ".dat")) || ieq(name, "bootcode.bin") || ieq(name, "config.txt") ||
           ieq(name, "cmdline.txt") || istarts(name, "u-boot");
}

void sdmap_core_reset(void)
{
    memset(&res, 0, sizeof res);
    memset(stack, 0, sizeof stack);
    memset(&fst, 0, sizeof fst);
    seen = 0;
    sp = 0;
    phase = 0;
    strcpy(stack[0].path, "/");
    if (fat_opendir(&stack[0].d, "/") == 0)
        sp = 1;
    else
        phase = 1;                      /* no root to walk: only the FAT count */
}

const sdmap_t *sdmap_core_result(void) { return &res; }

int sdmap_core_permille(void)
{
    if (phase == 2)
        return 1000;
    if (phase == 0)
        return (int)((seen < 1000 ? seen : 1000) * 700 / 1000);
    if (phase == 1)
        return 700 + (int)((uint64_t)fst.next * 300 / (fat_total_clusters() + 2));
    return 0;
}

static sdmap_dir_t *dir_entry(const char *folder, int depth)
{
    if (depth > 2 || strlen(folder) >= sizeof res.dirs[0].path)
        return NULL;
    for (int i = 0; i < res.ndirs; i++)
        if (strcmp(res.dirs[i].path, folder) == 0)
            return &res.dirs[i];
    if (res.ndirs >= SDMAP_DIRS)
        return NULL;
    sdmap_dir_t *d = &res.dirs[res.ndirs++];
    strcpy(d->path, folder);
    return d;
}

/* the SDMAP_FILES largest files the menu cannot show, sorted by bytes, largest first */
static void list_file(const char *folder, const char *name, uint32_t bytes)
{
    int at = res.nfiles;
    if (res.nfiles >= SDMAP_FILES) {
        if (bytes <= res.files[SDMAP_FILES - 1].bytes)
            return;
        at = SDMAP_FILES - 1;
    }
    while (at > 0 && res.files[at - 1].bytes < bytes) {
        if (at < SDMAP_FILES)
            res.files[at] = res.files[at - 1];
        at--;
    }
    char full[2 * PATH_MAX_LEN + FAT_NAME_MAX];
    size_t n = 0;
    for (const char *p = folder; *p && n < sizeof full - 1; p++)
        full[n++] = *p;
    if (n && full[n - 1] != '/' && n < sizeof full - 1)
        full[n++] = '/';
    for (const char *p = name; *p && n < sizeof full - 1; p++)
        full[n++] = *p;
    full[n] = 0;
    const char *tail = n > sizeof res.files[0].path - 1 ? full + n - (sizeof res.files[0].path - 1) : full;
    strcpy(res.files[at].path, tail);
    res.files[at].bytes = bytes;
    if (res.nfiles < SDMAP_FILES)
        res.nfiles++;
}

static void add_file(const frame_t *f, const fat_entry_t *e)
{
    const int kern = is_kernel_file(f->path, e->name);
    const int in_bm = under(f->path, "/bm") || under(f->path, "/carts");
    const int in_bme = under(f->path, "/bme");
    const int in_glb = under(f->path, "/glb");
    const int in_bmm = under(f->path, "/bmm");
    sdmap_dir_t *d = dir_entry(f->path, f->depth);
    if (d) {
        d->files++;
        d->kb += e->size / 1024;
    }
    if (kern)
        res.kernel += e->size;
    else if (in_bm)
        res.bm += e->size;
    else if (in_bme)
        res.bme += e->size;
    else if (in_glb)
        res.glb += e->size;
    else if (in_bmm)
        res.bmm += e->size;
    int shown = kern || (in_bm && (iends(e->name, ".bm") || iends(e->name, ".b16"))) || (in_bme && iends(e->name, ".bme"));
    if (!shown)
        list_file(f->path, e->name, e->size);
}

static void finish(void)
{
    const uint64_t cb = fat_cluster_bytes();
    res.total = (uint64_t)fat_total_clusters() * cb;
    res.free = (uint64_t)fst.free * cb;
    const uint64_t used = res.free + res.kernel + res.bm + res.bme + res.glb + res.bmm;
    res.other = res.total > used ? res.total - used : 0;
}

int sdmap_core_step(unsigned budget)
{
    if (phase < 0)
        sdmap_core_reset();
    if (phase == 0) {
        for (unsigned n = budget; n; n--) {
            if (sp == 0) {
                phase = 1;
                break;
            }
            frame_t *f = &stack[sp - 1];
            fat_entry_t e;
            if (!fat_readdir(&f->d, &e)) {
                sp--;
                continue;
            }
            seen++;
            if (!strcmp(e.name, ".") || !strcmp(e.name, ".."))
                continue;
            if (!e.is_dir) {
                add_file(f, &e);
                continue;
            }
            if (sp >= DEPTH_MAX)
                continue;
            frame_t *c = &stack[sp];
            size_t pl = strlen(f->path), nl = strlen(e.name);
            const int root = f->path[1] == 0;
            if (pl + nl + 2 > PATH_MAX_LEN)
                continue;
            memcpy(c->path, f->path, pl);
            if (!root)
                c->path[pl++] = '/';
            memcpy(c->path + pl, e.name, nl + 1);
            c->depth = f->depth + 1;
            if (fat_opendir(&c->d, c->path) == 0)
                sp++;
        }
        if (phase == 0)
            return 1;
    }
    if (phase == 1) {
        int r = fat_free_step(&fst, budget ? budget : 1);
        if (r < 0)
            return -1;
        if (r == 0)
            return 1;
        finish();
        phase = 2;
    }
    return 0;
}

/* ---- the cache text (bm/sdmap.txt): "sdmap 2", T, D, F lines ---- */

typedef struct { char *p; size_t n, cap; int over; } out_t;

static void put_s(out_t *o, const char *s)
{
    for (; *s; s++) {
        if (o->n + 1 < o->cap)
            o->p[o->n] = *s;
        else
            o->over = 1;
        o->n++;
    }
}

static void put_u(out_t *o, uint64_t v)
{
    char tmp[24];
    int i = 0;
    do
        tmp[i++] = (char)('0' + v % 10);
    while (v /= 10);
    while (i)
        {
            char c[2] = { tmp[--i], 0 };
            put_s(o, c);
        }
}

size_t sdmap_core_save(char *out, size_t n)
{
    out_t o = { out, 0, n, 0 };
    put_s(&o, "sdmap 2\n");
    put_s(&o, "T "); put_u(&o, res.total);
    put_s(&o, " "); put_u(&o, res.free);
    put_s(&o, " "); put_u(&o, res.kernel);
    put_s(&o, " "); put_u(&o, res.bm);
    put_s(&o, " "); put_u(&o, res.bme);
    put_s(&o, " "); put_u(&o, res.glb);
    put_s(&o, " "); put_u(&o, res.bmm);
    put_s(&o, " "); put_u(&o, res.other);
    put_s(&o, "\n");
    for (int i = 0; i < res.ndirs; i++) {
        put_s(&o, "D "); put_u(&o, res.dirs[i].files);
        put_s(&o, " "); put_u(&o, res.dirs[i].kb);
        put_s(&o, " "); put_s(&o, res.dirs[i].path);
        put_s(&o, "\n");
    }
    for (int i = 0; i < res.nfiles; i++) {
        put_s(&o, "F "); put_u(&o, res.files[i].bytes);
        put_s(&o, " "); put_s(&o, res.files[i].path);
        put_s(&o, "\n");
    }
    if (o.over || o.n >= n)
        return 0;
    out[o.n] = 0;
    return o.n;
}

/* a decimal number at *s (then one space or the end of line): 1 ok */
static int get_u(const char **s, const char *end, uint64_t *v)
{
    const char *p = *s;
    if (p >= end || *p < '0' || *p > '9')
        return 0;
    uint64_t r = 0;
    for (; p < end && *p >= '0' && *p <= '9'; p++)
        r = r * 10 + (uint64_t)(*p - '0');
    if (p < end && *p != ' ' && *p != '\n')
        return 0;
    *v = r;
    *s = p < end && *p == ' ' ? p + 1 : p;
    return 1;
}

int sdmap_core_load(const char *text, size_t len)
{
    sdmap_t tmp;
    memset(&tmp, 0, sizeof tmp);
    const char *p = text, *end = text + len;
    int seen_t = 0, first = 1;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\n')
            eol++;
        if (first) {
            if (eol - p != 7 || memcmp(p, "sdmap 2", 7) != 0)
                goto bad;
            first = 0;
        } else if (eol > p) {
            uint64_t a, b;
            const char *q = p + 2;
            if (p[1] != ' ')
                goto bad;
            if (*p == 'T') {
                uint64_t v[8];
                for (int i = 0; i < 8; i++)
                    if (!get_u(&q, eol, &v[i]))
                        goto bad;
                tmp.total = v[0]; tmp.free = v[1]; tmp.kernel = v[2]; tmp.bm = v[3]; tmp.bme = v[4]; tmp.glb = v[5]; tmp.bmm = v[6]; tmp.other = v[7];
                seen_t = 1;
            } else if (*p == 'D') {
                if (!get_u(&q, eol, &a) || !get_u(&q, eol, &b) || tmp.ndirs >= SDMAP_DIRS)
                    goto bad;
                size_t pl = (size_t)(eol - q);
                if (pl == 0 || pl >= sizeof tmp.dirs[0].path)
                    goto bad;
                sdmap_dir_t *d = &tmp.dirs[tmp.ndirs++];
                d->files = (uint32_t)a;
                d->kb = (uint32_t)b;
                memcpy(d->path, q, pl);
            } else if (*p == 'F') {
                if (!get_u(&q, eol, &a) || tmp.nfiles >= SDMAP_FILES)
                    goto bad;
                size_t pl = (size_t)(eol - q);
                if (pl == 0 || pl >= sizeof tmp.files[0].path)
                    goto bad;
                sdmap_file_t *f = &tmp.files[tmp.nfiles++];
                f->bytes = (uint32_t)a;
                memcpy(f->path, q, pl);
            } else {
                goto bad;
            }
        }
        p = eol + 1;
    }
    if (first || !seen_t)
        goto bad;
    res = tmp;
    return 1;
bad:
    memset(&res, 0, sizeof res);
    return 0;
}
