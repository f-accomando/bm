# 3D Hardware Transition & Benchmark Report (M33/M34)

Comprehensive historical transition report: software ARM rasterizer vs. VideoCore IV V3D GPU acceleration.
Includes QEMU instruction metrics, physical Raspberry Pi Zero W verification benchmarks, and Overbit 10-bot load analysis.

Code status: complete, tested on PC and in QEMU, verified on physical Raspberry Pi hardware (`kernel.img`). GPU is the default 3D pipeline for games; software ARM rasterizer serves as fallback.

---

## 1. Executive Summary

- **Software ARM 3D (Fallback)**: Identical pixel accuracy with fewer CPU cycles. Texture Room CPU instructions drop by 30% (5.52 M → 3.86 M insns/frame); textured pixels require 46 instructions each down from 70.
- **Hardware V3D GPU (Default)**: The ARM core delegates rasterization entirely, focusing exclusively on vertex transformations and lighting. Texture Room drops to 0.62 M CPU instructions per frame (**89% reduction**). CPU overhead scales strictly with triangle count, independent of screen pixel density.
- **Visual Accuracy**: GPU emulation tests demonstrate 99.25% to 99.99% pixel-identical output compared to the software reference renderer, with microscopic differences isolated to sub-texel edges.
- **Hardware Verification (Raspberry Pi Zero W)**:
  - ARM software renderer delivers 2.1×–2.3× speedups on small triangles and ~2× on 2D primitives.
  - V3D GPU passes all 11 driver verification steps: **811 Mpixel/s fill rate**, 182 spheres at 60 fps (7,142 triangles) vs. 69 on ARM software (6× improvement over September baseline).
  - **Texture Room HD (640×360, 32 crates)**: **5.8 ms/frame (solid 60 fps)** on GPU vs 25–26 ms on ARM CPU.
  - **Chaos Kitchen**: **6.8 ms/frame (solid 60 fps)** vs 14.1 ms previously.

---

## 2. Architectural Comparison

| Feature | Pre-Transition (`9cda3c7`) | Post-Transition (Hardware Accelerated) |
|---|---|---|
| **3D Rendering Engine** | Software ARM rasterizer only | VideoCore IV V3D GPU (default) or ARM CPU (*Settings > 3D of the games*) |
| **GPU Architecture** | Unused | Custom bare-metal driver: 12 QPUs, TMU texturing, 24-bit tiled depth buffer |
| **Software Rasterizer** | Float edges, single branchy loop | 32.32 Fixed-point edges, specialized span loops, early bounding-sphere culling |
| **Z-Buffer Clear** | Synchronous CPU `memset` | Asynchronous DMA z-clear during frame transition |
| **Resolution Modes** | 640×360, 320×180 | Adds **480×270** (exact 4× integer scale to 1080p) and square 360×360 (.b16) |
| **Interleaved 2D/3D Depth**| Not supported | Preserved via V3D tiled depth reload when required |
| **Fault Recovery** | System crash | Automatic fallback to ARM software renderer on GPU timeout with kernel log trace |
| **Benchmarking Tools** | Basic stress test | `make bench3d`, `make count-insns`, `tools/qpuasm.py`, V3D emulator, and `make test-gpu3d` |

---

## 3. Host Simulation Metrics (`qemu-arm`)

CPU instructions per frame counted using `qemu-arm` on `tests/bm/bench3d.c` scenes (`make count-insns`, ARM1176 @ -O2). Excludes cache latency to isolate raw CPU computation work.

| Scene | Software (Pre) | Software (Post) | GPU Acceleration (ARM Part) |
|---|---:|---:|---:|
| **Texture Room (8 crates, 320×180, 446 tri)** | 5.52 M | 3.86 M (−30%) | **0.62 M (−89%)** |
| **Texture Room (32 crates, 561 tri)** | 8.60 M | 6.27 M (−27%) | **0.80 M (−91%)** |
| **30 Flat Spheres (1,137 tri)** | 4.15 M | 3.77 M (−9%) | **1.06 M (−74%)** |
| **30 Gouraud Spheres** | 8.95 M | 8.66 M (−3%) | **1.17 M (−87%)** |
| **30 Textured Spheres** | 14.46 M | 12.34 M (−15%) | **1.28 M (−91%)** |
| **4 Textured Quads (230,400 pixels)** | 16.13 M | 10.55 M (−35%) | **0.02 M (−99.9%)** |

