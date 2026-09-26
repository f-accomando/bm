/*
 * FAT16 / FAT32 reader with long file names, on top of sd_read().
 */
#include "fat.h"
#include "drivers/sd.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

static struct {
    int mounted, fat32;
    uint32_t part_lba;          /* volume start */
    uint32_t spc;               /* sectors per cluster */
    uint32_t fat_lba;           /* first FAT */
    uint32_t root_lba, root_sectors;    /* FAT16 root directory */
    uint32_t root_cluster;      /* FAT32 */
    uint32_t data_lba;          /* cluster 2 */
    uint32_t clusters;
    uint32_t total_sectors;
    char label[12];
} vol;

static const char *err = "not mounted";
static char desc[64];

static uint8_t sec_buf[512] __attribute__((aligned(4)));
static uint32_t sec_lba = ~0u;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* one-sector cache for FAT and directory reads */
static const uint8_t *sector(uint32_t lba)
{
    if (lba == sec_lba)
        return sec_buf;
    if (sd_read(lba, 1, sec_buf)) {
        sec_lba = ~0u;
        err = "SD read error";
        return NULL;
    }
    sec_lba = lba;
    return sec_buf;
}

static int is_fat_vbr(const uint8_t *s)
{
    return (s[0] == 0xEB || s[0] == 0xE9) && rd16(s + 11) == 512 && s[13] &&
           !(s[13] & (s[13] - 1)) && rd16(s + 14) && s[16];
}

int fat_mount(void)
{
    vol.mounted = 0;
    sec_lba = ~0u;
    const uint8_t *s = sector(0);
    if (!s)
        return -1;

    vol.part_lba = 0;
    if (!is_fat_vbr(s)) {
        if (s[510] != 0x55 || s[511] != 0xAA) {
            err = "no partition table";
            return -1;
        }
        for (int i = 0; i < 4; i++) {
            const uint8_t *pe = s + 446 + i * 16;
            uint8_t type = pe[4];
            if (type == 0x04 || type == 0x06 || type == 0x0E ||
                type == 0x0B || type == 0x0C) {
                vol.part_lba = rd32(pe + 8);
                break;
            }
        }
        if (!vol.part_lba) {
            err = "no FAT partition";
            return -1;
        }
        if (!(s = sector(vol.part_lba)))
            return -1;
        if (!is_fat_vbr(s)) {
            err = "partition is not FAT";
            return -1;
        }
    }

    vol.spc = s[13];
    uint32_t reserved = rd16(s + 14), nfats = s[16];
    uint32_t root_entries = rd16(s + 17);
    uint32_t total = rd16(s + 19) ? rd16(s + 19) : rd32(s + 32);
    uint32_t fatsz = rd16(s + 22) ? rd16(s + 22) : rd32(s + 36);

    vol.total_sectors = total;
    vol.fat_lba = vol.part_lba + reserved;
    vol.root_sectors = (root_entries * 32 + 511) / 512;
    vol.root_lba = vol.fat_lba + nfats * fatsz;
    vol.data_lba = vol.root_lba + vol.root_sectors;
    vol.clusters = (total - (vol.data_lba - vol.part_lba)) / vol.spc;
    if (vol.clusters < 4085) {
        err = "FAT12 not supported (format as FAT32)";
        return -1;
    }
    vol.fat32 = vol.clusters >= 65525;
    const uint8_t *label = s + (vol.fat32 ? 71 : 43);
    memcpy(vol.label, label, 11);
    vol.label[11] = 0;
    for (int i = 10; i >= 0 && vol.label[i] == ' '; i--)
        vol.label[i] = 0;
    vol.root_cluster = vol.fat32 ? rd32(s + 44) : 0;
    vol.mounted = 1;
    err = "ok";
    ksnprintf(desc, sizeof desc, "FAT%d, %lu MiB, label %s", vol.fat32 ? 32 : 16,
              total / 2048, vol.label[0] ? vol.label : "-");
    return 0;
}

const char *fat_error(void) { return err; }
const char *fat_describe(void) { return vol.mounted ? desc : err; }

static uint32_t next_cluster(uint32_t c)
{
    uint32_t off = vol.fat32 ? c * 4 : c * 2;
    const uint8_t *s = sector(vol.fat_lba + off / 512);
    if (!s)
        return 0;
    if (vol.fat32)
        return rd32(s + off % 512) & 0x0FFFFFFF;
    return rd16(s + off % 512);
}

static int is_eoc(uint32_t c)
{
    return c < 2 || (vol.fat32 ? c >= 0x0FFFFFF8 : c >= 0xFFF8);
}

static uint32_t cluster_lba(uint32_t c)
{
    return vol.data_lba + (c - 2) * vol.spc;
}

/* LBA of directory entry `index`, or 0 past the end */
static uint32_t dir_entry_lba(fat_dir_t *d, uint32_t index)
{
    uint32_t sec = index / 16;
    if (d->cluster == 0) {
        return sec < vol.root_sectors ? vol.root_lba + sec : 0;
    }
    uint32_t c = d->cluster;
    for (uint32_t n = sec / vol.spc; n; n--) {
        c = next_cluster(c);
        if (is_eoc(c))
            return 0;
    }
    return cluster_lba(c) + sec % vol.spc;
}

