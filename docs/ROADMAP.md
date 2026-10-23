# bm Roadmap

The development plan for **bm** (BareMetal), a bare metal *fantasy console* for Raspberry Pi Zero W and PowKiddy RGB30. This file holds **goals and priorities**. The how lives in the area files under [`progress/`](progress.md). The history lives in `git log`. Closed milestones and historical text: commit `0e969b5`.

## How to read it

- One line is one goal, never a task. The line points to an area file. Its `Open work` or `To do` section is where work orders come from. Do not copy the detail here.
- Line format: `ID - priority - status - what`.
- ID: `M##` and `R##` stay. New entries use `SYS-n`, `API-n`, `APP-n`, `GAME-n`. Never reuse or renumber an ID.
- Priority: `P0` blocks other work or breaks a project promise. `P1` is next. `P2` is later. `P3` is an unplanned idea.
- Status: `in progress`, `to test on the Pi` (done on PC/QEMU), `in specification`, `to do`, `idea`.
- A goal without a verifiable condition is not ready for a work order. It goes to `bm-planner`.
- Agents pick the highest-priority lines with no open dependency, read the area file, and write the work orders (format in `docs/agents/routing.md`). On closing, they update the line and the area file in the same commit. The user does the hardware tests. They move the divergences from `untested` to `resolved` in `docs/spec/divergences.toml`.

---

## Graphics 3D

- M37 - P1 - to test on the Pi - 2D and quality on the GPU (`gpu3d_2d`, `menu_scale=3`, bilinear). Done when the menu at 1080p is correct on the Pi. [v3d](progress/v3d.md)
- API-1 - P1 - to test on the RGB30 - RGB30 numbers from the 3D Bench of the Mali and the 2D scenes, after the user's RGB30 runs. The Pi Zero W budget and the `profile(true)` guide are published. [PERF-BUDGET](PERF-BUDGET.md), [lua](progress/lua.md)
- M41 - P2 - in progress - RGB30 Mali-G52 GPU and Overbit in `.b16`. Done when Overbit `.b16` runs at 60 fps with the Mali and 3D Bench reports GPU metrics (device runs H1, H6 open). [graphics-mali](progress/graphics-mali.md), [platforms](progress/platforms.md)
- M39 - P2 - in progress - Toward the V3D limit. Pi 21 Oct: compact attributes +3.8% (kept off), 16-bit textures and ETC1 no gain (kept off). The 3D Bench test `stress` is done (Pi, 22 Oct, one run): 2x the load of 60 fps is stable at 40 fps, ARM-bound, pool 142 KiB ([BENCH3D](BENCH3D.md)); open: Overbit with 10 bots at 60 fps in HIGH. [v3d](progress/v3d.md) `To do`, [lua](progress/lua.md)
- API-6 - P2 - tuned, Pi re-measure of `ht_p10m` pending - Triangle throughput stage 3 (draw merging, hidden key `gpu3d_merge`, on). Pi 23 Oct: 100 small pieces +24%, 10 big pieces -26%, so a run merges only from 32 pieces with at most 150 faces a piece (host tests pass). Open: `b3d tests=htmap profiles=VSQ` and `VS` with the tuned kernel (`ht_p10m` equal to `ht_p10`, `ht_p100m` still +24%); the Overbit check is not measured. Delete this item when the numbers exist. [triangle-throughput](design/triangle-throughput.md) section 8, [BENCH3D](BENCH3D.md)
- SYS-11 - P2 - idea, clarify with the user first - External actions must not interrupt the current activity: a kernel, a cart, a `play` or a download sent from the PC (bridge, `bm_net.py`, netxfer), and other outside actions, now stop or disturb the running game; sending the cart that is running creates problems. To investigate: can downloads run in the background and start later; ask the user before a download or before a start (permission prompt, shown after the game, not over it)? Open questions for the user: which actions may wait for the end of the game, which need a prompt, whether the sender gets a "busy, queued" answer, what happens to a send of the running cart (refuse, queue, or ask). Link the report SYS-10 (multitasking per workload). [multitasking](progress/multitasking.md)
- SYS-9 - P2 - part 1 done (23 Oct) - Simple graphics settings. The performance flags (vs, queue, later, 2d, wc, fs2, sort, attr, tex16, etc1) are hidden keys with no Settings row; the player keeps *3D of the games*, *3D anti-aliasing*, *3D textures*, *3D impostors* and *3D on the ARM*. Defaults are unchanged: each is on only where a Pi number shows a gain. The flags table is in the 3D manual `manuals/content/{en,it}/api-3d.md` (*The graphics keys of the console*). SYS-9b: game-side options and an optional start benchmark that picks values (not built); first decision: `gpu3d_2d` measured +6.7% (`gpu2d`) and 3569+ sprites against 2574 on the ARM, so check a played game, then maybe turn it on. [v3d](progress/v3d.md)
- API-3 - P2 - to do - Triangle throughput: chain jobs (CT0/CT1) only if a measurement shows the GPU idle between jobs (22 Oct: the frame is ARM-bound, so not now). [triangle-throughput](design/triangle-throughput.md), [v3d](progress/v3d.md)
- API-5 - P1 - to do - Align the Mali driver with the V3D driver: textures and lit faces (remove `colour_only`), the vertex-shader mesh path (`BM_VS3`), the draw queue, cel shader and the 3D Bench scenes (heroes, herotris), with the same options as the Pi. Done when the 3D Bench of the RGB30 shows ARM, GPU and GPU+VS columns like the Pi. [graphics-mali](progress/graphics-mali.md), [v3d](progress/v3d.md)
- M43 - P2 - in progress - Sprite stacking (`stack`, `stackv`, `zstack` done, CPU only; the bm Pixel Stack page is done). Left: a conversion button in bm Pixel, try the page on the Pi and the RGB30. Measure stacks with a benchmark on the Pi Zero W (not measured); tens of stacks at 60 fps. [graphics](progress/graphics.md), [suite-pixel](design/suite-pixel.md)
- M44 - P3 - in progress - QPU programs (particles, Yharnam darkness, block audio). Done: driver, queue, probe, fallback, particle step, fade by levels, the register block run again after a QPU reset, the lock for `v3d_qpu_run` (emulator tests). The QPU programs PASS on the Pi Zero W (Dev > GPU test with `gpu3d_qpu=1`, 22 Oct: 1024 particles ARM 158 us, QPU 47 us; 1024 fade pixels ARM 79 us, QPU 36 us). The 3D Bench has the `QPU` profile of the `fade` test (opt-in, `qpufade` report line; host and QEMU tests pass). The gain of `dark_end()` on the QPU for a full-screen fade is measured on the Pi (22 Oct, one run): 20.4 ms on ARM, 9.3 ms on the QPU. Open: the decision to make `gpu3d_qpu` the default for `dark_end` (`docs/NEXT.md`), GPU drawing, a 3D Bench profile for the other fades and filters. `lib.particles` on QPU stays parked (decision of 22 Oct). [v3d](progress/v3d.md) `To do` §5, [graphics](progress/graphics.md)
- R40-R42 - P3 - idea - Profiler, sinc/QPU scaling, hero impostors. Do R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain. [v3d](progress/v3d.md)

