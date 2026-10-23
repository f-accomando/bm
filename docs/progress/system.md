# System

Boot, memory, interrupts, fibers, crash handling, drivers (USB, SD/FAT, Bluetooth, WiFi),
the network stack and the kernel's menu and services on the Pi. RGB30 specifics:
[platforms](platforms.md). Hardware: [`docs/HARDWARE.md`](../HARDWARE.md).

## How it is today

### Boot (`src/boot/start.S`, `src/kernel/main.c`, `linker.ld`)
- Firmware loads `kernel.img` at 0x8000. `start.S`: core 0 only (others parked), HYP → SVC
  on the A53, stacks, `.bss` cleared (`.noinit` is not), the `bmK6`/`bmK7` tag at +4.
- `main.c`: UART, vectors, board (`src/drivers/board.c`), ARM clock to max, memory split
  from the firmware (`arm_memory_end`, `sdram_end`), MMU and caches, VFP, timer, IRQs,
  watchdog (3 s), crash report of the boot before, Bluetooth if paired, `wifi_boot()`,
  `eth_boot()`, then `carts_menu()`; the monitor after it.
- `make` also builds `build/chainloader.img` (`chainloader/`).

### Memory (`src/arch/mmu.c`, `cache.c`)
1 MB sections: the ARM's RAM cached (WB/WA), the GPU's share mapped uncached (normal), the
peripherals as device, the rest unmapped. `mmu_set_cached` switches ranges (GPU job memory,
`gpu3d_wc`). Heap through newlib `_sbrk` (`src/lib/syscalls.c`).

### Interrupts, time, fibers
- `src/kernel/irq.c` (BCM2835 controller, 64 sources), `tick.c` (system timer channel 1),
  `src/drivers/timer.c` (1 MHz counter).
- Fibers, shares, the `menu idle:` log: [multitasking](multitasking.md).

### Crashes and freezes (`exceptions.c`, `crumbs.c`, `src/drivers/watchdog.c`)
Exceptions show a red screen with registers and the call trail. Breadcrumbs and the last 4 KiB
printed live in `.noinit`, survive a reset and become a report at the next boot
(`reports_text`), sent once the network is up. Watchdog: armed at 3 s (the Pi Zero W boot log says `watchdog: on, 3000 ms`).
`exception_handler()` sets the crash flag before all other work, so a restart from a hang in the handler is reported as a crash.

### USB (`src/usb/`)
Own DWC2 host stack (`dwc2.c`, `usb.c`). HID (`hid.c`): boot keyboards (IT/US layout),
generic HID gamepads (descriptor parsed), Xbox 360 wired, mice. `smsc95xx.c`: the Pi 1
B/B+ Ethernet.

