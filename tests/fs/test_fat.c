/*
 * Host test for src/fs/fat.c writing: the SD card is an image file.
 *
 *   test_fat IMAGE ORIGINAL_FILE /PATH/ON/IMAGE
 *
 * The image must already contain ORIGINAL_FILE at that path (to check that
 * writing never damages existing files). Afterwards tests/fs/run.py runs
 * fsck.vfat and reads the files back with mtools.
 */
#define _DEFAULT_SOURCE
#include "fs/fat.h"
#include "drivers/sd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *img;
static uint32_t writes;

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    return fseek(img, (long)lba * 512, SEEK_SET) || fread(buf, 512, count, img) != count ? -1 : 0;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    writes += count;
    return fseek(img, (long)lba * 512, SEEK_SET) || fwrite(buf, 512, count, img) != count ? -1 : 0;
}

const char *sd_error(void) { return "image"; }

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s (%s)\n", __FILE__, __LINE__, #c, fat_error()); } } while (0)

static uint8_t *pattern(size_t len, uint32_t seed)
{
    uint8_t *p = malloc(len ? len : 1);
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
    return p;
}

static int read_back(const char *path, const uint8_t *want, size_t len)
{
    fat_entry_t e;
    uint8_t *data;
    size_t n;
    if (fat_find(path, &e) || fat_load(&e, &data, &n))
        return 0;
    int ok = n == len && memcmp(data, want, len) == 0;
    free(data);
    return ok;
}

