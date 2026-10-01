/*
 * Fatal exceptions and panic() on the AArch64 build: register dump on the
 * serial console and on the screen (white on red), then the red LED blinks
 * the code (1 sync abort, 2 IRQ, 3 FIQ, 4 SError, 9 panic) forever.
 */
#include "a64.h"
#include "kernel/exceptions.h"
#include "kernel/crumbs.h"
#include "drivers/fb.h"
#include "drivers/led.h"
#include "gfx/console.h"
#include "lib/printf.h"

#include <stdarg.h>

static framebuffer_t *panic_fb;

void exceptions_set_panic_fb(void *fb)
{
    panic_fb = fb;
}

static void panic_screen(void)
{
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    if (console_active())
        console_panic();
    else if (panic_fb)
        fb_fill_rect(panic_fb, 0, 0, panic_fb->width, panic_fb->height,
                     fb_color(panic_fb, 170, 0, 0));
}

static const char *esr_class(uint32_t ec)
{
    switch (ec) {
    case 0x00: return "unknown reason";
    case 0x01: return "WFI/WFE trapped";
    case 0x07: return "FP/SIMD access trapped";
    case 0x0e: return "illegal execution state";
    case 0x15: return "SVC";
    case 0x16: return "HVC";
    case 0x17: return "SMC";
    case 0x18: return "system register access";
    case 0x20: case 0x21: return "instruction abort";
    case 0x22: return "PC alignment fault";
    case 0x24: case 0x25: return "data abort";
    case 0x26: return "SP alignment fault";
    case 0x2c: return "FP exception";
    case 0x2f: return "SError";
    case 0x3c: return "BRK (breakpoint)";
    default:   return "?";
    }
}

static void print_hex64(const char *name, uint64_t v)
{
    kprintf("%s=%08lx%08lx", name, (uint32_t)(v >> 32), (uint32_t)v);
}

void a64_exception(uint64_t type, a64_frame_t *f)
{
    static const char *const names[] = { "?", "synchronous exception", "IRQ", "FIQ", "SError" };
    uint32_t ec = (uint32_t)(f->esr >> 26) & 0x3f;

    panic_screen();
    kprintf("\n*** EXCEPTION: %s", type < 5 ? names[type] : "?");
    if (type == 1 || type == 4)
        kprintf(" - %s", esr_class(ec));
    kprintf(" ***\n");
    crumbs_print();
    print_hex64("PC", f->elr); kprintf("  ");
    print_hex64("LR", f->x[30]); kprintf("\n");
    print_hex64("SP", f->sp); kprintf("  ");
    print_hex64("FAR", f->far); kprintf("\n");
    kprintf("ESR=%08lx SPSR=%08lx\n", (uint32_t)f->esr, (uint32_t)f->spsr);
    for (int i = 0; i < 30; i++) {
        kprintf("x%-2d=%08lx%08lx%s", i, (uint32_t)(f->x[i] >> 32), (uint32_t)f->x[i],
                (i % 2 == 1) ? "\n" : "  ");
    }
    kprintf("System halted. LED blink code: %lu\n", (uint32_t)type);
    led_blink_code((unsigned)type);
}

void panic(const char *fmt, ...)
{
    va_list ap;
    panic_screen();
    kprintf("\n*** PANIC: ");
    va_start(ap, fmt);
    kvlog(fmt, ap);
    va_end(ap);
    kprintf("\n");
    crumbs_print();
    kprintf("System halted. LED blink code: 9\n");
    led_blink_code(9);
}
