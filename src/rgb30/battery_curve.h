/*
 * The RGB30's cell: charge from the voltage (2026-10-27, from the full
 * run-down in docs/design/rgb30-battery-calibration.md). Plain C, no
 * hardware, so a test on the PC can use it (tests/rgb30/test_battery_curve.c).
 *
 * The voltage sags under load (0.126 ohm: cell, protection, wiring), so the
 * table is read at the rest voltage: the measured voltage plus the drop of
 * the current. 0% is 3400 mV at rest (3.3 V under 700 mA, 14 minutes before
 * the cliff of the cell); 100% is 4212 mV (the charger's end).
 */
#ifndef BM_BATTERY_CURVE_H
#define BM_BATTERY_CURVE_H

#define BAT_OHM_MILLI   126             /* the resistance, in milliohm */
#define BAT_EMPTY_MV    3400            /* at rest: 0% */

/* the voltage at rest: ma is signed, negative = discharge, positive = charge */
static inline int battery_rest_mv(int mv, int ma)
{
    return mv - ma * BAT_OHM_MILLI / 1000;
}

/* the charge (0..100) at a rest voltage: a table, linear in between */
static inline int battery_curve_pct(int rest_mv)
{
    static const short v[] = { 3400, 3435, 3506, 3553, 3664, 3728, 3757, 3777, 3813, 3864, 3943, 4014, 4089, 4172, 4212 };
    static const short pc[] = {    0,    1,    4,   10,   19,   28,   37,   46,   56,   65,   74,   82,   91,   99,  100 };
    if (rest_mv <= v[0])
        return 0;
    for (int i = 1; i < (int)(sizeof v / sizeof v[0]); i++)
        if (rest_mv < v[i])
            return pc[i - 1] + (pc[i] - pc[i - 1]) * (rest_mv - v[i - 1]) / (v[i] - v[i - 1]);
    return 100;
}

#endif