**Per-Pixel CPU Instructions (ARM Software)**:
- Flat Quads (with Z): 12.5 → 10.4 insns/px
- Gouraud Quads: 35.8 → 32.7 insns/px
- Textured Quads (lit): 70.0 → 45.8 insns/px
- Textured Quads (unlit): 55.0 → 30.2 insns/px

**V3D Emulation Accuracy** (`make test-gpu3d`):
Differing pixels between software and hardware output remain under **0.1%** in standard scenes (e.g. flat/Gouraud spheres, textures, transparency, 480×270 modes).

---

## 4. Hardware Benchmarks (Physical Raspberry Pi Zero W)

Measurements recorded at 1000 MHz CPU, 250 MHz Core, 250 MHz V3D, 400 MHz SDRAM.

### 4.1 Software ARM Improvements
| Test | September Baseline | Post-Rewrite (ARM) | Speedup |
|---|---:|---:|---:|
| **16×16 Sprites (C)** | 4,482 | 4,456 | 1.0× |
| **2D Triangles (~170 px)** | 2,594 | 5,038 | **1.9×** |
| **3D Spheres (Flat, C)** | 1,195 tri (31 spheres) | 2,700 tri (70 spheres) | **2.3×** |
| **3D Spheres (Lua)** | 1,115 tri (29 spheres) | 2,323 tri (61 spheres) | **2.1×** |

### 4.2 Hardware GPU Throughput
Verification via diagnostic test `g`:
- **Small Triangles Throughput**: 20,000 triangles prepared in 7.7 ms; binning 2.7 ms + rendering 4.1 ms = **3.0 Million triangles/second**.
- **Pixel Fill Rate**: 20 fullscreen buffers rendered in 5.7 ms = **811 Mpixel/second** (vs. ~45 Mpixel/s software fill).

**Stress Test Scene Comparison (Max Load per Frame)**:
| Test Scenario | ARM Software (60 fps) | V3D GPU (60 fps) | V3D GPU (30 fps) | GPU vs ARM Gain |
|---|---:|---:|---:|---:|
| **Flat Spheres (96 tri)** | 69 (2,679 tri) | **182 (7,142 tri)** | 389 (15,346 tri) | **2.6×** |
| **Gouraud Spheres** | 18 (703 tri) | **170 (6,673 tri)** | 364 (14,360 tri) | **9.4×** |
| **Textured Spheres** | <1 (52 at 30 fps) | **156 (6,094 tri)** | 333 (13,135 tri) | **>15×** |
| **Fullscreen Flat Quads** | 11 (22 ns/px) | **200 (1 ns/px)** | 430 | **18×** |
| **Fullscreen Gouraud Quads** | 5 (49 ns/px) | **199 (1 ns/px)** | 427 | **40×** |

### 4.3 Real Game Comparisons on Hardware
- **Texture Room HD (640×360)**: Software ARM = 25.3 ms (41 fps) with 8 crates; V3D GPU = **5.8 ms (solid 60 fps)** with 32 crates (573 triangles).
- **Chaos Kitchen**: Software ARM = 14.1 ms (54 fps); V3D GPU = **6.8 ms (solid 60 fps)** with 758 triangles.

---

## 5. Architectural Differences (ARM vs GPU)

1. **Gouraud Shading**: GPU renders smooth color gradients without dither noise.
2. **Depth Precision**: GPU features 24-bit depth buffering vs. 16-bit 1/z on CPU, eliminating z-fighting on intersecting geometry.
3. **Automatic Depth Clearing**: GPU depth buffer resets per frame without invoking explicit `zclear()` CPU cycles.
4. **Interleaved 2D/3D**: Tiled depth preserves state when 2D layers render between 3D passes.

