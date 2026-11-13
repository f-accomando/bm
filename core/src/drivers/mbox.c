#include "mbox.h"
#include <stddef.h>
#include "mmio.h"
#include "arch/cache.h"
#include "timer.h"
#include "lib/printf.h"

#define MBOX_BASE    (PERIPHERAL_BASE + 0xB880)
#define MBOX_READ    (MBOX_BASE + 0x00)
#define MBOX_STATUS  (MBOX_BASE + 0x18)
#define MBOX_WRITE   (MBOX_BASE + 0x20)

#define MBOX_FULL    0x80000000u
#define MBOX_EMPTY   0x40000000u

/* The GPU firmware may stop answering (a QPU reset that does not end): the wait gives up
 * after this long and the call fails. The buffers of the callers are static, so a late
 * answer lands in valid memory. Harm of no limit: the main code hangs for ever while the
 * timer IRQ runs (the freeze of 23 Oct, SYS-2). */
#define MBOX_TIMEOUT_US 2000000u

static unsigned timeouts;                   /* in a row */
static void (*dead_hook)(void);
static void (*diag_hook)(void);
static uint32_t last_ok_us;                 /* when the last answer came (timer_ticks) */
static uint32_t last_tag;                   /* the first tag of that call */

void mbox_set_dead_hook(void (*fn)(void))
{
    dead_hook = fn;
}

void mbox_set_diag_hook(void (*fn)(void))
{
    diag_hook = fn;
}

/* a call ended without an answer: `why` says which wait gave up */
static int mbox_timeout(const char *why, uint8_t channel, const volatile uint32_t *buf)
{
    timeouts++;
    if (timeouts == 1) {
        kprintf("mbox: no answer on channel %u to tag 0x%08lx (%s), status %08lx, the last answer was %lu ms ago (tag 0x%08lx)\n",
                channel, (unsigned long)buf[2], why, (unsigned long)mmio_read(MBOX_STATUS),
                (unsigned long)((timer_ticks() - last_ok_us) / 1000), (unsigned long)last_tag);
        if (diag_hook)
            diag_hook();
    }
    if (mbox_is_dead(timeouts) && dead_hook) {
        void (*fn)(void) = dead_hook;
        dead_hook = NULL;                   /* once */
        fn();
    }
    return 0;
}

int mbox_call(uint8_t channel, volatile uint32_t *buf)
{
    uint32_t msg = (ARM_TO_BUS(buf) & ~0xFu) | (channel & 0xF);
    uint32_t len = buf[0];

    /* The GPU reads and writes the buffer in RAM, not in our D-cache. */
    dcache_clean_invalidate_range(buf, len);
    dmb();
    const uint32_t t0 = timer_ticks();
    while (mmio_read(MBOX_STATUS) & MBOX_FULL)
        if (timer_ticks() - t0 > MBOX_TIMEOUT_US)
            return mbox_timeout("the request queue stayed full", channel, buf);
    mmio_write(MBOX_WRITE, msg);

    for (;;) {
        while (mmio_read(MBOX_STATUS) & MBOX_EMPTY)
            if (timer_ticks() - t0 > MBOX_TIMEOUT_US)
                return mbox_timeout("no reply", channel, buf);
        uint32_t reply = mmio_read(MBOX_READ);
        if (reply == msg) {
            timeouts = 0;
            last_ok_us = timer_ticks();
            last_tag = buf[2];
            dmb();
            dcache_clean_invalidate_range(buf, len);
            return buf[1] == MBOX_RESPONSE_OK;
        }
    }
}
