/* bmhost: no interrupts on the PC; the audio "interrupt" runs from the
 * frame loop (audio_idle). */
#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

typedef void (*irq_fn)(void *arg);
void irq_init(void);
void irq_register(unsigned irq, irq_fn fn, void *arg);
void irq_enable(unsigned irq);
void irq_disable(unsigned irq);
static inline void irq_cpu_enable(void) { }
static inline void irq_cpu_disable(void) { }
static inline uint32_t irq_save(void) { return 0; }
static inline void irq_restore(uint32_t cpsr) { (void)cpsr; }
uint32_t irq_count(void);

#endif
