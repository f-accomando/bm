# Platforms

The boards bm builds for and the PC/QEMU targets used to test it. Hardware detail:
[`docs/HARDWARE.md`](../HARDWARE.md); RGB30 setup: [`docs/RGB30.md`](../RGB30.md).

## How it is today

| Target | SoC / CPU | Image | Build | State |
|---|---|---|---|---|
| Pi Zero W, Pi 1 (A/B/A+/B+, CM1) | BCM2835, ARM1176 (ARMv6) | `kernel.img` | `make` | main target |
| Pi Zero 2 W | BCM2710A1, Cortex-A53 in 32-bit ARMv7 | `kernel7.img` | `make ZERO2=1` | off |
| PowKiddy RGB30 | RK3566, Cortex-A55 (AArch64) | `kernel8.img` | `make TARGET=rgb30` | second target |
| PC | host | `bmhost` | `make` host targets | tests, videos |
| QEMU | `-M raspi0` / RGB30 `virt` | — | `make test-qemu`, `make TARGET=rgb30 test` | CI |

### Pi Zero W / Pi 1 (`src/`, `linker.ld`)
- Board detection in `src/drivers/board.c` (revision code: Pi 1 A/B/A+/B+, CM1, Zero, Zero W,
  Zero 2 W; LED GPIO per board in `led.c`). ARM clock raised to the firmware's maximum
  (`prop_clock_set_max(CLOCK_ARM)`).
- Boot, memory, drivers: [system](system.md). Network kernel tag `bmK6` at offset 4
  (`src/boot/start.S`).

### Pi Zero 2 W (`-DBM_ZERO2`)
- Off in `make`, `test`, `install`, `image`, `release` and CI; the `BM_ZERO2` code stays
  and must compile. Not built nor tested unless `ZERO2=1` is asked.
- Differences handled: peripheral base (`src/drivers/mmio.h`), core 0 leaves HYP for SVC and
  the other cores stay in the firmware stub (`start.S`), LED GPIO 29, CYW43436 WiFi/BT.
  Network kernel tag `bmK7`.

### PowKiddy RGB30 (`src/rgb30/`, `rgb30.mk`)
- Toolchain `aarch64-linux-gnu-gcc` + picolibc. U-Boot (mainline) boots `kernel8.img`;
  updates read `manifest-rgb30` (`src/kernel/update.c`).
- Display: 720×720 DSI panel (ST7703) through VOP2 (`rk_display.c`, `rk_dsi.c`); the menu
  is the Pi's `menu_ui.c` at 360×360 shown ×2 (`ui.c`).
- Controls (`pad.c`, `rk_input.c`, SARADC sticks): B confirms, A goes back (`confirm=a`
  swaps them; code uses `pad_ok`/`pad_back`). Button icons of its own: `PROMPT_RGB30_*` in
  `src/kernel/prompts.c` (A green, B blue, X red, Y yellow; `"RGB30_A"`… from Lua on any
  console).
- Audio: I2S1 + RK817 codec ([audio](audio.md)). Volume keys `volume.c`.
- Battery (`battery.c`): plug bit polled every 250 ms (the bolt), voltage every 10 s and 2 s
  after a plug change. One state for the bar, the low-battery LED and the games:
  `battery()` / `battery_low()` in Lua (`nil`/`false` on the Pi). Low from 20% off the
  charger until 23%: a red icon drawn by the runtime over games and tools (`battery_draw()`
  in `runtime.c`, after `flush3d`, outside lockstep); `battery_icon=0` turns it off. QEMU:
  `test_battery_low_game` (`tests/rgb30/qemu_test.py`).
- WiFi RTL8821CS over SDIO (`rtw_*.c`, `wpa.c`), Bluetooth RTL8821CS on UART1 (`rk_bt.c`,
  H5).
- Logs on the SD for the run before: `bm/bootprev.txt` (its boot log + last 4 KiB printed),
  `bm/lastrun.txt` (whole log of a run ended through `plat_reset`/`plat_poweroff`),
  `display: ...` lines in `bm/bootlog.txt` around the panel start.
