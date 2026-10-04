#include "lib.h"
#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define LIB_SOURCES 160
#define LIB_ITEMS   2048
#define LIB_FILE_MAX (8u * 1024 * 1024)

const char *const lib_groups[LIB_GROUPS] = { "Models", "Images", "Sounds", "Maps", "Palettes", "Kits" };

static lib_source_t sources[LIB_SOURCES];
static lib_item_t items[LIB_ITEMS];
static int nsources, nitems, ready;

/* the file lib_open read last */
static int open_src = -1;
static uint8_t *open_data;
static bm_cart_t open_cart;

void lib_invalidate(void)
{
    lib_view_reset();
    ready = 0;
    free(open_data);
    open_data = NULL;
    open_src = -1;
}

int lib_ready(void) { return ready; }
int lib_items(void) { return nitems; }
int lib_sources(void) { return nsources; }
const lib_item_t *lib_item(int i) { return i >= 0 && i < nitems ? &items[i] : NULL; }
const lib_source_t *lib_source(int s) { return s >= 0 && s < nsources ? &sources[s] : NULL; }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    if (n <= m)
        return 0;
    for (size_t i = 0; i < m; i++)
        if (lower(s[n - m + i]) != ext[i])
            return 0;
    return 1;
}

static int res_file(const char *name)
{
    static const char *const ext[] = { ".bmm", ".bmi", ".bms", ".bmt", ".bmc", ".bmk", ".bm" };
    for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++)
        if (ends_with(name, ext[i]))
            return 1;
    return 0;
}

static lib_item_t *add(int src, int group, int what, int index, const char *name, int nlen)
{
    if (nitems >= LIB_ITEMS)
        return NULL;
    lib_item_t *it = &items[nitems++];
    memset(it, 0, sizeof *it);
    int m = nlen < (int)sizeof it->name - 1 ? nlen : (int)sizeof it->name - 1;
    memcpy(it->name, name, (size_t)m);
    it->name[m] = 0;
    it->source = (uint16_t)src;
    it->group = (uint8_t)group;
    it->what = (uint8_t)what;
    it->index = (uint16_t)index;
    return it;
}

static int name_len(const uint8_t *p, int max)
{
    int n = 0;
    while (n < max && p[n])
        n++;
    return n;
}

static int popcount8(unsigned v)
{
    int n = 0;
    for (; v; v &= v - 1)
        n++;
    return n;
}

/* the songs, sound effects and sounds of a bank (src/audio/player.h) */
static void audio_items(int src, const uint8_t *a, uint32_t size)
{
    if (size < 16 || memcmp(a, "BMAU", 4))
        return;
    unsigned ns = a[5], nx = a[6], np = a[7], ng = a[8];
    uint32_t off = 16, sfx_off[64], song_off[8];
    if (off + ns * 24 > size)
        return;
    uint32_t snd_off = off;
    off += ns * 24;
    for (unsigned i = 0; i < nx && i < 64; i++) {
        if (off + 16 > size) return;
        sfx_off[i] = off;
        off += 16 + 4u * a[off + 10];
    }
    for (unsigned i = 0; i < np; i++) {
        if (off + 4 > size) return;
        off += 4 + 4u * a[off] * (uint32_t)popcount8(a[off + 1]);
    }
    for (unsigned i = 0; i < ng && i < 8; i++) {
        if (off + 16 > size) return;
        song_off[i] = off;
        off += 16 + a[off + 10];
    }
    if (off > size)
        return;
    char buf[16];
    for (unsigned i = 0; i < ng && i < 8; i++) {
        const uint8_t *p = a + song_off[i];
        int n = name_len(p, 8);
        if (!n) n = ksnprintf(buf, sizeof buf, "song %u", i);
        lib_item_t *it = add(src, LIB_SOUNDS, LIB_W_SONG, (int)i, n && p[0] ? (const char *)p : buf, n);
        if (!it) return;
        strcpy(it->tag, "S");
        it->n[0] = p[10];
        it->n[1] = p[8];
    }
    for (unsigned i = 0; i < nx && i < 64; i++) {
        const uint8_t *p = a + sfx_off[i];
        int n = name_len(p, 8);
        if (!n) n = ksnprintf(buf, sizeof buf, "effect %u", i);
        lib_item_t *it = add(src, LIB_SOUNDS, LIB_W_SFX, (int)i, p[0] ? (const char *)p : buf, n);
        if (!it) return;
        strcpy(it->tag, "E");
        it->n[0] = p[10];
    }
    for (unsigned i = 0; i < ns; i++) {
        const uint8_t *p = a + snd_off + i * 24;
        int n = name_len(p, 8);
        if (!n) n = ksnprintf(buf, sizeof buf, "sound %u", i);
        lib_item_t *it = add(src, LIB_SOUNDS, LIB_W_SOUND, (int)i, p[0] ? (const char *)p : buf, n);
        if (!it) return;
        strcpy(it->tag, "I");
    }
}

