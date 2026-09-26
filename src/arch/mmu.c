#include "mmu.h"
#include "cache.h"

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

#define PERIPH_START    0x20000000u
#define PERIPH_END      0x21000000u

#define SCTLR_M         (1u << 0)
#define SCTLR_A         (1u << 1)
#define SCTLR_C         (1u << 2)
#define SCTLR_Z         (1u << 11)
#define SCTLR_I         (1u << 12)
#define SCTLR_U         (1u << 22)
#define SCTLR_XP        (1u << 23)

static uint32_t ttb[4096] __attribute__((aligned(16384)));

void mmu_init(uint32_t arm_mem_end)
{
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t addr = i << 20;
        uint32_t attr;

        if (addr < arm_mem_end)
            attr = MEM_CACHED;
        else if (addr < PERIPH_START)
            attr = MEM_UNCACHED;
        else if (addr < PERIPH_END)
            attr = MEM_DEVICE;
        else
            attr = 0;
        ttb[i] = attr ? (addr | attr) : 0;
    }

    uint32_t r = 0;
    __asm__ volatile(
        "mcr p15, 0, %0, c7, c7, 0\n"   /* invalidate I and D caches */
        "mcr p15, 0, %0, c8, c7, 0\n"   /* invalidate TLBs */
        "mcr p15, 0, %0, c7, c10, 4\n"  /* DSB */
        "mcr p15, 0, %0, c2, c0, 2\n"   /* TTBCR = 0: TTBR0 only */
        : : "r"(r) : "memory");

    /* Domain 0 = client (permissions checked); walks uncached. */
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" : : "r"(1u));
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"((uint32_t)ttb));

    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_Z | SCTLR_XP | SCTLR_U;
    sctlr &= ~SCTLR_A;
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr) : "memory");
    arm_isb();
}

int mmu_enabled(void)
{
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    return sctlr & SCTLR_M;
}
