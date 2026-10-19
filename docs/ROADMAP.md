# bm Roadmap

The development plan for **bm** (BareMetal), a bare metal *fantasy console* for Raspberry Pi Zero W and PowKiddy RGB30. This file holds **goals, requirements and priorities**; the how lives in the area files under [`progress/`](progress.md), the history in `git log`. Closed milestones and historical text: commit `0e969b5`. The current target is the [Beta](#beta-next-milestone).

## How to read it

- **Level order** (developed top to bottom): 1 System → 2 Features and API → 3 Editors and apps → 4 Games (optional). A level does not block the next one, but at equal priority the higher level wins.
- **One row = one goal**, never a task. The row points to the area file (*Details* column): its `Open work` / `To do` section is where work orders come from. Do not copy the detail here.
- **ID**: `M##` (milestone) and `R##` (idea, promoted to milestone if decided) stay. New entries use `SYS-n`, `API-n`, `APP-n`, `GAME-n`. An ID is never reused or renumbered.
- **Pri**: `P0` blocks other work or breaks a project promise; `P1` next; `P2` later; `P3` unplanned idea.
- **State**: `in progress`, `to test on the Pi` (done on PC/QEMU), `in specification`, `to do`, `idea`.
- **Requirement**: the verifiable condition to close it ("done when"). If missing (`to be specified`), the row is not ready for a work order: it goes to `bm-planner`.
- **After**: dependencies (IDs). `-` = independent.

### How sync uses it

1. The supervisor (SPV) or `bm-planner` picks the highest-priority rows with no open dependencies, starting from the highest level.
2. It opens the indicated area file and takes the `Open work` entry to write the work orders in `sync/wo/<ID>.md` (format in [`SYNC.md`](../SYNC.md)); one operator = one function.
3. On closing it updates **this row** (state, or removal) and the area file's `Open work` section in the same commit. Hardware tests are left to the user and move the divergences from `untested` to `resolved` in `docs/spec/divergences.toml`.

---

## Beta (next milestone)

**What it is.** A closed beta: an indie developer, with only the bm suite and the documentation, writes a 2D or 3D game with a start menu, several stages and an ending, builds it to `.bm` and runs it on Pi Zero W and RGB30 with acceptable performance. Then the developer uses bm, finds its limits, and the next work starts from their feedback. The criteria below are those for **opening** it; each row points to the level rows (*After* column) where the work lives.

**Acceptance test.** A person who does not know the bm code starts from the guide, creates a game with title, 3 stages and ending (a 2D version and a 3D one) using only the suite's tools, tests it on both consoles and exports it. If the PC is needed only to copy files to the SD, that is fine; if reading the kernel code is needed, the test has failed.

| ID | Pri | Criterion to open the beta | Verifiable requirement | After |
|---|---|---|---|---|
| BETA-1 | P0 | Stable base | CI green on `bm-core`; no freeze in the logs of a long session on Pi and RGB30; a crash writes a correct `bootprev.txt` (hardware tests done by the user) | SYS-1, SYS-2, SYS-5 |
| BETA-2 | P0 | A complete game can be built with the suite | the acceptance test, 2D version | APP-4, APP-1, APP-6 |
| BETA-3 | P0 | The API is integrated in the editor | function help and lint in bm Code; completion with a single API source | APP-5 |
| BETA-4 | P1 | Acceptable performance in 2D and 3D | budget measured and published for Pi and RGB30; the test game at 60 fps (or at the frame rate declared in the budget) | API-1, M36, M35, M37, M41 |
| BETA-5 | P1 | The 3D version of the test | a multi-stage 3D game runs on the GPU (V3D on the Pi, Mali on the RGB30) within the budget | BETA-4, APP-2 |
| BETA-6 | P1 | Consistency between the two kernels | `python scripts/consistency.py gate` with no `open` or `untested` divergences touching menu, input, Games, SDK and saves (D01-D13, D28, D30-D36 and similar); the others accepted in writing | SYS-5, APP-1 |
| BETA-7 | P1 | Guide for testers | `GAME-GUIDE.md` and a one-page quick start lead to the first game. Tester feedback reaches the user through direct contact, outside bm: no channel in the product | APP-3 |

**Lua and LuaJIT (baseline decision, to be confirmed).** For the beta we stay on the Lua 5.4 interpreter: LuaJIT is neither needed nor advisable. One operation costs about 100 ns (about 140,000 per frame); drawing and 3D live in C and on the GPU, and `docs/PERFORMANCE.md` (discarded choices) already discarded the JIT because it requires generating code at runtime on bare metal; moreover the `profile` and breakpoint hook does not fire in compiled code (`docs/B16.md`, section on the debug hook). It is reconsidered only if tester feedback shows games limited by Lua and not by drawing: first measure with `profile(true)` and move the heavy work to C or to the QPUs.

**Out of the beta** (they stay in the levels, without blocking it): multiplayer, Tiled, sinc/QPU, 24-bit audio, video and sample games such as Overbit and Yharnam.

---

## 1. System (kernel, platforms, development tools)

| ID | Pri | State | Goal | Requirement | Details | After |
|---|---|---|---|---|---|---|
| SYS-1 | P0 | in progress | CI green on `bm-core` | all `ci.yml` jobs (host, qemu 1-4, rgb30) green on `bm-core`; `scripts/ci_audit.py` exit 0 | [tools](progress/tools.md), [sync](progress/sync.md) | - |
| SYS-2 | P0 | in progress | Reliable freeze report and SD logs | an intended reboot does not report a freeze; a crash writes a correct `bootprev.txt`; the password is masked in SD logs; the watchdog counts on the Pi Zero W | [system](progress/system.md) `Open work` | SYS-1 |
| SYS-3 | P1 | in progress | Pi tests from the PC (network, keys, screenshot) | `bm_net.py --shot/--keys` and the bridge tested on a Pi; R-NET3 with a limited test token | [tools](progress/tools.md) `Pi tests from the PC`, [pi-test](design/pi-test.md) | - |
| SYS-4 | P1 | to do | Measurable multi-agent coordination | ledger and `agent_audit.py` with baseline; work order scope checked by script | [sync](progress/sync.md), [sync-workflow](progress/sync-workflow.md) | - |
| M42 | P1 | in progress | Identical `.b16` profile on Pi and RGB30 | *Yharnam* `.b16` runs indistinguishably on Pi Zero W and RGB30, lint warnings in place | [b16](progress/b16.md) §2-3 | - |
| SYS-5 | P1 | in progress | Stable RGB30: panel after warm reboot, Start+Select, single core | warm reboot without black screen; Start+Select freezes and resumes as on the Pi | [platforms](progress/platforms.md) `Open work`, [multitasking](progress/multitasking.md) `Open` | - |
| M41 | P2 | in progress | RGB30 Mali-G52 GPU and Overbit in `.b16` | Overbit `.b16` at 60 fps with the Mali; 3D Bench reports GPU metrics | [graphics-mali](progress/graphics-mali.md) §M41, [platforms](progress/platforms.md) | M42 |
| SYS-6 | P2 | to do | Multitasking: measurements to redo, CPU stalls, download inside an app | `Open` in multitasking.md emptied or reduced to ideas; Beta R1/R2 roadmap | [multitasking](progress/multitasking.md) | - |
| R3, R4 | P2 | to verify | SD log and screenshot: close after console tests | SD log (D19) and `shot` tested on console; entries removed from `Open work` | [system](progress/system.md) | SYS-2, SYS-3 |
| R1, R2, R6, R8, R19, R20 | P3 | idea | Standalone console: WiFi from the pad, on-screen keyboard, web UI, DS4 rumble, BT pad, HDMI-CEC | to be specified | [system](progress/system.md) `Open work` | - |
| R49 | P3 | idea | Multitasking Ibrido & Scheduler Differenziato (Beta Rev 1 & Rev 2) | specifiche e architettura definite in multitasking.md | [multitasking](progress/multitasking.md) | SYS-6 |
| R22 | P3 | idea | Portable Pi Zero shell (GPIO, DPI/SPI display) | to be specified | [platforms](progress/platforms.md) | - |

## 2. Features and API (GPU, runtime, formats, audio)

| ID | Pri | State | Goal | Requirement | Details | After |
|---|---|---|---|---|---|---|
| M36 | P1 | in progress (almost done) | Vertices on the GPU: mesh loaded once, the QPUs transform and light | Overbit hero ring and stress test at double the ARM-only performance, at 60 fps | [v3d](progress/v3d.md) `To do` | - |
| M35 | P1 | to test on the Pi | ARM and GPU together: async queues | Overbit and stress benchmarks on the Pi with a clear gain, option on and off | [v3d](progress/v3d.md) | M36 |
| M37 | P1 | to test on the Pi | 2D and quality on the GPU (`gpu3d_2d`, `menu_scale=3`, bilinear) | step 16 of test `g`: identical ARM/GPU pixels; correct menu at 1080p | [v3d](progress/v3d.md) | M36 |
| M47 | P1 | in progress | Editable `.bme` projects, read-only `.bm` | the tools open a `.bm` read-only and ask for the `.bme` copy; `Ctrl+B` generates the `.bm` in Games | [tools](progress/tools.md), [lua](progress/lua.md) | - |
| M39 | P2 | in progress (on PC) | GPU 3: toward the V3D limit (compact attributes, 16-bit textures, ETC1) | stress test at double the triangles at 60 fps; Overbit with 10 bots at 60 fps in HIGH | [v3d](progress/v3d.md) `To do`, [lua](progress/lua.md) | M35 |
| M43 | P2 | in specification | Sprite stacking (`stack`, `zstack`, `stackv`) | tens of stacks at 60 fps on the Pi; conversion to mesh | [graphics](progress/graphics.md), [suite-pixel](design/suite-pixel.md) | - |
| M46 | P2 | in progress | Audio 2: 24-bit stereo synthesizer, PCM/WAV samples, wizard, `riff` | bm Sound games and tracks at 24 bit on Pi and RGB30 with samples and patterns | [audio](progress/audio.md), [audio-samples](progress/audio-samples.md) | - |
| M44 | P3 | to do | QPU programs (particles, Yharnam darkness, block audio) | particles and Yharnam darkness computed by the GPU with transparent fallback | [v3d](progress/v3d.md) `To do` §5, [graphics](progress/graphics.md) | M36, M41 |
| R40-R42 | P3 | idea | Profiler, sinc/QPU scaling, hero impostors | Pi measurements justifying each one | [v3d](progress/v3d.md) | M36 |
| R7, R27, bmnet | P3 | idea | UTF-8 accents in `print()`, battery and autosave, network multiplayer | to be specified | [runtime](progress/runtime.md) `Open work`, [system-net](progress/system-net.md) | - |
| R9, R17, R21, R25, R31-R35 | P3 | idea | WAV from SD, MIDI import, GPIO audio, PC audio, samples and riff | to be specified | [audio-samples](progress/audio-samples.md), [audio-output](progress/audio-output.md) | M46 |
| API-1 | P1 | to do | 2D and 3D performance budget published and measured on Pi and RGB30 | a table (sprites, tiles, meshes, particles at 60 fps) with real measurements on both platforms; the RGB30 has no numbers today; `profile(true)` explained in the guide | [PERFORMANCE](PERFORMANCE.md), [STRESS](STRESS.md), [lua](progress/lua.md) | M36, M41 |

## 3. Editors and apps (the bm suite)

| ID | Pri | State | Goal | Requirement | Details | After |
|---|---|---|---|---|---|---|
| APP-1 | P1 | in progress | bm SDK on RGB30 (Dev, pad, Games with `/carts`) | console tests for every `Open work` entry; divergences D28 etc. to `resolved` | [tools](progress/tools.md) `bm SDK (RGB30)` | M47 |
| M48 | P2 | in progress | Mouse across the whole suite (USB and Bluetooth) | every program selects, draws and navigates with the pointer without a keyboard | [tools](progress/tools.md) | - |
| M45 | P2 | in progress | bm Write: `.BMD` documents, TXT/MD/HTM/PDF export | a formatted document exported to PDF identical on the PC | [tools](progress/tools.md) | - |
| APP-2 | P2 | to do | 3D maps in bm SDK; bm Pixel for stacks (M43 step 3) | `Open work` steps closed with a 3D map saved and reloaded | [tools](progress/tools.md) `Mappe 3D in bm SDK`, [suite-sdk](design/suite-sdk.md), [suite-pixel](design/suite-pixel.md) | M43 |
| APP-3 | P3 | to do | PDF manuals and project structure | EN/IT manuals aligned; layout of `docs/PROJECT-LAYOUT.md` respected | [tools](progress/tools.md) `PDF manuals`, `Project layout` | - |
| R11, R37, R38, R39 | P3 | idea | Tiled, map palettes, 16×16 wizard, 720×720 SDK | to be specified | [suite-sdk](design/suite-sdk.md), [sdk-720](design/sdk-720.md) | - |
| R15, R16, R26 | P3 | idea | Live reload from the PC, bm Code templates, mouse in nano8 | to be specified | [tools](progress/tools.md), [suite-code](design/suite-code.md) | - |
| APP-4 | P0 | to do | Complete game template: start menu, several stages, ending, save | from bm SDK a new "Stages" project builds to `.bm` and runs with title, 3 stages, ending screen and record; it uses the state machine of `games-architecture.md` | [games-architecture](design/games-architecture.md), [tools](progress/tools.md) | APP-1 |
| APP-5 | P0 | to do | API integrated in the editor: help on the function under the cursor and lint | in bm Code a key shows the function's signature and description; a check flags nonexistent API names; the API list has a single source generated from `docs/api/` and also used by `predict` | [tools](progress/tools.md), [PREDICT](PREDICT.md), [API](API.md) | - |
| APP-6 | P1 | to do | Develop a game without a PC: resources and projects manageable on the device | on RGB30 and Pi a project with sprite, map, sounds and code is created, saved and built without the PC (today resource files are PC-only and the RGB30 has no Lib tab) | [tools](progress/tools.md) `bm SDK (RGB30)`, [CONSISTENCY](spec/CONSISTENCY.md) | APP-1 |

## 4. Games (optional)

| ID | Pri | State | Goal | Requirement | Details | After |
|---|---|---|---|---|---|---|
| GAME-1 | P3 | in progress | Yharnam: open checks, map and performance on the Pi | the §2 checks closed; map tested on the Pi | [yharnam](progress/yharnam.md) §2-3, [tools](progress/tools.md) | M42 |
| GAME-2 | P3 | in progress | Overbit: screens, quality, mech pilots from images | `Open work` entries closed; benchmark on the Pi and the RGB30 | [overbit](progress/overbit.md), [tools](progress/tools.md) | M41 |
| GAME-3 | P3 | idea | Game architecture and gameplay kit | a test game uses the kit without duplicated code | [games-architecture](design/games-architecture.md) | - |

## Post-beta milestones

| ID | Prio | Status | Item | Done when |
|---|---|---|---|---|
| WEB-1 | P2 | idea | bm in the browser: the portable runtime (Lua runtime, gfx16, audio, bmlib) built to WebAssembly with canvas, keyboard/gamepad and WebAudio, to let people try the platform and promote it | a static page (artifact or GitHub Pages, up to 16 MB with cartridges embedded) runs a `.bm` game at 360x360; first step is a read-only feasibility WO on how much of `src/bm/runtime.c` depends on the kernel |

---

## Baseline decisions in force

- **`.bm` cartridge video (2026-09-26)**: 640×360 (16:9; ×2 on 720p, ×3 on 1080p); 320×180 optional; 360×360 on `.b16`. RGB565 color. Drawing under 25% of the frame; rendering in C, Lua only for logic.
- **GPU and 3D options (2026-10-03)**: every GPU novelty that changes look or latency is an option in `bm/config.txt` or `gpu3d()`; comparative measurements on Overbit; a single FPS / ms / CPU / GPU overlay on F11.
- Other open decisions: [`open-decisions.md`](open-decisions.md).

## Project constraints

- Lua on ARM1176 is slow: drawing in C, caches on, critical computation in the C runtime or on the QPUs.
- Closed-source Broadcom firmware: version pinned (`FW_REF`) to avoid video or mailbox regressions.
- SD integrity: hashes of downloaded files, protected FAT chains, isolated system folders.
- No real GPU in CI: QPU/V3D emulators on PC, protection timeouts, software rasterizer fallback.
- Development hardware (serial, power, controller): [`HARDWARE.md`](HARDWARE.md) *Development setup*.
