# Development Proposals: bm Studio & bm Mesh (3D Modeling Suite)

This document analyzes what **bm Studio** (`carts/studio`) and **bm Mesh** (`carts/mesh`) lack to support fast creation of 3D assets, complex scenery and collision geometry for both three-dimensional games (like *Overbit* or *Astro Wing*) and hybrid 2D/3D ones.

---

## 1. Current State of bm Studio and bm Mesh

The bm 3D suite allows working entirely on the console:
- **bm Studio**: Block and tile modeling (tools *block*, *tile*, *select*, *vertex*, *paint*, *reduce*). It uses the sprite sheet palette to apply textures and colors to faces.
- **bm Mesh**: Analytical vertex and triangle editor:
  - Inspection and editing of models in the `MESH` section.
  - Display and editing of meshes generated from Lua code (`cart_meshes()`).
  - Export from model to Lua code (`mesh_nome()`) and vice versa.

---

## 2. What Is Missing for 3D Level Design and Complete Games

Despite the ease of use for small single objects, clear limitations emerge when tackling large scenery or articulated gameplay:

### A. No Simplified Collision Geometry (Collision Hulls)
- A scenic 3D model (e.g. a tree with detailed canopy or a gothic statue) can contain 300–800 triangles.
- In a game, testing player collisions against all triangles every frame with `hit3d()` or math calculations is prohibitive for the CPU of the Raspberry Pi or the retro console.
- There is no way to define a **simplified Collision Hull** (a cylinder, a capsule or an AABB bounding box) saved together with the model.
- Currently developers must compute collision radii by hand in Lua (`dist(p.x, p.z, obj.x, obj.z) < r`).

### B. No 3D Scene Assembler (Scene / Kitbash Layout)
- In *Studio Village* (a game removed from the repository), the village layout was defined by a hand-written Lua table:
  ```lua
  local things = {
    { "house", -3.5, 5.5, 0, 1 },
    { "well", 3, -3, 0.3, 1 },
    { "crates", 4.4, 0.2, 0, 0.8 },
    { "tree", -4.5, -3.5, 0, 1.6 },
    ...
  }
  ```
- bm Studio and bm Mesh allow working on **a single model at a time**, but offer no mode to **compose a 3D scene** by placing multiple models in space with rotations, scales and Y heights.

### C. Modeling Complex Shapes vs Mathematical Scripting
- In *Astro Wing* ([`carts/astrowing/main.lua`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/astrowing/main.lua)), the spaceships and details were written entirely as procedural mathematical generators (`builder()`, tetrahedra and cuboids with vector-product computation of normals).
- This happened because modeling tapered shapes, tapering wings and beveled sections by hand in bm Studio starting from simple cubes is long and lacks tools for extrusion along curves (loft/lathe) or edge cuts (chamfer).

### D. UV Mapping and Texture Assignment from the Sheet
- Textured faces in bm Studio take their coordinates from the sprite sheet.
- Adjusting orientation, scale and the 0.25 pixel padding to avoid edge fringing (*texture bleeding*) often requires meticulous vertex-by-vertex tweaking, lacking a quick visual selector of 8×8 / 16×16 tiles applicable with one click.

---

## 3. "Hand-Made" Complexity vs Integrated Solution

| Operation | Current Approach ("By hand") | With the Evolution of Studio & Mesh |
|---|---|---|
| **3D Level Composition** | Hand-write coordinate and angle tables in Lua | "Scene Layout" mode: place model instances with grid and snapping |
| **Collision Volumes** | Approximate geometric calculations hardcoded in the game code | Integrated "Collision Capsule/Box" tool in the model, read by `bounds3d()` |
| **Texture Application** | Manually moving UV coordinates for each vertex | "Tile Stamp" key: click the tile on the sheet and click the 3D model face |
| **LOD (Level of Detail) Generation** | Manually duplicating the mesh and removing faces one by one | "Bake LOD" key: automatically generates 50% and 25% polygon versions for the `draw3d()` flags |

---

## 4. Development Proposals & Roadmap for bm Studio & bm Mesh

### Proposal 1: "Collision Volumes" Tool (3D Hitbox)
- In bm Studio or bm Mesh, add the definition of simplified bounding volumes:
  - **Capsule**: ideal for bipedal characters (height, radius).
  - **Box**: ideal for houses, crates and walls.
  - **Cylinder**: for tree trunks, wells and columns.
- These volumes are stored in the `MESH` section and returned automatically by a new runtime function `col3d(model, x, y, z)` or integrated into `hit3d()`.

### Proposal 2: "Scene / Diorama Assembler" Mode
Add a page dedicated to assembling environments:
- Allows importing the file's models and placing them on a three-dimensional grid with 0.5-unit snapping.
- Rotation controls on the 3 axes (`rx, ry, rz`) and scale.
- Saving the layout into a dedicated `SCENE` cartridge section, loadable with a single Lua line:
  ```lua
  local villaggio = load_scene("village")
  villaggio:draw() -- draws all instantiated models, optimizing culling
  ```

### Proposal 3: Interactive UV Tile Brush
- A side panel showing the project's sprite sheet divided into 8×8 or 16×16 tiles.
- Clicking a tile and then a face of the 3D model applies the UV mapping instantly, with correct orientation and an automatic 0.25 px anti-bleeding margin.

### Proposal 4: Advanced Primitives and Beveling Tools
- Add to the primitive selector (currently Cube, Sphere, Plane):
  - **Cylinder / Cone**: 6-, 8- or 12-sided cylinders for robotic limbs, pipes, trees.
  - **Prism / Wedge**: wedges and ramps for roofs and buildings.
  - **Chamfer Tool**: quick beveling of sharp edges, essential for achieving the retro-mecha look of *Titan Clash*.

### Proposal 5: Role in 3D maps (decision made, to be developed)
3D game maps are edited in the Map page of **bm SDK** (see [`suite-sdk.md`](suite-sdk.md), Proposal 6), not in Studio/Mesh. Studio and Mesh produce the **props** (MESH models) that the map's `objects` layer references by name, with their collision volumes (Proposal 1). Proposal 2 (Scene Assembler) does not replace the grid map: it should be reconsidered only for off-grid dioramas, to avoid having two level formats.
