# Development Proposals: bm SDK (The Project Hub and Level Design)

This document analyzes what **bm SDK** (`carts/editor` and `carts/sdk_b16`) lacks to support integrated advanced level design, entity instantiation and the management of complex worlds like those of *Yharnam*, *Skyvale World* or future open-world/metroidvania games.

---

## 1. Current State of bm SDK

`bm SDK` is currently the nerve center of production on the console:
- **F1 (Project & Dev Kit)**: Project budget (tokens, lines, RAM, asset memory), performance report after the test run (**F5**) and shortcuts to open the other editors (`1`–`6`).
- **F2 (Code)**: Quick Lua code editor.
- **F3 (Sprites & Flags)**: Sprite sheet view with flags 0–7 on each tile (wall, platform, ladder, water, damage...).
- **F3 again (Map)**: Multi-layer map (`l`/`L`), 16×16 brush (`z`), zoom (`-`/`+`), global 256×256 view (`m`, Overview), compact palette of used tiles (`Tab`).
- **F4 (3D Models)**: Mesh viewer and sample Lua code.
- **Ctrl+N**: Project creation from template (Platform, Top-down, Versus, Shooter, 3D...).

---

## 2. What Is Missing for Structured Level Design and Gameplay

Despite the map's powerful graphics features, current level design suffers from structural limitations when moving from a simple graphic grid to a living game world:

### A. No "Entity & Object Spawner" (Instantiating Enemies and Objects)
- The bm SDK map stores only **integer tile numbers** (0 = empty, 1..4095 = cell ID).
- To place an enemy, a crate, a lamp or a door:
  - In *Yharnam*, developers were forced to create an `objects` layer with fake tiles (placeholder >= 1024) and write hundreds of Lua lines to scan the map and convert the tile number into the corresponding entity.
  - It is not possible to attach **properties or parameters** to an object placed on the map (e.g. `type: "wolf", hp: 80, direction: "left", patrol_path: [A, B]`).
- All placement of enemies and interactive objects ends up hand-coded in Lua tables or decoded through complex tricks.

### B. No Rectangular Triggers and Event Zones
- Collision flags (0–7) are tied to the **sheet tile**, not to the position on the map: if the "grass" tile has no solid flag, it cannot become an exit trigger only at a specific spot.
- There is no tool to draw **trigger rectangles** on the map, for example:
  - *Room transition / scene change zones*.
  - *Camera Lock Zone* (lock the camera during a boss fight).
  - *Checkpoints and Saving*.
  - *Dialogue or cutscene triggers*.

### C. Chunk / Room World Management (Metroidvania & Open World)
- The map supports 256×256-cell sizes. In *Yharnam*, this space is logically split into 8×8 chunks of 16×16 cells loaded as a stream (streaming).
- In bm SDK, the developer only sees a boundless continuous grid: there is no visual chunk grid, no way to name "rooms", display connecting doors or manage transitions between separate maps.

### D. No Multi-Layer Prefabs / Stamps
- To build a house, a tree or a tower, the level designer must manually place dozens of 8×8 tiles, constantly switching between the ground layer (`ground`) and the overlay layer (`overlay`).
- There is no way to save and paste **multi-layer Prefabs** (e.g. a complete tree with canopy above and solid trunk below).

---

## 3. "Hand-Made" Complexity vs Integrated Solution

| Feature | Current Approach ("By hand") | With the Evolution of bm SDK |
|---|---|---|
| **Enemy Spawn** | Hand-write coordinate tables in Lua (`{x=140, y=320, type="slime"}`) or use CSV layers with arbitrary IDs | "Entity" mode: click on the map, pick the entity from the dropdown menu, set the parameters |
| **Trigger Zones** | Detection in `_update` with math formulas (`if player.x > 500 and player.x < 550 then ...`) | "Trigger Area" tool: draw a box on the map and set the event name |
| **Camera Lock** | Conditions written in code for each room | "Camera Bounds" property draggable on the map |
| **Scenery Assembly** | Tile-by-tile placement on separate layers | "Stamps/Prefabs" library: composite stamps placeable with a single click |

---

## 4. Development Proposals & Roadmap for bm SDK

### Proposal 1: "Entities" Layer & Object Inspector
Add a dedicated mode to the F3 map page:
- **`Entities` layer**:
  - Selected from the layer panel next to Ground and Overlay.
  - Visual icons of the entities (player, enemies, NPCs, doors, chests) appear on the map.
