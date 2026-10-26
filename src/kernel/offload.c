#include "kernel/offload.h"

/* no worker core: the Pi Zero W, the host tests. The RGB30 defines it in src/rgb30/smp.c */
__attribute__((weak)) int offload_run(void (*fn)(void *blk), void *blk, unsigned size, void (*idle)(void))
{
    (void)fn; (void)blk; (void)size; (void)idle;
    return -1;
}
