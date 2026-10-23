/*
 * The crash record (src/kernel/crumbs.c) on the PC: the record of the boot before is in RAM when the
 * kernel starts and the timer IRQ is already on, so crumb_irq(), crumb_tick(), crumb_frame() and crumb()
 * run before crumbs_boot() reads that record. They must not write into it (BETA-1 C4: a freeze header
 * said "ring 3601, sink off" because flush() wrote the new boot's state over the old record). QEMU
 * cannot test this: it has no freeze guard, so it never restarts on the watchdog.
 * crumbs.c is included, to reach its statics.
 */
#include "../../src/kernel/crumbs.c"

#include <stdio.h>
#include <string.h>

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

/* ---- what crumbs.c uses */
const char bm_version_tag[] = "bmVER=testver";
void dcache_clean_range(const volatile void *addr, uint32_t len) { (void)addr; (void)len; }
void uart_putc(char c) { (void)c; }
void uart_flush(void) {}
void watchdog_halt(void) { for (;;) ; }
void watchdog_reboot(void) { for (;;) ; }
void watchdog_progress(void) {}
void wifi_leave(void) {}

int main(void)
{
    /* the record of a boot that froze: 3601 characters in the ring, the ring was the sink */
    cr.magic = MAGIC;
    cr.crashed = 0;
    cr.frame = 42;
    cr.up_ms = 12345;
    cr.irq = 7;
    cr.ring_at = 3601;
    cr.ring_on = 1;
    strcpy(cr.what, "doing the old thing");
    strcpy(cr.kernel, "oldkernel");
    ring.magic = RING_MAGIC;
    ring.at = 9;
    memcpy(ring.text, "old line\n", 9);

    /* the timer IRQ before crumbs_boot(): the old record stays as it was */
    crumb_irq(-1);
    crumb_tick(50);
    crumb_frame(3);
    crumb("new thing", NULL);
    check(cr.ring_at == 3601, "before crumbs_boot: ring_at of the old record is kept");
    check(cr.ring_on == 1, "before crumbs_boot: ring_on of the old record is kept");
    check(cr.up_ms == 12345, "before crumbs_boot: up_ms of the old record is kept");
    check(cr.irq == 7, "before crumbs_boot: the IRQ field of the old record is kept");
    check(cr.frame == 42 && !strcmp(cr.what, "doing the old thing"), "before crumbs_boot: frame and what are kept");
    check(strstr(crumbs_prev_state(), "ring 3601, sink on") != NULL, "prev_state says ring 3601, sink on");

    crumbs_boot();
    const char *kind = NULL;
    size_t len = 0;
    const char *rep = crumbs_last(&kind, &len);
    check(rep && !strcmp(kind, "freeze"), "the report is a freeze");
    check(rep && strstr(rep, "ring 3601, sink on") != NULL, "the freeze header has the old ring state");
    check(rep && strstr(rep, "the Pi froze") && strstr(rep, "12 s after boot"), "the header has the old up time");
    check(rep && strstr(rep, "old line") != NULL, "the old ring follows the marker");

    /* after crumbs_boot(): the record is this boot's */
    crumb_tick(777);
    check(cr.up_ms == 777, "after crumbs_boot: crumb_tick writes");
    check(cr.ring_on == 1 && klog_ring_get() == (void *)ring_putc, "after crumbs_boot: the ring is the sink");
    ring_putc('x');
    crumb_frame(5);
    check(cr.ring_at == ring.at, "after crumbs_boot: the live ring state is stored");

    /* a crash: the values of its first call stay */
    crumbs_crashed();
    const uint32_t at_crash = cr.ring_at;
    check(cr.crashed == 1 && cr.ring_on == 1 && at_crash == ring.at, "crumbs_crashed stores the ring state");
    ring_putc('y');
    ring_putc('z');
    crumb_tick(900);
    check(cr.ring_at == at_crash, "after the crash flush leaves the ring state alone");

    printf("crumbs: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
