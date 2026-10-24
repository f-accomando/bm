# bm web player: run bm in a web browser (WEB-1)

Status: stages 1 to 6 built, tested and release page ready. This page lists what exists, what is missing, the stages, the risks
and the test plan. It does not change the kernel API.

## 1. What we already decided (recovered, not redone)

- `docs/ROADMAP.md` item WEB-1: build the portable runtime (Lua runtime, gfx16, audio, bmlib) to
  WebAssembly, with canvas, keyboard/gamepad and WebAudio. Target: a static page with the cartridge
  embedded (up to 16 MB) that runs a `.bm` game at 360x360. The first step is a read-only study of how
  much of `src/bm/runtime.c` depends on the kernel. This page is that study.
- `docs/design/triage-and-roadmap-cleanup.md` (PR 88): WEB-1 is PARTIAL. The mock under `web/` is a static
  UI with no kernel code.
- `web/README.md`: the page calls `window.bmLauncher.start(cartUrl)`. The future player must start the
  WebAssembly runtime and attach it to `canvas#bm-canvas`. The page loads React 18.3.1 (MIT) from cdnjs and
  sends no data.
- `docs/progress/tools.md`, "Web player mock": React has no SRI. The 24 PNG previews are placeholders. A
  licence policy for real assets is open.
- `docs/design/web-browser-cart.md` (bm Web) is a different feature: it is a browser that runs *on* bm. It
  does not run bm in a browser.

## 2. What already works on the PC

`make bmhost` (`tests/host/bmhost.c`, `tests/host/stubs.c`) builds the console's own runtime with a plain
host C compiler and runs a `.bm` cartridge. It links `src/bm/runtime.c`, `gfx16.c`, `r3d.c`, `world3d.c`,
`map3d.c`, `format.c`, the sound player and synthesizer (`src/audio/*`), the PNG, JPEG and WebP decoders,
the Lua sources in `third_party/lua`, and the Lua libraries `bmlib`, `bm3d` and `bmui`. `tests/host/stubs.c`
replaces the kernel services:

| Service | How bmhost replaces it | Fit for the web |
|---|---|---|
| Frame (`fb_flip`) | calls `host_frame(px, w, h, stride)` with RGB565 pixels | direct: convert to RGBA, `putImageData` |
| Sound | `n8snd_mix_float` gives 48 kHz stereo floats to `host_audio` | direct: AudioWorklet ring buffer |
| Input | `host.pad` HID bits, sticks, keys | direct: map keyboard and Gamepad API to the same bits |
| Clock | virtual: frame n is at n/60 s | use a 60 Hz step from `requestAnimationFrame` |
| SD card | a host directory (`--sd`) | in-memory file system (cartridge, `bm/save`, `config.txt`) |
| 3D | no V3D: the ARM rasterizer `r3d.c` draws | the same path works; no GPU code is needed |
| Network, recorder, assistant | stubs that fail | keep as stubs |

`tests/host/libs.S` links the Lua libraries with `.incbin`. This is an assembler file. A wasm build needs a
generated C array instead.

## 3. What is missing

1. A WebAssembly toolchain. Done: the user allowed a local install. Emscripten 6.0.13 (emsdk) is in
   `~/emsdk` in WSL, outside the repository. Run `source ~/emsdk/emsdk_env.sh` before `make bmhost-wasm`.
2. A frame-step entry. `bmhost.c` runs a blocking loop. A browser needs `bmweb_frame(ms)` called from
   `requestAnimationFrame`. This entry belongs in a new runner file for the web, not in the kernel.
3. Fibers. `src/kernel/fiber.c` is cooperative and uses its own stacks. wasm cannot switch stacks without
   Asyncify. bmhost runs a cartridge's `_update` and `_draw` loop without fibers, so games are not blocked.
   Calls that wait (SD reads, `net_wait_step`) need a stub or Asyncify.
4. A loader. The page reads a `.bm` or `.b16` (file input, drag and drop, or `fetch`) and copies it to the
   runtime's memory. Stage 1 reads the container in JavaScript (`web/bmcart.js`).
5. Input mapping, audio output and a canvas scaler in JavaScript.
6. A licence policy for cartridges that the site hosts (the SPDX `license` key of the INFO section).

## 4. Design

- **Cartridge.** The page gets the bytes and checks them with `bmcart.js` (magic, version, size, pixel
  format, CRC-32, sections). It shows title, author, cover and sections. Then it gives the bytes to the
  runtime. The runtime parses them again with `format.c`: the C parser stays the single authority.
