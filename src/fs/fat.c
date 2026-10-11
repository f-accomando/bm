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
    /* the search for free clusters starts where FSInfo says (a PC's hint,
     * or the one bm left there: fsinfo_unknown), not at cluster 2 after
     * every boot (2026-10-11); a hint out of range is no hint */
    vol.next_free = 2;
    if (vol.fsinfo_lba) {
        const uint8_t *fi = sector(vol.fsinfo_lba);
        if (fi && rd32(fi) == 0x41615252 && rd32(fi + 484) == 0x61417272) {
            const uint32_t hint = rd32(fi + 492);
            if (hint >= 2 && hint < vol.clusters + 2)
                vol.next_free = hint;
        }
    }
    vol.fsinfo_reset = 0;
    vol.mounted = 1;
    err = "ok";
    ksnprintf(desc, sizeof desc, "FAT%d, %lu MiB, label %s", vol.fat32 ? 32 : 16,
              (unsigned long)(total / 2048), vol.label[0] ? vol.label : "-");
    return 0;
}

const char *fat_error(void) { return err; }
const char *fat_describe(void) { return vol.mounted ? desc : err; }
uint32_t fat_cluster_bytes(void) { return vol.mounted ? vol.spc * 512 : 0; }

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

int (*fat_load_tick)(void);

int fat_load_part(const fat_entry_t *e, size_t max, uint8_t **data, size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!vol.mounted || e->is_dir) {
        err = "not a file";
        return -1;
    }
    const uint32_t size = e->size < max ? e->size : (uint32_t)max;
    uint32_t csize = vol.spc * 512;
    uint8_t *buf = malloc(size ? size : 1);
    uint8_t *tmp = malloc(csize);
    if (!buf || !tmp) {
        free(buf);
        free(tmp);
        err = "out of memory";
        return -1;
    }
    uint32_t c = e->cluster, done = 0;
    while (done < size) {
        if (is_eoc(c) || c >= vol.clusters + 2) {
            err = "broken cluster chain";
            goto fail;
        }
        uint32_t n = size - done < csize ? size - done : csize;
        if (n == csize) {
            if (sd_read(cluster_lba(c), vol.spc, buf + done))
                goto io;
        } else {
            if (sd_read(cluster_lba(c), (n + 511) / 512, tmp))
                goto io;
            memcpy(buf + done, tmp, n);
        }
        done += n;
        if (fat_load_tick && fat_load_tick()) {
            err = "stopped";
            goto fail;
        }
        if (done < size)
            c = next_cluster(c);
    }
    free(tmp);
    *data = buf;
    *len = size;
    return 0;
io:
    err = "SD read error";
fail:
    free(tmp);
    free(buf);
    return -1;
}

int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len)
{
    return fat_load_part(e, e->size, data, len);
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

/* The FSInfo free count becomes "unknown" before the first change (a PC
 * recomputes it: it is never wrong); its hint says where this mount's
 * search for free clusters starts, the next boot's starts there too. */
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
        wr32(v + 4, vol.next_free >= 2 && vol.next_free < vol.clusters + 2 ? vol.next_free : 0xFFFFFFFF);
        if (patch_sector(vol.fsinfo_lba, 488, v, 8))
            return -1;
    }
    vol.fsinfo_reset = 1;
    return 0;
}

static uint32_t eoc(void) { return vol.fat32 ? 0x0FFFFFFF : 0xFFFF; }

/* the FAT sector (from the start of a copy) with cluster c's entry */
static uint32_t fat_sector_of(uint32_t c)
{
    return (vol.fat32 ? c * 4 : c * 2) / 512;
}

static void put_entry(uint8_t *s, uint32_t c, uint32_t v)
{
    uint32_t off = (vol.fat32 ? c * 4 : c * 2) % 512;
    if (vol.fat32)
        wr32(s + off, (rd32(s + off) & 0xF0000000u) | (v & 0x0FFFFFFF));
    else
        wr16(s + off, v);
}

/* The FAT entries of list[0..n-1], in every copy of the FAT: link, each to
 * the next cluster of the list and the last to `last`; else all 0 (free).
 * Entries next to each other in the list that share a FAT sector change
 * together: that sector of each copy is read and written once (2026-10-11:
 * it was a read and a write per cluster and per copy, a big file's FAT
 * took more card commands than its data). */
static int fat_set_list(const uint32_t *list, uint32_t n, int link, uint32_t last)
{
    for (uint32_t i = 0, j; i < n; i = j) {
        const uint32_t fs = fat_sector_of(list[i]);
        for (j = i + 1; j < n && fat_sector_of(list[j]) == fs; j++)
            ;
        for (uint32_t f = 0; f < vol.nfats; f++) {
            const uint32_t lba = vol.fat_lba + f * vol.fatsz + fs;
            const uint8_t *s = sector(lba);
            if (!s)
                return -1;
            memcpy(wbuf, s, 512);
            for (uint32_t k = i; k < j; k++)
                put_entry(wbuf, list[k], link ? (k + 1 < n ? list[k + 1] : last) : 0);
            if (write_sector(lba, wbuf))
                return -1;
        }
    }
    return 0;
}

