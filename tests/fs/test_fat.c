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
#include "kernel/sdmap.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *img;
static uint32_t writes, runs;           /* sectors written; writes of more than one */
static uint32_t cmds;                   /* write commands (one per sd_write) */
static int corrupt;                     /* a card that gets a write of several sectors wrong */

/* the volume, read from the image (fat.c keeps it to itself): FAT32 */
static struct { uint32_t part, spc, fat, fatsz, nfats, fsinfo; } g;
static uint32_t fat_writes;             /* sectors written in the FATs */

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    return fseek(img, (long)lba * 512, SEEK_SET) || fread(buf, 512, count, img) != count ? -1 : 0;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    writes += count;
    cmds++;
    for (uint32_t i = 0; i < count; i++)
        fat_writes += g.fat && lba + i >= g.fat && lba + i < g.fat + g.nfats * g.fatsz;
    static uint8_t bad[128 * 512];
    if (count > 1) {
        runs++;
        if (corrupt && count <= 128) {
            memcpy(bad, buf, count * 512);
            bad[count * 512 - 1] ^= 0x5a;
            buf = bad;
        }
    }
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

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static void geometry(void)
{
    uint8_t s[512];
    if (sd_read(0, 1, s))
        return;
    g.part = s[0] == 0xEB || s[0] == 0xE9 ? 0 : le32(s + 446 + 8);
    if (sd_read(g.part, 1, s))
        return;
    g.spc = s[13];
    g.nfats = s[16];
    g.fatsz = le32(s + 36);
    g.fsinfo = g.part + le16(s + 48);
    g.fat = g.part + le16(s + 14);
}

/* FAT entry c of copy `copy`, from the image */
static uint32_t entry(uint32_t copy, uint32_t c)
{
    uint8_t s[512];
    if (sd_read(g.fat + copy * g.fatsz + c * 4 / 512, 1, s))
        return 0;
    return le32(s + c * 4 % 512) & 0x0FFFFFFF;
}

/* every copy of the FAT the same as the first */
static int fats_equal(void)
{
    uint8_t a[512], b[512];
    for (uint32_t i = 0; i < g.fatsz; i++) {
        if (sd_read(g.fat + i, 1, a))
            return 0;
        for (uint32_t f = 1; f < g.nfats; f++)
            if (sd_read(g.fat + f * g.fatsz + i, 1, b) || memcmp(a, b, 512))
                return 0;
    }
    return 1;
}

/* the clusters of a file's chain, followed in the first FAT (0: broken) */
static uint32_t chain_len(const char *path)
{
    fat_entry_t e;
    if (fat_find(path, &e))
        return 0;
    uint32_t n = 0;
    for (uint32_t c = e.cluster; c >= 2 && c < 0x0FFFFFF8; c = entry(0, c))
        if (++n > (1u << 24))
            return 0;
    return n;
}

/* another writer while a write gives the CPU back (the menu saving a
 * setting while a fiber's write waits for its next slice): at the ticks
 * the case asks for, it writes, replaces and deletes files */
static int nest_case, nest_ticks, nesting;
static uint8_t *other;
static int nest_tick(void)
{
    if (nesting)
        return 0;
    nesting = 1;
    nest_ticks++;
    switch (nest_ticks < 100 ? nest_case * 100 + nest_ticks : 0) {
    case 101:                           /* a file of its own, then one deleted */
        CHECK(fat_write_file("/mix", "OTHER.TXT", other, 3000) == 0);
        break;
    case 103:
        CHECK(fat_delete("/mix/OLD1.BIN") == 0);
        break;
    case 104:                           /* the same file, written by the other first */
        CHECK(fat_write_file("/mix", "SAME.BIN", other, 2000) == 0);
        break;
    case 202:                           /* the file being replaced is deleted meanwhile */
        CHECK(fat_delete("/mix/GONE.BIN") == 0);
        break;
    case 302:                           /* a new file made meanwhile with the same name */
        CHECK(fat_write_file("/mix", "TWICE.BIN", other, 5000) == 0);
        break;
    }
    nesting = 0;
    return 0;
}

static uint32_t count_free(void)
{
    fat_free_t st = { 0, 0 };
    int r;
    while ((r = fat_free_step(&st, 3)) == 0)
        ;
    return r == 1 ? st.free : ~0u;
}

static void test_free_count(void)
{
    const uint32_t cb = fat_cluster_bytes();
    uint32_t f0 = count_free();
    uint8_t *p = pattern(9000, 77);
    CHECK(fat_write_file("/", "FREE9K.BIN", p, 9000) == 0);
    free(p);
    uint32_t f1 = count_free();
    CHECK(f0 - f1 == (9000 + cb - 1) / cb);
    CHECK(f0 < fat_total_clusters());
    CHECK(fat_delete("/FREE9K.BIN") == 0);
    CHECK(count_free() == f0);
}

static int has_file(const sdmap_t *r, const char *path, uint32_t bytes)
{
    for (int i = 0; i < r->nfiles; i++)
        if (!strcasecmp(r->files[i].path, path) && r->files[i].bytes == bytes)
            return 1;
    return 0;
}

