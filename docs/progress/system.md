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
(`reports_text`), sent once the network is up. Watchdog: meant to be armed at 3 s, but the Pi Zero W boot log
says "not available, no freeze guard" (see Open work).

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

- To confirm from a boot log on the device: whether a stale association after a reboot was
  part of the missing WiFi (the fix that is in: retries and `wifi_leave()`).
- **Freeze reports hardware checks (Pi Zero W):** verify real watchdog countdown on hardware (FRZ-3: boot log says "watchdog: on", monitor `X f` restarts within ~3 s with report "the Pi froze last time"). After a long session, copy `bm/` from the card and run `python scripts/sdlog_scan.py <dir> --secret <net_password>` (verdict OK = no freeze, no crash, password masked).
- Measurements to redo, stalls that still hold the CPU, downloads inside an app: [multitasking](multitasking.md).

### Console Standalone & Hardware Spunti (R1, R2, R3, R4, R6, R8, R19, R20)
- **R1 — WiFi dal menu col pad**: Settings > WiFi and network con selezione reti via gamepad e inserimento credenziali tramite tastiera a schermo (R2).
- **R2 — Tastiera a schermo kernel**: Trasformazione di `padtype` in servizio nativo C per il kernel (utilizzabile per configurazione WiFi, rinomina file, Market) ed esposto ai giochi via API Lua (`textinput(titolo, default)`).
- **R3 — Traceback Lua su SD**: Traceback dell'ultimo errore Lua di una cartuccia salvato su SD (log di boot, bootprev e lastrun completati in D19).
- **R4 — Screenshot**: done in the games' runtime (`shot()`, Print Screen, Select+L1+R1, `bm/shots/`: [`SCREENSHOTS.md`](../SCREENSHOTS.md)). To do: the menu's own screens, a try on the Pi and the RGB30 (3D `later` mode), the key's picture in the F12 list.
- **R6 — Web Interface locale (`bm.local`)**: Server HTTP integrato e responder mDNS (`bm.local`) per consentire il caricamento di file `.bm`, il download di screenshot/salvataggi e la modifica di `bm/config.txt` direttamente dal browser del telefono o PC locale.
- **R8 — Rumble e LED DualShock 4**: Esposizione delle API `rumble(p, forte, debole, ms)` e `padlight(p, colore)` sfruttando il report di uscita 0x11 già implementato in `bt.c`.
- **R19 — Supporto Controller Bluetooth Esteso**: Integrazione profili per DualSense, Switch Pro Controller, 8BitDo e gamepad Xbox (Bluetooth LE tramite `ble.c`).
- **R20 — Telecomando HDMI-CEC**: Supporto per le frecce direzionali e tasto Select/OK del telecomando della TV per navigare nei menu della console senza controller collegato.
- **Rete & bmnet Multiplayer**: Test di due console Pi collegate su modello *Online 2D* (anche via relay) e porting definitivo del protocollo di rete di Overbit sopra la libreria unificata `bmnet`.

## Rules (do not break)

- All SD logs only via `src/kernel/sdlog.c`.
- Planned restarts only via `crumbs_restart()`.
- SD logs masked with `reports_mask_secrets()`.
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
- Transfer port read ops (`netxfer.c`): `G` serves only files of `bm/shots` (one level, no `..`) and the three logs `bm/BOOTPREV.TXT|BOOTLOG.TXT|LASTRUN.TXT` (exact names);
  `T` needs a game drawing frames and gives up after 3 s (`NG`). Details in [system-net](system-net.md).
- Keys and tokens only in the environment or `bm/config.txt`, never in files.
- Tests: `make test-usb`, `test-fat`, `test-net`, `test-https`, `test-http`, `test-lan`,
  `test-github`, `test-release`, `test-catalog`, `test-hyp`; QEMU `-k <name>`.
