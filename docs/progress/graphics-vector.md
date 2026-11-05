# 2D vector graphics (OpenVG 1.1 and VGU capabilities)

Roadmap line: API-14 ([ROADMAP](../ROADMAP.md)).

Part of [graphics](graphics.md). Survey of 28 Oct 2026: what bm has of the OpenVG 1.1 and VGU feature set, and how to build the rest on the Pi Zero W (V3D, one core) and on the RGB30 (Mali-G52, four cores). Antialiasing is out of scope. bm does not adopt the OpenVG API. The Pi firmware OpenVG needs the VideoCore firmware services, which a bare-metal kernel does not have. The Mali has no OpenVG. Each item below is an own function in the style of [`docs/api/draw.md`](../api/draw.md).

Costs are estimates from the code, not measurements (ARM1176 700 MHz; A55 about 1.8 GHz). Measure before you keep a step.

## What bm has
- Solid colour paint on every drawing call. Clip rectangle (`clip`). `line`, `rect`, `rectfill`, `circ`, `circfill`, `tri` (flat or Gouraud with three colours), `pset`/`pget`, `sget`/`sset`.
- Sprites and tiles: `spr`, `sspr` (zoom and flip), `stack` (rotation and scale of a slice, nearest sampling, `stack_core` in `gfx16.c`), `map` with the tile flag mask.
- Bitmap text: 6x12, 8x14, 8x16, 16x32, 24x48 with integer scale.
- Light tables: `fades`, `g16_fade_apply`, `g16_fade_glow`.
- 2D on the GPU: only on the Pi (`gpu3d_2d=1`, `gpu2d_only=1`, `gpu2d_sheet`). On the RGB30 the 2D code is ARM only. The 2D code (`gfx16.c`) is the same on both.

## Capability list
Priority 1 is high, 3 is low. "Pi" and "RGB30" give the way to build it.

| OpenVG / VGU capability | Today | Way to build | Pi | RGB30 | Prio |
|---|---|---|---|---|---|
| Path (move, line, quad, cubic, arc, close) | has (`path_begin`, `path_move`, `path_line`, `path_quad`, `path_cubic`, `path_arc`, `path_close`) | flatten curves by subdivision, edge list, scanline intersection fill; about 250 lines | ARM, about 3 ms a full screen, 0.02 ms for a 32x32 icon | same, bands of rows on four cores: about 0.1 ms | 2 |
| Fill rules (even-odd, non-zero) | has (even-odd, non-zero winding via `path_fill`, `polyfill`) | part of the path scanline code | ~0 extra | same | 2 |
| Stroke (width, caps, joins, miter, dash) | has (`path_stroke`, 1 px line + offset quads, round joins/caps) | offset quads (two `g16_tri` a segment), round joins with `g16_circfill`; about 150 lines | ARM | bands | 2 |
| Solid paint | has | - | - | - | - |
| Linear gradient | has | `rectgrad` (two Gouraud triangles) and `tri` | 0.05 ms | same | 1 |
| Radial gradient, spread modes | has (`radgrad`, forward differences on d2, 256-entry RGB565 LUT) | forward differences on d2, 256-entry RGB565 table; about 80 lines | about 6 cycles a pixel | bands | 1 |
| Colour ramp | partial (`fades`) | the radial and linear tables | - | - | 2 |
| Pattern paint | partial (`spr`/`map`) | `fillpat` over a cell range; about 20 lines | ARM | ARM | 3 |
| Affine transform of an image | has (`sspr_affine`, `sspr_rot`, `spr_rot`) | `sspr_affine` with a 2x2 matrix, reuse `stack_core`; about 70 lines | 16x16 about 0.01 ms | 0.003 ms | 1 |
| Image perspective, quad warp (VGU) | 3D only | two textured triangles with `tri_grad` and `rq_tex` (`r3d.c`); about 120 lines | 0.02 ms for 16x16 | bands | 3 |
| Clip rectangle | has | - | - | - | - |
| Mask layers | partial (tile flag mask) | 1-bit mask plane built from paths; about 100 lines | +1 load a pixel | same | 3 |
| Image formats with alpha (ARGB) | has (coverage 0..255) | the sheet `alpha` byte holds coverage 2-255, 1/255 stays opaque so old sheets stay right; separate `sspr_alpha`/`spr_alpha` keeps `spr` the fast path | about +8 cycles a pixel where alpha is not binary | same | 2 |
| Copy, get, set pixels | has | - | - | - | - |
| Blend modes (alpha, add, multiply, screen...) | none (only `g16_fade_glow`) | `blend(mode)` before a draw; alpha on 5/6-bit fields, add and multiply by LUT; about 80 lines | 5-8 cycles a pixel; full screen about 2 ms | bands: 0.5 ms | 2 |
| Premultiplied alpha | none | a sheet option, no API | saves one multiply | same | 3 |
| Colour transform, lookup table | partial (`g16_fade_apply`) | `colmat(m)` 3x3 on RGB565; about 60 lines | about 10 cycles a pixel | bands | 3 |
| Colour matrix, convolve, Gaussian blur | has (`blur` separable box blur with running sum, `conv3` 3x3 convolution matrix) | box blur with running sums (`blur`), `conv3`; about 150 lines | blur 320x180 about 1.2 ms | bands: about 0.1 ms. A Mali pass needs the Texture descriptor (G1) | 2 (blur), 3 (rest) |
| Vector fonts, glyph origin | none (bitmap fonts) | `stb_truetype` (public domain or MIT; vendored about 5k lines), glyph raster once, cache as alpha sheet (96 glyphs at 24 px about 27 KB); wrapper about 300 lines: `vfont`, `vprint` | raster 0.05-0.1 ms a glyph, cached blit 0.01 ms | raster on cores 1-3 | 2 |
| VGU line, polyline, rect, round rect, ellipse, arc, polygon | has (`line`, `rect`, `circ`, `rrect`, `ellipse`, `rrectfill`, `ellipsefill`, `poly`, `polyfill`) | `poly`/`polyfill` (fill = path code); arc | `rrect` 0.03 ms | same | 1 |
| Offload of heavy 2D work to the GPU | Pi partial, RGB30 none | Pi exists. Mali: Texture and Sampler descriptors, textured fragment shader (G1 in [multicore](multicore.md)), alpha discard. 4 us a draw makes 600 sprites cost 2.4 ms, more than the ARM path (about 0.5 ms). The Mali pays only for big layers and fades | exists | skip for sprites | 3 |

