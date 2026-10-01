/*
 * FAT16 / FAT32 on top of sd_read() / sd_write(): reading with long file
 * names; writing whole files and directories with 8.3 names.
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
    uint32_t nfats, fatsz;      /* number and size (sectors) of the FATs */
    uint32_t fsinfo_lba;        /* FAT32 FSInfo sector, 0 if none */
    uint32_t next_free;         /* allocation hint */
    int fsinfo_reset;           /* free count set to "unknown" this mount */
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
    vol.nfats = nfats;
    vol.fatsz = fatsz;
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
    vol.fsinfo_lba = vol.fat32 && rd16(s + 48) ? vol.part_lba + rd16(s + 48) : 0;
    vol.next_free = 2;
    vol.fsinfo_reset = 0;
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
        e->dir_lba = lba;
        e->dir_off = (uint16_t)(de - s);
        e->is_dir = (de[11] & 0x10) != 0;
        e->size = rd32(de + 28);
        e->cluster = (vol.fat32 ? (uint32_t)rd16(de + 20) << 16 : 0) | rd16(de + 26);
        return 1;
    }
}

int fat_read_head(const fat_entry_t *e, uint8_t buf[512])
{
    memset(buf, 0, 512);
    if (!vol.mounted || e->is_dir || !e->size)
        return -1;
    if (e->cluster < 2 || e->cluster >= vol.clusters + 2) {
        err = "broken cluster chain";
        return -1;
    }
    if (sd_read(cluster_lba(e->cluster), 1, buf)) {
        err = "SD read error";
        return -1;
    }
    if (e->size < 512)
        memset(buf + e->size, 0, 512 - e->size);
    return 0;
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

/* ---------------------------------------------------------------- lookup */

int fat_find(const char *path, fat_entry_t *e)
{
    char dir[FAT_NAME_MAX];
    const char *slash = strrchr(path, '/');
    size_t dn = slash ? (size_t)(slash - path) : 0;
    if (dn >= sizeof dir) {
        err = "path too long";
        return -1;
    }
    memcpy(dir, path, dn);
    dir[dn] = 0;
    const char *name = slash ? slash + 1 : path;
    fat_dir_t d;
    if (fat_opendir(&d, dir) != 0)
        return -1;
    while (fat_readdir(&d, e))
        if (name_eq(name, e->name, strlen(name)))
            return 0;
    err = "file not found";
    return -1;
}

/* ---------------------------------------------------------------- writing */

static uint8_t wbuf[512] __attribute__((aligned(4)));

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

static int write_sector(uint32_t lba, const uint8_t *buf)
{
    if (sd_write(lba, 1, buf)) {
        err = "SD write error";
        sec_lba = ~0u;
        return -1;
    }
    if (lba == sec_lba)
        memcpy(sec_buf, buf, 512);
    return 0;
}

/* Read-modify-write of `len` bytes at byte `off` of sector `lba`. */
static int patch_sector(uint32_t lba, uint32_t off, const void *data, uint32_t len)
{
    const uint8_t *s = sector(lba);
    if (!s)
        return -1;
    memcpy(wbuf, s, 512);
    memcpy(wbuf + off, data, len);
    return write_sector(lba, wbuf);
}

/* The FSInfo free count and hint become "unknown" before the first change:
 * a PC recomputes them, and they are never wrong. */
static int fsinfo_unknown(void)
{
    if (vol.fsinfo_reset || !vol.fsinfo_lba)
        return 0;
    const uint8_t *s = sector(vol.fsinfo_lba);
    if (!s)
        return -1;
    if (rd32(s) == 0x41615252 && rd32(s + 484) == 0x61417272) {
        uint8_t v[8];
        wr32(v, 0xFFFFFFFF);
        wr32(v + 4, 0xFFFFFFFF);
        if (patch_sector(vol.fsinfo_lba, 488, v, 8))
            return -1;
    }
    vol.fsinfo_reset = 1;
    return 0;
}

static uint32_t eoc(void) { return vol.fat32 ? 0x0FFFFFFF : 0xFFFF; }

/* Sets FAT entry c in every copy of the FAT. */
static int set_fat(uint32_t c, uint32_t v)
{
    uint32_t off = vol.fat32 ? c * 4 : c * 2;
    for (uint32_t i = 0; i < vol.nfats; i++) {
        uint32_t lba = vol.fat_lba + i * vol.fatsz + off / 512;
        const uint8_t *s = sector(lba);
        if (!s)
            return -1;
        memcpy(wbuf, s, 512);
        if (vol.fat32)
            wr32(wbuf + off % 512, (rd32(wbuf + off % 512) & 0xF0000000u) | (v & 0x0FFFFFFF));
        else
            wr16(wbuf + off % 512, v);
        if (write_sector(lba, wbuf))
            return -1;
    }
    return 0;
}

/* Next free cluster after `after` (wrapping), 0 if the volume is full or
 * the FAT cannot be read (a read error must never look like "free"). */
static uint32_t find_free(uint32_t after)
{
    uint32_t c = after;
    for (uint32_t i = 0; i < vol.clusters; i++) {
        if (++c >= vol.clusters + 2)
            c = 2;
        uint32_t off = vol.fat32 ? c * 4 : c * 2;
        const uint8_t *s = sector(vol.fat_lba + off / 512);
        if (!s)
            return 0;
        uint32_t v = vol.fat32 ? rd32(s + off % 512) & 0x0FFFFFFF : rd16(s + off % 512);
        if (v == 0)
            return c;
    }
    err = "SD card full";
    return 0;
}

/* Collects `n` free clusters (not yet marked). */
static int pick_free(uint32_t *list, uint32_t n)
{
    uint32_t c = vol.next_free > 2 ? vol.next_free - 1 : vol.clusters + 1;
    for (uint32_t i = 0; i < n; i++) {
        c = find_free(c);
        if (!c)
            return -1;
        list[i] = c;
    }
    vol.next_free = c + 1;
    return 0;
}

/* Links list[0..n-1] into a chain ending with EOC. */
static int link_chain(const uint32_t *list, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;)
        if (set_fat(list[i], i + 1 < n ? list[i + 1] : eoc()))
            return -1;
    return 0;
}