## Video, recording and audio

- API-4 - P2 - to do - Video recording (VID-REC) next slices: run the Video bench on the Pi for the RLE565 numbers (bytes and ms a frame), real 8-bit palette, audio. [video](progress/video.md)
- M46 - P2 - in progress - Audio 2: 24-bit stereo synthesizer, PCM/WAV samples, wizard, `riff`. Done when bm Sound games and tracks run at 24 bit on Pi and RGB30 with samples and patterns. [audio](progress/audio.md), [audio-samples](progress/audio-samples.md)
- R21, R25, R32-R35 - P3 - idea - GPIO audio, MIDI export, PC audio, sample editor, sample packs, samples and riff. After M46. [audio-samples](progress/audio-samples.md), [audio-output](progress/audio-output.md)

## Network and web

- SYS-3 - P1 - in progress - Pi tests from the PC (network, keys, screenshot). Done when `bm_net.py --shot/--keys` and the bridge are tested on a Pi, and R-NET3 works with a limited test token. [tools](progress/tools.md) `Pi tests from the PC`, [pi-test](design/pi-test.md)
- SYS-6 - P2 - to test on the Pi - Multitasking stages 1-5 are in bm-core. Left: the Pi numbers for stages 3-5 (handshakes per report, the longest frame during a queued `file_write` of 4 MiB, `Accept-Ranges` and 206 of the catalog host and the release CDN, the real cost of a part file save of 4 MiB) and the download inside an app. [multitasking](progress/multitasking.md)
- WEB-1 - P2 - in progress - bm in the browser: build the portable runtime (Lua runtime, gfx16, audio, bmlib) to WebAssembly with canvas, keyboard/gamepad and WebAudio. Done when a static page (artifact or GitHub Pages, up to 16 MB with cartridges embedded) runs a `.bm` game at 360x360. Study done ([bm-web-player](design/bm-web-player.md)); stage 1 (a JavaScript `.bm` reader in the mock page) and stage 2 (the runtime builds to wasm with Emscripten and draws the same frames as the PC build) done. Stage 3 is in bm-core: the page plays a `.bm` with the canvas, keyboard, gamepad and audio code (tested under Node). Saves in IndexedDB and a 3D cartridge (frames equal to the PC build) are checked. The 60 Hz clock and the sound queue are tested in Node. Left: listen to the sound and read the rate (`bmLauncher.stats`) in a visible tab (not verified), release page.
- R49 - P3 - idea - Hybrid multitasking and differentiated scheduler (revisions 1 and 2). After SYS-6. [multitasking](progress/multitasking.md)
- SYS-10 - P2 - idea - Multitasking per workload (SD writes, downloads, covers, game freezes, app load and close). Done when the Pi numbers of phase 1 exist and each later phase is kept or dropped by them. Pi Zero W is the benchmark. [report](design/multitasking-report.md), [multitasking](progress/multitasking.md)
  - Phase 1, measure (no behavior change): SD write copy time against busy time, handshake, ECDSA verify, 500 ms read timeouts per 100 cover reads, GC pause, app load profile, free time per frame; a stall attribution log line.
  - Phase 2, SD card: find the cause of the 500 ms read timeout; pieces sized by `fiber_budget_us`; yield in the card busy wait; write-behind `save()`.
  - Phase 3, downloads: Range resume in the Market job; restartable ECC if the verify stall is over 20 ms; vsync slice for tools.
  - Phase 4, covers and app start: cover cache file; free job stacks at the stop; cancel flag at the selection; USB poll inside the cover tick (the pointer jump over covers, [report](design/multitasking-report.md) section 7).
  - Phase 5, games: GC steps in the vsync wait; vsync slice for games that opt in (never lockstep).
  - Phase 6, RGB30 core: find why PSCI `CPU_ON` is absent; then a worker core for pure functions (hash, verify, decode, handshake crypto).
