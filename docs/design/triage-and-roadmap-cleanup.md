# Triage of failing tests and roadmap cleanup list

Date: 2026-10-22. Base: `origin/bm-core` at 993ae1f7. Documents only. No code was built or changed.
Data comes from `gh run view --log-failed`, `gh pr view`, `gh pr diff` and from reading
`tests/qemu_test.py`, `tests/rgb30/qemu_test.py`, `Makefile` and `src/kernel/config.c`.
Every claim below is from logs or code. Nothing was reproduced locally. Nothing is verified on hardware.

Part 1 feeds stage 0 of the multitasking study (PR 100, `docs/design/multitasking-alternatives.md`,
item (e) of [multitasking](../progress/multitasking.md) `Open`). Part 2 is the checklist for the new rule:
when a PR closes, delete its finished items from `docs/ROADMAP.md`, from `Open work` of
`docs/progress/<area>.md` and from `sync/NEXT.md`.

## 1. Failing tests (stage 0 triage)

Fix owners: PR 92 (`claude/flaky-qemu-tests`, tip f2b67f41) steadies three tests. The stacked branch
`claude/flaky-qemu-tests-2` has no commit of its own yet (same tip f2b67f41 as PR 92). Queue item 1
in `sync/NEXT.md` (branch `sync/protocollo-ottobre`) gives it the two tests `test_code_editor` and `test_usb_mouse`.

| Test | Cause | Evidence | Fix owner | Still open |
|---|---|---|---|---|
| `test_sd_cartridges` | Race in the test. Keys typed before the menu runs, and `a`+Enter, `d`+Enter sent back to back. Not a kernel bug. | CI runs 39259256078 (19 Oct) and 39481911294 (20 Oct): screen dump of `?` only, `FAIL test_sd_cartridges`. PR 92 body, section "Causes". | PR 92 (`wait_menu()`, wait for each cover title). PR 92 already removed it from the `multitasking.md` failing list. | CI proof only after PR 92 merges. PR 92 passed CI twice. Its local counts were 0 failures before and after, so the cure is not proven. The "also on the old kernel" note has no run behind it that I could find. |
| `test_update_from_sd` (RGB30) | Race in the test. It uses fixed sleeps, then one screen dump. The 30 s wait for the signature check can also be short on a slow runner. | `tests/rgb30/qemu_test.py:425`, `:435`, `:440` (`time.sleep(0.5)`, `0.8`, `0.5`) before `screen_all(q.screendump())`; `expect("can be installed", timeout=30)` at `:430`. No failed CI run of it in the last 80 failed runs. | Nobody. PR 92 does not touch `tests/rgb30/qemu_test.py`. | Replace the sleeps by a poll of the screen. Needs an owner. Not reproduced. |
| `test_code_completion` | Race in the test. One screen sample is taken after Tab. The text is the same before and after the key, so a stale frame gives a wrong result. | `AssertionError: what Tab wrote, green` at `tests/qemu_test.py:7166` (`assert pred > 20`). CI runs 39225745946, 39403436819, 39471990281, 39564385970 (19 to 21 Oct). | PR 92 (`see_gone()` polls, green wait 10 s instead of 5 s). | No CI proof before the merge. |
| `test_code_editor` | Race in the test. A key or a prompt arrives later than the fixed delay. | Two shapes. `copy_yes` waits 30 s for `read only: save an editable copy` and gets nothing (`tests/qemu_test.py:275`, run 39228451312). `not on screen: ['Il mio demo.bm', 'pong.bm 1', 'two pages']` after `k("\x1bOS", 0.5)` at `:6794` (run 39689023558, PR 89). Eight more runs on 19 Oct, shard 1/4. | Not in PR 92. Queue item 1 of `sync/NEXT.md` (branch `claude/flaky-qemu-tests-2`, not started). | Whole test. |
| `test_usb_mouse` | Race in the test. The wait condition is too weak. The wait ends when "Open in the SDK" is absent, so it can end on a blank or half-drawn frame. The next assert needs the title `MOUSE_TITLES[1]`. | `assert "Open in the SDK" not in text and MOUSE_TITLES[1] in text` at `tests/qemu_test.py:4071`. The wait before it tests only the absence of one string (`:4068`). CI run 39712033065 (PR 87, 22 Oct, the dump is a blank frame). Runs 39395150503 and 39404685787 (20 Oct) failed with "a mouse icon before the mouse did anything". | Not in PR 92. Queue item 1 of `sync/NEXT.md`. | Add the title to the wait predicate. Whole test. |
| `monitor_line` | Real test bug, already fixed in `bm-core`. The test waited for the line `back to the monitor`, which the `b3d` line never prints. The branches that failed were older than the fix. | Failures 19 and 20 Oct at `test_monitor_line:5155` (`q.expect("back to the monitor", timeout=10)`), runs 39225505000 and 39430508818. Fix 9fe1f325 reached `bm-core` with PR 24 (16 Oct). `git merge-base --is-ancestor 9fe1f325 <sha>` says the failing shas 0615871a (`claude/beta1-c4-diag-run`) and 81e5c8e1 do not contain it. The current test (`tests/qemu_test.py:5266-5273`) has no such wait. | 9fe1f325 (in `bm-core`). PR 92 also adds `wait_menu()` at the start of the test. | Nothing, if the old branches rebase. Not a regression. |
| `test_update` (Pi; not in the task list) | Real regression in PR 87. The new Controls row shifted the key count in Settings. | `AssertionError: want ['Settings > Updates', 'Version']` at `tests/qemu_test.py:3543`. The screen shows `Settings > Screen`. Runs 39688065336 and 39712033065. | PR 87 fix commit (`sync/NEXT.md`: "`test_update` key count after the new Controls row"). | Check CI of PR 87 after the fix. |
| `make test-net`: link failure in `test_config` | Stale note or local environment. I found no missing symbol. | Rule: `Makefile:875-877`, objects `tests/kernel/test_config.c src/kernel/config.c src/lib/printf.c`. Every external function that `config.c` calls has a stub in `tests/kernel/test_config.c` (`hid_*`, `bm_*perf`, `bm_*via_ram`, `audio_*`, `pointer_*`, `fat_*`, `uart_putc`) or is in `printf.c`. The CI log of run 39712033065 shows `cc -O1 ... -o build/host/test_config ...` and then `build/host/test_config`, with no link error. The note was written in c880a334 (11 Oct 16:04). Commit 71292301 (11 Oct 16:07) changed `config.c` and `test_config.c` (the `perf` overlay). | Nobody needed. | Not built here (rule: no `make`). Run `make test-net` once in WSL. If it passes, delete the bullet. |

