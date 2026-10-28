title: Graphics techniques catalogue
accent: techniques
lang: en
kicker: A bm technical manual
subtitle: Every 2D and 3D technique of bm on the CPU, the V3D (Pi Zero W) and the Mali-G52 (RGB30): what it is, what is built, what it costs, and what is not built.
footer: bm manuals - graphics-techniques

::: readme
**What this is.** A list, one technique at a time, of what bm does to draw: 3D rasterising, lighting, textures, 2D sprites and maps, light tables, and the work that the GPUs take. Each row says if the technique is **Built**, **Partial**, **Not built** or **Dropped**, and why when it is not built.

**How to read it.** Three machines: the **ARM** (Pi Zero W, one core; RGB30, four cores), the **V3D** (VideoCore IV of the Pi Zero W) and the **Mali** (Mali-G52 of the RGB30). The last column gives the equivalent in the public APIs (OpenGL ES, Vulkan, Direct3D, OpenVG, OpenCL, OpenVX, SDL2 and SDL3, Mesa) so you can compare.

**Where the numbers come from.** Benchmark reports of the 3D Bench, 640x360, RGB565 on the Pi and XRGB on the RGB30. Sources: `docs/PERF-BUDGET.md`, `docs/progress/graphics.md`, `docs/progress/graphics-mali.md`, `docs/progress/graphics-vector.md`, `docs/progress/multicore.md`. A number marked *not measured* was never measured. Nothing is invented.
:::

# How to read the tables

| Word | Meaning |
|---|---|
| Built | Works in the kernel and has a test. |
| Partial | Works for some cases. The row says which. |
| Not built | Nothing in the code. The row gives the reason or says that nobody decided. |
| Dropped | Tried or planned, then left out. The row gives the reason. |

Load figures are "at 60 fps": how many of the thing fit in one frame of 16.6 ms. The Pi Zero W is the reference machine. A RGB30 number has its own note.

# 3D on the CPU (ARM rasteriser)

The ARM transforms, lights, clips and draws the triangles itself. It is the only 3D path on the RGB30 before the Mali, and the fall-back of both GPUs. Source: `src/bm/r3d.c`.

| Technique | State | What it does | Measured | Equivalent |
|---|---|---|---|---|
| Triangle rasteriser | Built | Edge functions in fixed point. Flat or Gouraud (dithered) fill, one pixel at a time. | Pi: 70.8 spheres of 96 faces at 60 fps. RGB30: 177.3 (old report, bm3d 6.8) | GL fixed pipeline raster; Vulkan `vkCmdDraw` |
| Depth buffer | Built | 16-bit z-buffer in RAM, cleared with DMA. | Part of the figures above | GL depth test; Vulkan depth attachment |
| Perspective-correct textures | Built | Texels from the sheet with correct divide; loops specialised by case. | Pi: hero with texture 3.4 at 60 fps | GL `texture2D`; D3D sampler |
| Near-plane clipping | Built | The ARM cuts triangles on the near plane. | Not measured alone | GL clip volume |
| Back-face culling | Built | The side where vertices go clockwise is visible. | Not measured alone | `glCullFace` |
| Light: sun, sky and ground, specular, rim, point lamps | Built | Computed per vertex or per face on the ARM with the VFP. | Part of the figures above | GL fixed lights; D3D lights |
| Fog | Built | Mix with a fog colour by depth. | Not measured alone | `glFog` (GL 1.x) |
| Per-face materials | Built | Emissive, glossy, screen-door (checker transparency), LOD. | Not measured alone | Material state in D3D |
| Planar shadows | Built | A flattened copy of the mesh on a floor plane. | Pi: hero with shadow, ARM misses 60 fps (24.7 ms for 1 hero) | Projection shadows (old GL technique) |
| Rigid skinning | Built | One bone per vertex; two-bone groups on the GPU. | Pi: hero of 1536 triangles with 16 bones, 3.9 at 60 fps | GPU skinning in a vertex shader |
| Fast mode (`r3d_fast=1`) | Built | One object-to-camera matrix, light in model space. | 2.2 to 3.6 percent fewer ARM instructions per triangle | - |
| Flat textures (flag 128) | Built | A textured face is drawn in its average texel. | Not measured alone | - |
| Cel shading | Built | Light cut into 4 bands. Option `dither3d`. | Cost against sun light: x1.00 on the ARM (Pi) | Toon ramp in a fragment shader |
| Outline (inverted hull) | Built | Draws the back faces larger, in a dark colour (flag 512). | x1.34 to x1.50 of the lit hero on the ARM (Pi) | Hull outline (common technique) |
| Point, line and billboard | Built | Sprites with depth in the 3D scene. | Not measured alone | Point sprites |
| Impostors (`r3d_impostor`) | Partial | Far heroes drawn as a picture. Off by default: a Pi session at 8 gave no gain. | Pi: 57.7 fps, GPU 7.8 ms a frame at setting 8 | Billboard impostors |
| 3D particles | Built | ARM step and draw (`particles*`). | Not measured on the Pi | Particle systems; Compute shader |
| Sprite stacking | Built | A model made of slices (`stack`, `zstack`, `stackv`). ARM only. | Pi: 54.6 stacks of 8 slices at 60 fps | - (a bm technique) |
| LOD by distance | Built | Mesh level by size on screen. | Not measured alone | Tessellation / LOD |
| Shadow maps | Not built | Nobody decided. Planar shadows exist. | - | Shadow mapping (GL, Vulkan, D3D) |