static void dir_open_cluster(fat_dir_t *d, uint32_t cluster)
{
    d->cluster = cluster;
    d->index = 0;
}

static int name_eq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
    }
    return b[n] == 0;
}

int fat_opendir(fat_dir_t *d, const char *path)
{
    if (!vol.mounted) {
        err = "not mounted";
        return -1;
    }
    dir_open_cluster(d, vol.root_cluster);
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        size_t n = 0;
        while (path[n] && path[n] != '/') n++;
        fat_entry_t e;
        int found = 0;
        while (fat_readdir(d, &e)) {
            if (e.is_dir && name_eq(path, e.name, n)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            err = "directory not found";
            return -1;
        }
        dir_open_cluster(d, e.cluster);
        path += n;
    }
    return 0;
}

static uint8_t lfn_checksum(const uint8_t *sfn)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + sfn[i]);
    return sum;
}

int fat_readdir(fat_dir_t *d, fat_entry_t *e)
{
    uint16_t lfn[260];
    int lfn_valid = 0, lfn_len = 0;
    uint8_t lfn_sum = 0;

    for (;;) {
        uint32_t lba = dir_entry_lba(d, d->index);
        if (!lba)
            return 0;
        const uint8_t *s = sector(lba);
        if (!s)
            return 0;
        const uint8_t *de = s + (d->index % 16) * 32;
        d->index++;

        if (de[0] == 0x00)
            return 0;
        if (de[0] == 0xE5) {
            lfn_valid = 0;
            continue;
        }
        if (de[11] == 0x0F) {                   /* long name piece */
            int seq = de[0] & 0x1F;
            if (de[0] & 0x40) {
                lfn_valid = 1;
                lfn_len = seq * 13;
                lfn_sum = de[13];
                memset(lfn, 0xFF, sizeof lfn);
            }
            if (!lfn_valid || seq < 1 || seq > 20 || de[13] != lfn_sum) {
                lfn_valid = 0;
                continue;
            }
            static const uint8_t offs[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
            for (int i = 0; i < 13; i++)
                lfn[(seq - 1) * 13 + i] = rd16(de + offs[i]);
            continue;
        }
        if (de[11] & 0x08) {                    /* volume label */
            lfn_valid = 0;
            continue;
        }
        if (de[0] == '.') {
            lfn_valid = 0;
            continue;
        }

        size_t n = 0;
        if (lfn_valid && lfn_checksum(de) == lfn_sum) {
            for (int i = 0; i < lfn_len && n + 1 < FAT_NAME_MAX; i++) {
                uint16_t ch = lfn[i];
                if (ch == 0 || ch == 0xFFFF) break;
                e->name[n++] = ch < 0x80 ? (char)ch : '?';
            }
        } else {
            for (int i = 0; i < 8 && de[i] != ' '; i++)
                e->name[n++] = (char)((de[12] & 0x08) && de[i] >= 'A' && de[i] <= 'Z' ? de[i] + 32 : de[i]);
            if (de[8] != ' ') {
                e->name[n++] = '.';
                for (int i = 8; i < 11 && de[i] != ' '; i++)
                    e->name[n++] = (char)((de[12] & 0x10) && de[i] >= 'A' && de[i] <= 'Z' ? de[i] + 32 : de[i]);
            }
        }
        e->name[n] = 0;
        e->is_dir = (de[11] & 0x10) != 0;
        e->size = rd32(de + 28);
        e->cluster = (vol.fat32 ? (uint32_t)rd16(de + 20) << 16 : 0) | rd16(de + 26);
        return 1;
    }
}

int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!vol.mounted || e->is_dir) {
        err = "not a file";
        return -1;
    }
    uint32_t csize = vol.spc * 512;
    uint8_t *buf = malloc(e->size ? e->size : 1);
    uint8_t *tmp = malloc(csize);
    if (!buf || !tmp) {
        free(buf);
        free(tmp);
        err = "out of memory";
        return -1;
    }
    uint32_t c = e->cluster, done = 0;
    while (done < e->size) {
        if (is_eoc(c) || c >= vol.clusters + 2) {
            err = "broken cluster chain";
            goto fail;
        }
        uint32_t n = e->size - done < csize ? e->size - done : csize;
        if (n == csize) {
            if (sd_read(cluster_lba(c), vol.spc, buf + done))
                goto io;
        } else {
            if (sd_read(cluster_lba(c), (n + 511) / 512, tmp))
                goto io;
            memcpy(buf + done, tmp, n);
        }
        done += n;
        if (done < e->size)
            c = next_cluster(c);
    }
    free(tmp);
    *data = buf;
    *len = e->size;
    return 0;
io:
    err = "SD read error";
fail:
    free(tmp);
    free(buf);
    return -1;
}
