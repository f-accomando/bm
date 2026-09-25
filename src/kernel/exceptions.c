/*
 * Fatal exception reporting: register dump on the serial console,
 * red screen if a framebuffer is available, and an LED blink code
 * equal to the exception number (1 = undef, 2 = swi, 3 = pabt, 4 = dabt...).
 */
#include "exceptions.h"
#include "drivers/fb.h"
#include "drivers/led.h"
#include "lib/printf.h"

#include <stdarg.h>

static framebuffer_t *panic_fb;

static const char *const exc_names[] = {
    "Reset", "Undefined instruction", "Software interrupt (SWI)",
    "Prefetch abort", "Data abort", "Unused vector", "IRQ", "FIQ",
};

static const char *const mode_names[16] = {
    [0x0] = "USR", [0x1] = "FIQ", [0x2] = "IRQ", [0x3] = "SVC",
    [0x7] = "ABT", [0xB] = "UND", [0xF] = "SYS",
};

void exceptions_set_panic_fb(void *fb)
{
    panic_fb = fb;
}

static inline uint32_t read_dfsr(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(v)); return v; }
static inline uint32_t read_ifsr(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(v)); return v; }
static inline uint32_t read_dfar(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(v)); return v; }

static void __attribute__((noreturn)) die(uint32_t code)
{
    if (panic_fb)
        fb_fill_rect(panic_fb, 0, 0, panic_fb->width, panic_fb->height,
                     fb_color(panic_fb, 170, 0, 0));
    kprintf("System halted. LED blink code: %lu\n", code);
    led_blink_code(code);
}

void exception_handler(uint32_t type, exc_frame_t *f)
{
    const char *name = type < 8 ? exc_names[type] : "Unknown";
    const char *mode = mode_names[f->spsr & 0xF];

    kprintf("\n*** EXCEPTION: %s ***\n", name);
    kprintf("PC=%08lx LR=%08lx SP=%08lx CPSR=%08lx (%s mode)\n",
            f->pc, f->lr, f->sp, f->spsr, mode ? mode : "???");
    for (int i = 0; i < 13; i++)
        kprintf("r%-2d=%08lx%s", i, f->r[i], (i % 4 == 3 || i == 12) ? "\n" : "  ");

    if (type == EXC_DABT)
        kprintf("DFAR=%08lx DFSR=%08lx\n", read_dfar(), read_dfsr());
    else if (type == EXC_PABT)
        kprintf("IFSR=%08lx\n", read_ifsr());
    if (!(f->pc & 3) && f->pc < 0x20000000u)
        kprintf("insn @PC = %08lx\n", *(volatile uint32_t *)f->pc);

    die(type);
}

void panic(const char *fmt, ...)
{
    va_list ap;
    kprintf("\n*** PANIC: ");
    va_start(ap, fmt);
    kvlog(fmt, ap);
    va_end(ap);
    kprintf("\n");
    die(9);
}