## The ARM on four cores (RGB30)

| Technique | State | What it does | Measured | Equivalent |
|---|---|---|---|---|
| Faces split among the cores | Built | `offload_par` gives one block of faces to each core, 96 faces or more, flat or baked meshes without texture. A hung core is marked and the caller runs its block. | RGB30 3D Bench ARM score 466 -> 601 | Parallel vertex processing |
| Pixels in bands of rows | Built | Each core draws its own rows of the page (`r3d_bands`, flat, Gouraud, textured). Same pixels as one core. | `heroes` 8.0 -> 15.8, `heroes_tex` 6.3 -> 12.7, `clip` 325 -> 692 | Tile-based parallel raster |
| Draw build for the Mali on the cores | Built | `build_draws` in `mali.c` splits the draws among the cores in blocks (32 draws or more). Key `mali_par`. | Games 451 -> 491, GPU 433 -> 513 | Multi-threaded command recording (Vulkan) |
| Cores on the Pi Zero W | Not built | The Pi Zero W has one core. A first try with 4 bands of tables lost the firmware mailbox. | - | - |
| Lighting per face on worker cores | Not built | Idea Q2 of the Mali queue. The loop is a pure function of the face range. | - | - |

# 3D on the V3D (Pi Zero W)

The V3D is the VideoCore IV (a tile renderer). bm drives it without Linux and without the firmware 3D library. Source: `src/gpu/gpu3d.c`, `v3d.c`, shaders written in `tools/qpuasm.py`. Version history: `docs/DRIVERS.md`.

