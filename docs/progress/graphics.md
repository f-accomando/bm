# Graphics & Video Subsystem

This document details the video architecture, 2D software blitter, 3D software rasterizer, VideoCore IV hardware 3D driver, and graphics performance benchmarks in **bm**.

---

## 1. Framebuffer & Display Modes

bm uses direct framebuffers requested from the Raspberry Pi VideoCore GPU via property mailbox channels ([`src/drivers/fb.c`](../../src/drivers/fb.c)).

### Video Specifications
* **Default Display Mode**: 640×360, 16:9 aspect ratio, 16-bit direct color (`RGB565`).
* **Hardware Video Scaler (HVS)**: The VideoCore hardware scaler enlarges the 640×360 or 320×180 image to 720p or 1080p outputs **at zero CPU cost**.
* **Direct 1080p Mode**: When running with hardware 3D (`gpu3d=1`), cartridges can render directly to native 1920×1080 without software upscaling.
* **Alternative Aspect Ratios**:
  * `320×180`: Low-resolution mode for retro pixel aesthetics.
  * `480×270`: Intermediate widescreen format used by *Overbit*.
  * `256×256`: Centered square viewport with black letterbox pillars, utilized by *Yharnam* and `.p8` carts.
* **Double Buffering**: Configured by allocating a virtual framebuffer twice the physical height. Swapping front and back buffers is performed by changing the virtual Y offset via mailbox tag `0x00048009` (`SET_VIRTUAL_OFFSET`), fully synchronized with the 60 Hz frame cycle to eliminate screen tearing.
* **Bandwidth Optimization**: The 16-bit RGB565 format halves frame traffic compared to 32-bit RGBA (0.46 MB per frame vs 0.92 MB), fitting within the tight memory bandwidth limits of the BCM2835 (~100 MB/s memcpy, ~430 MB/s fill).

---

## 2. 2D Software Renderer (`src/bm/gfx16.c`)

The 2D rendering engine is implemented in optimized C with ARM assembly inner loops:

