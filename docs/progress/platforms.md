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
- Boot, memory, drivers: [system](system.md). Network kernel tag `bmK6` at offset 4 (`src/boot/start.S`).
- Pi 1 GPIO SPI TFT (`src/drivers/spilcd.c`): 3.5" 480×320 ILI9486 on SPI0 (XPT2046 idle); standalone desk-robot mode (`spilcd_desk_robot`, multi-agent UDP 3337 via `tools/bm_robot.py`) in `Settings > Screen > GPIO screen` (`BOARD_PI1_A`..`BOARD_CM1`).

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
  The percent is the rest voltage (voltage plus 0.126 ohm times the current) against the
  table in `battery_curve.h`, from the run-down of 27 Oct 2026 ([calibration](../design/rgb30-battery-calibration.md)).
  Off the charger, 3 reads (30 s) at 0% switch the console off. PC test: `make test-battery-curve`.
- WiFi RTL8821CS over SDIO (`rtw_*.c`, `wpa.c`), Bluetooth RTL8821CS on UART1 (`rk_bt.c`,
  H5).
- Logs on the SD for the run before: `bm/bootprev.txt` (its boot log + last 4 KB printed),
  `bm/lastrun.txt` (whole log of a run ended through `plat_reset`/`plat_poweroff`),
  `display: ...` lines in `bm/bootlog.txt` around the panel start.
- Panel start (`rk_dsi_init`): reset low, supply on, 20 ms, reset released, 120 ms before
  the commands; no supply cut at boot, no power-mode readback. `rk_dsi_off(link_up)` runs
  before a restart.
- Games lists the `.bm` files; the
  Market has one entry per game id (a `.bm`). A cartridge made for the other device shows a faded cover (compat policy). Games are sorted by title; tools from the Market are not games. Bar: only WiFi and battery.
- SDK (RGB30) (`carts/sdk_b16/`): layout a due colonne affiancate su schermi larghi $\ge 480\text{ px}$ (720×720 via `SDK_B16_RES=720x720`, default 360×360); codice validato in CI/host, da provare sul dispositivo fisico RGB30 (R39).

### PC and QEMU
- `bmhost` (`tests/host/bmhost.c`): the console's runtime with kernel services stubbed;
  virtual clock (frame n at n/60 s, same run every time), `--shots`, `--video`, `--wav`,
  `--input` scripts, `--tool`, `--realtime`, `--clock-scale K` (≈21 to estimate the Pi).
  Variants: `bmhost-gpu` (V3D emulator `tests/gpu/v3d_emu.c`), `bmhost-ai`.
- QEMU: `tests/qemu_test.py` (`-M raspi0`, `--kernel7` for raspi2b),
  `tests/rgb30/qemu_test.py` (`virt,gic-version=3`, PLAT=virt build: PL011, ramfb, GICv3).
  No GPU in QEMU: 3D is software there.

## Open work

- Kernel consistency, what the gate still lists (25 Oct 2026, `python scripts/consistency.py gate`; plan: [kernel8-alignment](../design/kernel8-alignment.md), [CONSISTENCY](../spec/CONSISTENCY.md)):
  - Large or waiting for a device: D10 is done in the code (background covers in `src/kernel/library.c`, one job for both menus, QEMU test; needs a timing check on the console); D14 is dropped (user, 25 Oct: the DMA test is removed from the Pi; done); D15b (user, 25 Oct: PORT; RK3566 TSADC and clock readback, device-tested; the 3D driver row, D15a, is done); D17 (waits for the on-screen keyboard, R1 and R2). D24 (Overbit) left the divergences: it is in the game backlog. D23 is closed as merged into D45 (Mali driver, untested on a console).
  - Done 25 Oct 2026, QEMU and host only (try a Bluetooth pad and a Bluetooth mouse on the RGB30): D03 (the Pi player slots: built-in pad = P1, BT pads P2-4) and D05 (the pointer block, one function for both menus). Decided 25 Oct 2026: D11: Publish stays off on the RGB30 (owner decision). D22 and D48: the RGB30 monitor comes after v1.0.0. Utility cartridges stay in the Dev tab on both kernels. Both kernels no longer scan `/carts` (done 25 Oct 2026: games are in `/bm`); `/glb` and `/bmm` are APP-9's folders. Accepted in BETA-6 round 2 (23 Oct 2026): D49 (hardware difference `pad-only`: the Pi menus keep A and B, D32; the owner can ask for `mo` and `mc` on the Pi); D25 (hardware `multicore`); D54 and D55 (kept: `no-keyboard`, `square-panel`) and D48 (the crc is a monitor line, the RGB30 has no monitor, D22).
  - Written, not tried on a console: D01, D02, D18, D26, D10, D51, D52, D53. compat-policy is `implemented` (25 Oct 2026). cart-type is `implemented` (25 Oct 2026: D51, D52, D53 done, QEMU only). Specification item still partial: `/bme` folder. Resource files and project layout are `implemented` (round 2: every `carts/` project has `bm.info`).
