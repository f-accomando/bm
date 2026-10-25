/*
 * The game library shared by the Pi menu (carts.c) and the RGB30 menu (src/rgb30/ui.c).
 * See library.h.
 */
#include "library.h"
#include "carts.h"
#include "bm/bm.h"
#include "bm/runtime.h"
#include "bm/project.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "menu_ui.h"
#include "toolnames.h"

#include <stdlib.h>
#include <string.h>

lib_t library;                  /* (no initializer: it stays in .bss, 30 KB out of the kernel image) */

/* ---------------------------------------------------------------- text */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int lib_text_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        const int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
        if (x != y || !x)
            return x - y;
    }
}

int lib_same_text(const char *a, const char *b)
{
    return lib_text_cmp(a, b) == 0;
}

int lib_title_cmp(const void *a, const void *b)
{
    return lib_text_cmp(((const lib_game_t *)a)->title, ((const lib_game_t *)b)->title);
}

static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    if (n <= m)
        return 0;
    for (size_t i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i])
            return 0;
    }
    return 1;
}

int lib_ext_ok(const char *name)
{
    /* .b16, the handhelds' cartridge, is the same container on the Pi and runs the same way (the user,
     * 2026-10-05); .bme a project, the editable file of a game (project.h, 2026-10-06) */
    return ends_with(name, ".bm") || ends_with(name, ".b16") || ends_with(name, ".bme");
}

const char *lib_kind(const lib_game_t *g)
{
    return ends_with(g->name, ".b16") ? "b16" : ends_with(g->name, ".bme") ? "bme" : "bm";
}

/* ---------------------------------------------------------------- scan */

/* Title and author from the first bytes of the .bm image. */
static void read_header(const lib_t *l, lib_game_t *c, const uint8_t *h, uint32_t len)
{
    size_t toff = 0, tlen = 0, aoff = 0, alen = 0;
    if (len >= 128 && bm_is_cart(h)) {
        toff = 24; tlen = 48; aoff = 72; alen = 32;
    }
    if (tlen) {
        size_t n = tlen < sizeof c->title - 1 ? tlen : sizeof c->title - 1;
        memcpy(c->title, h + toff, n);
        c->title[n] = 0;
        memcpy(c->author, h + aoff, alen < sizeof c->author - 1 ? alen : sizeof c->author - 1);
        c->author[sizeof c->author - 1] = 0;
        bm_save_path(c->title, c->author, c->save, sizeof c->save);   /* as the runtime names it */
    }
    c->grey = 0;
    c->why = "";
    c->type = tlen ? (uint8_t)bm_type_of_head(h) : BM_TYPE_GAME;
    if (tlen && !c->builtin) {                  /* the policy of docs/spec/compat-policy.toml, from the header read */
        bm_compat_facts_t f;
        bm_compat_facts(h, len, c->size, c->name, &f);
        if (bm_compat_classify(&f, l->be->target, 0, &c->why, NULL) == BM_ACT_GREY)
            c->grey = 1;
    }
    for (char *p = c->title; *p; p++)           /* the console font is CP437 */
        if ((unsigned char)*p < 32) *p = ' ';
    for (char *p = c->author; *p; p++)
        if ((unsigned char)*p < 32) *p = ' ';
    if (!c->title[0])
        ksnprintf(c->title, sizeof c->title, "%s", c->name);
}

void lib_add_builtin(lib_t *l, const char *name, const char *tool, const uint8_t *start, const uint8_t *end)
{
    if (l->n >= LIB_MAX)
        return;
    lib_game_t *c = &l->g[l->n++];
    memset(c, 0, sizeof *c);
    strcpy(c->name, name);
    c->builtin = start;
    c->tool = tool;
    c->size = (uint32_t)(end - start);
    read_header(l, c, start, c->size);
}

static void scan_dir(lib_t *l, const char *path)
{
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, path) != 0)
        return;
    while (l->n < LIB_MAX && fat_readdir(&d, &e)) {
        if (e.is_dir || e.name[0] == '.')
            continue;
        const int want = l->be->want ? l->be->want(e.name) : lib_ext_ok(e.name);
        if (want < 0)
            l->hidden++;
        if (want <= 0)
            continue;
        lib_game_t *c = &l->g[l->n++];
        memset(c, 0, sizeof *c);
        memcpy(c->name, e.name, sizeof c->name);
        strcpy(c->dir, path);
        c->size = e.size;
        c->fe = e;
        c->tool = bm_tool_id_of_file(e.name);
        static uint8_t head[512] __attribute__((aligned(4)));
        if (fat_read_head(&e, head) == 0)
            read_header(l, c, head, e.size < 512 ? e.size : 512);
        else
            read_header(l, c, head, 0);
    }
}

