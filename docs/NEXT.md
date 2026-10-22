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
- Close the work in flight: multitasking stages 3-5, bench for hero, VS and cel, herocart LOD triangles.
- Run the hero benchmark on the Pi. Write the numbers into the sheets.

Friday morning:
- The three tools from the bundle (done).

Friday afternoon:
- API-3: triangle throughput. Decide the default of `gpu3d_queue` from the benchmark numbers.
- API-4: run the Video bench on the Pi Zero W (Dev tab, or the monitor line `:video`) and send the RLE565 lines of the report.

Friday after the Mali device runs (done by the SPV through the RGB30 bridge, one session only on the RGB30):
- Mali alignment with the Pi driver (API-5), stage by stage, each stage tested on the RGB30 through the bridge `--dev rgb30`.

Friday evening:
- Clean up branches and worktrees.
- Read this file, update `docs/ROADMAP.md`, write the summary.

## Waiting for the user (decisions and Pi runs)
- RGB30 runs (the user does them; exact steps in `docs/progress/graphics-mali.md` `Open work`): Dev > GPU test with the variant probe, then 3D Bench with `mali3d=1`.
- Pi runs (the user does them; the test integration `claude/pi-test-integration` of 22 Oct is on the Pi; `herocart.bm` stage 1 is on the SD):
  - Scenes: `b3d tests=map8,sprites,stack,fade,light` (not in the 22 Oct report). `herotris` and `cel` are done (22 Oct, one run each; more runs only if the numbers are needed again).
  - Herocart sweep (Start+A, Start saves `HEROCART.TXT` in `/docs`): GPU on/off; after the fix also VS 2 and Cel (4 bands) / Dither off. bm3D 7.5: with VS 2 compare Cel off and Cel 4 and Outline off / hull (baked) / flag 512 at LOD 0 and 1. The 3D Bench of 22 Oct already gave the numbers (cel +10%, outline +31%, `cel light yes`); this cart run only confirms them.
  - Recorder: `rec=on rec_fps=15 rec_size=320x180`, play Overbit; read the end-of-run lines.
  - Browser `bmweb` (stages 2b-5, built, not verified on hardware): build `make build/carts/bmweb.bm` and copy it to `/bm` on the SD (`./easy_install.sh` copies `dist/`). Join WiFi and allow the network when the cart asks. Open `bm Web` from the menu. Press Y, type the address of a site you may read (https only), press A. Check and note: (1) the status line shows "waiting ... (pace)", then "reading robots.txt", "connecting", "loading"; (2) the time of the TLS stall (the screen may freeze about 100 ms); (3) the page appears; the status line shows the Lua memory in KiB at the right (note it after a page of about 300 KB); (4) right/left move the link focus, B goes back, Start marks a page; (5) X loads the pictures one by one (WebP, PNG, JPEG) and the decode time per picture; (6) a link to a `.glb` or `.zip` opens "Save this file?"; A writes `bm/web/<NAME>.<EXT>` and a `.TXT` note. Do the same on the RGB30 (D49 stays `todo/untested` until then). QEMU has no network, so `make test-web` and `test_bmweb` do not replace this run.
  - Multitasking stages 3-5 (web bench, not a game): build with `python scripts/mkbm.py -o build/webbench.bm --lua tests/web/pi_bench.lua --title "Web bench" --author bm --res 640x360`, copy to `bm/` by hand, press A. Set `URL` at the top of the file to an https address of a file of at least 2 MiB that sends `Accept-Ranges` for the resume part. Read `/docs/WEBBENCH.TXT`: sync call ms, async worst frame ms, save during a queued write, resume 1 and 2 (`resumed` must be above 0). Also from the console log of a report: one TLS handshake for the 3 requests (`menu stalls:` handshake count).
  - `gpu3d_qpu=1` step, `PI_O3_FILES`.
- Decided 22 Oct, to implement (design in `docs/design/single-bm-and-adaptive-screen.md`): old `.b16` = Pi + RGB30; old `.bm` = Pi only; grey not hidden, start after confirm; audio max 8M; pad mouse; bars G1; 720x405 mode; `.bm` for all.