- **Object Inspector (Properties Panel)**:
  - Clicking an entity opens a side panel to configure:
    - Unique `ID / Name` (e.g. `boss_cathedral`).
    - `Class / Script` (e.g. `foe_beast`).
    - `Custom fields` (e.g. `hp = 150`, `speed = 2.5`, `drops = "key_bronze"`).
- **Saving into the `ENTITIES` Section**:
  - The cartridge file saves a new compact binary or text section, readable at runtime through the `cart_entities()` API.

### Proposal 2: "Trigger & Volume Boxes" Tool
- Allow drawing logical rectangles over the map with distinctive colors:
  - **Yellow**: Generic trigger (calls `on_trigger(name, entity)` when the player enters).
  - **Cyan**: Camera Confiner (defines the minimum and maximum camera scroll limits).
  - **Green**: Checkpoint / Spawn Point.
- Triggers are exported into the map section and handled automatically by the `bmlib` library.

### Proposal 3: Stamp & Prefab Library
- Ability to select an area of the map (e.g. 4×4 or 8×8 tiles) spanning multiple layers (Ground + Overlay + solidity flags) and save it as a **Stamp** with `Ctrl+C` or *Save Prefab*.
- A "Prefabs" palette lets you paste complex trees, houses, spike traps and platforms with one click, without rebuilding the structure every time.

### Proposal 4: Chunk & Room Viewer (Room Grid)
- A view option to show a sectioned grid (e.g. 320×180 boxes or 16×16 cells per screen).
- Key `n` on a box to assign a name to the room (e.g. `Cathedral_Entrance`, `Sewer_Level1`), easing debugging and allowing selective loading into memory of only the visible areas.

### Proposal 5: Unified Runtime API `lib.world()`
Provide in the bm standard library a unified class that automates world loading:
```lua
local lib = require "bmlib"
local world = lib.world() -- automatically loads map, flags, entities and triggers

function _init()
  world:load("level1")
  -- automatically spawns entities by defining the corresponding Lua tables
  world:spawn_all(function(obj)
    add_entity(obj.class, obj.x, obj.y, obj.props)
  end)
end

function _update()
  world:update(player) -- handles room-change triggers and camera limits
end
```

### Proposal 6: 3D Maps in the Map page (decided, to be developed; no external tool)
The most suitable suite for 3D game maps is **bm SDK**: it already has layers, flags, the `objects` layer, `.bme` projects and `.bm`/`.b16` builds, in both editions. Studio/Mesh remain for props. Overbit currently has its map generated in Python (`carts/overbit/art/partenope.py`, `mapgeo.py`): no editor, migration optional and later.
- **A single format, in the cartridge**: grid layers (floor, height, walls/ceiling, objects) in the `LAYERS` and `FLAGS` sections, at most one new section for height. Tiles and flags stay those of the invariants, so existing 2D maps do not break.
- **A single geometry builder in the runtime** (C, not in the suites' Lua): layers → mesh + collisions. Game and editor call the same function, so the preview matches the game.
- **Editor**: a key in the Map page to switch from 2D view (drawing) to 3D view (preview, `bm3d`).
- **Props**: MESH models made in Studio/Mesh, referenced by name from the `objects` layer.
- **Two editions**: `carts/editor` (Pi) and `carts/sdk_b16` (RGB30) must both be modified; the builder lives in the runtime to avoid duplicating logic.
- **Known limit**: a grid with heights = interiors, arenas, block cities; no arbitrary slopes or arches.
- **To decide before starting** (go through `bm-planner`): initial target (corridor/arena FPS like Overbit or 3D city/terrain); height per cell (steps) or per vertex (soft terrain). Proposed branch: `claude/map3d`.

---

## 5. Open Ideas and External Integrations (R11, R38, R39)

- **Map Import from Tiled `.tmj` (R11)**:
  - Support for importing levels generated with Tiled Map Editor (`.tmj` JSON) preserving multiple graphic layers and custom tile properties.
- **Assistant Support for Large Maps (R38)**:
  - Dedicated entry in the knowledge base for composing large maps with the 16×16 brush (`z`), with examples based on `mkmap.py` and the platform tutorial.
- **720×720 SDK Interface on RGB30 (R39)**:
  - Extension of the `carts/sdk_b16` layout with an 8×16 grid logical scale: on the native 720×720 display it allows placing the side panel and map view side by side, as on the Raspberry Pi edition.

