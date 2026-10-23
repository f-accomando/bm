# NEXT

State of 22 Oct. Goal now: clean the repository step by step and reach a stable point. Waiting for the user: the Pi runs below.

Rules for this list:
- Do the first task in "Queue". When it is done, delete it from this file. Then read this file again and start the next task.
- No new PRs (user, 22 Oct). One branch `claude/<name>` from an up-to-date `bm-core`, one subagent (`bm-runner`), pushed, no PR. The SPV brings the branch into `bm-core` after build and selective tests are green (`git merge-tree` check, then ff-only), then deletes the branch (remote and local, by name) and its worktree.
- Run only one task that uses `make` or WSL at a time. Before a `make`, check with `ps` in WSL that no other make/qemu runs. Never `pkill` broadly.
- A task that needs the Pi: prepare it, put the exact steps in "Waiting for the user", go on.
- When a task closes, remove the finished item from `docs/ROADMAP.md`, `## Open work` of the area sheet, and this file.

## Schedule (22-23 Oct)
Rules:
- Use WSL (`make`, QEMU) in at most 2 sessions at once. A third session does only light jobs: docs, Python tools, JavaScript, no `make`.
- Only ONE session uses the Pi. It is the SPV session.
- The user is away. The SPV opens WSL and the Pi bridge by itself. The bridge reads the password from the owner's 0600 file `~/.pi_bridge.pw` or from `BM_PASSWORD`. No agent reads or prints it.
- For every item: open a branch, develop, integrate into `bm-core`, close, delete the branches and worktrees, update the docs, remove the item from `docs/ROADMAP.md`, update this file.
- If all items are done before Fri 23 Oct 20:00, take the next open item from `docs/ROADMAP.md`.
- The user does the RGB30 hardware tests and the Pi runs. Write the results into the sheets when the user gives them.

Thursday evening:
- Close the work in flight: Pi numbers for multitasking stages 3-5 (web bench below), bench for hero, VS and cel, herocart LOD triangles.
- Run the hero benchmark on the Pi. Write the numbers into the sheets.

Friday morning:
- The three tools from the bundle (done).

Friday afternoon:
- API-4: run the Video bench on the Pi Zero W (Dev tab, or the monitor line `:video`) and send the RLE565 lines of the report.

Friday after the Mali device runs (done by the SPV through the RGB30 bridge, one session only on the RGB30):
- Mali alignment with the Pi driver (API-5), stage by stage, each stage tested on the RGB30 through the bridge `--dev rgb30`.

Friday evening (done 23 Oct, early):
- Branches and worktrees checked. The branches left are the ones in "Never delete". The unintegrated ones are also in `C:\Users\Utente\Documents\GitHub\bm-archive\unintegrated-2026-10-23.bundle`.
- The host test sweep ran: one failure, `make test-write`. Cause: the working tree of the main checkout had CRLF in `tests/write/input.txt` and `reopen.txt` (fixed). No code fault.

