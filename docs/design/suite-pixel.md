# Development Proposals: bm Pixel (2D Suite & Sprite Editor)

This document analyzes what **bm Pixel** (`carts/pixel`) lacks to support the full creation of animation kits for complex characters (*Titan Clash*, *Yharnam* style, or advanced action/platformers), identifying the pain points of the manual approach and outlining concrete proposals for the interface and the engine.

---

## 1. Current State of bm Pixel

`bm Pixel` is currently structured in three main modes:
- **F1 (Draw)**: Enlarged sprite drawing (pencil, eraser, fill, lines, rectangles, ovals, selections, mirror, single-frame onion skin).
- **F2 (Sheet)**: Overview of the sprite sheet (sprite selection, copy/paste, sheet resizing from 8 up to 4096 px).
- **F3 (Palette)**: Color management (palette editing, extraction from sheet, presets).

Pressing `j` was recently introduced to display the hitboxes of the `BOXES` section on the sheet.

---

## 2. What Is Missing to Create a Complete Animation Kit

If a developer wants to create a complete action character with its animations in bm Pixel, they face insurmountable obstacles by hand:

### A. No Naming Management of "Zones" (`SPRITES`)
- To use the high-level function `zspr("eroe_corre", x, y)`, the `.bm` file must contain the `SPRITES` section.
- In bm Pixel **there is no interface to define and name zones**: the developer must remember the coordinates from memory (e.g. `x=64, y=32, w=16, h=16, 6 frames, 10 fps`), leave bm Pixel, open a text editor or a PC, fill in `sprites.txt` and repack the cartridge with `mkbm.py --sprites`.

### B. Hitboxes & Hurtboxes Not Visually Editable
- Pressing the `j` key shows the boxes if already present in the file, but **it is impossible to draw, resize or move them interactively with the pointer**.
- In a game like *Titan Clash*, every move (jab, heavy punch, kick, parry, sweep) requires pixel-level calibration of:
  1. `body`: physical collision / pushbox.
  2. `hurt`: where the character can receive hits.
  3. `hit`: where the strike deals damage, active only in certain frames.
- Writing these numeric coordinates blindly is frustrating and requires hundreds of test restarts.

### C. No Anchor / Pivot Point (Feet Origin)
- When a character goes from `idle` (16×24) to `slash` (32×32 with the sword trail), the standard top-left `x, y` reference point makes the figure visually "jump" on screen.
- The definition of the **Anchor Point** (the point between the feet on the ground, e.g. `ax=8, ay=24`) is missing. Without it, every size change forces manual computation of offsets in the Lua code (`x - 8, y - 16`).

### D. No Multi-Frame Animation Timeline
- bm Pixel shows a small animated preview that automatically scrolls through contiguous horizontal cells.
- It does not allow you to:
  - Change the frame sequence (e.g. frame 1 -> 2 -> 3 -> 2).
  - Set different durations per frame (e.g. long anticipation frame, short impact frame).
  - Reorder or duplicate a frame without manually copying/pasting pixels on the sheet grid.

---

## 3. "Hand-Made" Complexity vs Integrated Solution

| Operation | Currently ("By hand") | With the Proposed Tools in bm Pixel |
|---|---|---|
| **Animation Definition** | Manual computation of `x, y, w, h` coordinates, manual writing of `sprites.txt` | Area selection, `Name` key, frame and FPS selection, automatic save into `SPRITES` |
| **Hitbox/Hurtbox Placement** | Measuring pixels on graph paper, entering strings like `hit 3 24 10 10 6` | Dedicated rectangle tool (`H` for Hurtbox in blue, `K` for Hitbox in red) drawn directly on the canvas |
| **Ground Alignment** | Offsets computed in Lua (`draw_x = x - ox`) | Click to place the anchor (yellow cross at the feet), applied automatically by `zspr()` |
| **Fluidity Check** | Onion skin limited to the adjacent frame | Multi-frame onion skin (previous frame in red, next in green) with opacity adjustment |

---

## 4. Development Proposals & Roadmap for bm Pixel

