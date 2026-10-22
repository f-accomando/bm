# NEXT

State of 22 Oct. Goal now: clean the repository step by step and reach a stable point. Waiting for the user: the Pi runs below.

Rules for this list:
- Do the first task in "Queue". When it is done, delete it from this file. Then read this file again and start the next task.
- No new PRs (user, 22 Oct). One branch `claude/<name>` from an up-to-date `bm-core`, one subagent (`bm-runner`), pushed, no PR. The SPV brings the branch into `bm-core` after build and selective tests are green (`git merge-tree` check, then ff-only), then deletes the branch (remote and local, by name) and its worktree.
- Run only one task that uses `make` or WSL at a time. Before a `make`, check with `ps` in WSL that no other make/qemu runs. Never `pkill` broadly.
- A task that needs the Pi: prepare it, put the exact steps in "Waiting for the user", go on.
- When a task closes, remove the finished item from `docs/ROADMAP.md`, `## Open work` of the area sheet, and this file.

## Waiting for the user (decisions and Pi runs)
- Pi runs (the user does them; the test integration `claude/pi-test-integration` of 22 Oct is on the Pi; `herocart.bm` stage 1 is on the SD):
  - Scenes: `b3d tests=map8,sprites,stack,fade,light,herotris` (3 runs for herotris). Not in the 22 Oct report.
  - Herocart sweep (Start+A, Start saves `HEROCART.TXT` in `/docs`): GPU on/off; after the fix also VS 2 and Cel (4 bands) / Dither off.
  - Recorder: `rec=on rec_fps=15 rec_size=320x180`, play Overbit; read the end-of-run lines.
  - Browser `bmweb` (stages 2b-5, built, not verified on hardware): build `make build/carts/bmweb.bm` and copy it to `/bm` on the SD (`./easy_install.sh` copies `dist/`). Join WiFi and allow the network when the cart asks. Open `bm Web` from the menu. Press Y, type the address of a site you may read (https only), press A. Check and note: (1) the status line shows "waiting ... (pace)", then "reading robots.txt", "connecting", "loading"; (2) the time of the TLS stall (the screen may freeze about 100 ms); (3) the page appears; the status line shows the Lua memory in KiB at the right (note it after a page of about 300 KB); (4) right/left move the link focus, B goes back, Start marks a page; (5) X loads the pictures one by one (WebP, PNG, JPEG) and the decode time per picture; (6) a link to a `.glb` or `.zip` opens "Save this file?"; A writes `bm/web/<NAME>.<EXT>` and a `.TXT` note. Do the same on the RGB30 (D49 stays `todo/untested` until then). QEMU has no network, so `make test-web` and `test_bmweb` do not replace this run.
  - Multitasking numbers, `gpu3d_qpu=1` step, `PI_O3_FILES`.
- Decided 22 Oct, to implement (design in `docs/design/single-bm-and-adaptive-screen.md`): old `.b16` = Pi + RGB30; old `.bm` = Pi only; grey not hidden, start after confirm; audio max 8M; pad mouse; bars G1; 720x405 mode; `.bm` for all.

