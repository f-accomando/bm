# Tools, SDK & Development Utilities

This document details the on-console creative suite, host-side packaging utilities, 3D asset decimation pipelines, AI assistants, and CI build automation in **bm**.

---

## 1. On-Console Creative Suite (`carts/`)

bm is designed as a self-contained creative environment. Developers can create entire 2D and 3D games directly on the console without a PC:

```
┌────────────────────────────────────────────────────────┐
│                        bm SDK                          │
│        Project Hub, Templates, Resource Budgets        │
└───────┬──────────┬──────────┬──────────┬──────────┬────┘
        │          │          │          │          │
        ▼          ▼          ▼          ▼          ▼
   ┌─────────┐┌─────────┐┌─────────┐┌─────────┐┌─────────┐
   │ bm Code ││bm Pixel ││bm Studio││bm Sound ││ bm Mesh │
   │ Lua IDE ││ 2D Art  ││3D Models││  Audio  ││ Geometry│
   └─────────┘└─────────┘└────┬────┘└─────────┘└─────────┘
                              │
                              ▼
                       ┌─────────────┐
                       │ bm Animator │
                       │Skeletons/Rig│
                       └─────────────┘
```

### 1. bm SDK (`carts/editor/main.lua`)
The project nerve center:
* **Project Templates**: Launches new projects from proven starter kits (Empty 2D, Platformer, Top-down, Shooter, Versus 2D, Online 2D, 3D Scene, 3D with Models).
* **Resource Tracker**: Monitors script token count, Lua RAM usage, asset memory, and file size relative to the future 8 MiB `.b16` limit.
* **One-Key Navigation**: Seamlessly jumps to specialized editors (bm Code, Pixel, Studio, Animator, Mesh, Sound) with automated state persistence.

### 2. bm Code (`carts/code/main.lua`)
The console's dedicated code editor:
* **Dual-Page Layout**: Displays two code pages side-by-side using a clean 6×12 bitmap font.
* **Predictive Typing ([`docs/PREDICT.md`](../PREDICT.md))**: Contextual inline word completion for Lua syntax, bm APIs, and comments in English or Italian.
* **Controller Typing ([`docs/PADTYPE.md`](../PADTYPE.md))**: Write code and text entirely using gamepad buttons and syllabic prediction.
* **Integrated Debugger (F8)**: Breakpoints, single-step execution, and variable inspection directly over the running game screen.

### 3. bm Pixel (`carts/pixel/main.lua`)
Sprite and 2D pixel art editor:
* Edits 1024×1024 cartridge spritesheets.
* Color palettes with up to 256 colors, transparency masking, and custom color remapping.
* Frame animation with configurable onion skinning.

### 4. bm Studio & bm Animator (`carts/studio/`, [`carts/animator/`](../../carts/animator/))
Console-native 3D modeling and skeletal animation:
* **bm Studio**: Builds low-poly 3D models using blocks, polygonal faces, vertex deformation, and direct surface tile painting.
* **bm Animator**: Constructs bone skeletons, skinning weights, and keyframe timeline clips; can bake 3D animations directly into 2D sprite frames.

### 5. Sound Editor (`carts/sound/main.lua`)
* 8-voice polyphonic tracker and sound effect designer.
* Audio 2 parameter control: PolyBLEP oscillator selection, resonant TPT filter sweeps, exponential envelopes, and FDN reverb / ping-pong echo sends.

### 6. Embedded AI Assistant (`src/ai/`)
* Runs a local INT8 neural inference engine directly on the ARM processor.
* Accessible via F6 in editors to answer API questions, diagnose syntax errors, sketch sprites, or generate low-poly 3D models from natural language descriptions.

---

## 2. Packaging & Asset Processing Utilities

For workstation workflows, the `scripts/` and `tools/` directories provide command-line utilities:

### `scripts/mkbm.py` (Cartridge Compiler)
Compiles game assets into binary `.bm` and `.b16` cartridges:
```sh
python3 scripts/mkbm.py main.lua \
    --sheet sheet.png \
    --map main=map.csv \
    --flags flags.csv \
    --models models.bm \
    --audio sound.bm \
    --cover cover.png \
    -o game.bm
```
* Generates standard chunked containers (`BMCART` / `B16` headers, CRC-32 integrity checks).
* Automatically encodes sprite sheets into compressed 8-bit paletted `SHEET8` sections.

### `scripts/bmres.py` (Resource Packager)
Extracts, converts, and manages standalone `.BMRES` resource libraries:
* `.bmm` (3D Models)
* `.bmi` (Images / Spritesheets)
* `.bms` (Sound banks)
* `.bmt` (Tilemaps & Layers)
* `.bmc` (Color palettes)
* `.bmk` (Asset kits)

### `tools/bmreduce.py` (3D Polygon Reducer)
An automated quadric edge collapse polygon decimation engine:
* Reduces high-poly meshes down to the Pi Zero's target 1,200 triangle budget.
* Preserves UV texture seams, material boundaries, and skeletal bone weights.

### `tools/qpuasm.py` (VideoCore IV QPU Assembler)
* Compiles SIMD assembly code into binary machine instructions executed by the 12 VideoCore IV QPUs.
* Assembles vertex shaders (`vs_baked`, `vs_tex_rgb`, `vs_lit`) and fragment shaders (`fs_*`).

---

## 3. Build Automation, Distribution & CI

### Master Build System ([`Makefile`](../../Makefile))
Key build targets:
* `make`: Compiles `build/kernel.img` (Pi Zero W) and `build/chainloader.img`.
* `make ZERO2=1`: Compiles `build/kernel7.img` for the Raspberry Pi Zero 2 W.
* `make TARGET=rgb30`: Builds `build/kernel8.img` for the PowKiddy RGB30.
* `make image`: Constructs raw bootable FAT32 SD card images (`dist/bm.img`).
* `make test`: Runs PC unit tests and QEMU regression tests.

### Workstation Tool (`easy_install.sh`)
An interactive bash setup utility for Linux/WSL:
* Installs ARM toolchain packages (`arm-none-eabi-gcc`).
* Automatically formats and installs kernel images onto target SD cards.
* Transfers cartridges and kernels over WiFi using [`tools/bm_net.py`](../../tools/bm_net.py).
* Publishes cartridges directly to the official game store.

### Tutorial videos ([`video/`](../../video/), skill `claude/skills/bm-video-tutorial`)
* `video/lib/bmvideo.py`: a key script for `bmhost` (the console's runtime on the PC, virtual clock, same run every time) and ffmpeg/libass for the page: console 2x, side panel with the place, the key pressed and the keys of the scene, narrator's line, hook, title and closing cards. Storyboard and script are generated from the same key script.
* `video/SERIE.md`: *Skyvale World*, a 2D platformer (original hero Kip) built only with the console's 2D tools, one episode per tool. Episode 1 (`video/01-pixel/`, bm Pixel) is done: Kip seen from the side, a six-frame run cycle, a jump and a coin; `verify.py` checks the saved cartridge pixel by pixel.
* On the PC there is no kernel menu and no assistant (`F6` in bm Pixel); QEMU is not in the cloud sessions.

### Continuous Integration (`.github/workflows/ci.yml`)
The GitHub Actions workflow distributes test execution across **6 concurrent virtual runners**:
1. Host unit tests (`make test-host`).
2. Overbit isolated test runner (`HOST_SKIP=test-overbit`).
3. 4 parallel QEMU shards running headless end-to-end integration tests (`make test-qemu SHARD=K/4`).
