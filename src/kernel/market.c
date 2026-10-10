#include "market.h"
#include "carts.h"
#include "config.h"
#include "fiber.h"
#include "bm/bm.h"
#include "bm/png.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "net/catalog.h"
#include "net/github.h"
#include "net/http.h"
#include "net/lan.h"
#include "net/net.h"

#include "lwip/ip4_addr.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASE_URL    "https://f-accomando.github.io/bm-market/"
#ifdef BM_RGB30
#define GAMES_DIR   "/bm"                       /* where the RGB30's menu reads its games */
#else
#define GAMES_DIR   "/carts"
#endif
#define CACHE_DIR   "/bm/market"
#define STACK_SIZE  (128 * 1024)                /* TLS, and the interrupts on top */
#define REFRESH_US  (10u * 60 * 1000000)        /* the catalog again after 10 minutes */
#define NOTE_US     (6u * 1000000)              /* how long a message stays in the footer */
#define SKELETONS   8                           /* placeholders before the catalog */
#define CHUNK       (16 * 1024)                 /* sd: sources: progress, then a pause */
#define PATH_MAX_   (FAT_NAME_MAX + 10)

enum { COVER_WAIT, COVER_BUSY, COVER_READY };

typedef struct {
    g16_sheet_t cover;
    int cover_state, cover_tries;
    int cache_miss;             /* not in the cache: it waits for the network */
    char path[PATH_MAX_];       /* the installed file, "" if none */
    int update;                 /* installed from the Market, other bytes there now */
    int busy;                   /* downloading */
    uint32_t got;               /* bytes so far */
    int failed;                 /* the last download failed */
} slot_t;

/* the games installed from the Market: /bm/market/GAMES.TXT, one line each,
 * "<id> <sha256 hex> <path>" */
typedef struct {
    char id[24];
    uint8_t sha[32];
    char path[PATH_MAX_];
} owned_t;

static catalog_t cat;
static slot_t *slots;           /* one per game of cat */
static owned_t *owned;
static int nowned;
static char base[160];
static int inited, active, sel, have, changed;
static int fetched, net_failed;
static uint32_t fetched_at, note_at;
static char note[64], banner[72];
static char want_id[24];        /* the game to download next */
static int delay_ms;            /* market_delay in bm/config.txt: slows a source down (tests) */

static fiber_t fib;
static uint8_t *stack;
static int running;
enum { JOB_CACHE, JOB_INDEX, JOB_COVER, JOB_GET };
static int job, job_game;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(note, sizeof note, fmt, ap);
    va_end(ap);
    note_at = timer_ticks();
    if (note[0])
        kprintf("market: %s\n", note);
}

static int from_sd(void)
{
    return strncmp(base, "sd:", 3) == 0;
}

static int online(void)
{
    return from_sd() || net_ip() != 0;
}

static void hex(const uint8_t *b, int n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = 0;
}

static int unhex(const char *s, uint8_t *out, int n)
{
    for (int i = 0; i < 2 * n; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0)
            return -1;
        out[i / 2] = (uint8_t)(i & 1 ? out[i / 2] | v : v << 4);
    }
    return 0;
}

/* ---------------------------------------------------------------- sources */

/* a pause that lets the menu run; -1 if the job was cancelled */
static int pause_ms(uint32_t ms)
{
    for (uint32_t t0 = timer_ticks(); timer_ticks() - t0 < ms * 1000u; )
        if (net_wait_step() < 0)
            return -1;
    return 0;
}

typedef struct {
    uint8_t *buf;
    size_t len, cap, max;
    uint32_t *got;
} sink_t;

static int sink(void *p, const uint8_t *d, size_t n)
{
    sink_t *s = p;
    if (n > s->max - s->len)
        return 1;                       /* more than the catalog says */
    if (s->len + n > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 16384;
        while (cap < s->len + n)
            cap *= 2;
        if (cap > s->max)
            cap = s->max;
        uint8_t *nb = realloc(s->buf, cap);
        if (!nb)
            return 1;
        s->buf = nb;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, d, n);
    s->len += n;
    if (s->got)
        *s->got = (uint32_t)s->len;
    return 0;
}

/* A file under the catalog's address ("index.txt", "games/snake/snake.bm"),
 * at most max bytes (expect: the size the catalog says, 0 if unknown), into
 * a malloc'd buffer; *got follows the bytes. 0, or -1 with err. */
