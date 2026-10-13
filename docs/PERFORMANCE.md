# Performance and Technical Decisions — Raspberry Pi Zero W v1.1

At-a-glance report: hardware constraints, technical choices made (and discarded), expected numbers vs. measured benchmarks.

**Sources**
- *(M)* Measured on physical Raspberry Pi; *(Q)* Measured in QEMU; *(H)* x86 Host benchmark (indicative proportions); *(D)* Datasheet / official documentation.
- Additional details: stress test in [`docs/STRESS.md`](STRESS.md); hardware specs in [`docs/HARDWARE.md`](HARDWARE.md); display modes in [`docs/RISOLUZIONI.md`](RISOLUZIONI.md); GPU transition history in [`docs/GPU-BENCHMARK-REPORT.md`](GPU-BENCHMARK-REPORT.md); benchmark suite in [`docs/BENCH3D.md`](BENCH3D.md).

---

## 1. Hardware Constraints

| Resource | Value | Impact on bm |
|---|---|---|
| **CPU** | ARM1176JZF-S, 1 core, 700 MHz at boot → **1000 MHz** requested from firmware *(M)* | Everything (game, graphics, audio, input) runs on a single core within a single loop |
| **Cache** | L1 16 KiB instructions + 16 KiB data *(D)* | Large sprite sheets and z-buffers do not fit in L1: cache access locality is paramount |
| **RAM Bandwidth** | memcpy ~100 MB/s, fill ~430 MB/s *(M)* | **The primary system bottleneck**: every additional byte per pixel incurs a penalty |
| **SDRAM Reads (ARM)**| ~4× slower than writes *(M)* | Reading the framebuffer (dynamic lights, screen copies) costs significantly more than writing |
| **Integer Division** | Not available in hardware (libgcc routine) *(D)* | Strictly forbidden inside per-pixel inner loops: use shifts or floating-point reciprocals |
| **VFP (Floating Point)**| VFPv2; FDIVS ~19 cycles *(D)*; denormals trap unless in RunFast *(M)* | Divisions allowed per-triangle, never per-scanline or per-pixel; RunFast mode mandatory |
| **3D GPU** | VideoCore IV (V3D: 12 QPUs, TMU, tiled z-buffer) *(D)*; 811 Mpixel/s, 3 M tri/s *(M)* | No OpenGL: custom lightweight fixed-function driver under `draw3d` (M33, default); software 3D as fallback |
| **Scaler (HVS)** | Hardware Video Scaler scales framebuffer up to 1080p for free *(M)* | Games render at 640×360, 480×270, or 320×180 and GPU scales to display |
| **System Memory** | 448 MiB dedicated to ARM *(M)* | Not constrained (accommodates 4096 px sheets, caches, lookup tables) |
| **Bluetooth UART** | 921,600 baud *(M)* | Accommodates 4 DualShock 4 pads at 125 reports/s; limits higher throughput |
| **Frame Budget (60 fps)**| **16.7 ms** | Single global budget for `_update` + `_draw` + scanout / composition |

---

## 2. Measured Baseline Costs

| Operation | Measured Cost | Source |
|---|---:|---|
| Fill 640×360 (32-bit) | 2.2 ms | *(M)* |
| Copy 450 KiB RAM → RAM (CPU / DMA) | 3.70 / 2.99 ms | *(M)* |
| Fill 450 KiB in RAM (CPU / DMA) | 1.02 / 0.85 ms | *(M)* |
| 48-line strip copy, RAM → screen (CPU / DMA) | 0.87 / 0.26 ms | *(M)* |
| Benchmark `p`, map + 256 sprites: direct render / RAM buffer | 8.58 / 11.85 ms | *(M)* |
| Full frame copy via RAM using DMA | 14.06 ms (slower than CPU) | *(M)* |
| Simple Lua operation | ~100 ns → ~140,000 per frame | *(M)* |
| 16×16 Sprite from native C | 3.5 µs (~14 ns/pixel) | *(M)* |
| 16×16 Sprite from Lua (`spr()` + logic) | 8.4 µs | *(M)* |
| 2D Triangle (~170 pixels) | 6.0 µs | *(M)* |
| 3D Rendered Triangle (flat, small) | ~14 µs (Sept), ~5 µs per sphere tri (Sept 2026) | *(M)* |