static void test_sdmap(void)
{
    uint8_t *buf = pattern(5000, 5);
    CHECK(fat_mkdirs("/bm") == 0);
    CHECK(fat_mkdirs("/bme") == 0);
    CHECK(fat_write_file("/bm", "G.BM", buf, 3000) == 0);
    CHECK(fat_write_file("/bm", "LOG.TXT", buf, 2000) == 0);
    CHECK(fat_write_file("/bme", "P.BME", buf, 5000) == 0);
    CHECK(fat_write_file("/", "kernel.img", buf, 4000) == 0);
    CHECK(fat_write_file("/", "X.DAT", buf, 1000) == 0);
    free(buf);
    sdmap_core_reset();
    int r;
    while ((r = sdmap_core_step(2)) == 1)
        ;
    CHECK(r == 0);
    const sdmap_t *m = sdmap_core_result();
    CHECK(m->bm >= 5000 && m->bme == 5000 && m->kernel == 4000);
    CHECK(m->free > 0 && m->free + m->kernel + m->bm + m->bme + m->other == m->total);
    int found = 0;
    for (int i = 0; i < m->ndirs; i++)
        if (!strcasecmp(m->dirs[i].path, "/bme")) {
            found = 1;
            CHECK(m->dirs[i].files == 1 && m->dirs[i].kb == 4);
        }
    CHECK(found);
    CHECK(has_file(m, "/bm/LOG.TXT", 2000) && has_file(m, "/X.DAT", 1000));
    CHECK(!has_file(m, "/bm/G.BM", 3000) && !has_file(m, "/bme/P.BME", 5000) && !has_file(m, "/kernel.img", 4000));
    for (int i = 1; i < m->nfiles; i++)
        CHECK(m->files[i - 1].bytes >= m->files[i].bytes);
    CHECK(sdmap_core_permille() == 1000);
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
    test_free_count();
    test_sdmap();

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

    /* whole clusters in one write (the RGB30's kernel took more than a
     * minute a sector at a time), each read back; fat_write_tick after
     * every cluster, a nonzero one stops the write with the old file kept;
     * a card that gets a run wrong is written a sector at a time from then
     * on, and the file is still right */
    CHECK(fat_write_runs(1) == 1);              /* on by default (the Pi too since 2026-10-11) */
    uint8_t *k1 = pattern(200000, 4242), *k2 = pattern(200000, 99), *k3 = pattern(150001, 7);
    fat_write_tick = tick;
    ticks = stop_at = 0;
    runs = 0;
    CHECK(fat_write_file("/", "kernel8.img", k1, 200000) == 0);
    CHECK(read_back("/kernel8.img", k1, 200000));
    CHECK(runs > 0 && ticks > 1);
    printf("fat: a 200000-byte file in %d pieces, %u of several sectors\n", ticks, (unsigned)runs);
    ticks = 0;
    stop_at = 3;
    CHECK(fat_write_file("/", "kernel8.img", k2, 200000) != 0);
    CHECK(strcmp(fat_error(), "stopped") == 0);
    CHECK(read_back("/kernel8.img", k1, 200000));
    ticks = stop_at = 0;
    corrupt = 1;
    CHECK(fat_write_file("/", "kernel8.img", k3, 150001) == 0);
    CHECK(read_back("/kernel8.img", k3, 150001));
    CHECK(fat_write_runs(0) == 0);              /* the wrong run turned them off */
    corrupt = 0;
    runs = 0;
    CHECK(fat_write_file("/", "kernel8.img", k2, 200000) == 0);
    CHECK(read_back("/kernel8.img", k2, 200000) && runs == 0);
    fat_write_tick = NULL;
    free(k1);
    free(k2);
    free(k3);

    /* the FAT of a big file (2026-10-11): the entries that share a FAT
     * sector change together, each copy's sector read and written once
     * (it was once per cluster and per copy: the old way writes 2 x 2000
     * sectors here); the copies stay the same; the chain of a replaced
     * file released the same way. In both ways of writing the data. */
    geometry();
    CHECK(g.spc >= 1 && g.nfats >= 1 && g.fatsz > 0);
    CHECK(fat_mkdirs("/mix") == 0);
    for (int way = 0; way < 2; way++) {
        fat_write_runs(way);
        const size_t big_len = (size_t)g.spc * 512 * 2000 + 100;      /* 2001 clusters */
        const uint32_t big_n = 2001, small_n = 300;
        uint8_t *b1 = pattern(big_len, 31337u + (uint32_t)way), *b2 = pattern((size_t)g.spc * 512 * small_n, 7u);
        fat_writes = 0;
        CHECK(fat_write_file("/mix", "BIG.BIN", b1, big_len) == 0);
        /* the sectors of 2001 entries (128 a sector) and a few more: each
         * piece of 512 marked joins the one before (its first sector again,
         * its last one shared), the free space may wrap around */
        printf("fat: %u clusters written, %u FAT sectors written (%s)\n", (unsigned)big_n,
               (unsigned)fat_writes, way ? "runs" : "sectors");
        CHECK(fat_writes <= g.nfats * (big_n / 128 + 2 * (big_n / 512 + 1) + 4));
        CHECK(read_back("/mix/big.bin", b1, big_len) && chain_len("/mix/big.bin") == big_n);
        CHECK(fats_equal());
        fat_writes = 0;
        CHECK(fat_replace("/mix/big.bin", b2, (size_t)g.spc * 512 * small_n) == 0);
        CHECK(fat_writes <= g.nfats * (big_n / 128 + small_n / 128 + 12));
        CHECK(read_back("/mix/big.bin", b2, (size_t)g.spc * 512 * small_n) && chain_len("/mix/big.bin") == small_n);
        CHECK(fats_equal());
        CHECK(fat_delete("/mix/big.bin") == 0);
        CHECK(fats_equal());
        free(b1);
        free(b2);
    }
    fat_write_runs(1);

    /* a small file: only the sectors with data of its last cluster (it was
     * the whole cluster, zeros past the end), its FAT entry, the entry */
    {
        uint32_t w0 = writes;
        CHECK(fat_write_file("/mix", "ONE.BIN", "x", 1) == 0);
        CHECK(writes - w0 <= 1 + g.nfats + 1);
        CHECK(read_back("/mix/one.bin", (const uint8_t *)"x", 1));
        uint8_t *t = pattern((size_t)g.spc * 512 + 700, 5);     /* a cluster, then 700 bytes */
        w0 = writes;
        CHECK(fat_write_file("/mix", "TAIL.BIN", t, (size_t)g.spc * 512 + 700) == 0);
        CHECK(writes - w0 <= g.spc + 2 + 2 * g.nfats + 1);
        CHECK(read_back("/mix/tail.bin", t, (size_t)g.spc * 512 + 700));
        free(t);
    }

    /* the free space hint: kept in FSInfo for the next mount (the search
     * started at cluster 2 after every boot); the free count "unknown" */
    {
        uint8_t fi[512];
        CHECK(sd_read(g.fsinfo, 1, fi) == 0 && le32(fi) == 0x41615252);
        const uint32_t hint = le32(fi + 492);
        CHECK(le32(fi + 488) == 0xFFFFFFFF && hint >= 2 && hint != 0xFFFFFFFF);
    }

    /* another writer at the ticks of a write that gives the CPU back: no
     * cluster shared (fsck.vfat in run.py), the directory looked up at the
     * end: the write that ends last wins, a file deleted meanwhile cannot
     * be replaced (its new chain released) */
    {
        other = pattern(5000, 404);
        uint8_t *mine = pattern((size_t)g.spc * 512 * 600 + 77, 505);
        const size_t mlen = (size_t)g.spc * 512 * 600 + 77;
        CHECK(fat_write_file("/mix", "OLD1.BIN", other, 4000) == 0);
        fat_write_tick = nest_tick;
        for (int way = 0; way < 2; way++) {
            fat_write_runs(way);
            nest_case = 1;
            nest_ticks = 0;
            CHECK(fat_write_file("/mix", "SAME.BIN", mine, mlen) == 0 && nest_ticks >= 4);
            CHECK(read_back("/mix/same.bin", mine, mlen) && chain_len("/mix/same.bin") == 601);
            CHECK(read_back("/mix/other.txt", other, 3000));
            CHECK(fat_find("/mix/old1.bin", &fe) != 0);
            CHECK(fat_write_file("/mix", "OLD1.BIN", other, 4000) == 0);   /* for the next way */
            CHECK(fat_write_file("/mix", "GONE.BIN", other, 100) == 0);
            nest_case = 2;
            nest_ticks = 0;
            CHECK(fat_replace("/mix/gone.bin", mine, mlen) != 0 && strcmp(fat_error(), "file not found") == 0);
            CHECK(fat_find("/mix/gone.bin", &fe) != 0);
            nest_case = 3;
            nest_ticks = 0;
            CHECK(fat_write_file("/mix", "TWICE.BIN", mine, mlen) == 0);
            CHECK(read_back("/mix/twice.bin", mine, mlen));
            fat_dir_t md;
            fat_entry_t me;
            int twice = 0;
            CHECK(fat_opendir(&md, "/mix") == 0);
            while (fat_readdir(&md, &me))
                twice += strcmp(me.name, "TWICE.BIN") == 0;
            CHECK(twice == 1);                  /* one entry, not two */
            CHECK(fat_delete("/mix/twice.bin") == 0);
            CHECK(fats_equal());
        }
        CHECK(fat_writing() == 0);
        fat_write_tick = NULL;
        fat_write_runs(1);
        free(mine);
        free(other);
    }

    /* for run.py: the last save file and the config, to compare with mtools */
    FILE *f = fopen("snake.expected", "wb");
    fwrite(last, 1, last_len, f);
    fclose(f);

    printf("fat: %d/%d checks passed, %u sectors written\n", checks - fails, checks, writes);
    free(last);
    return fails != 0;
}
