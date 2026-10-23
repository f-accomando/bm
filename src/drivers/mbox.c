#include "mbox.h"
#include "mmio.h"
#include "arch/cache.h"
#include "timer.h"

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
            return 0;
    mmio_write(MBOX_WRITE, msg);

    for (;;) {
        while (mmio_read(MBOX_STATUS) & MBOX_EMPTY)
            if (timer_ticks() - t0 > MBOX_TIMEOUT_US)
                return 0;
        uint32_t reply = mmio_read(MBOX_READ);
        if (reply == msg) {
            dmb();
            dcache_clean_invalidate_range(buf, len);
            return buf[1] == MBOX_RESPONSE_OK;
        }
    }
}