## Waiting for the user (decisions and Pi runs)
- Hardware check of the editor additions of 23 Oct (APP-17 to APP-28, list in `docs/progress/tools-suite.md` section *Editor additions*): try each new key on the Pi and the RGB30; send what fails. On the RGB30 SDK: press `t` on the map with a 4 x 4 tile block, paint a 3x3 area, erase the middle, then undo. Check also that the crowded hint bars of Pixel, Mesh and Animator are still readable (a proposal: one fixed chip for all keys).
- Pi run, 23 Oct: the `X g` freeze test passed; still open: a long run and a night in the menu with no false restart (SYS-2), and `b3d tests=htmap profiles=VS` shows a worst frame of 41 ms for `ht_p100m` against 17 ms (API-6, check).
- Decision, line endings: 1233 tracked files are CRLF in the working tree of the main checkout (index is LF). Cause: `core.autocrlf=true` in the global and user git config, `false` in the repo. Choose: renormalize the working tree (`git add --renormalize .` then `git checkout -- .`), and add `* text=auto eol=lf` to `.gitattributes`. Host tests that read input files can fail the way `test-write` did.
- Decision, stray folder `C:` in the repo root (made by a command with a Windows path). Delete it?
- Decision, pad names in game code: the standard `docs/design/games-input-standard.md` uses `act1`..`act8`. Proposal: write button names (`"x"`, `"l1"`) or gesture names (`"attack"`) in game code, add the DS4/Xbox/RGB30 symbol column, migrate Yharnam. User said to ignore it for now (23 Oct).
- Decision SYS-11 (blocks R15, live reload from the PC): what a send from the PC does to a running game (wait for the end, prompt, "busy, queued" answer, send of the running cart: refuse, queue or ask). Plan and "done when": `docs/progress/tools-code-templates.md`.
- Pi and RGB30 check, bm Code templates (R16): Ctrl+N shows 6 starters; choose each, Ctrl+S, F5. The Versus starter needs two controllers. Not verified on hardware.
- Pi check, ghost wire in bm Mesh (benchmarks only; for the SPV): load a ghost of 2000, 20000 and 65535 faces (a `.bmm` made on the PC) and read the frame rate with the ghost shown and hidden. Host numbers and what is not measured are in `docs/progress/tools.md` (Modelling suite, step 7). Set `T.G.GHOST_EDGES_FRAME` (now 4000) from the result.
- Decision: `gpu3d_qpu` default. Pi measurement: full-screen fade 20.4 ms on ARM, 9.3 ms on the QPU (one run). Recommendation: make `dark_end` use the QPU by default on the Pi with the ARM fallback, after one longer soak run (Yharnam) on the Pi; today the key is off.
- RGB30 runs (the user does them; exact steps in `docs/progress/graphics-mali.md` `Open work`): Dev > GPU test with the variant probe, then 3D Bench with `mali3d=1`.
- RGB30 runs for the performance budget (API-1; the user does them): 3D Bench with the Mali driver and the 2D scenes `b3d tests=map8,sprites,stack,fade,light,gpu2d`. Send the reports. Then fill the RGB30 column of `docs/PERF-BUDGET.md` and remove API-1 from `docs/ROADMAP.md`.
- Pi runs (the user does them; the test integration `claude/pi-test-integration` of 22 Oct is on the Pi; `herocart.bm` stage 1 is on the SD):
  - Scenes `map8`, `sprites`, `stack`, `fade`, `light`, `herotris` and `cel` are done (22 Oct, one run each; more runs only if the numbers are needed again). The Pi numbers are in `docs/PERF-BUDGET.md`.
  - Herocart sweep (Start+A, Start saves `HEROCART.TXT` in `/docs`): GPU on/off; after the fix also VS 2 and Cel (4 bands) / Dither off. bm3D 7.5: with VS 2 compare Cel off and Cel 4 and Outline off / hull (baked) / flag 512 at LOD 0 and 1. The 3D Bench of 22 Oct already gave the numbers (cel +10%, outline +31%, `cel light yes`); this cart run only confirms them.
  - Recorder: `rec=on rec_fps=15 rec_size=320x180`, play Overbit; read the end-of-run lines.
  - Browser `bmweb` (stages 2b-5, built, not verified on hardware): build `make build/carts/bmweb.bm` and copy it to `/bm` on the SD (`./easy_install.sh` copies `dist/`). Join WiFi and allow the network when the cart asks. Open `bm Web` from the menu. Press Y, type the address of a site you may read (https only), press A. Check and note: (1) the status line shows "waiting ... (pace)", then "reading robots.txt", "connecting", "loading"; (2) the time of the TLS stall (the screen may freeze about 100 ms); (3) the page appears; the status line shows the Lua memory in KiB at the right (note it after a page of about 300 KB); (4) right/left move the link focus, B goes back, Start marks a page; (5) X loads the pictures one by one (WebP, PNG, JPEG) and the decode time per picture; (6) a link to a `.glb` or `.zip` opens "Save this file?"; A writes `bm/web/<NAME>.<EXT>` and a `.TXT` note. Do the same on the RGB30 (D50 stays `todo/untested` until then). QEMU has no network, so `make test-web` and `test_bmweb` do not replace this run.
  - Multitasking stages 3-5 (web bench, not a game): build with `python scripts/mkbm.py -o build/webbench.bm --lua tests/web/pi_bench.lua --title "Web bench" --author bm --res 640x360`, copy to `bm/` by hand, press A. Set `URL` at the top of the file to an https address of a file of at least 2 MiB that sends `Accept-Ranges` for the resume part. Read `/docs/WEBBENCH.TXT`: sync call ms, async worst frame ms, save during a queued write, resume 1 and 2 (`resumed` must be above 0). Also from the console log of a report: one TLS handshake for the 3 requests (`menu stalls:` handshake count).
  - `gpu3d_qpu=1` step, `PI_O3_FILES`.
  - RGB30, BETA-6 (consistency): with a Bluetooth pad paired, reboot and check that the pad reconnects (D02) and that its PS button leaves a game (D01). Then set D01 and D02 to `resolved` in `docs/spec/divergences.toml`. QEMU proves the menu parts (frozen game, tool names, Market key); it does not prove the pad, the screen or the sound.
  - BETA-6 (consistency), left for the owner or the console: gate at 11 blockers. D03, D05 need your decision. D49 is accepted as it is (the Pi menus keep A and B); tell me if the Pi menus should follow `mo` and `mc`. On the RGB30, try Dev > Stack bench (new, QEMU only) and compare with the Pi's numbers. D10, D15, D17, D23, D24, compat-policy, cart-type and folders-bm-bme need real work (see `docs/progress/platforms.md` `Open work`).
