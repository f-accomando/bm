/*
 * The main-loop guard of the watchdog (src/drivers/watchdog.h, SYS-2): the timer IRQ pets the
 * watchdog only while the main code makes progress. The decision is watchdog_fresh(); this test
 * replays a boot and a hang with the real constant and a counter that wraps.
 */
#include "../../src/drivers/watchdog.h"

#include <stdio.h>

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

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
