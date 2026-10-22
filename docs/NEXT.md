# NEXT

State of 22 Oct. `bm-core` is at f881f351. PAD-Y-1, the `bm-runner` agent, the herocart chain and the `Integrate` rule in `CLAUDE.md` are done. Waiting for the user: send the new `build/herocart.bm` (in `bm33-pitest`) to the Pi. Goal now: clean the repository step by step and reach a stable point.

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
  - Browser stages 2 and 5 (after `bmweb` stage 2b), multitasking numbers, `gpu3d_qpu=1` step, `PI_O3_FILES`.
- Numbers from the 22 Oct reports (branch `reports`, Pi Zero W): Stack bench 128 stacks x 8 slices = 18270 us, about 58k slices/s at most; Video bench SD write 2.50 MB/s mean, worst 603 ms = NO GO for raw 15 fps; read 5.14 MB/s; JPEG decode 320x180 53 ms, 480x270 116 ms; GPU test 16/16 ok; 3D Bench score 3382 (GPU bm3d 2.1 = 950, ARM = 169). To write into `docs/progress/v3d.md` item 8 and `docs/progress/video.md` (branch, no PR).
- Decide: `gpu3d_attr` default (proposal off); the loosened cover 5-or-6 assertion in `test_usb_mouse` (proposal: keep strict, shorten the key press; PR 103 merged with the loose form, check); `lib.particles` on QPU stays parked (reopen as a C pool only if the Pi shows QPU faster than ARM).
- Delete with the user's go, by name: worktree `bm33-ciapi` with branch `claude/ci-api-lint`; worktree `bm33-qpupart` with branch `claude/qpu-particles-lib`; `agy/beta6-triage` (3 commits, `docs/spec/beta6-triage.md` not in core), `claude/bm-reviewer-agent` (2 commits not in core), `v3d-async-proposal`.
- Decided 22 Oct, to implement (design in `docs/design/single-bm-and-adaptive-screen.md`): old `.b16` = Pi + RGB30; old `.bm` = Pi only; grey not hidden, start after confirm; audio max 8M; pad mouse; bars G1; 720x405 mode; `.bm` for all.
- Publishing to the external repo `bm-market`: the 7 editors are on `main` (commit 3add43a, in `games/`). Later move the 9 apps to `apps/` (`git mv games/<id> apps/<id>` for editor code pixel studio animator mesh sound typing write) and push the new workflow and README, only after the user's go and after `market-apps-folder` is in `bm-core`. "Send to Market" (`src/net/github.c`) still opens PRs under `games/`.

