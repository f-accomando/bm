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