static int fetch(const char *rel, size_t max, size_t expect, uint32_t *got,
                 uint8_t **data, size_t *len, char *err, size_t err_len)
{
    *data = NULL;
    *len = 0;
    if (delay_ms && pause_ms(delay_ms) < 0)
        goto cancelled;
    if (from_sd()) {
        char path[sizeof base + 112];
        ksnprintf(path, sizeof path, "%s%s", base + 3, rel);
        fat_entry_t e;
        if (fat_find(path, &e) != 0 || e.is_dir) {
            ksnprintf(err, err_len, "%s: not found", rel);
            return -1;
        }
        if (e.size > max) {
            ksnprintf(err, err_len, "%s: too big", rel);
            return -1;
        }
        if (fat_load(&e, data, len) != 0) {
            ksnprintf(err, err_len, "%s: %s", rel, fat_error());
            return -1;
        }
        /* as if it came in pieces: the menu runs in between */
        for (size_t off = 0; off < *len; off += CHUNK) {
            if (got)
                *got = (uint32_t)(off + CHUNK < *len ? off + CHUNK : *len);
            if (net_wait_step() < 0 || (delay_ms && pause_ms(delay_ms) < 0)) {
                free(*data);
                *data = NULL;
                goto cancelled;
            }
        }
        if (got)
            *got = (uint32_t)*len;
        return 0;
    }
    char url[sizeof base + 112];
    ksnprintf(url, sizeof url, "%s%s", base, rel);
    sink_t s = { NULL, 0, 0, max, got };
    if (expect && (s.buf = malloc(expect)) != NULL)
        s.cap = expect;
    http_req_t req = { .timeout_ms = 15000 };
    static http_info_t info;
    int st = http_request(url, &req, sink, &s, &info);
    if (st == 200 && !info.error[0]) {
        *data = s.buf;
        *len = s.len;
        return 0;
    }
    free(s.buf);
    if (fiber_cancelled())
        goto cancelled;
    if (st < 0 || info.error[0])
        ksnprintf(err, err_len, "%s", info.error);
    else
        ksnprintf(err, err_len, "%s: HTTP %d", rel, st);
    return -1;
cancelled:
    ksnprintf(err, err_len, "cancelled");
    return -1;
}

/* ---------------------------------------------------------------- installed games */

static owned_t *owned_find(const char *id)
{
    for (int i = 0; i < nowned; i++)
        if (strcmp(owned[i].id, id) == 0)
            return &owned[i];
    return NULL;
}

static void owned_load(void)
{
    if (!owned && !(owned = calloc(CATALOG_MAX, sizeof *owned)))
        return;
    nowned = 0;
    fat_entry_t e;
    uint8_t *d;
    size_t len;
    if (fat_find(CACHE_DIR "/GAMES.TXT", &e) != 0 || fat_load(&e, &d, &len) != 0)
        return;
    char line[256];
    for (size_t pos = 0; pos < len && nowned < CATALOG_MAX; ) {
        size_t n = 0;
        for (; pos < len && d[pos] != '\n'; pos++)
            if (n < sizeof line - 1)
                line[n++] = (char)d[pos];
        pos++;
        line[n] = 0;
        char *sp1 = strchr(line, ' '), *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL;
        if (!sp2 || sp1 - line >= (long)sizeof owned->id || sp2 - sp1 - 1 != 64 ||
            strlen(sp2 + 1) >= sizeof owned->path || sp2[1] != '/')
            continue;
        owned_t *o = &owned[nowned];
        memcpy(o->id, line, (size_t)(sp1 - line));
        o->id[sp1 - line] = 0;
        if (unhex(sp1 + 1, o->sha, 32) == 0) {
            strcpy(o->path, sp2 + 1);
            nowned++;
        }
    }
    free(d);
}

static int owned_save(void)
{
    char *text = malloc((size_t)nowned * (sizeof *owned + 70) + 1), *p = text;
    if (!text)
        return -1;
    for (int i = 0; i < nowned; i++) {
        char h[65];
        hex(owned[i].sha, 32, h);
        p += ksnprintf(p, sizeof *owned + 70, "%s %s %s\n", owned[i].id, h, owned[i].path);
    }
    int r = fat_mkdirs(CACHE_DIR) == 0 && fat_write_file(CACHE_DIR, "GAMES.TXT", text, (size_t)(p - text)) == 0;
    free(text);
    return r ? 0 : -1;
}

static void owned_set(const char *id, const uint8_t *sha, const char *path)
{
    owned_t *o = owned_find(id);
    if (!o) {
        if (!owned || nowned == CATALOG_MAX)
            return;
        o = &owned[nowned++];
    }
    ksnprintf(o->id, sizeof o->id, "%s", id);
    memcpy(o->sha, sha, 32);
    ksnprintf(o->path, sizeof o->path, "%s", path);
}

static void owned_drop(const char *id)
{
    owned_t *o = owned_find(id);
    if (o)
        *o = owned[--nowned];
}

/* a .b16, the handhelds' cartridge (docs/B16.md), by its file's name */
static int is_b16(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && path[n - 4] == '.' && (path[n - 3] | 32) == 'b' && path[n - 2] == '1' && path[n - 1] == '6';
}

static const char *kind_of(const catalog_game_t *g)
{
    return is_b16(g->file.path) ? "b16" : "bm";
}

/* which games are on the SD card: installed from the Market (and still
 * there), or the same title and author put there another way; only a file
 * of the catalog's kind: a game that became a .b16 (Yharnam, Overbit) is not
 * its old .bm (on the RGB30 the .bm of the SD image would play instead, and
 * an update would write the .b16's bytes under a .BM name) */