Not regressions: all rows except `test_update`. Environment: none confirmed. The `host` job of PR 87 and
PR 98 also fails in `test-docs` (Makefile:1186 and 1194). That is a separate item and not in this list.

## 2. Cleanup list after the PRs close

Scope: open PRs 80 to 100. A "row" is an ID in `docs/ROADMAP.md`. The bullets are in
`docs/progress/<area>.md` `Open work`. `sync/NEXT.md` is on branch `origin/sync/protocollo-ottobre`
(it is not on `bm-core`).
DONE = delete the item. PARTIAL = shorten the item and keep what is open. NONE = the PR touches no row.
"Removed by the PR" = the PR diff already deletes or rewrites the item. Do not edit it again.

### Count of ROADMAP rows per PR

| PR | Rows (done / partial) | Rows |
|---|---|---|
| 80 | 0 | none |
| 82 | 0 | none |
| 83 | 0 | none |
| 84 | 0 / 1 | SYS-4 |
| 85 | 0 | none (SYS-3 stays) |
| 86 | 0 | none |
| 87 | 0 | none |
| 88 | 0 / 1 | WEB-1 |
| 89 | 0 / 1 | SYS-2 |
| 90 | 0 / 1 | M43 (row already edited by the PR) |
| 91 | 0 | none |
| 92 | 0 | none (SYS-1 stays) |
| 93 | 0 / 1 | M44 |
| 94 | 0 / 1 | M44 (same row as 93) |
| 95 | 0 / 1 | BETA-5 |
| 96 | 0 / 2 | BETA-7, APP-3 |
| 97 | 0 / 1 | M43 |
| 98 | 0 / 2 | APP-2, M43 |
| 99 | 0 / 1 | M44 |
| 100 | 0 | none (SYS-6 and R49 stay) |

No PR closes a whole ROADMAP row. The partial rows need a text change, not a deletion.

### Checklist per PR

PR 80 (video study)
- [ ] ROADMAP: none. `docs/progress/video.md` is new and has no row in ROADMAP.
- [ ] Open work: none.
- [ ] NEXT: delete the "In progress" clause that says PR 80 waits for the user's merge. Delete branch `claude/video-study` and its worktree after the merge.

