/*
 * Data cache maintenance by address (Cortex-A55: 64-byte lines), the API of
 * arch/cache.h. "All" variants walk nothing by set/way (unreliable with the
 * DSU's L3): they are not used on this build for large ranges.
 */
#include "arch/cache.h"
#include "a64.h"

static inline uintptr_t line_down(uintptr_t a) { return a & ~(uintptr_t)(CACHE_LINE - 1); }

void dcache_clean_range(const volatile void *addr, uint32_t len)
{
    uintptr_t a = line_down((uintptr_t)addr), end = (uintptr_t)addr + len;
    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("dc cvac, %0" :: "r"(a) : "memory");
    dsb_sy();
}

void dcache_clean_invalidate_range(const volatile void *addr, uint32_t len)
{
    uintptr_t a = line_down((uintptr_t)addr), end = (uintptr_t)addr + len;
    for (; a < end; a += CACHE_LINE)
        __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
    dsb_sy();
}

/* Set/way over L1 and L2 of this core: only meaningful before other masters
 * share the memory; kept for the API. */
static void dcache_setway(int invalidate)
{
    uint64_t clidr = read_sysreg(clidr_el1);
    for (unsigned level = 0; level < 2; level++) {
        unsigned ctype = (clidr >> (level * 3)) & 7;
        if (ctype < 2)
            continue;               /* no data cache at this level */
        write_sysreg(csselr_el1, level << 1);
        isb();
        uint64_t ccsidr = read_sysreg(ccsidr_el1);
        unsigned line_shift = (ccsidr & 7) + 4;
        unsigned ways = ((ccsidr >> 3) & 0x3ff) + 1;
        unsigned sets = ((ccsidr >> 13) & 0x7fff) + 1;
        unsigned way_shift = __builtin_clz(ways - 1 ? ways - 1 : 1);
        for (unsigned w = 0; w < ways; w++)
            for (unsigned s = 0; s < sets; s++) {
                uint64_t v = ((uint64_t)w << way_shift) | ((uint64_t)s << line_shift) | (level << 1);
                if (invalidate)
                    __asm__ volatile("dc cisw, %0" :: "r"(v) : "memory");
                else
                    __asm__ volatile("dc csw, %0" :: "r"(v) : "memory");
            }
    }
    dsb_sy();
}

void dcache_clean_all(void)
{
    dcache_setway(0);
}

void dcache_clean_invalidate_all(void)
{
    dcache_setway(1);
}

void icache_invalidate_all(void)
{
    __asm__ volatile("dsb sy\n ic iallu\n dsb sy\n isb" ::: "memory");
}