static void refresh_slots(void)
{
    for (int i = 0; i < cat.n; i++) {
        slot_t *s = &slots[i];
        const catalog_game_t *g = &cat.games[i];
        const owned_t *o = owned_find(g->id);
        const int b16 = is_b16(g->file.path);
        s->path[0] = 0;
        s->update = 0;
        if (o && carts_has_path(o->path) && is_b16(o->path) == b16) {
            ksnprintf(s->path, sizeof s->path, "%s", o->path);
            s->update = memcmp(o->sha, g->file.sha256, 32) != 0;
        } else {
            const char *p = carts_find_title(g->title, g->author);
            if (p && is_b16(p) == b16)
                ksnprintf(s->path, sizeof s->path, "%s", p);
        }
    }
}

/* ---------------------------------------------------------------- the catalog */

/* A new catalog replaces the one in memory; covers that did not change
 * are kept. */
static void adopt(catalog_t *nc)
{
#ifdef BM_RGB30
    /* the RGB30 plays only the .b16 games (the user's choice, 2026-10-05):
     * the others are not in its Market */
    int k = 0;
    for (int i = 0; i < nc->n; i++)
        if (is_b16(nc->games[i].file.path))
            nc->games[k++] = nc->games[i];
    nc->n = k;
#endif
    slot_t *ns = calloc(nc->n ? (size_t)nc->n : 1, sizeof *ns);
    if (!ns) {
        catalog_free(nc);
        return;
    }
    for (int i = 0; i < nc->n; i++) {
        const catalog_game_t *g = &nc->games[i];
        for (int j = 0; j < cat.n && g->cover.size; j++)
            if (slots[j].cover_state == COVER_READY && slots[j].cover.px && cat.games[j].cover.size &&
                memcmp(cat.games[j].cover.sha256, g->cover.sha256, 32) == 0) {
                ns[i].cover = slots[j].cover;
                memset(&slots[j].cover, 0, sizeof slots[j].cover);
                ns[i].cover_state = COVER_READY;
                break;
            }
        if (!g->cover.size && menu_make_cover(&ns[i].cover, g->title, kind_of(g)) == 0)
            ns[i].cover_state = COVER_READY;
    }
    for (int j = 0; j < cat.n; j++)
        g16_sheet_free(&slots[j].cover);
    free(slots);
    catalog_free(&cat);
    cat = *nc;
    slots = ns;
    have = 1;
    refresh_slots();
}

/* a signed catalog: 0 and *out, or -1 with err */
static int read_catalog(const uint8_t *idx, size_t il, const uint8_t *sig, size_t sl, catalog_t *out,
                        char *err, size_t err_len)
{
    if (catalog_verify(idx, il, sig, sl, err, err_len) != 0)
        return -1;
    return catalog_parse(idx, il, out, err, err_len);
}

static void load_file(const char *path, uint8_t **d, size_t *len)
{
    fat_entry_t e;
    *d = NULL;
    *len = 0;
    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, d, len) != 0)
        *d = NULL;
}

/* first time on the tab: the settings, a key of one's own, the list of
 * installed games and the catalog saved last time */
static void job_cache(void)
{
    const char *u = config_get("market_url");
    ksnprintf(base, sizeof base, "%s", u && u[0] ? u : BASE_URL);
    size_t bl = strlen(base);
    if (bl && base[bl - 1] != '/' && bl + 1 < sizeof base)
        strcpy(base + bl, "/");
    const char *dl = config_get("market_delay");
    delay_ms = dl ? (int)strtol(dl, NULL, 10) : 0;
    fat_entry_t e;
    uint8_t *d;
    size_t len;
    if (config_find_file("market.pem", &e) == 0 && fat_load(&e, &d, &len) == 0) {
        char *pem = malloc(len + 1);
        if (pem) {
            memcpy(pem, d, len);
            pem[len] = 0;
            if (catalog_add_key(pem) != 0)
                kprintf("market: bm/market.pem is not a P-256 public key\n");
            free(pem);
        }
        free(d);
    }
    owned_load();
    uint8_t *idx, *sig;
    size_t il, sl;
    load_file(CACHE_DIR "/INDEX.TXT", &idx, &il);
    load_file(CACHE_DIR "/INDEX.SIG", &sig, &sl);
    catalog_t c;
    char err[96];
    if (idx && sig && read_catalog(idx, il, sig, sl, &c, err, sizeof err) == 0)
        adopt(&c);
    free(idx);
    free(sig);
    kprintf("market: %s, %s%s\n", base, have ? "catalog saved on the SD card" : "no catalog saved",
            catalog_has_key() ? "" : ", no key");
    inited = 1;
}

static void job_index(void)
{
    uint8_t *idx = NULL, *sig = NULL;
    size_t il, sl;
    char err[96];
    catalog_t c;
    if (fetch("index.txt", CATALOG_MAX_LEN, 0, NULL, &idx, &il, err, sizeof err) != 0 ||
        fetch("index.sig", 1024, 0, NULL, &sig, &sl, err, sizeof err) != 0 ||
        read_catalog(idx, il, sig, sl, &c, err, sizeof err) != 0) {
        if (strcmp(err, "cancelled") != 0) {
            net_failed = 1;
            say("catalog: %s", err);
        }
        free(idx);
        free(sig);
        return;
    }
    fetched = 1;
    fetched_at = timer_ticks();
    if (have && !catalog_newer(c.serial, cat.serial)) {
        if (strcmp(c.serial, cat.serial) != 0)
            say("catalog %s is older than %s: kept", c.serial, cat.serial);
        catalog_free(&c);
    } else {
        adopt(&c);
        if (fat_mkdirs(CACHE_DIR) != 0 || fat_write_file(CACHE_DIR, "INDEX.TXT", idx, il) != 0 ||
            fat_write_file(CACHE_DIR, "INDEX.SIG", sig, sl) != 0)
            kprintf("market: cannot save the catalog: %s\n", fat_error());
        kprintf("market: catalog %s, %d games\n", cat.serial, cat.n);
    }
    free(idx);
    free(sig);
}

