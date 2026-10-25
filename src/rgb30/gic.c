/*
 * GICv3 (GIC-600 on the RK3566, QEMU's gicv3 in the tests), the API of
 * kernel/irq.h: numbers are INTIDs (PPI 16-31, SPI 32+). Everything goes to
 * this core as non-secure group 1. On the RGB30 TF-A has already set the
 * distributor up (secure state): writes it does not allow us are ignored.
 */
#include "kernel/irq.h"
#include "plat.h"
#include "a64.h"
#include "io.h"

#define GICD_CTLR       0x0000
#define GICD_TYPER      0x0004
#define GICD_IGROUPR    0x0080
#define GICD_ISENABLER  0x0100
#define GICD_ICENABLER  0x0180
#define GICD_ICPENDR    0x0280
#define GICD_IPRIORITYR 0x0400
#define GICD_ICFGR      0x0c00
#define GICD_IROUTER    0x6000

#define GICR_WAKER      0x0014
#define GICR_SGI        0x10000         /* SGI/PPI frame */
#define GICR_IGROUPR0   (GICR_SGI + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI + 0x0100)
#define GICR_ICENABLER0 (GICR_SGI + 0x0180)
#define GICR_IPRIORITYR (GICR_SGI + 0x0400)

#define MAX_IRQ         256

static struct { irq_fn fn; void *arg; } handlers[MAX_IRQ];
static uint32_t irq_total;

static inline volatile uint32_t *gicd(uint32_t off) { return (volatile uint32_t *)(uintptr_t)(PLAT_GICD + off); }
static inline volatile uint32_t *gicr(uint32_t off) { return (volatile uint32_t *)(uintptr_t)(PLAT_GICR + off); }

void irq_init(void)
{
    /* CPU interface through system registers */
    write_sysreg(S3_0_C12_C12_5, read_sysreg(S3_0_C12_C12_5) | 7);   /* ICC_SRE_EL1 */
    isb();
    /* redistributor of this core awake (TF-A did it on the RGB30) */
    *gicr(GICR_WAKER) &= ~2u;
    for (int i = 0; i < 100000 && (*gicr(GICR_WAKER) & 4); i++)
        ;
    /* distributor: affinity routing, group 1 on (bit 4 ARE, bit 1 Grp1) */
    *gicd(GICD_CTLR) |= (1u << 4) | (1u << 1);
    for (int i = 0; i < 100000 && (*gicd(GICD_CTLR) & (1u << 31)); i++)
        ;                                   /* RWP */

    *gicr(GICR_ICENABLER0) = 0xffffffffu;
    *gicr(GICR_IGROUPR0) = 0xffffffffu;
    for (int i = 0; i < 32; i += 4)
        *gicr(GICR_IPRIORITYR + i) = 0xa0a0a0a0u;

    write_sysreg(S3_0_C4_C6_0, 0xf0);       /* ICC_PMR_EL1: let everything above 0xf0 in */
    write_sysreg(S3_0_C12_C12_3, 0);        /* ICC_BPR1_EL1 */
    write_sysreg(S3_0_C12_C12_4, 0);        /* ICC_CTLR_EL1: EOImode 0 (EOI also deactivates) */
    write_sysreg(S3_0_C12_C12_7, 1);        /* ICC_IGRPEN1_EL1 */
    isb();
}

void irq_register(unsigned irq, irq_fn fn, void *arg)
{
    if (irq >= MAX_IRQ)
        return;
    handlers[irq].fn = fn;
    handlers[irq].arg = arg;
}

void irq_enable(unsigned irq)
{
    if (irq < 32) {
        *gicr(GICR_ISENABLER0) = 1u << irq;
        return;
    }
    if (irq >= MAX_IRQ)
        return;
    volatile uint32_t *grp = gicd(GICD_IGROUPR + (irq / 32) * 4);
    *grp |= 1u << (irq % 32);
    volatile uint8_t *prio = (volatile uint8_t *)gicd(GICD_IPRIORITYR) + irq;
    *prio = 0xa0;
    *(volatile uint64_t *)gicd(GICD_IROUTER + irq * 8) = 0;   /* affinity 0.0.0.0: this core */
    *gicd(GICD_ISENABLER + (irq / 32) * 4) = 1u << (irq % 32);
}

void irq_disable(unsigned irq)
{
    if (irq < 32)
        *gicr(GICR_ICENABLER0) = 1u << irq;
    else if (irq < MAX_IRQ)
        *gicd(GICD_ICENABLER + (irq / 32) * 4) = 1u << (irq % 32);
}

/* from vectors.S */
void irq_dispatch(void)
{
    for (;;) {
        uint32_t id = (uint32_t)read_sysreg(S3_0_C12_C12_0) & 0xffffff;     /* ICC_IAR1_EL1 */
        if (id >= 1020)
            return;                         /* spurious: nothing (more) pending */
        irq_total++;
        if (id < MAX_IRQ && handlers[id].fn)
            handlers[id].fn(handlers[id].arg);
        else
            irq_disable(id);                /* nobody wants it: keep it quiet */
        write_sysreg(S3_0_C12_C12_1, id);   /* ICC_EOIR1_EL1 */
    }
}

uint32_t irq_count(void)
{
    return irq_total;
}
