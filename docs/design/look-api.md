# Look API: one call for the pixel look of a game

Status: idea, 2026-10-28. Nothing is built. This note is the base for the work orders.

## Goal

A game sets its whole retro look with one call. The call takes the screen size, the number of colours, the palette, the dither, the cel bands, and the outline. A game for the first computers can use 4 colours and big pixels. A game for later years can use more colours and finer pixels. The first user is the programmer-at-home game ([game-proposals](../giochi/game-proposals.md)).

## Call

```lua
look{
  res           = 3,          -- index of screen_modes(), or {w, h}
  colors        = 16,         -- 4, 8, 16, 256, or "565" (native)
  palette       = "auto",     -- "auto", a name, or { 0xRRGGBB, ... }
  dither        = 0.5,        -- 0 to 1, strength before the colour pass
  bands         = 4,          -- cel bands, 0 is off
  outline       = 0x140e1a,   -- colour, or false
  outline_width = 0.02,
}
```

- A key that is not in the table keeps its value.
- `look()` with no argument returns the values in force.
- `look(false)` turns every part off.
- The state goes back to off when the cart ends, as `cel3d` does.
- `colors = 24` and `colors = 32` are accepted. They mean `"565"` with the dither on, because the framebuffer is RGB565 (decision of 2026-10-21: no 32-bit colour for now).

## What exists and what is new

| Key | Maps to | State |
|---|---|---|
| `res` | `screen(i)` or `screen(w, h)`, `screen_modes()` | exists |
| `bands`, `outline`, `outline_width` | `cel3d(bands, floor, outline_rgb, outline_width)` and `draw3d` flags 256 and 512 | exists |
| `dither` (3D) | `dither3d([on])`, ARM raster only | exists, no strength |
| `dither` (whole screen) | the pass below | new |
| `colors`, `palette` | the pass below | new |

Limits of what exists today: the cel bands act on the sun light of `draw3d` only, not on lamps or 2D. The outline is made by the inverted hull, so it exists only on meshes. `dither3d` is not verified on the GPU.

## The colour pass

The pass runs once at the end of a frame, at the logical resolution, before the scale to the display.

1. For each pixel, add an ordered dither offset (4x4 Bayer) to the channels. The offset size is `dither` times the step of the palette.
2. Look up the result in a table that maps RGB444 (the top 4 bits of each channel) to the nearest palette colour. The table has 4096 entries. The dither before the lookup hides the loss of the low bits.
3. Write the palette colour as RGB565.

Palette choice. The main source is the image that the game already draws (decision of 2026-10-28):

- `"auto"`: the kernel picks the `colors` colours from the pixels of the frame. It counts the pixels of a sample (one pixel in four) in 4096 bins (RGB444) and splits the bins by median cut. The result is sorted by brightness, so the cel bands follow the ramp.
- `"auto"` keeps three colours fixed: the darkest, the lightest, and the most saturated. A small accent colour (a LED, a flame) then survives.
- When the palette is picked: `palette = "auto"` picks it once, at the first frame. `palette = "live"` picks it again every 30 frames. In `"live"` mode each colour moves at most a small step toward the new colour, so the picture does not flicker. `look_sample()` picks it on demand, for example after a scene change.
- A list: the colours of the game. This is the best choice when the textures are drawn with the same palette.
- A ramp: `palette = { ramp = {0x0f380f, 0x9bbc0f}, steps = 4 }` maps the brightness of the image to a ramp of colours. It gives the one-colour look of the first computers (green phosphor, four greys). It does not use the colours of the image.
- No named presets in the first release.

The table is built when `look` sets or changes a palette. It costs 4096 distance tests for each palette colour: 65 thousand tests for 16 colours, so a `"live"` update is cheap. The table is not built while `colors` is `"565"`, so a game that does not use it pays nothing.

## Cost, to measure

- At 360x360 a frame has 130000 pixels. At 180x180 it has 32000.
- The table is 8 KB, so it fits in the cache of the Pi Zero W. Measure the cost of the lookups at both sizes.
- Run the pass at the logical resolution, never at the panel resolution.
- Write the numbers in [graphics](../progress/graphics.md) after the first Pi run. Do not call the pass cheap before that run.

## Size in the kernel (estimate, not measured)

The kernel image is 4.6 MB today (`build/kernel.img`, 4798228 bytes). The estimate for the feature:

| Part | Code in the image | RAM |
|---|---|---|
| `look{}` wrapper and the state | under 1 KB | a few bytes |
| ARM colour pass (dither and lookup) | 1 to 2 KB | none |
| Colour table, RGB444 to palette | none | 8 KB (4096 entries of 2 bytes); only while a palette is set |
| `"auto"` and `"live"` palette (count and median cut) | 2 to 3 KB | 16 KB for the count, kept for `"live"` |
| GPU pass: one QPU shader and the driver code (V3D and Mali) | 4 to 8 KB | the table as a texture of 64 x 64, the same 8 KB |

Total: about 6 to 12 KB of code, which is under 0.3% of the image. The RAM cost is 8 KB for the table and 16 KB for the count, and only for a cartridge that sets a palette. The first work order measures the real numbers: compare the size of `kernel.img` before and after each step, and write the result here.

## Rules for the design

- The default is off, with no cost and no change for old cartridges.
- The pass sees the whole frame, HUD included. There is no `ui` key (decision of 2026-10-28).
- The pass has a GPU version and an ARM version (decision of 2026-10-28). The ARM version is the fallback, as for `gpu3d_light`: no GPU, a failed job, the Mali.
- `fades()` and `dark_begin` stay as they are. Their tables work on the palette, so they combine with `look`.
- Textures drawn with the palette of the game give the best result at 4 and 8 colours.

## Retained frame (idea, 2026-10-28)

The programmer game shows a still room. Only the monitor, the cursor, the clock, and a few props change. The game does not need to draw the same pixels again each frame.

- The game declares what changes: `dirty(x, y, w, h)`. The kernel keeps the last frame and redraws only those rectangles. The look pass runs only on the tiles (64 x 64, as `dark_end`) that touch a dirty rectangle.
- `retain(true)` turns the mode on. `retain(false)` goes back to a full redraw. A full redraw also happens at a `screen()` change and at a palette change.
- The game draws the dirty rectangles with the same calls as today. The kernel clips the drawing to them.
- A palette picked with `"live"` changes the whole image. A palette change forces a full redraw.
- Gain: less ARM and GPU work, less heat, and a longer battery on the RGB30. Measure it on the Pi Zero W and on the RGB30 before the work order.
- Open: how a 3D mesh that moves inside a dirty rectangle is clipped, and how the 2D page of `dark_begin` works with the mode.

## Steps

1. `look{}` as a wrapper over `screen`, `cel3d` and `dither3d`. No new C code. Docs in [light3d](../api/light3d.md) and [draw](../api/draw.md).
2. The colour pass on the ARM: the table, the dither, a list palette.
3. `"auto"` and `"live"` palettes, `look_sample()`, and the ramp palette.
4. A strength for the 3D dither.
5. The GPU version of the pass: V3D and Mali, with the ARM pass as the fallback. Measure on the Pi Zero W and on the RGB30.
6. Tests: a cart that sets each key, with a check of the colour count of a frame (`tests/qemu_test.py`).

## Open questions

- The Mali (RGB30) gets the GPU pass in the first release (decision of 2026-10-28). The Mali driver is still behind the V3D driver (API-5): the pass needs the Mali work of that row, or its own small shader.
- How many frames between two `"live"` picks, and how large is the step of a colour? Start with 30 frames and decide with a test cart.
