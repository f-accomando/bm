#include "rng.h"
#include "mmio.h"
#include "timer.h"

#include <string.h>

#define RNG_BASE    (PERIPHERAL_BASE + 0x104000)
#define RNG_CTRL    (RNG_BASE + 0x0)
#define RNG_STATUS  (RNG_BASE + 0x4)        /* bits 31..24: words ready */
#define RNG_DATA    (RNG_BASE + 0x8)
#define RNG_INT_MASK (RNG_BASE + 0x10)

static int started;

static void start(void)
{
    mmio_write(RNG_STATUS, 0x40000);        /* discard the first numbers (warm-up) */
    mmio_write(RNG_INT_MASK, mmio_read(RNG_INT_MASK) | 1);
    mmio_write(RNG_CTRL, 1);
    started = 1;
}

int rng_read(void *buf, size_t len)
{
    if (!started)
        start();
    uint8_t *p = buf;
    while (len) {
        uint32_t t0 = timer_ticks();
        while (!(mmio_read(RNG_STATUS) >> 24))
            if (timer_ticks() - t0 > 200000)
                return -1;
        uint32_t v = mmio_read(RNG_DATA);
        size_t n = len < 4 ? len : 4;
        memcpy(p, &v, n);
        p += n;
        len -= n;
    }
    return 0;
}
