# bm — BareMetal Console & Game Engine

<p align="center">
  <img src="docs/showreel.gif" width="720" alt="bm Showreel: map in SDK, code in bm Code with AI assistant, animated game">
</p>

<p align="center">
  <b>A complete game from scratch, on bare-metal hardware alone</b> (16 s).<br>
  Map painting in SDK · code editor with AI assistant · live gameplay with skeletal animation.<br>
  <sub>Recorded in QEMU (<code>-M raspi0</code>) running the exact Raspberry Pi kernel binary. Rebuilt with <code>make showreel</code> · <a href="docs/showreel.mp4">MP4 1280×720</a></sub>
</p>

<p align="center">
  <a href="#-quick-start"><img src="https://img.shields.io/badge/Quick_Start-Make_Image-blue?style=flat-square" alt="Quick Start"></a>
  <a href="docs/API.md"><img src="https://img.shields.io/badge/API_Reference-Lua_5.4-green?style=flat-square" alt="API Reference"></a>
  <a href="docs/progress.md"><img src="https://img.shields.io/badge/Documentation-Living_Index-orange?style=flat-square" alt="Docs Index"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-BM_Community_1.0-purple?style=flat-square" alt="License"></a>
</p>

---

## 🚀 Overview

**bm** (BareMetal) is an open-source games console and development environment running on the **Raspberry Pi Zero W** (and Zero 2 W) with **no underlying operating system**.

Power on and the console boots straight into the games menu in **~2 seconds**. Games are written in Lua 5.4, powered by a custom bare-metal runtime with full 2D and 3D pipelines, hardware-accelerated V3D rendering, polyphonic synthesizer, network multiplayer, and an on-device creative suite.

---

## ✨ Key Features

- **True Bare Metal Architecture**:
  - `kernel.img` (~2 MB of C, ARM assembly, and Lua 5.4) communicates directly with hardware registers.
  - No Linux kernel, background daemons, or OS scheduler: the entire 1 GHz core and 448 MiB RAM belong exclusively to your game frame.
  - Zero boot wait: boots directly into interactive gameplay in under 2 seconds.
- **Hardware & Peripherals**:
  - **Video**: Crisp HDMI output (640×360, 480×270, 320×180, or 360×360 square scaled up to 720p/1080p).
  - **Audio**: 8-voice polyphonic synthesizer with HDMI digital audio output.
  - **Inputs**: Plug-and-play USB keyboards, gamepads, mice, and Bluetooth (up to 4 DualShock 4 controllers + BLE peripherals).
  - **Connectivity**: WiFi with HTTPS, local UDP multiplayer, and FAT32 SD card file system.
- **Cartridge System (`.bm` & `.b16`)**:
  - Single standalone cartridge files encapsulating Lua source, sprite sheets, tile maps, 3D meshes, skeletal animations, and sound banks.
  - Compatible with PICO-8 carts (`.p8`, `.p8.png`) through the built-in `nano8` emulator.
- **On-Console Creative Suite**:
  - Code, draw, sculpt 3D models, rig skeletons, compose music, and test games directly on the console with no PC required.

```lua
local x = 320
function _update()                       -- 60 fps fixed update
  if btn(0) then x = x - 2 end           -- Left pad / arrow
  if btn(1) then x = x + 2 end           -- Right pad / arrow
end

function _draw()
  cls(0x102030)
  spr(1, x, 180)                         -- Draw sprite from cartridge sheet
end
```

---

## ⚡ Performance Highlights

Hardware target: **Raspberry Pi Zero W** (ARM1176 @ 1.0 GHz, VideoCore IV V3D GPU).

| Test Scenario (at 640×360 RGB565) | 60 FPS | 30 FPS |
|---|---:|---:|
| **16×16 Sprites (Native C)** | **4,482** | 9,255 |
| **32×32 Sprites (Native C)** | **1,513** | 3,130 |
| **2D Triangles (~170 px each)** | **2,594** | 5,368 |
| **3D Spheres (V3D GPU hardware)** | **7,142** | 15,346 |
| **3D Spheres (ARM software rasterizer)** | **2,700** | 8,914 |
| **16×16 Sprites (from Lua `spr()` calls)** | **1,829** | 3,774 |
| **3D Triangles (from Lua `draw3d()`)** | **1,115** | 4,692 |