/* ---------------------------------------------------------------- covers */

static void cache_name(const catalog_file_t *f, char *name)
{
    char h[9];
    hex(f->sha256, 4, h);
    for (char *p = h; *p; p++)
        if (*p >= 'a') *p = (char)(*p - 32);
    ksnprintf(name, 13, "%s.PNG", h);
}

static void job_cover(int i)
{
    slot_t *s = &slots[i];
    const catalog_game_t *g = &cat.games[i];
    s->cover_state = COVER_BUSY;
    char name[13], path[40], err[96];
    cache_name(&g->cover, name);
    ksnprintf(path, sizeof path, CACHE_DIR "/%s", name);
    uint8_t *d;
    size_t len;
    load_file(path, &d, &len);
    if (d && catalog_check_file(&g->cover, d, len) != 0) {
        free(d);
        d = NULL;
    }
    if (!d && !online()) {
        s->cache_miss = 1;
        s->cover_state = COVER_WAIT;
        return;
    }
    if (!d) {
        kprintf("market: cover of %s\n", g->id);
        if (fetch(g->cover.path, g->cover.size, g->cover.size, NULL, &d, &len, err, sizeof err) != 0) {
            if (strcmp(err, "cancelled") == 0) {
                s->cover_state = COVER_WAIT;
                return;
            }
            kprintf("market: cover of %s: %s\n", g->id, err);
        } else if (catalog_check_file(&g->cover, d, len) != 0) {
            kprintf("market: cover of %s: not the one of the catalog\n", g->id);
            free(d);
            d = NULL;
        } else if (fat_mkdirs(CACHE_DIR) != 0 || fat_write_file(CACHE_DIR, name, d, len) != 0) {
            kprintf("market: cannot keep the cover of %s: %s\n", g->id, fat_error());
        }
    }
    uint8_t *rgba = NULL;
    int w = 0, h = 0;
    if (d && png_rgba(d, len, &rgba, &w, &h) == 0 && menu_load_cover(&s->cover, rgba, w, h) == 0) {
        s->cover_state = COVER_READY;
    } else if (++s->cover_tries >= 2 || d) {
        /* broken or unreachable: the title on a label, as for games without one */
        if (menu_make_cover(&s->cover, g->title, kind_of(g)) == 0)
            s->cover_state = COVER_READY;
    } else {
        s->cover_state = COVER_WAIT;
    }
    free(rgba);
    free(d);
}

