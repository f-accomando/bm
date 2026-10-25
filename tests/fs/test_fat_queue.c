/*
 * Host test for the steps of a queued file_write (src/bm/webfile.c): the SD card is an image file.
 *
 *   test_fat_queue IMAGE
 *
 * webfile.c writes a queued file in a fiber: fat_write_tick (fiber_slice) is where the cartridge's
 * frame goes on. This test counts, between two ticks, the sectors written and read and the
 * commands, for files of 1 KB to 4 MB (2048 KB is the size of tests/web/pi_bench.lua): a new file,
 * a file replaced by a bigger one, by a smaller one and by one of the same size. No step may write
 * more than one piece of data (32 KB: a run) plus the FAT sectors and the entries of that piece.
 * A step that allocates the whole chain, or writes the whole data, before a tick fails here.
 * The image is made by tests/fs/run_queue.py (a card with 4 KB or 32 KB clusters).
 */
#define _DEFAULT_SOURCE
#include "fs/fat.h"
#include "drivers/sd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *img;
static uint32_t w_sec, r_sec, w_cmd, r_cmd;     /* since the last tick */

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    r_sec += count;
    r_cmd++;
    return fseek(img, (long)lba * 512, SEEK_SET) || fread(buf, 512, count, img) != count ? -1 : 0;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    w_sec += count;
    w_cmd++;
    return fseek(img, (long)lba * 512, SEEK_SET) || fwrite(buf, 512, count, img) != count ? -1 : 0;
}

const char *sd_error(void) { return "image"; }

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s (%s)\n", __FILE__, __LINE__, #c, fat_error()); } } while (0)

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static uint32_t spc, nfats;
static uint32_t ticks, max_w, max_r, max_wc, max_rc, first_w;

static int tick(void)
{
    if (!ticks)
        first_w = w_sec;
    ticks++;
    if (w_sec > max_w) max_w = w_sec;
    if (r_sec > max_r) max_r = r_sec;
    if (w_cmd > max_wc) max_wc = w_cmd;
    if (r_cmd > max_rc) max_rc = r_cmd;
    w_sec = r_sec = w_cmd = r_cmd = 0;
    return 0;
}

static void begin(void)
{
    ticks = max_w = max_r = max_wc = max_rc = first_w = 0;
    w_sec = r_sec = w_cmd = r_cmd = 0;
}

/* the steps of one call; the last step is what follows the last tick */
static void end(const char *what, size_t len)
{
    tick();
    /* the data of a piece: a run of 32 KB (or one cluster if it is bigger); the FAT sectors of the
     * clusters of a piece of 512 (4 sectors and the joins, in every copy); FSInfo; the entry and the
     * release of an old chain of a FAT sector's worth (128 entries) in every copy */
    const uint32_t run = spc > 64 ? spc : 64;
    const uint32_t limit = run + 1 + nfats * (512 / 128 + 2) + 4;
    printf("queue %-24s %5zu KB: %3u steps, most written %3u sectors in %u commands, most read %3u in %u, "
           "before the first tick %u (limit %u)\n", what, len / 1024, (unsigned)ticks, (unsigned)max_w,
           (unsigned)max_wc, (unsigned)max_r, (unsigned)max_rc, (unsigned)first_w, (unsigned)limit);
    CHECK(max_w <= limit);
    CHECK(first_w <= limit);
    CHECK(ticks >= (len / (run * 512)));        /* about one step for each piece of data */
}

static uint8_t *pattern(size_t len, uint32_t seed)
{
    uint8_t *p = malloc(len ? len : 1);
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
    return p;
}

static int same(const char *path, const uint8_t *want, size_t len)
{
    fat_entry_t e;
    uint8_t *data;
    size_t n;
    if (fat_find(path, &e) || fat_load(&e, &data, &n))
        return 0;
    const int ok = n == len && memcmp(data, want, len) == 0;
    free(data);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 2 || !(img = fopen(argv[1], "r+b"))) {
        fprintf(stderr, "usage: test_fat_queue IMAGE\n");
        return 2;
    }
    uint8_t s[512];
    CHECK(sd_read(0, 1, s) == 0);
    const uint32_t part = s[0] == 0xEB || s[0] == 0xE9 ? 0 : le32(s + 446 + 8);
    CHECK(sd_read(part, 1, s) == 0);
    spc = s[13];
    nfats = s[16];
    CHECK(fat_mount() == 0);
    CHECK(fat_mkdirs("/bm/web") == 0);

    const size_t big = 2048 * 1024, small = 1024 * 1024, bigger = 4096 * 1024;
    uint8_t *a = pattern(bigger, 1), *b = pattern(bigger, 2);
    fat_write_tick = NULL;
    CHECK(fat_write_file("/bm/web", "BENCH1.BIN", a, big) == 0);          /* the sync write of the bench */

    fat_write_tick = tick;
    static const size_t sizes[] = { 1024, 32 * 1024, 1024 * 1024, 2048 * 1024, 4096 * 1024 };
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        begin();
        CHECK(fat_write_file("/bm/web", "Q.BIN", a, sizes[i]) == 0);
        end("new", sizes[i]);
        CHECK(same("/bm/web/q.bin", a, sizes[i]));
        CHECK(fat_delete("/bm/web/q.bin") == 0);
    }
    begin();
    CHECK(fat_write_file("/bm/web", "BENCH2.BIN", a, big) == 0);
    end("new, after a sync file", big);
    CHECK(same("/bm/web/bench2.bin", a, big));
    begin();
    CHECK(fat_replace("/bm/web/bench2.bin", b, big) == 0);
    end("replaced, same size", big);
    CHECK(same("/bm/web/bench2.bin", b, big));
    begin();
    CHECK(fat_replace("/bm/web/bench2.bin", a, small) == 0);
    end("replaced by a smaller", small);
    CHECK(same("/bm/web/bench2.bin", a, small));
    begin();
    CHECK(fat_replace("/bm/web/bench2.bin", b, bigger) == 0);
    end("replaced by a bigger", bigger);
    CHECK(same("/bm/web/bench2.bin", b, bigger));
    CHECK(same("/bm/web/bench1.bin", a, big));

    free(a);
    free(b);
    printf("fat queue: %s\n", fails ? "FAILED" : "all steps are short");
    return fails != 0;
}
