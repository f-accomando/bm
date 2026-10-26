/*
 * The main-loop guard of the watchdog (src/drivers/watchdog.h, SYS-2): the timer IRQ pets the
 * watchdog only while the main code makes progress. The decision is watchdog_fresh(); this test
 * replays a boot and a hang with the real constant and a counter that wraps.
 */
#include "../../src/drivers/mbox.h"
#include "../../src/drivers/watchdog.h"

#include <stdio.h>
#include <stdlib.h>
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

/* a model of the IRQ: it pets every 100 ms of the clock */
static int pets_between(uint32_t from, uint32_t to, uint32_t mark)
{
    int n = 0;
    for (uint32_t t = from; (uint32_t)(t - to) > 0x80000000u; t += 100000u)
        n += watchdog_fresh(t, mark, WATCHDOG_STALL_US);
    return n;
}

/* The long waits of the main code must mark progress, or a legal block of more than 20 s resets the
 * Pi (found by the audit in docs/design/watchdog-hang.md). Run from the repository root: the call
 * must stand within 400 bytes after the head of each function. */
static void need_mark(const char *file, const char *head)
{
    FILE *f = fopen(file, "rb");
    char *buf = f ? calloc(1, 1 << 20) : NULL;
    size_t n = buf ? fread(buf, 1, (1 << 20) - 1, f) : 0;
    if (f)
        fclose(f);
    char *at = n ? strstr(buf, head) : NULL;
    int ok = at != NULL;
    if (ok) {
        char win[401] = { 0 };
        strncpy(win, at, 400);
        ok = strstr(win, "watchdog_progress();") != NULL;
    }
    free(buf);
    char what[160];
    snprintf(what, sizeof what, "%s: %s marks progress", file, head);
    check(ok, what);
}

int main(void)
{
    const uint32_t L = WATCHDOG_STALL_US;

    check(L >= 5000000u && L <= 60000000u, "the stall limit is between 5 s and 60 s");
    check(watchdog_fresh(123456789u, 0, L), "no call yet (boot): the IRQ pets");
    check(watchdog_fresh(1000, 1000, L), "same instant: fresh");
    check(watchdog_fresh(1000 + L, 1000, L), "exactly at the limit: still fresh");
    check(!watchdog_fresh(1000 + L + 1, 1000, L), "one us past the limit: stale");

    /* main code stops at t = 5 s: the IRQ pets for 20 s more, then never again */
    uint32_t mark = 5000000u;
    int early = pets_between(mark, mark + L, mark);
    int late = pets_between(mark + L + 200000u, mark + L + 3600000000u, mark);
    check(early == 200, "the IRQ pets during the limit after the last progress");
    check(late == 0, "the IRQ never pets after the limit (an hour later too)");

    /* the 1 MHz counter wraps every 71 minutes: the mark just before the wrap */
    mark = 0xFFFFF000u;
    check(watchdog_fresh(0x00000100u, mark, L), "wrap: 4 ms after the mark is fresh");
    check(!watchdog_fresh(0x00000100u + L, mark, L), "wrap: past the limit is stale");

    /* the firmware that does not answer: one or two timeouts are not a verdict, three in a row are (6 s) */
    check(!mbox_is_dead(0) && !mbox_is_dead(1) && !mbox_is_dead(MBOX_DEAD_TIMEOUTS - 1), "one or two mailbox timeouts: alive");
    check(mbox_is_dead(MBOX_DEAD_TIMEOUTS) && mbox_is_dead(10), "three mailbox timeouts in a row: dead");
    need_mark("src/kernel/fiber.c", "void fiber_slice(void)");
    need_mark("src/net/net.c", "int net_wait_step(void)");
    need_mark("src/net/netxfer.c", "static int keep_net(void)");
    need_mark("src/wifi/wifi.c", "static int bp_mem(int write");

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
