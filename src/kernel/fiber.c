#include "fiber.h"
#include "lib/printf.h"

#include <string.h>

#define GUARD 0xF1BE5AFEu

void fiber_switch(uint32_t **save, uint32_t *load);
void fiber_start(void);

static fiber_t *current;

static void fiber_main(fiber_t *f)
{
    f->fn(f->arg);
    f->done = 1;
    current = NULL;
    fiber_switch(&f->sp, f->back);      /* never comes back */
}

void fiber_prepare(fiber_t *f, void *stack, size_t size, void (*fn)(void *), void *arg)
{
    memset(f, 0, sizeof *f);
    f->stack = stack;
    f->size = size;
    f->fn = fn;
    f->arg = arg;
    for (size_t i = 0; i < 16 && i < size / 4; i++)
        f->stack[i] = GUARD;
    /* the frame fiber_switch pops (fiber.S): fpscr, pad, d8-d15, r4-r12, lr */
    uint32_t *sp = (uint32_t *)((uintptr_t)((uint8_t *)stack + size) & ~7u) - 28;
    memset(sp, 0, 28 * 4);
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    sp[0] = fpscr;
    sp[18] = (uint32_t)(uintptr_t)f;            /* r4: the argument */
    sp[19] = (uint32_t)(uintptr_t)fiber_main;   /* r5: the function */
    sp[27] = (uint32_t)(uintptr_t)fiber_start;  /* lr */
    f->sp = sp;
}

int fiber_resume(fiber_t *f)
{
    if (f->done || current)
        return !f->done;
    current = f;
    fiber_switch(&f->back, f->sp);
    current = NULL;
    for (int i = 0; i < 16; i++)
        if (f->stack[i] != GUARD) {
            /* the stack ran over: memory below it is already damaged */
            kprintf("\x1b[91mfiber: stack overflow (%lu bytes)\x1b[0m\n", (unsigned long)f->size);
            for (;;)
                ;
        }
    return !f->done;
}

void fiber_yield(void)
{
    fiber_t *f = current;
    if (!f)
        return;
    current = NULL;
    fiber_switch(&f->sp, f->back);
    current = f;
}

fiber_t *fiber_current(void)
{
    return current;
}

void fiber_cancel(fiber_t *f)
{
    f->cancel = 1;
}

int fiber_cancelled(void)
{
    return current && current->cancel;
}
