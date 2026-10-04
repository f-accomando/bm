#include "update.h"
#include "config.h"
#include "crumbs.h"
#include "input.h"
#include "version.h"
#include "drivers/timer.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "fs/fat.h"
#include "lib/printf.h"
#include "net/http.h"
#include "net/net.h"
#include "net/release.h"

#include "mbedtls/sha256.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_URL "https://github.com/f-accomando/bm/releases"
#define BACKUP_DIR  "/bm/backup"

/* this kernel's file on the SD card, the other board's, the release's
 * manifest for this console and where its menu has the pages */
#if defined(BM_RGB30)
#define OWN_KERNEL   "/kernel8.img"
#define OTHER_KERNEL ""                 /* the Pi's kernels are not the RGB30's */
#define MANIFEST     "manifest-rgb30"
#define WHERE_CHECK  "System > Updates"
#define WHERE_NET    "System > WiFi"
#define WHERE_INSTALL "System > Updates"
#elif defined(BM_ZERO2)
#define OWN_KERNEL   "/kernel7.img"
#define OTHER_KERNEL "/kernel.img"
#else
#define OWN_KERNEL   "/kernel.img"
#define OTHER_KERNEL "/kernel7.img"
#endif
#ifndef MANIFEST
#define MANIFEST     "manifest"
#define WHERE_CHECK  "Settings > System > Check for updates"
#define WHERE_NET    "Settings > WiFi and network"
#define WHERE_INSTALL "Settings > System > Install the update"
#endif

enum { F_SKIP, F_SAME, F_NEW, F_CHANGED, F_ABSENT_GAME };

static struct {
    int checked, ok;
    char state[64], ready[24];
    char base[160];
    release_t rel;
    int cmp;
    int what[RELEASE_MAX_FILES];    /* F_* for each file of rel */
    int n;
    uint32_t bytes;
} up;

static int from_sd(void)
{
    return strncmp(up.base, "sd:", 3) == 0;
}

static int is_kernel(const char *path)
{
    return strcmp(path, "/kernel.img") == 0 || strcmp(path, "/kernel7.img") == 0 ||
           strcmp(path, "/kernel8.img") == 0;
}

/* the "bmK6" / "bmK7" mark at +4 (src/boot/start.S): a kernel for the board
 * its file name says; kernel8.img (the RGB30's) is an arm64 Image, "ARM\x64"
 * at +56 (src/rgb30/start.S) */
static int kernel_mark_ok(const char *path, const uint8_t *d, size_t len)
{
    if (strcmp(path, "/kernel8.img") == 0)
        return len >= 64 && memcmp(d + 56, "ARM\x64", 4) == 0;
    char want = strcmp(path, "/kernel7.img") == 0 ? '7' : '6';
    return len >= 8 && memcmp(d + 4, "bmK", 3) == 0 && d[7] == want;
}

/* ---------------------------------------------------------------- fetching */

typedef struct {
    uint8_t *buf;
    size_t len, cap;
    uint32_t next;              /* bytes at which to print the progress */
    const char *name;
} sink_t;

static int sink(void *p, const uint8_t *d, size_t n)
{
    sink_t *s = p;
    if (n > s->cap - s->len)
        return 1;                       /* more than the manifest says */
    memcpy(s->buf + s->len, d, n);
    s->len += n;
    if (s->name && s->len >= s->next) {
        kprintf("\r  %-20s %3lu%%", s->name, (unsigned long)((uint64_t)s->len * 100 / s->cap));
        s->next += s->cap / 10 + 1;
    }
    return 0;
}

/* A file of the release (a URL's tail, or a name in the SD folder), at most
 * max bytes, into a malloc'd buffer. 0, or -1 with err. */