| Technique | State | What it does | Measured | Equivalent |
|---|---|---|---|---|
| Tile binning and render | Built | The binner makes the tile lists; the renderer draws tile by tile. Two hardware jobs per frame. | Raw: 2966 thousand small triangles a second; 811 Mpixel a second (full screens, render only) | Tile-based GPU (Mesa vc4) |
| Fragment shaders on the QPU | Built | Flat, Gouraud, textured, screen-door, cel. | `quad_flat`: 163.7 quarter screens at 60 fps (ARM 11.5) | GL fragment shader |
| Vertex shaders on the QPU | Built | Transform, light, bones, shadows; the GPU places the corners (`gpu3d_vs=2`). | `heroes`: 66.3 at 60 fps (GPU alone 6.3, ARM 3.9); `spheres` 782.9 (ARM 70.8) | GL vertex shader |
| Coordinate shaders and near clip | Built | A separate shader gives clip coordinates for the binner. The GPU clips on near plane and guard band. | Part of the vertex figures | Vulkan binning pass |
| Indexed meshes (16-bit) | Built | Shared vertices are set up once. Meshes up to 65535 vertices and faces. | `big`: 9.2 models of 10080 triangles | `glDrawElements`; `vkCmdDrawIndexed` |
| Depth and early z | Built | 24-bit depth in the tile buffer. Early z is off after a `zclear()` and with MSAA. | Not measured alone | Depth test; early-Z |
| MSAA 4x | Built | Option `gpu3d_aa`. Antialiasing is the only antialiasing in bm. | Not measured in the sheets used here | `GL_SAMPLES`; Vulkan MSAA |
| Job queue (1 job) | Built | The GPU job starts at the end of the frame. The next `_update` runs meanwhile. Default on the Pi. | `queue` test: 557.8 -> 631.1 spheres (+13 percent) | Async submit (`vkQueueSubmit`) |
| Job queue (2 jobs) | Built | Two job memory blocks; the ARM fills one while the GPU draws the other. | `herotris`: 49956 triangles at 13.7 ms (Q) and 50024 at 13.5 ms (Q2) | Frames in flight |
| Frame held one frame (`gpu3d_later`) | Built, off | Costs a frame of latency. | +0.4 percent `herotris`, +10 percent `heroes` | - |
| Draw merging (`gpu3d_merge`) | Built | Many small pieces become one mesh in world axes. Only from 32 pieces. | 100 pieces: +24 to +33 percent; 10 big pieces: -26 percent | Instancing / draw batching |
| Textures RGBA8888 (T-format) | Built | Up to 8 textures in a job. | `quad_tex`: 131.1 quarter screens | `glTexImage2D` |
| Textures RGB565 and RGBA5551 | Built, off | Half the memory and half the texture reads (`gpu3d_tex16`). No gain on the Pi (21 Oct). | - | Compressed or packed formats |
| Textures ETC1 | Built, off | `gpu3d_etc1`, encoder in `src/gpu/etc1.c`. No gain on the Pi. | - | ETC1 (GL ES 2 extension) |
| Bilinear filter | Built | `gpu3d_filter=1`. The look differs from the ARM. 2D stays nearest. | Fill: 2.3 ns a pixel against 1.65 nearest | `GL_LINEAR` |
| Compact vertex attributes | Built, off | `gpu3d_attr=1`. `herotris` 54056 plain, 54013 compact: no gain, the frame is ARM-bound. | 72.2 heroes against 66.2 in `heroes` only | Packed vertex formats |
| Hardware sort (`gpu3d_sort`) | Dropped | Off: the numbers show no gain. | `heroes`: 14.2 ms against 15.9 ms but the pixels field was 0 | Sorting transparent geometry |
| Cel shading on the QPU | Built | Light cut into 4 bands in the vertex programs. | x0.99 to x1.02 of sun light; 50 heroes at 12.1 ms | Toon shader |
| Outline on the GPU | Built | Inverted hull in the vertex programs. | x1.05 to x1.23 of the lit hero; 42.4 heroes at 14.4 ms | - |
| Draw calls | Built | One cube per call. | 1386 calls at 60 fps (12 us a call; ARM 467) | Draw-call count |
| Many triangles at 60 fps | Built | `herotris` with the vertex shader and the queue. | About 50000 triangles at 13.7 ms | - |
| Compute on the QPU | Built | Programs outside 3D: fade by levels and a particle step (`gpu3d_qpu`). Only `dark_end` uses them. | Fade 8.6 to 9.3 ms against 20.4 ms; particles 1024 in 47 to 52 us against 158 to 171 us | OpenCL kernels; compute shaders |
| Chained jobs (CT0/CT1) | Not built | Only if a measurement shows the GPU idle between jobs. It is not: the frame is ARM-bound. | - | - |
| Geometry and tessellation shaders | Not built | The VideoCore IV has no such stages. | - | GL 4 geometry and tessellation |
| Ray tracing | Not built | No hardware; never considered. | - | `VK_KHR_ray_tracing` |

# 3D on the Mali-G52 (RGB30)

The Mali is a tile renderer of the Bifrost family. bm builds its job descriptors from Mesa (`v7.xml`, MIT) and its shaders with Mesa's Bifrost compiler (`tools/mali/mkshader.c`). Source: `src/rgb30/mali.c`, `mali_desc.c`. Table of parity with the V3D: `docs/progress/mali-parity.md`.