### 🚀 3D Engine & Architecture (bm3d & V3D Driver)
bm integrates **bm3d** (v7.2+), a custom dual-path 3D engine:
- **GPU Path (V3D Hardware - Default)**: The ARM core performs vertex transforms, lighting, and clipping, while the VideoCore IV V3D GPU fills pixels via custom QPU assembly shaders (`tools/qpuasm.py`) at **811 Mpixel/s** with 24-bit depth and optional 4× MSAA. CPU pixel-drawing instructions drop by **up to 89%**.
- **ARM Fallback Rasterizer**: Highly optimized fixed-point 32.32 software rasterizer with per-face LOD, screen-door transparency, and planar shadows when GPU is absent or in emulators.

### 🎯 Real-World Game Benchmarks
- **Overbit (3D Hero Shooter, 10 Bots, Partenope map at 480×270 / 1080p scaled)**:
  - ARM instructions reduced by **23% to 32% per frame** across all visual presets.
  - GPU unlocks real-time planar hero shadows, smooth Gouraud shading, textured fog, dynamic explosion lighting, and 24-bit depth.
  - Frame rates on Pi Zero W jump from ~36 fps up to **~50 fps (LOW)**, ~34 to **~45 fps (MEDIUM)**, and ~30 to **~36 fps (HIGH)**.
  - Budget: ~6,000 triangles at 60 fps; hero models feature up to 1,394 triangles at full LOD.
- **Texture Room HD (640×360, 32 Crates)**: **5.8 ms/frame (solid 60 fps)** on V3D GPU vs 25–26 ms on software ARM.
- **Chaos Kitchen (3D Co-op)**: **6.8 ms/frame (60 fps)** on V3D GPU vs 14.1 ms on software ARM.
- **Astro Wing (3D Flight Combat)**: **6.3 ms/frame (solid 60 fps)**.

> Read the full performance breakdown in [`docs/GPU-BENCHMARK-REPORT.md`](docs/GPU-BENCHMARK-REPORT.md) and [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).

---

## 🛠️ The Creative Suite

Everything in a cartridge can be created using bm's built-in editors in the **Dev** menu:

| Tool | Focus | Capabilities |
|---|---|---|
| **bm SDK** | Project Hub | Templates (platformer, shooter, 3D), asset management, token & memory monitor, quick-launch into suite editors. |
| **bm Code** | IDE | Dual-column editor with 6×12 font, auto-completion, keyword prediction, and syntax help. |
| **bm Pixel** | 2D Art | Sprite sheet editor, 256-color palettes, multi-frame animation with onion skinning. |
| **bm Studio** | 3D Modeling | Box modeling, vertex tweaking, UV tile painting, and mesh creation right on the console. |
| **bm Animator** | Animation | Bone hierarchies, skeletal rigging, timeline tracks, and baking into 2D sprite sheets. |
| **bm Mesh** | Geometry | Vertex, edge, and face inspection for procedural and static 3D meshes. |
| **Sound Editor** | Audio | 8-voice synth, custom instrument presets, sound effects, and music pattern sequencers. |
| **AI Assistant** | Assistant | On-device INT8 assistant answering API questions, generating sprite sketches, and building low-poly meshes. |

<p align="center">
  <img src="docs/img/code.png" width="48%" alt="bm Code on the console">
  <img src="docs/img/assistant.png" width="48%" alt="AI assistant on the console"><br>
  <img src="docs/img/sound.png" width="48%" alt="Sound editor on the console">
  <img src="docs/img/studio-console.png" width="48%" alt="bm Studio 3D editor"><br>
  <img src="docs/img/animator-console.png" width="48%" alt="bm Animator skeletal animation">
  <img src="docs/img/pixel.png" width="48%" alt="bm Pixel sprite editor">
</p>

---

## 🎮 Featured Games

