#include "crumbs.h"
#include "arch/cache.h"
#include "drivers/uart.h"
#include "drivers/watchdog.h"
#include "kernel/version.h"
#include "lib/printf.h"
#include "wifi/wifi.h"

#include <string.h>

#define MAGIC 0xBA5EC0E0u

typedef struct {
    uint32_t magic;
    uint32_t frame;
    uint32_t up_ms;
    int32_t irq;
    uint32_t crashed;                   /* the red screen, not a freeze */
    uint32_t ring_at;                   /* ring.at when the crash began */
    uint32_t ring_on;                   /* the ring was the log sink then */
    char what[44];
    char kernel[32];
} crumbs_t;

/* not in .bss: start.S does not clear it (see linker.ld) */
static crumbs_t cr __attribute__((section(".noinit"), aligned(CACHE_LINE)));

/* the last lines printed, kept the same way (2026-10-06: a crash or a
 * freeze on a Pi without a serial cable becomes a report at the next
 * boot, with what came before it; a power cut loses them) */
#define RING 4096
#define RING_MAGIC 0x4C4F4752u
typedef struct {
    uint32_t magic, at;
    char text[RING];
} ring_t;
static ring_t ring __attribute__((section(".noinit"), aligned(CACHE_LINE)));

/* the report of the boot before, until main.c takes it */
static char last[RING + 160];
static const char *last_kind;
static size_t last_len;

/* snapshot of the boot before, kept for sdlog_prev() if called after crumbs_boot() */
static char prev_state[160];
static char prev_ring[RING + 1];
static size_t prev_ring_len;
static int booted;

static void ring_putc(char c)
{
    ring.text[ring.at & (RING - 1)] = c;
    ring.at++;
    ring.text[ring.at & (RING - 1)] = 0;
    if (c == '\n')                      /* a whole line in the SDRAM */
        dcache_clean_range(&ring, sizeof ring);
}

static void flush(void)
{
    dcache_clean_range(&cr, sizeof cr);     /* in SDRAM even if the CPU stops */
}

void crumbs_boot(void)
{
    if (cr.magic == MAGIC) {
        cr.what[sizeof cr.what - 1] = 0;
        cr.kernel[sizeof cr.kernel - 1] = 0;
        for (char *p = cr.kernel; *p; p++)
            if ((unsigned char)*p < 32 || *p == 127)
                *p = '?';
        char irq[24] = "";
        if (cr.irq >= 0)
            ksnprintf(irq, sizeof irq, ", inside IRQ %ld", cr.irq);
        last_kind = cr.crashed ? "crash" : "freeze";
        last_len = (size_t)ksnprintf(last, sizeof last, "%s while: %s, frame %lu%s, %lu s after boot, kernel %s, ring %lu, sink %s\n"
                                     "--- the last lines printed ---\n",
                                     cr.crashed ? "a crash (the red screen)" : "the Pi froze (the watchdog restarted it)",
                                     cr.what, cr.frame, irq, cr.up_ms / 1000, cr.kernel,
                                     cr.ring_at, cr.ring_on ? "on" : "off");
        if (last_len >= sizeof last)
            last_len = sizeof last - 1;
        if (ring.magic == RING_MAGIC) {
            uint32_t n = ring.at < RING ? ring.at : RING;
            uint32_t i = 0;
            if (ring.at > RING) {
                while (i < n && ring.text[(ring.at - n + i) & (RING - 1)] != '\n')
                    i++;
                if (i < n)
                    i++;
            }
            for (; i < n && last_len + 1 < sizeof last; i++) {
                char c = ring.text[(ring.at - n + i) & (RING - 1)];
                if (!c)
                    continue;
                last[last_len++] = c;
            }
            last[last_len] = 0;
        }
        if (cr.crashed)
            kprintf("\x1b[91mthe last run ended in a crash: saved as a report\x1b[0m\n");
        else
            kprintf("\x1b[91mthe Pi froze last time and the watchdog restarted it:\n"
                    "  doing: %s, frame %lu%s, %lu s after boot, kernel %s\x1b[0m\n",
                    cr.what, cr.frame, irq, cr.up_ms / 1000, cr.kernel);
    }
    const char *st = crumbs_prev_state();
    size_t slen = strlen(st);
    if (slen >= sizeof prev_state)
        slen = sizeof prev_state - 1;
    memcpy(prev_state, st, slen);
    prev_state[slen] = 0;
    prev_ring_len = crumbs_ring_copy(prev_ring, sizeof prev_ring);
    booted = 1;

    memset(&cr, 0, sizeof cr);
    cr.magic = MAGIC;
    cr.irq = -1;
    strcpy(cr.what, "boot");
    ksnprintf(cr.kernel, sizeof cr.kernel, "%s", bm_version);
    flush();
    ring.magic = RING_MAGIC;
    ring.at = 0;
    dcache_clean_range(&ring, sizeof ring);
    klog_set_ring(ring_putc);
}

