# D05-STICK - the RGB30 stick as a pointer, option on|off, only for apps and games with the API (split: D05-STICK-a, D05-STICK-b)
SPV assigns the two parts in order (a, then b). Split because it crosses three layers (kernel module, Settings, runtime plus docs) and each part has its own check. Each part <= 45 min.
Decision of the user, 17 Oct: the RGB30's stick works as a pointer through an on|off option in the menu (Settings), ONLY for apps and games that call a dedicated API; the kernel pointer (M32, `src/kernel/pointer.h`) stays for the real mouse only. The mouse code stays.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, then `git worktree add -b claude/rgb30-stick-pointer C:\Users\Utente\Documents\GitHub\bm33-stick origin/claude/integrazione-ottobre`); ONE branch `claude/rgb30-stick-pointer` for a and b (b continues on a's commit). Never push to bm-core. The worktree is NOT `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. Run `make` and host tests with `scripts/wsl-run.ps1` (skill `bm-wsl`). Needs DIV-1 delivered first (b rewrites the D05 block DIV-1 edits).

## What exists (checked with `git grep` on the base branch)
- The kernel pointer is mouse-only by design: `src/kernel/pointer.h:2-5` ("Never by a controller"), `src/kernel/pointer.c:93-97` (`pointer_update`: "only a mouse moves it"), read of `mouse=` at `src/kernel/pointer.c:25-29`, called from `src/kernel/config.c:115`. Config key `mouse` is registered at `docs/spec/config-keys.toml:357-364`. Settings shows it as an info row `src/kernel/settings.c:402-415` (inside `case HOME_CONTROLLERS`, `:368`). It stays as it is.
- Lua `mouse(on, [arrow])` / `mouse()` / `mousep()`: `src/bm/runtime.c:4503-4531` (registered `:4772`); docs `docs/api/input.md:17-27`. `mouse()` is nil without a real mouse (`src/bm/runtime.c:4513`).
- The right stick is `stick(p, 1)` (`docs/api/input.md:15`), read by `input_stick_r` (`src/kernel/input.h:75`, RGB30: `src/rgb30/bm_input.c:151-158`); runtime call site of `input_stick`: `src/bm/runtime.c:1703`. The pad bits are in `rt.praw[p]` (`src/bm/runtime.c:98`); R3 is 32768 and L3 16384 (`docs/api/input.md:95`).
- Carts that already emulate a stick cursor by hand: `carts/nano8/src/40_vm.lua:232-262` (falls back to the left stick when `mouse()` is nil), `carts/overbit/src/10_input.lua:154-163`, `carts/sdk_b16/main.lua:3029`. They are not changed by this WO.
- An RGB30-only Settings row to copy: `R_BATTERY_ICON` (enum `src/kernel/settings.c:91`, helper `:141-148` inside `#ifdef BM_RGB30`, row `:548`, toggle `:876-881`). Config-key block to copy: `docs/spec/config-keys.toml:19-30` (`battery_icon`).
- Host-test pattern: a small C test linking one source: `Makefile:873-877` (`test_config`), the target list `Makefile:830` (`HOST_TESTS`). The host stubs for the pointer: `tests/host/stubs.c:373-379`.
- No file names a stick-pointer function (`git grep -n -i 'stickmouse' -- docs src carts` finds nothing): the name below is free.