static int free_chain(uint32_t c)
{
    while (c >= 2 && c < vol.clusters + 2) {
        uint32_t next = next_cluster(c);
        if (set_fat(c, 0))
            return -1;
        if (is_eoc(next))
            break;
        c = next;
    }
    return 0;
}

static int zero_cluster(uint32_t c)
{
    memset(wbuf, 0, 512);
    for (uint32_t i = 0; i < vol.spc; i++)
        if (write_sector(cluster_lba(c) + i, wbuf))
            return -1;
    return 0;
}

/* "config.txt" -> "CONFIG  TXT"; -1 if not a valid 8.3 name */
static int to_83(const char *name, uint8_t out[11])
{
    memset(out, ' ', 11);
    int i = 0, n = 0;
    for (; name[i] && name[i] != '.'; i++, n++) {
        if (n >= 8) return -1;
        out[n] = (uint8_t)name[i];
    }
    if (n == 0) return -1;
    if (name[i] == '.') {
        i++;
        for (n = 0; name[i]; i++, n++) {
            if (n >= 3 || name[i] == '.') return -1;
            out[8 + n] = (uint8_t)name[i];
        }
    }
    for (int k = 0; k < 11; k++) {
        uint8_t ch = out[k];
        if (ch >= 'a' && ch <= 'z') out[k] = (uint8_t)(ch - 32);
        else if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                   ch == '-' || ch == ' ' || ch == '~'))
            return -1;
    }
    return 0;
}

#define FAT_DATE ((2026 - 1980) << 9 | 1 << 5 | 1)   /* no clock: 2026-01-01 */

static void make_entry(uint8_t de[32], const uint8_t name[11], uint8_t attr,
                       uint32_t cluster, uint32_t size)
{
    memset(de, 0, 32);
    memcpy(de, name, 11);
    de[11] = attr;
    wr16(de + 16, FAT_DATE);                    /* created */
    wr16(de + 18, FAT_DATE);                    /* accessed */
    wr16(de + 20, vol.fat32 ? cluster >> 16 : 0);
    wr16(de + 24, FAT_DATE);                    /* written */
    wr16(de + 26, cluster);
    wr32(de + 28, size);
}

