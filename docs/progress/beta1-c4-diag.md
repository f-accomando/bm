# BETA-1 C4: why `keys crash-dabt` ends as a watchdog restart on the Pi

Diagnosis of BETA1-C4-DIAG (2026-10-20, operator CI). Read only: no change in `src/`. Kernel: `bm-core` d80c0f6b. Evidence: delivery line `BETA1-N1` in `sync/NET.md`. Verdict: **(c) other, and BLOCKED**: the source does not explain the result, and one hardware run is needed.

## Answers

1. **The key table is right.** `tools/pi_bridge.py:20` maps `crash-dabt` to `Xa`. `tools/bm_net.py` sends `X`, waits 0.2 s (`KEYS_PAUSE`), then sends `a`. In the monitor, `X` asks for a second key (`src/kernel/monitor.c:445`): `a` calls `trigger_dabt()` (line 451), which is a data abort. The freeze command is `f` (`monitor.c:454`), not `X`. So `X` alone is not the freeze, but the crumb "monitor command X" is the same for `Xa` and `Xf`.
2. **Real data abort.** `trigger_dabt()` (`monitor.c:107`) sets the A bit (alignment check) in SCTLR, then reads the word at `0x8001`. `tests/qemu_test.py:5242` (`test_crash_report`) does the same: `q` (monitor), wait 21 s, `X`, wait for "other keys cancel", `a`, then wait for "Restarting in 60 s". It passes in QEMU.
3. **Who writes the report.** `src/boot/start.S` installs the vectors; `src/kernel/vectors.S` (`exc_dabt`) calls `exception_handler` (`src/kernel/exceptions.c:112`). It runs `panic_screen()` (stops the watchdog, line 50), prints "*** EXCEPTION", then `die()` (line 87). `die()` calls `crumbs_crashed()` (sets `cr.crashed`, flushes, `crumbs.c:190`). Then, if uptime is 20 s or more, it waits **60 s** and calls `watchdog_reboot()`. At the next boot `crumbs_boot()` (`crumbs.c:60`) prints "the last run ended in a crash: saved as a report" when `cr.crashed` is set. The report exists only after these 60 s. Before 20 s of uptime there is no restart at all: the LED blinks for ever.
4. **Verdict.** The N1 result ("restart", kind freeze: no EXCEPTION, no `crashed` flag) means the exception handler did not reach `watchdog_stop()`. If it had, the watchdog would be off and the restart would come only after 60 s, with the crash flag set. Two readings fit and the source cannot separate them:
   - **A.** The abort is not taken or the handler hangs before line 50, with the watchdog still armed (3000 ms, pet by the 10 Hz tick). Then the kernel has a bug on the Pi that QEMU does not show (QEMU may not run the watchdog; not checked).
   - **B.** The abort works, and N1 read the Pi before the 60 s ended (or power-cycled it). RAM is kept only over a watchdog reset, so a power cycle loses the crumbs. Then there is no bug.

## Smallest fix (text only, not applied)

- If B: `docs/design/pi-test.md` (line about `keys NAME`) and the N1 procedure say: after `keys crash-dabt`, wait at least 75 s and do not power-cycle; the uptime must be 20 s or more.
- If A: show the failing step first (see the run). Likely place: the first lines of `exception_handler` before `watchdog_stop()`; a test would be a Pi run that checks BOOTPREV for "ended in a crash" 75 s after `keys crash-dabt`. No QEMU test can show it, if QEMU does not run the watchdog (not checked).

## Hardware run needed (exact)

Same kernel, with a serial cable or HDMI in view:
1. `pi.py status`, `keys monitor`, wait 25 s.
2. `keys crash-dabt`. Keep the 3 s answer of `--keys`: it must contain "crash test: u undefined insn, a data abort". Look at the HDMI screen: red with "*** EXCEPTION: Data abort" means the abort works (reading B).
3. Do not touch the Pi for 75 s. It must restart by itself.
4. `logs bootprev` and `logs bootlog`: look for "the last run ended in a crash". A report `RPT*.TXT` must be on the SD.
5. Control: `keys monitor`, wait 25 s, `X` then `f` by `bm_net.py --keys Xf`. It must restart after 3 s with "the Pi froze". This is the result N1 saw for `Xa`.
Result 2 with a red screen but no restart means B; no red screen and a restart in 3 s means A.