## Queue (development, in order)
1. Remote branches left: `api-particles3d`, `beta1-c4-ring2`, `beta1-c4-ring3`, `gpu3d-fade`, `stages3d-budget`, `stages3d-qemu-test`, `v3d-compact-tex-attrs`, `reports`, `sync-handshake`. The Mali work is in `bm-core` (sheet `docs/progress/graphics-mali.md`, `Open work`: the device runs).
2. Old work in the archive: 23 old unmerged branches are in `C:\Users\Utente\Documents\GitHub\bm-archive\branches-2026-10-22.bundle` and are deleted from the repository. Port their content on demand from the bundle: budget cart and `perf_table.py`, RGB30 cover job D10, xmenu close game, RGB30 QEMU freeze and delete tests, bench-vs-etc1 VSE column, 3D budget table, PROP-1 and PROP-2 docs, gfx-backends rule.
4. Single-bm (SYS-8), next stage: the menu and the Market read the compat header (bytes 120..127) and show a not-compatible cart grey, start it after a confirm; wire `docs/spec/compat-policy.toml`. Then the screen modes, the input chips, the end of `.b16`. List: `docs/progress/b16.md`, `## Open work`.
5. VID-REC next slices: real 8-bit palette, audio (the RLE565 encoder is done; the Pi numbers are the API-4 item above).
6. Web player (WEB-1): stages 1 and 2 are done (emsdk is in `~/emsdk` in WSL). Next: stage 3, the browser runner (`-sASYNCIFY`, canvas, keyboard and pad). Plan in `docs/design/bm-web-player.md`.
7. Triangle throughput: stage 1, make `gpu3d_queue=1` the default in a code change (the benchmark of 22 Oct gave x1.75 on `herotris`; see `docs/design/triangle-throughput.md` section 7). `gpu3d_attr` stays off (decided 22 Oct). The V3D async proposal is decided "skip" (12 Oct, `docs/progress/v3d-notes.md`: interrupts, triple buffering, `v3d_async.c`). `queue=2` and `gpu3d_later` replaced it. Chain jobs (CT0/CT1) only if a measurement shows the GPU idle between jobs.
8. Evaluate the security findings of `docs/OS-CAPABILITIES.md`: `lua_load(..., NULL)` at `runtime.c:9438` may accept bytecode; default net password is 6 digits from `srand(timer_ticks())` (`netcon.c:183`), plaintext on TCP 3334; carts can `ls` the SD and `cart_read` any `.bm` without a prompt; FAT dates fixed at 2026-01-01 (`fat.c:682`); docs disagree (`docs/progress/b16.md:16` says 4 MiB heap, code 64 MiB; RGB30 menu framebuffer 360x360).
9. QPU step 4: re-run the `v3d_init` register block after a QPU reset; lock for `v3d_qpu_run` (needs a Pi run).
10. R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain.
11. Mali alignment with the Pi driver (API-5), stage by stage, each stage tested on the RGB30 through the bridge `--dev rgb30`.

## Open issues
- The Pi froze once on `b3d tests=herotris` with all profiles in one line (8 profiles in one run). The user power-cycled the Pi. The same profiles run one at a time did not freeze. Cause unknown, no fix. Command: `b3d tests=herotris` (no profile named, so all 8 profiles in one line). To reproduce, run it again and note the profile that is running at the freeze.
- `qemu_test.py -k herocart` is red on bm-core 4c65205d (res=1920x1080 assert or timeout at `,cel2,`); same without the LOD change; needs its own fix.

## Never delete
`bm-core`, `reports`, `sync-handshake`, `claude/beta1-c4-ring2`, `claude/beta1-c4-ring3`, and the graphics branches below until their commits are ported or the user says go.

## Useful commits on graphics branches (keep the branch until the commit is ported)
- `claude/gpu3d-fade`: a2c1e904 and 4f7fbc3b (fade through the `g16_fade_accel` hook).
- `claude/api-particles3d`: a2795d0f (runtime 3D particles), ea7e0d2e and 0c543cc6 (API docs).
- `claude/v3d-compact-tex-attrs` (b8f01430, 1ced7d02): compact attributes (`gpu3d_attr`, stays off).
- `v3d-async-proposal` (2c383089, 69ef8453, 23b9c7bf): read before any V3D async work.
- `claude/stages3d-qemu-test` (eddfd2e6, 628739b7, 26671568, c0ca055a) and `claude/stages3d-budget` (342a8018, bb9d2a12): Stages 3D carts and governor.

## Done on 22 Oct (do not redo)
Herocart chain, PAD-Y-1 (Y is the only back button on the RGB30), the `bm-runner` agent, the `Integrate` rule in `CLAUDE.md`, branch cleanup (old branches are in the bundles in `C:/Users/Utente/Documents/GitHub/bm-archive`), Market: `apps/` move done 22 Oct, the Pi numbers of 22 Oct in the area sheets, decisions on `gpu3d_attr` (off), `lib.particles` on QPU (parked) and `test_usb_mouse` (strict assertion).
PRs 80-120 closed (stack bench, audit and bridge tools, games input, controls remap, web mock, crumbs, stackv, QPU steps, Stages 3D, manuals, stack2mesh, Pixel Stack page, particles3d, multitasking stages 1-2 and alternatives, single-bm design with decisions, triage doc, browser design and stage 1, `map()` tile path and `PI_O3_FILES`, triangle throughput doc, hero cart doc, 2D bench scenes, OS capabilities doc, WebP decoder, Market with the 7 editors as apps). 33 merged branches deleted. 11 old `claude/*` evidence branches deleted. 7 editors published in `bm-market` (village removed).
