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
- API-4: (DONE 24 Oct, report `10993a4b` on `reports` branch). Video bench run on Pi Zero W (c62830b8):
  - SD write: 128 files 256 KB, 1.82 MB/s mean (worst 1060 ms, 35 over 100 ms; raw 15 fps NO GO). SD read: 2.29 MB/s.
  - JPEG decode: 320x180 53.15 ms mean (fits 15 fps budget of 66 ms). 480x270 115.74 ms mean.
  - RLE565 encoder: pixel-art 9253 bytes (8.0% of 115 KB), 0.78 ms encode (1% frame budget), 15 fps (0.13 MB/s) & 30 fps (0.27 MB/s) fits card; photo frame 70306 bytes (61.0%), 1.75 ms encode, 15 fps (1.05 MB/s) fits card. Round-trip exact.

Friday after the Mali device runs (done by the SPV through the RGB30 bridge, one session only on the RGB30):
- Mali alignment with the Pi driver (API-5), stage by stage, each stage tested on the RGB30 through the bridge `--dev rgb30`.

Friday evening (done 23 Oct):
- Branches and worktrees checked. The branches left are the ones in "Never delete". The unintegrated ones are also in `C:\Users\Utente\Documents\GitHub\bm-archive\unintegrated-2026-10-23.bundle`.
- The host test sweep ran: one failure, `make test-write`. Cause: the working tree of the main checkout had CRLF in `tests/write/input.txt` and `reopen.txt` (fixed). No code fault.

## Waiting for the user (decisions and Pi runs)
- Pi run, SD stall plan S0 (the user does it, 24 Oct; kernel from `bm-core` after the merge of `claude/sd-timeout-solutions-220aq8`; plan in [`docs/design/sd-stall-plan.md`](design/sd-stall-plan.md)). S0 only measures; the behavior is the same as before. Steps:
  1. Build and copy the kernel (`./easy_install.sh`), boot, join WiFi, open the console log.
  2. Menu, Games tab with all covers: move the selection through the covers, fast and slow, for 2 minutes while the covers load. Do it once with a fresh boot and once after leaving the menu idle for 1 minute.
  3. Start 3 games of different size (small, about 1 MB, and one over 6 MB, for example Yharnam or Overbit). Note by eye: splash delay, any sound or picture glitch at the start, the freeze when you go back to the menu.
  4. Send the report (the `reports` branch) or the log with: the `menu idle:` lines (`sd sites: ...`, `first word ...`, `sd N retried, N timed out`, `covers ... +x`, `stop`) and the `load:` line of each game (`close, parse, assets+state, mode, audio, lua` in ms).
  What I decide from it: the site of the 500 ms timeouts (S1), the park threshold from the first-word histogram (S2), and which load step causes the splash lag and the glitches (not covered by S1-S4 yet).
