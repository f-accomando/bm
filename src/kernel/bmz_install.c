#include "bmz_install.h"
#include "bm/bmz.h"
#include "fs/fat.h"
#include "lib/printf.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FILES  32                       /* one pass; the rest waits for the next */
#define E_CARD     (-100)

int bmz_install_file(const char *path, char *msg, size_t n)
{
    fat_entry_t e;
    uint8_t *file = NULL, *raw = NULL;
    size_t flen = 0;
    bmz_info_t info;
    int r;

    if (fat_find(path, &e) != 0 || e.is_dir || fat_load(&e, &file, &flen) != 0) {
        ksnprintf(msg, n, "cannot read %s", path);
        return E_CARD;
    }
    r = bmz_parse(file, flen, &info);
    if (r == BMZ_OK) {
        raw = malloc(info.raw_len);
        if (!raw) {
            free(file);
            ksnprintf(msg, n, "no memory for %u KB", (unsigned)(info.raw_len / 1024));
            return E_CARD;
        }
        r = bmz_extract(&info, raw);
    }
    if (r != BMZ_OK) {
        ksnprintf(msg, n, "%s: %s", path, bmz_error(r));
        free(raw);
        free(file);
        return r;
    }
    char target[32];
    ksnprintf(target, sizeof target, "%s/%s", info.dir, info.name);
    r = fat_mkdirs(info.dir) == 0 && fat_write_file(info.dir, info.name, raw, info.raw_len) == 0 &&
                fat_find(target, &e) == 0 && e.size == info.raw_len
            ? BMZ_OK
            : E_CARD;
    if (r == BMZ_OK)
        ksnprintf(msg, n, "%s", target);
    else
        ksnprintf(msg, n, "%s: could not write %s (%s)", path, target, fat_error());
    free(raw);
    free(file);
    return r;
}

int bmz_install_all(int *failed)
{
    char names[MAX_FILES][FAT_NAME_MAX];
    int count = 0, done = 0, bad = 0;
    fat_dir_t d;
    fat_entry_t e;

    if (failed)
        *failed = 0;
    if (fat_opendir(&d, BMZ_DIR) != 0)
        return 0;                           /* no /bmz: nothing to do */
    while (count < MAX_FILES && fat_readdir(&d, &e) == 1) {
        const char *dot = strrchr(e.name, '.');
        if (!e.is_dir && dot && (!strcmp(dot, ".bmz") || !strcmp(dot, ".BMZ")))
            ksnprintf(names[count++], FAT_NAME_MAX, "%s/%s", BMZ_DIR, e.name);
    }
    /* the directory is read before anything is written or deleted in it */
    int (*const saved_tick)(void) = fat_write_tick;
    fat_write_tick = NULL;                  /* a boot or a monitor command: no fiber to give the CPU back to */
    for (int i = 0; i < count; i++) {
        char msg[160];
        if (bmz_install_file(names[i], msg, sizeof msg) == BMZ_OK) {
            kprintf("bmz: %s -> %s\n", names[i], msg);
            if (fat_delete(names[i]) != 0)
                kprintf("bmz: %s stays in /bmz (%s)\n", names[i], fat_error());
            done++;
        } else {
            kprintf("bmz: %s\n", msg);
            bad++;
        }
    }
    fat_write_tick = saved_tick;
    if (failed)
        *failed = bad;
    return done;
}
