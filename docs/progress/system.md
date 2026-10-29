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
Exceptions show a red screen with registers and the call trail. Breadcrumbs and the last 4 KB
printed live in `.noinit`, survive a reset and become a report at the next boot
(`reports_text`), sent once the network is up. Watchdog: armed at 3 s (the Pi Zero W boot log says `watchdog: on, 3000 ms`).
`exception_handler()` sets the crash flag before all other work, so a restart from a hang in the handler is reported as a crash. The timer IRQ pets the watchdog through `watchdog_pet_irq()`, which pets only while the main code makes progress (`watchdog_progress()`, called by `crumb`, `crumb_frame`, `fb_flip` and the input polls). More than 20 s without progress: no pet, and the SoC resets 3 s later. `mbox_call` gives up after 2 s. Analysis of the freezes of 22-23 Oct: [watchdog-hang](../design/watchdog-hang.md).
Freeze of 26 Oct 2026 (Pi, 3 hours after boot, from the report `kind: freeze`): the VideoCore firmware stopped answering. Every `fb_init_depth` failed with `mbox_call failed`, the menu could not draw, the network stayed alive. Fixes (host and QEMU tested, not yet on the Pi): after `MBOX_DEAD_TIMEOUTS` (3) mailbox timeouts in a row `firmware_dead()` (`src/kernel/main.c`) sets a crumb and restarts through the watchdog; the next boot sends a freeze report. The kernel log (`klog`, 64 KB) keeps its first 12 KB and the last lines, with a marker (`bootlog_trim`, `make test-klog`); before, it stopped when full. A planned restart (`crumbs_restart`) saves the log to the SD first and marks the run as ended last. The nearby-console listening (`market_lan`) retries every 5 s, says it once with the lwIP error number, and the listener may reuse a port in TIME_WAIT (`SO_REUSE`, `SOF_REUSEADDR`): before, after a game received a moment ago it failed every frame and the next transfer found no listener. Open: the cause of the firmware stop is not known (the 3D, the QPU or the Market download are suspects); watch the next freeze report.

### USB (`src/usb/`)
Own DWC2 host stack (`dwc2.c`, `usb.c`). HID (`hid.c`): boot keyboards (IT/US layout),
generic HID gamepads (descriptor parsed), Xbox 360 wired, mice. `smsc95xx.c`: the Pi 1
B/B+ Ethernet.

