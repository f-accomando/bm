#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#define EXC_RESET   0
#define EXC_UNDEF   1
#define EXC_SWI     2
#define EXC_PABT    3
#define EXC_DABT    4
#define EXC_UNUSED  5
#define EXC_IRQ     6
#define EXC_FIQ     7

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef struct {
    uint32_t sp;        /* banked SP of the interrupted mode */
    uint32_t lr;        /* banked LR of the interrupted mode */
    uint32_t spsr;
    uint32_t r[13];
    uint32_t pc;        /* address of the faulting instruction */
} exc_frame_t;

/* Optional: paint the screen red on a fatal exception. */
void exceptions_set_panic_fb(void *fb);

void exception_handler(uint32_t type, exc_frame_t *f) __attribute__((noreturn));

void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
#endif

#endif