- Panel start (`rk_dsi_init`): reset low, supply on, 20 ms, reset released, 120 ms before
  the commands; no supply cut at boot, no power-mode readback. `rk_dsi_off(link_up)` runs
  before a restart.
- Market and Games show `.b16`; `.bm` are visible in Games for tests (`show_bm=0` hides them); the
  Market lists only `.b16` (`market_bm=1` adds the `.bm`). Games are sorted by title; tools from the Market are not games. Bar: only WiFi and battery.
- SDK b16 (`carts/sdk_b16/`): layout a due colonne affiancate su schermi larghi $\ge 480\text{ px}$ (720×720 via `SDK_B16_RES=720x720`, default 360×360); codice validato in CI/host, da provare sul dispositivo fisico RGB30 (R39).

### PC and QEMU
- `bmhost` (`tests/host/bmhost.c`): the console's runtime with kernel services stubbed;
  virtual clock (frame n at n/60 s, same run every time), `--shots`, `--video`, `--wav`,
  `--input` scripts, `--tool`, `--realtime`, `--clock-scale K` (≈21 to estimate the Pi).
  Variants: `bmhost-gpu` (V3D emulator `tests/gpu/v3d_emu.c`), `bmhost-ai`.
- QEMU: `tests/qemu_test.py` (`-M raspi0`, `--kernel7` for raspi2b),
  `tests/rgb30/qemu_test.py` (`virt,gic-version=3`, PLAT=virt build: PL011, ramfb, GICv3).
  No GPU in QEMU: 3D is software there.

## Open work

- RGB30 menu keys, user decision 19 Oct 2026 (target, not implemented): B = ok, Y = cancel,
  A is not a second back (games that rely on A as back are fixed one by one). Today the code
  still maps A to back (`pad_back`). Per-device key reference: branch `claude/ui-reference`
  (file ui-reference.md under docs/spec there, UI-REF-1, reviewed, not merged).
- **M41** (RGB30): Overbit in `.b16` at 360×360 e 720×720, benchmark ARM e driver Mali (dettagli in [graphics-mali](graphics-mali.md)).
- **M42**: profilo unificato `.b16` (dettagli in [b16](b16.md)).
- **R22 — Guscio Portatile per Pi Zero**: supporto per configurazioni handheld basate su Pi Zero (lettura tasti/pad direttamente su pin GPIO, display compatto su bus DPI o SPI).
- RGB30 runs on one core (no PSCI `CPU_ON`), fibers in the menu only; multicore: [multitasking](multitasking.md).
- RGB30 panel after a warm restart: confirmed OK on hardware on 21 Oct 2026. Watch for a regression (`src/rgb30/rk_dsi.c`):
  1. The black screen after a restart or an update may come back now that the boot power
     cycle is withdrawn (power off/on recovers it). If it does: cut and restore the supply
     **before** `dphy_power_on`, never with the PHY or the link up; a power-mode readback
     only as an opt-in log line, tried on the console first.
  2. Once, on the first start after kernels of the other build, horizontal lines shifted
     and repeated (menu stacked in copies), gone at the next start. Probably the panel state
     left by the previous shutdown. If it returns, note when and read the `display:` lines
     of `bm/bootlog.txt`.

## Rules (do not break)

- `src/rgb30/start.S` copy_up: the copy loop must stay within the first BOOT_HEAD (0x100) bytes
  (`ASSERT` in `src/rgb30/kernel.ld`); it copies the head to the link address and continues from there (an
  in-place forward copy overwrote itself above 1.5 MiB in QEMU virt).

- One menu (`src/kernel/menu_ui.c`) and one Settings (`src/kernel/settings.c`) for Pi and
  RGB30: a new row goes there for both.
- Confirm and back go through `pad_ok` / `pad_back`, never a fixed button (rule 5 in
  `docs/spec/CONSISTENCY.md`).
- A kernel from the network must match the board (`bmK6`/`bmK7` at offset 4).
- Numbers on the Pi or RGB30 come from the console's reports (`reports` branch,
  `reports/<branch>/…`): ask the user to run them.