## Design (no open choices)
- Key `stick_mouse` in `bm/config.txt`: `on` or `off`; default `off`; RGB30 only (the Pi build ignores it and the API returns false there).
- Settings > Controllers (RGB30): row "Stick as pointer" (choice, `On` / `Off`), hint "The right stick moves a pointer in the apps and games that ask for it".
- Lua API (new, separate from `mouse()` so the kernel pointer is untouched): `stickmouse(on)` / `stickmouse()`.
  - `stickmouse(true)`: the cartridge wants the stick pointer; returns `true` only if the option is on (and the build is the RGB30's), else `false` and nothing moves. `stickmouse(false)` takes it away. The cartridge draws its own cursor (the console draws nothing).
  - `stickmouse()`: `x, y, buttons, pressed` in the cartridge's screen pixels (`rt.g.w` x `rt.g.h`), `buttons` as bits (1 = R3 left, 2 = L3 right), `pressed` the buttons pressed this frame; `nil` if the cartridge did not ask or the option is off.
  - Motion: the right stick (`input_stick_r(0, ...)`), `pixels per frame = stick * w / 150` per axis (4.3 px at full tilt on 640 wide), starts in the middle, clamped to the screen.
- The logic is a pure module `src/kernel/stickptr.c/.h` (no hardware, no config access): easy to test on the PC.

## D05-STICK-a - the module, the key, the Settings row (bm-light, 20 min real)
Files: new `src/kernel/stickptr.h`, new `src/kernel/stickptr.c`, new `tests/kernel/test_stickptr.c`; edit `src/kernel/settings.c`, `docs/spec/config-keys.toml`, `Makefile` (a `$(BUILD)/host/test_stickptr` rule and `test-stickptr` added to `HOST_TESTS`, as `Makefile:873-877` and `:830`).
1. `stickptr.h`:
```
typedef struct { float x, y; uint8_t buttons, pressed; } stickptr_t;
int  stickptr_parse(const char *v);                 /* "on" -> 1; NULL, "off" or anything else -> 0 */
void stickptr_reset(stickptr_t *s, int w, int h);   /* in the middle, no buttons */
void stickptr_step(stickptr_t *s, float sx, float sy, uint32_t pad, int w, int h); /* pad 32768 = button 1, 16384 = button 2 */
```
2. `stickptr.c`: `stickptr_step` adds `sx * w / 150` and `sy * w / 150` (both with `w`, so the speed is the same on both axes), clamps x to `0..w-1` and y to `0..h-1`, sets `buttons` from the two pad bits and `pressed = buttons & ~old`.
3. `settings.c`: in the existing `#ifdef BM_RGB30` helper block (`:141-148`) add `static int stick_mouse_on(void) { return stickptr_parse(config_get("stick_mouse")); }`; add `R_STICK_MOUSE` to the enum after `R_BATTERY_ICON` (`:91`); in `case HOME_CONTROLLERS` (`:368`), inside a new `#ifdef BM_RGB30 ... #endif` just before the "Pair a new controller" row (`:416`), the `MENU_ROW_CHOICE` row; in the toggle switch next to `R_BATTERY_ICON` (`:876`) a case that does `config_set("stick_mouse", stick_mouse_on() ? "off" : "on"); config_save();` and sets `d->note` as the battery one does.
4. `config-keys.toml`: a `[[key]]` block `name = "stick_mouse"`, `scope = "rgb30"`, `values = "on or off"`, `default = "off"`, `meaning = "The right stick moves a pointer in the apps and games that ask for it (stickmouse()); the kernel pointer stays for a real mouse."`, `settings = "Settings > Controllers > Stick as pointer"`, `read_in = "src/kernel/settings.c:<line of the config_get, re-found with git grep -n>"`; place it in the file's alphabetical order.
5. `test_stickptr.c`: asserts for `stickptr_parse` (NULL, "off", "on", "ON" -> 0, 0, 1, 0), reset in the middle, a full right push for 10 frames on 640x360 gives x > 320 + 40, clamping at 639 / 359 and 0, `pressed` is set only on the frame the bit goes up.
Verification: `scripts/wsl-run.ps1 make test-stickptr` -> exit 0; `scripts/wsl-run.ps1 make TARGET=rgb30` and `scripts/wsl-run.ps1 make` build; `python scripts/consistency.py check` -> exit 0; `git diff --stat`: 6 files at most. `not verified on hardware` (the row on the RGB30 menu is not seen).
Delivery: commit `rgb30: stick pointer module, stick_mouse key and Settings row`.

## D05-STICK-b - the API, the docs, the spec (bm-light, 25 min real; after a)
Files: `src/bm/runtime.c`, `tests/host/stubs.c` (only if bmhost fails to link), `tests/gameapi/` plus its `Makefile` target (or a new `tests/stickptr/cart.lua`), `docs/api/input.md`, `docs/spec/divergences.toml` (D05 block only), `docs/progress/system.md` (the D05 line in `## Open work`, `:74`, if present), `docs/spec/CONSISTENCY.md` (only if `consistency.py check` demands it).
1. `runtime.c`: next to `int mouse, mouse_arrow;` (`:157`) add `int stick_ptr; stickptr_t sp;`. Add `l_stickmouse` right after `l_mousep` (`:4531`): with an argument set `rt.stick_ptr` only `#ifdef BM_RGB30` and only if `stickptr_parse(config_get("stick_mouse"))`, call `stickptr_reset(&rt.sp, rt.g.w, rt.g.h)` when it turns on, return the boolean; without an argument push `nil` unless `rt.stick_ptr`, else `x, y, buttons, pressed` (integers; `0` for buttons and pressed while `rt.leave_ask`, as `l_mouse` does at `:4519`). Register `{ "stickmouse", l_stickmouse }` next to `mouse` (`:4772`). Once a frame, right after `rt.ptr = *pointer_update();` (`:5131`): `if (rt.stick_ptr) { float sx, sy; input_stick_r(0, &sx, &sy); stickptr_step(&rt.sp, sx, sy, rt.praw[0], rt.g.w, rt.g.h); }`. Turn it off where `rt.mouse` is reset on a cartridge change (`git grep -n 'rt.mouse' src/bm/runtime.c`; the `pointer_env(0, 0, 0)` area, `:7221`).
2. `docs/api/input.md`: after the `mousep` row (`:26`) two rows for `stickmouse(on)` and `stickmouse()` as in "Design", and one sentence under the "Mouse and pointer (M32)" paragraph (`:17-20`): the stick pointer is separate from the kernel pointer, is RGB30 only, needs Settings > Controllers > Stick as pointer on, and the cartridge draws its cursor.
3. `divergences.toml` D05 block: `cause = "hardware"`, `capability = "pad-only"`, delete `priority`, `status = "open"`, `action = "none: keep the docs honest"`, `rgb30 = "no menu pointer; the stick is a pointer in apps and games that call stickmouse(), with Settings > Controllers > Stick as pointer on"`, and in `note` "built 2026-10-17 by D05-STICK-a and D05-STICK-b, not tried on hardware".
4. Test: a bmhost cart (add to `tests/gameapi` or a new `tests/stickptr/cart.lua`, wired as `Makefile:503` does for `test-gameapi`) that calls `stickmouse(true)` and checks it returns `false` (no option on the PC) and that `stickmouse()` is `nil`. The on-path is covered by `test-stickptr` of part a.
Verification: `scripts/wsl-run.ps1 make test-gameapi` (or the new target) exit 0; `make TARGET=rgb30` and `make` build; `python scripts/consistency.py check` exit 0 (the API rule finds `stickmouse` documented and registered); `python scripts/docs_audit.py score` not worse; `python scripts/consistency.py gate` no longer lists D05. `not verified on hardware`.
Delivery: commit `rgb30: stickmouse() API, docs and D05 spec`.

## Delivery criterion (both)
`DELIVERED D05-STICK-a|b @ claude/rgb30-stick-pointer@<sha> · verifica: <command> -> <result> · N files · non verificato su hardware · <ISO>`. Push only the branch. Last commit line `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
Designated agent: bm-light for both (the design is closed here). Estimates halved: a 20 min, b 25 min.

## Open decisions (for SPV)
1. Right stick plus R3/L3 as buttons is the choice made here (the left stick stays for the game's movement; A/B belong to the game). To use other inputs, change the Design and `stickptr_step` only.
2. The Pi ignores the option (returns false). If the Pi's pad stick should work too, drop the `#ifdef BM_RGB30` in b and the row's guard in a; D05 would then be resolved instead of hardware.
3. Carts nano8, overbit, sdk_b16 keep their own hand-made stick cursor; moving them to `stickmouse()` is a later WO.

## Proof for the user (does not block)
After delivery, on the RGB30: Settings > Controllers > Stick as pointer: On; a test cart calling `stickmouse(true)` draws a cursor that follows the right stick, R3 clicks. Not verified on hardware.
