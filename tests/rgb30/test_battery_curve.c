/*
 * The RGB30's charge from the voltage and the current (src/rgb30/battery_curve.h):
 * rows of the full run-down of 2026-10-27 (bm/bat09.csv, menu only), with the
 * charge left from the summed current (3700 mAh from full to 3.3 V under
 * load). The percent must be within 4 points; the bars of the menu
 * (75/50/25/10) must be the right ones.
 */
#include <stdio.h>
#include "rgb30/battery_curve.h"

static const struct { int mv, ma, left; } row[] = {
    { 4092,  -633, 100 }, { 4009,  -638,  91 }, { 3933,  -645,  82 }, { 3861,  -653,  74 },
    { 3780,  -665,  65 }, { 3728,  -673,  56 }, { 3692,  -677,  46 }, { 3671,  -680,  37 },
    { 3642,  -683,  28 }, { 3577,  -692,  19 }, { 3464,  -706,  10 }, { 3417,  -709,   4 },
    { 3344,  -722,   1 },
    /* on the charger: an indication, wider tolerance below */
};

static int bars(int pct)
{
    return pct >= 75 ? 4 : pct >= 50 ? 3 : pct >= 25 ? 2 : pct >= 10 ? 1 : 0;
}

int main(void)
{
    int n = 0, ok = 0;
    for (unsigned i = 0; i < sizeof row / sizeof row[0]; i++) {
        const int p = battery_curve_pct(battery_rest_mv(row[i].mv, row[i].ma));
        const int d = p > row[i].left ? p - row[i].left : row[i].left - p;
        n++;
        if (d <= 4)
            ok++;
        else
            printf("battery_curve: %d mV %d mA: %d%%, expected %d%%\n", row[i].mv, row[i].ma, p, row[i].left);
    }
    /* the bars at the loaded voltage of the menu (about 700 mA) */
    static const struct { int mv, ma, bars; } b[] = {
        { 3900, -640, 4 }, { 3780, -665, 3 }, { 3700, -677, 2 }, { 3560, -692, 1 }, { 3417, -709, 0 },
    };
    for (unsigned i = 0; i < sizeof b / sizeof b[0]; i++) {
        const int got = bars(battery_curve_pct(battery_rest_mv(b[i].mv, b[i].ma)));
        n++;
        if (got == b[i].bars)
            ok++;
        else
            printf("battery_curve: %d mV %d mA: %d bars, expected %d\n", b[i].mv, b[i].ma, got, b[i].bars);
    }
    /* the ends and the charger */
    n++; ok += battery_curve_pct(3000) == 0;
    n++; ok += battery_curve_pct(4300) == 100;
    n++; ok += battery_rest_mv(4119, 1782) < 4119 - 200;        /* charging: the voltage is brought down */
    n++; ok += battery_rest_mv(3700, -700) == 3788;             /* discharging: it is brought up */
    printf("battery_curve: %d/%d checks passed\n", ok, n);
    return ok == n ? 0 : 1;
}
