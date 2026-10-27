# RGB30 battery: calibration and power use

Status: plan, no measurement yet (2026-10-26). The console was in use by another session.

## Observation (user)

- 30 minutes of a short test used about half of the battery. Estimate: 1 h of run time.
- Battery: Li-Po 4100 mAh. Published run time: 8 h claimed, 5 to 7 h real (Linux firmware).
- The test was not controlled. Treat the numbers as a reason to measure.

## What the code does today

| Part | State | File |
|---|---|---|
| Percent | Voltage only, from a fixed table (3450 mV = 0%, 4180 mV = 100%). The table is an open-circuit curve. Under load the voltage sags, so the percent reads low. | `battery_percent()` in `src/rgb30/plat.h` |
| Voltage | RK817 ADC, two-point calibration at first read (registers 0x93, 0x95 against 2300 and 4025 mV). Read every 10 s. | `rk817_battery_mv()` in `src/rgb30/rk_pmic.c` |
| Coulomb counter | Not used. The RK817 fuel gauge (current, charge counter) is not read. | - |
| CPU clock | Not changed by bm. No DVFS. Run frequency is the one the loader leaves. To check. | - |
| Idle | The main loop has no `wfi` (grep of `src/rgb30`). The secondary cores park with `wfe`. To check on the main loop. | `src/rgb30/smp.c` |
| Backlight | PWM4, a percent value. Default level to check. | `rk_display.c:231` |
| Radios | WiFi and BT modules are on only when used. To check. | `rk_wlbt.c` |

## Likely causes of the fast drain (hypotheses, not facts)

1. The percent reads low under load (voltage sag, no current compensation). The real charge may be higher than shown.
2. The CPU runs at a high fixed clock with no idle state while the menu waits for input.
3. The GPU (Mali) and its clocks stay on when no game draws.
4. The backlight is bright by default.
5. WiFi or BT stay powered after use.

## Measurement protocol

Goal: separate "the gauge is wrong" from "the console uses much power".

1. **Current**: read the RK817 current ADC (battery current, registers in the RK817 datasheet and the Linux driver `rk817_charger.c`) and the coulomb counter. Show them in *Settings > System > Battery* and in `bm/lastrun.txt`.
2. **State table**: log current in each state, 5 minutes each, backlight at a fixed level, charger out:
   - menu idle, backlight 50%
   - menu idle, backlight off (screen dark)
   - 2D game
   - 3D game (Mali)
   - WiFi connected, idle
   - BT pad connected
3. **Run-down curve**: charge to full, run a fixed load (menu idle, backlight 50%), log mV and coulombs every 60 s to the SD until power-off. This gives a real voltage-to-charge curve for this cell under load.
4. **Compare**: mAh used from the counter against 4100 mAh. Compare average current against the Linux figure (about 600 to 800 mA for 5 to 7 h).

The console reports numbers to the `reports` branch without a serial line. Use the same path.

## Done (2026-10-26, built, not yet run on the console)

- `rk817_battery_ma()` and `rk817_charge_uah()` in `src/rgb30/rk_pmic.c` (registers 0x7a/0x7b current, 0x74 to 0x77 charge counter; scale 172 uA per count, from the public Linux driver). `RK817_SENSE_DIV` is 1; it must be 2 if the board has a 20 mOhm sense resistor. Check it on the console: the current must be near 0 mA at idle with the screen off, and the counter after a full run-down must be near 4100 mAh.
- Battery log: `battery_log=1` in `bm/config.txt` writes `bm/bat00.csv` (first free name) once a minute: `sec,mV,mA,mAh,plug,charge`. Code in `src/rgb30/battery.c`. The log has no backlight column yet.
- The current offset calibration of the PMIC is not done (read only). A small constant offset in mA at idle is expected.

## First run (2026-10-26)

Kernel built on the merged `bm-core` (bridge gate commit) went to the console through the bridge, with `battery_log=1`. The charger came out at 83%, 4.04 V. The log is on the SD (`bm/bat00.csv` or the next free name). The bridge cannot read SD files, so the file is read from the card.