---

## 3. 2D Pipeline Decisions

| Choice | Discarded Alternative | Rationale | Measured Data |
|---|---|---|---|
| **RGB565** (16-bit) for `.bm` cartridges | XRGB8888 (32-bit) | Halves memory bandwidth for every pixel read/written | 32-bit doubles frame copy time from ~6.5 ms to ~13 ms with CPU |
| **32-bit mode deferred** | Implement in M14 | Entire blit library duplication for minimal gain in gradients | Banding eliminated via 4×4 Bayer dithering |
| **Direct rendering** to framebuffer | Render to RAM then copy | Copying reads back slow SDRAM | 8.58 ms direct vs 11.85 ms via RAM copy *(M)* |
| RAM buffer **only when needed** (lights) | Render to RAM always | Lighting requires reading pixels: reading directly from display is slower | Activated dynamically on first `light_begin` |
| **Triple buffering** | Double buffering | Screen tearing on lower half of console menu on hardware | One additional 450 KiB page, zero per-frame cost |
| Full-frame copy with **CPU** | DMA | DMA is slower than CPU for full contiguous frame copies | 14.06 ms DMA vs 11.85 ms CPU *(M)* |
| DMA conservative settings (Circle-aligned) | Aggressive burst | Aggressive settings caused hardware lockups on Pi | Safe burst 8, high priority, `WAIT_RESP` |
| Lights on **4×4 px grid**, bilinear + dither | Per-pixel lighting | 1/16th of calculation overhead | Smooth lighting in Hunter's Night at 320×180 |
| Dynamic lighting games at **320×180** | 640×360 | 1/4th of total pixels to light and write | Hunter's Night maintains 60 fps |
| Fast C blit with row `memcpy` for opaque cells | Per-pixel check everywhere | Opaque cells bypass alpha transparency tests | ~14 ns/pixel *(M)* |
| **SHEET8** (≤256 colors + RLE in cart) | Raw RGBA | Drastically smaller cartridge size, fast one-time decode at boot | Titan Clash: 2048×3376, 166 colors |
| Sprite sheets up to **4096 px** | 256×256 limit | Arcade-sized character sprites | Memory is ample (448 MiB) |
| Console 8×16 monospace font on 16 px grid | Proportional font | Fast, predictable, and verifiable in QEMU screenshot tests | Zero layout overhead |

---

## 4. 3D Pipeline Decisions