static int fetch(const char *url_tail, const char *sd_name, size_t max, const char *show,
                 uint8_t **data, size_t *len, char *err, size_t err_len)
{
    *data = NULL;
    *len = 0;
    if (from_sd()) {
        char path[sizeof up.base + 64];
        ksnprintf(path, sizeof path, "%s%s", up.base + 3, sd_name);
        fat_entry_t e;
        if (fat_find(path, &e) != 0 || e.is_dir) {
            ksnprintf(err, err_len, "%s: not found", path);
            return -1;
        }
        if (e.size > max || fat_load(&e, data, len) != 0) {
            ksnprintf(err, err_len, "%s: %s", path, e.size > max ? "too big" : fat_error());
            return -1;
        }
        return 0;
    }
    char url[sizeof up.base + 96];
    ksnprintf(url, sizeof url, "%s/%s", up.base, url_tail);
    sink_t s = { malloc(max ? max : 1), 0, max, 0, show };
    if (!s.buf) {
        ksnprintf(err, err_len, "out of memory");
        return -1;
    }
    http_req_t req = { .timeout_ms = 20000 };
    static http_info_t info;
    int st = http_request(url, &req, sink, &s, &info);
    if (show)
        kprintf("\n");
    if (st != 200 || info.error[0]) {
        free(s.buf);
        if (st == 404)
            ksnprintf(err, err_len, "%s: not found (no release yet?)", url_tail);
        else if (st < 0 || info.error[0])
            ksnprintf(err, err_len, "%s", info.error);
        else
            ksnprintf(err, err_len, "%s: HTTP %d", url_tail, st);
        return -1;
    }
    *data = s.buf;
    *len = s.len;
    return 0;
}

static void settings(void)
{
    const char *u = config_get("update_url");
    ksnprintf(up.base, sizeof up.base, "%s", u && u[0] ? u : DEFAULT_URL);
    size_t n = strlen(up.base);
    if (from_sd()) {                    /* a folder: ends with / */
        if (n && up.base[n - 1] != '/' && n + 1 < sizeof up.base)
            strcpy(up.base + n, "/");
    } else {
        while (n && up.base[n - 1] == '/')
            up.base[--n] = 0;
    }
    /* a key of one's own on the SD card, besides the built-in one */
    fat_entry_t e;
    uint8_t *d;
    size_t len;
    if (config_find_file("release.pem", &e) == 0 && fat_load(&e, &d, &len) == 0) {
        char *pem = malloc(len + 1);
        if (pem) {
            memcpy(pem, d, len);
            pem[len] = 0;
            if (release_add_key(pem) != 0)
                kprintf("update: bm/release.pem is not a P-256 public key\n");
            free(pem);
        }
        free(d);
    }
}

/* ---------------------------------------------------------------- the check */

static const char *cmp_text(int c)
{
    switch (c) {
    case RELEASE_NEWER: return "newer than this kernel";
    case RELEASE_SAME:  return "the one this kernel is";
    case RELEASE_OLDER: return "older than this kernel";
    case RELEASE_DEV:   return "this kernel is a build of the sources";
    }
    return "not a version (vX.Y.Z)";
}

/* what each file of the release is to this SD card */
static void plan(void)
{
    up.n = 0;
    up.bytes = 0;
    for (int i = 0; i < up.rel.nfiles; i++) {
        const release_file_t *f = &up.rel.files[i];
        fat_entry_t e;
        uint8_t *d;
        size_t len;
        int w;
        if (fat_find(f->path, &e) != 0 || e.is_dir) {
            /* a game deleted from the card stays deleted (the Market has
             * it); a kernel or the certificates go there anyway */
            w = strncmp(f->path, "/carts/", 7) == 0 ? F_ABSENT_GAME : F_NEW;
        } else if (e.size == f->size && fat_load(&e, &d, &len) == 0) {
            w = release_check_file(f, d, len) == 0 ? F_SAME : F_CHANGED;
            free(d);
        } else {
            w = F_CHANGED;
        }
        up.what[i] = w;
        if (w == F_NEW || w == F_CHANGED) {
            up.n++;
            up.bytes += f->size;
        }
    }
}