/* Sets FAT entry c in every copy of the FAT. */
static int set_fat(uint32_t c, uint32_t v)
{
    return fat_set_list(&c, 1, 1, v);
}

/* Between the pieces of a write's bookkeeping (the chain marked, the old
 * one released): the CPU may go back to the menu (fat_write_tick), but
 * this part is never stopped half way. */
static void tick_only(void)
{
    if (fat_write_tick)
        (void)fat_write_tick();
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

/* Collects `n` free clusters (not yet marked: the caller marks them before
 * the CPU can go anywhere else). The search goes on from where the last
 * one stopped. */
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

/* clusters picked and marked between two ticks: 4 FAT32 sectors (a join
 * writes the sector at a piece's start once more) */
#define ALLOC_PIECE 512

/* n free clusters into list, already a chain in the FAT (the last EOC),
 * before any data goes into them (2026-10-11): a cluster picked is marked
 * at once, in the same piece, so a write that gives the CPU back half way
 * (a fiber of the menu) never shares a cluster with another writer (the
 * menu's settings, another fiber). Pieces of ALLOC_PIECE, each joined to
 * the one before; a tick between them, nonzero stops (all of it released,
 * fat_error() "stopped"). A power cut before the directory entry points at
 * the chain leaves it as lost clusters (a PC's check gives them back). */
static int alloc_chain(uint32_t *list, uint32_t n)
{
    uint32_t done = 0, marked = 0;
    while (done < n) {
        const uint32_t k = n - done < ALLOC_PIECE ? n - done : ALLOC_PIECE;
        if (pick_free(list + done, k))
            goto fail;
        /* the last of the piece before now points at this one: changed with it */
        const uint32_t from = done ? done - 1 : 0;
        marked = done + k;
        if (fat_set_list(list + from, done + k - from, 1, eoc()))
            goto fail;
        done += k;
        if (done < n && fat_write_tick && fat_write_tick()) {
            err = "stopped";
            goto fail;
        }
    }
    return 0;
fail:
    if (marked) {
        const char *why = err;
        fat_set_list(list, marked, 0, 0);
        err = why;
    }
    return -1;
}

/* Releases the chain from c: walked a FAT sector's worth at a time (the
 * next of each one read before any of them changes), each piece released
 * in one go (fat_set_list), a tick between pieces. */
static int free_chain(uint32_t c)
{
    uint32_t piece[128], n = 0;
    for (uint32_t guard = 0; c >= 2 && c < vol.clusters + 2 && guard < vol.clusters; guard++) {
        const uint32_t next = next_cluster(c);
        if (n && (n == sizeof piece / sizeof *piece || fat_sector_of(c) != fat_sector_of(piece[0]))) {
            if (fat_set_list(piece, n, 0, 0))
                return -1;
            n = 0;
            tick_only();
        }
        piece[n++] = c;
        if (is_eoc(next))
            break;
        c = next;
    }
    return n ? fat_set_list(piece, n, 0, 0) : 0;
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

int (*fat_write_tick)(void);
size_t fat_write_done;
static int runs = 1;                    /* the Pi too since 2026-10-11 (each run read back) */
static int writing;                     /* stores under way (fat_writing) */

int fat_write_runs(int on)
{
    int was = runs;
    runs = on;
    return was;
}

int fat_writing(void)
{
    return writing;
}

#define RUN_SECTORS 64                  /* a run of clusters next to each other: 32 KiB */

/* Sectors in one command, read back into check and compared; if it fails
 * or differs, one sector at a time (from now on, too). */
static int write_run(uint32_t lba, uint32_t count, const uint8_t *src, uint8_t *check)
{
    if (sec_lba - lba < count)
        sec_lba = ~0u;                          /* the cached sector is in it */
    if (sd_write(lba, count, src) == 0 && sd_read(lba, count, check) == 0 &&
        memcmp(src, check, count * 512) == 0)
        return 0;
    runs = 0;
    kprintf("\x1b[91mfat: %lu sectors in one write failed or came back wrong: one sector at a "
            "time from now on\x1b[0m\n", (unsigned long)count);
    for (uint32_t s = 0; s < count; s++)
        if (write_sector(lba + s, src + s * 512))
            return -1;
    return 0;
}

/* Where the file goes, looked up once its data is on the card (another
 * writer may have changed the directory meanwhile): 1 and its entry, 0
 * and the cluster of the directory for a new one, -1 if neither. `path`:
 * fat_replace's file; else `name` in `dir` (fat_write_file). */
static int locate(const char *path, const char *dir, const char *name, fat_entry_t *e, uint32_t *dc)
{
    if (path) {
        if (fat_find(path, e))
            return -1;
        if (e->is_dir) {
            err = "a directory";
            return -1;
        }
        return 1;
    }
    if (dir_cluster(dir, dc))
        return -1;
    fat_dir_t d;
    dir_open_cluster(&d, *dc);
    while (fat_readdir(&d, e))
        if (!e->is_dir && name_eq(name, e->name, strlen(name)))
            return 1;
    return 0;
}

/* A whole file (2026-10-11, the Market's 6 MB game froze the menu 30 s):
 *   1. its clusters picked and marked as a chain (alloc_chain)
 *   2. the data into them, a tick after each piece (a run of clusters, or
 *      one cluster without runs): the CPU may go back to the menu, nonzero
 *      stops (the chain released, the old file as it was)
 *   3. with no tick from here, the file looked up again (locate): its
 *      entry points at the new chain, or a new entry `n83`
 *   4. the old chain released last.
 * A power cut leaves the old file or the new one, at worst lost clusters
 * too. Another writer (the menu's settings, another fiber) may come in at
 * a tick: no cluster is ever free in the FAT while one of the two owns it,
 * and the directory is read and changed in one go. */
static int store(const char *path, const char *dir, const char *name, const uint8_t n83[11],
                 const void *data, size_t len)
{
    const uint32_t csize = vol.spc * 512;
    const uint32_t n = (uint32_t)((len + csize - 1) / csize);
    uint32_t *list = n ? malloc(n * sizeof *list) : NULL;
    const uint32_t run_max = vol.spc > RUN_SECTORS ? vol.spc : RUN_SECTORS;
    const uint32_t check_sectors = (uint32_t)((len + 511) / 512 < run_max ? (len + 511) / 512 : run_max);
    uint8_t *check = runs && check_sectors > 1 ? malloc(check_sectors * 512) : NULL;  /* none: sectors */
    if (n && !list) {
        err = "out of memory";
        free(check);
        return -1;
    }
    int rc = -1, ours = 0;              /* ours: the new chain, not yet in a directory entry */
    writing++;
    fat_write_done = 0;
    if (fsinfo_unknown())
        goto out;
    if (n) {
        if (alloc_chain(list, n))
            goto out;
        ours = 1;
    }

    /* the data: clusters next to each other in one piece, up to 32 KiB in
     * one command (runs); else a sector at a time, a cluster a piece. Of
     * the last cluster only the sectors with data (before: all of it,
     * zeros past the end: 64 writes for a byte with 32 KiB clusters) */
    const uint8_t *p = data;
    for (uint32_t i = 0, k; i < n; i += k) {
        const size_t at = (size_t)i * csize;
        k = 1;
        if (check && runs)
            while (i + k < n && list[i + k] == list[i] + k && (k + 1) * vol.spc <= run_max)
                k++;
        const size_t bytes = len - at < (size_t)k * csize ? len - at : (size_t)k * csize;
        const uint32_t full = (uint32_t)(bytes / 512), tail = (uint32_t)(bytes % 512);
        const uint32_t lba = cluster_lba(list[i]);
        if (full > 1 && check && runs) {
            if (write_run(lba, full, p + at, check))
                goto out;
        } else {
            for (uint32_t s = 0; s < full; s++)
                if (write_sector(lba + s, p + at + (size_t)s * 512))
                    goto out;
        }
        if (tail) {
            memset(wbuf, 0, 512);
            memcpy(wbuf, p + at + (size_t)full * 512, tail);
            if (write_sector(lba + full, wbuf))
                goto out;
        }
        fat_write_done = at + bytes;
        if (fat_write_tick && fat_write_tick()) {
            err = "stopped";                    /* nothing points at the clusters yet */
            goto out;
        }
    }

    /* the directory entry points at it: found now, changed in one go */
    fat_entry_t e;
    uint32_t dc = 0;
    const int found = locate(path, dir, name, &e, &dc);
    if (found < 0)
        goto out;
    const uint32_t first = n ? list[0] : 0;
    if (found) {
        uint8_t f[12];                          /* +20: one sector write */
        wr16(f, vol.fat32 ? first >> 16 : 0);   /* cluster, high */
        wr16(f + 2, 0);                         /* write time */
        wr16(f + 4, FAT_DATE);                  /* write date */
        wr16(f + 6, first);                     /* cluster, low */
        wr32(f + 8, (uint32_t)len);
        if (patch_sector(e.dir_lba, e.dir_off + 20, f, 12))
            goto out;
    } else {
        uint8_t de[32];
        make_entry(de, n83, 0x20, first, (uint32_t)len);
        if (add_entry(dc, de))
            goto out;
    }
    ours = 0;
    /* the old data is released last */
    rc = found && e.cluster ? free_chain(e.cluster) : 0;
out:
    if (ours) {                                 /* stopped or failed: nothing points at it */
        const char *why = err;
        fat_set_list(list, n, 0, 0);
        err = why;
    }
    writing--;
    free(list);
    free(check);
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
    return store(NULL, dir, name, n83, data, len);
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
    return store(path, NULL, NULL, NULL, data, len);
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
