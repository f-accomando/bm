# Lua & Runtime Subsystem

This document covers the Lua 5.4 virtual machine integration, cartridge execution lifecycle, core API surface, built-in system libraries, and developer debugging facilities in **bm**.

---

## 1. Lua 5.4 Virtual Machine Integration

bm embeds the standard Lua 5.4 interpreter ([`third_party/lua/`](../../third_party/lua/)) directly into the bare-metal kernel:
* **Architecture & Compilation**: Compiled using the GNU ARM toolchain with VFP hard-float ABI (`-mfloat-abi=hard -mfpu=vfp`) and Newlib libc integration.
* **Deterministic Memory Management**:
  * Uses a dedicated custom memory allocator that allocates from the kernel heap via Newlib's `_sbrk`.
  * Generational garbage collection is enabled by default to minimize GC pause times during active gameplay.
* **Execution Performance on Pi Zero W (1.0 GHz ARM1176)**:
  * `fib(25)`: 83 ms
  * 1,000,000 arithmetic additions: 104 ms
  * Quicksort of 100,000 integers: 657 ms
  * Average opcode cost: **~100 ns** per basic Lua instruction, providing a budget of **~140,000 to 150,000 Lua instructions per 16.7 ms frame** at 60 fps.

---

## 2. Cartridge Runtime Lifecycle (`src/bm/runtime.c`)

Each `.bm` cartridge runs in an isolated `lua_State`. When a cartridge is launched from the system menu or another tool, the runtime creates a fresh state, injects the system API table, compiles the cartridge's Lua code, and invokes its lifecycle callbacks:

```
[System Menu / Tool]
        │
        ▼ (cart_run / loading screen)
   [lua_newstate] ───► Inject bm APIs & Sandbox
        │
        ▼
    [_init()] ────────► Setup game state & load assets
        │
 ┌──────┴──────────────────────────┐
 │ 60 Hz Game Loop                 │
 │                                 │
 │  1. Poll Input (USB, BT, GPIO)  │
 │  2. _update() (Fixed time step) │
 │  3. _draw()   (Graphics blit)   │
 │  4. V3D / Render Flush          │
 │  5. Wait for VSync / 16.6 ms    │
 └──────┬──────────────────────────┘
        ▼ (Ctrl+Esc / Exit)
   [lua_close] ───────► Free RAM & return to menu
```

### Frame Pacing & Frameskip
* The main loop targets a rigid 60.0 fps tick derived from the 1 MHz hardware timer.
* **Adaptive Frameskip (`frameskip(n)`)**: When heavy scenes cause frame computation to exceed 16.6 ms, the runtime can invoke up to `n` consecutive `_update()` calls while skipping intermediate `_draw()` passes to preserve gameplay physics and synchronization.
* **Resource Monitoring**:
  * `stat(10)`: Lua instructions executed during the current frame.
  * `stat(11)`: Total token count of the cartridge script.
  * `stat(12)`: Peak Lua memory allocation in KiB.
  * `stat(13)`: Total asset memory in KiB.
  * `stat(14)`: Execution time of the heaviest recorded frame.

---

## 3. Global Core Lua API Surface

The system exports a comprehensive suite of C-bound functions to the global Lua environment:

### 2D Graphics & Display
| API Call | Description |
|---|---|
| `cls(c)` | Clears the active screen or clipping rectangle to color `c` |
| `pset(x, y, c)` / `pget(x, y)` | Sets or retrieves a single pixel |
| `line(x0, y0, x1, y1, c)` | Draws a line between two points |
| `rect(x, y, w, h, c)` / `rectfill(...)` | Draws hollow or filled rectangles |
| `circ(x, y, r, c)` / `circfill(...)` | Draws hollow or filled circles |
| `spr(n, x, y, [w, h, fx, fy])` | Blits a sprite from the 1024×1024 spritesheet |
| `sspr(sx, sy, sw, sh, dx, dy, [dw, dh, fx, fy])` | Scaled and clipped sub-rectangle sprite blitter |
| `map(cx, cy, sx, sy, w, h, [layer, mask])` | Renders multi-layer tilemaps with collision flags |
| `camera([x, y])` / `clip([x, y, w, h])` | Configures global camera offsets and clipping rects |

### 3D Graphics & Models
| API Call | Description |
|---|---|
| `draw3d(mesh, [x, y, z, rx, ry, rz, sx, sy, sz])` | Submits a 3D mesh for software or GPU rasterization |
| `clear3d([color])` / `zclear()` | Clears depth buffer and 3D screen canvas |
| `camera3d(x, y, z, tx, ty, tz, [fov])` | Configures perspective 3D camera matrices |
| `light3d(dx, dy, dz, [col, ambient])` | Configures directional sun lighting and ambient intensity |
| `animate(model, anim_id, frame)` | Calculates skeletal bone transforms for mesh skinning |
| `gpu3d([on, aa, vs])` | Inspects or toggles VideoCore IV hardware acceleration modes |