void update_check(framebuffer_t *fb)
{
    (void)fb;
    uint8_t *man = NULL, *sig = NULL;
    size_t ml, sl;
    char err[192];
    memset(&up, 0, sizeof up);
    up.checked = 1;
    settings();
    kprintf("\n\x1b[1;96mbm update\x1b[0m\n");
    kprintf("  this kernel: %s\n", bm_version);
    kprintf("  releases: %s\n", up.base);
    if (!from_sd() && !net_ip()) {
        ksnprintf(up.state, sizeof up.state, "no network");
        kprintf("\x1b[91mno network: connect in " WHERE_NET "\x1b[0m\n");
        return;
    }
    kprintf("  reading the latest release...\n");
    if (fetch("latest/download/" MANIFEST ".txt", MANIFEST ".txt", 16384, NULL, &man, &ml, err, sizeof err) != 0 ||
        fetch("latest/download/" MANIFEST ".sig", MANIFEST ".sig", 1024, NULL, &sig, &sl, err, sizeof err) != 0 ||
        release_verify(man, ml, sig, sl, err, sizeof err) != 0 ||
        release_parse(man, ml, &up.rel, err, sizeof err) != 0) {
        free(man);
        free(sig);
        ksnprintf(up.state, sizeof up.state, "check failed");
        kprintf("\x1b[91m%s\x1b[0m\n", err);
        return;
    }
    free(man);
    free(sig);
    up.cmp = release_compare(bm_version, up.rel.version);
    kprintf("  latest release: \x1b[1m%s\x1b[0m%s%s, signed: good\n", up.rel.version,
            up.rel.commit[0] ? ", commit " : "", up.rel.commit);
    kprintf("  that is: %s\n", cmp_text(up.cmp));
    if (up.cmp == RELEASE_BAD) {
        ksnprintf(up.state, sizeof up.state, "bad release %s", up.rel.version);
        return;
    }
    plan();
    static const char *const words[] = { "", "same", "new", "changed", "not on the card (Market)" };
    for (int i = 0; i < up.rel.nfiles; i++) {
        const release_file_t *f = &up.rel.files[i];
        kprintf("  %-22s %6lu KiB  %s\n", f->path, (unsigned long)((f->size + 1023) / 1024), words[up.what[i]]);
    }
    up.ok = 1;
    if (up.n && (up.cmp == RELEASE_NEWER || up.cmp == RELEASE_DEV)) {
        ksnprintf(up.state, sizeof up.state, "%s: %d files, %lu KiB", up.rel.version, up.n,
                  (unsigned long)((up.bytes + 1023) / 1024));
        ksnprintf(up.ready, sizeof up.ready, "%s", up.rel.version);
        kprintf("\x1b[92m%s can be installed: " WHERE_INSTALL "\x1b[0m\n", up.rel.version);
    } else {
        ksnprintf(up.state, sizeof up.state, "up to date (%s)", up.rel.version);
        kprintf("\x1b[92mnothing to install\x1b[0m\n");
    }
}

/* ---------------------------------------------------------------- the install */

static int write_file(const char *path, const uint8_t *d, size_t len)
{
    fat_entry_t e;
    if (fat_find(path, &e) == 0 && !e.is_dir)
        return fat_replace(path, d, len);
    char dir[64];
    const char *slash = strrchr(path, '/');
    size_t n = (size_t)(slash - path);
    if (n >= sizeof dir)
        return -1;
    memcpy(dir, path, n);
    dir[n] = 0;
    if (n && fat_mkdirs(dir) != 0)
        return -1;
    return fat_write_file(n ? dir : "/", slash + 1, d, len);
}