---

## 6. Overbit GPU Benchmark Analysis (M38)

Benchmark profile of bm's most demanding title: **Overbit** running on Partenope map with 10 active AI bots fighting on the control point.

### 6.1 Performance Summary
- **CPU Reduction**: GPU cuts ARM instructions by **23% to 32% per frame** across all visual presets.
- **Visual Enhancements**: Enables real-time planar hero shadows, smooth Gouraud lighting, distance fog, dynamic weapon explosion illumination, and optional 4× MSAA.
- **Estimated Framerates on Pi Zero W**:
  - **LOW**: ~36 fps (ARM) → **~50 fps (GPU)**
  - **MEDIUM**: ~34 fps (ARM) → **~45 fps (GPU)**
  - **HIGH**: ~30 fps (ARM) → **~36 fps (GPU)**
  - **ULTRA**: ~25 fps (ARM) → **~34 fps (GPU)**
  - **EXTREME**: ~25 fps (ARM) → **~31 fps (GPU)**

### 6.2 Preset Breakdown (10-Bot Control Point Skirmish)

| Quality Preset | Renderer | ARM CPU M Insns | V3D Render Time | Est. Frame Time | Est. FPS | Triangles | Vertices |
|---|---|---:|---:|---:|---:|---:|---:|
| **LOW** | Software (ARM) | 13.06 / 12.17 | — | 27.8 ms | 36.0 fps | 3,717 | 6,872 |
| | **Hardware (GPU)** | **8.90 / 8.17 (−32%)** | 1.4 ms | **20.2 ms** | **49.6 fps** | 3,363 + 320 | 6,304 |
| **MEDIUM** | Software (ARM) | 14.01 / 12.98 | — | 29.7 ms | 33.7 fps | 4,257 | 7,679 |
| | **Hardware (GPU)** | **9.91 / 8.84 (−31%)** | 1.6 ms | **22.2 ms** | **45.0 fps** | 3,742 + 484 | 6,919 |
| **HIGH** | Software (ARM) | 16.20 / 14.28 | — | 33.5 ms | 29.8 fps | 4,970 | 8,889 |
| | **Hardware (GPU)** | **12.47 / 10.99 (−23%)** | 2.3 ms | **28.1 ms** | **35.5 fps** | 4,675 + 1,697 | 8,445 |
| **ULTRA** | Software (ARM) | 19.14 / 17.13 | — | 39.9 ms | 25.1 fps | 5,131 | 9,115 |
| | **Hardware (GPU)** | **13.12 / 11.59 (−32%)** | 2.4 ms | **29.6 ms** | **33.8 fps** | 4,939 + 1,805 | 8,838 |
| **EXTREME** | Software (ARM) | 19.63 / 17.15 | — | 40.5 ms | 24.7 fps | 5,421 | 9,571 |
| | **Hardware (GPU)** | **14.30 / 12.40 (−27%)** | 2.9 ms | **32.3 ms** | **31.0 fps** | 5,360 + 2,810 | 9,493 |

*(Triangles indicate base geometry + shadow/effect geometry. At HIGH preset, GPU rendering costs roughly the same CPU time as LOW preset on software ARM).*

### 6.3 Hero Model Complexity
Hero character models feature 4 levels of detail (LOD):
- **LOD 3 (Maximum)**: ~1,000 triangles and ~750 vertices (ranging from 652 to 1,394 triangles).
- **LOD 1 (Medium)**: ~570 triangles.
- **LOD 0 (Minimum)**: ~320 triangles and ~240 vertices (used for planar shadows and distant silhouettes).
- Triangle cost: ~1.3 µs per model triangle on GPU. Full hero models fit comfortably within a 30 fps budget (~1,900 tri/hero), with dynamic quality scaling automatically adjusting LOD to sustain target frame rates.