- Projects in `/bme` (M47, built and tested in QEMU, not on hardware): on the Pi and the RGB30 open a game in bm SDK, save: the copy is `/bme/NAME.BME` and the status line says so. Press `Ctrl+B`: the game is in `/carts` (Pi) or `/bm` (RGB30). Copy an old SD card with a `.bme` next to its game: it still opens and saves in place (`old place`). The Pi menu and the Lib tab also read `/bm` (QEMU `test_sd_games_bm`). Still open: stop scanning `/carts` (needs the decision on the Pi games folder).
- Decided 22 Oct, to implement (design in `docs/design/single-bm-and-adaptive-screen.md`): old `.b16` = Pi + RGB30; old `.bm` = Pi only; grey not hidden, start after confirm; audio max 8M; pad mouse; bars G1; 720x405 mode; `.bm` for all.

- M48 mouse (Pi and RGB30, USB mouse): in bm Write click, drag, double click, wheel, right-click menu and the bar chips; in the other tools the table "Mouse per program" in `docs/progress/tools.md`. Send what fails. Also: drag and double click in the SDK code view, a click in a dialog line puts the caret, the Pixel palette wheel (not verified on hardware).
- Pi run, pointer over the covers (the user does it; kernel built by `claude/fix-menu-cursor-jump`): in the menu with a USB mouse, move the pointer across the covers, fast and slow, with all covers loaded and while they load. The pointer must follow the hand with no jump. If it still jumps, send the `menu idle:` lines of the run (`+x` and `sd retried/timed out`): the next causes are in `docs/design/multitasking-report.md` section 7 (no `usb_poll` in a cover read, the gain of `accel()` on a large sum).
- Yharnam `.b16` on the Pi and on the RGB30: run the same `.b16` on both and send the reports (screen, input, sound, a 5 minute hunt). Not verified on hardware. The lint of `mkbm.py` (and of the SDK export, same rule list) gives one warning for Yharnam (`math.random`, line 2898).