## First data (2026-10-26, from `logs batNN` through the bridge)

- Sign and scale: off the charger about -660 mA, and the counter falls about 11 mAh a minute (660 mA x 1 min = 11 mAh), so `RK817_SENSE_DIV = 1` is right. On the charger the current is positive (+830 mA first, falling to +510 mA at 4.21 V) and the counter rises.
- The load of the first discharge segment (bat02, 6 minutes) is not known: the menu or a game. -660 mA is a lot for the menu and fits the 1 h run time the user saw (4100 mAh / 660 mA = 6.2 h in theory; the user saw 1 h for 30 min, so a game or WiFi drew more).
- The counter has no absolute meaning (about 103800 mAh at start); use differences.
- The log starts a new file at each game start (`play`); read the newest `batNN`.
- Overbit title screen, started with `play` through the bridge, is the planned constant load; the console stayed on the charger, so the first run-down is still to do.

## Analysis of 2026-10-27 (bat08, console on the battery, backlight and settings as the user left them)

- **Current**: charger on, fast charge +1855 mA at 4.07 V; the counter rises 31 mAh a minute (1855 mA x 1 min), so the scale of the counter is right at both ends. On the battery, the menu draws about -735 mA and Overbit's title screen about -745 mA over 5 minutes: the load does not depend on what is on the screen. The base load (backlight, four cores, display, radios) is what costs.
- **Run time**: 4100 mAh / 740 mA = about 5.5 h, inside the 5 to 7 h of the Linux firmware. The console does not use too much power. The percent shown is what misleads.
- **Voltage sag**: the cable pulled out at 4083 mV (+1855 mA) and the voltage fell at once to 3761 mV (-709 mA). The step is 322 mV for 2.56 A, so the internal resistance is about 0.126 ohm (cell, protection, wiring). On the battery at 740 mA the voltage is 0.09 V under the rest voltage. `battery_percent()` reads a table of rest voltages: it shows too little charge under load. Example: at 3698 mV on load the bar says about 12%, the voltage plus 0.126 ohm x 0.74 A (about 3790 mV at rest) says about 36%, and the counter (full about 104175, now 102867: 1308 mAh used of 4100) says about 68%. The three do not agree yet: the full point of the counter is not proven.
- **User's 30 min test**: 30 min at 740 mA is about 370 mAh, 9% of 4100 mAh. The drop to "half" in 30 min is mostly the sag and the table, not the use.
- **Open**: `bat07` has 57 lines but only about 4 minutes passed after the kernel was sent. The cause is not known (an earlier boot may have written it). `bat08` is consistent: the lines are 60 s apart and the counter agrees with the current.

## Data read by hand (2026-10-27, bat05 to bat08)

- **The counter stops.** In bat08 the `mAh` column falls to 102601 at 2520 s and stays there for 64 minutes while the current reads about -750 mA. Do not use the register for the charge left. Sum the current (mA x time) instead.
- **Full point.** The charge ends at about 214 mA (0.05C) at 4212 mV. The counter reads 104175 there. Use it as 100%.
- **Plugged, charge ended.** The current goes to about -118 mA and the voltage falls from 4176 to 4157 mV in 24 minutes with the cable in. The cause is not known.
- **Three estimates agree now** (cell of 4100 mAh, load resistance 0.126 ohm): at the start of bat08 the counter says 65% and the rest voltage (4071 - 0.126 x 1.855 = 3837 mV) says 55 to 65%. At the end of bat08 (97 min on the battery, about 1200 mAh summed) the sum says about 42% and the rest voltage is 3671 mV. `battery_percent()` says 9% at 3671 mV and 17% at 3576 mV: the table is low even after the load correction.
- **Decision.** Do not change `battery_percent()` before a full run-down: a table fitted on one partial run could show too much and the console would stop early. The log has all the columns needed; the backlight is fixed (no setting), so no backlight column.
- **Next run.** Charge to full, pull the cable, run the menu with `battery_log=1` until the console switches off. Sum the current to get the capacity and the voltage-to-charge curve.