- **Rendering.** A cartridge is 640x360, 480x270, 320x180, 256x256 or 360x360 in RGB565. The page converts
  each frame to RGBA in a typed array and draws it on `canvas#bm-canvas` with integer scaling and
  `image-rendering: pixelated`. The first target is a `.b16` cartridge (360x360), as in the ROADMAP row.
- **Input.** Keyboard and Gamepad API map to the `HID_*` bits of `src/usb/hid.h` (D-pad, A, B, X, Y, L1,
  R1, L2, R2, Start, Select). Keys per device are in `docs/spec/ui-reference.md`.
- **Audio.** The runtime mixes at 48 kHz in blocks of `AUDIO_CHUNK` (256) samples. An AudioWorklet reads a
  ring buffer. A browser starts audio only after a click: the "Start bm" button is that click.
- **3D.** V3D (Pi) and Mali (RGB30) are hardware paths and are out of scope. The web uses the ARM
  rasterizer `r3d.c`. Its speed in wasm is not measured. A later WebGL backend would implement
  `r3d_backend_t` (`gpu3d_backend()` is the hook). That is kernel-side design and is only listed here.
- **Storage.** `bm/save/*.SAV` and `bm/config.txt` live in the in-memory file system. IndexedDB keeps them
  between visits, written when a game saves. Nothing leaves the browser.
- **Size.** The wasm module is not built yet, so its size is not verified. The ROADMAP limit is 16 MB for a
  page with cartridges. Measure at stage 2.
- **Licences.** Lua is MIT. The runtime sources are original code. React loads from cdnjs: vendor it or add
  SRI before release (open item in `docs/progress/tools.md`). A sample cartridge must carry an SPDX licence.

## 5. Stages

| # | Stage | Result | Needs |
|---|---|---|---|
| 1 | Cartridge reader in JavaScript (done) | `web/bmcart.js`; the mock shows title, author, cover, sections, CRC; `tests/webplayer/test_bmcart.js` | Node |
| 2 | wasm build of the runtime (done) | `make bmhost-wasm` builds `bmhost` to `build/web/bmhost.wasm` (1.5 MB, 0.57 MB gzip); `make test-wasm` runs a cartridge under Node and the frames equal the PC build byte for byte | emsdk |
| 3 | Canvas, input, sound (done) | `make web-player` writes `build/web/player/`; the page plays a `.bm` from the file input: canvas, keyboard, Gamepad API, AudioWorklet | stage 2 |
| 4 | Check of sound and speed (clock and sound queue tested in Node; listening and the visible-tab rate left) | `web/bmring.js` holds the 60 Hz clock (`BmPacer`) and the worklet queue (`BmRing`); `make test-webplayer` tests both with a fake clock. Listen to `sfx()` and `music()` and read `bmLauncher.stats` in a visible tab | the user, or a visible browser |
| 5 | Storage and 3D (done) | IndexedDB saves; a 3D cartridge on `r3d.c` | stage 4 |
| 6 | Release page (done) | SRI on cdnjs React, size check (5 MB total with 3 cartridges), preset selector (Chaos Kitchen, bm Demo, Stages 3D), WebAudio synthesizer previews, clean build in `build/web/player/` | static server / GitHub Pages |

## 6. Risks

- **Speed.** The Pi Zero W is the benchmark. A PC browser is much faster. A web run proves nothing about the
  Pi. Never use it as a benchmark.
- **Fibers and blocking calls** in `runtime.c` may need Asyncify or stubs.
- **Section data casts** (`docs/BM-FORMAT.md`, "Not verified"): wasm allows unaligned access, so this is
  safe here. Pointers are 32 bit, as on the Pi.
- **Autoplay rules** block audio until the user clicks.
- **Content.** The site must not host a cartridge without a licence.
- **Kernel changes (listed, not done).** The web runner may need `runtime.c` to expose one frame step. Check
  first whether the loop of `bmhost.c` can be copied as it is.

## 7. Test plan

Stage 1 (done): `node tests/webplayer/test_bmcart.js`, or `make test-webplayer` (it skips when Node is
missing). The test checks the CRC-32 check value, a cartridge built in memory (title, author, size, `.b16`
flag, sections, INFO lines, cover, Lua), a bad magic, a resource file, a short file, a missing LUA section, a
wrong size, a wrong CRC, and a cartridge packed by `scripts/mkbm.py`. The page was also loaded in the
browser pane, and a file was opened through the file input.

