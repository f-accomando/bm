#include "mbox.h"
#include "mmio.h"
#include "arch/cache.h"

#define MBOX_BASE    (PERIPHERAL_BASE + 0xB880)
#define MBOX_READ    (MBOX_BASE + 0x00)
#define MBOX_STATUS  (MBOX_BASE + 0x18)
#define MBOX_WRITE   (MBOX_BASE + 0x20)

#define MBOX_FULL    0x80000000u
#define MBOX_EMPTY   0x40000000u

int mbox_call(uint8_t channel, volatile uint32_t *buf)
{
    uint32_t msg = (ARM_TO_BUS(buf) & ~0xFu) | (channel & 0xF);
    uint32_t len = buf[0];

    /* The GPU reads and writes the buffer in RAM, not in our D-cache. */
    dcache_clean_invalidate_range(buf, len);
    dmb();
    while (mmio_read(MBOX_STATUS) & MBOX_FULL)
        ;
    mmio_write(MBOX_WRITE, msg);

    for (;;) {
        while (mmio_read(MBOX_STATUS) & MBOX_EMPTY)
            ;
        uint32_t reply = mmio_read(MBOX_READ);
        if (reply == msg) {
            dmb();
            dcache_clean_invalidate_range(buf, len);
            return buf[1] == MBOX_RESPONSE_OK;
        }
    }
}