| Choice | Discarded Alternative | Rationale | Measured Data |
|---|---|---|---|
| **Custom V3D backend** under `draw3d` (M33, default) | OpenGL ES on GPU | OpenGL requires heavy Linux DRM or VCHIQ firmware; fixed-function pipeline suffices | 182 spheres (7,142 triangles) at 60 fps *(M)* |
| **Software rasterizer** on ARM (fallback & QEMU) | None | Runs universally, identical pixel output in regression tests | 69 spheres (2,679 triangles) at 60 fps *(M)* |
| **16-bit 1/z depth buffer** | 32-bit float z | Halves memory bandwidth; 1/z is screen-space linear | 460 KiB depth buffer cleared per frame |
| **Depth-buffer bypass flag** for floors/skies | Z-test everywhere | Eliminates depth read/write bandwidth for background geometry | Utilized by Chaos Kitchen |
| Early **backface culling** | Render all faces | Culls ~60% of sphere geometry upfront | Stress test: ~40% drawn |
| Near-plane clipping **only** | Clip against all 6 planes | Screen-space scanline bounds clip other edges naturally | Eliminates redundant clipping math |
| **Flat lighting** per face by default | Gouraud everywhere | Significantly cheaper per pixel | Flat vs Gouraud: see Section 5 |
| **Gouraud** on demand (`draw3d` flag 4) | Per-pixel lighting | Vertex-color interpolation with clamped vertex values | Exact pixel output |
| **4×4 Bayer dithering** | Hard color quantization | Eliminates color banding on 16-bit RGB565 | 3 additions per pixel |
| **Perspective-correct textures** every 16 px (Quake style) | Affine textures / per-pixel division | 1 division every 16 pixels; affine textures distort | Error undetectable under 1 texel |
| Z, color, and UV gradients **once per triangle** | Per-scanline interpolation | Saves 2–3 divisions per scanline | Section 6 |
| **Inline rounding** | Library `ceilf()` | Saves 4 function call overheads per scanline | Scanline inner loop optimization |
| Cached camera across `draw3d` calls | Recalculate sin/cos every call | Saves 12 trigonometric evaluations per call | Chaos Kitchen: 490 calls/frame |
| Matrices and transforms in **VFP float** | Fixed-point matrices | Hardware VFP is available and fast | Clean 4×4 transformations |
| Inner raster loops in **fixed-point** (16.16, 24.8) | Float per-pixel | Fast integer arithmetic, zero float-to-int conversions | Inner loop speedup |
| **Fog calculated per face** | Per-pixel fog | Identical cost to flat lighting | Astro Wing |
| Up to 4 point lights per face/vertex | Per-pixel dynamic lights | Light calculated at vertex level | Fast dynamic lamps |
| **32.32 Fixed-point triangle edges** (M33) | `ceilf` and VFP comparisons per line | VFP comparisons stall pipeline (`vmrs`): 5 per scanline | ~80 → ~60 insns/scanline; identical pixels |
| **Specialized texture loops** (M33): ±z, alpha, light | Single branchy loop | Clamp checked once per 16 px span; alpha checked per 8×8 cell | 70 → 46 insns/px lit, 55 → 30 unlit |
| Dual-multiply texture lighting (R and B packed) | 3 separate multiplies | Same visual result in fewer cycles | Packed ALU optimization |
| Early frustum bounding-sphere culling (M33) | Transform all vertices | Bounding sphere tested against 4 screen edges + near plane | Chaos Kitchen: hundreds of `draw3d` calls/frame |
| **DMA z-clear** at end of frame (M33) | `memset` in `zclear()` | DMA clears buffer while next frame's `_update` runs | ~1 ms at 640×360 saved; disable via `dma_zclear=0` |

---

## 5. Cost Per Pixel: Flat, Gouraud, Textures

| Render Mode | Host Baseline *(H)* | Host Rewritten *(H)* | Pi Hardware: Fullscreen Sphere (~180,000 px) *(M)* |
|---|---:|---:|---|
| **Flat** | 0.27 ms (1×) | 0.22 ms | Fits in 16.7 ms (60 fps with 15 spheres) |
| **Gouraud** | 0.84 ms (**3.1×**) | 0.59 ms (2.7×) | **> 16.7 ms**: exceeds ~90 ns/pixel |
| **Textured** | 1.12 ms (**4.1×**) | 1.08 ms (4.9×) | **> 33 ms**: exceeds ~180 ns/pixel |

**Practical Rule of Thumb**:
- Gouraud shading and textured triangles are optimal on **small to medium geometry**, not fullscreen fills.
- Large background geometry (floors, skies): render flat without z-buffer testing.

**ARM Instructions Per Pixel (M33, `make count-insns`)** measured via `qemu-arm` on `tests/bm/bench3d.c`:

| Scene | Before (M32) | After (M33) |
|---|---:|---:|
| Flat Quads with Z | 12.5 | 10.4 |
| Flat Quads without Z | 3.4 | 1.2 |
| Gouraud Quads | 35.8 | 32.7 |
| Textured Quads with Lighting | 70.0 | 45.8 |
| Textured Quads without Lighting | 55.0 | 30.2 |
| Flat Spheres (small triangles) | 24.5 | 21.6 |
| Textured Spheres | 94.8 | 79.9 |
| Texture Room (8 crates) | 84.7 | 57.7 |