- Menu lag on the Pi (read from the console log of 23 Oct, 19:46, report `reports/unknown/20261023-174601_log_pi-zero-w_dev.txt` on the branch `reports`): each cover job loses about 500 ms at every SD read timeout (`covers ... +503.9`, `sd 17 retried, 17 timed out`); the report job loses about 100 ms a slice and a TLS handshake takes 270-300 ms; with no job the menu is healthy (31 of 1864 ms). Next stages after S0 (S1 to S5) are in the plan. The 0.1 s timeout with controller reset was tried and reverted because the Pi became unusable: one change per test. The RGB30 gets a worker core later (SYS-10).
- Session close, 23 Oct: both bridges are still running in WSL (`pi_bridge.py --detach 192.168.1.108` and the RGB30 one). Stop them if you do not need them (kill by process id, one at a time). The WSL `/tmp` is full (3.9 GB, about 60 old `bm64-*` and `tmp*` folders): say if the SPV may delete them. The empty folders `bm33-cursor` and `bm33-ik` next to the repository are held by Windows: delete them by hand.
- Decision, ease-in and ease-out (APP-30): a kernel from before this change refuses a cartridge with a clip of mode 3 or 4 ("bad skeletons (ANIM)"), it does not play it linear. Choose: (1) the editors warn when a clip uses mode 3 or 4 ("needs a kernel with ease-in and ease-out"), (2) the build writes mode 1 or 2 instead when the target is an old kernel (the `targets` line of the compat check), (3) leave it. Recommended: 1 now, 2 with SYS-8.
- Hardware check of the editor additions of 23 Oct (APP-17 to APP-28, list in `docs/progress/tools-suite.md` section *Editor additions*): try each new key on the Pi and the RGB30; send what fails. On the RGB30 SDK: press `t` on the map with a 4 x 4 tile block, paint a 3x3 area, erase the middle, then undo. Check also that the crowded hint bars of Pixel, Mesh and Animator are still readable (a proposal: one fixed chip for all keys).
- Pi run, 23-24 Oct: the `X g` freeze test passed; `b3d tests=htmap profiles=VS,VSQ` (API-6, DONE 24 Oct, report `dfedfea0` on `reports` branch):
  - `ht_p10` GPU+VS (40,360 tris, 16.4 ms) equals `ht_p10m` GPU+VS (40,360 tris, 16.2 ms) — verified identical.
  - Draw merging on `ht_p100m` delivers +33% gain over `ht_p100` (4.0 n60 vs 3.0 n60; 30,121 tris in 16.6 ms).
  - With Queue (`GPU+VS+Q`): `ht_p100m` hits 60,242 tris drawn60 in 16.3 ms; `ht_p10m` hits 91,475 tris drawn60 in 15.3 ms. Sustained 50k-60k tris @ 60 fps confirmed on Pi Zero.
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
  - Browser `bmweb` (DONE 24 Oct, report `62dc910a` on `reports` branch): verified on Pi Zero W hardware (c62830b8): 723 frames @ 59.6 fps, 0 dropped/late frames, avg update+draw 3.22 ms (19% of frame budget), max 4.29 ms. Zero audio crash, zero WiFi drop (GPIO 45 guarded). Search bar and full app verified.
  - Multitasking stages 3-5 (web bench, not a game): build with `python scripts/mkbm.py -o build/webbench.bm --lua tests/web/pi_bench.lua --title "Web bench" --author bm --res 640x360`, copy to `bm/` by hand, press A. Set `URL` at the top of the file to an https address of a file of at least 2 MB that sends `Accept-Ranges` for the resume part. Read `/docs/WEBBENCH.TXT`: sync call ms, async worst frame ms, save during a queued write, resume 1 and 2 (`resumed` must be above 0). Also from the console log of a report: one TLS handshake for the 3 requests (`menu stalls:` handshake count).
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
8. Evaluate the security findings of `docs/OS-CAPABILITIES.md`: `lua_load(..., NULL)` at `runtime.c:9438` may accept bytecode; default net password is 6 digits from `srand(timer_ticks())` (`netcon.c:183`), plaintext on TCP 3334; carts can `ls` the SD and `cart_read` any `.bm` without a prompt; FAT dates fixed at 2026-01-01 (`fat.c:682`); docs disagree (`docs/progress/b16.md:16` says 4 MB heap, code 64 MB; RGB30 menu framebuffer 360x360).
9. R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain.
10. Mali alignment with the Pi driver (API-5), stage by stage, each stage tested on the RGB30 through the bridge `--dev rgb30`.
11. Multitab (APP-8), what is left: show the module tabs of bm Code in the bm SDK code page (`U.tabbar` and `U.modules_split` are in `src/script/bmui.lua`; the SDK keeps `S.lines` and a local `undo`, so a module switch must swap both) and, if wanted, files of their own for the modules (needs a file API for tools in `runtime.c`). See `docs/ROADMAP.md`.
12. Modelling suite. Phases 1 (APP-10), 2 (APP-11, box select, hide and isolate, soft move, numeric entry, reference pictures, 100 undo steps) and 3 (APP-12, bevel, box projection, humanoid base with skeleton, Studio ramp, corner and rounded blocks) are done: `tests/studio/humanoid_host.lua` (in `make test-studio`) builds a humanoid of 1540 triangles and 16 bones with keys in bm Mesh and bm Animator and plays a walk. Bones in bm Mesh (APP-14) are done (Shift+B; the humanoid test skins with it). The live mirror covers adding a part and the loop cut (APP-16, done). The suite plan is in `docs/design/modelling-suite.md`. Host tests only; hardware not verified: try the bevel, the projection and the new blocks on the Pi and the RGB30.
13. Multitasking per workload (SYS-10): phase 1 is the measurement list in `docs/design/multitasking-report.md` section 8 (benchmarks only, no live game). Order of phases: 1 measure, 2 SD card, 3 downloads, 4 covers and app start, 5 games, 6 RGB30 core. Start with phase 1 and the stall attribution line. The pointer jump over covers is explained in section 7 of the report; another runner fixes it in menu code.
14. SYS-9b: game-side graphics options and an optional start benchmark that picks values (not built). Decide first if `gpu3d_2d` goes on by default: the Pi measured +6.7% (`gpu2d` test) and 3569+ sprites against 2574 on the ARM; check one played game on the Pi, then flip it in `runtime.c` and the manual table. Sheet: `docs/progress/v3d-flags.md`.

