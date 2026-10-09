/*
 * Fatal exception reporting: register dump on the serial console,
 * red screen if a framebuffer is available, and an LED blink code
 * equal to the exception number (1 = undef, 2 = swi, 3 = pabt, 4 = dabt...).
 */
#include "exceptions.h"
#include "drivers/watchdog.h"
#include "crumbs.h"
#include "drivers/fb.h"
#include "drivers/mmio.h"
#include "gfx/console.h"
#include "drivers/led.h"
#include "lib/printf.h"
#include "lib/heap.h"
#include "drivers/timer.h"

#include <stdarg.h>

extern char __text_end[];

static framebuffer_t *panic_fb;
static uint32_t panic_w, panic_h;       /* the console's mode */

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
    panic_w = panic_fb->width;
    panic_h = panic_fb->height;
}

static inline uint32_t read_dfsr(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(v)); return v; }
static inline uint32_t read_ifsr(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(v)); return v; }
static inline uint32_t read_dfar(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(v)); return v; }

/* Red screen: the console (if up) is cleared white on red so the dump that
 * follows is readable on HDMI; otherwise the raw framebuffer is filled. */
static void panic_screen(void)
{
    __asm__ volatile("cpsid i" ::: "memory");  /* stop the tick (LED, etc.) */
    watchdog_stop();                            /* the crash screen stays */
    /* during a game the screen is in the game's mode and the console is
     * suspended: back to the console mode, or the dump is never seen */
    if (console_active() && panic_fb &&
        (panic_fb->depth != 32 || panic_fb->width != panic_w || panic_fb->height != panic_h))
        fb_init(panic_fb, panic_w, panic_h, 2);
    if (console_active())
        console_panic();
    else if (panic_fb)
        fb_fill_rect(panic_fb, 0, 0, panic_fb->width, panic_fb->height,
                     fb_color(panic_fb, 170, 0, 0));
}

/* The words on the stack that are return addresses (the instruction before
 * them a BL or BLX): who called what crashed, for addr2line on the kernel's
 * ELF. A guess, but without frame pointers it is what there is. */
static void callers(uint32_t sp)
{
    if (sp & 3 || sp < 0x8000 || sp >= heap_end())
        return;
    int n = 0;
    kprintf("called from:");
    for (const uint32_t *w = (const uint32_t *)sp; n < 16 && (uintptr_t)(w + 1) <= heap_end() &&
                                                   w < (const uint32_t *)sp + 2048; w++) {
        const uint32_t a = *w;
        if (a & 3 || a < 0x8004 || a > (uint32_t)__text_end)
            continue;
        const uint32_t insn = *(const uint32_t *)(a - 4);
        if ((insn & 0x0F000000u) == 0x0B000000u || (insn & 0xFE000000u) == 0xFA000000u ||
            (insn & 0x0FFFFFF0u) == 0x012FFF30u) {
            kprintf("%s%08lx", n == 8 ? "\n  " : " ", a);
            n++;
        }
    }
    kprintf("\n");
}

static void __attribute__((noreturn)) die(uint32_t code)
{
    kprintf("System halted. LED blink code: %lu\n", code);
    crumbs_crashed();
    /* after a minute the watchdog restarts the Pi, which keeps its memory:
     * the next boot sends this screen as a report (2026-10-06, the crashes
     * seen without a serial cable); not in the first 20 s of a boot, where
     * it would come again and again */
    if (crumbs_uptime_ms() >= 20000) {
        kprintf("Restarting in 60 s: this screen goes out as a report\n");
        const uint32_t t0 = timer_ticks();
        while (timer_ticks() - t0 < 60u * 1000000u) {
            for (unsigned i = 0; i < code; i++) {
                led_set(1);
                timer_delay_ms(200);
                led_set(0);
                timer_delay_ms(300);
            }
            timer_delay_ms(1500);
        }
        watchdog_reboot();
    }
    led_blink_code(code);
}

void exception_handler(uint32_t type, exc_frame_t *f)
{
    const char *name = type < 8 ? exc_names[type] : "Unknown";
    const char *mode = mode_names[f->spsr & 0xF];

    panic_screen();
    kprintf("\n*** EXCEPTION: %s ***\n", name);
    crumbs_print();
    kprintf("PC=%08lx LR=%08lx SP=%08lx CPSR=%08lx (%s mode)\n",
            f->pc, f->lr, f->sp, f->spsr, mode ? mode : "???");
    for (int i = 0; i < 13; i++)
        kprintf("r%-2d=%08lx%s", i, f->r[i], (i % 4 == 3 || i == 12) ? "\n" : "  ");

    if (type == EXC_DABT)
        kprintf("DFAR=%08lx DFSR=%08lx\n", read_dfar(), read_dfsr());
    else if (type == EXC_PABT)
        kprintf("IFSR=%08lx\n", read_ifsr());
    if (!(f->pc & 3) && f->pc < PERIPHERAL_BASE)
        kprintf("insn @PC = %08lx\n", *(volatile uint32_t *)f->pc);
    callers(f->sp);

    die(type);
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
    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    callers(sp);
    die(9);
}
