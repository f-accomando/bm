# Input Standard for Games (bm and b16)

One set of logical actions for every game, so the same game reads the same on the Pi keyboard, a Pi pad and the RGB30. The input layer already exists (`keymap()`, the `"ok"` / `"back"` names, `controller().ok`, `prompt()`: [`docs/api/input.md`](../api/input.md) lines 9-12); this standard only fixes which action goes on which button. Per-device keys and screen reference: `docs/spec/ui-reference.md` (planned, written by another operator).

---

## 1. The Standard Actions

Games declare them once: `keymap({ confirm = "ok", cancel = "back", pause = "start", act1 = "ok", act2 = ..., shoulder_l = "l1", shoulder_r = "r1" })`.

| Logical action | Keymap button | RGB30 today | RGB30 target | Pi keyboard | Pi pad | Context |
|---|---|---|---|---|---|---|
| confirm | `"ok"` | B (`pad_ok = PAD_B`, `src/rgb30/pad.c:10`; `confirm=a` swaps, `pad.c:16`) | B (unchanged) | Space (`docs/api/input.md:9`) | cross (DS4) / A (Xbox) (`docs/api/input.md:9`) | menus |
| cancel | `"back"` | A (`pad_back = PAD_A`, `src/rgb30/pad.c:10`; `docs/progress/platforms.md:35`) | Y only, A is no back (menus: `claude/pad-y-cancel`, to merge; games keep A/B as before) | X (`docs/api/input.md:9`, `:75`) | circle (DS4) / B (Xbox) (`docs/api/input.md:9`) | menus |
| pause | `"start"` | Start | Start (unchanged) | Enter (`docs/api/input.md:78`) | Start / Options (`docs/api/input.md:78`) | gameplay; opens `lib.pause` (`src/script/bmlib.lua:1179`, opened by `btnp("start")` at `:1196`) |
| move | `"left"`..`"down"` or `stick()` | D-pad / left stick | unchanged | arrows / WASD (`docs/api/input.md:70-73`) | cross / stick (`docs/api/input.md:70-73`, `:15`) | gameplay and menus |
| act1 | `"ok"` | B | B (unchanged) | Space | cross (DS4) / A (Xbox) | gameplay |
| act2 | the A/B button that is not `controller().ok` (`docs/api/input.md:12`) | A | A (unchanged) | X | circle (DS4) / B (Xbox) | gameplay |
| shoulder_l / shoulder_r | `"l1"` / `"r1"` | L1 / R1 | unchanged | Q / E (`docs/api/input.md:97`) | L1 / R1 | gameplay |

Rows marked "to implement (kernel)": cancel on Y (the only RGB30 target that differs from today). Everything else already works through `pad_ok` / `pad_back` and the `"ok"` / `"back"` names (`src/rgb30/bm_input.c:68-76`).

Exit: Start+Select (or the PS button) always closes the cartridge and is fixed (`docs/api/input.md:81-82`). Start alone is the game's pause.

Screen: the RGB30 panel is 720x720; the menu and `.b16` games run at 360x360 scaled x2 (`docs/RGB30.md:13-14`).

---

## 2. Contexts

- **Menus**: confirm and cancel only (`"ok"` / `"back"`), plus move. Use `pad_ok` / `pad_back` in C and the names in Lua, so `confirm=a` and `game_buttons=position` are honoured.
- **Gameplay**: act1 on `"ok"`, act2 on the other face button, pause on Start, L1/R1 for the shoulders. Read the buttons through actions, never through the letters.

## 3. Declared Exceptions

A game that needs a different binding declares it with `lib.input({ act2 = "x" })`. Each such key is listed in the game's `docs/progress/<game>.md` with the reason. Any other raw button (`btn(4)`, `btn("a")` for an action) is an undeclared deviation and gets migrated to an action.

## 4. On-Screen Hints

Show hints with `prompt("act1", x, y)`, which draws the right symbol for the player's controller (`lastinput()` tells which: `docs/api/input.md:98`). Write hints through `prompt()` so key names never appear as literal text. `.b16` sources carry no `prompt()` keyboard words: `.b16` games get RGB30 pad bindings only.

## 5. Editors

The editors keep their own keyboard model; the standard above is for games. See [suite-code.md](suite-code.md), [suite-pixel.md](suite-pixel.md), [suite-sdk.md](suite-sdk.md) and [docs/video/07-code/input.md](../video/07-code/input.md).

## 6. Inventory

`python scripts/input_inventory.py --games` lists the raw buttons each game reads and compares them with this table. It prints one markdown table, cart by cart (`std`, `exc:<button>`, `raw:<button>` or `-`); read-only, exit 0.

---

## 7. Open Decisions

1. Kernel change: RGB30 `pad_back` = Y with A as a second back, in `src/rgb30/pad.c` / `src/rgb30/bm_input.c`; check the interaction with `game_buttons=position` (`bm_input.c:34-42`, `:56-60`, `:70-76`). Separate Work Order.
2. Gameplay act1 = the ok button (RGB30 B), act2 = the A/B button that is not ok: the user confirms.
3. Whether X and Y get roles act3 / act4 or stay free.
4. Esc on the Pi keyboard as a cancel alias.
5. Migration of existing games to `lib.input`, and its order.
6. Where exceptions are recorded in the cartridge (INFO, section 10).
7. Whether `lib.input` re-runs on a controller change (`lastinput()`).
