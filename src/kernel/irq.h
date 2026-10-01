#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

/* BCM2835 interrupt numbers (0-31 bank 1, 32-63 bank 2). */
#define IRQ_TIMER1  1       /* system timer compare 1 (0 and 2 belong to the GPU) */
#define IRQ_TIMER3  3
#define IRQ_USB     9
#define IRQ_AUX     29
#define IRQ_GPIO0   49
#define IRQ_UART    57

typedef void (*irq_fn)(void *arg);

void irq_init(void);
void irq_register(unsigned irq, irq_fn fn, void *arg);
void irq_enable(unsigned irq);
void irq_disable(unsigned irq);

#ifdef __aarch64__
/* RGB30 build (src/rgb30): the I bit of DAIF; the numbers are GIC INTIDs */
static inline void irq_cpu_enable(void)  { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline void irq_cpu_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }

static inline uint32_t irq_save(void)
{
    uint64_t daif;
    __asm__ volatile("mrs %0, daif\n msr daifset, #2" : "=r"(daif) :: "memory");
    return (uint32_t)daif;
}

static inline void irq_restore(uint32_t daif)
{
    if (!(daif & 0x80))
        irq_cpu_enable();
}
#else
static inline void irq_cpu_enable(void)  { __asm__ volatile("cpsie i" ::: "memory"); }
static inline void irq_cpu_disable(void) { __asm__ volatile("cpsid i" ::: "memory"); }

/* Masks IRQs and returns the previous state, for short critical sections. */
static inline uint32_t irq_save(void)
{
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr\n cpsid i" : "=r"(cpsr) :: "memory");
    return cpsr;
}

static inline void irq_restore(uint32_t cpsr)
{
    if (!(cpsr & 0x80))
        irq_cpu_enable();
}

#endif

uint32_t irq_count(void);   /* total IRQs handled */

#endif
