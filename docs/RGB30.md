# bm on the PowKiddy RGB30

Italian version: [RGB30-IT.md](RGB30-IT.md).

Bare metal version of bm for the **PowKiddy RGB30**: Rockchip **RK3566** SoC (4 × 64-bit
Cortex-A55, 1 GiB LPDDR4), **720×720** IPS screen over MIPI-DSI (Sitronix ST7703 panel), two
microSD slots, Realtek **RTL8821CS** WiFi + Bluetooth, Rockchip RK817 PMIC. The code lives in the
`rgb30-powkiddy` branch; the Pi build does not change.

Decisions (2026-10-01, user):

- **true bare metal**, as on the Pi: no Linux; U-Boot boots the kernel from the SD;
- **1:1 format**: the menu was **512×512**, centred on the panel with no scaling; since
  2026-10-03 (user) it is **360×360 scaled ×2** and fills the panel's 720×720;
- RGB30 games and apps will use a new format, **`.b16`** (formerly `.s16`: the limited-resource cartridge for
  handheld consoles, `docs/B16.md`), still to be defined: the menu
  lists them but does not launch them;
- the Pi's **`.bm` cartridges were hidden** (`show_bm=1` only listed them); since 2026-10-03
  (user), for testing, the menu shows and launches them (`show_bm=0` in `bm/config.txt`
  hides them);
- first milestone: base + **Bluetooth + WiFi** (audio and saves later).

## Building

In WSL/Ubuntu:

```sh
sudo apt install gcc-aarch64-linux-gnu picolibc-aarch64-linux-gnu mtools dosfstools \
                 qemu-system-arm python3
make TARGET=rgb30              # build/rgb30/kernel8.img, copied to dist/rgb30/
make TARGET=rgb30 test         # the same kernel for QEMU's virt machine, with the tests
make TARGET=rgb30 firmware     # once: bootloader and Realtek firmware (firmware/rgb30/)
make TARGET=rgb30 image        # dist/rgb30/bm-rgb30.img (+ .img.gz): the SD image
```

- Compiler: `aarch64-linux-gnu-gcc` (Ubuntu's) with **picolibc** instead of newlib
  (Ubuntu has no newlib for aarch64). Ubuntu's "Linux" options (PIE, stack protector,
  fortify, branch protection, outline atomics) are turned off in `rgb30.mk`.
- `PLAT=rk3566` (default) is the console; `PLAT=virt` is QEMU (`-M virt,gic-version=3 -cpu
  cortex-a55`, PL011 serial, ramfb screen, SD as a RAM disk). The two builds differ only
  in the `plat_*.c` / `rk_*.c` files and in the link address.

## The SD card

1. `make TARGET=rgb30 firmware` downloads:
   - the **bootloader** from the official ROCKNIX image for RK3566 (release `20260901`): only the
     first MiB of the `.img.gz` (HTTP range request), from which `idbloader.img`
     (sector 64) and `u-boot.itb` (sector 16384) are cut out. They contain Rockchip's DDR
     initialisation, **U-Boot 2026.01** (mainline, `anbernic-rgxx3-rk3566_defconfig` + 3 ROCKNIX patches)
     and Rockchip's BL31. It is the same bootloader the console already uses with ROCKNIX;
   - the Bluetooth firmware `rtl8821cs_fw.bin` / `rtl8821cs_config.bin` from linux-firmware.

   Every file is checked against its SHA-256 (`scripts/fetch-rgb30.sh`).
2. `make TARGET=rgb30 image` creates `dist/rgb30/bm-rgb30.img` (256 MiB; 1.1 MB compressed): MBR with
   one 16 MiB FAT32 partition "BM", active, containing `extlinux/extlinux.conf`,
   `kernel8.img`, `LEGGIMI.txt` and `bm/` with the Bluetooth firmware.
