# Input Standard for Games (bm)

One set of logical actions for every game, so the same game reads the same on the Pi keyboard, a Pi pad and the RGB30. The input layer already exists (`keymap()`, the `"ok"` / `"back"` names, `controller().ok`, `prompt()`: [`docs/api/input.md`](../api/input.md) lines 9-12); this standard only fixes which action goes on which button. Per-device keys and screen reference: `docs/spec/ui-reference.md` (planned, written by another operator).

---

## 1. Menus (every game and the system)

Decided by the user, 19 Oct 2026. `lib.input()` always sets `confirm = "ok"`, `cancel = "back"`, `pause = "start"`.

| Action | RGB30 | Pi keyboard (target) | Pi keyboard today | Pi pad |
|---|---|---|---|---|
| confirm (`"ok"`) | B (`pad_ok = PAD_B`, `src/rgb30/pad.c:10`; `confirm=a` swaps to A) | Enter: to implement (kernel) | Space (`docs/api/input.md:9`) | cross (DS4) / A (Xbox) (`docs/api/input.md:9`) |
| cancel (`"back"`) | Y only, A is no back (`claude/pad-y-cancel`, to merge; games keep A/B as before) | Esc: to implement (kernel) | X (`docs/api/input.md:9`, `:75`) | circle (DS4) / B (Xbox) (`docs/api/input.md:9`) |
| pause (`"start"`) | Start: system menu (controls, volume, save, quit) | Esc as well: to implement (kernel) | Enter (`docs/api/input.md:78`) | Start / Options (`docs/api/input.md:78`) |
| home | Start+Select: back to the bm menu, the game frozen (`docs/api/input.md:81-82`) | | | |

Start opens `lib.pause` (`src/script/bmlib.lua:1201`, opened by `btnp("start")` at `:1218`). Select may open an in-game menu where a game has one.

## 2. Gameplay Schemes

A game picks one scheme with `lib.input({ scheme = "scroller" | "topdown" | "stick" })`; without a `scheme` it is `"scroller"` (`lib.input_scheme` tells which one runs). Movement is the D-pad, or `stick()` in the stick scheme. The button names are the RGB30's; on the Pi the same keymap names resolve to the Pi pad and keyboard ("ok" is the cross / A / Space, the other A/B button the circle / B / X).

| Action | scroller 2D (default) | top-down 2D (e.g. Yharnam) | stick (e.g. Overbit) |
|---|---|---|---|
| act1 | B (`"ok"`): jump | B (`"ok"`): attack, interact | R2: fire |
| act2 | Y: attack | A (the other A/B button): dodge, run | L2: secondary fire |
| act3 | A (the other A/B button): secondary action | Y: secondary (pistol, ...) | L1: ability |
| act4 | X: game menu or special ability | X: game menu or ability | R1: ability |
| act5..act8 | | | B, A, Y, X: jump, crouch, reload, ultimate and similar; X may be the game menu |
| shoulder_l / shoulder_r | L1 / R1 | L1 / R1 | (taken by act3 / act4) |

Keyboard keys for the actions today: act1 Space, act2 X, act3 V, act4 C, shoulders Q / E (`docs/api/input.md:9`, `:70-78`, `:97`); the keyboard map per scheme is to implement (kernel). Read the buttons through the actions, never through the letters.

## 3. Declared Exceptions

A game that needs a different binding declares it with `lib.input({ scheme = "topdown", act3 = "x" })`: every key but `scheme` is an exception. Each such key is listed in the game's `docs/progress/<game>.md` with the reason. Any other raw button (`btn(4)`, `btn("a")` for an action) is an undeclared deviation and gets migrated to an action.

## 4. On-Screen Hints

Show hints with `prompt("act1", x, y)`, which draws the right symbol for the player's controller (`lastinput()` tells which: `docs/api/input.md:98`). Write hints through `prompt()` so key names never appear as literal text. A cartridge that lists the RGB30 carries no keyboard words in `prompt()`. It gets RGB30 pad bindings.

## 5. Editors

The editors keep their own keyboard model; the standard above is for games. See [suite-code.md](suite-code.md), [suite-pixel.md](suite-pixel.md), [suite-sdk.md](suite-sdk.md) and [docs/video/07-code/input.md](../video/07-code/input.md).

## 6. Inventory

`python scripts/input_inventory.py --games` lists the raw buttons each game reads and compares them with this table. It prints one markdown table, cart by cart (`std`, `exc:<button>`, `raw:<button>` or `-`); read-only, exit 0.

---

## 6b. Player remap (CONTROLS-1)

Until 28 Oct 2026 Settings > Controls let the player change, per scheme, the button of each action; the page is gone, and a player who wants the system remap writes `controls=` in `bm/config.txt` by hand. Each game now gives its own remap in its menu (Overbit: COMMANDS, see `docs/api/input.md`). The schemes: Scroller games (act1-act4), Top-down games (act1-act4), Stick games (act1-act8), Pause and menus (pause, and on the RGB30 the menus' OK and Cancel). Movement and the shoulders of the scroller and top-down schemes are not remappable. The choice is global, stored in `controls=` of `bm/config.txt` (`docs/spec/config-keys.toml`); an action left on Default keeps the game's own button. A game declares its scheme with `lib.input({scheme=})`, which calls `controls(scheme)`; the runtime then replaces the game's buttons of the remapped actions in `keymap()` and again when the game resumes. The player's remap overrides a game's declared exception. The keyboard stays as is (the panel shows it). Access from inside a game: not yet.

## 7. Decided and Open

Decided by the user on 19 Oct 2026: menus B ok / Y cancel with A no back; the three gameplay schemes of section 2 (each game declares its scheme); keyboard Enter = ok and Esc = back and pause; existing games migrate to `lib.input` one by one as they are fixed; the bindings become mappable from Settings > Controls (CONTROLS-1).

Open:

1. Kernel: the keyboard map (Enter = ok, Esc = back and pause, one key set per scheme) and the merge of `claude/pad-y-cancel`; separate Work Orders.
2. Where a game's exceptions are recorded in the cartridge (INFO, section 10), besides its `docs/progress/<game>.md`.
3. Whether `lib.input` re-runs on a controller change (`lastinput()`); to re-evaluate once the remap menu exists.