- RGB30 menu keys, user decision 19 Oct 2026 (done, `src/rgb30/pad.c:11-37`): B confirms and Y goes back by default. A is not a second back. Settings > Controls (`mo`, `mc`) remaps them. Per-device key reference: branch `claude/ui-reference` (file ui-reference.md under docs/spec there, UI-REF-1, reviewed, not merged).
- RGB30 game options (X): the panel is the Pi's (`lib_panel` in `src/kernel/library.c`), with the rows the console has in its backend (`LIB_CAP_SDK | LIB_CAP_SEND`): Play or Resume, Close, Open in the SDK, Send to a nearby console, the info rows (Author, File, Size, Type, Save data) and the deletes. "Send to a nearby console" works (same code as the Pi; not verified on hardware). Publish stays off by decision; the editors and the editable copy are not offered (no keyboard; divergence D11 accepted). The menu code is shared with the Pi since 25 Oct 2026 (`lib_*`: scan, covers, frozen game, panel, row actions, play, menu idle); not tried on a console.
- RGB30 battery: the new curve and the clean switch-off are built and pass the PC and QEMU tests, not tried on the console. To check: the bars, the red at 10%, the switch-off at about 3.3 V in a second run-down. Then the power savings, one at a time ([calibration](../design/rgb30-battery-calibration.md)).
- **Mali-G52 driver** (RGB30): Overbit at 360×360 and 720×720, ARM benchmark and Mali driver (details in [graphics-mali](graphics-mali.md)).
- **Handheld shell for the Pi Zero**: support for handheld setups based on the Pi Zero (reading keys and pad directly on GPIO pins, compact display on a DPI or SPI bus).
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
- RGB30 planned restart, kernel `bm dev`, 2026-10-20 (18b71f8d, 6de90f0b): the console came back
  after 14 s (idle soak of more than 2.5 h, BETA1-A1) and after 17 s (bridge update). Both bootlogs show
  `display:` DSI link and panel on, dclk 36562 kHz, PHY locked, errors 0; BOOTPREV says the run ended
  cleanly. The screen was not seen in these two runs (the 21 Oct check above covers it).
- RGB30 network bridge, real console, 2026-10-20 (c6593655): `bm_net.py --kernel` sent a 1980016-byte
  `kernel8.img` at 237 KB/s (8.2 s), then a clean restart after a 3 s countdown. `line` is refused (the keys go
  to the buttons, no monitor). `--get bm/BOOTLOG.TXT`, `BOOTPREV.TXT`, `LASTRUN.TXT` work (583, 726, 2657 bytes).
  `--shot` answers `no game drew a frame` until a game runs. A kernel older than Op T/G refuses logs and shot.
- RGB30 QEMU tests (22 Oct). The menu reads a serial key as held for `PAD_SERIAL_MS` (120 ms, `src/rgb30/pad.c`): a
  frame longer than that, as under load, loses the key. Use `press`, `press_serial` and `wait_screen` in
  `tests/rgb30/qemu_test.py`, no blind keys or fixed sleeps. Fixed so: `test_update_from_sd`, `test_gpu_test` (removed on 24 Oct 2026),
  `test_menu_input_page` (sends X again until a screen shows `held: 00000040`), `test_sdk_template_build`,
  `test_wifi_page_without_chip`: 5 of 5 alone, 3 of 3 with 14 busy loops (load 16 on 8 cores). Still blind:
  `test_bluetooth_page_without_chip`, `test_games_in_bm_only`, `test_menu_games_bm_and_b16`, `test_screen_console`.
  A full `/tmp` (tmpfs) in WSL fails any test that writes an SD image: delete `/tmp/bm64*` or set `TMPDIR`.

## Rules (do not break)

- `src/drivers/spilcd.c` owns GPIO 7-11, 24 and 25 only on a Pi 1 after `spilcd_init` (`Settings > Screen > GPIO screen`), never at boot or on a Pi Zero.
- `src/rgb30/start.S` copy_up: the copy loop must stay within the first BOOT_HEAD (0x100) bytes
  (`ASSERT` in `src/rgb30/kernel.ld`); it copies the head to the link address and continues from there.
- One menu (`src/kernel/menu_ui.c`) and one Settings (`src/kernel/settings.c`) for Pi and
  RGB30: a new row goes there for both.
- Confirm and back go through `pad_ok` / `pad_back`, never a fixed button (rule 5 in
  `docs/spec/CONSISTENCY.md`).
- A kernel from the network must match the board (`bmK6`/`bmK7` at offset 4).
- Numbers on the Pi or RGB30 come from the console's reports (`reports` branch,
  `reports/<branch>/…`): ask the user to run them.