| Technique | State | What it does | Measured | Equivalent |
|---|---|---|---|---|
| Triangle through Vertex, Tiler and Fragment jobs | Built | Checked on the device with a pixel check. | - | Panfrost job chain |
| Vertex shader (`BM_VS`, `BM_VS3`) | Built | Position in pixels, or a 4x4 matrix pushed per draw (model-space vertices). | `draws`: 533.8 at 60 fps with the shader against 116.5 without | Vertex shader |
| Fragment shader, flat colour | Built | One colour a draw, pushed to the shader. | `quad_flat`: 143.2 quarter screens (ARM 15.8) | Fragment shader |
| Per-face light, cel and fog | Built | Computed on the ARM in `be_mesh`, then sent as draws. | The cost is on the ARM: 6960 instructions a triangle in `spheres`, against 1213 in `spheres_unlit` | Per-vertex light |
| Textures (opaque, nearest) | Built | Texture, Surface and Sampler descriptors, a 2 MB pool, key `mali_tex`. Alpha, screen-door and bilinear stay on the ARM. | `quad_tex` 6.4 -> 126.3; games score 482 -> 530 | `glTexImage2D` |
| Draw batching in one pass | Built | 4 MB arena, 1024 draws in one GPU pass; more than that merges in screen space. | Games score 192 -> 247; GPU score 14 -> 228 (bm3d 7.15) | Command buffer |
| Async job queue (2 sets) | Built | The ARM works while the GPU draws (`gpu3d_queue=1` or `2`). | 2 frames in flight finish in 204 us | Frames in flight |
| Resolve into the page | Built | Cached 2 MB resolve; key colour `0xFF00FF` marks "not covered". | `resolve_us` 0.8 to 2.1 ms in a 14 ms frame | Present / blit |
| Draw build on the cores | Built | See the four-core table above. | Games 451 -> 491 | - |
| Hardware Z-buffer and early z | Partial | In work (track G2 of `docs/progress/multicore.md`). Today the ARM sorts triangles far to near (`sort_tris`). | - | Depth attachment |
| Lighting per face in the vertex shader | Not built | Queue item Q2. The Pi does it in its vertex shader; the Mali only transforms. | The Pi `spheres` 782.9 against 22.9 | Per-vertex lighting |
| Alpha blend and bilinear on the GPU | Not built | They stay on the ARM. | - | `glBlendFunc`, `GL_LINEAR` |
| MSAA | Not built | No Mali setting is used. | - | MSAA |
| 2D on the Mali | Dropped for sprites | Each draw costs about 4 us, so 600 sprites cost 2.4 ms, more than the ARM path (about 0.5 ms). Worth it only for big layers and fades, after the texture work. | - | - |
| Mali beats the ARM raster | Partial | In some scenes the ARM on four cores is ahead: `spheres_unlit` ARM 164.4 against GPU+VS 103.8; `heroes`, `baked`, `shine` and `split` the same. The Mali wins `quad_flat` and `draws`. | See rows above | - |

# 2D on the CPU

The 2D code (`src/bm/gfx16.c`) is the same on the Pi and the RGB30. Pixels are RGB565.

| Technique | State | What it does | Measured | Equivalent |
|---|---|---|---|---|
| Primitives | Built | `line`, `rect`, `rectfill`, `circ`, `circfill`, `tri` (flat or Gouraud with three colours), `pset`, `pget`. | Not measured alone | `SDL_RenderLine`, `SDL_RenderFillRect`, OpenVG `vgDrawPath` |
| Clip rectangle | Built | `clip` limits every call. | - | `SDL_SetRenderClipRect`, scissor |
| Sprites | Built | `spr`, `sspr` with zoom and flip; 1-bit alpha (a pixel is on or off). | 2574 sprites 16x16 at 60 fps (6.5 us each) | `SDL_RenderTexture` |
| Tile map | Built | `map()` up to 8 layers, tile flag mask, packed copy of the cells (128 bytes a cell). | One opaque full-screen layer 3.08 ms; 7.7 layers fit 60 fps. Packed pool: 2.9 -> 2.0 ms on Yharnam | Tilemap in a game engine |
| Bitmap text | Built | Fonts 6x12, 8x14, 8x16, 16x32, 24x48, integer scale. | Not measured alone | `SDL_ttf` bitmap use |
| Sprite stacking, rotation of a slice | Built | `stack()` rotates and scales a slice (nearest sampling). | 54.6 stacks of 8 slices at 60 fps | Affine transform of an image (OpenVG) |
| Light tables (`fades`, `g16_fade_apply`, `g16_fade_glow`) | Built | Darkness and lamps by a table of colour levels. | Full-screen fade 20.3 ms; light 30.7 ms (Pi); at 30 fps 24.7 and 20.3 lamps | Colour lookup table |
| Screen modes | Built | `screen(w, h)` from 320x180 to 1920x1080; the HDMI scaler enlarges at no CPU cost. | - | `SDL_SetRenderLogicalPresentation` |
| Bitmap blit with scale | Partial | Works. It divides for every pixel; the ARM1176 has no divide instruction. A table of x indices would be 2 to 3 times faster (estimate). | Not measured | `SDL_RenderTexture` with scale |
| Text run drawing | Partial | `g16_text` tests every pixel. Drawing runs would be about 2 times faster (estimate). | Not measured | - |
| Four-core bands for blit, light and `cls` | Not built | RGB30 only, about 3 times on full-screen passes (estimate). The `r3d` band pattern exists. | - | - |
| Per-file `-O3` for 2D and 3D (`PI_O3_FILES`) | Partial | Built and tested on the host; not measured on the Pi. Accept only with 3 percent less `ms` in `gpu2d` and `mix` and 5 percent more kernel. | - | - |
| 2D benchmarks | Built | `b3d tests=map8,sprites,stack,fade,light,gpu2d`. | See `docs/PERF-BUDGET.md` | - |
| Sprite rotation with a transform stack | Not built | API gap against LÖVE (API-7). A design note comes first. | - | `SDL_RenderGeometry`; Vulkan transform |
| Off-screen render targets | Not built | API gap (API-8 to API-11). | - | `SDL_SetRenderTarget`; OpenVG `VGImage` |
| `palt` with alpha, blend modes | Not built | API gap. The only blend is `g16_fade_glow`. | - | `SDL_SetTextureBlendMode`; OpenVG blend modes |

