#include "prop.h"
#include "mbox.h"

#define PROP_MAX_WORDS 8

static volatile uint32_t __attribute__((aligned(16))) msg[6 + PROP_MAX_WORDS];

int prop_query(uint32_t tag, uint32_t *vals, unsigned n)
{
    if (n > PROP_MAX_WORDS)
        return -1;

    msg[0] = (6 + n) * 4;
    msg[1] = MBOX_REQUEST;
    msg[2] = tag;
    msg[3] = n * 4;
    msg[4] = 0;
    for (unsigned i = 0; i < n; i++)
        msg[5 + i] = vals[i];
    msg[5 + n] = MBOX_TAG_LAST;

    if (!mbox_call(MBOX_CH_PROP, msg) || !(msg[4] & 0x80000000u))
        return -1;
    for (unsigned i = 0; i < n; i++)
        vals[i] = msg[5 + i];
    return 0;
}

uint32_t prop_clock_rate(uint32_t clock_id)
{
    uint32_t v[2] = { clock_id, 0 };
    return prop_query(PROP_GET_CLOCK_RATE, v, 2) == 0 ? v[1] : 0;
}