## Queue (development, in order)
2. Remote branches left: `api-particles3d`, `b3d-mali`, `beta1-c4-ring2`, `beta1-c4-ring3`, `docs-3d-beta5`, `gpu3d-fade`, `integrazione-ottobre` (holds the Mali W15c commits 1b5d866a, 2df74a77, 67447ba8), `mali-matrix`, `mali-triangle-batch`, `mali-variant-probe`, `spec-3d-beta5`, `stages3d-budget`, `stages3d-qemu-test`, `v3d-compact-tex-attrs`, `reports`, `sync-handshake`. The branches `controls-remap` and `pad-y-cancel` are deleted. Their Mali work is in `integrazione-ottobre`.
3. Old work in the archive: 23 old unmerged branches are in `C:\Users\Utente\Documents\GitHub\bm-archive\branches-2026-10-22.bundle` and are deleted from the repository. Port their content on demand from the bundle: `carts_layout.py`, `sdlog_scan.py`, `public_export.py`, budget cart and `perf_table.py`, RGB30 cover job D10, xmenu close game, RGB30 QEMU freeze and delete tests, bench-vs-etc1 VSE column, 3D budget table, PROP-1 and PROP-2 docs, gfx-backends rule.
4. `bmweb` stages 2b-5 as a `.bme` project (generic browser, https only, human pace, no site name in the cart; API `web_get`/`image_load`/`file_write`). Fix `docs/design/web-browser-cart.md`: the download folder is `bm/web` (8.3 names), not `bm/downloads`. D49 (web_get on RGB30) is `todo/untested`.
5. Multitasking stages 3 (SD writer lock; `file_write` is synchronous and stops the frame until then), 4 (keep-alive), 5 (Range resume `.part`).
6. Cel shader stage 3 (V3D vertex programs) and stage 4 (outline by inverted hull); Mali uses the ARM path.
7. Dev kit for weight and performance (user, 22 Oct): weight report of a `.bme`/`.bm` (bytes per section and asset, biggest first) and a performance analysis during development (frame time, draw and 3D cost, memory, SD reads), reusing the dev kit and bench tools. Compat policy table per kernel (show, grey, hide) as editable data.
8. Single-bm implementation by the stages of the design: remove the 8 MiB limits (and palette/audio limits), per-system input chips, aspect modes, compat header in bytes 120..127 (change the `memset` in `project.c:143,178,225`).
9. VID-REC next slices: MJPEG or RLE encoder for 30 fps (SD write is 2.50 MB/s, raw is a NO GO), real 8-bit palette, audio.
10. Triangle throughput: stage 1 `gpu3d_queue=1` default and stage 2 `gpu3d_attr` only after the user's Pi runs. V3D async proposal (`v3d-async-proposal`, 2c383089): decided "skip" on 12 Oct (`docs/progress/v3d-notes.md`: interrupts, triple buffering, `v3d_async.c`). `queue=2` and `gpu3d_later` replaced it. Chain jobs (CT0/CT1) only if a measurement shows the GPU idle between jobs.
11. Evaluate the security findings of `docs/OS-CAPABILITIES.md`: `lua_load(..., NULL)` at `runtime.c:9438` may accept bytecode; default net password is 6 digits from `srand(timer_ticks())` (`netcon.c:183`), plaintext on TCP 3334; carts can `ls` the SD and `cart_read` any `.bm` without a prompt; FAT dates fixed at 2026-01-01 (`fat.c:682`); docs disagree (`docs/progress/b16.md:16` says 4 MiB heap, code 64 MiB; RGB30 menu framebuffer 360x360).
12. QPU step 4: re-run the `v3d_init` register block after a QPU reset; lock for `v3d_qpu_run` (needs a Pi run).
13. R41 and R42 only if the R40 numbers (`stat(17)`, `stat(18)`) show a gain.
14. Docs: `docs/progress/tools.md` is 133 lines (audit limit about 120); the label "Default (Y)" in `docs/progress/lua.md`.

## Never delete
`bm-core`, `reports`, `sync-handshake`, `claude/mali-variant-probe`, `claude/integrazione-ottobre`, `claude/beta1-c4-ring2`, `claude/beta1-c4-ring3`, and the graphics branches below until their commits are ported or the user says go.

## Useful commits on graphics branches (keep the branch until the commit is ported)
- `claude/gpu3d-fade`: a2c1e904 and 4f7fbc3b (fade through the `g16_fade_accel` hook).
- `claude/api-particles3d`: a2795d0f (runtime 3D particles), ea7e0d2e and 0c543cc6 (API docs).
- Mali, parked until the Mali alignment: `claude/mali-matrix` (62e46d04, 4235bfde, c6a57ec8 async frames, 515c3b40, 276c2a4c, 1b5d866a, 2df74a77, 44cf34df), `claude/b3d-mali`, `claude/mali-triangle-batch`. The triangle fails with `JS_STATUS 58`; `claude/mali-variant-probe` (e64e3028) waits for a run on the device.
- `claude/v3d-compact-tex-attrs` (b8f01430, 1ced7d02): for the `gpu3d_attr` decision.
- `v3d-async-proposal` (2c383089, 69ef8453, 23b9c7bf): read before any V3D async work.
- `claude/stages3d-qemu-test` (eddfd2e6, 628739b7, 26671568, c0ca055a) and `claude/stages3d-budget` (342a8018, bb9d2a12): Stages 3D carts and governor.
- Low value: `claude/bench-vs-etc1` (72710c09), `claude/perf-3d-budget-table` (e0afc683), `claude/v3d-closeout` (4123405b, f15ff570).

## Done on 22 Oct (do not redo)
PRs 80-120 closed (stack bench, audit and bridge tools, games input, controls remap, web mock, crumbs, stackv, QPU steps, Stages 3D, manuals, stack2mesh, Pixel Stack page, particles3d, multitasking stages 1-2 and alternatives, single-bm design with decisions, triage doc, browser design and stage 1, `map()` tile path and `PI_O3_FILES`, triangle throughput doc, hero cart doc, 2D bench scenes, OS capabilities doc, WebP decoder, Market with the 7 editors as apps). 33 merged branches deleted. 11 old `claude/*` evidence branches deleted. 7 editors published in `bm-market` (village removed).