# 2D on the GPUs

| Technique | State | What it does | Measured | Equivalent |
|---|---|---|---|---|
| 2D over the 3D in the V3D job | Built | Rectangles, points, lines, `spr`, `sspr` (whole zooms), `map` cells and `print` text become quads in the same job. | 3569 sprites or more at 60 fps (ARM 2574) | Textured quads |
| Light by the GPU (`gpu3d_light`) | Built | The V3D lights the page that `dark_begin` .. `dark_end` makes. Up to 12 lamps in a tile of 64x64 pixels. The lamps add up; the ARM light has levels. | Yharnam: light 12.9 ms -> 1.7 ms; 31.6 -> 50.1 fps autoplay. Pi only | Deferred light pass |
| Fade by the QPU (`gpu3d_qpu`) | Built, off | Full-screen fade on the QPU. | 8.6 to 9.3 ms against 20.3 ms | Post-process pass |
| Menu on the V3D (`menu_scale`) | Built | The menu is drawn at 720p or 1080p by the V3D, enlarge cost 0. 1080p verified; 720p not yet. | - | - |
| Map as a resident batch of quads | Not built | The idea: a block of 16x16 cells is one buffer made once. The GPU 2D today builds each quad again: slower than the ARM on a big map (44.3 fps against 50.5 on Yharnam). | Estimate: 2 to 2.5 ms of gain, not measured | Vertex buffer object |
| 2D only on the GPU (`gpu2d_only`) | Dropped for big maps | Slower than the ARM. | See above | - |
| Light by the Mali | Not built | No Mali equivalent of the V3D light shader. Considered only if Yharnam runs under 60 fps on the RGB30. | - | - |
| Fade of `dark_end` as a fragment shader | Not built | The colour tables have 65536 entries. Only a scale without a table fits, and the pictures would differ. | - | - |
| Copy to the page by the GPU | Dropped | DMA takes 3.1 ms against 1.8 ms on the ARM. | - | - |

# The OpenVG and VGU list (2D vector)

bm does not adopt the OpenVG API: its Pi firmware version needs the VideoCore firmware services, and the Mali has no OpenVG. Each item below would be a function of bm, in the style of `docs/api/draw.md`. Costs are estimates from the code. Source: `docs/progress/graphics-vector.md`. Antialiasing is out of scope.