<p align="center">
  <img src="docs/img/overbit-match.gif" width="720" alt="Overbit gameplay">
  <br><sub><b>Overbit</b>: 3D first-person hero shooter at 480×270 / 60 fps with 10-player AI bot battles, 8 heroes, and local/LAN netplay. <a href="docs/img/overbit-match.mp4">Watch MP4</a></sub>
</p>

<p align="center">
  <img src="docs/img/astrowing.png" width="24%" alt="Astro Wing">
  <img src="docs/img/titan.png" width="24%" alt="Titan Clash">
  <img src="docs/img/kitchen.png" width="24%" alt="Chaos Kitchen">
  <img src="docs/img/yharnam.png" width="24%" alt="Yharnam">
</p>

- **Overbit**: 3D first-person hero shooter featuring 8 heroes (Rally, Kaiju, Sarge, Frost, Fuse, Rail, Orbit, Akari), control point mode, baked lighting, and 10-player lockstep netplay.
- **Astro Wing**: High-speed 3D flight combat with procedural terrain and lighting.
- **Titan Clash**: Giant fighting mechas with large sprite sheets and 2-player combat.
- **Chaos Kitchen**: Cooperative 3D cooking party game for 1 to 4 players.
- **Yharnam**: Gothic nighttime action RPG at 360×360 (.b16 screen) with 27 hunter animations, trick weapons, gun parries, visceral attacks, stamina management, and 4 district boss fights.
- **Hunter's Night**: 2D gothic platformer with real-time dynamic light sources.
- **bm Market**: Integrated online catalog downloading free cartridges with cryptographic SHA-256 verification directly to the SD card.

---

## 📦 Quick Start

### Build Prerequisites
Requires `arm-none-eabi-gcc`, Python 3, `dosfstools`, `mtools`, and QEMU (for emulation tests):

```bash
# Ubuntu / Debian
sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \
  qemu-system-arm make python3 python3-numpy python3-pil dosfstools mtools
```

### Build & Run
```bash
# 1. Build the complete bootable disk image (kernel + firmware + carts)
make firmware && make image

# 2. Run unit tests and full QEMU regression tests
make test

# 3. Launch QEMU emulator directly
make qemu
```

### Flashing to Hardware
1. Flash `dist/bm.img` to a microSD card using [Raspberry Pi Imager](https://www.raspberrypi.com/software/) (*"Use Custom"*), balenaEtcher, or `dd`.
2. Connect HDMI to a display and plug in a USB keyboard or gamepad via OTG adapter.
3. Insert the card into your Raspberry Pi Zero W and power on.

---

## 📚 Documentation & Architecture

- **Living Progress & Index**: [`docs/progress.md`](docs/progress.md) *(the canonical navigation root)*.
- **Lua API Reference**: [`docs/API.md`](docs/API.md) *(complete draw, input, audio, 3D, and netplay reference)*.
- **Game Authoring Guide**: [`docs/GAME-GUIDE.md`](docs/GAME-GUIDE.md) (Italian: [`docs/GUIDA-GIOCHI.md`](docs/GUIDA-GIOCHI.md)).
- **Architecture & Design RFCs**: [`docs/design/`](docs/design/).
- **Performance & Drivers**: [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md), [`docs/DRIVERS.md`](docs/DRIVERS.md), and [`docs/HARDWARE.md`](docs/HARDWARE.md).
- **OS Capabilities**: [`docs/OS-CAPABILITIES.md`](docs/OS-CAPABILITIES.md) *(what bm can and cannot do, compared with Linux and Windows)*.
- **AI Agent Guidelines**: [`CLAUDE.md`](CLAUDE.md) and [`GEMINI.md`](GEMINI.md).

---

## ⚖️ License

bm is developed by **F. Accomando** and licensed under the **BM Community License 1.0** ([`LICENSE`](LICENSE)).

- Individuals may freely use, modify, and redistribute the project, including commercial projects on their own behalf.
- Attribution is required; modified versions must remain under the same license.
- Organizations require a separate commercial license.
- Embedded third-party components retain their respective open-source licenses: Lua (MIT), lwIP (BSD), mbedTLS (Apache 2.0).
