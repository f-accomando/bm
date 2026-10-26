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

## Work items (in order)

1. Run on the console: check the sign and scale of mA (plug the charger: mA must turn positive).
2. Add the backlight level and the state name (menu, game) to the log lines.
3. Run the measurement table. Fix the biggest consumer first.
4. Replace the percent table by a curve from the run-down test. Optionally add a load correction from the current.
5. Candidate savings, each measured alone: `wfi` in the idle path, lower CPU clock in the menu, GPU clock gate when idle, dim and screen-off timeout, radio off when unused.

## Open questions for the user

- Does the 30-minute test run a game, the menu, or WiFi? It sets the first hypothesis to check.
- Is a screen-off timeout wanted as a setting?