| Technique | State | What it does | Estimate | Equivalent |
|---|---|---|---|---|
| Solid colour paint | Built | Every draw call. | - | `vgSetColor` |
| Linear gradient | Partial | `tri` with three colours; `rectgrad` would be two Gouraud triangles. | 0.05 ms | `vgSetParameter` gradient |
| Radial gradient and spread | Not built | Forward differences on the squared distance and a 256-entry table. Priority 1. | About 6 cycles a pixel | OpenVG radial gradient |
| Path (move, line, quad, cubic, arc, close) | Not built | Subdivide curves, an edge list and scanline fill. Priority 2. | 3 ms full screen; 0.02 ms for a 32x32 icon on the Pi | `vgAppendPathData` |
| Fill rules (even-odd, non-zero) | Not built | Part of the path code. | About 0 extra | `VG_EVEN_ODD` |
| Stroke (width, caps, joins, miter, dash) | Not built | Offset quads and round joins. Only 1 pixel lines exist. | - | `vgDrawPath(VG_STROKE_PATH)` |
| VGU shapes (round rect, ellipse, arc, polygon) | Not built | `rrect`, `ellipse`, `poly` on top of `circfill` and `rectfill`. Priority 1. | `rrect` 0.03 ms | `vguRoundRect`, `vguEllipse` |
| Affine transform of an image | Partial | `stack` rotates. `sspr_affine` with a 2x2 matrix would use `stack_core`. Priority 1. | 16x16: 0.01 ms (Pi) | `vgDrawImage` with a matrix |
| Perspective warp (VGU) | Not built | Two textured triangles. Priority 3. | 0.02 ms for 16x16 | `vguComputeWarpQuadToQuad` |
| Image formats with real alpha | Partial | Today alpha is on or off. A coverage byte 2-255 in the sheet would work with `blit_alpha`. | About 8 cycles a pixel more where alpha is not binary | ARGB images |
| Blend modes | Not built | `blend(mode)`: alpha, add, multiply, screen. | Full screen 2 ms (Pi); 0.5 ms with bands (RGB30) | `VG_BLEND_*`; `SDL_BlendMode` |
| Mask layers | Partial | Tile flags only. | One more load a pixel | `vgMask` |
| Pattern paint | Partial | `spr` and `map` cover most uses. | - | Pattern paint |
| Colour matrix, convolve, Gaussian blur | Not built | Box blur with running sums. Blur 320x180: 1.2 ms (Pi); 0.1 ms with bands (RGB30). | See text | `vgColorMatrix`, `vgGaussianBlur`; OpenVX filters |
| Vector fonts | Not built | `stb_truetype` (public domain), glyphs cached as an alpha sheet. | 0.05 to 0.1 ms a glyph; cached blit 0.01 ms | `vgDrawGlyph`; `SDL_ttf` |
| Antialiasing of 2D shapes | Dropped | Out of scope (user, 28 Oct). | - | `VG_RENDERING_QUALITY` |

Order of work for 2D, agreed with the user: the "better" items of the existing code first, then `rectgrad`, `rrect`, `ellipse`; then `sspr_affine` and alpha; then paths and stroke; then vector fonts and blur. The Mali takes 2D only for big layers.

# What the other APIs have that bm does not

| API | Feature | bm |
|---|---|---|
| OpenGL ES 2 / Mesa vc4 | Programmable shaders on the VideoCore IV | Built, with own QPU assembler (`tools/qpuasm.py`), no OpenGL |
| Vulkan | Command buffers, explicit sync, several queues | Partial: a job queue with two sets, no general command API |
| Direct3D | Feature levels, shader models | Not applicable |
| OpenCL | General compute | Partial: QPU programs for the fade and particles. No OpenCL |
| OpenVX | Graph of image filters (blur, convolve, colour) | Not built (see the OpenVG list) |
| SDL2 / SDL3 | `SDL_Renderer`: blend modes, scaling, render targets, geometry | Partial: scaling and clip. No blend modes, no render targets |
| Mesa Panfrost | Open driver for Bifrost | Used: bm takes the job descriptors (`v7.xml`) and the shader compiler |
| Mesa vc4 / v3d | Open driver for VideoCore | Read for the structures and the probes. Linux code is read, never copied |

# Open points

- Pi Zero W: the 2D scenes `map8`, `sprites`, `stack`, `fade` and `light` were measured once (22 Oct). The `-O3` list for `gfx16.c` is not measured.
- RGB30 with the Mali: only the 3D Bench figures from 25 and 28 Oct exist. The 2D scenes are not measured on the RGB30.
- Flips and alpha each alone, 2D particles and 3D particles on the Pi: not measured.
- [TO FILL: the Mali-G52 hardware Z-buffer figures, when track G2 ends]
- [TO FILL: the RGB30 figures of the four-core 2D bands, which are not built]
