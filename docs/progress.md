# bm progress (the one index)

**Start here.** This is the only file `CLAUDE.md` sends you to: every other doc is reached
from a line below. Find the line, `Grep -n '^##'` the file it names, read one section.

Working notes on the **current** state of bm (BareMetal), for whoever changes the code: how
each area is built today, what is open, and the rules that keep it working. One topic lives
in one file; the others link to it.

Not here: history (git log), measurements ([`docs/PERFORMANCE.md`](PERFORMANCE.md), `docs/STRESS.md`,
[`docs/GPU-BENCHMARK-REPORT.md`](GPU-BENCHMARK-REPORT.md), `docs/BENCH3D.md`), plans and milestones
([`docs/ROADMAP.md`](ROADMAP.md)), the game API ([`docs/API.md`](API.md)), working rules
(`CLAUDE.md`).

## Subsystems

| File | Scope | Main code |
|---|---|---|
| [system](progress/system.md) (+ [net](progress/system-net.md), [beta1-c4-diag](progress/beta1-c4-diag.md), [menu and services](progress/system-menu.md)) | boot, MMU, IRQ, fibers, crash reports, USB, SD/FAT, Bluetooth, WiFi, network, menu and kernel services | `src/boot/`, `src/arch/`, `src/kernel/`, `src/drivers/`, `src/usb/`, `src/fs/`, `src/bt/`, `src/wifi/`, `src/net/` |
| [multitasking](progress/multitasking.md) | cooperative fibers of the menu, shares, background jobs, measurements; later multicore | `src/kernel/fiber.c`, `src/arch/fiber.S` |
| [multicore](progress/multicore.md) | RGB30 quad core schedule: secondary cores (M), Mali bm3D end (G), use of four cores (V) | `src/rgb30/start.S`, `src/rgb30/mmu.c` |
| [multitasking-stages](progress/multitasking-stages.md) | stages 1-5: measurement, hash steps, SD writer lock, keep-alive, Range resume | `src/fs/sdlock.c`, `src/bm/webfile.c`, `src/net/http.c` |
| [graphics](progress/graphics.md) | framebuffer, 2D, software 3D, V3D driver, rendering tests | `src/drivers/fb.c`, `src/bm/gfx16.c`, `src/bm/r3d.c`, `src/gpu/` |
| [herocart](progress/herocart.md) | Hero Stress cart: heroes in a plaza, live 3D options, cost panel | `carts/herocart/`, `tests/herocart/` |
| [graphics-mali](progress/graphics-mali.md) | the RGB30's Mali-G52: bring-up, descriptors, shaders, triangle | `src/rgb30/mali.c`, `src/rgb30/mali_desc.c`, `tools/mali/` |
| [v3d](progress/v3d.md) (+ [notes](progress/v3d-notes.md), [graphics keys](progress/v3d-flags.md), [formats](progress/v3d-formats.md), [Pi measurements](progress/v3d-pi-measurements.md)) | V3D driver on the Pi Zero W: async jobs, cache, binner memory, open work and Pi measurements | `src/gpu/v3d.c`, `src/gpu/gpu3d.c`, `src/bm/runtime.c` |
| [lua](progress/lua.md) | Lua VM, cartridge runtime, built-in libraries, dev kit, profiler, session report | `third_party/lua/`, `src/bm/runtime.c`, `src/bm/require.c`, `src/script/`, `src/bm/profile.c` |
| [audio](progress/audio.md) (+ [output](progress/audio-output.md), [samples](progress/audio-samples.md), [WAV and import](progress/audio-samples-wav.md)) | synthesizer, sound bank and player, presets, outputs, riff, music assistant | `src/audio/`, `src/script/riff.lua`, `src/ai/music.c`, `src/rgb30/rk_audio.c` |
| [platforms](progress/platforms.md) | Pi Zero W / Pi 1, Pi Zero 2 W (off), RGB30, `bmhost`, QEMU | `src/rgb30/`, `rgb30.mk`, `tests/host/`, `tests/qemu_test.py` |
| [single-bm](progress/single-bm.md) | one cartridge type, the targets of the header, the faded cover, Market entries | `src/bm/format.c`, `docs/spec/compat-policy.toml`, `src/kernel/library.c` |
| [overbit](progress/overbit.md), [yharnam](progress/yharnam.md) | the games: screens and quality, bench flags, assets, verification | `carts/overbit/`, `carts/yharnam/` |
| [smw](progress/smw.md) | native port of Super Mario World for the RGB30 (personal use): ROM extractor, emulator capture, physics reference | `scripts/smw_extract.py`, `scripts/smw_capture.py` |
| [smw-guide](progress/smw-guide.md) | PDF guide of the SMW port and its redrawn guide edition | `manuals/content/en/smw-guide.md`, `carts/smw/guide/build.py` |
| [tools](progress/tools.md) (+ [assistant](progress/tools-assistant.md), [suite](progress/tools-suite.md), [code templates](progress/tools-code-templates.md)) | the suite, editors, PC scripts, Market, CI (design: [suite specs](design/)) | `carts/`, `src/ai/`, `scripts/`, `tools/` |
| [tropic-trail-final-report](progress/tropic-trail-final-report.md) | final report of the 3D video series: deliveries, grades of the operators, estimated vs actual times, lessons | `video/3d/` |
| [video](progress/video.md) | clips for cartridges (container, MJPEG, RLE+delta) and chunked screen recording; study and spikes | `src/bm/jpeg.c`, `docs/bench/video-feasibility-2026-10.md` |
| [devkit-weight](progress/devkit-weight.md) | dev kit: weight report of a `.bm`/`.bme`, performance plan from the docs, compat policy table | `scripts/bm_weight.py`, `docs/spec/compat-policy.toml` |

