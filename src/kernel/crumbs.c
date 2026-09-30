#include "crumbs.h"
#include "arch/cache.h"
#include "lib/printf.h"

#include <string.h>

#define MAGIC 0xBA5EC0DEu

typedef struct {
    uint32_t magic;
    uint32_t frame;
    uint32_t up_ms;
    int32_t irq;
    char what[48];
} crumbs_t;

/* not in .bss: start.S does not clear it (see linker.ld) */
static crumbs_t cr __attribute__((section(".noinit"), aligned(CACHE_LINE)));

static void flush(void)
{
    dcache_clean_range(&cr, sizeof cr);     /* in SDRAM even if the CPU stops */
}

void crumbs_boot(void)
{
    if (cr.magic == MAGIC) {
        cr.what[sizeof cr.what - 1] = 0;
        char irq[24] = "";
        if (cr.irq >= 0)
            ksnprintf(irq, sizeof irq, ", inside IRQ %ld", cr.irq);
        kprintf("\x1b[91mthe Pi froze last time and the watchdog restarted it:\n"
                "  doing: %s, frame %lu%s, %lu s after boot\x1b[0m\n",
                cr.what, cr.frame, irq, cr.up_ms / 1000);
    }
    memset(&cr, 0, sizeof cr);
    cr.magic = MAGIC;
    cr.irq = -1;
    strcpy(cr.what, "boot");
    flush();
}

void crumb(const char *what, const char *name)
{
    ksnprintf(cr.what, sizeof cr.what, name ? "%s \"%s\"" : "%s", what, name);
    cr.frame = 0;
    flush();
}

void crumb_frame(uint32_t frame)
{
    cr.frame = frame;
    flush();
}

void crumb_tick(uint32_t ms)
{
    cr.up_ms = ms;
    flush();
}

void crumb_irq(int irq)
{
    cr.irq = irq;
    flush();
}

void crumbs_print(void)
{
    cr.what[sizeof cr.what - 1] = 0;
    kprintf("while: %s, frame %lu%s\n", cr.what, cr.frame, cr.irq >= 0 ? " (in an IRQ)" : "");
}

void crumbs_clean_exit(void)
{
    cr.magic = 0;
    flush();
}
