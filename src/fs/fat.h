/*
 * FAT16 / FAT32, read only, with long file names. One volume: the first
 * FAT partition of the SD card (or a card formatted without partitions).
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

#endif