static int opaque_colours(const bm_cart_t *c)
{
    if (!c->sheet8)
        return 0;
    int nc = c->sheet8[4] | c->sheet8[5] << 8, k = 0;
    for (int i = 0; i < nc; i++)
        k += c->sheet8[8 + i * 4 + 3] >= 128;
    return k;
}

/* the items of one parsed file */
static void file_items(int src, const bm_cart_t *c)
{
    const lib_source_t *s = &sources[src];
    int kind = c->kind;
    if (kind == BM_RES_KIT) {
        lib_item_t *it = add(src, LIB_KITS, LIB_W_KIT, 0, s->title, (int)strlen(s->title));
        if (it) strcpy(it->tag, "kit");
    }
    for (int i = 0; i < c->models; i++) {
        bm_model_t m;
        if (bm_mesh_model(c->mesh, c->mesh_size, i, &m) != 0)
            break;
        lib_item_t *it = add(src, LIB_MODELS, LIB_W_MODEL, i, m.name, (int)strlen(m.name));
        if (!it) return;
        it->n[0] = m.nverts;
        it->n[1] = m.nfaces;
        bm_rig_t r;
        if (c->anim && bm_anim_rig(c->anim, c->anim_size, m.name, &r) == 0) {
            strcpy(it->tag, "A");
            it->n[2] = r.nclips;
        }
    }
    /* images: the zones; without zones the whole sheet (not the textures of
     * a models file, the tiles of a map, or a palette) */
    if (c->zones) {
        for (int i = 0; i < c->zones; i++) {
            bm_zone_t z;
            bm_zone(c, i, &z);
            lib_item_t *it = add(src, LIB_IMAGES, LIB_W_ZONE, i, z.name, (int)strlen(z.name));
            if (!it) return;
            ksnprintf(it->tag, sizeof it->tag, "%ux%u", z.w, z.h);
            it->n[0] = z.w;
            it->n[1] = z.h;
            it->n[2] = z.frames;
        }
    } else if (c->sheet_w && (kind == BM_RES_CART || kind == BM_RES_IMAGE || kind == BM_RES_KIT)) {
        lib_item_t *it = add(src, LIB_IMAGES, LIB_W_SHEET, 0, "sheet", 5);
        if (!it) return;
        ksnprintf(it->tag, sizeof it->tag, "%ux%u", c->sheet_w, c->sheet_h);
        it->n[0] = c->sheet_w;
        it->n[1] = c->sheet_h;
    }
    if (c->audio)
        audio_items(src, c->audio, c->audio_size);
    if (c->map_cells && kind != BM_RES_MODEL) {
        lib_item_t *it = add(src, LIB_MAPS, LIB_W_MAP, 0, "map", 3);
        if (!it) return;
        ksnprintf(it->tag, sizeof it->tag, "%ux%u", c->map_w, c->map_h);
        it->n[0] = c->map_w;
        it->n[1] = c->map_h;
    }
    if (c->sheet8 && kind != BM_RES_MODEL && kind != BM_RES_MAP) {
        const char *nm = kind == BM_RES_PALETTE ? s->title : "palette";
        lib_item_t *it = add(src, LIB_PALETTES, LIB_W_PALETTE, 0, nm, (int)strlen(nm));
        if (!it) return;
        it->n[0] = (uint16_t)opaque_colours(c);
        ksnprintf(it->tag, sizeof it->tag, "%u col", it->n[0]);
    }
}

static int add_source(const char *dir, const fat_entry_t *e)
{
    if (nsources >= LIB_SOURCES || e->size > LIB_FILE_MAX)
        return -1;
    lib_source_t *s = &sources[nsources];
    memset(s, 0, sizeof *s);
    ksnprintf(s->path, sizeof s->path, "%s/%s", strcmp(dir, "/") ? dir : "", e->name);
    ksnprintf(s->file, sizeof s->file, "%s", e->name);
    for (char *p = s->file; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    return nsources++;
}

static void scan_dir(const char *dir, int res)
{
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, dir) != 0)
        return;
    while (fat_readdir(&d, &e)) {
        if (e.is_dir || e.name[0] == '.')
            continue;
        if (res ? !res_file(e.name) : !ends_with(e.name, ".bm"))
            continue;
        add_source(dir, &e);
    }
}

static int src_cmp(const void *a, const void *b)
{
    const lib_source_t *x = a, *y = b;
    /* resource files first (by name), then the cartridges by title */
    int rx = x->kind != BM_RES_CART, ry = y->kind != BM_RES_CART;
    if (rx != ry)
        return ry - rx;
    const char *p = rx ? x->file : x->title, *q = ry ? y->file : y->title;
    for (;; p++, q++) {
        int cx = lower(*p), cy = lower(*q);
        if (cx != cy || !cx)
            return cx - cy;
    }
}

