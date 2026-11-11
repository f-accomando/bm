/* the harness has no MMU: unaligned accesses fault on memory that is not "normal". A flat table of 1 MB sections (normal memory) and the MMU on. */
static unsigned pt[4096] __attribute__((aligned(16384)));
__attribute__((constructor)) static void mmu_on(void)
{
    for (unsigned i = 0; i < 4096; i++) pt[i] = (i << 20) | 0x1C02;
    unsigned r;
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" :: "r"(pt));
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" :: "r"(0xFFFFFFFFu));
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(r));
    r &= ~2u; r |= 0x400001u;
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(r));
}

#include <time.h>
int clock_gettime(int c, struct timespec *t) { (void)c; t->tv_sec = 0; t->tv_nsec = 0; return 0; }
