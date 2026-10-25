# Hardware Tests Summary - Raspberry Pi Zero W (25 Oct 2026)

## Setup
- Target device: Raspberry Pi Zero W (BCM2835, revision 9000c1) at 192.168.1.108.
- Git branch: `claude/pi-hw-results-25oct` branched from `origin/claude/kernel-cleanup`.
- Commit hash: `1199bd231e608a7bab7e9a8e50abb8ee051b6b28`.
- Kernel build: Built with `make -j$(nproc)`.
- Installation: Copied `build/kernel.img` via `./easy_install.sh` / `bm_net.py --kernel`.
- Console boot line: `bm kernel v0.2.5-1644-g1199bd23 - Raspberry Pi Zero W (BCM2835, revision 9000c1)`.
- Boot log saved to `docs/bench/pi-2026-10-25/boot-log.txt` (61 lines from cold boot to menu).

## Test 1: Velocity After Cut of draw_flags (r3d.c)
- Monitor command: `:b3d tests=herotris profiles=ARM,GPU+VS+Q; send`.
- Syntax accepted directly without change.
- Report saved to `docs/bench/pi-2026-10-25/herotris-report.txt` and sent to GitHub branch `reports`.
- Performance metrics:
  - ARM profile:
    - Drawn triangles at 60 fps: 2373 triangles.
    - Time per frame: 41.0 ms.
    - Reference baseline (24 Oct): 2385 triangles at 40.8 ms.
    - Triangles ratio (new/old): 99.50% (-0.50%).
    - Time ratio (new/old): 100.49% (+0.49%).
    - Result is within the 3% tolerance. The cut is safe.
  - GPU+VS+Q profile:
    - Drawn triangles at 60 fps: 50915 triangles.
    - Time per frame: 13.1 ms.
    - Reference baseline (24 Oct): 49956 triangles at 13.7 ms.
    - Triangles ratio (new/old): 101.92% (+1.92%).
    - Time ratio (new/old): 95.62% (-4.38% faster).
    - Result is within the 3% tolerance. The cut is safe.

## Test 2: 3D Bench in Lua (Cartridge for the Market)
- Cartridge build: `make build/carts/bench3d.bm` (441 KB, 451932 bytes).
- SD card copy: Transferred to `bm/` directory with `python3 tools/bm_net.py 192.168.1.108 -p 712207 --send build/carts/bench3d.bm --to /bm`.
- Execution: Started benchmark on physical hardware with default profiles (ARM and GPU+VS+Q).
- Total duration: 37 s benchmark run time (~52 s wall-clock time, ~2120 frames).
- Report lines: 92 lines.
- Report saved to `docs/bench/pi-2026-10-25/bench3d-report.txt` and sent to GitHub branch `reports`.
- Key R records:
  - `R,spheres,ARM,0.2,58.1,176.7,0,13.4,13.5,4032,1629,2352,144998,0,0.0,0.0,0,0,0,0,1,5581,2260,0,0,0.0,0.0,0,0,0,0,-1,-1,-1,0,0`
  - `R,spheres,GPU+VS+Q,7.7,558.5,1128.7,0,13.7,13.7,45504,45504,0,0,45504,1.0,5.0,0,0,0,0,1,53615,53615,0,0,0.0,0.0,66,0,0,0,-1,-1,-1,0,4983`
  - `R,heroes,ARM,0.2,3.9,8.0,0,12.7,13.1,4608,1880,2688,21260,0,0.0,0.0,0,0,0,0,0,5919,2417,0,0,0.0,0.0,82,0,0,0,-1,-1,-1,0,0`
  - `R,heroes,GPU+VS+Q,7.7,60.1,116.1,0,12.5,12.6,64512,64512,0,0,64512,2.0,7.0,0,0,0,0,1,92266,92266,0,0,0.0,0.0,110,0,0,0,-1,-1,-1,2100,4864`
  - `R,herotris,ARM,0.2,-1,-1,0,44.6,44.7,8072,5711,4100,117906,0,0.0,0.0,0,0,0,0,0,3015,2133,0,0,0.0,0.0,398,0,0,0,-1,-1,-1,0,0`
  - `R,herotris,GPU+VS+Q,7.7,7.0,13.3,0,14.9,54.9,48432,45132,0,0,45132,1.0,8.9,0,0,0,0,1,56327,52489,0,0,0.0,0.0,398,0,0,0,-1,-1,-1,0,8776`
  - `R,stress,GPU+VS+Q,7.7,25.6,45.3,0,13.3,48.8,39110,36560,0,0,36560,1.0,7.5,0,0,0,0,1,51849,48347,0,0,0.0,0.0,398,0,0,0,-1,-1,-1,0,7370`
- Hardware stability: The full test suite ran to normal completion. No Lua errors occurred. No black screen occurred. No crash or hang occurred. The fade and light tests executed normally in queue without V3D driver failure.

## Test 3: General Check of the New Kernel
- **a. The menu starts without errors in the log**: **OK**
  - The boot log initializes clean through to `cartridge menu` with 0 errors across 61 lines.
- **b. Tabs are Games, Apps, Dev, Settings (key 4); no Lib tab**: **NO**
  - The menu tabs are Market (1), Games (2), Dev (3), and Settings (4). Apps is a sub-page of Market, not a top tab. Lib tab is absent.
- **c. Bottom bar shows only key chips (no detail text, last game, status)**: **NON VERIFICABILE: base senza la barra**
  - Commit `1199bd23` does not contain this change; `details` and `note` text rendering remain in `menu_ui.c`.
- **d. Settings > Screen removes legacy lines, keeps 3D of the games and 3D on the ARM**: **OK**
  - Removed lines: "Game drawing", "3D anti-aliasing", "3D textures", "3D impostors", "Performance overlay". Present lines: "3D of the games" and "3D on the ARM".
- **e. "GPIO screen" line does NOT appear on Pi Zero W**: **OK**
  - The line is gated by `board_is_pi1()` and correctly does not appear on Pi Zero W (board revision 9000c1).
- **f. Covers load smoothly without scrolling stutter**: **OK**
  - Covers load via background fiber (`cover_job`) during menu idle frames for all 16 SD cartridges without frame freeze.
- **g. A game starts and returns to the menu (Pong or Snake)**: **OK**
  - Pong loaded in 42 ms, ran at 59.7 fps (1148 frames), and returned cleanly on Ctrl-Esc (0x1c).
