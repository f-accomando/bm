#include "vidbench.h"
#include "bm/jpeg.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "kernel/syskeys.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define DIR         "/bm/vidbench"
#define CHUNK       (256u * 1024u)
#define CHUNKS      128
#define READ_BACK   16
#define DECODES     100
#define QUICK_DIV   16              /* "video quick": a sixteenth of each count (the QEMU test) */

static int chunks = CHUNKS, read_back = READ_BACK, decodes = DECODES;

/* MB/s * 100 (1 MB = 10^6 bytes) */
uint32_t vidbench_mbps100(uint64_t bytes, uint32_t us)
{
    return us ? (uint32_t)(bytes * 100u / us) : 0;
}

static void ms2(const char *label, uint32_t us)
{
    kprintf("%s %lu.%02lu ms", label, (unsigned long)(us / 1000), (unsigned long)(us % 1000 / 10));
}

static void name_of(char *name, int i)
{
    ksnprintf(name, 13, "VB%03d.TMP", i);
}

static void remove_files(int n)
{
    char name[13], path[40];
    for (int i = 0; i < n; i++) {
        name_of(name, i);
        ksnprintf(path, sizeof path, DIR "/%s", name);
        fat_delete(path);
    }
}

static void sd_bench(void)
{
    uint8_t *buf = malloc(CHUNK);
    if (!buf || fat_mkdirs(DIR) != 0) {
        kprintf("SD bench: no memory or no %s\n", DIR);
        free(buf);
        return;
    }
    for (uint32_t i = 0; i < CHUNK; i++)            /* not zeros: a card may shortcut them */
        buf[i] = (uint8_t)(i * 2654435761u >> 24);
    kprintf("SD write: %d files of %u KiB in %s\n", chunks, CHUNK / 1024, DIR);
    uint32_t total = 0, best = 0xFFFFFFFFu, worst = 0, over100 = 0;
    int done = 0;
    char name[13];
    for (; done < chunks && !syskeys_test_stop(); done++) {
        name_of(name, done);
        const uint32_t t0 = timer_ticks();
        if (fat_write_file(DIR, name, buf, CHUNK) != 0) {
            kprintf("write %d failed: %s\n", done, fat_error());
            break;
        }
        const uint32_t us = timer_ticks() - t0;
        total += us;
        if (us < best)
            best = us;
        if (us > worst)
            worst = us;
        over100 += us > 100000;
    }
    if (done) {
        const uint32_t w = vidbench_mbps100((uint64_t)done * CHUNK, total);
        kprintf("  %d writes, %lu.%02lu MB/s mean\n", done, (unsigned long)(w / 100), (unsigned long)(w % 100));
        ms2("  best", best);
        ms2("   worst", worst);
        kprintf("   writes over 100 ms: %lu\n", (unsigned long)over100);
        kprintf("  raw 15 fps (go if >= 5.00 MB/s and worst <= 100 ms): %s\n",
                w >= 500 && worst <= 100000 ? "GO" : "NO GO");
    }
    int rd = 0;
    uint64_t bytes = 0;
    uint32_t rus = 0;
    for (; rd < read_back && rd < done && !syskeys_test_stopped(); rd++) {
        char path[40];
        fat_entry_t e;
        name_of(name, rd);
        ksnprintf(path, sizeof path, DIR "/%s", name);
        uint8_t *data = NULL;
        size_t len = 0;
        const uint32_t t0 = timer_ticks();
        if (fat_find(path, &e) != 0 || fat_load(&e, &data, &len) != 0)
            break;
        rus += timer_ticks() - t0;
        bytes += len;
        free(data);
    }
    if (rd) {
        const uint32_t r = vidbench_mbps100(bytes, rus);
        kprintf("SD read: %d files, %lu.%02lu MB/s\n", rd, (unsigned long)(r / 100), (unsigned long)(r % 100));
    }
    remove_files(done);
    free(buf);
}

static void jpeg_case(const char *label, const uint8_t *jpg, unsigned len)
{
    uint32_t total = 0, best = 0xFFFFFFFFu, worst = 0;
    int n = 0;
    for (; n < decodes && !syskeys_test_stop(); n++) {
        uint8_t *rgb = NULL;
        int w, h;
        char err[48];
        const uint32_t t0 = timer_ticks();
        const int r = jpeg_decode(jpg, len, &rgb, &w, &h, err, sizeof err);
        const uint32_t us = timer_ticks() - t0;
        if (r != 0) {
            kprintf("%s: decode failed: %s\n", label, err);
            return;
        }
        free(rgb);
        total += us;
        if (us < best)
            best = us;
        if (us > worst)
            worst = us;
    }
    if (!n)
        return;
    kprintf("%s (%u bytes):", label, len);
    ms2("  mean", total / (uint32_t)n);
    ms2("  best", best);
    ms2("  worst", worst);
    kprintf("  (malloc and free of the 24-bit picture included)\n");
}

void bm_video_bench(int quick)
{
    chunks = quick ? CHUNKS / QUICK_DIV : CHUNKS;
    read_back = quick ? READ_BACK / QUICK_DIV + 1 : READ_BACK;
    decodes = quick ? DECODES / QUICK_DIV + 1 : DECODES;
    syskeys_test_begin();
    kprintf("Video bench: SD write and read, JPEG decode\n");
    sd_bench();
    if (!syskeys_test_stopped()) {
        kprintf("JPEG decode, %d calls each (go if mean <= 66 ms for 15 fps):\n", DECODES);
        jpeg_case("320x180", vidbench_jpg320, vidbench_jpg320_len);
        jpeg_case("480x270", vidbench_jpg480, vidbench_jpg480_len);
    }
    kprintf(syskeys_test_stopped() ? "Video bench stopped\n" : "Video bench done\n");
}