15. RGB30 HDMI and multiplayer (queued 23 Oct by the user): SYS-12 HDMI output (panel off, HDMI on), SYS-13 Bluetooth keyboard and mouse for the SDK apps, SYS-14 more Bluetooth pads, split and shared screen. Start with a study of the RK3566 HDMI path (SYS-12); it needs the user's hardware. See `docs/ROADMAP.md`.

## Open issues
- [RESOLVED 24 Oct, report `c51c239a` on `reports` branch] `b3d tests=herotris` with all 8 profiles in one line: ran completely in 11 seconds with zero freezes on Pi Zero W (c62830b8). Profile metrics:
  - ARM: 2,385 tris/frame drawn60 (40.8 ms)
  - GPU: 3,214 tris/frame drawn60 (30.3 ms)
  - GPU+VS: 29,398 tris/frame drawn60 (15.4 ms)
  - GPU+VS+Q: 49,956 tris/frame drawn60 (13.7 ms)
  - GPU+VS+Q2: 50,024 tris/frame drawn60 (13.5 ms)
  - GPU+VS+FS2: 28,830 tris/frame drawn60 (15.3 ms)
  - GPU+VS+A8: 28,875 tris/frame drawn60 (15.3 ms)
  - GPU+VS+A8+Q: 49,989 tris/frame drawn60 (13.6 ms)
  Conclusion: Vertex Shader + Command Queue consistently pushes 50k triangles @ 60 fps on Pi Zero hardware.

- [RESOLVED 24 Oct, verified on Pi Zero W hardware] `Hero Stress` (`herocart.bm`) High-Throughput Target Benchmark:
  - **Pi Zero W @ 640x360 (52,992 triangoli totali)**:
    - 50 eroi a LOD 0 (984 tris ciascuno) + scenario piazza (~3.792 tris).
    - *Baseline iniziale*: 18.2 ms frame time, 53-54 fps, 8 skip/s, anim 2.0 ms, wait 6.0 ms (barra superiore rossa).
    - *Post-ottimizzazione* (pose bucketing 16 walk poses, map3d_height caching, zero-alloc draw_opts/drawn, pipelining `gpu3d_later=1` + `queue=2`):
      **15.1 ms frame time** (-3.1 ms), **59 fps**, **2 skip/s**, **anim 0.7 ms** (-65%), barra superiore completamente verde entro i 16.6 ms.
  - **Pi Zero W @ 1080p 1920x1080 (27,408 triangoli totali @ 60 fps)**:
    - 24 eroi a LOD 0 (984 tris ciascuno = 23,616 tris) + scenario piazza = **27,408 triangoli stabili a 60 fps in Full HD 1080p** con opzioni extra attive.

## Next steps (immediate roadmap based on verified hardware runs)
1. **VID-REC (Video Recording)**: Adopt RLE565 codec (codec 4) as the standard recorder on Pi Zero at 15 fps (0.13 - 1.05 MB/s fits SD write mean of 1.82 MB/s with <2 ms encode time). Next slice: audio interleaving.
2. **Graphics Engine Defaults**: Standardize `gpu3d_queue=1` and `gpu3d_merge=1` (32+ pieces, <=150 faces) in production pipelines. Verify `gpu3d_2d=1` default (+6.7% boost, 3569+ sprites vs 2574 on ARM).
3. **Multitasking Stage 3-5 (Web Bench)**: Run `tests/web/pi_bench.lua` on hardware to benchmark async file writes and Range resume.
4. **Single-bm (SYS-8)**: Proceed with adaptive screen modes (720x405, `safe_area()`, `aspect()`, bars).

## Piano di lavoro approvato: 3D High-Throughput Pipeline (50k Triangoli @ 60fps)
Stato esecuzione:
- [x] **Benchmark e ottimizzazione Herocart**: verificati 52.992 tris a 59-60 fps (640x360) e 27.408 tris a 60 fps (1080p) su Pi Zero W.
- [ ] **Semplificazione API Utente (bmlib)**: helper `lib.crowd3d`, `lib.gpu3d_fast`, batch instancing e pose deduplication.
- [ ] **Documentazione Tecnica e Manuali**: aggiornare `manuals/content/{it,en}/api-3d.md`, `api-lib.md`, e `docs/PERF-BUDGET.md`.
- [ ] **Ottimizzazione Editor e Documentazione**: aggiornare `src/script/bm3d.lua` (budget 50k vs 1.2k arm, warning calibrati, static merging) e relativi manuali editor (`editor-mesh.md`, `editor-studio.md`).

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
