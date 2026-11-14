/*
 * romtabs.c: the NES and SNES tabs of the menu (romtabs.h). Original code.
 */
#include "romtabs.h"
#include "menu_ui.h"
#include "drivers/timer.h"
#include "fs/fat.h"

#include <stdlib.h>
#include <string.h>

static rom_entry_t roms[ROM_PLATFORMS][ROMTABS_MAX];
static int nroms[ROM_PLATFORMS];

static const char *const dirs[ROM_PLATFORMS] = { "/nes", "/snes" };

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ext_is(const char *name, const char *ext)
{
    const size_t n = strlen(name), e = strlen(ext);
    if (n <= e)
        return 0;
    for (size_t i = 0; i < e; i++)
        if (lower((unsigned char)name[n - e + i]) != ext[i])
            return 0;
    return 1;
}

static int wanted(int plat, const char *name)
{
    if (plat == ROM_NES)
        return ext_is(name, ".nes");
    return ext_is(name, ".sfc") || ext_is(name, ".smc");
}

/* "Super Mario World (USA) [!].sfc" -> "Super Mario World" */
static void make_title(const char *name, char *out, size_t n)
{
    size_t k = 0;
    int depth = 0;
    const char *dot = strrchr(name, '.');
    for (const char *p = name; *p && p != dot && k + 1 < n; p++) {
        if (*p == '(' || *p == '[') { depth++; continue; }
        if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
        if (depth)
            continue;
        out[k++] = *p == '_' ? ' ' : *p;
    }
    while (k && out[k - 1] == ' ')
        k--;
    out[k] = 0;
    if (!k && n > 1)
        strncpy(out, name, n - 1), out[n - 1] = 0;
}

static int title_cmp(const void *a, const void *b)
{
    const char *x = ((const rom_entry_t *)a)->title, *y = ((const rom_entry_t *)b)->title;
    for (;; x++, y++) {
        const int c = lower((unsigned char)*x), d = lower((unsigned char)*y);
        if (c != d || !c)
            return c - d;
    }
}

void romtabs_scan(void)
{
    for (int pl = 0; pl < ROM_PLATFORMS; pl++) {
        static rom_entry_t old[ROMTABS_MAX];
        const int nold = nroms[pl];
        memcpy(old, roms[pl], sizeof old);
        nroms[pl] = 0;
        fat_dir_t d;
        fat_entry_t e;
        if (fat_opendir(&d, dirs[pl]) == 0) {
            while (nroms[pl] < ROMTABS_MAX && fat_readdir(&d, &e)) {
                if (e.is_dir || e.name[0] == '.' || !wanted(pl, e.name))
                    continue;
                rom_entry_t *r = &roms[pl][nroms[pl]++];
                memset(r, 0, sizeof *r);
                strncpy(r->path, dirs[pl], sizeof r->path - 1);
                strncat(r->path, "/", sizeof r->path - strlen(r->path) - 1);
                strncat(r->path, e.name, sizeof r->path - strlen(r->path) - 1);
                make_title(e.name, r->title, sizeof r->title);
                for (int i = 0; i < nold; i++)                  /* a cover made before stays */
                    if (old[i].cover.px && !strcmp(old[i].path, r->path)) {
                        r->cover = old[i].cover;
                        old[i].cover.px = NULL;
                        break;
                    }
            }
        }
        for (int i = 0; i < nold; i++)                          /* the covers of the ROMs that went away */
            if (old[i].cover.px)
                g16_sheet_free(&old[i].cover);
        qsort(roms[pl], (size_t)nroms[pl], sizeof roms[pl][0], title_cmp);
    }
}

int romtabs_count(int plat) { return plat >= 0 && plat < ROM_PLATFORMS ? nroms[plat] : 0; }

rom_entry_t *romtabs_get(int plat, int i)
{
    return plat >= 0 && plat < ROM_PLATFORMS && i >= 0 && i < nroms[plat] ? &roms[plat][i] : NULL;
}

void romtabs_covers_tick(int plat, int sel, uint32_t until)
{
    if (plat < 0 || plat >= ROM_PLATFORMS)
        return;
    const int n = nroms[plat];
    for (int k = 0; k < n && (int32_t)(until - timer_ticks()) > 0; k++) {
        const int i = (sel + k) % n;                            /* the selection first, then the ones after it */
        rom_entry_t *r = &roms[plat][i];
        if (!r->cover.px)
            menu_make_box_cover(&r->cover, r->title, plat == ROM_SNES);
    }
}

const char *romtabs_emulator(int plat) { return plat == ROM_NES ? "nes.bm" : "snes.bm"; }

const char *romtabs_empty_hint(int plat)
{
    return plat == ROM_NES ? "Put .nes files in the /nes folder of the SD card" : "Put .sfc or .smc files in the /snes folder of the SD card";
}