### Storage (`src/drivers/sd.c`, `src/fs/fat.c`)
- SDHOST by default (`sdhost.c`), Arasan EMMC (`sd_emmc.c`, the Zero W's WiFi controller)
  if SDHOST fails a transfer twice; not on a failed multi-block write. `sd_counters()`
  (retries, waits run out) and `fat_cluster_bytes()` in the boot `sd:` line.
- FAT16/FAT32: long names read, whole files and 8.3 directories written. `store()`: the
  clusters marked as a chain first (`alloc_chain`), the data in runs of up to 32 KB read
  back (`fat_write_runs`, on; off for good on a mismatch), the last cluster only where it
  has data, then with no tick the entry found again (`locate`) and changed in one sector;
  the old chain released last. FAT entries change a sector per copy at a time
  (`fat_set_list`); the free-space search starts at FSInfo's hint.
- `fat_write_tick` (a write in a fiber yields): [multitasking](multitasking.md). `fat_writing()`.

### Bluetooth, WiFi and network
Details (`src/bt/`, `src/wifi/`, `src/net/`, lwIP, mbedTLS): [system-net](system-net.md).

### Menu and kernel services (`src/kernel/`)
Detail of `src/kernel/` (menu loop, settings, Market, update, services): [system-menu](system-menu.md).

## Open work

- **C4:** the RAM ring stops before the crash (BOOTPREV ends at the netcon prompt). BETA1-C4-RING is merged (PR 50): the crash record holds `ring_at` and the sink state, and a nested exception leaves the crumb `nested exception`. BETA1-C4-RING3: the timer IRQ is on before `crumbs_boot()`, and `flush()` then wrote the new boot's state over the old freeze record (the header said `ring 3601, sink off`). The writers of the record now do nothing until `crumbs_boot()` has copied it (`cr_live`), and `flush()` stores the live ring state until the first crash call (`crash_noted`). `make test-crumbs` covers this on the PC. The item stays open until a Pi run shows a crash header with `ring N, sink on` (N above 0) and handler lines. A real crash (Xu) that still showed `sink off` is not explained yet. Details: [beta1-c4-diag.md](beta1-c4-diag.md).
- After a long session on the Pi, copy `bm/` from the card and run `python scripts/sdlog_scan.py <dir> --secret <net_password>`. Verdict OK means no freeze, no crash, and the password is masked.
- Measurements to redo, stalls that still hold the CPU, downloads inside an app: [multitasking](multitasking.md).
- `net fetch` in the Monitor: download a `.bm` from a public GitHub file (`gh:f-accomando/bm-market/...`) and from a private one with `github_token` on a Pi; check the progress line, the speed and the menu that lists it.
- bm Agent and `ai_ask` (spec 6, [system-net](system-net.md)): run them on a Pi and on an RGB30 against a free provider (Groq or Gemini) and against `tools/bm_agent_relay.py` with Claude Code. Note the TLS stall, the answer time and any error in [system-net](system-net.md). Try `/setup` on both consoles: a free key, and the relay's pairing code (the CA download on port 8444, the question, `bm/ai.txt` and `bm/ca-user.pem` on the card). Then close the `ai_ask` part of D50. Not done: streaming answers (`"stream": true`), the provider's native tool calls.
- Names the system writes in lowercase (owner decision, 2026-10-22): not verified on real cards in Windows, Linux and the console. Writers not changed: none known. Names that stay in capitals on purpose: the key `allow_XXXXXXXX` in `config.txt`; the `built from` field inside a `.bm` (now lowercase too, compared in any case); the volume label. Files the PC tools name for the card are lowercase (`easy_install.sh`, `bm_net.py`, `pi_bridge.py`).
- **SYS-2 freeze guard, on the Pi:** the guard is built and tested on the PC only (`make test-crumbs`). Check on a Pi Zero W: (1) a long run of 3D Bench profiles and a night in the menu with no false restart (the limit is 20 s with no progress); (2) done 23 Oct (Pi, kernel of `aa741d1f`): `X g` (bridge key `freeze-irq`) restarted the Pi and the next boot reported "freeze test (X g, IRQs on)" with the crumb, 115 s after boot (`X f` turns IRQs off and tests only the 3 s watchdog); (3) read `bootprev.txt` and `crumbs` of a real freeze. Not known yet: which wait hung on 22 and 23 Oct (see [watchdog-hang](../design/watchdog-hang.md)). If a false restart shows, add `watchdog_progress()` to the loop that reports it.
- **Monitor as a terminal (25 Oct 2026):** done on the Pi and verified in QEMU and on the PC (`make test-monitor-cmd`, the QEMU tests `test_monitor_help_table`, `test_monitor_prompt`, `test_monitor_keys_and_words`). Not verified on hardware: a USB keyboard at the `bm>` prompt (arrows, Tab, the lone Esc after 100 ms), the network console (TCP 3333) on a real Pi, and a terminal program on the serial line. The RGB30 does not compile `monitor.c` (D22); wire the shared core `monitor_cmd.c` there when the user decides D22.
- Try the SD map (Settings > System) on the Pi Zero W (speed of the scan, stutter of the menu) and on the RGB30: not verified on hardware.

### Ideas and the file layout
- The ideas for a standalone console (WiFi with the pad, on-screen keyboard, `bm.local`, DualShock rumble, HDMI-CEC, bmnet) and the file layout by type (SYS-16) are in [system-ideas](system-ideas.md).

- **USB hotplug on the Pi (SYS-17, done 28 Oct 2026, works on the Pi Zero W: checked by the user).** `usb_poll` reads `dwc2_port_connected()` every 250 ms (`hotplug` in `src/usb/usb.c`). Unplugged: the device is forgotten and its keys go up. The search runs only while no keyboard is connected (and no pad is in use): `usb_init()`, tried again every 2 s up to 5 times, then it waits for the next plug. A hub on the root port (the Ethernet of the Pi 1) is never touched. QEMU `test_usb_hotplug` plugs and unplugs a keyboard. Left: a keyboard behind a hub plugged later (the children do not change HPRT: read `port_status`), and a mouse alone.
- **The monitor as a shell (SYS-19, in progress).** Step 1 done on 28 Oct 2026: `monitor_keys=0` opens the monitor at the `bm>` prompt; `hotkey` turns the single keys on, Enter or Esc goes back to the prompt, `exit` leaves to the menu; the default (no key) is the single keys, so the bridge (`pi.py keys`) and the QEMU tests keep working; `d` (demo) and `k` (CPU bench) removed with `demo.c` and `bench.c`. Step 2 done on 28 Oct 2026: the file commands `ls`, `cd`, `pwd`, `cat`, `cp`, `rm`, `mkdir` (`src/kernel/monitor_fs.c`, FAT; relative and absolute paths, a new name is 8.3, `rm` refuses the files the card needs to start, `cat` shows the first 16 KB; no rename yet: FAT has no rename). Steps 3-5 done on 28 Oct 2026: `lua FILE` and `lua -e CODE` at the prompt (no word: the REPL as before); the `bm` module of the shell's Lua has `sh(line)` (ok, output: `klog_capture`), `read`, `write`, `ls` (FAT), `size`, `fill`, `rect`, `text`, `key` (drawn at once on the console's page, a colour is 0xRRGGBB; `key` returns nil on the RGB30) beside `micros`, `millis`, `sleep`, `mem`, `color`, `cls`, `reboot` (`src/script/lib_bm.c`); `boot=shell` starts at the monitor (`menu` opens the cartridges; the default boot stays the menu); `autoexec=1` runs `/bm/autoexec.lua` once at boot (the PC and the bridge cannot set this key: `config_merge`). Tried on the Pi Zero W on 28 Oct 2026 (the user): it works. Not done: a `flip`/vsync call (the drawing is immediate), a Lua rename (FAT has none), a headless mode with no display. The network shell must stay behind the bridge gate (`bridge.txt`) and the password.

