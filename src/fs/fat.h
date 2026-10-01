/*
 * FAT16 / FAT32 with long file names (reading). One volume: the first FAT
 * partition of the SD card (or a card formatted without partitions).
 * Writing: files and directories with 8.3 names, created or replaced.
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

/* Opens a directory by path ("/" or "/carts", case insensitive). */
int fat_opendir(fat_dir_t *d, const char *path);
/* Next entry (skips volume labels, "." and ".."). Returns 1, or 0 at the end. */
int fat_readdir(fat_dir_t *d, fat_entry_t *e);

/* Reads the first 512 bytes of a file (zero padded). */
int fat_read_head(const fat_entry_t *e, uint8_t buf[512]);

/* Reads a whole file into a malloc'd buffer (caller frees). */
int fat_load(const fat_entry_t *e, uint8_t **data, size_t *len);

/* Finds a file or directory by path ("/bm/config.txt"). Returns 0. */
int fat_find(const char *path, fat_entry_t *e);

/* Creates the directories of `path` that are missing ("/bm/save"). Names
 * must be valid 8.3 names. Returns 0. */
int fat_mkdirs(const char *path);

/* Writes a whole file in directory `dir` (which must exist), creating it or
 * replacing its content. A new file needs a valid 8.3 name ("CONFIG.TXT";
 * lower case is stored upper case); an existing one is found by its long
 * name too, and keeps it. The new data is written to free
 * clusters before the directory entry points at it, so a power cut leaves
 * either the old or the new file (plus, at worst, lost clusters). */
int fat_write_file(const char *dir, const char *name, const void *data, size_t len);

/* Deletes a file ("/carts/Il mio gioco.bm", long names too; not a
 * directory). The directory entry goes first, then the clusters are
 * released: a power cut in between only leaves lost clusters. */
int fat_delete(const char *path);

#endif