/* the cover to load next: the highlighted game's, then the nearest */
static int pick_cover(void)
{
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < cat.n; i++) {
        const slot_t *s = &slots[i];
        if (s->cover_state != COVER_WAIT || !cat.games[i].cover.size || (s->cache_miss && !online()))
            continue;
        int d = i > sel ? i - sel : sel - i;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* ---------------------------------------------------------------- downloads */

static int find(const char *id)
{
    for (int i = 0; i < cat.n; i++)
        if (strcmp(cat.games[i].id, id) == 0)
            return i;
    return -1;
}

/* where a new game goes: /carts/SNAKE.BM from the id (a .b16 SNAKE.B16;
 * on the RGB30 in /bm), another name if that one is taken */
static int new_path(const char *id, const char *ext, char *name, size_t n)
{
    char stem[9];
    int k = 0;
    for (; id[k] && k < 8; k++)
        stem[k] = id[k] >= 'a' && id[k] <= 'z' ? (char)(id[k] - 32) : id[k];
    stem[k] = 0;
    for (int t = 0; t < 10; t++) {
        char path[32];
        if (t) {
            char s7[8];
            memcpy(s7, stem, 7);
            s7[7] = 0;                          /* ksnprintf has no precision */
            ksnprintf(name, n, "%s%d%s", s7, t, ext);
        } else {
            ksnprintf(name, n, "%s%s", stem, ext);
        }
        ksnprintf(path, sizeof path, GAMES_DIR "/%s", name);
        fat_entry_t e;
        if (fat_find(path, &e) != 0)
            return 0;
    }
    return -1;
}

static void job_get(int i)
{
    slot_t *s = &slots[i];
    const catalog_game_t *g = &cat.games[i];
    s->busy = 1;
    s->got = 0;
    s->failed = 0;
    uint8_t *d;
    size_t len;
    char err[96];
    kprintf("market: downloading %s (%lu bytes)\n", g->file.path, (unsigned long)g->file.size);
    if (fetch(g->file.path, g->file.size, g->file.size, &s->got, &d, &len, err, sizeof err) != 0) {
        s->busy = 0;
        if (strcmp(err, "cancelled") == 0) {
            say("download of %s interrupted", g->title);
        } else {
            s->failed = 1;
            say("download failed: %s", err);
        }
        return;
    }
    if (catalog_check_file(&g->file, d, len) != 0) {
        free(d);
        s->busy = 0;
        s->failed = 1;
        say("%s: not the file of the catalog", g->title);
        return;
    }
    char path[PATH_MAX_], name[16];
    int ok;
    if (s->path[0]) {
        ksnprintf(path, sizeof path, "%s", s->path);
        ok = fat_replace(path, d, len) == 0;
    } else if (new_path(g->id, is_b16(g->file.path) ? ".B16" : ".BM", name, sizeof name) == 0) {
        ksnprintf(path, sizeof path, GAMES_DIR "/%s", name);
        ok = fat_mkdirs(GAMES_DIR) == 0 && fat_write_file(GAMES_DIR, name, d, len) == 0;
    } else {
        ok = 0;
    }
    free(d);
    s->busy = 0;
    if (!ok) {
        s->failed = 1;
        say("cannot write %s: %s", g->title, fat_error());
        return;
    }
    owned_set(g->id, g->file.sha256, path);
    if (owned_save() != 0)
        kprintf("market: cannot save the list of games: %s\n", fat_error());
    ksnprintf(s->path, sizeof s->path, "%s", path);
    s->update = 0;
    changed = 1;
    kprintf("market: %s -> %s\n", g->file.path, path);
    say("%s installed", g->title);
}

/* ---------------------------------------------------------------- the fiber */

static void job_main(void *arg)
{
    (void)arg;
    switch (job) {
    case JOB_CACHE: job_cache(); break;
    case JOB_INDEX: job_index(); break;
    case JOB_COVER: job_cover(job_game); break;
    case JOB_GET:   job_get(job_game); break;
    }
}

static int start_job(void)
{
    int i;
    if (!inited) {
        job = JOB_CACHE;
    } else if (!catalog_has_key()) {
        return 0;
    } else if (want_id[0] && online()) {
        i = find(want_id);
        want_id[0] = 0;
        if (i < 0)
            return 0;
        job = JOB_GET;
        job_game = i;
    } else if (!fetched && !net_failed && online()) {
        job = JOB_INDEX;
    } else if ((i = pick_cover()) >= 0) {
        job = JOB_COVER;
        job_game = i;
    } else {
        return 0;
    }
    if (!stack && !(stack = malloc(STACK_SIZE)))
        return 0;
    fiber_prepare(&fib, stack, STACK_SIZE, job_main, NULL);
    running = 1;
    return 1;
}

void market_set_active(int on)
{
    if (on && !active) {
        active = 1;
        if (fetched && timer_ticks() - fetched_at > REFRESH_US)
            fetched = 0;
        net_failed = 0;                 /* each visit tries the network again */
    } else if (!on && active) {
        active = 0;
        if (running) {                  /* stop it now: it ends at its next wait */
            fiber_cancel(&fib);
            while (fiber_resume(&fib))
                ;
            running = 0;
        }
    }
}

void market_tick(uint32_t until)
{
    if (!active)
        return;
    while ((int32_t)(until - timer_ticks()) > 0) {
        if (!running && !start_job())
            return;
        if (!fiber_resume(&fib))
            running = 0;
    }
}

void market_select(int i)
{
    sel = i;
}

/* ---------------------------------------------------------------- what the menu shows */

static int loading(void)
{
    return !inited || (running && job == JOB_INDEX) || (!fetched && !net_failed && online());
}

int market_items(menu_item_t *items, int max)
{
    if (!have) {
        if (!loading() || (inited && !catalog_has_key()))
            return 0;
        int n = SKELETONS < max ? SKELETONS : max;
        for (int i = 0; i < n; i++)
            items[i] = (menu_item_t){ .title = "", .author = "", .path = "", .kind = "market", .loading = 1 };
        return n;
    }
    int n = cat.n < max ? cat.n : max;
    for (int i = 0; i < n; i++) {
        const catalog_game_t *g = &cat.games[i];
        const slot_t *s = &slots[i];
        int ready = s->cover_state == COVER_READY && s->cover.px;
        items[i] = (menu_item_t){
            .title = g->title, .author = g->author, .path = s->path, .kind = "market",
            .size = g->file.size, .cover = ready ? &s->cover : NULL, .loading = !ready,
            .busy = s->busy || strcmp(want_id, g->id) == 0,
            .percent = s->busy && g->file.size ? (int)((uint64_t)s->got * 100 / g->file.size) : 0,
            .badge = s->busy ? NULL : s->path[0] ? (s->update ? "Update" : "Installed")
                   : s->failed ? "Retry" : NULL,
        };
    }
    return n;
}

const char *market_banner(void)
{
    if (inited && !catalog_has_key())
        return "The Market needs a key: scripts/market-key.sh";
    if (have)
        return cat.n ? NULL : "The Market is empty";
    if (loading())
        return "Loading the Market...";
    if (!online())
        return "No network: connect in Settings > WiFi";
    ksnprintf(banner, sizeof banner, "Market: %s", note[0] ? note : "no catalog");
    return banner;
}

const char *market_status(void)
{
    static char line[48];
    long rx = lan_receiving();
    lan_offer_t o;
    if (rx >= 0 && lan_offer(&o) == 0) {
        ksnprintf(line, sizeof line, "receiving: %ld KiB", rx / 1024);
        return line;
    }
    if (note[0] && timer_ticks() - note_at < NOTE_US)
        return note;
    if (!have)
        return "";
    if (!online())
        return "Offline: the saved catalog";
    if (running && job == JOB_INDEX)
        return "Checking for new games...";
    const char *s = cat.serial;
    int n = ksnprintf(line, sizeof line, "%d games", cat.n);
    if (strlen(s) >= 8 && n + 12 < (int)sizeof line)     /* the date of the serial */
        ksnprintf(line + n, sizeof line - (size_t)n, ", %c%c%c%c-%c%c-%c%c",
                  s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    return line;
}

void market_details(int i, char *buf, size_t n)
{
    buf[0] = 0;
    if (!have || i < 0 || i >= cat.n)
        return;
    const catalog_game_t *g = &cat.games[i];
    const slot_t *s = &slots[i];
    if (s->busy)
        ksnprintf(buf, n, "downloading %lu of %lu KiB", (unsigned long)(s->got / 1024),
                  (unsigned long)((g->file.size + 1023) / 1024));
    else
        ksnprintf(buf, n, "%s   v%s   %s   %lu KiB", g->author[0] ? g->author : "-", g->version, g->license,
                  (unsigned long)((g->file.size + 1023) / 1024));
}

int market_action(int i)
{
    if (!have || i < 0 || i >= cat.n)
        return MARKET_NONE;
    const slot_t *s = &slots[i];
    if (s->busy || strcmp(want_id, cat.games[i].id) == 0)
        return MARKET_BUSY;
    if (s->path[0])
        return s->update ? MARKET_UPDATE : MARKET_PLAY;
    return MARKET_GET;
}

const char *market_action_label(int i)
{
    switch (market_action(i)) {
    case MARKET_GET: return "Get";
    case MARKET_PLAY: return "Play";
    case MARKET_UPDATE: return "Update";
    }
    return NULL;
}

void market_ask(int i, home_do_t *d)
{
    const catalog_game_t *g = &cat.games[i];
    int up = slots[i].path[0] != 0;
    d->what = HOME_ASK;
    ksnprintf(d->ask, sizeof d->ask, "%s %s?", up ? "Update" : "Download", g->title);
    if (up)
        ksnprintf(d->ask_detail, sizeof d->ask_detail, "The Market's version replaces it; saves stay.");
    else
        ksnprintf(d->ask_detail, sizeof d->ask_detail, "%lu KiB, free, license %s",
                  (unsigned long)((g->file.size + 1023) / 1024), g->license);
    ksnprintf(d->ask_yes, sizeof d->ask_yes, "%s", up ? "Update" : "Download");
}

void market_get(int i)
{
    if (!have || i < 0 || i >= cat.n)
        return;
    ksnprintf(want_id, sizeof want_id, "%s", cat.games[i].id);
    slots[i].failed = 0;
    if (!online())
        say("no network: the download waits for it");
}

const char *market_path(int i)
{
    return have && i >= 0 && i < cat.n ? slots[i].path : "";
}

/* ---------------------------------------------------------------- options */

void market_panel(int i, home_panel_t *p)
{
    memset(p, 0, sizeof *p);
    if (!have || i < 0 || i >= cat.n)
        return;
    const catalog_game_t *g = &cat.games[i];
    const slot_t *s = &slots[i];
    /* the panel keeps the help's pointer: a copy, not the catalog's (a new
     * catalog can replace it while the panel is open) */
    static char about[sizeof g->about];
    ksnprintf(about, sizeof about, "%s", g->about[0] ? g->about : "A free game of the bm Market");
    ksnprintf(p->title, sizeof p->title, "Market > %s", g->title);
    switch (market_action(i)) {
    case MARKET_GET:
        home_row(p, MENU_ROW_ACTION, M_GET, "Download", about, NULL);
        break;
    case MARKET_UPDATE:
        home_row(p, MENU_ROW_ACTION, M_UPDATE, "Update", "A new version is in the Market", NULL);
        home_row(p, MENU_ROW_ACTION, M_PLAY, "Play this version", about, NULL);
        break;
    case MARKET_PLAY:
        home_row(p, MENU_ROW_ACTION, M_PLAY, "Play", about, NULL);
        home_row(p, MENU_ROW_ACTION, M_UPDATE, "Download again",
                 "The Market's file replaces the one on the SD card", NULL);
        break;
    default:
        home_row(p, MENU_ROW_INFO, M_GET, "Downloading", about, "%lu%%",
                 g->file.size ? (unsigned long)((uint64_t)s->got * 100 / g->file.size) : 0ul);
        break;
    }
    home_row(p, MENU_ROW_INFO, M_AUTHOR, "Author", "From the cartridge's header", "%s",
             g->author[0] ? g->author : "-");
    home_row(p, MENU_ROW_INFO, M_VERSION, "Version", "Given by the author", "%s", g->version);
    home_row(p, MENU_ROW_INFO, M_LICENSE, "License", "What you may do with it", "%s", g->license);
    home_row(p, MENU_ROW_INFO, M_SIZE, "Size", "The whole cartridge", "%lu KiB",
             (unsigned long)((g->file.size + 1023) / 1024));
    home_row(p, MENU_ROW_INFO, M_FILE, "File", s->path[0] ? "On the SD card" : "Not on the SD card yet",
             "%s", s->path[0] ? s->path : "-");
    if (s->path[0] && !s->busy)
        home_row(p, MENU_ROW_ACTION, M_DELETE, "Delete from the SD card",
                 "The game leaves the SD card; its saves stay", NULL);
}

void market_act(int i, int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    if (!have || i < 0 || i >= cat.n)
        return;
    const catalog_game_t *g = &cat.games[i];
    slot_t *s = &slots[i];
    switch (row) {
    case M_GET:
    case M_UPDATE:
        if (market_action(i) == MARKET_BUSY)
            break;
        if (how == 0) {
            market_ask(i, d);
        } else if (how == HOME_YES) {
            market_get(i);
            d->what = HOME_BACK;
        }
        break;
    case M_DELETE:
        if (how == 0) {
            d->what = HOME_ASK;
            ksnprintf(d->ask, sizeof d->ask, "Delete %s?", g->title);
            ksnprintf(d->ask_detail, sizeof d->ask_detail, "It can be downloaded again; saves stay.");
            ksnprintf(d->ask_yes, sizeof d->ask_yes, "Delete");
        } else if (how == HOME_YES) {
            if (fat_delete(s->path) == 0) {
                ksnprintf(d->note, sizeof d->note, "deleted %s", s->path);
                owned_drop(g->id);
                if (owned_save() != 0)
                    kprintf("market: cannot save the list of games: %s\n", fat_error());
                s->path[0] = 0;
                s->update = 0;
                changed = 1;
                d->what = HOME_BACK;
            } else {
                ksnprintf(d->note, sizeof d->note, "cannot delete %s: %s", s->path, fat_error());
            }
            kprintf("market: %s\n", d->note);
        }
        break;
    }
}

static int same_ci(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

int market_lookup(const char *title, const char *author, char *id, size_t idn, char *version,
                  size_t vn, char *license, size_t ln, char *about, size_t an)
{
    for (int i = 0; have && i < cat.n; i++) {
        const catalog_game_t *g = &cat.games[i];
        if (same_ci(g->title, title) && same_ci(g->author, author)) {
            ksnprintf(id, idn, "%s", g->id);
            ksnprintf(version, vn, "%s", g->version);
            ksnprintf(license, ln, "%s", g->license);
            ksnprintf(about, an, "%s", g->about);
            return 0;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- nearby consoles (M24) */

static void lan_name(char *name, size_t n)
{
    const char *c = config_get("name");
    if (c && c[0])
        ksnprintf(name, n, "%s", c);
    else                                /* the last number of the address: bm-108 */
        ksnprintf(name, n, "bm-%lu", (unsigned long)(net_ip() >> 24));
}

/* the catalog's game with these bytes, or -1 */
static int find_sha(const uint8_t *sha)
{
    for (int i = 0; have && i < cat.n; i++)
        if (memcmp(cat.games[i].file.sha256, sha, 32) == 0)
            return i;
    return -1;
}

/* a game from a nearby console, its bytes already checked against what the
 * sender announced: the Market's own goes where the Market would put it;
 * another replaces the game with its title and author, or gets a new name */
static void keep_received(uint8_t *d, size_t len, const lan_offer_t *o)
{
    char path[PATH_MAX_], name[16], id[24];
    int ok = 0, g = find_sha(o->sha256);
    if (len < 128 || !bm_is_cart(d)) {
        say("%s from %s: not a cartridge", o->title, o->from);
        free(d);
        return;
    }
    const char *same = g >= 0 && slots[g].path[0] ? slots[g].path : carts_find_title(o->title, o->author);
    if (same) {
        ksnprintf(path, sizeof path, "%s", same);
        ok = fat_replace(path, d, len) == 0;
    } else {
        if (g >= 0)
            ksnprintf(id, sizeof id, "%s", cat.games[g].id);
        else if (github_id_from_name(o->title, id, sizeof id) != 0)
            ksnprintf(id, sizeof id, "game");
        if (new_path(id, g >= 0 && is_b16(cat.games[g].file.path) ? ".B16" : ".BM", name, sizeof name) == 0) {
            ksnprintf(path, sizeof path, GAMES_DIR "/%s", name);
            ok = fat_mkdirs(GAMES_DIR) == 0 && fat_write_file(GAMES_DIR, name, d, len) == 0;
        }
    }
    free(d);
    if (!ok) {
        say("cannot write %s: %s", o->title, fat_error());
        return;
    }
    if (g >= 0) {                       /* the Market's game: as if downloaded */
        owned_set(cat.games[g].id, o->sha256, path);
        if (owned_save() != 0)
            kprintf("market: cannot save the list of games: %s\n", fat_error());
        ksnprintf(slots[g].path, sizeof slots[g].path, "%s", path);
        slots[g].update = 0;
    }
    changed = 1;
    kprintf("market: %s from %s -> %s\n", o->title, o->from, path);
    say("%s received from %s", o->title, o->from);
}

void market_lan(int on)
{
    if (on && !lan_running() && net_ip()) {
        char name[24];
        lan_name(name, sizeof name);
        if (lan_start(name) == 0)
            kprintf("market: nearby consoles: listening as %s\n", name);
        else
            kprintf("market: cannot listen for nearby consoles\n");
    } else if (!on && lan_running()) {
        lan_stop();
    }
    if (!lan_running())
        return;
    lan_poll();
    uint8_t *d;
    size_t len;
    lan_offer_t o;
    if (lan_take(&d, &len, &o))
        keep_received(d, len, &o);
}

int market_offer(char *q, size_t qn, char *detail, size_t dn)
{
    lan_offer_t o;
    if (!lan_offer(&o))
        return 0;
    ksnprintf(q, qn, "%s sends %s", o.from, o.title);
    unsigned long kib = (o.size + 1023) / 1024;
    if (find_sha(o.sha256) >= 0)
        ksnprintf(detail, dn, "%lu KiB, the Market's own: checked.", kib);
    else
        ksnprintf(detail, dn, "%lu KiB, not in the Market: from friends only.", kib);
    return 1;
}

void market_offer_answer(int yes)
{
    lan_offer_t o;
    if (lan_offer(&o))
        say("%s %s", yes ? "receiving" : "refused", o.title);
    lan_answer(yes);
}

static struct {
    char path[PATH_MAX_], title[49], author[33];
    lan_peer_t peers[LAN_MAX_PEERS], to;
    int npeers;
} snd;

void market_send_setup(const char *path, const char *title, const char *author)
{
    memset(&snd, 0, sizeof snd);
    ksnprintf(snd.path, sizeof snd.path, "%s", path);
    ksnprintf(snd.title, sizeof snd.title, "%s", title);
    ksnprintf(snd.author, sizeof snd.author, "%s", author);
}

void market_send_panel(home_panel_t *p)
{
    memset(p, 0, sizeof *p);
    ksnprintf(p->title, sizeof p->title, "Send > %s", snd.title);
    char name[24];
    lan_name(name, sizeof name);
    home_row(p, MENU_ROW_INFO, S_THIS, "This console", "Its name for the others (name= in bm/config.txt)",
             "%s", net_ip() ? name : "no network");
    snd.npeers = lan_peers(snd.peers, LAN_MAX_PEERS);
    for (int i = 0; i < snd.npeers && p->n < HOME_ROWS_MAX; i++) {
        ip4_addr_t a;
        ip4_addr_set_u32(&a, snd.peers[i].ip);
        home_row(p, MENU_ROW_ACTION, S_PEER + i, snd.peers[i].name, "Its player is asked first",
                 "%s", ip4addr_ntoa(&a));
    }
    if (!snd.npeers)
        home_row(p, MENU_ROW_INFO, S_NONE, "No console nearby yet",
                 net_ip() ? "The other one: the Market tab open, on this network"
                          : "Connect in Settings > WiFi and network", NULL);
}

static void send_step(const char *s)
{
    kprintf("  %s\n", s);
}

static void send_run(framebuffer_t *fb)
{
    (void)fb;
    ip4_addr_t a;
    ip4_addr_set_u32(&a, snd.to.ip);
    kprintf("\n\x1b[1;96mSending %s to %s (%s)\x1b[0m\n", snd.title, snd.to.name, ip4addr_ntoa(&a));
    fat_entry_t e;
    uint8_t *d;
    size_t len;
    if (fat_find(snd.path, &e) != 0 || fat_load(&e, &d, &len) != 0) {
        kprintf("\x1b[91mcannot read %s: %s\x1b[0m\n", snd.path, fat_error());
        return;
    }
    char me[24], err[128];
    lan_name(me, sizeof me);
    int r = lan_send(snd.to.ip, me, snd.title, snd.author, d, len, send_step, err, sizeof err);
    free(d);
    if (r == 0)
        kprintf("\x1b[92m%s has it now.\x1b[0m\n", snd.to.name);
    else
        kprintf("\x1b[91mnot sent: %s\x1b[0m\n", err);
}

void market_send_act(int row, int how, home_do_t *d)
{
    memset(d, 0, sizeof *d);
    d->what = HOME_STAY;
    int i = row - S_PEER;
    if (i < 0 || i >= snd.npeers)
        return;
    if (how == 0) {
        snd.to = snd.peers[i];
        d->what = HOME_ASK;
        ksnprintf(d->ask, sizeof d->ask, "Send %s to %s?", snd.title, snd.to.name);
        ksnprintf(d->ask_detail, sizeof d->ask_detail, "Its player is asked first.");
        ksnprintf(d->ask_yes, sizeof d->ask_yes, "Send");
    } else if (how == HOME_YES) {
        d->what = HOME_TEXT;
        d->text = send_run;
        d->wait = 1;
    }
}

int market_take_changed(void)
{
    int c = changed;
    changed = 0;
    return c;
}

void market_carts_changed(void)
{
    if (have)
        refresh_slots();
}