### Input & Controllers
| API Call | Description |
|---|---|
| `btn([b, player])` | Queries current state of buttons (0–7 or symbolic names `"ok"`, `"back"`) |
| `btnp([b, player])` | Queries button state with initial press edge-detection |
| `pad([player])` | Queries full analog stick, trigger, and D-pad states |
| `mouse([visible])` | Reads mouse coordinates, relative deltas, and wheel state |
| `controller(player)` | Returns connected controller type, capabilities, and lightbar color |
| `prompt(action, [player])` | Renders contextual controller or keyboard button icon chips |

### Sound & Audio
| API Call | Description |
|---|---|
| `sfx(id, [channel, offset, length])` | Plays a sound effect from the cartridge sound bank |
| `music(track, [fade_ms, loop])` | Plays or stops background music patterns |
| `play(note, [instrument, pan, volume])`| Directly plays notes using Audio 2 synthesis presets |
| `tone(freq, [wave, env, mod])` | Emits raw waveforms through the hardware synthesizer |

### Storage & Files
| API Call | Description |
|---|---|
| `save(table, [slot])` / `saved([slot])` | Reads or writes persistent data across 8 slots (`.SAV`, `.S02`–`.S08`)|
| `saves()` / `delsave([slot])` | Queries byte counts and deletes saved slot files |
| `doc_read(path)` / `doc_write(path, data)` | Reads and writes user documents in `/docs/` |

---

## 4. Built-in Shared Libraries

To prevent code duplication across cartridges, bm embeds standard libraries directly into kernel flash:

### `bmlib` (`require "bmlib"`, [`src/script/bmlib.lua`](../../src/script/bmlib.lua))
The standard game development utility kit:
* **Math & Random**: `clamp`, `lerp`, `approach`, `sign`, `rnd`, `choose`, `shuffle`, deterministic `rng(seed)` (xorshift).
* **Collision & Physics**: AABB rectangle overlaps, circle collisions, tilemap raycasts (`lib.ray`), sliding wall movement (`lib.move`), and platformer physics with one-way jump-through platforms (`lib.step`).
* **Combat Hitboxes (`lib.hits()`)**: Structured hurtboxes and hitboxes with team filtering, multi-part bodies, lane depth (`z`/`depth`), and weapon clash resolution.
* **Tweening & Timers**: Smooth interpolation curves (quad, cubic, bounce, elastic) and timed execution blocks (`lib.wait`).
* **Cameras & Effects**: Smooth tracking cameras with deadzones, screenshake, and 2D particle systems.
* **Multiplayer Support**: Splitscreen viewport manager (`lib.split(n)`), local party join lobby (`lib.party()`), and per-player color palettes.

### `bmnet` (`require "bmnet"`, [`src/script/bmnet.lua`](../../src/script/bmnet.lua))
The universal multiplayer networking library derived from *Overbit*:
* Works across local LAN (UDP broadcast) or via internet relay servers ([`tools/overbit_relay.py`](../../tools/overbit_relay.py)).
* **Deterministic Lockstep Engine**: Gathers 32-bit player input masks at 60 Hz, synchronizes frame turns (`net.input`, `net.frames`), and checks desync hashes (`net.check`).
* Reliable ordered messaging alongside fast unreliable packet streams.

### `bm3d` (`require "bm3d"`, [`src/script/bm3d.lua`](../../src/script/bm3d.lua))
Shared 3D toolkit powering the console's creative suite (bm SDK, bm Studio, bm Animator):
* Color palettes, 3D transform gizmos, wireframe rendering, face picking, and `.bm` mesh serialization (`split_mesh`, `encode_mesh`).

### `riff` (`require "riff"`, [`docs/RIFF.md`](../RIFF.md))
Live-coding musical pattern library inspired by Strudel and TidalCycles:
* Miniature notation for rhythmic beat slicing, euclidean rhythms, and polyphonic note sequencing triggered at deterministic sub-frame audio timestamps.

---

## 5. Developer Debugging & Profiling Tools

### Lua Breakpoint Debugger (R13)
Integrated directly into bm Code and the kernel runtime:
* **Breakpoints**: Set in bm Code (F8) or via code with `breakpoint([reason])`.
* **Step Control**: Step over (F10 / A), step into (F8 / X), step out (Shift+F8 / Y), continue (F5 / Start), or stop (Esc / Select).
* **Interactive Inspection**: Overlays source code context, local variables, upvalues, and call stack directly over the frozen game screen without corrupting video memory.

### Function-Level Sampling Profiler (R14, [`src/bm/profile.c`](../../src/bm/profile.c))
Accessible via F11 DevKit mode 3 or programmatically with `profile(true)`:
* Combines Lua VM instruction hook counters (sampled every 1000 instructions) with C function intercept hooks (`luai_cprof` in `ldo.c`).
* Tracks the top 10 most expensive functions over rolling 1-second windows.
* Separates **Self Time** (execution within the function body) from **All Time** (including sub-calls).
* Highlights native C calls (`spr`, `map`, `draw3d`) in cyan to distinguish script logic from engine rendering.