PR 82 (video spikes, base `bm-core`, includes PR 80)
- [ ] ROADMAP: none.
- [ ] Open work `video.md`, DONE: "VID-S3, VID-S4: done on the PC (see the spikes sheet)". Delete the bullet. The result stays in `docs/bench/video-spikes-2026-10.md`.
- [ ] Open work `video.md`, PARTIAL: "VID-S1, VID-S2: the Video bench is built. The run on the Pi is open". Keep it. Only the Pi run is open.
- [ ] NEXT: update the "In progress" line (PR 82 conflicts and needs a rebase after PR 80). Merge order item 5 lists 82.

PR 83 (`.bmv` format, VID-FMT slice 1, base `claude/video-spikes`)
- [ ] ROADMAP: none.
- [ ] Open work `video.md`, DONE: "Decoder: `jpeg_decode` allocates a 24-bit picture on each call. The player needs a call that writes RGB565 into a given buffer". `jpeg_decode565` does this. The PR does not edit `video.md`, so delete the bullet.
- [ ] Open work `video.md`, PARTIAL: "VID-FMT: container and player API". Slice 1 is done (`BM-VIDEO.md`, `src/bm/vid.c`, `scripts/mkvid.py`, `make test-vid`). Keep: the Lua API (slice 2) and the memory decision.
- [ ] NEXT: merge order item 5 (81, 82, 83).