- Check the Assistant app (SYS-20) on the Pi hardware: install it from the Market (`apps/assistant`, 1.2 MB, with the DATA section, published 28 Oct 2026) on a clean kernel. The QEMU test `assistant_app` runs the clean kernel with the app on the card.

## Rules (do not break)
- The Assistant's data (SYS-20, 28 Oct 2026): the dictionaries (`words.lua`) and the knowledge file (`assist.bin`) are not in the clean (shipped) kernel. They are blobs of the DATA section (type 15) of `assistant.bm` (`mkbm.py --data NAME=FILE`, `make build/assistant.bm`). `src/kernel/appdata.c` (`appdata_get`) reads them from `/bm/assistant.bm` at first use and keeps the file in memory. Without the app, `require "words"` and `ai.*` raise "the Assistant app is not installed" and the monitor key `I` prints "assistant: not installed". The complete kernel (`CLEAN_KERNEL=0`) still embeds them, for the tests. Do not embed them in the clean kernel. The clean Pi kernel is 2.4 MB (3.75 MB before). Test: `tests/qemu_test.py -k assistant_app` (builds `build-clean/` on the first run). The RGB30 kernel has no `words`; it is unchanged.
- The bridge gate (26 Oct): `bm/bridge.txt` holds one word: `off` (default), `on` or `fast`. `off` refuses every operation of the transfer port (S, P, K, C, T, G; answer `BO`). `on` accepts them and stages a kernel. `fast` writes a kernel and a file at once (`dev_mode()`, the old `dev=1`). Only the user sets it: Settings > System > Bridge, or the file put on the SD card from a PC. `config_merge` refuses the keys `dev` and `bridge` (`BF`), and the transfer refuses any file named `bridge.txt` in any folder (`BF`). Do not add a network way to write this file.

