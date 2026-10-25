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
#include "crumbs.h"
#include "bm/loading.h"
#include "market.h"
#include "net/netxfer.h"
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

int lib_start_question(const lib_game_t *g, int compat_known, char *q, size_t qn, char *d, size_t dn, char *y, size_t yn)
{
    if (!compat_known && lib_compat_question(g, q, qn, d, dn, y, yn))
        return LIB_ASK_COMPAT;
    if (lib_need_switch(g)) {
        lib_switch_question(q, qn, d, dn, y, yn);
        return LIB_ASK_SWITCH;
    }
    return LIB_ASK_NONE;
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

/* ---------------------------------------------------------------- start */

int lib_play(framebuffer_t *fb, lib_game_t *c, bm_stats_t *st)
{
    const lib_backend_t *be = library.be;
    const int resume = lib_is_suspended(c);
    if (be->play_begin)
        be->play_begin(c, resume);
    crumb("playing", c->title[0] ? c->title : c->name);
    if (resume) {                               /* frozen by Start + Select: continue from the same frame */
        kprintf("\nresuming %s\n", c->name);
        if (bm_resume(fb, LIB_PLAY_SECS, st) != BM_SUSPENDED)
            lib_suspend_clear();
        crumb("cartridge menu", NULL);
        return LIB_PLAY_OK;
    }
    lib_suspend_close();                        /* another game: the frozen one's memory is freed */
    kprintf("\nplaying %s\n", c->name);
    uint8_t *data = NULL;
    const uint8_t *image = c->builtin;
    size_t len = c->size;
    if (!image) {
        if (fat_load(&c->fe, &data, &len) != 0) {
            loading_stop();
            crumb("cartridge menu", NULL);
            return LIB_PLAY_UNREADABLE;
        }
        image = data;
    }
    if (len < 8 || !bm_is_cart(image)) {
        loading_stop();
        free(data);
        crumb("cartridge menu", NULL);
        return LIB_PLAY_BADFORMAT;
    }
    kprintf("play: %s (%lu bytes)%s\n", c->path, (unsigned long)len, lib_is_dev(c) ? ", no splash" : "");
    if (bm_run(fb, image, len, LIB_PLAY_SECS, st, 1) == BM_SUSPENDED)
        lib_suspend_set(c);
    free(data);
    crumb("cartridge menu", NULL);
    return LIB_PLAY_OK;
}

/* ---------------------------------------------------------------- the panel */

void lib_panel_open(const lib_game_t *g)
{
    library.panel_save = lib_save_size(g, &library.panel_slots);
}

void lib_panel(const lib_game_t *c, home_panel_t *p)
{
    const unsigned cap = library.be->panel;
    memset(p, 0, sizeof *p);
    ksnprintf(p->title, sizeof p->title, "%s", c->title);
    const int susp = lib_is_suspended(c);      /* the tabs list no project (D44): a panel is always a game's */
    home_row(p, MENU_ROW_ACTION, LIB_ROW_PLAY, susp ? "Resume" : "Play",
             susp ? "Back to the point where the game was left" : "Start the game", NULL);
    if (susp)
        home_row(p, MENU_ROW_ACTION, LIB_ROW_CLOSE, "Close the game",
                 "Ends the suspended game: what was not saved is lost", NULL);
    if (!c->builtin) {
        if (cap & LIB_CAP_SDK)
            home_row(p, MENU_ROW_ACTION, LIB_ROW_SDK, "Open in the SDK",
                     "Code, sprites and map of this cartridge", NULL);
        if (cap & LIB_CAP_TOOLS) {
            home_row(p, MENU_ROW_ACTION, LIB_ROW_CODE, "Open in bm Code",
                     "Its code in tabs, small font; sprites, map and sounds stay as they are", NULL);
            home_row(p, MENU_ROW_ACTION, LIB_ROW_SOUND, "Open in the Sound editor",
                     "Sounds, sound effects and music of this cartridge", NULL);
            home_row(p, MENU_ROW_ACTION, LIB_ROW_STUDIO, "Open in bm Studio",
                     "Its 3D models: build them with tiles and blocks, choose, move, paint", NULL);
            home_row(p, MENU_ROW_ACTION, LIB_ROW_ANIMATOR, "Open in bm Animator",
                     "Its models' skeletons and animations: play, rig, animate, sprites", NULL);
            home_row(p, MENU_ROW_ACTION, LIB_ROW_MESH, "Open in bm Mesh",
                     "Its meshes, also those its code builds: vertices, faces; to models or to code", NULL);
            home_row(p, MENU_ROW_ACTION, LIB_ROW_PIXEL, "Open in bm Pixel",
                     "Its sprite sheet: pixel art, palette, animation; the rest stays as it is", NULL);
        }
        if (cap & LIB_CAP_COPY)
            home_row(p, MENU_ROW_ACTION, LIB_ROW_COPY, "Make an editable copy",
                     "Games are read only: open the copy (.bme) from an editor", NULL);
        if (strcmp(lib_kind(c), "b16") != 0)
            home_row(p, MENU_ROW_ACTION, LIB_ROW_BUILD_B16, "Export .b16",
                     "Exports a .b16 edition without changing the .bm", NULL);
        if (cap & LIB_CAP_PUBLISH)
            home_row(p, MENU_ROW_ACTION, LIB_ROW_PUBLISH, "Publish to the Market",
                     "A pull request with your GitHub token: everyone can get it", NULL);
        if (cap & LIB_CAP_SEND)
            home_row(p, MENU_ROW_ACTION, LIB_ROW_SEND, "Send to a nearby console",
                     "To a console on this network with the Market tab open", NULL);
    }
    if (c->grey)                                /* the reason of the grey cover, before Play asks */
        home_row(p, MENU_ROW_INFO, LIB_ROW_COMPAT, "Not here", "Play asks before it starts this game", "%s", c->why);
    home_row(p, MENU_ROW_INFO, LIB_ROW_AUTHOR, "Author", "From the cartridge header",
             "%s", c->author[0] ? c->author : "-");
    home_row(p, MENU_ROW_INFO, LIB_ROW_FILE, "File", c->builtin ? "Built into the kernel" : "On the SD card",
             "%s", c->path);
    home_row(p, MENU_ROW_INFO, LIB_ROW_SIZE, "Size", "The whole cartridge",
             "%lu KB", (c->size + 1023) / 1024);
    home_row(p, MENU_ROW_INFO, LIB_ROW_TYPE, "Type", "Lua 5.4 on bm", "%s",
             !strcmp(lib_kind(c), "b16") ? "b16 (handheld)" : "bm (native)");
    char v[40];
    if (library.panel_save >= 0 && library.panel_slots > 1)
        ksnprintf(v, sizeof v, "%ld bytes in %d slots", library.panel_save, library.panel_slots);
    else if (library.panel_save >= 0)
        ksnprintf(v, sizeof v, "%ld bytes", library.panel_save);
    else
        ksnprintf(v, sizeof v, "none");
    home_row(p, MENU_ROW_INFO, LIB_ROW_SAVE, "Save data", c->save[0] ? c->save : "-", "%s", v);
    if (library.panel_save >= 0)
        home_row(p, MENU_ROW_ACTION, LIB_ROW_DEL_SAVE, "Delete the save data",
                 "Records and progress start again", NULL);
    if (!c->builtin)
        home_row(p, MENU_ROW_ACTION, LIB_ROW_DELETE, "Delete from the SD card",
                 "Removes the file: it cannot be undone", NULL);
}

/* ---------------------------------------------------------------- the rows that stay in the menu */

static void stop_background(void)
{
    if (library.be->stop_background)
        library.be->stop_background();
}

int lib_act(lib_game_t *c, int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    switch (row) {
    case LIB_ROW_CLOSE:
        lib_suspend_close();
        ksnprintf(d->note, sizeof d->note, "closed %s", c->name);
        return 1;
    case LIB_ROW_COPY:
    case LIB_ROW_BUILD_B16: {
        char to[96], err[64];
        stop_background();
        int r = row == LIB_ROW_COPY ? bm_copy_name(c->path, to, sizeof to) : 0;
        if (r != 0)
            ksnprintf(err, sizeof err, "no free name");
        else if (row == LIB_ROW_COPY)
            r = bm_make_copy(c->path, to, err, sizeof err);
        else
            r = bm_build_b16(c->path, to, sizeof to, err, sizeof err);
        if (r == 0) {
            const char *slash = strrchr(to, '/');
            ksnprintf(d->note, sizeof d->note, row == LIB_ROW_COPY ? "%s is a project (SDK: Open)" : "%s is in Games",
                      slash ? slash + 1 : to);
            kprintf("menu: %s %s to %s\n", row == LIB_ROW_COPY ? "copied" : "built", c->path, to);
            d->what = HOME_BACK;            /* the menu reads the SD card again */
        } else {
            d->fail = 1;
            ksnprintf(d->note, sizeof d->note, "%s: %s", row == LIB_ROW_COPY ? "cannot copy" : "cannot build", err);
        }
        return 1;
    }
    case LIB_ROW_SEND:
        market_send_setup(c->path, c->title, c->author);
        d->what = HOME_OPEN;
        d->panel = HOME_SEND;
        return 1;
    case LIB_ROW_DEL_SAVE:
        if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Delete the save data?");
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "Records and progress start again.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Delete");
        } else if (how == HOME_YES) {
            if (lib_save_delete(c, d->note, sizeof d->note) == 0)
                ksnprintf(d->note, sizeof d->note, "save data deleted");
            else
                d->fail = 1;
            lib_panel_open(c);
        }
        return 1;
    case LIB_ROW_DELETE:
        if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Delete %s?", c->title);
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "The file leaves the SD card for good.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Delete");
        } else if (how == HOME_YES && netxfer_updating(c->path)) {
            d->fail = 1;
            ksnprintf(d->note, sizeof d->note, "Updating, wait for the end of the download");
        } else if (how == HOME_YES) {
            if (lib_is_suspended(c))
                lib_suspend_close();
            char path[sizeof c->path];
            ksnprintf(path, sizeof path, "%s", c->path);
            stop_background();          /* no read of it left half way */
            if (fat_delete(path) == 0) {
                ksnprintf(d->note, sizeof d->note, "deleted %s", path);
                d->what = HOME_BACK;
            } else {
                d->fail = 1;
                ksnprintf(d->note, sizeof d->note, "cannot delete %s: %s", path, fat_error());
            }
        }
        return 1;
    }
    return 0;
}

int lib_is_project(const lib_game_t *g)
{
    return !g->builtin && bm_is_project(g->name);
}

int lib_tab_items(int dev, int *idx)
{
    int n = 0;
    for (int i = 0; i < library.n; i++)
        if (lib_is_dev(&library.g[i]) == dev && !lib_is_project(&library.g[i]))
            idx[n++] = i;
    return n;
}

/* ---------------------------------------------------------------- the menu's free time */

void lib_menu_idle(uint32_t until)
{
    const lib_backend_t *be = library.be;
    /* a file (or a kernel) from the PC first, alone on the SD card: the rest waits for it. With nothing
     * waiting the tick only notes that the menu is there. */
    if (netxfer_write_pending()) {
        lib_covers_stop();
        if (be->write_waits)
            be->write_waits();
    }
    if (netxfer_write_tick(until))
        return;
    if (be->before_jobs)
        be->before_jobs();
    fiber_share_run(be->jobs, be->njobs, FIBER_MENU, until);
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
