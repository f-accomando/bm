/*
 * Deflate Bench (utility): times png_inflate (src/bm/png.c) on a corpus of real cartridge data. The corpus is
 * defbench.bm (scripts/defbench_make.py): a DATA section of blobs, each "u32 raw size, u32 CRC-32 of the raw
 * bytes, zlib stream". The report says MB/s of raw bytes per blob, the SD read speed and a memcpy baseline.
 */
#include "defbench.h"
#include "bm/bm.h"
#include "bm/png.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "lib/crc32.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

static const char *const blobs[] = { "lua", "sheet", "sheet8", "mesh", "anim", "map", "audio" };

static void rate(const char *what, uint32_t bytes, uint32_t us)
{
    if (!us)
        us = 1;
    uint32_t kbs = (uint32_t)(((uint64_t)bytes * 1000000u / 1024u) / us);
    kprintf("%-22s %8u KB in %7u ms = %6u KB/s\n", what, (unsigned)(bytes / 1024), (unsigned)(us / 1000),
            (unsigned)kbs);
}

void defbench_run(framebuffer_t *fb)
{
    (void)fb;
    static const char *const paths[] = { "/bm/defbench.bm", "/bm/DEFBENCH.BM", "/carts/defbench.bm" };
    fat_entry_t e;
    uint8_t *file = NULL;
    size_t flen = 0;
    int found = 0;
    uint32_t t0 = 0, t1 = 0;

    kprintf("Deflate Bench: png_inflate on real cartridge data\n\n");
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0] && !found; i++) {
        if (fat_find(paths[i], &e) != 0)
            continue;
        t0 = timer_ticks();
        found = fat_load(&e, &file, &flen) == 0;
        t1 = timer_ticks();
        if (found)
            kprintf("file %s\n", paths[i]);
    }
    if (!found) {
        kprintf("defbench.bm not found in /bm or /carts\n");
        return;
    }
    rate("SD read", (uint32_t)flen, t1 - t0);
    kprintf("\n%-8s %9s %9s %6s  %-24s %s\n", "blob", "raw KB", "zip KB", "ratio", "inflate", "CRC");

    uint64_t raw_all = 0, zip_all = 0, us_all = 0;
    for (unsigned i = 0; i < sizeof blobs / sizeof blobs[0]; i++) {
        const uint8_t *p;
        uint32_t n;
        if (bm_data_find(file, flen, blobs[i], &p, &n) != 0 || n < 10)
            continue;
        uint32_t raw = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
        uint32_t crc = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
        uint8_t *out = malloc(raw);
        if (!out) {
            kprintf("%-8s no memory for %u KB\n", blobs[i], (unsigned)(raw / 1024));
            continue;
        }
        t0 = timer_ticks();
        long got = png_inflate(p + 8, n - 8, out, raw);
        t1 = timer_ticks();
        int ok = got == (long)raw && crc32(out, raw) == crc;
        uint32_t us = t1 - t0;
        uint32_t kbs = (uint32_t)(((uint64_t)raw * 1000000u / 1024u) / (us ? us : 1));
        kprintf("%-8s %9u %9u %5u.%u  %5u ms %6u KB/s  %s\n", blobs[i], (unsigned)(raw / 1024),
                (unsigned)((n - 8) / 1024), (unsigned)(raw / (n - 8)), (unsigned)(raw * 10 / (n - 8) % 10),
                (unsigned)(us / 1000), (unsigned)kbs, ok ? "ok" : "BAD");
        if (ok) {
            raw_all += raw;
            zip_all += n - 8;
            us_all += us;
        }
        free(out);
    }
    kprintf("\n");
    rate("inflate, all blobs", (uint32_t)raw_all, (uint32_t)us_all);
    kprintf("compressed %u KB -> raw %u KB, ratio %u.%02u\n", (unsigned)(zip_all / 1024), (unsigned)(raw_all / 1024),
            (unsigned)(raw_all / (zip_all ? zip_all : 1)), (unsigned)(raw_all * 100 / (zip_all ? zip_all : 1) % 100));

    /* the same number of bytes copied, as the floor the inflate cannot beat */
    uint32_t cp = 4u << 20;
    uint8_t *a = malloc(cp), *b = malloc(cp);
    if (a && b) {
        memset(a, 0x5A, cp);
        t0 = timer_ticks();
        void *(*volatile copy)(void *, const void *, size_t) = memcpy;     /* the compiler keeps the loop */
        for (int i = 0; i < 8; i++)
            copy(b, a, cp);
        t1 = timer_ticks();
        rate("memcpy baseline", 8 * cp, t1 - t0);
    }
    free(a);
    free(b);
    free(file);
}
