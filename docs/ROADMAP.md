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
- API-1 - P1 - to do - Publish a 2D and 3D performance budget measured on Pi and RGB30 (sprites, tiles, meshes, particles at 60 fps). The RGB30 has no numbers today. Explain `profile(true)` in the guide. [PERFORMANCE](PERFORMANCE.md), [STRESS](STRESS.md), [lua](progress/lua.md)
- BETA-5 - P1 - to do - A multi-stage 3D game runs on the GPU (V3D on the Pi, Mali on the RGB30) within the budget. Needs API-1 and APP-2. [v3d](progress/v3d.md)
- M41 - P2 - in progress - RGB30 Mali-G52 GPU and Overbit in `.b16`. Done when Overbit `.b16` runs at 60 fps with the Mali and 3D Bench reports GPU metrics (device runs H1, H6 open). [graphics-mali](progress/graphics-mali.md), [platforms](progress/platforms.md)
- M39 - P2 - in progress - Toward the V3D limit. Pi 21 Oct: compact attributes +3.8% (kept off), 16-bit textures and ETC1 no gain (kept off). Open: stress test at double the triangles at 60 fps; Overbit with 10 bots at 60 fps in HIGH. [v3d](progress/v3d.md) `To do`, [lua](progress/lua.md)
- API-3 - P2 - to do - Triangle throughput: make `gpu3d_queue=1` the default after the user's Pi runs. Chain jobs (CT0/CT1) only if a measurement shows the GPU idle between jobs. `gpu3d_attr` stays off. [triangle-throughput](design/triangle-throughput.md), [v3d](progress/v3d.md)
- API-2 - P2 - to do - Cel shader stage 3 (V3D vertex programs) and stage 4 (outline by inverted hull). The Mali uses the ARM path. [v3d](progress/v3d.md)
- M43 - P2 - in progress - Sprite stacking (`stack`, `stackv`, `zstack` done, CPU only). Measure stacks with a benchmark on the Pi Zero W (not measured); tens of stacks at 60 fps. [graphics](progress/graphics.md), [suite-pixel](design/suite-pixel.md)
- M44 - P3 - in progress (steps 1-3 on the emulator) - QPU programs (particles, Yharnam darkness, block audio). Done on the emulator: driver, queue, probe, fallback, particle step, fade by levels. Open: runtime calls, GPU drawing, 3D Bench profile, Pi test. Step 4: re-run the `v3d_init` register block after a QPU reset and lock `v3d_qpu_run` (needs a Pi run). [v3d](progress/v3d.md) `To do` §5, [graphics](progress/graphics.md)
- R40-R42 - P3 - idea - Profiler, sinc/QPU scaling, hero impostors. Do R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain. [v3d](progress/v3d.md)

## Video, recording and audio

- API-4 - P2 - to do - Video recording (VID-REC) next slices: MJPEG or RLE encoder for 30 fps (SD write is 2.50 MB/s, raw is a NO GO), real 8-bit palette, audio.
- M46 - P2 - in progress - Audio 2: 24-bit stereo synthesizer, PCM/WAV samples, wizard, `riff`. Done when bm Sound games and tracks run at 24 bit on Pi and RGB30 with samples and patterns. [audio](progress/audio.md), [audio-samples](progress/audio-samples.md)
- R9, R17, R21, R25, R31-R35 - P3 - idea - WAV from SD, MIDI import, GPIO audio, PC audio, samples and riff. After M46. [audio-samples](progress/audio-samples.md), [audio-output](progress/audio-output.md)

## Network and web

- SYS-3 - P1 - in progress - Pi tests from the PC (network, keys, screenshot). Done when `bm_net.py --shot/--keys` and the bridge are tested on a Pi, and R-NET3 works with a limited test token. [tools](progress/tools.md) `Pi tests from the PC`, [pi-test](design/pi-test.md)
- SYS-6 - P2 - to do - Multitasking stages 3 (SD writer lock; `file_write` stops the frame until then), 4 (keep-alive) and 5 (Range resume `.part`). Redo the measurements; fix CPU stalls and the download inside an app. [multitasking](progress/multitasking.md)
- WEB-1 - P2 - in progress - bm in the browser: build the portable runtime (Lua runtime, gfx16, audio, bmlib) to WebAssembly with canvas, keyboard/gamepad and WebAudio. Done when a static page (artifact or GitHub Pages, up to 16 MB with cartridges embedded) runs a `.bm` game at 360x360. Study done ([bm-web-player](design/bm-web-player.md)); stage 1 done (a JavaScript `.bm` reader in the mock page). Next: a wasm toolchain (the user decides the install), then a web runner with a frame-step entry.
- R49 - P3 - idea - Hybrid multitasking and differentiated scheduler (revisions 1 and 2). After SYS-6. [multitasking](progress/multitasking.md)
- R7, R27, bmnet - P3 - idea - UTF-8 accents in `print()`, battery and autosave, network multiplayer. [runtime](progress/runtime.md) `Open work`, [system-net](progress/system-net.md)

## Platforms and kernel