- R7, R27, bmnet - P3 - idea - UTF-8 accents in `print()`, battery and autosave, network multiplayer. [runtime](progress/runtime.md) `Open work`, [system-net](progress/system-net.md)

## Platforms and kernel

- SYS-2 - P0 - in progress - Reliable freeze report and SD logs. An intended reboot does not report a freeze. A crash writes a correct `bootprev.txt` (needs the Pi run). The watchdog now resets a hung main code (guard built; the Pi run is open). [system](progress/system.md) `Open work`
- BETA-6 - P1 - partly done - Consistency between the two kernels. The gate is at 11 blockers (was 20; round 2 on 23 Oct closed D48, D49, resource files and project layout). Left in menu, input and Games: D03 and D05 (owner decision), D10 (background covers), D01 and D02 (to try with a Bluetooth pad on the RGB30). Also D14 (partly: Stack bench ported), D15, D17, D23, D24 and three partial specification items (compat-policy, cart-type, folders-bm-bme). Done is: `python scripts/consistency.py gate` shows none of D01-D13, D28, D30-D36 as `open` or `untested`. [platforms](progress/platforms.md) `Open work`, [CONSISTENCY](spec/CONSISTENCY.md)
- M42 - P1 - in progress - Identical `.b16` profile on Pi and RGB30. Left: Yharnam `.b16` on both consoles (user's hardware run) and the lint warnings in the SDK export (`mkbm.py` has them). [b16](progress/b16.md) §2-3
- SYS-8 - P2 - partial - Single-bm and adaptive screen, remaining stages of the design: (1) the rest of the compat check (the build lint is done: one rule list `docs/spec/lint-rules.toml`, `mkbm.py` and the SDK export); the menus (Pi, RGB30), the Market list (`targets` line, `market_bm` default) and the load-time message already grey a not-compatible cart with a reason and ask before the start (policy `docs/spec/compat-policy.toml`, C table by `scripts/mkcompat.py`); (2) the 720x405 mode, `safe_area()`, `aspect()`, bars; (3) per-system input chips and the pad cursor; (4) retire `.b16`; (5) long sound stream. Decided 22 Oct: old `.b16` = Pi + RGB30; old `.bm` = Pi only; grey not hidden, start after confirm; audio max 8M; pad mouse; bars G1; 720x405 mode; `.bm` for all. [single-bm-and-adaptive-screen](design/single-bm-and-adaptive-screen.md), sheet [b16](progress/b16.md)
- R3, R4 - P2 - to verify - SD log (D19) and screenshot (`shot`): close after the console tests. After SYS-2 and SYS-3. [system](progress/system.md)
- R1, R2, R6, R8, R19, R20 - P3 - idea - Standalone console: WiFi from the pad, on-screen keyboard, web UI, DS4 rumble, BT pad, HDMI-CEC. [system](progress/system.md) `Open work`
- R22 - P3 - idea - Portable Pi Zero shell (GPIO, DPI/SPI display). [platforms](progress/platforms.md)

## Tools and SDK

- SYS-1 - P0 - in progress - CI green on `bm-core`. All `ci.yml` jobs (host, qemu 1-4, rgb30) are green and `scripts/ci_audit.py` exits 0. [tools](progress/tools.md)
- M47 - P1 - in progress - Editable `.bme` projects, read-only `.bm`. The tools open a `.bm` read-only and ask for the `.bme` copy. `Ctrl+B` generates the `.bm` in Games. Projects are written in `/bme` (done); the Pi menu and the Lib tab scan `/bm` (done); left: the hardware check of `/bme` on the Pi and RGB30, the `/carts` retirement (the games folder of the Pi, the Ctrl+B Pi path decision). [tools](progress/tools.md), [lua](progress/lua.md)
- R15, R16, R26 - P3 - idea - Live reload from the PC, bm Code templates, mouse in nano8. [tools](progress/tools.md), [suite-code](design/suite-code.md)

## Editors and apps

- M48 - P2 - in progress - Mouse across the whole suite (USB and Bluetooth). Done 23 Oct: shared `U.list`/`U.col`/`U.scroll`, bm Write and the assistant cart, hub wheel, Sound text dialogs. Round 2 done: caret by click in the dialog lines, selection and word select in the SDK code views, sdk_b16 sound page and message, Pixel palette wheel. Left: try on the Pi and the RGB30 (not verified on hardware); I-beam cursor; dragging panels. [tools](progress/tools.md) `Mouse per program`
- M45 - P2 - in progress - bm Write: `.BMD` documents, TXT/MD/HTM/PDF export. A formatted document exported to PDF is identical on the PC (checked against the screen on the host). Left: Pi and RGB30 try, a real PDF viewer, Market. [tools](progress/tools.md)
- APP-8 - P2 - partly done - Multitab in bm Code for the source files of one project. Done: bm Code opens a `.bme` with `--@module name` lines as one tab per module (second row), `U.tabbar` in bmui is the shared tab bar, the project keeps ONE code section (same bytes, same build). Done when (what is left): bm SDK and the other editors with a code page show the same module tabs with `U.tabbar` (the SDK edits the whole code without tabs today); the modules are separate files in the project, each saved to its own file (this needs a file API for tools in `runtime.c`; today the modules are lines of the one code section). The tab layout is for readability only. [suite-code](design/suite-code.md), [tools](progress/tools.md)
- R39 - P3 - idea - 720×720 SDK on the RGB30. [suite-sdk](design/suite-sdk.md), [sdk-720](design/sdk-720.md)

## Games

- GAME-1 - P3 - in progress - Yharnam: close the open checks (§2), test the map on the Pi. After M42. [yharnam](progress/yharnam.md) §2-3, [tools](progress/tools.md)
- GAME-2 - P3 - in progress - Overbit: screens, quality, mech pilots from images. Benchmark on the Pi and the RGB30. After M41. [overbit](progress/overbit.md), [tools](progress/tools.md)
- GAME-3 - P3 - in progress - Game architecture and gameplay kit. Done: `lib.world`, `lib.sounds`, `lib.slots`, the test game `tests/gamekit` (`make test-gamekit`) and Pong on the kit (`make test-game-trace`). Open: spawn from an OBJECTS section (format change), kit cost on the Pi Zero W, more games on the kit. [games-architecture](design/games-architecture.md)

## Process and docs


---

## Baseline decisions in force

- `.bm` cartridge video (2026-09-26): 640×360 (16:9; ×2 on 720p, ×3 on 1080p); 320×180 optional; 360×360 on `.b16`. RGB565 color. Drawing under 25% of the frame; rendering in C, Lua only for logic.
- GPU and 3D options (2026-10-03): every GPU novelty that changes look or latency is an option in `bm/config.txt` or `gpu3d()`. Comparative measurements use Overbit. One FPS / ms / CPU / GPU overlay on F11.
- Lua stays on the 5.4 interpreter (to be confirmed): LuaJIT needs code generation at runtime on bare metal (`docs/PERFORMANCE.md`) and the `profile` hook does not fire in compiled code (`docs/B16.md`). Reconsider only if feedback shows games limited by Lua and not by drawing. First measure with `profile(true)` and move the heavy work to C or to the QPUs.
- Out of scope for now: multiplayer, Tiled, sinc/QPU, 24-bit audio, video and sample games such as Overbit and Yharnam do not block other work.
- Other open decisions: [`open-decisions.md`](open-decisions.md).

## Project constraints

- Lua on ARM1176 is slow: drawing in C, caches on, critical computation in the C runtime or on the QPUs.
- Closed-source Broadcom firmware: version pinned (`FW_REF`) to avoid video or mailbox regressions.
- SD integrity: hashes of downloaded files, protected FAT chains, isolated system folders.
- No real GPU in CI: QPU/V3D emulators on PC, protection timeouts, software rasterizer fallback.
- Development hardware (serial, power, controller): [`HARDWARE.md`](HARDWARE.md) *Development setup*.