PR 84 (audit tools)
- [ ] ROADMAP SYS-4 (`ledger and agent_audit.py with baseline; work order scope checked by script`), PARTIAL. The baseline is in (`agent_audit.py` baseline options, `docs/agents/baseline.txt`). The scope check is open: `tools/sync_perimeter.py` was skipped because sync was retired. Cut the requirement to "work order scope checked by script".
- [ ] Open work: none. `tools.md` has no open bullet for these tools. The PR adds two lines under "How it is today".
- [ ] NEXT: delete the item "PRs waiting for review and the user's merge: 84 (...)". After the merge, delete by name `agy/sync-perimeter-quarantine`, `claude/translate-skill`, `agy/agent-audit-baseline`, `claude/docs-audit-check` (the user's go is needed).

PR 85 (bridge and net tools)
- [ ] ROADMAP SYS-3: no change. The requirement says "tested on a Pi". The PR says `check_bm_net_config.py` was not run in WSL and `monitor.c` was not built. R-NET3 stays.
- [ ] Open work `tools.md` "Pi tests from the PC": no change (R-NET3 only). The PR already rewrote the bridge paragraph. Add the one open point that the PR names: `stack` is not in `LINE_CMDS`.
- [ ] NEXT: delete the PR 85 clause in "PRs waiting for review". After the merge, delete `claude/net-bridge-crc`, `net-bridge-line-allowlist`, `net-bridge-pwfile`, `claude/bridge-rgb30`, `claude/monitor-fb-crc` (go needed). Delete the "rerun `check_bm_net_config.py` in WSL" note when it passes.

PR 86 (games input standard, GAME-INPUT-1)
- [ ] ROADMAP: none. GAME-3 is related (kit), but its requirement ("a test game uses the kit without duplicated code") is not met.
- [ ] Open work `lua.md` GAME-INPUT-1: PARTIAL. Removed by the PR: "document `lib.input` in `API.md`, `API-IT.md`, `GAME-GUIDE.md`, `GUIDA-GIOCHI.md` and `src/ai/kb`". Open: "migrate the existing games one by one" and the open decisions in section 7 of the standard.
- [ ] NEXT: delete from the "In progress" integrate list: `claude/games-input-standard` and `claude/pad-y-cancel` (PAD-Y-1 is in PR 87). Merge order item 2.

PR 87 (controls remap, CONTROLS-1, Y back button; stacked on 86)
- [ ] ROADMAP: none. BETA-6 (consistency, D-numbers) gets new divergences D48 and D49. It is not closed.
- [ ] Open work `lua.md` CONTROLS-1 ("planned, deferred to the next session ..."), DONE. The PR does not delete the bullet. Delete it.
- [ ] Open work `lua.md`: the PR adds one open point (the Cancel row label when `mc` is unset). Keep it.
- [ ] NEXT: delete `claude/controls-remap` from the integrate list. Delete the line "PR 87 got CI fixes (`test_update` key count ...)" when PR 87 is green. For `test_usb_mouse` in the CI notes, see part 1.

PR 88 (web player mock, WEB-MOCK-1)
- [ ] ROADMAP WEB-1 (`bm in the browser ...`), PARTIAL. The mock is a static UI with no kernel code. The first step (the feasibility work order on `src/bm/runtime.c`) and the WebAssembly build are open. Add "UI mock done (PR 88)" and keep the row.
- [ ] Open work `tools.md`: none removed. The PR adds a "How it is today" line.
- [ ] NEXT: delete `claude/web-mock-1` from the integrate list. Merge order item 1.

PR 89 (crumbs host test, live-record guard, `klog_ring_get()`)
- [ ] ROADMAP SYS-2 (`Reliable freeze report and SD logs`), PARTIAL. The requirement "a crash writes a correct `bootprev.txt`" needs the Pi run. Keep the row. BETA-1 depends on it.
- [ ] Open work `system.md` C4 ("the RAM ring stops before the crash ..."): the PR rewrites it with BETA1-C4-RING3. Keep it until the Pi run reads the record. Nothing to delete yet.
- [ ] NEXT: in "In progress", shorten "`claude/beta1-c4-ring2` and `claude/beta1-c4-ring3`: look for useful code or test data, then delete". Ring3 is ported. Keep `ring2`. Keep the CI note "Flaky in CI, not caused by the PRs: `test_code_editor` (PR 89)" until queue item 1 is done.

PR 90 (stackv)
- [ ] ROADMAP M43 (`Sprite stacking (stack, stackv, zstack done, CPU only)`): already updated by the PR (`stackv open` is gone from the row).
- [ ] Open work `graphics.md` "Sprite stacking ... Not done: `stackv`": already updated by the PR.
- [ ] Open work `tools.md` "Sprite stacking, step 3" names `stackv` as open (rewritten by PR 98). Delete the `stackv` clause when 98 and 90 are in.
- [ ] NEXT: merge order item 4. Delete `claude/stackv` after the merge.

PR 91 (QEMU test for `stat(16)` to `stat(18)`)
- [ ] ROADMAP: none. `stat(17)` and `stat(18)` are R40 (NEXT queue item 7).
- [ ] Open work: none (the PR says no Open work item existed).
- [ ] NEXT: item 7 says "R40 is `stat(17)` and `stat(18)`, merged". No change. Merge order item 1.

PR 92 (steady three QEMU tests)
- [ ] ROADMAP SYS-1: no change. Other tests still fail (part 1).
- [ ] Open work `multitasking.md`: already updated by the PR (`test_sd_cartridges` left the failing list). The bullet "Failing tests: RGB30 `test_update_from_sd` flaky, `make test-net` link failure ..." stays. Remove the `make test-net` part after one passing `make test-net` in WSL.
- [ ] Open work `tools.md`: the CI note is already updated by the PR.
- [ ] NEXT: delete the "flaky tests PR 92" line after the merge. Queue item 1 stays (the two other tests).

PR 93 (QPU step 1 rest: `gpu3d_qpu` key, GPU test step, 3D Bench row; base `bm-core`)
- [ ] ROADMAP M44 (`... Open: runtime calls, GPU drawing, 3D Bench profile, Pi test`), PARTIAL. Done on the emulator: the GPU test step and the 3D Bench row (a report line, not a profile column). Remove "3D Bench profile" from the row text. The Pi run stays.
- [ ] Open work `v3d.md` item 5 step 1: already rewritten by the PR. The "Open (step 1)" bullet keeps the Pi checks. Keep the "run the `v3d_init` registers again" and "lock for `v3d_qpu_run`" parts (NEXT queue item 2).
- [ ] NEXT: "Waiting for the user: After PR 93 is merged: run the QPU step on the Pi" stays. Merge order item 3.

PR 94 (QPU step 3 rest: Yharnam fade behind `gpu3d_qpu`; base PR 93)
- [ ] ROADMAP M44, PARTIAL (same row). "Runtime calls" is done for the fade (`bm_fade_accel`, default off). Particles and GPU drawing are open (particles are parked).
- [ ] Open work `v3d.md` step 3 ("Open: the call in the runtime ..."): the PR already shortened it.
- [ ] Open work `graphics.md` "QPU programs outside 3D ... Nothing calls them in the runtime": false for the fade after PR 94. Edit it (PR 94 does not touch `graphics.md`).
- [ ] NEXT: in "Useful commits", delete `claude/gpu3d-fade` a2c1e904 and 4f7fbc3b (the `g16_fade_accel` hook). Keep 07672f3d and b38be94e (particles). Queue item 2 stays.

PR 95 (Stages 3D port, frame budget and quality governor)
- [ ] ROADMAP BETA-5 (`a multi-stage 3D game runs on the GPU within the budget`), PARTIAL. The cart, the host tests, the RGB30 QEMU test and the governor are in. The run on the Pi and the RGB30 is open. Keep the row.
- [ ] Open work `stages3d.md` (new file): the one bullet is the `.b16` run on the RGB30. Keep it. `platforms.md` Open work has no item for it.
- [ ] NEXT: in "Useful commits", delete the line for `claude/stages3d-qemu-test` and `claude/stages3d-budget`. Delete both branches by name after the merge (go needed). Merge order item 4.

PR 96 (manuals refresh)
- [ ] ROADMAP BETA-7 (`Manuals up to date ... python scripts/manuals.py status shows no open item`), PARTIAL. boot-os (en, it) is done. The PR does not say that `manuals.py status` is clean. Check it before the edit.
- [ ] ROADMAP APP-3 (`EN/IT manuals aligned`), PARTIAL for the same reason.
- [ ] Open work `tools.md` "PDF manuals": rewritten by the PR. Keep the rest (pictures to take, the Assistant manual).
- [ ] NEXT: none. Merge order item 1.

PR 97 (stack to mesh, host tool)
- [ ] ROADMAP M43 (`... conversion to mesh`), PARTIAL. Delete "conversion to mesh" from the requirement. Open: the Pi benchmark (Stack bench, PR 81, is merged; the run on the Pi is in NEXT "Waiting for the user").
- [ ] Open work `graphics.md` "Not done: ... the mesh conversion": already updated by the PR.
- [ ] Open work `tools.md`: the PR adds a done line. Keep the open points of the tool (outer faces only, UI button).
- [ ] NEXT: none. Merge order item 1.

PR 98 (bm Pixel Stack page, F4)
- [ ] ROADMAP APP-2 (`3D maps in bm SDK; bm Pixel for stacks (M43 step 3)`), PARTIAL. The Stack page is done. Delete "bm Pixel for stacks (M43 step 3)" from the goal text. The 3D maps part has its own `Open work` bullet in `tools.md` that says "completed". Ask the owner whether the row can close.
- [ ] ROADMAP M43, PARTIAL: step 3 is done (see PR 97 for the rest). `design/suite-pixel.md` is the second link.
- [ ] Open work `tools.md` "Sprite stacking, step 3: a Stack page in bm Pixel": already rewritten by the PR ("try it on the Pi and the RGB30"). Keep the new bullet. It still names `stackv` as open: edit it when PR 90 is in.
- [ ] NEXT: none. Merge order item 4.

PR 99 (3D particles, ARM only)
- [ ] ROADMAP M44 (`particles and Yharnam darkness computed by the GPU`), PARTIAL. The ARM `particles*` and `lib.particles3d` are done. The QPU step for particles is parked (NEXT "Waiting for the user"). Cut "runtime calls" for particles from the row.
- [ ] Open work `v3d.md` step 2 ("Open: the Lua `particles()` call and the `lib.particles` module in bmlib ... and the drawing on the GPU"): partly done by PR 99 (the ARM call and the module `lib.particles3d`). The PR edits `graphics.md` only. Edit `v3d.md`: keep only "the QPU step" and "drawing on the GPU".
- [ ] NEXT: in "Useful commits", delete the line for `claude/api-particles3d` (a2795d0f, ea7e0d2e, 0c543cc6). Delete the branch by name after the merge (go needed). Keep the parked `lib.particles` QPU item. Merge order item 4.

PR 100 (multitasking alternatives, study)
- [ ] ROADMAP SYS-6 and R49: no change. The study does not close an Open item. `multitasking.md` already gets the one-line pointer from the PR.
- [ ] After this document: the "Failing tests" bullet in `multitasking.md` can link to part 1 here and drop the names that the table closes.
- [ ] NEXT: none.

### Items for NEXT.md that no PR in 80 to 100 closes
- "Merge order for the user" (04:25): delete each PR number after its merge. Delete the whole section when it is empty.
- "BLOCKED on the user (23:40) ... PR 81 (Stack bench ...)": PR 81 is merged in `bm-core` (993ae1f7). Delete PR 81 from the line. Delete branch `claude/stack-bench-pi` and worktree `bm33-stackbench` by name after the user's go.
