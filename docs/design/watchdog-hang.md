# Watchdog and the freezes of 22-23 October

The Pi Zero W froze twice and stayed frozen for hours: once in an all-profile 3D Bench run, once
(23 Oct, about 03:40) after a long loop of 3D Bench runs sent through the bridge. The watchdog was
armed at 3 s (`watchdog: on, 3000 ms`) and did not reset the board. This note comes from reading the
code. Nobody ran the Pi for it.

## Confirmed by the code

- The only caller of `watchdog_pet()` was `heartbeat()` in `src/kernel/main.c`. The 1 kHz timer IRQ
  calls it (10 Hz). So the watchdog proved only that IRQs run. A hang of the main code with IRQs on
  was never reset. This fits "frozen for hours": the screen and the netcon stop, the timer goes on.
- A hang with IRQs off, or inside the IRQ, is reset after 3 s. So the two freezes were hangs with
  IRQs on, or a stopped watchdog (`watchdog_stop()` is called only by the crash screen, which
  restarts after 60 s at uptime 20 s or more).

## Waits that can hang the main code (IRQs on)

1. `mbox_call()` (`src/drivers/mbox.c`) had no time limit, in two loops (mailbox full, no reply).
   The 3D path calls it: `prop_query` for the clocks at the start of each 3D Bench, `PROP_ENABLE_QPU`
   off and on after a QPU timeout (`v3d_run_qpu`), `fb_flip` (vsync tag). A firmware that does not
   answer (VPU busy or stopped after a QPU or V3D reset) hangs the call for ever. Likely cause.
2. `fiber.c`: a fiber stack overflow prints a line and spins in `for (;;)` with IRQs on. Likely only
   if a fiber overflows (not seen in a log).
3. Other waits without a limit that are polled with IRQs on (not read in full): the FAT and SD waits
   have counters (`sd_counters`), the network waits have limits in `stream.c`.

The V3D waits have a limit: `wait_count` (control list, `timeout_us`) and the QPU wait in `v3d.c`
(`timeout`); both snapshot and reset. So a GPU job that does not end does not hang the CPU by itself.
The hang risk is in the firmware call that follows the reset.

## Ranked

1. The mailbox call that never returns after a GPU error (item 1). Cannot be confirmed without a Pi.
2. Another unbounded loop in the main code with IRQs on. The new guard turns it into a restart and a
   freeze report with the crumb (`while: ...`).
3. A stack overflow or a loop in a fiber (item 2).

## The change (SYS-2)

- `watchdog_pet_irq()` replaces the plain pet in the timer hook. The main code calls
  `watchdog_progress()` in `crumb()`, `crumb_frame()`, `fb_flip()` and the input polls. After the first
  call, 20 s with no call stop the pets and the SoC resets 3 s later. Before the first call the IRQ
  pets (boot has no poll).
- `mbox_call()` returns 0 after 2 s.
- Test: `make test-crumbs` (the decision `watchdog_fresh()` with a wrapping counter).

## Audit of long blocks (false restart)

A block of the main code longer than 20 s with no mark would reset the Pi. The RGB30 is not at risk:
`src/rgb30/glue.c` has stubs (`watchdog_arm` returns -1, `watchdog_progress` does nothing).
Result, by reading the code (nothing ran on a Pi):

- Boot (SD init, FAT scan, USB, `wifi_boot`, TLS in the first reports): no mark exists before the
  cartridge menu, so the IRQ pets. Safe.
- The menu jobs run in fibers (covers, reports, Market, update, SD map, WiFi retry). A fiber yields to
  the menu loop, which polls. Safe where the fiber calls `fiber_slice`.
- Games: `crumb_frame()` each frame. A game load reads the SD with `fat_load_tick` (`sd_tick`). Safe.
- Gaps found and closed. Each mark is one timer read and one store.
  1. Settings > Connect (`x_connect`, `settings.c`) and monitor `wifi` run `wifi_start`, scan (8 s),
     join (15 s) and `net_wait_ip` (15 s) in the main code. Up to 40 s. `fiber_slice()` now marks, and
     those loops call it outside a fiber too. `net_wait_ip()` marks.
  2. `net_wait_step()` outside a fiber (Settings > Test the connection: DNS, connect, TLS, up to 1 MB)
     had no mark. It marks now.
  3. `file_write` of 8 MB in a game: `fat_write_tick` is `fiber_slice_tick`, which does nothing
     outside a fiber. `fiber_slice()` marks now, so each written run marks.
  4. A kernel written at once from netxfer (`save_now`, `keep_net`): marks per piece now.
  5. The WiFi firmware upload (`bp_mem`, `wifi.c`, 400 KB) marks per chunk.
- Not measured, bounded by the code only: the build in the editor (Ctrl+B, `project.c`) and the decode
  of a 16 MB game after it is read. If a false restart names one of them in `bm/bootprev.txt`, add
  `watchdog_progress()` there.
- Test: `make test-crumbs` checks that the marks stay in `fiber_slice`, `net_wait_step`, `keep_net`
  and `bp_mem`.

## Only a Pi run can show

- Which wait hung. After the next freeze, read `bm/bootprev.txt`: the `while:` line names the crumb and
  the frame. The ring holds the last lines.
- That a long legal block of the main code (more than 20 s with no flip, poll or crumb) does not exist.
  If a false restart appears, the report names the crumb: add `watchdog_progress()` to that loop.
- That the 20 s limit and a 3 s arm restart a spinning main code in about 23 s.
- QEMU has no working watchdog (`watchdog_arm` returns -2 or -1 there), so no emulator run checks the reset.