- SYS-2 - P0 - in progress - Reliable freeze report and SD logs. An intended reboot does not report a freeze. A crash writes a correct `bootprev.txt` (needs the Pi run). [system](progress/system.md) `Open work`
- BETA-6 - P1 - to do - Consistency between the two kernels. `python scripts/consistency.py gate` shows no `open` or `untested` divergence that touches menu, input, Games, SDK and saves (D01-D13, D28, D30-D36 and similar). Accept the others in writing.
- M42 - P1 - in progress - Identical `.b16` profile on Pi and RGB30. Done when *Yharnam* `.b16` runs the same on both and the lint warnings are in place. [b16](progress/b16.md) §2-3
- SYS-7 - P2 - to do - Evaluate the security findings of `docs/OS-CAPABILITIES.md`: `lua_load(..., NULL)` at `runtime.c:9438` may accept bytecode; the default net password is 6 digits from `srand(timer_ticks())` (`netcon.c:183`) and goes plaintext on TCP 3334; carts can `ls` the SD and `cart_read` any `.bm` without a prompt; FAT dates are fixed at 2026-01-01 (`fat.c:682`); docs disagree (`docs/progress/b16.md:16` says 4 MiB heap, code 64 MiB; RGB30 menu framebuffer 360x360). [OS-CAPABILITIES](OS-CAPABILITIES.md)
- SYS-8 - P2 - to do - Single-bm and adaptive screen, by the stages of the design: remove the 8 MiB limits (and palette/audio limits), per-system input chips, aspect modes, compat header in bytes 120..127 (change the `memset` in `project.c:143,178,225`). Decided 22 Oct: old `.b16` = Pi + RGB30; old `.bm` = Pi only; grey not hidden, start after confirm; audio max 8M; pad mouse; bars G1; 720x405 mode; `.bm` for all. [single-bm-and-adaptive-screen](design/single-bm-and-adaptive-screen.md)
- R3, R4 - P2 - to verify - SD log (D19) and screenshot (`shot`): close after the console tests. After SYS-2 and SYS-3. [system](progress/system.md)
- R1, R2, R6, R8, R19, R20 - P3 - idea - Standalone console: WiFi from the pad, on-screen keyboard, web UI, DS4 rumble, BT pad, HDMI-CEC. [system](progress/system.md) `Open work`
- R22 - P3 - idea - Portable Pi Zero shell (GPIO, DPI/SPI display). [platforms](progress/platforms.md)

## Tools and SDK

- SYS-1 - P0 - in progress - CI green on `bm-core`. All `ci.yml` jobs (host, qemu 1-4, rgb30) are green and `scripts/ci_audit.py` exits 0. [tools](progress/tools.md)
- M47 - P1 - in progress - Editable `.bme` projects, read-only `.bm`. The tools open a `.bm` read-only and ask for the `.bme` copy. `Ctrl+B` generates the `.bm` in Games. [tools](progress/tools.md), [lua](progress/lua.md)
- APP-7 - P2 - to do - Dev kit for weight and performance. Weight report of a `.bme`/`.bm` (bytes per section and asset, biggest first). Performance analysis during development (frame time, draw and 3D cost, memory, SD reads), reusing the dev kit and bench tools. Compat policy table per kernel (show, grey, hide) as editable data.
- R15, R16, R26 - P3 - idea - Live reload from the PC, bm Code templates, mouse in nano8. [tools](progress/tools.md), [suite-code](design/suite-code.md)

## Editors and apps

- APP-5 - P0 - to do - API integrated in the editor. In bm Code a key shows the signature and description of the function under the cursor. A check flags nonexistent API names. The API list has a single source generated from `docs/api/` and also used by `predict`. [tools](progress/tools.md), [PREDICT](PREDICT.md), [API](API.md)
- APP-6 - P1 - to do - Develop a game without a PC. On RGB30 and Pi the SDK makes a project with sprite, map, sounds and code, saves it and builds it. The resources are made inside the SDK pages (the Lib tab is closed by decision). Acceptance test: a person who does not know the bm code makes a game with title, 3 stages and ending (2D and 3D) with the suite tools only. [tools](progress/tools.md) `bm SDK (RGB30)`, [CONSISTENCY](spec/CONSISTENCY.md)
- M48 - P2 - in progress - Mouse across the whole suite (USB and Bluetooth). Every program selects, draws and navigates with the pointer without a keyboard. [tools](progress/tools.md)
- M45 - P2 - in progress - bm Write: `.BMD` documents, TXT/MD/HTM/PDF export. A formatted document exported to PDF is identical on the PC. [tools](progress/tools.md)
- APP-2 - P2 - to do - 3D maps in bm SDK; bm Pixel for stacks (M43 step 3). Done when a 3D map is saved and reloaded. [tools](progress/tools.md) `Mappe 3D in bm SDK`, [suite-sdk](design/suite-sdk.md), [suite-pixel](design/suite-pixel.md)
- R11, R37, R38, R39 - P3 - idea - Tiled, map palettes, 16×16 wizard, 720×720 SDK. [suite-sdk](design/suite-sdk.md), [sdk-720](design/sdk-720.md)

## Games

- GAME-1 - P3 - in progress - Yharnam: close the open checks (§2), test the map on the Pi. After M42. [yharnam](progress/yharnam.md) §2-3, [tools](progress/tools.md)
- GAME-2 - P3 - in progress - Overbit: screens, quality, mech pilots from images. Benchmark on the Pi and the RGB30. After M41. [overbit](progress/overbit.md), [tools](progress/tools.md)
- GAME-3 - P3 - idea - Game architecture and gameplay kit. A test game uses the kit without duplicated code. [games-architecture](design/games-architecture.md)

## Process and docs

- BETA-7 - P1 - to do - Manuals up to date. Each manual in `manuals/content/` (English and Italian) matches the code, and `python scripts/manuals.py status` shows no open item. No tester guide and no feedback channel: users send feedback to the owner in private. After APP-3.
- APP-3 - P3 - to do - PDF manuals and project structure. EN/IT manuals aligned; layout of `docs/PROJECT-LAYOUT.md` respected. [tools](progress/tools.md) `PDF manuals`, `Project layout`
- SYS-4 - P1 - to do - Measurable multi-agent coordination. Ledger and `agent_audit.py` with a baseline; work order scope checked by script. [archive](archive/sync/README.md)

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