## Do better (code that exists)
Numbers are estimates. Each one needs a measurement with `b3d tests=map8,sprites,fade` (Pi and RGB30).
1. `g16_blit565`, scaled path (`gfx16.c:134`): a division a pixel and a row. The ARM1176 has no divide instruction. Use an x index map. About 2-3x on scaled blits.
2. `g16_text` (`gfx16.c:486`): `plot()` a lit pixel with four clip tests. Draw runs with `hline`, as `g16_text_scaled` does. About 2x on a screen of text.
3. `g16_sspr_zoom` (`gfx16.c:277`): clamp, 64-bit multiply and alpha test a pixel. Build the x map once a call and use `span_fill` for opaque rows. About 1.5x on the Pi, 3x with bands on the RGB30.
4. `g16_fade_apply` (`gfx16.c:897`): write two pixels a 32-bit store, as `span_fill` does. About 1.3x on the Pi.
5. RGB30 only: `g16_blit565`, `g16_light_apply`, `g16_cls` as four bands with a narrowed clip (the `r3d.c` band pattern). About 3x on full-screen passes. The Pi stays as it is.
6. `stack_core` computes the bounding box for each slice. Compute it once a call when `vert` and `sw` are equal. About 5%.
7. Real alpha: see the "Image formats" row.
8. Pi build: list `gfx16.c` in `PI_O3_FILES` (item 3 of [graphics](graphics.md)) and measure.

## Order of work
1. Done (5 Nov 2026): Items 1 and 2 of "Do better" (xmap on scaled blits, runs with `hline` on `g16_text`), and `rectgrad`, `rrect`, `rrectfill`, `ellipse`, `ellipsefill` for UI. Item 5 (RGB30 bands).
2. Done (5 Nov 2026): `sspr_affine` (2x2 matrix rasterizer), `sspr_rot`, `spr_rot`, `sspr_alpha`, `spr_alpha`, `g16_sspr_zoom` xmap optimization, 8-bit translucent sheet alpha (`sget`/`sset` alpha).
3. Done (5 Nov 2026): Paths with even-odd and non-zero winding rules, Bézier curves (quad, cubic), circular arcs, vector stroker with round caps/joins, `poly` and `polyfill`.
4. Done (5 Nov 2026): Separable 2D box blur with running sums (`blur`), 3x3 convolution filter (`conv3`), radial gradients (`radgrad`). Next: TrueType vector fonts (`vfont`, `vprint`) with alpha glyph cache.
5. Mali 2D only after G1 and only for big layers.