/* Directory cluster of `dir` (0 = FAT16 root). */
static int dir_cluster(const char *dir, uint32_t *cluster)
{
    fat_dir_t d;
    if (fat_opendir(&d, dir))
        return -1;
    *cluster = d.cluster;
    return 0;
}

/* Puts a 32-byte entry in the first free slot of the directory, growing
 * it by one cluster if needed. */
static int add_entry(uint32_t dcluster, const uint8_t de[32])
{
    fat_dir_t d = { dcluster, 0 };
    for (;;) {
        uint32_t lba = dir_entry_lba(&d, d.index);
        if (!lba)
            break;
        const uint8_t *s = sector(lba);
        if (!s)
            return -1;
        uint32_t off = (d.index % 16) * 32;
        if (s[off] == 0x00 || s[off] == 0xE5)
            return patch_sector(lba, off, de, 32);
        d.index++;
    }
    if (dcluster == 0) {
        err = "root directory full";
        return -1;
    }
    /* last cluster of the directory, then a new zeroed one after it */
    uint32_t last = dcluster, c;
    while (!is_eoc(c = next_cluster(last)))
        last = c;
    uint32_t nc;
    if (pick_free(&nc, 1) || zero_cluster(nc) || set_fat(nc, eoc()) || set_fat(last, nc))
        return -1;
    return patch_sector(cluster_lba(nc), 0, de, 32);
}

int fat_mkdirs(const char *path)
{
    if (!vol.mounted) {
        err = "not mounted";
        return -1;
    }
    char sofar[FAT_NAME_MAX] = "";
    size_t len = 0;
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        size_t n = 0;
        while (path[n] && path[n] != '/') n++;
        if (len + 1 + n >= sizeof sofar) {
            err = "path too long";
            return -1;
        }
        char parent[FAT_NAME_MAX];
        memcpy(parent, sofar, len + 1);
        sofar[len++] = '/';
        memcpy(sofar + len, path, n);
        len += n;
        sofar[len] = 0;
        path += n;

        fat_dir_t d;
        if (fat_opendir(&d, sofar) == 0)
            continue;                           /* already there */
        uint8_t name[11];
        char comp[16];
        if (n >= sizeof comp) {
            err = "not an 8.3 name";
            return -1;
        }
        memcpy(comp, sofar + len - n, n);
        comp[n] = 0;
        uint32_t pc, nc;
        if (to_83(comp, name)) {
            err = "not an 8.3 name";
            return -1;
        }
        if (fsinfo_unknown() || dir_cluster(parent, &pc) || pick_free(&nc, 1) || zero_cluster(nc))
            return -1;
        uint8_t dot[64];
        static const uint8_t n_dot[11] = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
        static const uint8_t n_dotdot[11] = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
        make_entry(dot, n_dot, 0x10, nc, 0);
        make_entry(dot + 32, n_dotdot, 0x10, pc == vol.root_cluster ? 0 : pc, 0);
        uint8_t de[32];
        make_entry(de, name, 0x10, nc, 0);
        if (patch_sector(cluster_lba(nc), 0, dot, 64) || set_fat(nc, eoc()) || add_entry(pc, de))
            return -1;
    }
    return 0;
}

/* The data into free clusters, then the directory entry: the existing
 * `old` (its clusters released last), or a new entry `n83` in `dc`. */
