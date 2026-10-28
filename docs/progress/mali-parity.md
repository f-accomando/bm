# Mali vs Pi V3D parity (RGB30)

Split from [graphics-mali](graphics-mali.md). The driver state and the open work are there.

## Mali vs Pi V3D Architectural Equivalence & Feature Parity (API-5)

Mapping of Mali-G52 developments against reference Pi V3D driver:

| Pipeline Stage / Feature | Pi Zero W (V3D 2.1) | PowKiddy RGB30 (Mali-G52) | Parity Status & Notes |
|---|---|---|---|
| **Framebuffer / Clear** | FBD (Framebuffer Descriptor) / Tile setup in RGB565/RGBA8888 | SFBD / MFBD (Single/Multi FB Descriptor) in linear XRGB8888 | **Full Parity** (HW verified; 64x64 clear & screen presentation pass) |
| **Vertex Processing** | Coordinate / Vertex Shaders on QPU (`vs_colour`, `vs_lit_tex`) | Compute Job on slot 1 with Bifrost vertex shader (`BM_VS`, `BM_VS3`) | **Full Parity** (HW verified in `mali_backend.mesh` with `gpu3d_vshader=1/2`) |
| **Binning / Tiling** | Binner Job (`BPOA` / `BPOS`), tile allocator pool with overflow | Tiler Job on slot 1 (`SO_HEAP`, `SO_HEAPD`, `SO_TCTX`, mask 0x28) | **Full Parity** (HW verified; Primitive Size 1.0f fixed fault) |
| **Fragment / Raster** | QPU Fragment Shaders (`fs_colour`, `fs_tex_lit`, `fs_cel`) | Fragment Job on slot 0 (`BM_FS` with pushed color words 4-7) | **Flat Colour Parity** (HW verified; textured shader done: `BM_FS_TEX`) |
| **State Management** | NV Shader State Record / Blend configuration | `Renderer State` (`md_renderer_state_t`) + `Blend` per target | **Full Parity** (Descriptors from Mesa `v7.xml`) |
| **Depth & Sorting** | Hardware Early-Z / Late-Z buffer attachment and depth test | Depth test in the tile buffer (`mali_z=1`, less or equal, cleared to 1); sort only when a frame does not fit one pass | **Parity**: hardware depth test, no ZS writeback |
| **Textures & Lighting** | TMU texturing (nearest, bilinear), lit faces, ETC1, 16-bit sheets | Opaque textures on GPU (`mali_tex=1`); sun and ambient light in the vertex shader for rigid meshes (`mali_lit=1`); rim, lamps, gloss, cel, fog, skinned meshes and alpha, screen and bilinear textures stay on ARM | **Partial Parity**: textures and sun-lit meshes on GPU |
| **Cel Shading** | `fs_cel` on QPU with 64-entry quant table and inverted hull outline | Per-face cel quantization (`cel_step`) in `be_mesh` | **Partial Parity**: Flat-shaded cel quant on GPU; smooth per-pixel cel on ARM |
| **Job Queue & Async** | `queue=1`/`queue=2` (`JOB_BLOCK`/`JOB2_BLOCK`), ARM/GPU overlap | 2 memory sets (`MALI_SET_SIZE`) + async Slot 0 Fragment Job (`mali_backend_submit`) | **Full Parity** (HW verified in 3D Bench `GPU+VS+Q` / `GPU+VS+Q2`) |
| **Draw Merging** | `gpu3d_merge` (bm3d 7.6, world-space multi-mesh batching) | 4 MB single-pass arena (`1,024` draws) + screen-space merge (`>1,024` draws) | **Full Parity** (HW verified; merges up to `1,024` draws in 1 GPU pass) |
| **Config & Safety** | `gpu3d=1` default, automatic fallback to ARM | `mali3d=1` default (`bm3d 7.15`), clean ARM fallback on any fault | **Full Parity** (`gpu3d_init` verifies Mali and falls back to ARM) |