## Details of implemented steps

### Step 1: UI primitives, gradients & ARM inner loops (5 Nov 2026)
- `rrect`, `rrectfill`: rounded rectangles; midpoint circle quadrant spans with `hline` and single center `rectfill` (zero overdraw).
- `ellipse`, `ellipsefill`: integer midpoint ellipse; symmetric scanline `hline` pairs.
- `rectgrad(x, y, w, h, c0, c1, [c2, c3, dir])`: 2 Gouraud triangles (`g16_tri_gouraud`). Vertical, horizontal (`dir="h"`), or 4-corner gradients.
- `g16_blit565` & `g16_text` optimizations: precomputed `xmap` array replaces ARM1176 software integer division; RLE runs with `hline` for font glyphs.

### Step 2: Affine transforms, sprite rotation & 8-bit alpha (5 Nov 2026)
- `sspr_affine(sx, sy, sw, sh, dx, dy, a, b, c, d, [ox, oy])`: 2x2 affine matrix inverse rasterizer in 16.16 fixed point with clipping and arbitrary pivot.
- `sspr_rot(sx, sy, sw, sh, dx, dy, rz, [scale, ox, oy])` / `spr_rot(n, dx, dy, rz, [scale, wc, hc])`: high-level sprite rotation with uniform/2D scaling.
- 8-bit alpha blending: sheet coverage $\alpha \in [2, 254]$ blended via unpacked channels; `sspr_alpha`, `spr_alpha`, `sset`/`sget` alpha.
- `g16_sspr_zoom`: precomputed `xmap` replaces per-pixel 64-bit multiplications.

### Step 3: 2D Paths, Bézier curves, fill rules & vector stroker (5 Nov 2026)
- Dynamic path buffer (`g16_path_t`): `path_begin`, `path_move`, `path_line`, `path_close`.
- Curves: `path_quad` (recursive de Casteljau), `path_cubic` (chord flatness subdivision), `path_arc` (curvature-adaptive circular arcs).
- Scanline polygon fill: `path_fill([c, rule])` & `polyfill(points, [c, rule])` with even-odd (`"evenodd"`) and non-zero winding (`"nonzero"`).
- Vector stroker & poly: `path_stroke([c, width])` via offset quads and round caps/joints; `poly` and `polyfill` for point tables/varargs.

### Step 4: Separable 2D box blur, 3x3 convolution & radial gradients (5 Nov 2026)
- `blur([x, y, w, h, radius, passes])`: separable 2D box blur on RGB565 with sliding running sum accumulators ($O(1)$ per pixel independent of radius, fixed-point shift multiplier, multi-pass approximation of Gaussian blur, defaults to full screen).
- `conv3(x, y, w, h, kernel, [divisor, offset])`: general 3x3 spatial convolution matrix on RGB565 with clamped boundary sampling.
- `radgrad(cx, cy, r, c0, c1)`: radial gradient circle using forward differences on $d^2 = dx^2 + dy^2$ ($(dx+1)^2 - dx^2 = 2dx + 1$) and 256-entry precomputed RGB565 LUT.

## Can only the RGB30 pay for it?
A full-screen pass on one core is about 3 ms on the Pi and 1 ms on the A55. Full-screen gradients with blend, full-screen blur and many icons a frame belong to the RGB30. The Pi takes: `rrect`, `ellipse`, `rectgrad`, `radgrad`, `sspr_affine`, alpha sprites for the HUD, bitmap text, and two or three vector icons a frame.

## Rules (do not break)
- The Pi keeps RGB565. No 32-bit colour for the 2D path.
- A function that runs on bands writes only its own rows (see [multicore](multicore.md) rules). A band function has a one-band path for the Pi.
- A new function goes in `docs/api/draw.md` and `src/ai/api.tsv` with its Pi and RGB30 cost measured.