Stage 2 (done): `make test-wasm` (`tests/webplayer/run_wasm.py`). It packs `tests/webplayer/cart.lua`, runs
`build/web/bmhost.js` under Node and checks that the run ends ok, that the two frames match the SHA-256 in
`tests/webplayer/expected.sha256`, and, when `build/host/bmhost-bin` exists, that they are byte-equal to the PC
build. It skips when Node is missing. Run result on 22 Oct: ok, equal to the PC build.

## 8. Stage 3: the browser runner

- `web/runner/bmweb.c` plays a cartridge with `bm_play()` and the PC stubs. It is linked with `-sASYNCIFY`:
  each frame flip calls a JavaScript promise (`Module.bmPace`), so the page gets its time between frames.
  An export that sleeps returns at once, so the runner calls `Module.bmDone` when the cartridge ends.
- `web/player.js` defines `window.bmLauncher` (`start(url)`, `startBytes(bytes, name)`, `stop()`). It paces at
  60 Hz with a timer, draws the RGBA frame on `canvas#bm-canvas`, maps the keyboard and the Gamepad API to the
  `HID_*` bits, and plays the 48 kHz mix through an AudioWorklet. Keys: arrows, Space or J = A, K or X = B,
  U = X, I = Y, Enter = Start, Backspace or right Shift = Select, Q and E = L1 and R1, 1 and 3 = L2 and R2,
  Esc = leave. Gamepad: standard mapping.
- The mock page uses it: open a `.bm` with "Open a .bm file", then press "Start bm".
- `bm_play(..., seconds)` stops at 0 seconds, so the runner passes one year.
- Checked: `tests/webplayer/test_bmweb.js` runs the runner under Node with the same hooks (150 frames, the pad
  moves the square, about 800 audio frames per video frame, quit). In the built-in browser pane the page drew
  frames, the arrow keys moved the square, 252 frames came in 6 s (the pane was hidden, so its timers are
  slowed: this is not the real rate), the audio context ran and `stop()` ended the run.
- Clock and sound queue: `web/bmring.js` (`BmPacer`, `BmRing`). `tests/webplayer/test_bmring.js` (in `make test-webplayer`) checks, with a fake clock: 59.98 Hz with a timer up to 4 ms late; no drift after a slow frame; no burst of frames after a pause of 5 s; the queue returns the samples in order, gives silence when empty, drops the oldest sound above 14400 frames and keeps the newest; a 60 Hz producer and a 128 frame consumer never underrun. The worklet runs `BmRing.toString()`. `bmLauncher.stats` shows `hz`, `frameAvgMs`, `frameWorstMs`, `audioUnderruns`, `audioDrops`, `audioQueued` (updated every 3 s): read them in a visible tab while a game plays.
- Not checked: the sound itself (by ear), the 60 Hz rate in a visible tab with real timers, a gamepad, the new worklet in a browser (only the Node tests and the build ran).
- Saves: `/sd/bm/save` is an IDBFS mount (`-lidbfs.js`). `player.js` reads it before a start and writes it every
  3 s and when the cartridge ends. Checked in the built-in browser with `tests/webplayer/save.lua`: the file
  `5B2B419C.SAV` was written, a reload of the page gave `runs=2` (it was 1), and a press of A saved 102. This
  check is by hand: IDBFS needs a browser.
- 3D: `make test-wasm-3d` plays the 3D stages cartridge (`tests/stages3d/input.txt`, 20 s) on the PC build and
  on the wasm build and compares one frame in 60: 20 frames, all byte-equal. So `r3d.c` and the float maths
  give the same pixels in wasm. The speed of 3D in wasm is not measured.

## 9. What stage 2 found

- The unchanged `src/bm/*` sources and `tests/host/stubs.c` compile with Emscripten. No kernel change was
  needed. Only the runner links `-sNODERAWFS` so that the SD card is a host folder under Node.
- `wasm-ld` has no `--defsym`. `scripts/mkweblibs.py` writes the Lua libraries as one C array and defines the
  `bm_lib_*` and `bm_lib_*_end` symbols with module assembly `.set`. `tests/host/libs.S` is not used.
- `bm_play()` is a blocking loop. For a browser, link with `-sASYNCIFY` and make `fb_flip` yield with
  `emscripten_sleep(0)`, or move the loop to a frame-step function. The first option needs no kernel change.
  This is stage 3.
- The frame colours are 0xRRGGBB values, so a fixture cartridge must not use small numbers.