void lib_scan(void)
{
    lib_invalidate();
    nsources = nitems = 0;
    scan_dir("/bm/lib", 1);
    scan_dir("/bm/sounds", 0);
    scan_dir("/carts", 0);
    scan_dir("/", 0);
    /* the headers: kind, title, author (the files that are not ours go) */
    int keep = 0;
    for (int i = 0; i < nsources; i++) {
        lib_source_t *s = &sources[i];
        fat_entry_t e;
        static uint8_t head[512] __attribute__((aligned(4)));
        if (fat_find(s->path, &e) != 0 || fat_read_head(&e, head) != 0 || e.size < BM_HEADER_SIZE)
            continue;
        if (bm_is_res(head))
            s->kind = (uint8_t)(head[12] | head[13] << 8);
        else if (bm_is_cart(head))
            s->kind = BM_RES_CART;
        else
            continue;
        memcpy(s->title, head + 24, 48);
        s->title[48] = 0;
        memcpy(s->author, head + 72, 32);
        s->author[32] = 0;
        if (!s->title[0])
            ksnprintf(s->title, sizeof s->title, "%s", s->file);
        sources[keep++] = *s;
    }
    nsources = keep;
    qsort(sources, (size_t)nsources, sizeof *sources, src_cmp);
    for (int i = 0; i < nsources; i++) {
        fat_entry_t e;
        uint8_t *data = NULL;
        size_t len;
        bm_cart_t c;
        char err[64];
        if (fat_find(sources[i].path, &e) != 0 || fat_load(&e, &data, &len) != 0)
            continue;
        if (bm_parse_any(data, len, &c, err, sizeof err) == 0)
            file_items(i, &c);
        else
            kprintf("lib: %s: %s\n", sources[i].path, err);
        free(data);
    }
    ready = 1;
    kprintf("lib: %d resources in %d files\n", nitems, nsources);
}

const bm_cart_t *lib_open(int source)
{
    if (source == open_src && open_data)
        return &open_cart;
    free(open_data);
    open_data = NULL;
    open_src = -1;
    const lib_source_t *s = lib_source(source);
    fat_entry_t e;
    size_t len;
    char err[64];
    if (!s || fat_find(s->path, &e) != 0 || fat_load(&e, &open_data, &len) != 0)
        return NULL;
    if (bm_parse_any(open_data, len, &open_cart, err, sizeof err) != 0) {
        free(open_data);
        open_data = NULL;
        return NULL;
    }
    open_src = source;
    return &open_cart;
}

const char *lib_info_type(const lib_item_t *it)
{
    static const char *const t[] = { "model", "sprite", "sprite", "song", "sfx", "sound", "map", "palette", "kit" };
    return it->what < sizeof t / sizeof t[0] ? t[it->what] : "";
}

void lib_details(const lib_item_t *it, char lines[5][48], int with_info)
{
    for (int i = 0; i < 5; i++)
        lines[i][0] = 0;
    if (!it)
        return;
    const lib_source_t *s = lib_source(it->source);
    ksnprintf(lines[0], 48, "%s", it->name);
    switch (it->what) {
    case LIB_W_MODEL:
        if (it->n[2])
            ksnprintf(lines[1], 48, "%u vertices, %u faces, %u clips", it->n[0], it->n[1], it->n[2]);
        else
            ksnprintf(lines[1], 48, "%u vertices, %u faces", it->n[0], it->n[1]);
        break;
    case LIB_W_ZONE:
        if (it->n[2] > 1)
            ksnprintf(lines[1], 48, "%ux%u pixels, %u frames", it->n[0], it->n[1], it->n[2]);
        else
            ksnprintf(lines[1], 48, "%ux%u pixels", it->n[0], it->n[1]);
        break;
    case LIB_W_SHEET: ksnprintf(lines[1], 48, "sheet %ux%u", it->n[0], it->n[1]); break;
    case LIB_W_MAP: ksnprintf(lines[1], 48, "map %ux%u tiles", it->n[0], it->n[1]); break;
    case LIB_W_PALETTE: ksnprintf(lines[1], 48, "%u colours", it->n[0]); break;
    case LIB_W_SONG: ksnprintf(lines[1], 48, "song, %u positions, %u bpm", it->n[0], it->n[1]); break;
    case LIB_W_SFX: ksnprintf(lines[1], 48, "sound effect, %u step%s", it->n[0], it->n[0] == 1 ? "" : "s"); break;
    case LIB_W_SOUND: ksnprintf(lines[1], 48, "instrument"); break;
    case LIB_W_KIT: ksnprintf(lines[1], 48, "kit"); break;
    }
    if (s)
        ksnprintf(lines[2], 48, "from %s", s->path[0] == '/' ? s->path + 1 : s->path);
    if (!with_info)
        return;
    const bm_cart_t *c = s ? lib_open(it->source) : NULL;
    char author[33] = "", lic[24] = "", tags[40] = "";
    if (c) {
        const char *t = lib_info_type(it);
        bm_info_get(c, t, it->name, "author", author, sizeof author);
        bm_info_get(c, t, it->name, "license", lic, sizeof lic);
        bm_info_get(c, t, it->name, "tags", tags, sizeof tags);
    }
    if (!author[0] && s)
        ksnprintf(author, sizeof author, "%s", s->author);
    if (author[0] || lic[0])
        ksnprintf(lines[3], 48, "%s%s%s", author, author[0] && lic[0] ? "   " : "", lic);
    if (tags[0])
        ksnprintf(lines[4], 48, "tags: %s", tags);
}