/* fat_load_tick: counts the clusters, stops the read at the one asked */
static int ticks, stop_at;
static int tick(void)
{
    return ++ticks == stop_at;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    rewind(f);
    uint8_t *d = malloc(*len ? *len : 1);
    if (fread(d, 1, *len, f) != *len) { free(d); d = NULL; }
    fclose(f);
    return d;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: test_fat IMAGE ORIGINAL /PATH\n");
        return 2;
    }
    img = fopen(argv[1], "r+b");
    size_t orig_len;
    uint8_t *orig = slurp(argv[2], &orig_len);
    if (!img || !orig) {
        perror("open");
        return 2;
    }
    CHECK(fat_mount() == 0);
    CHECK(read_back(argv[3], orig, orig_len));

    /* directories */
    CHECK(fat_mkdirs("/bm/save") == 0);
    CHECK(fat_mkdirs("/bm/save") == 0);         /* already there: fine */
    fat_dir_t d;
    CHECK(fat_opendir(&d, "/BM/SAVE") == 0);
    CHECK(fat_mkdirs("/not an 8.3 name") != 0);

    /* a file, then replaced with every interesting size */
    static const char cfg[] = "layout=it\ndraw=direct\n";
    CHECK(fat_write_file("/bm", "config.txt", cfg, sizeof cfg - 1) == 0);
    CHECK(read_back("/bm/config.txt", (const uint8_t *)cfg, sizeof cfg - 1));
    static const size_t sizes[] = { 0, 1, 511, 512, 513, 5000, 70000, 3, 0, 1024 };
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        uint8_t *p = pattern(sizes[i], (uint32_t)i);
        CHECK(fat_write_file("/bm/save", "SIZES.SAV", p, sizes[i]) == 0);
        CHECK(read_back("/bm/save/sizes.sav", p, sizes[i]));
        free(p);
    }

    /* 200 files: the directory grows over several clusters */
    for (int i = 0; i < 200; i++) {
        char name[16];
        snprintf(name, sizeof name, "F%07d.SAV", i);
        uint8_t *p = pattern((size_t)(i * 37 % 900), (uint32_t)i + 100);
        CHECK(fat_write_file("/bm/save", name, p, (size_t)(i * 37 % 900)) == 0);
        free(p);
    }
    for (int i = 0; i < 200; i += 7) {
        char path[40];
        snprintf(path, sizeof path, "/bm/save/f%07d.sav", i);
        uint8_t *p = pattern((size_t)(i * 37 % 900), (uint32_t)i + 100);
        CHECK(read_back(path, p, (size_t)(i * 37 % 900)));
        free(p);
    }

    /* 1000 rewrites of a save file */
    uint8_t *last = NULL;
    size_t last_len = 0;
    for (int i = 0; i < 1000; i++) {
        free(last);
        last_len = (size_t)((i * 7919) % 3000);
        last = pattern(last_len, (uint32_t)i + 5000);
        if (fat_write_file("/bm/save", "SNAKE.SAV", last, last_len) != 0) {
            CHECK(!"rewrite");
            break;
        }
    }
    CHECK(read_back("/bm/save/snake.sav", last, last_len));

    /* the start of a file (a cartridge's cover), and a read stopped half way
     * (a fiber of the menu that has to end) */
    {
        fat_entry_t pe;
        uint8_t *data;
        size_t n;
        CHECK(fat_find(argv[3], &pe) == 0);
        size_t want = orig_len < 1000 ? orig_len : 1000;
        CHECK(fat_load_part(&pe, 1000, &data, &n) == 0 && n == want && memcmp(data, orig, want) == 0);
        free(data);
        CHECK(fat_load_part(&pe, orig_len + 5000, &data, &n) == 0 && n == orig_len && memcmp(data, orig, n) == 0);
        free(data);
        uint8_t *big = pattern(70000, 4242);
        CHECK(fat_write_file("/bm/save", "BIG.SAV", big, 70000) == 0);
        CHECK(fat_find("/bm/save/big.sav", &pe) == 0);
        fat_load_tick = tick;
        ticks = 0;
        stop_at = 0;
        CHECK(fat_load(&pe, &data, &n) == 0 && n == 70000 && memcmp(data, big, n) == 0 && ticks > 1);
        free(data);
        const int clusters = ticks;
        ticks = 0;
        stop_at = 2;
        CHECK(fat_load(&pe, &data, &n) != 0 && data == NULL && strcmp(fat_error(), "stopped") == 0);
        CHECK(ticks == 2 && clusters > 2);
        ticks = 0;
        stop_at = 0;
        CHECK(fat_load_part(&pe, 3000, &data, &n) == 0 && n == 3000 && memcmp(data, big, n) == 0 && ticks < clusters);
        free(data);
        fat_load_tick = NULL;
        free(big);
        CHECK(fat_delete("/bm/save/BIG.SAV") == 0);     /* run.py counts the files */
    }

    /* the rest of the card is untouched, and it reads the same after a remount */
    CHECK(read_back(argv[3], orig, orig_len));
    CHECK(fat_mount() == 0);
    CHECK(read_back("/bm/save/snake.sav", last, last_len));
    CHECK(read_back("/bm/config.txt", (const uint8_t *)cfg, sizeof cfg - 1));
    CHECK(fat_write_file("/", "bad.name.txt", "x", 1) != 0);
    CHECK(fat_write_file("/nodir", "A.TXT", "x", 1) != 0);

    /* deleting: every fourth save file, a long name, then files reuse the space */
    fat_entry_t fe;
    for (int i = 0; i < 200; i += 4) {
        char path[40];
        snprintf(path, sizeof path, "/bm/save/F%07d.SAV", i);
        CHECK(fat_delete(path) == 0);
        CHECK(fat_find(path, &fe) != 0);
    }
    uint8_t *p1 = pattern(37, 101);
    CHECK(read_back("/bm/save/f0000001.sav", p1, 37));
    free(p1);
    CHECK(fat_find("/carts/Un gioco da cancellare.bm", &fe) == 0);
    /* a long name keeps its name when its content is replaced */
    uint8_t *p2 = pattern(9000, 77);
    CHECK(fat_replace("/carts/un gioco da cancellare.bm", p2, 9000) == 0);
    CHECK(read_back("/carts/Un gioco da cancellare.bm", p2, 9000));
    free(p2);
    CHECK(fat_replace("/carts/nessuno.bm", "x", 1) != 0);
    CHECK(fat_replace("/bm/save", "x", 1) != 0);
    CHECK(fat_delete("/carts/un gioco da CANCELLARE.bm") == 0);
    CHECK(fat_find("/carts/Un gioco da cancellare.bm", &fe) != 0);
    CHECK(read_back(argv[3], orig, orig_len));
    CHECK(fat_delete("/carts/Un gioco da cancellare.bm") != 0);    /* gone already */
    CHECK(fat_delete("/bm/save") != 0);                           /* a directory */
    for (int i = 0; i < 10; i++) {
        char name[16];
        snprintf(name, sizeof name, "N%07d.SAV", i);
        uint8_t *p = pattern(5000, (uint32_t)i + 9000);
        CHECK(fat_write_file("/bm/save", name, p, 5000) == 0);
        free(p);
    }

    /* for run.py: the last save file and the config, to compare with mtools */
    FILE *f = fopen("snake.expected", "wb");
    fwrite(last, 1, last_len, f);
    fclose(f);

    printf("fat: %d/%d checks passed, %u sectors written\n", checks - fails, checks, writes);
    free(last);
    return fails != 0;
}
