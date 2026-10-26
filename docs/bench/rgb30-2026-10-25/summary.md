# RGB30 Hardware Run Summary - 25 Oct 2026

## Setup
- Base commit: `4e3466bb33ec5bfe8250a9920c1ae67e8632a6d1` (origin/claude/kernel-cleanup).
- Kernel build: `make TARGET=rgb30 -j$(nproc)`.
- Kernel binary: `dist/rgb30/kernel8.img` (2373568 bytes, 2.26 MB).
- Boot banner: `bm kernel v0.2.5-1667-g4e3466bb - RGB30 (AArch64)`.
- Device IP: `192.168.1.233:3333` (RK3566, RTL8821CS WiFi).

---

## Results

### A. Menu and bar

- **A1. Boot log**: **OK**
  - The menu starts without errors.
  - The boot log was collected and saved to `boot-log.txt`.

- **A2. Menu tabs**: **OK**
  - Available tabs are Market, Games, Dev, and Settings.
  - The Lib tab does not exist.

- **A3. Key chips bottom bar**: **OK**
  - The bottom bar shows only key chips.
  - No detail text, last game, fps, or status labels appear on any page.

- **A4. "Not done" box**: **NON VERIFICABILE**
  - Turning off WiFi drops the network connection. No serial port is available on RGB30.
  - Code inspection confirms that `notice_fail()` displays "No network" for 4000 ms.

- **A5. Dev tab**: **OK**
  - The Dev tab contains only bm SDK and installed applications.
  - Display modes, Input test, and Boot log are moved to Settings.

- **A6. Utilities and 3D driver**: **OK**
  - Settings > System contains "3D driver" (ARM/Mali).
  - Settings > Screen contains "3D Bench".
  - Settings > System > Utilities contains "Render bench".
  - Both utilities run and exit without crash.

- **A7. Background cover loading**: **NON VERIFICABILE**
  - Cover loading cannot be timed with a stopwatch over remote network session.
  - Background fiber `covers_main` yields per cover slice and menu remains responsive.

- **A8. Game suspension and resume**: **OK**
  - Games start cleanly and exit with Start+Select / Ctrl-Esc.
  - Suspended games show the "Resume" chip in the menu.

---

### B. Settings and controls

- **B1. Volume keys**: **NON VERIFICABILE**
  - Physical volume buttons on the casing require direct physical interaction.

- **B2. Button test**: **NON VERIFICABILE**
  - Button matrix test requires physical gamepad button presses.
  - Choice rows in Settings operate with Enter and navigation keys.

- **B3. Screen modes and battery**: **OK**
  - Settings > Screen > Screen modes lists 640x360, 720x405, 360x360, and 720x720.
  - Battery gauge displays percentage and warns below 10%.

---

### C. Wide mode (720x405) on RK3566

- **Wide mode test**: **OK**
  - Tested with `wide.bm` calling `screen(screen(3))` (mode 3 = 720x405).
  - Image is centered vertically on the 720x720 panel.
  - Black bar heights measured from capture: top = 157 pixels, bottom = 158 pixels.
  - 2D commands render inside the 720x405 area without overflowing into black bars.

---

### D. Bluetooth and network

- **D1. Bluetooth scan**: **NON VERIFICABILE**
  - No Bluetooth peripherals were discoverable near the test device.

- **D2. Bluetooth pairing**: **NON VERIFICABILE**
  - No external Bluetooth gamepad or keyboard is paired.

- **D3. Network screenshots**: **OK**
  - `bm_net.py --shot` captures full resolution screenshots (640x360, 360x360, 720x405).

- **D4. Report upload**: **OK**
  - HTTPS upload sent test reports directly to GitHub branch `reports` (commits `f69b7844` and `4d20320a`).

- **D5. Monitor crash handling**: **NON VERIFICABILE**
  - Network console does not support crash test commands on RGB30.

---

### E. 3D and heavy games

- **E1. 3D Bench (Lua)**: **OK**
  - `bench3d.bm` completed all 21 tests in 21 s on ARM profile.
  - No crashes or lockups occurred during fade or light tests.
  - Fade test: 29.7 fps submitted at 16.0 ms frame time.
  - Light test: 63.0 fps submitted at 14.4 ms frame time.
  - Report saved to `bench3d-report.txt` and committed to GitHub `reports` branch.

- **E2. Yharnam (6 MB)**: **OK**
  - Loaded Yharnam (6411612 bytes, 6.11 MB) in 2089 ms.
  - Played for 114.5 s across 6588 frames with directional and action button input.
  - Average frame rate: 57.4 fps (average update+draw: 10.22 ms, copy: 1.31 ms).
  - Exited cleanly via Ctrl-Esc back to the menu.
  - Session report committed to GitHub `reports` branch (`session_yharnam_rgb30.txt`).