## Queue (development, in order)
1. Remote branches left: `api-particles3d`, `beta1-c4-ring2`, `beta1-c4-ring3`, `gpu3d-fade`, `stages3d-budget`, `stages3d-qemu-test`, `v3d-compact-tex-attrs`, `reports`. The Mali work is in `bm-core` (sheet `docs/progress/graphics-mali.md`, `Open work`: the device runs).
2. Old work in the archive: 23 old unmerged branches are in `C:\Users\Utente\Documents\GitHub\bm-archive\branches-2026-10-22.bundle` and are deleted from the repository. Port their content on demand from the bundle: budget cart and `perf_table.py`, RGB30 cover job D10, xmenu close game, RGB30 QEMU freeze and delete tests, bench-vs-etc1 VSE column, 3D budget table, PROP-1 and PROP-2 docs, gfx-backends rule.
4. Single-bm (SYS-8), next stage: the screen modes (720x405, `safe_area()`, `aspect()`, bars), then the input chips and the end of `.b16`. The compat check is in the menus, the Market list (`targets` line) and the load-time message; the build lint is done (`docs/spec/lint-rules.toml`, `make test-lint`); what is left is in `docs/progress/b16.md`, `## Open work`.
5. VID-REC next slices: real 8-bit palette, audio (the RLE565 encoder is done; the Pi numbers are the API-4 item above).
6. Web player (WEB-1): stages 1-3 are in bm-core (emsdk is in `~/emsdk` in WSL). Saves (IndexedDB) and 3D frames are checked. The clock and the sound queue are tested in Node (`web/bmring.js`). Next: in a visible tab play a game and read `bmLauncher.stats` (`hz`, `audioUnderruns`) and listen to the sound, then the release page. Plan in `docs/design/bm-web-player.md`.
7. Triangle throughput: stage 1 is done (`gpu3d_queue=1` is the default). Stages 2 and 3 are measured (Pi, 23 Oct, API-6): compact attributes gave no gain, so `gpu3d_attr` is off by default again; draw merging (`gpu3d_merge`, hidden, on) gained +24% for 100 small pieces and lost 26% for 10 big ones, so a run now merges only from 32 pieces with at most 150 faces a piece. To collect on the Pi: `b3d tests=htmap profiles=VSQ` and `profiles=VS` (`ht_p10m` must equal `ht_p10`, `ht_p100m` must keep +24%); see `docs/design/triangle-throughput.md` section 8. The kernel for the run: `dist/kernel_bench5.img` (not in git). The V3D async proposal is decided "skip" (12 Oct, `docs/progress/v3d-notes.md`: interrupts, triple buffering, `v3d_async.c`). `queue=2` and `gpu3d_later` replaced it. Chain jobs (CT0/CT1) only if a measurement shows the GPU idle between jobs.
8. Evaluate the security findings of `docs/OS-CAPABILITIES.md`: `lua_load(..., NULL)` at `runtime.c:9438` may accept bytecode; default net password is 6 digits from `srand(timer_ticks())` (`netcon.c:183`), plaintext on TCP 3334; carts can `ls` the SD and `cart_read` any `.bm` without a prompt; FAT dates fixed at 2026-01-01 (`fat.c:682`); docs disagree (`docs/progress/b16.md:16` says 4 MiB heap, code 64 MiB; RGB30 menu framebuffer 360x360).
9. R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain.
10. Mali alignment with the Pi driver (API-5), stage by stage, each stage tested on the RGB30 through the bridge `--dev rgb30`.
11. Multitab (APP-8), what is left: show the module tabs of bm Code in the bm SDK code page (`U.tabbar` and `U.modules_split` are in `src/script/bmui.lua`; the SDK keeps `S.lines` and a local `undo`, so a module switch must swap both) and, if wanted, files of their own for the modules (needs a file API for tools in `runtime.c`). See `docs/ROADMAP.md`.
12. Modelling suite. Phases 1 (APP-10), 2 (APP-11, box select, hide and isolate, soft move, numeric entry, reference pictures, 100 undo steps) and 3 (APP-12, bevel, box projection, humanoid base with skeleton, Studio ramp, corner and rounded blocks) are done: `tests/studio/humanoid_host.lua` (in `make test-studio`) builds a humanoid of 1540 triangles and 16 bones with keys in bm Mesh and bm Animator and plays a walk. Bones in bm Mesh (APP-14) are done (Shift+B; the humanoid test skins with it). The live mirror covers adding a part and the loop cut (APP-16, done). The suite plan is in `docs/design/modelling-suite.md`. Host tests only; hardware not verified: try the bevel, the projection and the new blocks on the Pi and the RGB30.
13. Multitasking per workload (SYS-10): phase 1 is the measurement list in `docs/design/multitasking-report.md` section 8 (benchmarks only, no live game). Order of phases: 1 measure, 2 SD card, 3 downloads, 4 covers and app start, 5 games, 6 RGB30 core. Start with phase 1 and the stall attribution line. The pointer jump over covers is explained in section 7 of the report; another runner fixes it in menu code.
14. SYS-9b: game-side graphics options and an optional start benchmark that picks values (not built). Decide first if `gpu3d_2d` goes on by default: the Pi measured +6.7% (`gpu2d` test) and 3569+ sprites against 2574 on the ARM; check one played game on the Pi, then flip it in `runtime.c` and the manual table. Sheet: `docs/progress/v3d-flags.md`.

