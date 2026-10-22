# bm web player: run bm in a web browser (WEB-1)

Status: study, with stage 1 built. This page lists what exists, what is missing, the stages, the risks
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

1. A WebAssembly toolchain. WSL on the development PC has `gcc` and `python3` only: no `emcc`, and no
   `clang` with the wasm32 target. The Windows side has Node 24. An install needs the user's OK.
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
| 2 | wasm toolchain and a minimal runtime | the runtime builds to wasm; `bmweb_frame` returns the first frame of a cartridge that calls `cls()` | emcc, or clang with wasm-ld (the user decides) |
| 3 | Canvas and input | a 2D cartridge draws and moves with keyboard and pad | stage 2 |
| 4 | Audio | an AudioWorklet plays `sfx()` and `music()` | stage 3 |
| 5 | Storage and 3D | IndexedDB saves; a 3D cartridge on `r3d.c` | stage 4 |
| 6 | Release page | SRI or vendored React, licence policy, size check, `bmLauncher.start` connected | the user decides where to host |

## 6. Risks

- **No toolchain here.** Stage 2 cannot start before the user allows an install (emsdk, or `clang` and `lld`
  in WSL).
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

Stage 2: build `bmweb.wasm` from the `BMHOST_RT` sources plus a web runner. Run it under Node with a fixture
cartridge. Compare the first frame with the PNG that `bmhost --shots` writes for the same cartridge.