/* The ring as it stands, oldest first, characters that are not text as
 * '?'. Before crumbs_boot() it is the run before's (RGB30's BOOTPREV.TXT).
 * After crumbs_boot() it returns the snapshot taken before the reset. */
size_t crumbs_ring_copy(char *out, size_t size)
{
    if (!size)
        return 0;
    if (booted) {
        size_t len = prev_ring_len < size - 1 ? prev_ring_len : size - 1;
        memcpy(out, prev_ring, len);
        out[len] = 0;
        return len;
    }
    size_t len = 0;
    if (ring.magic == RING_MAGIC) {
        uint32_t n = ring.at < RING ? ring.at : RING;
        uint32_t i = 0;
        if (ring.at > RING) {
            while (i < n && ring.text[(ring.at - n + i) & (RING - 1)] != '\n')
                i++;
            if (i < n)
                i++;
        }
        for (; i < n && len + 1 < size; i++) {
            char c = ring.text[(ring.at - n + i) & (RING - 1)];
            if (!c)
                continue;
            out[len++] = (c == '\n' || c == '\t' || ((unsigned char)c >= 32 && c != 127)) ? c : '?';
        }
    }
    out[len] = 0;
    return len;
}

const char *crumbs_prev_state(void)
{
    if (booted)
        return prev_state;
    static char s[160];
    if (cr.magic != MAGIC)
        return "ended cleanly, or the power went (no record in RAM)";
    cr.what[sizeof cr.what - 1] = 0;
    for (char *p = cr.what; *p; p++)
        if ((unsigned char)*p < 32 || *p == 127)
            *p = '?';
    cr.kernel[sizeof cr.kernel - 1] = 0;
    for (char *p = cr.kernel; *p; p++)
        if ((unsigned char)*p < 32 || *p == 127)
            *p = '?';
    ksnprintf(s, sizeof s, "%s while: %s, %lu s after boot, kernel %s, ring %lu, sink %s",
              cr.crashed ? "ended in a crash" : "ended without a clean exit (a freeze, or a restart)",
              cr.what, cr.up_ms / 1000, cr.kernel, cr.ring_at, cr.ring_on ? "on" : "off");
    return s;
}

const char *crumbs_last(const char **kind, size_t *len)
{
    if (!last_kind)
        return NULL;
    *kind = last_kind;
    *len = last_len;
    last_kind = NULL;
    return last;
}

void crumbs_crashed(void)
{
    if (!cr.crashed) {
        /* the first call only: die() or a nested exception must not overwrite these */
        cr.ring_at = ring.at;
        cr.ring_on = (uint32_t)klog_ring_is(ring_putc);
        klog_set_ring(ring_putc);
    }
    cr.crashed = 1;
    flush();
    dcache_clean_range(&ring, sizeof ring);
}

uint32_t crumbs_uptime_ms(void)
{
    return cr.up_ms;
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

void crumbs_restart(int halt)
{
    wifi_leave();
    crumbs_clean_exit();
    uart_flush();
    if (halt)
        watchdog_halt();
    else
        watchdog_reboot();
}
