/*
 * drivers/sd.h in QEMU (tests): the SD image is a RAM disk that QEMU puts
 * at RAMDISK_ADDR (-device loader,file=sd.img,addr=0x50000000,force-raw=on);
 * its size is in the first partition entry plus the partition start, so
 * the MBR is all it needs.
 */
#ifdef PLAT_VIRT
#include "drivers/sd.h"
#include "plat.h"

#include <string.h>

#define RAMDISK_ADDR    0x50000000u
#define RAMDISK_MAX     (0x0e000000u)    /* up to the framebuffers */

static uint32_t blocks;
static const char *err = "";

int sd_init(void)
{
    const uint8_t *mbr = (const uint8_t *)(uintptr_t)RAMDISK_ADDR;
    if (mbr[510] != 0x55 || mbr[511] != 0xaa) {
        err = "no disk image (no MBR signature)";
        blocks = 0;
        return -1;
    }
    uint32_t start = mbr[446 + 8] | mbr[446 + 9] << 8 | mbr[446 + 10] << 16 | (uint32_t)mbr[446 + 11] << 24;
    uint32_t count = mbr[446 + 12] | mbr[446 + 13] << 8 | mbr[446 + 14] << 16 | (uint32_t)mbr[446 + 15] << 24;
    blocks = start + count;
    if (blocks > RAMDISK_MAX / 512)
        blocks = RAMDISK_MAX / 512;
    err = "";
    return 0;
}

int sd_read(uint32_t lba, uint32_t count, void *buf)
{
    if (lba + count > blocks || lba + count < lba)
        return -1;
    memcpy(buf, (const void *)(uintptr_t)(RAMDISK_ADDR + lba * 512u), count * 512u);
    return 0;
}

int sd_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (lba + count > blocks || lba + count < lba)
        return -1;
    memcpy((void *)(uintptr_t)(RAMDISK_ADDR + lba * 512u), buf, count * 512u);
    return 0;
}

uint32_t sd_blocks(void)        { return blocks; }
int sd_is_hc(void)              { return 1; }
const char *sd_error(void)      { return err; }
const char *sd_controller(void) { return "ramdisk"; }
#endif