With 3D rendering delegated to the VideoCore IV GPU, the ARM core no longer draws pixels directly: Texture Room CPU instructions drop from 3.86 million to 0.62 million per frame. Historical transition data and Overbit benchmarks are documented in [`docs/GPU-BENCHMARK-REPORT.md`](GPU-BENCHMARK-REPORT.md) and [`docs/BENCH3D.md`](BENCH3D.md).

**ARM Instructions Per Triangle with GPU (M34)**, using `+gpu` scenes:

| Scene | Before | After | Reduction |
|---|---:|---:|---:|
| Flat Spheres | 936 | 706 | **−25%** |
| Gouraud Spheres | 1030 | 830 | **−19%** |
| Textured Spheres | 1131 | 888 | **−21%** |
| Texture Room (8 crates) | 1386 | 1109 | **−20%** |
| Scene with interleaved 2D | 1432 | 1130 | **−21%** |

---

## 5.1 3D Performance Milestone History

| Milestone / Phase | Optimization Focus | Measured Result on Hardware (Pi Zero W) |
|---|---|---|
| **Sept 2026 (Pre-M33)** | Baseline Software Rasterizer (ARM) | 1,195 triangles at 60 fps; Texture Room 320×180 at 30 fps (5.52 M CPU insns/frame). |
| **M33 (Oct 1, 2026)** | VideoCore IV V3D Hardware Acceleration | **811 Mpixel/s**, 182 spheres at 60 fps (7,142 triangles); Texture Room HD (640×360) at 60 fps (5.8 ms/frame, −89% ARM insns). |
| **M34 (Oct 3, 2026)** | `r3d` Backend Tuning & Overbit 10-Bot Match | Overbit GPU path cuts ARM instructions by **23–32%** across presets; frame rates rise from ~36 to ~50 fps (LOW) and ~30 to ~36 fps (HIGH); hero planar shadows and 24-bit depth unlocked. |
| **M35–M38 (Benchmark Suite)** | 16-bit Textures (`gpu3d_tex16`), ETC1, Dual-Thread Shaders (`fs2`) | Automated profiling in [`docs/BENCH3D.md`](BENCH3D.md) with load scaling from 60 fps to 30 fps. |

> **Guideline for Future Enhancements**: When landing pipeline optimizations (e.g. QPU vertex shaders, multi-frame asynchronous queues, compressed texture formats), record measured deltas in the table above and in the active area tracker [`docs/progress/v3d.md`](progress/v3d.md).

---

## 6. Expected (Emulation) vs Measured (Hardware)

| Test Case | Expected (QEMU) | Measured on Physical Pi Zero W | Notes |
|---|---|---|---|
| **QEMU Baseline** | QEMU ~2× slower than Pi | Confirmed for sprites (2,133 → 4,482) and flat 3D (602 → 1,195 triangles) | QEMU does not model cache stalls or memory bus bandwidth |
| **Gouraud / Textures in QEMU** | 3 spheres at 60 fps | **< 1 sphere** | QEMU under-represents hardware per-pixel memory latency |
| **Chaos Kitchen 1-1** | 15–17 ms in QEMU (~8 ms estimated Pi) | **14.1 ms (54 fps)** on CPU; **6.1 ms (60 fps)** on V3D GPU | Heavy triangle counts previously bottlenecked on per-triangle scanline setup |
| **Titan Clash** | — | **11.6 ms (61 fps)** | Large 2D sprites and backgrounds easily sustain 60 fps |
| **Astro Wing** | 60 fps | **59.9 fps (6.34 ms)** | Solid 60 fps following VFP RunFast configuration fix |
| **Frame copy via RAM + DMA** | DMA anticipated faster | Full frame copy **slower** (14.06 ms DMA vs 11.85 ms CPU) | Bus contention negates DMA advantages on full contiguous frame copy |
| **Bluetooth Input (600 queued reports)** | Zero latency | < 0.5 s in test, undetectable in gameplay | Event loop prioritizes freshest report, capping processing at 4 ms |
| **M33 Rasterizer on Spheres** | −9% instructions | **2.3× more spheres at 60 fps** (31 → 70) | Instruction counts miss pipeline un-stalls from removing floating-point comparisons |
| **V3D Initial Bringup** | Step 3 passes | "No end of frame" | Driver bug: `PCS` binner out-of-memory bit asserted at boot was misidentified as error |
| **V3D Driver Fix (`d0c7fe8`)** | Hundreds of Mpixels/s | **811 Mpixel/s**, 3.0 M tri/s; 182 spheres at 60 fps vs 69 on ARM | Current bottleneck is ARM vertex submission (~2 µs/tri); 256×256 linear textures cost 9 ns/px vs 1 ns |

