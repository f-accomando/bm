# Development Proposals: bm Animator (3D/2D Rigging and Animation Suite)

This document analyzes what **bm Animator** (`carts/animator`) lacks to close the gap with the advanced procedural pipelines used for *Titan Clash* and *Yharnam*, and to turn it into the definitive tool for producing complete pre-rendered 3D and 2D animation kits.

---

## 1. Current State of bm Animator

`bm Animator` is one of the most advanced tools of the bm console:
- **F1 (Play)**: Viewing 3D models with skeleton, real-time interpolation (`animate()`), blend tests between different animations (`b` to blend at 25%, 50%, 75%).
- **F2 (Rig)**: Bone skeleton creation (`n`), joint and tail placement (`w a s d r f`), parent/child hierarchy (`p`), symmetric mirroring of `.L` and `.R` bones (`m`), automatic or manual skinning (rigid or soft weights with `k`/`K`).
- **F3 (Animate)**: Animation creation on a 12 fps timeline, bone rotation and translation with keyframes, bone onion skin (`o`), linear/smooth/step interpolation (`i`), pose mirroring (`m`).
- **F4 (Sprites)**: 3D-to-2D bake engine: renders the animation directly into the sprite sheet at pixel art resolution, with options for number of directions (1, 2, 4, 8), camera tilt, light, outline and palette reduction to 8, 16 or 32 colors.

---

## 2. Why Did Titan Clash and Yharnam Use Python Instead of bm Animator?

Despite the F4 Sprites mode, the flagship titles developed complex external generators in Python ([`carts/titan/mkrobot.py`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/titan/mkrobot.py) and [`carts/yharnam/art/`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/yharnam/art/)). The technical reasons lie in specific shortcomings of the on-console editor:

### A. No Event Markers on the Timeline (Active Frames, Sounds, Shots)
- In a slash animation (e.g. 6 frames), damage does not occur for the whole duration: frames 1–2 are windup, frame 3 is the impact with the hitbox active, frames 4–6 are recovery.
- In bm Animator there is **no way to mark a frame with an event** (e.g. `event = "hit"`, `sound = "slash"`, `spawn = "bullet"`).
- All of this must be synchronized manually in Lua code by memorizing frame numbers.

### B. No "Sockets" (2D Anchor Points for Weapons and Particles)
- In *Yharnam*, every frame of the Hunter computes the exact 2D coordinate of the cleaver tip (for blood trails and sparks) and of the gun muzzle (for the muzzle flash and the bullet trajectory).
- bm Animator draws the composite model into the sprite sheet but **does not store the 2D coordinates of the end bones**.
- As a result, if a 2D game wants to show a flash from a weapon barrel, the developer does not know where to place it without hardcoding coordinates by hand for every frame.

### C. No Support for Modular Layers (Composable Armor and Weapons)
- In *Titan Clash*, the Vanguard robot has interchangeable armor (intact, damaged by heavy hits, forearm-mounted weapons).
- Instead of duplicating 100 frames for each armor combination, the Python script rendered the base + separate transparent layers stacked in a fixed order.
- F4 in bm Animator renders the whole model as a single opaque block, with no way to export separate accessory layers aligned to the same grid.

### D. No Automatic Direction Naming in `SPRITES`
- When F4 bakes an 8-direction animation into the sprite sheet, it generates a matrix of rectangles but does not automatically create the structured labels for the `SPRITES` section (e.g. `cammina_s`, `cammina_se`, `cammina_e`, etc.) ready for immediate use with `zspr()`.

---

## 3. "Hand-Made" Complexity vs Integrated Solution

| Need | Current Approach (Python script or by hand) | With the Evolution of bm Animator |
|---|---|---|
| **Hitboxes from Skeleton** | Write a math parser in Python that projects bone volumes into 2D coordinates | In F4 Sprites, bones flagged as "Attack" or "Body" directly generate the `BOXES` section |
| **Sound/Hit Synchronization** | `if frame == 3 then play_sfx() end` counters scattered in the game code | `e` key on the frame to insert an event marker (`hit`, `sfx_sword`, `step`) exported in the metadata |
| **Shot/Trail Point** | Find the projectile X/Y coordinates by opening the image in GIMP and noting the pixels | Define a bone as "Socket": F4 exports a table with the screen coordinates relative to each frame |
| **8-Direction Animations** | Hundreds of script lines to rotate the camera and compose the atlas | F4 generates the standard 8-direction nomenclature ready for `zspr()` with a ground anchor point |

---

## 4. Development Proposals & Roadmap for bm Animator

### Proposal 1: Event Marker Track in F3 (Animate)
Add an **Events** track below the keyframe timeline:
- With a frame selected, press `e` (Event) to assign a tag:
  - **HIT (Red)**: activates the hit's hitbox.
  - **SFX (Yellow)**: triggers a specific sound from the audio bank.
  - **SPAWN (Cyan)**: spawns a secondary entity or particle.
  - **CANCEL (Green)**: window in which the animation can be interrupted by another action (dodge/parry).
- These events are saved in the cartridge metadata and triggered automatically by `bmlib`.

### Proposal 2: Socket Point Exporter
- In the F2 (Rig) page, allow setting the `Socket` flag on certain bones (e.g. `hand.R`, `muzzle`, `head`).
- During the bake in F4 Sprites, the 2D socket coordinates are computed and saved for every frame and every direction:
  ```lua
  -- Automatically generated by F4
  local SOCKETS = {
    hero_shoot = {
      [1] = { muzzle = { x = 24, y = 14 } },
      [2] = { muzzle = { x = 26, y = 12 } },
    }
  }
  ```
- This allows the code to place sparks, flashes and weapons with sub-pixel precision.

### Proposal 3: Batch Directional Bake with `SPRITES` & `BOXES` Generation
Enhance the F4 Sprites page:
- Select the list of animations to bake (`idle`, `walk`, `attack`, `hurt`) and the direction set (4 orthogonal or 8 isometric directions).
- **Base Name**: set the prefix (e.g. `hunter`).
- **Pressing Enter (Full Bake)**:
  1. Renders the frames into the sprite sheet with a compact layout (skyline packing).
  2. Writes into the `SPRITES` section the names with directional suffixes: `hunter_walk_s`, `hunter_walk_se`, etc.
  3. Projects the volumes of the weapon and torso bones, automatically creating the `hit`, `hurt` and `body` boxes for the `BOXES` section.
  4. Sets the Anchor Point exactly at the base of the foot bones at ground contact.

### Proposal 4: Multi-Layer Pass Rendering (Modular Bake)
- Allow marking model parts in visual groups (e.g. Group A: `Body`, Group B: `Armor`, Group C: `Weapon`).
- Option in F4: *Bake as Layers*: outputs a sequence for each group, keeping transparency and the exact same bounding rectangle, allowing 2D games to implement customizable armor and weapons at zero cost.