15. RGB30 HDMI and multiplayer (queued 23 Oct by the user): SYS-12 HDMI output (panel off, HDMI on), SYS-13 Bluetooth keyboard and mouse for the SDK apps, SYS-14 more Bluetooth pads, split and shared screen. Start with a study of the RK3566 HDMI path (SYS-12); it needs the user's hardware. See `docs/ROADMAP.md`.

## Open issues
- The Pi froze once on `b3d tests=herotris` with all profiles in one line (8 profiles in one run). The user power-cycled the Pi. The same profiles run one at a time did not freeze. Cause unknown, no fix. Command: `b3d tests=herotris` (no profile named, so all 8 profiles in one line). To reproduce, run it again and note the profile that is running at the freeze.

## Never delete
`bm-core`, `reports`, `claude/beta1-c4-ring2`, `claude/beta1-c4-ring3`, and the graphics branches below until their commits are ported or the user says go.

## Useful commits on graphics branches (keep the branch until the commit is ported)
- `claude/gpu3d-fade`: a2c1e904 and 4f7fbc3b (fade through the `g16_fade_accel` hook).
- `claude/api-particles3d`: a2795d0f (runtime 3D particles), ea7e0d2e and 0c543cc6 (API docs).
- `claude/v3d-compact-tex-attrs` (b8f01430, 1ced7d02): compact attributes (`gpu3d_attr`, off by default; the branch has the texture part, stage 2b).
- `v3d-async-proposal` (2c383089, 69ef8453, 23b9c7bf): read before any V3D async work.
- `claude/stages3d-qemu-test` (eddfd2e6, 628739b7, 26671568, c0ca055a) and `claude/stages3d-budget` (342a8018, bb9d2a12): Stages 3D carts and governor.

## Done on 22 Oct (do not redo)
RGB30 QEMU flaky tests (SYS-1): five tests wait for a printed state, no blind keys. Next: do the same for `test_bluetooth_page_without_chip`, `test_games_in_carts`, `test_menu_games_and_hidden_bm` and `test_screen_console` (`docs/progress/platforms.md`), and run each new RGB30 test once under load.
Herocart chain, PAD-Y-1 (Y is the only back button on the RGB30), the `bm-runner` agent, the `Integrate` rule in `CLAUDE.md`, branch cleanup (old branches are in the bundles in `C:/Users/Utente/Documents/GitHub/bm-archive`), Market: `apps/` move done 22 Oct, the Pi numbers of 22 Oct in the area sheets, decisions on `gpu3d_attr` (off), `lib.particles` on QPU (parked) and `test_usb_mouse` (strict assertion).
PRs 80-120 closed (stack bench, audit and bridge tools, games input, controls remap, web mock, crumbs, stackv, QPU steps, Stages 3D, manuals, stack2mesh, Pixel Stack page, particles3d, multitasking stages 1-2 and alternatives, single-bm design with decisions, triage doc, browser design and stage 1, `map()` tile path and `PI_O3_FILES`, triangle throughput doc, hero cart doc, 2D bench scenes, OS capabilities doc, WebP decoder, Market with the 7 editors as apps). 33 merged branches deleted. 11 old `claude/*` evidence branches deleted. 7 editors published in `bm-market` (village removed).
