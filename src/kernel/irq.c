#include "irq.h"
#include "exceptions.h"
#include "drivers/mmio.h"

#define IRQ_BASE        (PERIPHERAL_BASE + 0xB200)
#define IRQ_PENDING1    (IRQ_BASE + 0x04)
#define IRQ_PENDING2    (IRQ_BASE + 0x08)
#define FIQ_CONTROL     (IRQ_BASE + 0x0C)
#define IRQ_ENABLE1     (IRQ_BASE + 0x10)
#define IRQ_ENABLE2     (IRQ_BASE + 0x14)
#define IRQ_ENABLE_BASIC (IRQ_BASE + 0x18)
#define IRQ_DISABLE1    (IRQ_BASE + 0x1C)
#define IRQ_DISABLE2    (IRQ_BASE + 0x20)
#define IRQ_DISABLE_BASIC (IRQ_BASE + 0x24)

#define NUM_IRQS 64

static struct {
    irq_fn fn;
    void *arg;
} handlers[NUM_IRQS];

static volatile uint32_t count;

void irq_init(void)
{
    irq_cpu_disable();
    mmio_write(FIQ_CONTROL, 0);
    mmio_write(IRQ_DISABLE1, 0xFFFFFFFFu);
    mmio_write(IRQ_DISABLE2, 0xFFFFFFFFu);
    mmio_write(IRQ_DISABLE_BASIC, 0xFFFFFFFFu);
    dmb();
}

void irq_register(unsigned irq, irq_fn fn, void *arg)
{
    if (irq >= NUM_IRQS)
        return;
    uint32_t s = irq_save();
    handlers[irq].fn = fn;
    handlers[irq].arg = arg;
    irq_restore(s);
}

void irq_enable(unsigned irq)
{
    dmb();
    mmio_write(irq < 32 ? IRQ_ENABLE1 : IRQ_ENABLE2, 1u << (irq % 32));
    dmb();
}

void irq_disable(unsigned irq)
{
    dmb();
    mmio_write(irq < 32 ? IRQ_DISABLE1 : IRQ_DISABLE2, 1u << (irq % 32));
    dmb();
}

uint32_t irq_count(void)
{
    return count;
}

/* Called from irq_entry (vectors.S) with IRQs masked. */
void irq_handler(void)
{
    dmb();
    uint32_t pend[2] = { mmio_read(IRQ_PENDING1), mmio_read(IRQ_PENDING2) };
    dmb();

    for (unsigned bank = 0; bank < 2; bank++) {
        uint32_t p = pend[bank];
        while (p) {
            unsigned bit = __builtin_ctz(p);
            unsigned irq = bank * 32 + bit;
            p &= p - 1;
            if (handlers[irq].fn) {
                handlers[irq].fn(handlers[irq].arg);
            } else {
                irq_disable(irq);
                panic("unhandled IRQ %u", irq);
            }
            count++;
        }
    }
    dmb();
}
