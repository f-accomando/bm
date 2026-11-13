/*
 * Host test for core/src/kernel/bmz_install.c: the SD card is an image file (tests/fs/test_fat.c does the same).
 *
 *   test_install IMAGE EXPECT_DONE EXPECT_FAILED
 *
 * tests/bmz/run_install_test.py puts the .bmz files in /bmz, runs this, then checks the card with fsck.vfat and mtools.
 */
#define _DEFAULT_SOURCE
#include "kernel/bmz_install.h"
#include "bm/bmz.h"
#include "fs/fat.h"
#include "drivers/sd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *img;

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    return fseek(img, (long)lba * 512, SEEK_SET) || fread(buf, 512, count, img) != count ? -1 : 0;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
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

int main(int argc, char **argv)
{
    int done, failed = 0;

    if (argc != 4)
        return 2;
    img = fopen(argv[1], "r+b");
    if (!img || fat_mount() != 0) {
        printf("FAIL: cannot mount %s\n", argv[1]);
        return 2;
    }
    done = bmz_install_all(&failed);
    printf("done %d, failed %d\n", done, failed);
    if (done != atoi(argv[2]) || failed != atoi(argv[3])) {
        printf("FAIL: expected done %s, failed %s\n", argv[2], argv[3]);
        return 1;
    }
    /* a second pass finds only the files that failed */
    int again = bmz_install_all(&failed);
    if (again != 0 || failed != atoi(argv[3])) {
        printf("FAIL: second pass done %d failed %d\n", again, failed);
        return 1;
    }
    fclose(img);
    return 0;
}