### Storage (`src/drivers/sd.c`, `src/fs/fat.c`)
- SDHOST by default (`sdhost.c`), Arasan EMMC (`sd_emmc.c`, the Zero W's WiFi controller)
  if SDHOST fails a transfer twice; not on a failed multi-block write. `sd_counters()`
  (retries, waits run out) and `fat_cluster_bytes()` in the boot `sd:` line.
- FAT16/FAT32: long names read, whole files and 8.3 directories written. `store()`: the
  clusters marked as a chain first (`alloc_chain`), the data in runs of up to 32 KiB read
  back (`fat_write_runs`, on; off for good on a mismatch), the last cluster only where it
  has data, then with no tick the entry found again (`locate`) and changed in one sector;
  the old chain released last. FAT entries change a sector per copy at a time
  (`fat_set_list`); the free-space search starts at FSInfo's hint.
- `fat_write_tick` (a write in a fiber yields): [multitasking](multitasking.md). `fat_writing()`.

### Bluetooth, WiFi and network
Details (`src/bt/`, `src/wifi/`, `src/net/`, lwIP, mbedTLS): [system-net](system-net.md).

### Menu and kernel services (`src/kernel/`)
- `carts.c` (the menu loop, launches, `background_stop()`), `menu_ui.c` (the one menu, Pi
  and RGB30), `settings.c`, `syskeys.c`, `splash.c` / `loading.c`, `market.c`, `update.c`,
  `reports.c`, `ledstate.c`, `notice.c`, `prompts.c`, `monitor.c`.
- Splash only for games: the suite (SDK, editors, Sound, Studio, Animator, Mesh, Pixel,
  projects) opens without it (`suite` in `carts.c`).
- Bar icons: controllers, keyboard and mouse without a player number; Bluetooth keeps a blue
  dot (`ICON_DOT`, `status_icons()` in `menu_ui.c`).
- Market job (`market.c`): `fiber_slice()` between the steps of the catalog (check, parse,
  adopt, the copy on the card); a game's SHA-256 in 64 KiB pieces (`catalog_hash_tick`);
  a game checked and being written (`finishing`) goes on off the tab, nothing else does.
  Separates **Games** and **Apps** pages (`market_page`, toggled with `Y` / `V` or clicking the
  subtabs on `TITLE_ROW`), tracks `.bm` and `.b16` editions independently, and displays `.bm` /
  `.b16` format badges on cards, title bar and details panel.
- Updates keep old kernels in `/bm/backup`; nothing is written before files are downloaded
  and checked; the board's own kernel last. Check and install run in a fiber of the menu
  (`update_start`, `update_tick`; the Updates panel lists the files, `update_notice` the
  install's box and its 3 s); publishing too (`publish_tick`, the Send row and its help).
  The monitor's `u` runs `update_check`/`update_install` straight through.

## Open work

- **C4:** the RAM ring stops before the crash (BOOTPREV ends at the netcon prompt). BETA1-C4-RING is merged (PR 50): the crash record holds `ring_at` and the sink state, and a nested exception leaves the crumb `nested exception`. BETA1-C4-RING3: the timer IRQ is on before `crumbs_boot()`, and `flush()` then wrote the new boot's state over the old freeze record (the header said `ring 3601, sink off`). The writers of the record now do nothing until `crumbs_boot()` has copied it (`cr_live`), and `flush()` stores the live ring state until the first crash call (`crash_noted`). `make test-crumbs` covers this on the PC. The item stays open until a Pi run shows a crash header with `ring N, sink on` (N above 0) and handler lines. A real crash (Xu) that still showed `sink off` is not explained yet. Details: [beta1-c4-diag.md](beta1-c4-diag.md).
- After a long session on the Pi, copy `bm/` from the card and run `python scripts/sdlog_scan.py <dir> --secret <net_password>`. Verdict OK means no freeze, no crash, and the password is masked.
- Measurements to redo, stalls that still hold the CPU, downloads inside an app: [multitasking](multitasking.md).
- Names the system writes in lowercase (owner decision, 2026-10-22): not verified on real cards in Windows, Linux and the console. Writers not changed: none known. Names that stay in capitals on purpose: the key `allow_XXXXXXXX` in `config.txt`; the `built from` field inside a `.bm` (now lowercase too, compared in any case); the volume label. Files the PC tools name for the card are lowercase (`easy_install.sh`, `bm_net.py`, `pi_bridge.py`).
- Try the SD map (Settings > System) on the Pi Zero W (speed of the scan, stutter of the menu) and on the RGB30: not verified on hardware.

### Standalone console and hardware ideas
- **WiFi from the menu with the pad**: Settings > WiFi and network with network selection by gamepad. The user enters credentials with the on-screen keyboard.
- **Kernel on-screen keyboard**: Turn `padtype` into a native C kernel service. Use it for WiFi setup, file renaming and the Market. Expose it to games through the Lua API (`textinput(title, default)`).
- **Lua traceback on the SD card**: Save the traceback of the last Lua error of a cartridge on the SD card (boot log, bootprev and lastrun are done).
- **Screenshot**: done in the games' runtime (`shot()`, Print Screen, Select held 3 s, `bm/shots/`: [`SCREENSHOTS.md`](../SCREENSHOTS.md)). To do: the menu's own screens, a try on the Pi and the RGB30 (3D `later` mode), the key's picture in the F12 list.
- **Local web interface (`bm.local`)**: Built-in HTTP server and mDNS responder (`bm.local`). It lets you upload `.bm` files, download screenshots and saves, and edit `bm/config.txt` from the browser of a phone or PC on the local network.
- **Rumble and LED on the DualShock 4**: Expose the APIs `rumble(p, strong, weak, ms)` and `padlight(p, color)`. Use the output report 0x11 that `bt.c` already implements.
- **Extended Bluetooth controller support**: Add profiles for DualSense, Switch Pro Controller, 8BitDo and Xbox gamepads (Bluetooth LE through `ble.c`).
- **HDMI-CEC remote control**: Use the direction arrows and the Select/OK key of the TV remote to move in the console menus without a controller.
- **Network and bmnet multiplayer**: Test two Pi consoles on the *Online 2D* model (also through a relay). Port the Overbit network protocol to the unified `bmnet` library.

## Rules (do not break)

- Settings shows a graphics choice only if a player can judge it by sight. Put every performance flag in `config.txt` as a hidden key, with no Settings row (SYS-9; table: [v3d-flags](v3d-flags.md)).
- All SD logs only via `src/kernel/sdlog.c`.
- A cartridge writes files only through `file_write` into `bm/web` (a bare 8.3 name, never `.bm`, `.bme` or `.b16`), and fetches only `https://` addresses through `web_get`: the rules live in `src/bm/webpolicy.c`, nowhere else.
- Planned restarts only via `crumbs_restart()`. SD logs masked with `reports_mask_secrets()`.
- The SD map scan never runs while a game or a tool runs (`sdmap_stop` in `background_stop` on the Pi, in `run_page` on the RGB30).
- The bar's categories are fixed: kernel, bm, bme, other, free (other = total - free - the rest).
- Free space is counted from the FAT (`fat_free_step`), not from FSInfo.
- Fibers (`background_stop()`, `net_wait_step`, update/publish vs apps): [multitasking](multitasking.md).
- Tabs on the Pi: Market · Games · Dev · Lib · Settings, Lib hidden until `lib_tab=1`; the
  menu opens on Games; the Market never blocks the menu and loads only when its tab is on.
- System keys only in `syskeys.c` (F12 held help, Esc menu, Ctrl+Esc = PS, F11 overlay,
  F6 assistant, F5/Ctrl+R try). Online games (`online(true)`): PS asks "Leave the match?" and
  calls `_leave()`. Automatic restart: always 3 s counted.
- Only a mouse moves the pointer (`mouse(true)` to ask, `mouse=off` turns it off).
- Network and `report()` ask permission the first time (`allow_…` in `bm/config.txt`).
- FAT writes may meet at a tick (a fiber's write and the menu's `config_save`): a picked
  cluster is marked before any tick, and the directory is read and changed with no tick
  in between (`store()` in `fat.c`). Keep both.
- Transfer port read ops (`netxfer.c`): `G` serves only files of `bm/shots` (one level, no `..`) and the three logs `bm/bootprev.txt|bootlog.txt|lastrun.txt` (exact names, matched in any case: cards of before have capitals);
  `T` needs a game drawing frames and gives up after 3 s (`NG`). Details in [system-net](system-net.md).
- Keys and tokens only in the environment or `bm/config.txt`, never in files.
- The system writes lowercase names and extensions, and reads in any case (owner decision, 2026-10-22). The FAT driver stores a new name as the 8.3 entry in capitals with the NT flags of byte 12 (`0x08` base, `0x10` extension), whatever case the caller gives (`to_83()` in `fat.c`): Windows, Linux and the console show it in lowercase. Every writer also builds its names in lowercase (`rptNNNNN.txt`, `3dNNNN.txt`, `bootlog.txt`, `config.txt`, `pongNNNN.png`, `rNNNNNNN.bmv`, `xxxxxxxx.sav`, `.prt`, `/bme/name.bme`, `.bm`, `.b16`). A name typed by the user is lowercased too. Lookups, `fat_replace` and `fat_delete` ignore case: an old file keeps its name and is replaced in place. Nothing renames or deletes a file of the user. Harm if a writer builds a capital name: the card shows two styles side by side. Exception: the key `allow_XXXXXXXX` in `bm/config.txt` keeps its capitals (cards of before).
- Call `crumbs_crashed()` as the first statement of `exception_handler()` (`src/kernel/exceptions.c`). Harm if moved: a hang before `watchdog_stop()` is reported as a freeze (BETA-1 C4).
- Clear SCTLR.A as the first step of `exception_handler`. Keep the re-entry guard: a nested exception does not print; it restarts (uptime 20 s or more) or blinks.
- Tests: `make test-usb`, `test-fat`, `test-net`, `test-https`, `test-http`, `test-lan`,
  `test-github`, `test-release`, `test-catalog`, `test-hyp`; QEMU `-k <name>`.