### Proposal 1: New F4 Page — "Animation & Zones"
Add a fourth page to bm Pixel dedicated to animation management:
- **Left panel (Zone List)**:
  - List of all zones in the project (`idle`, `walk`, `punch`, `jump`, `hurt`).
  - Keys: `n` for new zone, `d` for duplicate, `Enter` for rename, `Del` for delete.
- **Bottom timeline**:
  - Shows the frames of the selected zone in sequence with thumbnails.
  - Ability to add/remove frames picking from anywhere on the sheet.
  - Cursor to adjust FPS (from 1 to 60 fps) and playback mode (Loop, One-shot, Ping-pong).
- **Central Preview canvas**:
  - Plays the animation in a loop at real and enlarged size.
  - Shows a ground line and the anchor editable with the cursor.

### Proposal 2: Visual "Box Editor" Tool (Hitbox / Hurtbox)
In the F4 page (or integrated in F1):
- Box type selection with hotkeys:
  - **1: Body Box (Green)** — solid footprint.
  - **2: Hurtbox (Blue)** — vulnerable area of the character.
  - **3: Hitbox (Red)** — active area of the strike.
- Draw the rectangle with the mouse or keyboard arrows directly on the current frame.
- Option to copy the box to all frames (`*`) or leave it active only in the selected frame.
- Automatic saving into the cartridge `BOXES` section.

### Proposal 3: Color Ramp Generator & Assisted Palette
- In games like *Yharnam* and *Titan Clash*, each material uses ramps of 5–6 coherent tones (chiaroscuro, cool shadows and warm lights).
- Proposal: in the F3 (Palette) page, add a **Ramp Builder** tool:
  - Select a base color: the assistant (or a math function) automatically generates a 5-tone ramp (bluish shadow, midtone, warm highlight) and saves it into the palette.
  - **Shading Brush** tool: a brush that does not change color but raises/lowers the tone by one step on the current ramp.

### Proposal 4: 8-Direction Mirroring Assistant
- For characters with a 3/4 view (*Yharnam* or *Hunter's Night* style):
  - The left-facing directions (SW, W, NW) are often mirrors of the right-facing ones (SE, E, NE).
  - A dedicated "Generate mirrored directions" command that clones the sequences in the sheet applying a horizontal flip, preserving the hand holding the weapon (to prevent the character from suddenly becoming left-handed).

---

## 5. Sprite Stacking (Overlaid Slices and Voxels)

The *sprite stacking* technique allows generating pseudo-3D volumes by overlaying scaled and rotated 2D slices, obtaining depth at the cost of classic 2D rendering:

1. **Runtime Integration**:
   - `stack(sx, sy, w, h, n, x, y, [rz, scala, passo])`: draws `n` consecutive slices from the sheet starting at `(sx, sy)` (in C `gfx16.c` with `sspr` inverse sampling, color 0 transparent), supported in `draw2d()` and `bmhost`.
2. **Named Stacks in `SPRITES`**:
   - Zone definition with type `stack` and slice count in `sprites.txt` / `mkbm.py --sprites`.
   - `zstack(nome, x, y, [rz, scala])` API, display with interactive rotation in the Lib tab and in `.bmi` files.
3. **Dedicated Editor in bm Pixel**:
   - New **Stack** page: slice view both side by side in a grid and overlaid with vertical onion skin; real-time rotating 3D preview; automatic duplication of the slice onto the upper level to ease extrusion; quick creation of a stack starting from an existing zone.
4. **Full x, y, z Rotation**:
   - `stackv(...)` as a voxel volume with free 3D orientation along the axis closest to the camera.
5. **Export to the Other Suite Tools**:
   - Automatic conversion of the stack into `MESH` (colored outer voxel faces) for `bm Studio` and `bm Mesh`.
   - Automatic generation of pre-rendered 8-direction sprites for `bm Animator`.
- **Completion criterion**: dozens of rotating stacks rendered at 60 fps on the Pi, natively editable in bm Pixel and exportable to 3D models.
