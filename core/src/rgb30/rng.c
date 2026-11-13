/*
 * drivers/rng.h on the AArch64 build (Bluetooth LE pairing keys): the
 * counter's low bits, read with timing jitter (memory traffic, interrupts),
 * stirred through splitmix64. Not a hardware TRNG: good enough for the
 * keys of a game console's keyboard link.
 */
#include "drivers/rng.h"
#include "a64.h"

#include <stdint.h>

static uint64_t state;

static uint64_t splitmix(void)
{
    uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static void stir(void)
{
    static volatile uint8_t scratch[4096];
    for (int i = 0; i < 64; i++) {
        uint64_t t0 = read_sysreg(cntpct_el0);
        for (int j = 0; j < 4096; j += 64)
            scratch[(j * 7 + i * 131) & 4095] ^= (uint8_t)t0;
        uint64_t t1 = read_sysreg(cntpct_el0);
        state ^= (t1 - t0) << (i & 31) ^ t1;
        splitmix();
    }
}

void rng_start(void)
{
    stir();
}

int rng_read(void *buf, size_t len)
{
    stir();
    uint8_t *p = buf;
    while (len) {
        uint64_t v = splitmix();
        for (int i = 0; i < 8 && len; i++, len--)
            *p++ = (uint8_t)(v >> (i * 8));
    }
    return 0;
}
