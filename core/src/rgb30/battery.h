/* The RGB30's battery for the bar, the LED and the games (battery.c). */
#ifndef RGB30_BATTERY_H
#define RGB30_BATTERY_H

/* 1 and its charge (0..100), whether it is on the charger (the bolt) and
 * whether it is low (20% or less off the charger, until 23%); 0 when the
 * console cannot read it. Cheap: called every frame (the menu, the games:
 * bm_set_battery). The charger is looked at 4 times a second, the voltage
 * every 10 s and again 2 s after the charger came or went. */
int battery_state(int *pct, int *charging, int *low);

#endif
