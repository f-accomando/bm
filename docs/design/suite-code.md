# Development Proposals: bm Code (The Console's Code Editor)

This document analyzes what **bm Code** (`carts/code`) lacks to support complex programming projects on the console, overcome the monolithic-file constraint and improve integration with graphic and sound assets.

---

## 1. Current State of bm Code

`bm Code` is currently an agile code editor, well optimized for low-resolution screens:
- **Display**: Tabs for multiple cartridges, side-by-side two-page view (Split), 6×12 font (106 columns, 28 code lines), 8×14 and 8×16.
- **Assisted writing**: Predictive autocomplete (`predict`), gamepad-assisted writing (`padtype`), AI assistant on F6 and `#entry: ... #` lines.
- **Debug & Audio**: Line breakpoints with F8, playback of code lines as musical notes with Ctrl+Enter (`riff`).

---

## 2. What Is Missing for Complex Projects and Games

Despite the excellent usability on the console, large projects run into strong limitations:

### A. No Multi-File Structure Within the Same Project
- Currently bm Code tabs open **distinct `.bm` or `.bme` cartridges**, not multiple source files of the same project.
- As a result:
  - In *Titan Clash*, developers had to create a `carts/titan/src/` folder with 11 Lua files (`00_core.lua`, `20_fighter.lua`, `30_fx.lua`, `70_screens.lua`...) and create a Python script `build.py` to run on a PC to concatenate them into a single `main.lua`.
  - In *Yharnam*, where the build script was not set up the same way, the whole game lives in a single gigantic `main.lua` file of **5,573 lines**.
- Editing, navigating and maintaining a 5,000-line file on a retro console with a 640×360 or 360×360 resolution is a grueling experience.

### B. No Asset Picker and Visual Completion
- When a programmer writes in bm Code:
  ```lua
  zspr("eroe_colpo_pesante", x, y)
  sfx("slash_crit")
  local m = model("gargoyle")
  ```
  they must remember by heart the exact spelling of strings defined in other editors (bm Pixel, bm Sound, bm Studio).
- If they misspell a letter (`"eroe_colpo_pesabte"`), the game fails silently or crashes at runtime.
- There is no autocomplete or contextual preview of the project's assets.

### C. No Runtime Variable Inspector (Live Watcher)
- For debugging, developers are forced to insert dozens of temporary `print("stato=" .. p.state, 10, 10)` calls in the code, which clutter the screen and must then be deleted by hand.
- Although F8 exists for breakpoints, there is no **Watch** panel to monitor the selected entity's variables (e.g. `p1.state`, `p1.vx`, `p1.vy`, `p1.swing`, `p1.invuln`) while running at 60 fps.

### D. Repetitive Writing of State Machines from Scratch
- Every new game requires rewriting from scratch the same 100–200 lines to handle:
  - Input buffers (remembering a key press for 4–8 frames so that the attack fires as soon as the previous animation ends).
  - Finite state machine (FSM) with frame checks and transitions.
  - Post-hit invulnerability timers (i-frames) and hit-stop (temporary frame freeze on impact).

---

## 3. "Hand-Made" Complexity vs Integrated Solution

| Operation | Current Approach ("By hand") | With the Evolution of bm Code |
|---|---|---|
| **Project Organization** | Single 5000+ line file, or external Python build script on a PC | Tabs for modules (`player.lua`, `boss.lua`, `hud.lua`) in the same `.bme` project, loaded natively by `require` |
| **Asset Recall** | Remember sprite, mesh and sound names by heart or look them up on notes | Type `zspr("` and the contextual menu appears with sprite thumbnails and zone names |
| **Debugging Movement and States** | `print()` scattered in the drawing code | F7 overlay: table of variables of interest visible in a transparent overlay during the F5 test run |
| **Input Buffer & Combat** | Dozens of lines of custom logic with frame counters | Assisted snippet: *Insert Combat State Machine* from the assistant menu |

---

## 4. Development Proposals & Roadmap for bm Code

### Proposal 1: Native Multi-Module Support in `.bme` Projects
- **Status:** bm Code shows modules as tabs (lines `--@module name` in the one code section; see `docs/progress/tools.md`). `require "player"` between modules and the SDK tabs are not built.
- Allow a `.bme` project to contain multiple Lua code sections (e.g. `main.lua`, `player.lua`, `enemies.lua`, `ui.lua`).
- In bm Code, a secondary bar or a shortcut (e.g. `Ctrl+Tab` or `Ctrl+P`) lets you jump instantly from one module to another.
- The Lua runtime natively integrates `require "player"`, searching first among the cartridge's internal modules. This way the project stays clean, modular and 100% developable on the console without external Python scripts.

### Proposal 2: Asset IntelliSense & Visual Popup
- When the cursor is inside the asset-recall functions:
  - **`zspr("`**: opens a popup with the list of all named zones saved in `SPRITES`, with a graphic thumbnail preview (8×8, 16×16) and frame count.
  - **`sfx("`** or **`note(`**: popup with the names of the sound effects and the ability to hear a preview by pressing Space.
  - **`model("`**: list of the 3D models available in the `MESH` section with wireframe preview.
  - **`flag(`**: display of flags 0–7 and their meaning as configured in the project (e.g. `0=Wall, 1=Platform, 4=Spikes`).

### Proposal 3: Live Variable Watcher (F7 Panel during F5)
- In bm Code you can mark a line or a variable with a "Watch" marker (e.g. `#watch player.state, player.vx, player.vy`).
- When starting the game test run with **F5**, pressing **F7** shows a semi-transparent box at the top right with values updated at 60 fps, without touching the `_draw()` code.

### Proposal 4: Snippet Generators for Game Archetypes
Integrate into the assistant menu (F6) generators of proven architectural patterns:
1. **FSM Controller**: Generates the standard structure for `IDLE`, `WALK`, `ATTACK`, `HURT`, `FALL` states.
2. **Input Buffer Queue**: Generates the standard 6-frame logic to accept attack input in advance.
3. **Screen Shake & Hit-stop**: Ready-to-use standard functions to give hits visual impact.

### Proposal 5: Game Templates for "New Game" (R16)
- **Status:** done (23 Oct) with code-only starters: Ctrl+N lists Empty, Platform 2D, Top-down 2D, Shooter 2D, Versus 2D and 3D scene, each split in `--@module` modules. See [tools-code-templates](../progress/tools-code-templates.md). The SDK templates with art and maps stay in the SDK.
- Currently "New Game" in bm Code generates a single basic empty skeleton (`TEMPLATE`).
- Integrate guided selection of the templates already available in the SDK (Platform 2D, Top-Down 2D, Versus 2D, Shooter 2D, 3D Scene), generating ready, module-structured files.
