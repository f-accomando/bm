# bm Code templates and live reload (R16, R15)

Area sheet of the tools. Main sheet: [tools](tools.md). Design: [suite-code](../design/suite-code.md), Proposal 5.

## Done: templates for "New project" (R16)

- Ctrl+N (and `New project` in the menu, and `+ New project...` in the Ctrl+O list) opens a list: `New project: choose a template (Enter)`. Enter chooses, Esc leaves with no project. The list is the `files` overlay of `carts/code/main.lua`; the mouse and the pad work as in the other lists. After the choice, bm Code asks the 8.3 name as before.
- The templates are the table `TEMPLATES` in `carts/code/main.lua`, between the lines `-- templates: begin` and `-- templates: end`. Each is `{ id, name, desc, code }`.
  - `empty`: the old starter (`TEMPLATE`, a ball that moves). One module.
  - `platform` (Platform 2D): run, jump, planks. Modules main, level, hero, loop.
  - `topdown` (Top-down 2D): walls and coins. Modules main, world, player, loop.
  - `shooter` (Shooter 2D): ship, shots, foes, game over. Modules main, ship, foes, loop.
  - `versus` (Versus 2D): two fighters on two controllers, punches with boxes. Modules main, fight, loop.
  - `scene3d` (3D scene): floor, cubes and a ball. Modules main, loop.
- A template needs no art and no map: it draws with `rectfill` and `circfill`. The SDK templates (`carts/editor`) paint sprites and maps; they stay in the SDK.
- The modules are `--@module name` lines (see `tools.md`, APP-8). A module sees the locals of the modules before it, so the last module (`loop`) holds `_update` and `_draw`. A new project opens with the module tabs (`MOD.load`).
- Rule: keep the code of a template under the 200 locals limit of a Lua main chunk. New state goes in table fields.
- Tests:
  - `tests/studio/code_templates_host.lua` (in `make test-studio`): reads the block from `main.lua`, checks the module split and join, compiles each template and runs it for 30 to 6000 frames with stand-ins. It checks that the hero lands and jumps, the player is stopped by a wall, foes come, a shot kills a foe, a foe ends the game, five punches win, the 3D scene draws 7 objects.
  - QEMU `test_code_templates`: Ctrl+N shows the list, Esc leaves, Platform 2D opens the project with its module tabs and Ctrl+S writes `/bme/game1.bme` with the markers.
  - `test_code_editor` presses Enter on `Empty` first.
- Not verified on hardware: the list and the keys on the Pi and the RGB30; the feel of each starter with a pad (the 2 player starter needs two controllers); the look at 360x360 on the RGB30 (the starters use `SCREEN_W` and `SCREEN_H`).

## Blocked: live reload from the PC (R15)

Goal: the PC pushes a changed `main.lua` (or `.bme`) to the console over the bridge or `netxfer`, and the running cart reloads.

R15 depends on SYS-11 (`docs/ROADMAP.md`): what a send from the PC does to a running game. The open questions for the user are in SYS-11:
- may a send wait until the game ends, or does it need a prompt;
- what the sender gets (a "busy, queued" answer);
- what happens to a send of the cart that is running (refuse, queue or ask).

A reload sends the running cart and restarts it. That is the case SYS-11 leaves undecided. R15 changes nothing until the user answers.

What exists today (read only, not changed):
- `tools/bm_net.py --send FILE --to DIR` writes a file on the SD card and `--play FILE` starts a `.bm`. `netxfer.c` answers one transfer at a time, another gets `BY` (see `system-net.md`).
- `bm_net.py` has no `--watch`.

Plan once SYS-11 is decided (testable on the PC and in QEMU):
1. Protocol: a `reload` request with the cart path, answered as SYS-11 says (busy, queued or asked).
2. Host tool: `bm_net.py --watch FILE` polls the file, builds a `.bm` from a `.bme` or a `main.lua` with `scripts/mkbm.py`, sends it and asks for the reload. A unit test with a fake server on the PC.
3. Kernel handler in `netxfer.c`: apply the SYS-11 rule; a QEMU test with the bridge over the user network.
4. The handler is slow main-code work: call `watchdog_progress()` in its loops (`docs/design/watchdog-hang.md`) and keep the SD writer lock (`sdlock.c`).

Done when: SYS-11 has the user's answers, the three steps above pass `tests/qemu_test.py -k netxfer` and a host test, and a Pi run is done by the user.