3. Write the image to a **spare** microSD with balenaEtcher, Raspberry Pi Imager ("Use
   custom") or Rufus. WSL does not write to cards directly.
4. The card goes in slot **TF1** (the one ROCKNIX boots from); to go back to ROCKNIX just
   put its card back. Power on with the **power button** (plugging in the charger,
   U-Boot shuts down again).
5. Updating bm: Windows sees the "BM" partition as a drive with that name; just copy
   `dist/rgb30/sd/` onto it (`make TARGET=rgb30 sdcard`). From WSL `make TARGET=rgb30 sdcard
   SD=/mnt/<letter>` copies by itself (`scripts/copy-sd-rgb30.sh`) and, if it cannot, says why:
   drive not mounted in WSL (with the `mount -t drvfs` command to mount it), mounted for
   root only, SD adapter locked, or it is not the RGB30's card. `bm/config.txt` on the card
   stays as it is (it is replaced only with `RGB30_CONFIG=file`).

Do not format or repartition the card: the bootloader sits before the partition.

### How it boots

Boot ROM → `idbloader.img` (DDR, SPL) → `u-boot.itb` (BL31, U-Boot) → `bootflow scan` →
`/extlinux/extlinux.conf` on the first partition of the SD in TF1 → `booti kernel8.img`. The
kernel has the arm64 Image header (`src/rgb30/start.S`): U-Boot loads it at
0x02000000 and starts it at **EL2**, MMU and caches off, `x0` = device tree. `start.S` moves itself to
its link address (0x10000000), drops to EL1 and calls `kernel_main`.

## During boot, without a serial cable

| LED | Meaning |
|---|---|
| red on | booting (stuck like this: the kernel hung early) |
| solid green | bm is running and nothing is wrong (since 2026-10-04: `src/kernel/ledstate.c`) |
| green blinking slowly (1 s on, 1 s off) | something else: boot not finished, the SD is missing or unreadable, the battery is low (below 3.45 V, without charger), a kernel is arriving or an update is being installed |
| solid red + green blinking | the screen had problems: read `bm/bootlog.txt` |
| red blinking N times, pause | fatal exception N (1 synchronous, 4 SError, 9 panic) |

At boot the screen shows the bm logo (`src/kernel/splash.c`); what the kernel writes
goes to the console (behind), the serial port, `bm/bootlog.txt` and *Settings > System > Log since
boot*.

If it stops while turning on the screen (`bm/bootlog.txt` ends with `display: starting`), the steady
LEDs say where:

| Steady LEDs | Stuck in |
|---|---|
| red and green | video clock (VPLL, `clocks_on`) |
| green only | video controller (VOP2, `vop_init`) |
| none | DSI link, D-PHY or panel commands (`rk_dsi_init`) |
| red only | video power domain, or window and backlight |

**After a restart** (an update, *Restart*, a kernel from the network; 2026-10-05): before
restarting, bm turns off backlight, panel, WiFi module and the two LEDs (`quiet()` in
`plat_rk3566.c`), then asks the firmware for a reset (PSCI SYSTEM_RESET) and, if the firmware comes
back, does the chip's global reset (`CRU_GLB_SRST_FST`, like Linux). A reset of the chip alone
leaves the PMIC rails and the PMU GPIOs as they were: turned off beforehand, the panel restarts as
from power-on. Since 2026-10-10 (black screen after an update, fixed by turning off and
on again), at shutdown the panel, if the link is up, receives *display off*
(DCS 0x28) and *sleep in* (0x10), then reset low, power (GPIO0_C2) off and 300 ms to
discharge. At boot `rk_dsi_init` does as before: reset low, power on, 20 ms,
reset released, 120 ms, commands. It **no longer** turns off the panel at boot and does not read back the
*power mode* (DCS 0x0A): that version (same day: power removed at the start and restored
with the D-PHY already on, read-back, second cycle with the link up) left the screen black
at every boot, even cold (boot sound yes, solid red LED + green blinking). If the
screen stays black after the restart:
- LEDs off: bm did not restart (stuck in the firmware or in U-Boot), or it is stuck in the DSI
  (table above): `bm/bootlog.txt` says so, to be read on the PC **before** powering on again (every
  boot rewrites it). The first line is the version of the boot that wrote it: if it is still the
  previous one, bm did not restart;
- red on, or the other combinations in the table: bm restarted and stopped there.

The screen: the border around an image smaller than the panel (a game with `bm_scale=int`)
is the video controller's background colour (the same dark blue-grey as the menu). All uniform blue-grey = panel on but window not
working; black = DSI link or panel; nothing at all = backlight.

**`bm/bootlog.txt`**: at every boot the kernel writes to the SD everything it printed
(version, CPU, SD, what the screen driver did step by step, menu). The SD is read
before turning on the screen and the log is written three times (before the screen, after, and at
`ready`): even if the screen locks everything up, from the PC you can see how far it got. The same log
is in the menu, under *Boot log*.

While the screen starts, `bm/bootlog.txt` is rewritten at every step, with `display: ...` lines:
`display: starting`, `display: DSI link and panel starting`, then `display: panel on` or
`display: panel failed`. The last line present says where it stopped.

**The logs from the previous time** (to understand afterwards what happened; FAT, 8.3 names, all in `bm/`):

| File | When it is written | What it contains |
|---|---|---|
| `bootlog.txt` | at every boot, several times (see above) | the log of this boot up to `ready` |
| `bootprev.txt` | at every boot, as soon as the SD is read (before the screen) | the `bootlog.txt` of the previous boot, then a line on how it ended (from the RAM record in `crumbs.c`) and its last printed lines (4 KiB kept in RAM across the restart; after a power-off they are not there: `(nothing in RAM)`) |
| `lastrun.txt` | before a restart or a shutdown done by bm (*Restart*, *Shut down*, update, kernel from the network: all go through `plat_reset` / `plat_poweroff`), before turning off screen and audio | everything that session printed (the first 64 KiB; if full, the last lines at the end) |

`lastrun.txt` is not written from an interrupt nor while another transfer is writing
to the SD (a kernel from the PC): in that case the previous one stays. There is no (yet) periodic
copy from the menu: a hang without a restart leaves only `bootprev.txt` at the next boot.

To read them: turn off the console, take out the SD, put it in the PC and open the `bm/` folder
(with any text editor). After a black screen: `lastrun.txt` says how the session
before the restart ended (it must end with `restarting: this run's log in bm/lastrun.txt`),
`bootlog.txt` how the black boot went (the `display: ...` lines), and if in the meantime the console
was powered on again `bootprev.txt` has the log of the black boot.

## What is there (status)

- 64-bit base: boot from U-Boot (EL2 → EL1), MMU (WB cached RAM, non-cacheable framebuffer,
  Device peripherals), GICv3, generic timer at 1 kHz, exceptions with registers on screen, picolibc,
  Lua 5.4. **Tested in QEMU** (`make TARGET=rgb30 test`), also through U-Boot 2026.01.
- Screen: VOP2 (video port 1, Esmart0 window) → MIPI DSI0 → Innosilicon D-PHY → ST7703
  panel, PWM4 backlight. Values from Linux (`rockchip_drm_vop2.c`, `dw-mipi-dsi.c`,
  `phy-rockchip-inno-dsidphy.c`, `panel-sitronix-st7703.c`). **To be tested on the console.**
- Controls: 18 buttons on GPIO3, sticks on SARADC channel 3 with a switch; *Input test* page.
  **Tested on the console** (2026-10-03): buttons fine; the vertical axis of the sticks was
  inverted (the dts gives it differently), fixed.
- Confirm and back (user decision, 2026-10-03): **B** (the bottom button) confirms and
  **A** goes back, as on the RGB30; `confirm=a` in `bm/config.txt` swaps them. From the serial port and
  from the network console Enter is confirm, Backspace/Esc back; Bluetooth pads and keyboards
  press the button in the same position (the DS4's cross is B, the keyboard's space too).
- SD: SDMMC0 controller (DesignWare MSHC) in PIO, 4 bit, 12 MHz; FAT from the Pi code.
- RK817 PMIC on I2C0: power-off, battery voltage, charge status.
- Video modes ready for the GPU (`src/rgb30/display.h`): every framebuffer has the shape the
  Mali-G52 GPU wants in order to draw into it (rows aligned to 64 bytes, height in 16-pixel tiles,
  pages on 64 KiB boundaries, up to 3 pages) in video and GPU memory, with its physical
  address (`fb->bus`). The video controller scales the image onto the panel: a game can
  draw at 720×720 or at 360×360 shown ×2 (a quarter of the pixels for the GPU), sharp or
  smoothed; the menu is 360×360 ×2. **Display** page in the menu: the modes one after another
  with a test image (borders, tile grid, colour bars).
- **Mali-G52 GPU, first step of the driver** (M41, bm3d 6.0; `src/rgb30/mali.c`, *Dev > GPU test*):
  only when asked for (boot does not touch the GPU), one step at a time with a line on screen and
  a `gpu` report: vdd_gpu on (RK817 DCDC2, otherwise it stops at once), clocks of the
  PD_GPU domain (CRU) and domain on (PMU, like Linux's domains), GPU_ID and present parts, reset,
  power-up of L2, cores and tiler, address space 0 with the "Mali LPAE" tables (64 MiB of
  video and GPU memory seen 1:1), a WRITE_VALUE job on slot 1 and a chain of two. Every
  wait has a limit; the first step that does not come back stops the test and says why (the job
  status, an MMU fault with its address). Then (bm3d 6.1) a fragment job with no
  draws that clears a 64×64 surface (checked pixel by pixel) and a **green square
  at the top right** of the screen: if it shows, the GPU wrote the pixels. No triangles yet. **Tested on the PC** with a simulated GPU, CRU and
  PMU (`make TARGET=rgb30 test-mali`); in QEMU the page says there is no GPU
  (`test_gpu_test`); **to be tested on the console.**
- Pi cartridges (`.bm`) in the menu and launchable, for testing: Yharnam in the SD image (see
  below).
- 360×360 menu scaled ×2 (fills the panel): since 2026-10-04 it is **the same as the Pi's**
  (`src/kernel/menu_ui.c`: bar with pill tabs and icons, square 88×88 covers, three per
  row, panels, button hints). Tabs **Market** (since 2026-10-05, the first, off
  screen to the left until it is the current tab: the same Market as the Pi, but only the
  catalogue's `.b16` games; B asks and downloads, the game goes into `bm/` and is played from there, X the details:
  play, download again, delete), **Games** (`.b16` games and the `.bm` ones, with their
  cover; with `show_bm=0` it says how many cartridges are hidden), **Dev** (3D Bench, Render bench,
  Display, Input test, Boot log, Lua on the serial port, GPU test) and **Settings**, the last, a page of its own:
  since 2026-10-04 the same sections as the Pi (`src/kernel/settings.c`): Controllers
  (Bluetooth, pairing pads, keyboards and mice, button test, *Confirm button*, keyboard
  layout, icons), WiFi and network (saved network, address, network console, connecting,
  *Test the connection*), Screen and sound (the screen modes, the performance overlay, the
  sound: Sound, Volume, *Test the sound*), Updates, Reports, System (version, memory, SD, battery, the log, restart,
  shutdown). Controller only: L1/R1 the tabs (without wrapping around, as on the Pi), the D-pad
  the covers and rows, **B** opens and **A** goes back (`confirm=a` swaps them, in the
  hints too). The button icons, in the menu, in the hints, in the system dialogs and in
  apps (`prompt()`), are the console's (2026-10-10): a dark button with the letter in its
  colour, A green, B blue, X red, Y yellow (`PROMPT_RGB30_*` in `src/kernel/prompts.c`; from Lua
  also `"RGB30_A"`…`"RGB30_Y"`). The icon of the confirm button is that of the physical button
  (B, or A with `confirm=a`); after a Bluetooth pad the apps show its own. On the right of the bar only the network and the **battery** (since 2026-10-05; no icons
  for controllers, mice and keyboards, user decision): four
  notches from 75% up, one fewer every 25%, empty and red below 10%, a lightning bolt on the
  charger; the charge comes from the voltage (0% at 3.45 V, when the LED warns, 100% at 4.18 V),
  also shown in *Settings > System > Battery*. On the charger the voltage rises: there the
  percentage is only an indication. The lightning bolt follows the cable at once (2026-10-10: the RK817
  bit read 4 times a second, `src/rgb30/battery.c`; before, everything every 10 s). At 20% or
  less, off the charger, a small red battery also appears over games and tools
  (top right; *Settings > Screen and sound > Low battery icon* turns it off); in Lua
  `battery()` and `battery_low()`. The lines that do work (pairing, connecting, updating, bench, log) still write
  to the text console.
- **3D Bench** (Dev tab, `src/rgb30/b3d_rgb30.c`): the same test bench as the Pi
  (`src/bm/b3d.c`), at 640×360 scaled onto the panel; all 3D tests drawn by the ARM (the Mali
  GPU has no driver yet: the GPU columns stay empty), the Cortex-A55 counters
  (instructions, L1 data cache misses, cycles; `start.S` leaves them to EL1 with `MDCR_EL2.HPMN`), the
  report in `bm/bench/3DNNNN.TXT` on the SD and on the serial port. Left/right page through the
  result pages, the back button (A) returns to the menu. In QEMU it takes about 40 s
  (`test_bench3d`).
- Bluetooth: RTL8821CS on UART1, H5 protocol (`src/bt/h5.c`) and Realtek firmware
  (`src/bt/rtlbt.c`), then the same stack as the Pi (controllers and keyboards). H5 and firmware **tested
  on the PC** against a simulated chip (`make TARGET=rgb30 test-bt`); **to be tested on the console.**
- WiFi (RTL8821CS over SDIO, port of rtw88): power-up, firmware, MAC from the efuse, MAC/BB/RF tables,
  channels and power, IQK calibration; **scan** (probe requests and listening on channels 1-13; on
  screen the packets per channel and the replies to our MAC, which prove transmission);
  **connection** to open and WPA2-PSK networks (authentication, association, 4-way handshake and
  group key renewal in software, `src/rgb30/wpa.c`; the keys in the chip's CAM, which
  encrypts and decrypts in CCMP); 802.11 ↔ Ethernet data for **lwIP** (DHCP, network console, sending
  files: the same as M18); the connection is monitored through beacons (8 s without: lost) and
  deauths. 2.4 GHz, up to 54 Mbit/s (no 802.11n for now). **Tested on the PC** with a simulated chip and two
  access points, DHCP and ping included (`make TARGET=rgb30 test-wifi`); **to be tested on the
  console.**

## Sound

Since 2026-10-05 (branch `claude/rgb30-audio`, verified on the console and merged on 2026-10-06) the
RGB30 plays like the Pi: the same 8-voice
synthesiser, the games' banks, music, effects and nano8 (`src/audio/audio.c`). The output is
`src/rgb30/rk_audio.c`: the RK3566's I2S1 sends 48 kHz 16-bit to the codec inside the RK817 (the
battery chip), which plays through the **headphones** or the **speaker**: when the headphones
are plugged in the console switches to them by itself, and the headphones are mono. The values come from Linux
(device tree `rk3566-powkiddy-rk2023.dtsi`, drivers `rockchip_i2s_tdm.c` and `rk817_codec.c`).

- **Volume**: the **+** and **−** buttons on the side, everywhere (menu, pages, games): a "Volume"
  bar over the screen for a moment; held, they keep going. The level is saved in
  `bm/config.txt` (`volume=`, 0–10, as on the Pi) when the buttons stay still for 2 s. Also
  *Settings > Screen and sound > Volume*.
- **At boot** a little two-note sound, as on the Pi: if you hear it, sound works.
- **If you hear nothing**: *Settings > Screen and sound*: the *Sound* line says `on` or `off`
  and, when selected, why below; *Test the sound* writes the status, the I2S counters
  (interrupts, chunks, gaps) and the MCLK (must be 12288000 Hz), then plays the test melody
  (the six waveforms, a chord, glissando, vibrato, arpeggio). A photo of that page
  is enough to see where it stops.
- Tests on the PC: `make TARGET=rgb30 test-audio` (the driver on a simulated chip) and
  `test_sound` in QEMU (a sink that takes the 48 kHz in place of the I2S and says which note it hears).

## Pi cartridges (`.bm`) for testing

For testing (user decision, 2026-10-03: "for the time being") the Games tab lists the
`.bm` cartridges in `bm/` and **launches** them, without settings; `show_bm=0` in `bm/config.txt`
hides them. The runtime is the Pi's (`src/bm`), the same code compiled for
64 bit; what the Pi has and this does not is replaced by `src/rgb30/bm_port.c` (3D drawn
by the ARM, no DMA) and `src/rgb30/bm_input.c` (the controls); the sound is the Pi's (above). In the SD image there is
**Yharnam** (360×360 since 2026-10-10, before 256×256; from the `claude/yharnam` branch).

- Screen: the cartridge draws at its resolution and the video controller scales it up to
  fill the panel (256×256 → 720×720), sharp. `bm_scale=int`: integer multiples only (256 ×2 =
  512×512, all pixels equal, with the border); `bm_smooth=1`: smoothed.
- Buttons: in games the letters printed on the console count (a game that writes "A: start" wants
  the A button); `game_buttons=position` maps them by position, like a DS4 on the Pi (the bottom button,
  B, becomes the game's A). The sticks are the game's left and right stick; Start +
  Select exits and returns to the menu.
- In QEMU the game runs (test `test_bm_cartridge`), shown 1:1 (QEMU does not scale and has no
  16-bit format: `plat_virt.c` converts it).

## WiFi: how to use it

In `bm/config.txt` on the SD (from the PC):

```
wifi_ssid=NomeDellaRete
wifi_psk=password
```

Or already in the image: `make TARGET=rgb30 image RGB30_CONFIG=$HOME/rgb30-config.txt` puts that
file as `bm/config.txt` (keep it outside the repository: it contains the password).

In the menu, *WiFi*: **B** scans for networks (list with signal, channel, security), **X** joins the
`wifi_ssid` network; then DHCP and the IP address on screen, with the password of the network
console (`python3 tools/bm_net.py <ip>`: the w/a/s/d keys, Enter, Esc reach the menu as from the
serial port). At boot the console joins the saved network by itself, like the Pi (`wifi_boot=0` turns it
off, also from *Settings > WiFi and network*). Supported networks: open and WPA2-PSK (also
mixed WPA2/WPA3); not WPA3 only, WPA1, WEP, enterprise.

## Test reports

*Settings > Reports*: the test reports (3D Bench, Render bench, the log) wait in
`bm/reports` on the SD and go to the `reports` branch of `f-accomando/bm` with `github_token` in
`bm/config.txt` and WiFi; *Send the reports* sends them, *Report the log* makes a report of the log. The name gives kernel,
branch and board: `reports/<branch>/<data>_<tipo>_rgb30_<kernel>.txt`.

## Updating bm

- **From the console** (WiFi connected): *Settings > Updates* reads the latest GitHub release
  (`manifest-rgb30.txt`, signed with the release key that is in the kernel), says whether it is
  newer and which files change (`kernel8.img`, `bm/ca.pem`); *Install the update* installs it: it downloads and
  checks everything before writing, keeps the previous kernel in `bm/backup/kernel8.img`,
  writes `kernel8.img` last and restarts. `update_url=sd:/cartella/` in `bm/config.txt` for
  a release copied onto the SD (testing).
- **From the network**: `python3 tools/bm_net.py <ip> --kernel build/rgb30/kernel8.img` (the password is
  the one *WiFi* shows when the console joins the network), or `./easy_install.sh`, item
  1 ([NET] update kernel), with an RGB30 board profile.
- **From the PC**: copy `kernel8.img` onto the SD.

## Games from the network

`python3 tools/bm_net.py <ip> --send gioco.b16` (or `./easy_install.sh`, item 5): a `.bm` /
`.b16` for `/carts` (the default) ends up in `bm/`, the folder the menu lists. The console
replies as soon as the file has arrived whole (`QD`) and writes it to the SD from the menu, one chunk per
frame in a fiber: the game has the *Updating* label and does not start until it is written; the
top bar shows the KiB written. If a game is open the file waits in memory and goes to the SD
on return to the menu. A second file while the first is waiting: `BY` (busy), resend later.

## Files

- `rgb30.mk` — the build (included by the Makefile with `TARGET=rgb30`).
- `src/rgb30/` — everything specific: `start.S`, `vectors.S`, `mmu.c`, `cache.c`, `gic.c`,
  `timer.c`, `exc.c`, `syscalls.c` (picolibc), `fb.c`, `glue.c` (the Pi driver APIs),
  `pad.c` (controls, also from the serial port), `ui.c` (menu), `main.c`;
  `plat_virt.c` + `sd_virt.c` (QEMU); `plat_rk3566.c`, `rk_gpio.c`, `rk_input.c`, `rk_board.c`
  (LEDs), `rk_display.c` (VOP2), `rk_dsi.c` (DSI, D-PHY, panel), `rk_mmc.c` (DesignWare MSHC),
  `rk_sd.c`, `rk_pmic.c`; Bluetooth: `rk_wlbt.c` (module power), `rk_btuart.c`,
  `rk_bt.c`; WiFi: `rk_sdio.c`, `rtw_io.c`, `rtw_mac.c` (power-up, firmware, efuse),
  `rtw_init.c` + `rtw8821c_table.c` (MAC and radio, CAM, firmware commands), `rtw_frame.c`
  (packets, 802.11), `wpa.c` (WPA2), `rtw_sta.c` (the functions of `wifi/wifi.h`); GPU: `mali.c`
  (the driver, portable), `gputest_rgb30.c` (the *GPU test* page).
- Code shared with the Pi: `gfx/`, `lib/printf.c`, `script/luavm.c` and `lib_bm.c`, `fs/fat.c`,
  `kernel/config.c`, `crumbs.c`, `version.c`, Lua.
- `tests/rgb30/qemu_test.py` — tests in QEMU (boot, EL2 and relocation, screen read from pixels,
  Lua, menu, hidden `.bm`, input test, bootlog on the SD).
- `tests/rgb30/rtw_frame_test.c`, `wpa_test.c` (published vectors and a handshake computed
  separately by `wpa_vectors.py`), `wifi_sim_test.c` (the whole station on a simulated RTL8821C and two access
  points): `make TARGET=rgb30 test-wifi`.
- `tests/rgb30/mali_test.c` — the GPU test on a simulated Mali-G52, CRU and PMU (page
  tables walked as the GPU does, jobs executed, variants that do not add up):
  `make TARGET=rgb30 test-mali`.
- `boot/rgb30/` — the card's `extlinux.conf` and `LEGGIMI.txt`.
- `scripts/fetch-rgb30.sh` — bootloader and firmware; `scripts/mksd.py --start-mib --raw --active`.

## Memory map (RK3566)

| From | To | Use |
|---|---|---|
| 0x00000000 | 0x00200000 | TF-A (BL31): not mapped |
| 0x02000000 | | where U-Boot loads `kernel8.img` (then it moves) |
| 0x10000000 | | kernel (text, data, bss, 1 MiB stack, MMU tables), then the heap |
| 0x3c000000 | 0x3fc00000 | video memory, 60 MiB (non-cacheable): the framebuffers |
| 0x3fc00000 | 0x40000000 | Mali GPU memory, 4 MiB (non-cacheable): page tables, jobs (`PLAT_GPU_START`) |
| 0xfc000000 | 0xffffffff | peripherals (GIC 0xfd400000, CRU 0xfdd20000, VOP2 0xfe040000, DSI0 0xfe060000, SDMMC0 0xfe2b0000, UART2 0xfe660000, …) |

## Licences of the downloaded files

- U-Boot: GPL-2.0+ (tag `v2026.01` + the ROCKNIX patches in
  `projects/ROCKNIX/devices/RK3566/packages/u-boot-Generic/patches` of release `20260901`).
- DDR initialisation `rk3568_ddr_1056MHz_v1.23.bin` and BL31 `rk3568_bl31_v1.45.elf`: Rockchip's
  rkbin licence (redistribution allowed, no reverse engineering).
- Realtek firmware: `LICENCE.rtlwifi_firmware.txt` (redistributable without modification), copied
  into `bm/` on the card.
- The WiFi driver (`src/rgb30/rtw*.c`, tables in `rtw8821c_table.c`) is a port of Linux's rtw88,
  GPL-2.0 OR BSD-3-Clause, used under the BSD-3-Clause licence (Copyright Realtek Corporation).

None of these files is in the repository: `make TARGET=rgb30 firmware` downloads them.