## Which section to open

Open the file of the area and `Grep -n '^##'` it; read only the section you need. Every area
file ends with *Open work* and *Rules (do not break)*.

- Boot, MMU, cache, IRQ, fibers, crashes, USB, SD/FAT, Bluetooth, WiFi, menu: [system](progress/system.md).
- Framebuffer, 2D, software 3D (`r3d`), V3D, rendering tests: [graphics](progress/graphics.md); Mali: [graphics-mali](progress/graphics-mali.md).
- Menu fibers, shares, background jobs, SD/network measurements: [multitasking](progress/multitasking.md).
- V3D driver on the Pi (async jobs, cache, binner memory, Pi measurements): [v3d](progress/v3d.md).
- VM, runtime, built-in libraries, dev kit: [lua](progress/lua.md).
- Synth, mixer, player, presets, riff, music assistant: [audio](progress/audio.md); output depth and dither: [audio-output](progress/audio-output.md); samples, effects, WAV: [audio-samples](progress/audio-samples.md), [audio-samples-wav](progress/audio-samples-wav.md).
- Pi Zero W, Pi Zero 2 W, RGB30, PC and QEMU: [platforms](progress/platforms.md).
- Suite (SDK, Studio, Animator, Mesh, Pixel, Code, Sound, Write): [tools](progress/tools.md) (future suite evolution: `docs/design/suite-*.md`; modelling plan: [modelling-suite](design/modelling-suite.md)).
- Games (Overbit, Yharnam): [overbit](progress/overbit.md), [yharnam](progress/yharnam.md) (gameplay architecture: [`docs/design/games-architecture.md`](design/games-architecture.md)).
- Input standard for games (actions on buttons, RGB30 and Pi): [`docs/design/games-input-standard.md`](design/games-input-standard.md).
- Game API reference (not progress): [API.md](API.md) → `docs/api/<area>.md`.
- Next tasks and open decisions of the whole repository: [`docs/NEXT.md`](NEXT.md).

## Navigation (how to read, where things are)

- [reading](progress/reading.md): the search command per big file, then `Read` offset/limit.
- [runtime](progress/runtime.md): sections of `src/bm/runtime.c` by banner, to grep.

## Reference docs (not progress)

Performance: [`docs/PERF-BUDGET.md`](PERF-BUDGET.md) (what a frame holds), [`docs/PERFORMANCE.md`](PERFORMANCE.md), [`docs/GPU-BENCHMARK-REPORT.md`](GPU-BENCHMARK-REPORT.md) · GPU: `docs/DRIVERS.md`, `docs/BENCH3D.md` · RGB30: `docs/RGB30.md` · OS limits and comparison with Linux/Windows: [`docs/OS-CAPABILITIES.md`](OS-CAPABILITIES.md) ·
resources `BMRES`: `docs/RISORSE.md` · project layout: `docs/PROJECT-LAYOUT.md` · formats: `docs/BM-FORMAT.md`, `docs/FORMATS-AUDIT.md` · program structure: `docs/APP-ARCHITECTURE.md` · consistency and the 1.0.0 gate: `docs/spec/CONSISTENCY.md` · UI reference: [`docs/spec/ui-reference.md`](spec/ui-reference.md) · open decisions: `docs/open-decisions.md` · riff: `docs/RIFF.md` · Predict and pad: `docs/PREDICT.md`,
`docs/PADTYPE.md` · assistant: `src/ai/kb/README.md`, `AI.md` · Market: `market/README.md` ·
game guide: `docs/GAME-GUIDE.md` · SDK and tools: `sdk/README.md` · design RFCs: `docs/design/`. All are large: `Grep -n '^#'` first.

## Plan and priorities

[`docs/ROADMAP.md`](ROADMAP.md) is the one plan: levels (system, features and API, editors and apps, games), priority, state, requirement and the area file to open for each line. It replaces the milestone list that used to be here.

## Keeping these files

- State only: what is true in the code now, with file and symbol names to grep. No dates,
  no "we did", no measurements, no closed milestones.
- A change that makes a line false fixes or removes it in the same commit.
- Each file stays around 80–120 lines; detail belongs in the topic doc it links. A file that
  grows past that is split by topic (`<area>-<topic>.md`) and listed in the table and in
  *Which section to open*.