## Queue (development, in order)
2. Remote branches left (all kept; see "Never delete" and the list below): `api-particles3d`, `b3d-mali`, `beta1-c4-ring2`, `beta1-c4-ring3`, `docs-3d-beta5`, `gpu3d-fade`, `integrazione-ottobre` (holds the Mali W15c commits 1b5d866a, 2df74a77, 67447ba8), `mali-matrix`, `mali-triangle-batch`, `mali-variant-probe`, `spec-3d-beta5`, `stages3d-budget`, `stages3d-qemu-test`, `v3d-compact-tex-attrs`, `reports`, `sync-handshake`. Port their commits to `bm-core` when the work needs them.
3. Old work in the archive: 23 old unmerged branches are in `C:\Users\Utente\Documents\GitHub\bm-archive\branches-2026-10-22.bundle` and are deleted from the repository. Port their content on demand from the bundle: `carts_layout.py`, `sdlog_scan.py`, `public_export.py`, budget cart and `perf_table.py`, RGB30 cover job D10, xmenu close game, RGB30 QEMU freeze and delete tests, bench-vs-etc1 VSE column, 3D budget table, PROP-1 and PROP-2 docs, gfx-backends rule.
5. Multitasking stages 3 (SD writer lock; `file_write` is synchronous and stops the frame until then), 4 (keep-alive), 5 (Range resume `.part`).
6. Cel shader stage 3 (V3D vertex programs) and stage 4 (outline by inverted hull); Mali uses the ARM path.
7. Dev kit for weight and performance (user, 22 Oct): weight report of a `.bme`/`.bm` (bytes per section and asset, biggest first) and a performance analysis during development (frame time, draw and 3D cost, memory, SD reads), reusing the dev kit and bench tools. Compat policy table per kernel (show, grey, hide) as editable data.
8. Single-bm implementation by the stages of the design: remove the 8 MiB limits (and palette/audio limits), per-system input chips, aspect modes, compat header in bytes 120..127 (change the `memset` in `project.c:143,178,225`).
9. VID-REC next slices: MJPEG or RLE encoder for 30 fps (SD write is 2.50 MB/s, raw is a NO GO), real 8-bit palette, audio.
10. Triangle throughput: stage 1 `gpu3d_queue=1` default after the user's Pi runs. `gpu3d_attr` stays off (decided 22 Oct). The V3D async proposal is decided "skip" (12 Oct, `docs/progress/v3d-notes.md`: interrupts, triple buffering, `v3d_async.c`). `queue=2` and `gpu3d_later` replaced it. Chain jobs (CT0/CT1) only if a measurement shows the GPU idle between jobs.
11. Evaluate the security findings of `docs/OS-CAPABILITIES.md`: `lua_load(..., NULL)` at `runtime.c:9438` may accept bytecode; default net password is 6 digits from `srand(timer_ticks())` (`netcon.c:183`), plaintext on TCP 3334; carts can `ls` the SD and `cart_read` any `.bm` without a prompt; FAT dates fixed at 2026-01-01 (`fat.c:682`); docs disagree (`docs/progress/b16.md:16` says 4 MiB heap, code 64 MiB; RGB30 menu framebuffer 360x360).
12. QPU step 4: re-run the `v3d_init` register block after a QPU reset; lock for `v3d_qpu_run` (needs a Pi run).
13. R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain.

## Never delete
`bm-core`, `reports`, `sync-handshake`, `claude/mali-variant-probe`, `claude/integrazione-ottobre`, `claude/beta1-c4-ring2`, `claude/beta1-c4-ring3`, and the graphics branches below until their commits are ported or the user says go. Delete a branch only by its explicit name.

## Useful commits on graphics branches (keep the branch until the commit is ported)
- `claude/gpu3d-fade`: a2c1e904 and 4f7fbc3b (fade through the `g16_fade_accel` hook).
- `claude/api-particles3d`: a2795d0f (runtime 3D particles), ea7e0d2e and 0c543cc6 (API docs).
- Mali, parked until the Mali alignment: `claude/mali-matrix` (62e46d04, 4235bfde, c6a57ec8 async frames, 515c3b40, 276c2a4c, 1b5d866a, 2df74a77, 44cf34df), `claude/b3d-mali`, `claude/mali-triangle-batch`, and the Mali W15c commits on `claude/integrazione-ottobre`. The triangle fails with `JS_STATUS 58`; `claude/mali-variant-probe` (e64e3028) waits for a run on the device.
- `claude/v3d-compact-tex-attrs` (b8f01430, 1ced7d02): compact attributes (`gpu3d_attr`, stays off).
- `claude/stages3d-qemu-test` (eddfd2e6, 628739b7, 26671568, c0ca055a) and `claude/stages3d-budget` (342a8018, bb9d2a12): Stages 3D carts and governor.
- `claude/docs-3d-beta5` and `claude/spec-3d-beta5`: 3D docs and spec of the beta 5 line.

## Done on 22 Oct (do not redo)
Herocart chain, PAD-Y-1 (Y is the only back button on the RGB30), the `bm-runner` agent, the `Integrate` rule in `CLAUDE.md`, branch cleanup (old branches are in the bundles in `C:/Users/Utente/Documents/GitHub/bm-archive`), Market: `apps/` move done 22 Oct, the Pi numbers of 22 Oct in the area sheets, decisions on `gpu3d_attr` (off), `lib.particles` on QPU (parked) and `test_usb_mouse` (strict assertion).
PRs 80-120 closed (stack bench, audit and bridge tools, games input, controls remap, web mock, crumbs, stackv, QPU steps, Stages 3D, manuals, stack2mesh, Pixel Stack page, particles3d, multitasking stages 1-2 and alternatives, single-bm design with decisions, triage doc, browser design and stage 1, `map()` tile path and `PI_O3_FILES`, triangle throughput doc, hero cart doc, 2D bench scenes, OS capabilities doc, WebP decoder, Market with the 7 editors as apps). 33 merged branches deleted. 11 old `claude/*` evidence branches deleted. 7 editors published in `bm-market` (village removed).