## Full run-down (2026-10-27, bat09, menu only, backlight fixed)

The console ran from the full charge (4212 mV, charge ended at 241 mA, counter 105660) to the switch-off. The cable came out at 10380 s. The load is the menu: -633 mA at 4.09 V, -680 mA at 3.67 V, -707 mA at 3.45 V, -800 mA at 2.9 V (the current rises as the voltage falls: constant power).

- **Capacity.** From the cable out to the last log line (2876 mV, 30900 s): 5 h 42 min and about 3860 mAh. To 3300 mV under load (30060 s): 5 h 28 min and about 3680 mAh. The 4100 mAh of the label is 90 to 94% usable.
- **Counter.** It stops at 102601 (3059 mAh under the full point) at 26880 s and 3559 mV, as in bat08. The value is the same in both runs: a fixed floor of the register. Sum the current instead.
- **Switch-off.** There is no clean shut-down. The kernel only lights the LED at 3450 mV (28740 s). The console runs 36 more minutes, and the voltage falls from 3450 to 2876 mV, where the log ends. The knee: 3.35 V at 29940 s, 3.13 V at 30600 s, 2.88 V at 30900 s.
- **Rest voltage** (loaded mV + 0.126 ohm x A) against the charge left (100% = full, 0% = 3300 mV under load = 3700 mAh used):

| Used (mAh) | Left | Rest mV |
|---|---|---|
| 14 | 100% | 4172 |
| 332 | 91% | 4089 |
| 653 | 82% | 4014 |
| 977 | 74% | 3943 |
| 1307 | 65% | 3864 |
| 1642 | 56% | 3813 |
| 1980 | 46% | 3777 |
| 2320 | 37% | 3757 |
| 2661 | 28% | 3728 |
| 3005 | 19% | 3664 |
| 3346 | 10% | 3553 |
| 3545 | 4% | 3506 |
| 3665 | 1% | 3435 |

- **The old table** gives 58% at 3864 mV (real 65%), 31% at 3757 mV (real 37%), 7% at 3664 mV (real 19%), 3% at 3553 mV (real 10%). It is low over the whole range, and it is applied to the loaded voltage: with the menu the bar reads 20 to 30 points low (red at 54% real).
- **Charger.** The charger takes up to 1.85 A with the good supply. The old supply gave about 1.4 A. Charge ends at about 240 mA at 4215 mV (0.05C).

## Built (2026-10-27, not tried on the console)

- `src/rgb30/battery_curve.h`: rest voltage = voltage - mA x 0.126 (mA negative on the battery), and the table of the run-down. `battery_percent(mv, ma)` in `plat.h` uses it. The bars keep 75/50/25/10%.
- `battery.c`: the current is read with the voltage (every 10 s). The LED lights below 3500 mV at rest. Off the charger, 3 reads in a row below 3400 mV at rest and no write to the card: `plat_poweroff()`. If the gauge cannot be read the count stays at 0.
- Tests: `make test-battery-curve` (rows of bat09, 22 checks) and `tests/rgb30/qemu_test.py -k battery` (the QEMU values changed: 3880 mV is 66%, 3500 mV is red, 3650 mV is 17%).
- To check on the console: the bars at the same load, the LED, and the switch-off (a second run-down, with `battery_log=1`).

## Work items (in order)

1. Run on the console: check the sign and scale of mA (plug the charger: mA must turn positive).
2. Done as not needed: the backlight is fixed.
3. Run the measurement table. Fix the biggest consumer first.
4. Replace the percent table by a curve from the run-down test. Optionally add a load correction from the current.
5. Candidate savings, each measured alone: `wfi` in the idle path, lower CPU clock in the menu, GPU clock gate when idle, dim and screen-off timeout, radio off when unused.

## Open questions for the user

- Does the 30-minute test run a game, the menu, or WiFi? It sets the first hypothesis to check.
- Is a screen-off timeout wanted as a setting?
