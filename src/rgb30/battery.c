/*
 * The RGB30's battery (2026-10-10): one place for the bar of the menu, the
 * low-battery LED and the icon over the games (bm_set_battery).
 *
 * The bolt used to come up to 10 s after the cable went in (the user,
 * 2026-10-10): the whole battery was read every 10 s, and "charging" came
 * from the charger's state alone, which starts a moment after the plug.
 * Now the RK817's plug bit (SYS_STS, one I2C byte) is looked at 4 times a
 * second and decides the bolt; a change reads the rest at once and the
 * voltage again 2 s later (on and off the charger it jumps, then settles).
 * The voltage alone every 10 s, as before.
 */
#include "battery.h"
#include "plat.h"
#include "drivers/timer.h"
#include "kernel/ledstate.h"

#define FULL_US     10000000u       /* the voltage and the charger's state */
#define PLUG_US     250000u         /* the plug */
#define SETTLE_US   2000000u        /* after the plug came or went */
#define LOW_ON      20              /* the icon over the games from 20%... */
#define LOW_OFF     23              /* ...until 23% (the voltage sags under load) */

static int known, pct, charging, low, plug = -2;
static uint32_t at_full, at_plug, next_full = FULL_US;

static void read_all(void)
{
    int mv, charge;
    known = plat_battery(&mv, &charge) == 0 && mv > 0;
    if (!known)
        return;
    const int in = plat_power_in();
    plug = in;
    /* on the charger: the plug when the PMIC says it, its state if not */
    const int on = in >= 0 ? in : charge >= 1;
    ledstate_set(LED_POWER, mv < 3450 && !on);
    pct = charge == 2 ? 100 : battery_percent(mv);         /* full: four bars */
    charging = on && charge != 2;
    if (on)
        low = 0;
    else if (pct <= LOW_ON)
        low = 1;
    else if (pct >= LOW_OFF)
        low = 0;
}

int battery_state(int *p, int *c, int *l)
{
    const uint32_t now = timer_ticks();
    if (!at_full || now - at_full >= next_full) {
        read_all();
        at_full = at_plug = now | 1;
        next_full = FULL_US;
    } else if (known && now - at_plug >= PLUG_US) {
        at_plug = now;
        const int in = plat_power_in();
        if (in >= 0 && in != plug) {
            read_all();                     /* the cable: the bolt now */
            at_full = now | 1;
            next_full = SETTLE_US;          /* and the voltage once it settles */
        }
    }
    if (p) *p = pct;
    if (c) *c = charging;
    if (l) *l = low;
    return known;
}
