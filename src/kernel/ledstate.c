#include "ledstate.h"
#include "drivers/led.h"
#include "net/netxfer.h"

static volatile unsigned reasons = LED_BOOT;
static int lit = -1;                    /* what the LED shows now (-1: not set yet) */

void ledstate_set(unsigned reason, int on)
{
    if (on)
        reasons |= reason;
    else
        reasons &= ~reason;
}

unsigned ledstate(void)
{
    /* a kernel coming over the network (netxfer.c), then its restart; a
     * file arriving or being written */
    const int f = netxfer_file_state(0, 0, 0, 0);
    return reasons | (netxfer_kernel_state(0, 0, 0) || f == NETXFER_RECEIVING || f == NETXFER_WRITING ?
                      LED_BUSY : 0);
}

void ledstate_tick(uint32_t ms)
{
    const int on = ledstate() ? (int)(ms / 1000 % 2 == 0) : 1;
    if (on != lit) {
        led_set(on);
        lit = on;
    }
}