- The bottom bar of the menu shows the key chips only. Put a failure of a user action in the notice box (`notice_fail()`) and any other message in the console log. Add no text line to the bar.
- A menu card shows only the cover. The cover of a game or an app comes from `scripts/mkartcovers.py` (88x88, one art style); the bm editors keep the covers of `scripts/mkcovers.py`.
- Settings shows a graphics choice only if a player can judge it by sight. Put every performance flag in `config.txt` as a hidden key, with no Settings row (SYS-9; table: [v3d-flags](v3d-flags.md)).
- All SD logs only via `src/kernel/sdlog.c`.
- The Monitor (`src/kernel/monitor.c`) is a table of commands. Add a command with ONE row in `commands`: word, arguments, one help line, shortcut key, handler (a group has subcommand rows). The help, the Tab completion and the key shortcut come from that row. Write no help text by hand. Keep the parser, the editor and the history in `src/kernel/monitor_cmd.c`: it has no hardware, and `make test-monitor-cmd` tests it. Keep an old word as an `alias` row (`b3d`, `render`, `log`, `send`, `reboot`, `get`): `tools/pi_bridge.py`, `bm_net.py`, `pi_test.py`, `easy_install.sh` and the QEMU tests use them. A key handler may ask the user (`from_key` is 1). A line handler takes its words from the arguments and never waits for a key.
- The AI key stays in the kernel (spec 6). Keep the AI settings in `bm/ai.txt` (`src/net/aiconf.c`), never in `bm/config.txt`: `config.txt` is for the system only. Write `bm/ai.txt` and `bm/ca-user.pem` only after the kernel's own question (`ai_setup`, `ai_pair_finish`, `sys_ask`). Keep both names refused by the network transfer (`ai_protected_name` in `netxfer.c`). Ask for a new key when a cartridge changes the server. The kernel adds `Authorization` itself (`l_ai_ask` in `src/bm/api_web.c`). Give no Lua call that returns a config value or a request's header lines. Send a POST to the configured host only: `web_start_post` follows no redirect. Wipe the header copy when the request ends (`free_post` in `src/bm/webget.c`). Never put a provider's subscription token (Claude, ChatGPT, Google sign-in) on the console: a subscription goes through `tools/bm_agent_relay.py` and the official program ([ai-agent](../design/ai-agent.md)).
- A cartridge writes files only through `file_write` into `bm/web` (a bare 8.3 name, never `.bm` or `.bme`), and fetches only `https://` addresses through `web_get`: the rules live in `src/bm/webpolicy.c`, nowhere else. One exception: a name that ends in `.bmm` goes to `/bmm/NAME.bmm`, only after `bm_parse_any` says it is a model (kind 1, MESH) and never over a file that is there (`file_write_model` in `src/bm/runtime.c`; the same on the Pi and the RGB30).
- Planned restarts only via `crumbs_restart()`. SD logs masked with `reports_mask_secrets()`.
- The SD map scan never runs while a game or a tool runs (`sdmap_stop` in `background_stop` on the Pi, in `run_page` on the RGB30).
- The bar's categories are fixed: kernel, bm, bme, other, free (other = total - free - the rest).
- Free space is counted from the FAT (`fat_free_step`), not from FSInfo.
- Fibers (`background_stop()`, `net_wait_step`, update/publish vs apps): [multitasking](multitasking.md).
- One library for both menus. The list of cartridges, the covers job, the frozen game, the save data, the panel of a
  cartridge, its row actions, `lib_play` and the menu's free time live in `src/kernel/library.c`. Change them there.
  Do not copy them back into `carts.c` or `src/rgb30/ui.c`. A difference between the consoles goes in the
  `lib_backend_t` of the console (folders, `want`, `LIB_CAP_*`, jobs, `play_begin`), with its reason in
  `docs/spec/divergences.toml`.
- What differs in Settings between the consoles is a small function in `home.c` (Pi) and `src/rgb30/ui.c` (RGB30),
  declared in `home.h` (`home_restart`, `home_poweroff`, `home_wireless`...). Do not add an `#ifdef BM_RGB30` to
  `settings.c` for a text or a call: add a hook.
- Tabs on the Pi and the RGB30: Market · Games · Dev · Settings (keys `1`-`4`; the Lib tab is
  removed, 25 Oct 2026); the menu opens on Games; the Market never blocks the menu and loads only when its tab is on.
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
- The system writes lowercase names and extensions, and reads in any case (owner decision, 2026-10-22). The FAT driver stores a new name as the 8.3 entry in capitals with the NT flags of byte 12 (`0x08` base, `0x10` extension), whatever case the caller gives (`to_83()` in `fat.c`): Windows, Linux and the console show it in lowercase. Every writer also builds its names in lowercase (`rptNNNNN.txt`, `3dNNNN.txt`, `bootlog.txt`, `config.txt`, `pongNNNN.png`, `rNNNNNNN.bmv`, `xxxxxxxx.sav`, `.prt`, `/bme/name.bme`, `.bm`). A name typed by the user is lowercased too. Lookups, `fat_replace` and `fat_delete` ignore case: an old file keeps its name and is replaced in place. Nothing renames or deletes a file of the user. Harm if a writer builds a capital name: the card shows two styles side by side. Exception: the key `allow_XXXXXXXX` in `bm/config.txt` keeps its capitals (cards of before).
- Pet the watchdog only through `watchdog_pet_irq()` in the timer hook. Call `watchdog_progress()` in every new loop that the main code can sit in for more than 1 s and that does not call `crumb`, `fb_flip`, an input poll, `fiber_slice`, `net_wait_step` or an SD tick. Harm of a plain `watchdog_pet()` in the IRQ: a hung main loop is never reset (the Pi stayed frozen for hours on 23 Oct). Give every wait on hardware a time limit (`mbox_call`, V3D and QPU waits have one). Harm of a wait with no limit: the main code hangs while the IRQ runs.
- Call `crumbs_crashed()` as the first statement of `exception_handler()` (`src/kernel/exceptions.c`). Harm if moved: a hang before `watchdog_stop()` is reported as a freeze (BETA-1 C4).
- Clear SCTLR.A as the first step of `exception_handler`. Keep the re-entry guard: a nested exception does not print; it restarts (uptime 20 s or more) or blinks.
- Tests: `make test-usb`, `test-fat`, `test-net`, `test-https`, `test-http`, `test-lan`,
  `test-github`, `test-release`, `test-catalog`, `test-hyp`; QEMU `-k <name>`.