static int store(const fat_entry_t *old, uint32_t dc, const uint8_t n83[11], const void *data, size_t len)
{
    uint32_t csize = vol.spc * 512;
    uint32_t n = (uint32_t)((len + csize - 1) / csize);
    uint32_t *list = n ? malloc(n * sizeof *list) : NULL;
    if (n && !list) {
        err = "out of memory";
        return -1;
    }
    int rc = -1;
    if (fsinfo_unknown() || (n && pick_free(list, n)))
        goto out;

    /* 1. the data, into free clusters */
    const uint8_t *p = data;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t chunk = len - (size_t)i * csize < csize ? (uint32_t)(len - (size_t)i * csize) : csize;
        for (uint32_t s = 0; s < vol.spc; s++) {
            uint32_t o = s * 512;
            memset(wbuf, 0, 512);
            if (o < chunk)
                memcpy(wbuf, p + (size_t)i * csize + o, chunk - o < 512 ? chunk - o : 512);
            if (write_sector(cluster_lba(list[i]) + s, wbuf))
                goto out;
        }
    }
    /* 2. the chain */
    if (n && link_chain(list, n))
        goto out;
    /* 3. the directory entry points at it */
    uint32_t first = n ? list[0] : 0;
    if (old) {
        uint8_t fields[4];
        wr16(fields, vol.fat32 ? first >> 16 : 0);
        if (patch_sector(old->dir_lba, old->dir_off + 20, fields, 2))
            goto out;
        wr16(fields, FAT_DATE);
        wr16(fields + 2, first);
        uint8_t tail[10];
        wr16(tail, 0);                          /* write time */
        memcpy(tail + 2, fields, 4);            /* write date, cluster low */
        wr32(tail + 6, (uint32_t)len);
        if (patch_sector(old->dir_lba, old->dir_off + 22, tail, 10))
            goto out;
        /* 4. the old data is released last */
        if (old->cluster && free_chain(old->cluster))
            goto out;
    } else {
        uint8_t de[32];
        make_entry(de, n83, 0x20, first, (uint32_t)len);
        if (add_entry(dc, de))
            goto out;
    }
    rc = 0;
out:
    free(list);
    return rc;
}

int fat_write_file(const char *dir, const char *name, const void *data, size_t len)
{
    uint8_t n83[11];
    if (!vol.mounted) {
        err = "not mounted";
        return -1;
    }
    if (to_83(name, n83)) {
        err = "not an 8.3 name";
        return -1;
    }
    uint32_t dc;
    if (dir_cluster(dir, &dc))
        return -1;

    /* existing file? */
    fat_dir_t d;
    fat_entry_t e;
    int exists = 0;
    fat_opendir(&d, dir);
    while (fat_readdir(&d, &e))
        if (!e.is_dir && name_eq(name, e.name, strlen(name))) {
            exists = 1;
            break;
        }
    return store(exists ? &e : NULL, dc, n83, data, len);
}

int fat_replace(const char *path, const void *data, size_t len)
{
    fat_entry_t e;
    if (!vol.mounted) {
        err = "not mounted";
        return -1;
    }
    if (fat_find(path, &e))
        return -1;
    if (e.is_dir) {
        err = "a directory";
        return -1;
    }
    return store(&e, 0, NULL, data, len);
}

int fat_delete(const char *path)
{
    if (!vol.mounted) {
        err = "not mounted";
        return -1;
    }
    const char *name = strrchr(path, '/');
    char dir[FAT_NAME_MAX];
    size_t dl = name ? (size_t)(name - path) : 0;
    name = name ? name + 1 : path;
    if (dl >= sizeof dir || !*name) {
        err = "not a file path";
        return -1;
    }
    memcpy(dir, path, dl);
    dir[dl] = 0;
    fat_dir_t d;
    fat_entry_t e;
    if (fat_opendir(&d, dl ? dir : "/"))
        return -1;
    for (;;) {
        uint32_t start = d.index;
        if (!fat_readdir(&d, &e)) {
            err = "file not found";
            return -1;
        }
        if (!name_eq(name, e.name, strlen(name)))
            continue;
        if (e.is_dir) {
            err = "a directory";
            return -1;
        }
        /* 1. the short entry, then the long name pieces right before it */
        static const uint8_t gone = 0xE5;
        if (fsinfo_unknown() || patch_sector(e.dir_lba, e.dir_off, &gone, 1))
            return -1;
        for (uint32_t i = d.index - 1; i-- > start;) {
            uint32_t lba = dir_entry_lba(&d, i);
            const uint8_t *s = lba ? sector(lba) : NULL;
            if (!s)
                return -1;
            uint32_t off = (i % 16) * 32;
            if (s[off + 11] != 0x0F || s[off] == 0xE5)
                break;
            if (patch_sector(lba, off, &gone, 1))
                return -1;
        }
        /* 2. the data is released last: a power cut leaves lost clusters */
        return e.cluster ? free_chain(e.cluster) : 0;
    }
}