## Run

2026-10-20, Pi Zero W (pizero), kernel bm-core d80c0f6b (dev), operator NET. Times UTC, the host clock; status polled every ~5-7 s (each failed poll takes a few s).

| run | key | T0 | first status fail | first status back | down time | "the last run ended in a crash" (BOOTPREV / BOOTLOG) | "saved as a report" (BOOTPREV / BOOTLOG) |
|---|---|---|---|---|---|---|---|
| 1 | `keys crash-dabt` (X a) | 10:55:56 | 10:56:01 (first poll, T0+5 s) | 10:57:17 | about 76 s from T0 | 0 / 0 | 0 / 0 |
| 2 control | freeze (Xf) | - | - | - | - | not run | not run |

BOOTLOG after the restart: "watchdog: on, 3000 ms" and "the Pi froze last time and the watchdog restarted it" (monitor command X). Result: the restart came after about 76 s (not under 10 s) but the crash count is 0 (B needs >= 1): neither A nor B by the rule of the work order. Control not run: the freeze key was denied by the permission classifier (the bridge refuses it on bm-core). Not verified on hardware: the control, and any run on another kernel.

### Boot time

2026-10-20, pizero, kernel bm-core d80c0f6b, operator NET. Run 2 of `keys crash-dabt` (X a) with the bridge `--dev pizero`. Times UTC, the host clock; status polled every ~7 s. Run 1 logs were not saved, so this run repeats N1b and reads the logs first.

| T0 (crash key) | first status fail | first status back | down time | "crash test: u undefined insn, a data abort" (BOOTLOG / BOOTPREV) | "EXCEPTION" (BOOTLOG / BOOTPREV) |
|---|---|---|---|---|---|
| 13:24:15 | 13:24:20 (T0+5 s) | 13:25:36 | about 81 s from T0 (76 s from first fail) | 0 / 0 | 0 / 0 |

BOOTLOG (2947 bytes) is the log of the boot after the restart: wifi joined, "cartridge menu". BOOTPREV answered "kernel too old (it does not know T/G)" (39 bytes), so it holds no record of the run before.

Rule of the work order: boot about 70 s and no "EXCEPTION" is A (the restart came from the watchdog). Measured: about 76-81 s, "EXCEPTION" 0. This matches A by the rule. The same result as run 1 (about 76 s). Not verified: the freeze control (Xf, denied), and any other kernel. No code diagnosis made.

### Fix2

2026-10-20, pizero, kernel from `claude/beta1-c4-fix2@501ae6ba` (version string "bm dev"), operator NET. Times UTC, host clock; status polled every ~7 s. Precondition: BOOTLOG before the crash has "watchdog: on, 3000 ms" (count 1).

| T0 | first status fail | first back | back after first fail |
|---|---|---|---|
| 14:59:44 | 14:59:51 | 15:01:09 | 78 s |

| file | text | count |
|---|---|---|
| BOOTPREV | C "the run before ended in a crash" | 1 |
| BOOTPREV | E "*** EXCEPTION: Data abort" | 0 |
| BOOTPREV | R "Restarting in 60 s" | 0 |
| BOOTPREV | R50 "restart in 50 s" | 0 |
| BOOTPREV | R10 "restart in 10 s" | 0 |
| BOOTLOG | S "ended in a crash: saved as a report" | 1 |

Outcome: BLOCKED-HANDLER (E = 0). BOOTPREV says "the run before ended in a crash while: monitor command X, 52 s after boot", so the crash flag is set, but the RAM log holds no exception text: the printed lines stop at "cartridge menu" and the menu idle line, then `net: console opened from 192.168.1.115`. After "--- the run before" the file has 68 lines, 3 of them empty. Last 40 lines: wifi start-up, the scan, "net: IP 192.168.1.108", "menu idle: 297/300 frames", "net: console opened", ">" (no handler output). The restart came 78 s after the first fail, as before. Not diagnosed.
