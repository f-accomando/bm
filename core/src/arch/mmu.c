#include "mmu.h"
#include "cache.h"
#include "drivers/mmio.h"

#define SECTION         (2u << 0)
#define B               (1u << 2)
#define C               (1u << 3)
#define XN              (1u << 4)
#define AP_RW           (3u << 10)      /* privileged and user read/write */
#define TEX(x)          ((uint32_t)(x) << 12)

/* Note: on ARM1176 the S bit makes normal memory uncacheable, keep it 0. */
#define MEM_CACHED      (SECTION | AP_RW | TEX(1) | C | B)  /* WB, WA */
#define MEM_UNCACHED    (SECTION | AP_RW | TEX(1) | XN)     /* normal, NC */
#define MEM_DEVICE      (SECTION | AP_RW | TEX(0) | B | XN) /* shared device */

#define PERIPH_START    PERIPHERAL_BASE
#ifdef BM_ZERO2
#define PERIPH_END      (LOCAL_BASE + 0x100000u)    /* + the ARM's own */
#else
#define PERIPH_END      0x21000000u
#endif

#define SCTLR_M         (1u << 0)
#define SCTLR_A         (1u << 1)
#define SCTLR_C         (1u << 2)
#define SCTLR_Z         (1u << 11)
#define SCTLR_I         (1u << 12)
#define SCTLR_U         (1u << 22)      /* ARMv7: always 1 */
#define SCTLR_XP        (1u << 23)      /* ARMv7: always 1 */

static uint32_t ttb[4096] __attribute__((aligned(16384)));

void mmu_init(uint32_t arm_mem_end, uint32_t ram_end)
{
    if (ram_end > PERIPH_START)
        ram_end = PERIPH_START;
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t addr = i << 20;
        uint32_t attr;

        if (addr < arm_mem_end)
            attr = MEM_CACHED;
        else if (addr < ram_end)
            attr = MEM_UNCACHED;
        else if (addr >= PERIPH_START && addr < PERIPH_END)
            attr = MEM_DEVICE;
        else
            attr = 0;
        ttb[i] = attr ? (addr | attr) : 0;
    }

    uint32_t r = 0;
#if __ARM_ARCH >= 7
    /* the caches are clean at reset (Cortex-A53): instruction cache,
     * branch predictor and TLBs; ARMv7 has no "both caches" operation */
    __asm__ volatile(
        "mcr p15, 0, %0, c7, c5, 0\n"   /* ICIALLU */
        "mcr p15, 0, %0, c7, c5, 6\n"   /* BPIALL */
        "mcr p15, 0, %0, c8, c7, 0\n"   /* TLBIALL */
        "mcr p15, 0, %0, c2, c0, 2\n"   /* TTBCR = 0: TTBR0 only */
        "dsb\n"
        "isb\n"
        : : "r"(r) : "memory");
#else
    __asm__ volatile(
        "mcr p15, 0, %0, c7, c7, 0\n"   /* invalidate I and D caches */
        "mcr p15, 0, %0, c8, c7, 0\n"   /* invalidate TLBs */
        "mcr p15, 0, %0, c7, c10, 4\n"  /* DSB */
        "mcr p15, 0, %0, c2, c0, 2\n"   /* TTBCR = 0: TTBR0 only */
        : : "r"(r) : "memory");
#endif

    /* Domain 0 = client (permissions checked); walks uncached. */
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" : : "r"(1u));
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"((uint32_t)ttb));
    arm_isb();

    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_Z | SCTLR_XP | SCTLR_U;
    sctlr &= ~SCTLR_A;
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr) : "memory");
    arm_isb();
}

int mmu_set_cached(const void *start, uint32_t size, int cached)
{
    const uint32_t a = (uint32_t)(uintptr_t)start, first = (a + 0xFFFFFu) >> 20, end = (a + size) >> 20;
    if (end <= first)
        return 0;
    /* no dirty line of the range may be written back over what is written
     * uncached from now on, and no stale line read after */
    dcache_clean_invalidate_range(start, size);
    arm_dsb();
    int n = 0;
    for (uint32_t i = first; i < end; i++) {
        const uint32_t attr = ttb[i] & ~0xFFF00000u;
        if (attr != MEM_CACHED && attr != MEM_UNCACHED)
            continue;                   /* not ARM memory */
        ttb[i] = i << 20 | (cached ? MEM_CACHED : MEM_UNCACHED);
        n++;
    }
    dcache_clean_range(&ttb[first], (end - first) * 4u);    /* the walks read memory */
    arm_dsb();
    const uint32_t r = 0;
#if __ARM_ARCH >= 7
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0\n" "dsb\n" "isb\n" : : "r"(r) : "memory");  /* TLBIALL */
#else
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0\n" "mcr p15, 0, %0, c7, c10, 4\n" : : "r"(r) : "memory");
#endif
    arm_isb();
    return n;
}

int mmu_enabled(void)
{
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    return sctlr & SCTLR_M;
}