---

## 7. Deferred and Avoided Features

| Feature | Reason Avoided / Deferred | Status / Solution |
|---|---|---|
| **OpenGL ES on GPU** | Requires heavy Linux DRM or closed VCHIQ firmware stack | Replaced by custom direct-register fixed-function V3D driver |
| **32-Bit Framebuffer** | Doubles memory bandwidth; negligible visual benefit over dithered RGB565 | Deferred |
| **Full-Frame DMA Blit** | Slower than optimized CPU unrolled blit | Reserved for test `D` and clearing |
| **Per-Pixel Dynamic Lights** | Exceeds single-core CPU budget | 4×4 Bilinear lighting grid |
| **Affine Texture Mapping** | Noticeable perspective warping | Subdivided perspective-correct spans every 16 px |
| **Per-Pixel Texture Division** | ~19 cycles per pixel | 1 reciprocal every 16 px |
| **Multi-Port USB Hubs** | Excessive DWC2 host controller driver complexity | Bluetooth adopted for local 4-player multiplayer |
| **Full Git / OpenSSH** | Excessively heavy for bare-metal standalone kernel | Lightweight HTTP/HTTPS client implementations |
| **Lua JIT** | Requires complex bare-metal dynamic code generation port | Standard lightweight Lua 5.4 interpreter |

---

## 8. Hardware-Specific Bugs Resolved

| Symptom | Root Cause | Fix |
|---|---|---|
| Astro Wing freezes after ~2 minutes with buzzing audio | Denormalized floating-point values triggered VFP hardware trap | RunFast mode enabled at boot (FPSCR `FZ|DN`) |
| Hard system lockup during benchmark `p` | Aggressive DMA burst and bus priority settings | Circle-aligned conservative DMA configuration |
| Screen tearing on lower half of console menu | Scanout beam overlapped active frame rendering | Implemented triple-buffered page flipping |
| Kernel crash with blank screen | Hardware exception handler rendered to off-screen buffer | Exception display forced to visible buffer with 3-second watchdog |
| Second DualShock 4 fails to pair | Controller requested SDP handshake prior to HID; refusal aborted connection | Implemented minimal SDP response; console opens HID channels after 1 s |

---

## 9. Hardware Verification Targets

- **Oct 5, 2026 Test Pass** (verified in `reports` branch, recorded in [`docs/STRESS.md`](STRESS.md) and [`docs/ROADMAP.md`](ROADMAP.md)): test `g`, stress test with GPU and AA passes, Texture Room HD, 3D Bench, Overbit on GPU and GPU+AA at 1080p scaled with HUD overlay.
- Ongoing verification: Vertex shader pass (`GPU+VS`) and queued async execution (`GPU+VS+Q`).
- Boot log inspection (`Report the log`, `Z`): check `gpu3d:` diagnostic output for vertex shader probe logs.
- Chaos Kitchen multi-chef stress test (World 3 and World 6 with 2+ players).
