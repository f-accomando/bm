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
- Fibers (`src/kernel/fiber.c`, `src/arch/fiber.S`): cooperative tasks for the Market,
  WiFi retries, network writes; waits yield (`fiber_yield`, `net_wait_step`).

### Crashes and freezes (`exceptions.c`, `crumbs.c`, `src/drivers/watchdog.c`)
Exceptions show a red screen with registers and the call trail. Breadcrumbs and the last 4 KiB
printed live in `.noinit`, survive a reset and become a report at the next boot
(`reports_text`), sent once the network is up. Watchdog armed at 3 s.

### USB (`src/usb/`)
Own DWC2 host stack (`dwc2.c`, `usb.c`). HID (`hid.c`): boot keyboards (IT/US layout),
generic HID gamepads (descriptor parsed), Xbox 360 wired, mice. `smsc95xx.c`: the Pi 1
B/B+ Ethernet.

### Storage (`src/drivers/sd.c`, `src/fs/fat.c`)
SDHOST driver by default (`sdhost.c`), Arasan EMMC (`sd_emmc.c`) as fallback if SDHOST fails
(on the Zero W the Arasan is the WiFi's). FAT16/FAT32: reads long names, writes whole files
and directories with 8.3 names.

### Bluetooth (`src/bt/`)
Classic (`bt.c`, `hci.c`, `btuart.c`): up to four HID pads (DualShock 4 the reference, light
set to the player colour) and a mouse; link keys in `bm/config.txt` (`bt_pad<n>`). LE
(`ble.c`, `smp_crypto.c`): one keyboard (passkey) and one mouse, keys `bt_kbd*`,
`bt_mouse*`.

### WiFi and network (`src/wifi/`, `src/net/`, `third_party/lwip`, `third_party/mbedtls`)
- BCM43430/CYW43436 over SDIO (`wifi.c`, `sdio.c`): firmware and NVRAM loaded from the SD;
  the chip's firmware does the WPA 4-way handshake (`sup_wpa`).
- Auto-join (`wifi_auto.c`, Pi and RGB30): two tries at boot, then from the menu in a fiber
  after 5 s, 15 s, 30 s, 1 min and every 2 min while down; `wifi_boot=0` turns it off.
  `wifi_leave()` before every reboot.
- lwIP with DHCP and SNTP (`net.c`), TLS with mbedTLS (`tls.c`), HTTP(S) (`http.c`): updates
  (`update.c`, `release.c`, signed releases), Market catalog (`catalog.c`), GitHub reports
  (`github.c`, `report.c`), 3D images (`img3d.c`), games' UDP and LAN (`lan.c`, `cartnet.c`).
- PC link (`netcon.c` console, `netxfer.c` files) for `tools/bm_net.py`: a file is answered
  `QD` once received and checked, written from the menu in a fiber
  (`netxfer_write_tick`); one at a time, another gets `BY`. An open or suspended game is
  replaced when closed. Tests: `tests/net/test_netcon.c`, `check_bm_net_config.py`.

### Menu and kernel services (`src/kernel/`)
- `carts.c` (the menu loop, launches, `background_stop()`), `menu_ui.c` (the one menu, Pi
  and RGB30), `settings.c`, `syskeys.c`, `splash.c` / `loading.c`, `market.c`, `update.c`,
  `reports.c`, `ledstate.c`, `notice.c`, `prompts.c`, `monitor.c`.
- Splash only for games: the suite (SDK, editors, Sound, Studio, Animator, Mesh, Pixel,
  projects) opens without it (`suite` in `carts.c`).
- Bar icons: controllers, keyboard and mouse without a player number; Bluetooth keeps a blue
  dot (`ICON_DOT`, `status_icons()` in `menu_ui.c`).
- Updates keep old kernels in `/bm/backup`; nothing is written before files are downloaded
  and checked; the board's own kernel last.

## Open work

- To confirm from a boot log on the device: whether a stale association after a reboot was
  part of the missing WiFi (the fix that is in: retries and `wifi_leave()`).
- No milestone in `docs/ROADMAP.md` is about this area today; spunti R… at its end.

## Rules (do not break)

- Fibers: `background_stop()` before an app, `rescan()` and a delete; check
  `net_wait_step` (it returns -1 when cancelled).
- Tabs on the Pi: Market · Games · Dev · Lib · Settings, Lib hidden until `lib_tab=1`; the
  menu opens on Games; the Market never blocks the menu and loads only when its tab is on.
- System keys only in `syskeys.c` (F12 held help, Esc menu, Ctrl+Esc = PS, F11 overlay,
  F6 assistant, F5/Ctrl+R try). Online games (`online(true)`): PS asks "Leave the match?" and
  calls `_leave()`. Automatic restart: always 3 s counted.
- Only a mouse moves the pointer (`mouse(true)` to ask, `mouse=off` turns it off).
- Network and `report()` ask permission the first time (`allow_…` in `bm/config.txt`).
- Keys and tokens only in the environment or `bm/config.txt`, never in files.
- Tests: `make test-usb`, `test-fat`, `test-net`, `test-https`, `test-http`, `test-lan`,
  `test-github`, `test-release`, `test-catalog`, `test-hyp`; QEMU `-k <name>`.
