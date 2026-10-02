#include "cache.h"

void dcache_clean_range(const volatile void *addr, uint32_t len)
{
    uint32_t a = (uint32_t)addr & ~(CACHE_LINE - 1);
    uint32_t end = (uint32_t)addr + len;

    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(a) : "memory");
    arm_dsb();
}

void dcache_clean_invalidate_range(const volatile void *addr, uint32_t len)
{
    uint32_t a = (uint32_t)addr & ~(CACHE_LINE - 1);
    uint32_t end = (uint32_t)addr + len;

    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(a) : "memory");
    arm_dsb();
}

void icache_invalidate_all(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" : : "r"(0) : "memory");
#if __ARM_ARCH >= 7
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 6" : : "r"(0) : "memory");  /* BPIALL */
    arm_dsb();
#endif
    arm_isb();
}

#if __ARM_ARCH >= 7
/* ARMv7 has no operation on the whole data cache: each set and way of
 * each level up to the point of coherency (CLIDR, CCSIDR). */
static void dcache_by_set_way(int invalidate)
{
    uint32_t clidr;
    __asm__ volatile("mrc p15, 1, %0, c0, c0, 1" : "=r"(clidr));
    uint32_t loc = clidr >> 24 & 7;
    arm_dsb();
    for (uint32_t level = 0; level < loc; level++) {
        if ((clidr >> (3 * level) & 7) < 2)
            continue;                           /* no data cache here */
        uint32_t ccsidr;
        __asm__ volatile("mcr p15, 2, %1, c0, c0, 0\n"    /* CSSELR */
                         "isb\n"
                         "mrc p15, 1, %0, c0, c0, 0"       /* CCSIDR */
                         : "=r"(ccsidr) : "r"(level << 1));
        uint32_t line_shift = (ccsidr & 7) + 4;
        uint32_t ways = (ccsidr >> 3 & 0x3FF) + 1;
        uint32_t sets = (ccsidr >> 13 & 0x7FFF) + 1;
        uint32_t way_shift = ways > 1 ? (uint32_t)__builtin_clz(ways - 1) : 0;
        for (uint32_t way = 0; way < ways; way++)
            for (uint32_t set = 0; set < sets; set++) {
                uint32_t v = way << way_shift | set << line_shift | level << 1;
                if (invalidate)
                    __asm__ volatile("mcr p15, 0, %0, c7, c14, 2" : : "r"(v) : "memory");
                else
                    __asm__ volatile("mcr p15, 0, %0, c7, c10, 2" : : "r"(v) : "memory");
            }
    }
    arm_dsb();
}

void dcache_clean_all(void)
{
    dcache_by_set_way(0);
}

void dcache_clean_invalidate_all(void)
{
    dcache_by_set_way(1);
}
#else
void dcache_clean_all(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 0" : : "r"(0) : "memory");
    arm_dsb();
}

void dcache_clean_invalidate_all(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c14, 0" : : "r"(0) : "memory");
    arm_dsb();
}
#endif