### Primitives & Functions
* **Clear & Fills**: `cls(color)` clears the backbuffer or sub-rectangle via fast 32-bit word stores (2 pixels per store).
* **Geometry**: Lines (`line()`, Bresenham's algorithm), rectangles (`rect()`, `rectfill()`), circles (`circ()`, `circfill()`).
* **Sprite Blitting**:
  * `spr(n, x, y, [w, h, flip_x, flip_y])`: Fast blit from the 1024×1024 cartridge spritesheet.
  * `sspr(sx, sy, sw, sh, dx, dy, [dw, dh, flip_x, flip_y])`: Scaled and clipped sub-sprite blitter.
  * Transparency: Color 0 is treated as transparent; bitwise mask testing skips transparent spans.
* **Multi-Layer Tilemaps (`map()`)**:
  * Supports up to 8 discrete tilemap layers (LAYERS section 12 in `.bm`).
  * Tile flags (`FLAGS` section 13) provide per-tile collision masks (solid, ladder, platform, water, hazard) checked via `mflags()`.
* **Lighting & Shading**:
  * Dark levels and atmospheric fog via color look-up tables (`g16_fade_*`), enabling dynamic torchlight, lantern glows, and radial darkness without per-pixel floating-point math.

---

## 3. 3D Software Rasterizer (`src/bm/r3d.c`)

For environments without hardware 3D acceleration (e.g., QEMU or `gpu3d=0`), bm includes a complete, self-contained 3D software pipeline:

```
[Vertices & UVs]
       │
       ▼
[Transformation & Matrix Multiply] (Fixed-point 16.16)
       │
       ▼
[View-Frustum Culling & Near-Plane Clip]
       │
       ▼
[Perspective Division & Viewport Mapping]
       │
       ▼
[Lighting Calculation] (Gouraud shading with directional sun)
       │
       ▼
[Span-Based Rasterizer + 16-bit Z-Buffer]
```

* **Coordinates & Math**: Fixed-point 16.16 arithmetic ensures deterministic computation and prevents floating-point pipeline stalls on the ARM1176.
* **Depth Testing**: Uses a dedicated 16-bit integer depth buffer (`r3d_zbuffer`).
* **Rasterization Features**:
  * Flat-shaded, Gouraud-shaded, and affine/perspective-corrected textured triangles.
  * Sub-pixel correction avoids edge jitter during slow camera movements.
  * Backface culling rejects non-visible triangles prior to rasterization.
* **Skeletal Animation**: Computes bone hierarchy matrices in real-time, blending vertex positions via skinning matrices ([`src/bm/runtime.c`](../../src/bm/runtime.c)).

---

## 4. VideoCore IV Hardware 3D Driver (`src/gpu/gpu3d.c`, `src/gpu/v3d.c`)

bm features a proprietary bare-metal driver for the Raspberry Pi VideoCore IV 3D hardware (V3D), bypassing Mesa and Linux completely:

### Architecture: Tile-Based Deferred Rendering (TBDR)
1. **Binning Engine**: The screen is divided into 64×64 pixel tiles. The binning processor evaluates primitive bounding boxes and writes per-tile command lists into tile memory.
2. **Rendering Engine**: Each tile is cleared, rendered into the tile buffer, and resolved out to the target framebuffer.

### Programmable QPU Shaders ([`tools/qpuasm.py`](../../tools/qpuasm.py), [`src/gpu/shaders.h`](../../src/gpu/shaders.h))
The 12 Quad Processing Units (QPUs) run custom SIMD machine code:
* **Vertex Shaders**:
  * `vs_baked`: Renders static world geometry with pre-baked light colors.
  * `vs_tex_rgb`: Multi-varying textured geometry with vertex fog and corner ambient light.
  * `vs_lit`: Hardware skeletal animation and real-time directional sunlight calculated on QPU.
* **Fragment Shaders**:
  * Single-thread and dual-thread (`fs_*_t`) pixel shaders. Dual-thread shaders increase texture throughput by **20% to 33%** on hardware.
  * Texture sampling handles native 16-bit RGB565 and indexed T-format textures.

### Hardware Acceleration Features
* **Indexed Primitives**: Supports `INDEXED_PRIMITIVE_LIST` for compact mesh representations up to 65,535 vertices.
* **Early-Z Rejection**: Rejects occluded fragments before fragment shader execution.
* **Hardware 4x MSAA**: Sub-pixel anti-aliasing resolved directly during tile writeback.
* **Mesh Caching**: Meshes are copied to GPU-accessible arena memory with per-sheet texture caches to prevent redundant work submissions.

### Driver Milestones (`bm3d` Versioning)
The 3D driver tracks explicit progression milestones documented in [`docs/DRIVERS.md`](../DRIVERS.md):
* **bm3d 2.1**: Initial hardware triangle rendering with fixed-function pipeline.
* **bm3d 3.4**: First programmable vertex shader integration (`GPU+VS`).
* **bm3d 5.x**: Indexed meshes, two-thread fragment shaders (`fs2`), early-Z optimizations.
* **bm3d 6.6–6.8**: Hardware vertex clipper enabled by default, multi-varying sun-lit texture shaders (3 varyings), and robust indexed vertex buffer alignment.

---

## 5. Mali-G52 GPU Driver on RGB30 (`src/rgb30/mali.c`)

For the PowKiddy RGB30 (Rockchip RK3566), bm is bringing up a bare-metal driver for the ARM Mali-G52 (Bifrost v7):
* **Memory & MMU**: Configures Mali LPAE page tables over 64 MiB of video space.
* **Job Manager**: Submits job chains directly to the Mali command queues (WRITE_VALUE, fragment job descriptors).
* **Current Status**: bm3d 6.0/6.1 functional on hardware, executing hardware surface clears and debug color blocks.

---

## 6. Performance & Benchmark Comparisons

Measured on physical **Raspberry Pi Zero W** (1 GHz, 448 MiB RAM, 640×360 RGB565):

| Workload / Benchmark | Software ARM (`r3d`) | Hardware GPU (`gpu3d` / V3D) | Speedup Factor |
|---|---:|---:|---:|
| **16×16 Sprites (C)** | 4,482 sprites/frame | *N/A (2D hardware blit)* | — |
| **3D Spheres (triangles @ 60 fps)** | 2,700 tri/frame | **7,142 tri/frame** | **2.6×** |
| **3D Spheres (triangles @ 30 fps)** | 8,914 tri/frame | **15,346 tri/frame** | **1.7×** |
| **Texture Room (456 textured tris)** | 25.5 ms (39 fps) | **5.8 ms (60 fps)** | **4.4×** |
| **Overbit (Control Match on Partenope)**| 14.1 ms/frame | **9.6 ms/frame (GPU+VS)** | **1.5×** |
| **3D Bench Geometric Score** | 1,000 (baseline) | **3,506 (bm3d 6.7)** | **3.5×** |

### Key Bottleneck Findings
1. **Memory Bandwidth**: The BCM2835 SDRAM bus reaches ~100 MB/s on sequential reads and ~430 MB/s on writes. Minimizing full-screen frame reads is critical; the GPU TBDR pipeline avoids main RAM roundtrips during tile composition.
2. **Instruction Pipeline**: Moving skeletal transforms and vertex lighting from ARM to QPU vertex shaders reduces per-frame ARM load by **23% to 32%** in heavy combat scenes.

---

## 7. Retrospective: What Worked, What Failed, Discarded Paths & Next Steps

This section documents engineering iterations, failure post-mortems, and trade-offs discovered during graphics development:

### 1. Software 3D on ARM (`r3d`) vs Hardware GPU
* **Considered**: Pure CPU rasterization avoids reverse-engineering Broadcom GPU hardware.
* **Positive Outcome**: `bm3d 0.1` achieved working 3D; `bm3d 0.2` doubled performance via fixed-point edge arithmetic and DMA z-buffer clears (2,700 triangles/frame @ 60 fps).
* **Limitations**: Saturated the single 1 GHz core in complex scenes (e.g., *Texture Room* took 25.5 ms, *Overbit* stalled at 14.1 ms). Memory bandwidth (~100 MB/s read) made software z-buffering an inescapable wall.
* **Resolution**: Hardware offloading to VideoCore IV V3D engine (`bm3d 1.0+`), relegating `r3d` to a reliable fallback.

### 2. QPU Programmable Vertex Shaders
* **Considered**: Transforming vertices, bone skinning, and lighting on QPUs to free ARM CPU cycles.
* **What Failed (Failure 1 - bm3d 4.3)**: Rendered completely black on physical Pi Zero W (probe returned `0000 0000 0000 0000`).
  * *Root Cause*: Broadcom VPM attribute offsets and total sizes were passed in 32-bit words. Hardware requires offsets strictly in **bytes** (as done in Mesa `vc4`).
* **What Failed (Failure 2 - bm3d 6.5)**: Even with byte offsets, hardware reported `ERRSTAT: VPM write range` and froze.
  * *Root Cause*: Coordinate shaders expect a compact vertex unless the hardware clipper is **always active** (`gl_clip_all`).
* **Positive Outcome (bm3d 6.6)**: Keeping the clipper always on unlocked hardware vertex shading: 4× spheres, 11× hero meshes, and 9× in mixed scenes (79,071 triangles @ 60 fps; 3D score jumped to 2,601).

### 3. Early-Z Rejection vs In-Job Depth Clears
* **Considered**: Skipping pixel shading for occluded surfaces using VideoCore hardware early-Z.
* **What Failed (bm3d 4.4)**: Clearing depth between world and first-person weapon models (`zclear()`) via a full-screen quad (`fs_zclear`) caused subsequent geometry to vanish.
  * *Root Cause*: VideoCore early-Z tracking is updated exclusively by primitives with *early-z updates*. The quad shader bypassed this tracker, desynchronizing it. Furthermore, hardware erratum HW-2905 corrupts early-Z after tile reloads under 4x MSAA.
* **Resolution**: Primitives following `zclear()` run without early-Z (falling back to tile-buffer depth test). Early-Z is disabled during MSAA tile reloads but preserved during tile clears.

### 4. Dual-Thread Fragment Shaders (`fs2`)
* **Considered**: Running pixel shaders in 2-thread mode (`lthrsw`) to interleave instruction execution during TMU texture memory read stalls.
* **What Failed (bm3d 6.2–6.3)**: Flat-color shaders and untextured meshes suffered a 4–5% performance drop due to thread-switching overhead without texture latency to hide.
* **Positive Outcome (bm3d 6.4)**: Texture shaders default to 2 threads (+20% to +33% fillrate improvement); flat color shaders remain single-threaded and delay scoreboard locking until writeback.

### 5. Mesh Caching & Texture Swapping
* **Considered**: Caching GPU-transformed mesh data in arena memory to avoid regenerating vertex lists.
* **What Failed (bm3d 6.7)**: Cycling through 3 textures on a single mesh (`texswap` bench) caused throughput to collapse from 1,436 quads to 6.6 quads!
  * *Root Cause*: Cache only indexed by `mesh_id`. Switching textures invalidated the mesh copy, forcing the open GPU job to flush prematurely (one job per quad).
* **Resolution**: Keyed mesh cache by compound `(mesh_id, sheet_id)`. Restored throughput to 1,302 quads.

### 6. Indexed Meshes & Heap Overruns
* **Considered**: `INDEXED_PRIMITIVE_LIST` for models up to 65,535 vertices.
* **What Failed (bm3d 6.8)**: *Overbit* crashed intermittently with a Data Abort inside `free()` (heap corruption).
  * *Root Cause*: Indexed vertex copy routines aligned bone groups to 16-byte boundaries, but the buffer allocation did not account for padding. Multi-bone flat meshes wrote up to 12 bytes past the arena buffer directly into adjacent heap headers.
* **Resolution**: Group size allocations padded to 16 bytes. Added 16-byte guard zones to the emulator.

### 7. Discarded Architectural Paths
* **Real-Time Neural Graphics / Upscaling (AI.md)**: Discarded. A full-screen pass reads and writes 0.46 MB (~5 ms on the bus before compute). The VideoCore hardware scaler already enlarges 640×360 to 1080p for free.
* **Pure Software 1080p Rendering**: Discarded. Software clearing and drawing at 1920×1080 requires 4 MB per buffer, choking ARM bandwidth. 1080p is exclusive to GPU modes.
* **Full 2D Pipeline on QPU**: Discarded. Quads and fonts run in GPU jobs (`gpu3d_2d=1`), but complex 2D primitives (circles, non-integer scaling, lines) remain on ARM because QPU curve math and clipping logic exceed the 16.6 ms frame budget.

### 8. Pending & Alternatives to Explore
* **Mali-G52 GPU on RGB30 (M41)**: Surface clears and job manager functional; triangle rasterization and vertex shading pending.
* **QPU Compute Outside 3D (M44)**: Offloading particle systems and 2D dynamic lighting to QPUs when 3D is idle.
* **Sprite Stacking (M43)**: Pseudo-3D volume rendering via rotated 2D slices in `gfx16.c`.

---

## 8. Resolution Scaling & Upscaling Strategies (Nearest, Bilinear, Bicubic, FSR vs. DLSS)

Scaling low-resolution internal render targets (320×180, 480×270, 640×360) up to HD/FHD display outputs (720p, 1080p) involves distinct quality-versus-bandwidth trade-offs on the BCM2835:

### 1. Comparison of Scaling Techniques

| Technique | Computational Cost | Visual Quality Profile | Feasibility on Pi Zero W / BCM2835 |
|---|---|---|---|
| **Nearest-Neighbor** | Extremely cheap (coordinate stepping only) | Sharp integer pixels, but heavily pixelated/aliased on 3D geometry | **Ideal for 2D retro pixel art** (integer 2×/3× scale); unsuitable for 3D camera rotation. |
| **Bilinear** | Cheap (4-tap linear interpolation) | Smoother than nearest, but causes noticeable edge and texture blur | **Free in Hardware**: Handled natively by the VideoCore Hardware Video Scaler (HVS) during scanout. |
| **Bicubic / Catmull-Rom** | Moderate/High (16-tap cubic kernel) | Noticeably sharper edges than bilinear, preserves gradients | Feasible on QPUs as a post-processing pass or offline for asset scaling; costly for CPU. |
| **FSR-Style Spatial (EASU + RCAS)** | Moderate (12-tap edge-adaptive kernel + contrast sharpening) | Reconstructs geometric edges cleanly along gradient directions; near-native visual sharpness | **Practical analytical approach**: Operates without AI/temporal vectors; can run on QPUs or for asset scaling. |
| **DLSS (Deep Learning Super Sampling)** | Extremely high (Tensor Cores / NPU + Temporal vectors) | Temporal reconstruction with deep neural network inference | **Unfeasible on ARM1176**: Requires motion vectors, jitter buffers, and tensor accelerators absent on the SoC. |

### 2. Deep Dive: FSR-Style Spatial Upscaling vs. DLSS

Modern game upscalers fall into two broad architectures:
1. **Temporal Neural Reconstruction (DLSS)**:
   * Requires per-pixel motion vectors, previous frame history reprojection, camera jitter, and dense deep neural network evaluation (Tensor Core MAC operations).
   * *Verdict on BCM2835*: Discarded. The Pi Zero W lacks an NPU, and evaluating a neural network per pixel would consume dozens of frames per second just for inference.
2. **Edge-Adaptive Spatial Upsampling (FSR 1.0 Style)**:
   * **EASU (Edge-Adaptive Spatial Upsampling)**: Evaluates a 12-tap spatial neighborhood (cross-stencil) to detect the direction and magnitude of luminance gradients, interpolating along edges rather than across them.
   * **RCAS (Robust Contrast-Adaptive Sharpening)**: Follows EASU with a local contrast-preserving unsharp mask that restores micro-contrast without creating ringing halos around dark/light boundaries.
   * *Verdict on BCM2835*: **Computationally viable**. Because it is a purely analytical spatial filter with no temporal state and no neural weights, its math fits within QPU SIMD instruction sets.

### 3. The Real Hardware Bottleneck: HVS Hardware vs. Shader Passes

When evaluating modern shader-based upscaling (Bicubic or FSR) on the Raspberry Pi Zero W, the true constraint is **memory bus bandwidth**:

1. **Broadcom Hardware Video Scaler (HVS)**:
   * The VideoCore display controller reads the 640×360 or 480×270 framebuffer during HDMI scanout and resamples it on-the-fly to 720p/1080p.
   * **Cost**: **0 ms CPU/GPU time, 0 MB/s extra SDRAM writes**.
2. **Shader-Based Upscaling (QPU FSR/Bicubic Pass)**:
   * To apply a custom spatial upscaler, the pipeline must:
     1. Render the game at 480×270 or 640×360.
     2. Run a full-screen post-processing shader pass.
     3. Write the resulting 1920×1080 framebuffer to physical SDRAM.
   * A 1920×1080 RGB565 frame requires **~4.15 MB**. At 60 fps, writing this buffer consumes **~249 MB/s of bandwidth** (or ~41.5 MB/s at 10 fps). On a memory bus delivering ~430 MB/s fill rate, this memory traffic directly starves CPU and GPU rendering.

### 4. Optimal Roles for FSR-Style Spatial Upscaling in bm

Given these hardware realities, FSR-style edge-adaptive scaling is most effectively deployed in two specific domains:
* **Asset & Cover Upscaling (Offline / Semi-Static)**:
  * Enhancing imported low-res 2D textures, game cover art for the Market, and sprite sheets (the "3D→sprite" pipeline in M22 / AI.md) without real-time 16.6 ms deadlines.
* **Low-Resolution Intermediate Passes**:
  * Scaling ultra-low-resolution 3D targets (e.g., 320×180 or 480×270 up to the native 640×360 system buffer). Because 640×360 RGB565 is only **0.46 MB**, writing the output consumes negligible bandwidth (~27.6 MB/s at 60 fps), after which the HVS hardware scales the 640×360 image to 1080p cleanly.


- Rendering tests stoppable (2026-10-10): 3D Bench, Render bench, Stress test (C and Lua parts) and Texture Room stop on Start+Select, Ctrl+Esc or PS after the frame on screen (no GPU job in flight); a stopped test leaves no report (the capture dropped with `reports_drop`, nothing in `bm/bench` or `bm/reports`, nothing sent), the Dev tab goes straight back to the menu and a monitor line ends there (`syskeys_test_*` in `src/kernel/syskeys.c`, `b3d_platform_t.stop`, `bm_stats_t.left`; tests: `make test-b3d` `--stop`, QEMU `test_bench_stop`).
