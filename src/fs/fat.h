/*
 * FAT16 / FAT32 with long file names (reading). One volume: the first FAT
 * partition of the SD card (or a card formatted without partitions).
 * Writing: files and directories with 8.3 names, created or replaced;
 * the content of an existing file of any name replaced.
 */
#ifndef FAT_H
#define FAT_H

#include <stddef.h>
#include <stdint.h>

#define FAT_NAME_MAX 128

typedef struct {
    char name[FAT_NAME_MAX];    /* long name if present, else 8.3; ASCII */
    uint32_t size;
    uint32_t cluster;
    int is_dir;
    uint32_t dir_lba;           /* where the short entry is (for updates) */
    uint16_t dir_off;
} fat_entry_t;

typedef struct {
    uint32_t cluster;           /* 0 = FAT16 root directory */
    uint32_t index;             /* next 32-byte entry */
} fat_dir_t;

/* Mounts the volume through the block reader (sd_read). Returns 0 on
 * success; fat_error() says why not. */
int fat_mount(void);
const char *fat_error(void);
/* "FAT32, 1024 MiB, label BOOT" */
const char *fat_describe(void);
/* The volume's cluster size in bytes (0 if not mounted): a card's write
 * speed depends on it (the sd: line at boot says it). */
uint32_t fat_cluster_bytes(void);

/* Opens a directory by path ("/" or "/carts", case insensitive). */
int fat_opendir(fat_dir_t *d, const char *path);
/* Next entry (skips volume labels, "." and ".."). Returns 1, or 0 at the end. */
int fat_readdir(fat_dir_t *d, fat_entry_t *e);

/* Reads the first 512 bytes of a file (zero padded). */
int fat_read_head(const fat_entry_t *e, uint8_t buf[512]);

/* Reads a whole file into a malloc'd buffer (caller frees). */
int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len);
/* The same with at most the first `max` bytes (a cartridge's cover: it
 * comes first in the file). */
int fat_load_part(const fat_entry_t *e, size_t max, uint8_t **data, size_t *len);
/* Called after each cluster the two read, if set: the loading screen goes
 * on, the menu's fibers give the CPU back. Nonzero stops the read (it
 * fails, fat_error() "stopped"). */
extern int (*fat_load_tick)(void);

/* Finds a file or directory by path ("/bm/config.txt"). Returns 0. */
int fat_find(const char *path, fat_entry_t *e);

/* Creates the directories of `path` that are missing ("/bm/save"). Names
 * must be valid 8.3 names. Returns 0. */
int fat_mkdirs(const char *path);

/* Writes a whole file in directory `dir` (which must exist), creating it or
 * replacing its content. `name` must be a valid 8.3 name ("CONFIG.TXT";
 * lower case is stored upper case). The new data is written to clusters
 * already marked as a chain before the directory entry points at it, so a
 * power cut leaves either the old or the new file (plus, at worst, lost
 * clusters). The directory is looked up once the data is on the card: a
 * write that gave the CPU back half way (fat_write_tick) and another
 * writer in between both end well, the one that ends last wins. */
int fat_write_file(const char *dir, const char *name, const void *data, size_t len);

/* Replaces the content of an existing file ("/carts/astrowing.bm", long
 * names too), as safely as fat_write_file (gone by the end: it fails). */
int fat_replace(const char *path, const void *data, size_t len);
/* Called, if set, after each piece the two write (a run of clusters, or a
 * cluster) and between the pieces of the FAT's own work (the chain marked,
 * the old chain released; fat_delete's too). The kernel sets it once to
 * fiber_slice_tick: a long write in a fiber of the menu gives the CPU back
 * when its time is over (the Market, an update); netxfer sets its own
 * while it writes (the network polled). Nonzero, during the data or the
 * marking, stops the write before anything points at the new data (it
 * fails, fat_error() "stopped", the clusters released); elsewhere it is
 * not looked at. */
extern int (*fat_write_tick)(void);
/* The bytes of the file being written that are on the card so far (set
 * before each fat_write_tick: a progress bar). */
extern size_t fat_write_done;
/* Whole clusters (up to 32 KiB of them next to each other, the sectors
 * with data of the last one too) in one SD command, read back and
 * compared, instead of one sector at a time (a kernel took the RGB30 more
 * than a minute, a 6 MB game froze the Pi's menu 30 s); a run that fails
 * or comes back different turns it off for good. On by default (the Pi
 * too since 2026-10-11). Returns what it was. */
int fat_write_runs(int on);
/* Writes under way (a fiber's, half way through, included): the RGB30
 * does not write its last run's log over one before a restart. */
int fat_writing(void);

/* Deletes a file ("/carts/Il mio gioco.bm", long names too; not a
 * directory). The directory entry goes first, then the clusters are
 * released (a FAT sector's worth at a time): a power cut in between only
 * leaves lost clusters. */
int fat_delete(const char *path);

#endif