void lib_scan(lib_t *l)
{
    l->cover_job.name = "covers";
    if (l->be->stop_background)
        l->be->stop_background();

    /* Keep covers that have already been read so the menu does not reload them
     * every time a game exits or the user returns to the menu. */
    static struct {
        char path[FAT_NAME_MAX + 10];
        uint32_t size;
        uint32_t cluster;
        g16_sheet_t cover;
        uint8_t cover_read;
        uint8_t used;
    } old_covers[LIB_MAX];
    int n_old = 0;

    for (int i = 0; i < l->n && n_old < LIB_MAX; i++) {
        if (l->g[i].cover_read && l->g[i].cover.px) {
            ksnprintf(old_covers[n_old].path, sizeof old_covers[n_old].path, "%s", l->g[i].path);
            old_covers[n_old].size = l->g[i].size;
            old_covers[n_old].cluster = l->g[i].fe.cluster;
            old_covers[n_old].cover = l->g[i].cover;
            old_covers[n_old].cover_read = l->g[i].cover_read;
            old_covers[n_old].used = 0;
            n_old++;
        } else {
            g16_sheet_free(&l->g[i].cover);
        }
    }
    l->n = 0;
    l->hidden = 0;
    if (l->sd_ok) {
        for (const char *const *d = l->be->dirs; *d; d++)
            scan_dir(l, *d);
        qsort(l->g, (size_t)l->n, sizeof l->g[0], lib_title_cmp);
    }
    l->nsd = l->n;
    if (l->be->add_builtins)
        l->be->add_builtins();
    for (int i = 0; i < l->n; i++) {
        lib_game_t *c = &l->g[i];
        if (c->builtin)
            ksnprintf(c->path, sizeof c->path, "%s", c->name);
        else
            ksnprintf(c->path, sizeof c->path, "%s%s%s", c->dir, strcmp(c->dir, "/") ? "/" : "", c->name);
        for (int j = 0; j < n_old; j++) {
            if (!old_covers[j].used &&
                strcmp(c->path, old_covers[j].path) == 0 &&
                c->size == old_covers[j].size &&
                (c->builtin || c->fe.cluster == old_covers[j].cluster)) {
                c->cover = old_covers[j].cover;
                c->cover_read = old_covers[j].cover_read;
                old_covers[j].used = 1;
                break;
            }
        }
    }
    for (int j = 0; j < n_old; j++)
        if (!old_covers[j].used)
            g16_sheet_free(&old_covers[j].cover);
}

int lib_is_dev(const lib_game_t *g)
{
    return g->tool != NULL || (!g->builtin && g->type != BM_TYPE_GAME);
}

/* ---------------------------------------------------------------- suspended game, save data */

int lib_is_suspended(const lib_game_t *g)
{
    return library.susp_path[0] && strcmp(library.susp_path, g->path) == 0 && bm_suspended(NULL, 0);
}

int lib_need_switch(const lib_game_t *g)
{
    return bm_suspended(NULL, 0) && !lib_is_suspended(g);
}

void lib_suspend_set(const lib_game_t *g)
{
    ksnprintf(library.susp_path, sizeof library.susp_path, "%s", g->path);
}

void lib_suspend_clear(void)
{
    library.susp_path[0] = 0;
}

void lib_suspend_close(void)
{
    bm_close_suspended();
    library.susp_path[0] = 0;
}

void lib_switch_question(char *q, size_t qn, char *d, size_t dn, char *y, size_t yn)
{
    char t[49];
    t[0] = 0;
    bm_suspended(t, sizeof t);
    ksnprintf(q, qn, "Close %s?", t);
    ksnprintf(d, dn, "It is suspended: what was not saved is lost.");
    ksnprintf(y, yn, "Close it");
}

int lib_compat_question(const lib_game_t *g, char *q, size_t qn, char *d, size_t dn, char *y, size_t yn)
{
    if (!g->grey || lib_is_suspended(g))
        return 0;
    ksnprintf(q, qn, "Start it anyway?");
    ksnprintf(d, dn, "%s", g->why);
    ksnprintf(y, yn, "Start");
    return 1;
}

long lib_save_size(const lib_game_t *g, int *slots)
{
    long bytes = -1;
    *slots = 0;
    for (int slot = 1; library.sd_ok && g->save[0] && slot <= BM_SAVE_SLOTS; slot++) {
        char path[sizeof g->save];
        fat_entry_t e;
        bm_save_slot(g->save, slot, path, sizeof path);
        if (fat_find(path, &e) == 0) {
            bytes = (bytes < 0 ? 0 : bytes) + (long)e.size;
            (*slots)++;
        }
    }
    return bytes;
}