void update_install(framebuffer_t *fb)
{
    (void)fb;
    if (!up.ok || !up.ready[0]) {
        kprintf("\x1b[91mno update checked: " WHERE_CHECK "\x1b[0m\n");
        return;
    }
    kprintf("\n\x1b[1;96mbm update: installing %s\x1b[0m\n", up.rel.version);
    if (!from_sd() && !net_ip()) {
        kprintf("\x1b[91mno network: connect in " WHERE_NET "\x1b[0m\n");
        return;
    }
    uint8_t *data[RELEASE_MAX_FILES] = { 0 };
    size_t len[RELEASE_MAX_FILES] = { 0 };
    char err[192];
    int failed = 0;
    uint32_t t0 = timer_ticks();

    /* 1. everything downloaded and checked first: nothing written yet */
    for (int i = 0; i < up.rel.nfiles && !failed; i++) {
        const release_file_t *f = &up.rel.files[i];
        if (up.what[i] != F_NEW && up.what[i] != F_CHANGED)
            continue;
        char tail[96];
        ksnprintf(tail, sizeof tail, "download/%s/%s", up.rel.version, f->asset);
        if (fetch(tail, f->asset, f->size, f->asset, &data[i], &len[i], err, sizeof err) != 0) {
            kprintf("\x1b[91m%s: %s\x1b[0m\n", f->asset, err);
            failed = 1;
        } else if (release_check_file(f, data[i], len[i]) != 0) {
            kprintf("\x1b[91m%s: not the file of the manifest (size or SHA-256)\x1b[0m\n", f->asset);
            failed = 1;
        } else if (is_kernel(f->path) && !kernel_mark_ok(f->path, data[i], len[i])) {
            kprintf("\x1b[91m%s: not a kernel for %s\x1b[0m\n", f->asset, f->path);
            failed = 1;
        }
    }
    if (failed) {
        for (int i = 0; i < up.rel.nfiles; i++)
            free(data[i]);
        kprintf("\x1b[91mnothing was written; the console is as it was\x1b[0m\n");
        return;
    }
    kprintf("  all %d files downloaded and checked (%lu s)\n", up.n, (timer_ticks() - t0) / 1000000);

    /* 2. a copy of the kernels that are replaced: to go back from a PC */
    for (int i = 0; i < up.rel.nfiles; i++) {
        const release_file_t *f = &up.rel.files[i];
        fat_entry_t e;
        uint8_t *old;
        size_t ol;
        if (!data[i] || !is_kernel(f->path) || up.what[i] != F_CHANGED)
            continue;
        if (fat_find(f->path, &e) == 0 && fat_load(&e, &old, &ol) == 0) {
            int ok = fat_mkdirs(BACKUP_DIR) == 0 && fat_write_file(BACKUP_DIR, f->path + 1, old, ol) == 0;
            kprintf("  %s kept in %s%s\n", f->path, BACKUP_DIR, ok ? "" : " (failed)");
            free(old);
        }
    }

    /* 3. the files: the games and the certificates, the other board's kernel,
     * then this one's */
    const char *order[3] = { "", OTHER_KERNEL, OWN_KERNEL };
    for (int pass = 0; pass < 3 && !failed; pass++)
        for (int i = 0; i < up.rel.nfiles && !failed; i++) {
            const release_file_t *f = &up.rel.files[i];
            if (!data[i] || (pass == 0 ? is_kernel(f->path) : strcmp(f->path, order[pass]) != 0))
                continue;
            if (write_file(f->path, data[i], len[i]) != 0) {
                kprintf("\x1b[91mcannot write %s: %s\x1b[0m\n", f->path, fat_error());
                failed = 1;
            } else {
                kprintf("  written %s\n", f->path);
            }
        }
    for (int i = 0; i < up.rel.nfiles; i++)
        free(data[i]);
    if (failed) {
        kprintf("\x1b[91mthe update stopped; the kernels in %s are the ones of before\x1b[0m\n", BACKUP_DIR);
        return;
    }
    kprintf("\x1b[92m%s installed: restarting...\x1b[0m\n", up.rel.version);
    timer_delay_ms(1500);               /* the line on the screen */
    crumbs_clean_exit();
    uart_flush();
    watchdog_reboot();
}

#ifndef BM_RGB30                        /* the RGB30 has no monitor */
void update_monitor(void)
{
    update_check(NULL);
    if (!update_ready())
        return;
    kprintf("install %s and restart? (y/n) ", up.rel.version);
    char line[8];
    if (input_read_line(line, sizeof line, 0) > 0 && (line[0] == 'y' || line[0] == 'Y'))
        update_install(NULL);
    else
        kprintf("not installed\n");
}
#endif

const char *update_state(void)
{
    return up.checked ? up.state : "not checked";
}

const char *update_ready(void)
{
    return up.ok && up.ready[0] ? up.ready : NULL;
}