int lib_save_delete(const lib_game_t *g, char *note, size_t n)
{
    int err = 0;
    for (int slot = 1; g->save[0] && slot <= BM_SAVE_SLOTS; slot++) {
        char path[sizeof g->save];
        fat_entry_t e;
        bm_save_slot(g->save, slot, path, sizeof path);
        if (fat_find(path, &e) == 0 && fat_delete(path) != 0 && !err) {
            ksnprintf(note, n, "cannot delete %s: %s", path, fat_error());
            err = -1;
        }
    }
    return err;
}

/* ---------------------------------------------------------------- covers */

/* The cover from the .bm COVER section, or a label with the title. */
static void read_cover(lib_game_t *c)
{
    const uint8_t *rgba = NULL;
    int w = 0, h = 0, r = -1;
    uint32_t need = 48 * 1024;          /* header, sections, an 88x88 or 128x80 cover: one read */
    uint8_t *data = NULL;
    if (c->builtin) {
        r = bm_cover_peek(c->builtin, c->size, &need, &rgba, &w, &h);
    } else {
        for (int tries = 0; tries < 3; tries++) {
            size_t len;
            free(data);
            data = NULL;
            if (fat_load_part(&c->fe, need, &data, &len) != 0)
                break;
            r = bm_cover_peek(data, len, &need, &rgba, &w, &h);
            if (r || len >= c->size)
                break;
        }
    }
    if (fiber_cancelled()) {            /* stopped half way: read it next time */
        free(data);
        return;
    }
    g16_sheet_t s = { 0 };
    if (r == 1)
        menu_load_cover(&s, rgba, w, h);
    free(data);
    if (!s.px)
        menu_make_cover(&s, c->title);
    c->cover = s;
    c->cover_read = 1;
}

/* the next cover to read: the ones of the list shown, nearest to the selection first, then the others */
static int next_cover(const lib_t *l)
{
    int best = -1, best_d = 1 << 30;
    if (l->cover_dev >= 0) {
        int sel_pos = 0;
        for (int i = 0, pos = 0; i < l->n; i++)
            if (lib_is_dev(&l->g[i]) == l->cover_dev) {
                if (i == l->cover_sel)
                    sel_pos = pos;
                pos++;
            }
        for (int i = 0, pos = 0; i < l->n; i++) {
            if (lib_is_dev(&l->g[i]) != l->cover_dev)
                continue;
            const int d = pos > sel_pos ? pos - sel_pos : sel_pos - pos;
            pos++;
            if (!l->g[i].cover_read && d < best_d) {
                best = i;
                best_d = d;
            }
        }
    }
    for (int i = 0; best < 0 && i < l->n; i++)
        if (!l->g[i].cover_read)
            best = i;
    return best;
}

int lib_covers_read(const lib_t *l)
{
    int n = 0;
    for (int i = 0; i < l->n; i++)
        n += l->g[i].cover_read;
    return n;
}

static void covers_main(void *arg)
{
    (void)arg;
    int i;
    while (!fiber_cancelled() && (i = next_cover(&library)) >= 0) {
        read_cover(&library.g[i]);
        fiber_slice();
    }
    if (!fiber_cancelled())
        kprintf("covers: all %d read\n", lib_covers_read(&library));
}

void lib_covers_tick(uint32_t until)
{
    if (!library.cover_job.busy) {
        if (next_cover(&library) < 0 || fiber_job_start(&library.cover_job, 16 * 1024, covers_main, NULL) != 0)
            return;
    }
    fiber_job_run(&library.cover_job, until);
}

void lib_covers_stop(void)
{
    fiber_job_stop(&library.cover_job);
}

void lib_cover_focus(int dev, int sel)
{
    library.cover_dev = dev;
    library.cover_sel = sel;
}

/* ---------------------------------------------------------------- find */

int lib_find_path(const lib_t *l, const char *path)
{
    for (int i = 0; i < l->nsd; i++)
        if (lib_same_text(l->g[i].path, path))
            return i;
    return -1;
}

/* what the Market asks of the games on the card (carts.h) */
const char *carts_find_title(const char *title, const char *author)
{
    for (int i = 0; i < library.nsd; i++)
        if (lib_same_text(library.g[i].title, title) && lib_same_text(library.g[i].author, author))
            return library.g[i].path;
    return NULL;
}

int carts_has_path(const char *path)
{
    return path[0] && lib_find_path(&library, path) >= 0;
}
