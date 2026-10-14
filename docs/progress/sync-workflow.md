# Working with several operators through `SYNC.md`: what we learned

> Written by SPV (Claude local, the supervisor) on 2026-10-14, in the middle of the Tropic Trail video series (8 videos, three operators: SPV, CLD = Claude cloud, AGY = Antigravity, plus the owner). It is raw material for improving the workflow; the final report of the series will redo it with the full numbers. Facts below come from `SYNC.md`, `sync/*.md`, the git log and this session.

## How it worked

`SYNC.md` (written only by SPV) holds the contracts, the work orders, a status table (§1b) and a log (§5) of one row per message. Each other operator writes only its own file (`sync/CLD.md`, `sync/AGY.md`) and pushes to `bm-core`; the code stays on work branches. SPV polls every 15 minutes (a cron prompt), a small subagent reads the two files and tells what changed. Since the afternoon: `sync/operators.toml`, a status block at the top of each file written by `tools/sync_hdr.py`, a `READY video N` row to release a dependent operator.

## What worked

- **One writer per file.** No merge conflict in `sync/` in two days, with three machines pushing to the same branch. Keep it.
- **Short, concrete work orders** with a verification command. AGY closed steps 3–6 of AGY-09 (`main.lua`, two pages, the `SDK_B16_RES` build variable) in about 50 minutes (11:52 → 12:45) once it was told to go on without waiting for approval; AGY-08 (a 348-line recorder draft) was delivered the same way, but its draft did not work (empty map) and was rewritten by SPV.
- **`READY …` rows** (what is ready, which commit, what to check) gave CLD an unambiguous start signal; `verify.py` per video gave an objective "done" for the recordings.
- **Explicit questions get answers.** The heartbeat request was answered by AGY within minutes (`Heartbeat:` line); CLD answered the crash question with a measurement (`strings build/kernel.img | grep -c map3d_build`) that disproved SPV's hypothesis.
- **The log as an archive** made it possible to reconstruct what happened (timings for the final report) without asking anyone.
- **Probes before long runs** (SPV's `proto.py`, CLD's `--limit-s`) found more problems per minute than the recordings: the 3D floor missing (`map3d` reads only the first tile of each 2×2 block), the `lamp3d(0)` crash of the editor preview, the "editable copy" dialog that blocks `bmhost`.

## What did not work

1. **A message is only seen when its reader polls.** CLD recorded the obsolete draft of video 4 (70 minutes) after the stop row had been published (09:40; recording ended 10:33) and recorded the "definitive" video 4 (another 70 minutes) whose editor crashed at the 3D preview. Neither run could be interrupted: CLD had no timer, the container kills background jobs after ~30 minutes of inactivity, and the error shows only when the run ends. Cost: about 2 h 20 min of the critical path (2 × 70 minutes).
2. **Silence is ambiguous.** CLD: 73 minutes between two updates during a recording; AGY: 64 minutes with two steps assigned and an "active subagent" declared. Both looked like IDLE and neither was a failure of the operator in the usual sense. The 60-minute rule cannot tell "working" from "stuck".
3. **Two sources of truth.** The status table and the operator files disagreed several times (AGY `FREE` in its file, `WORKING` in the table; the table stale for hours). SPV also wrote *predicted* states for CLD and AGY in its replies ("WORKING", "WAITING") that no file confirmed. The owner caught it; the rule now is to report only what a file says, with its time.
4. **Times without a zone.** "10:16" read by a subagent when the clock said 10:31; `updated` lines written by hand in free text. Comparing "minutes since the last update" went wrong more than once.
5. **Steps that wait for the supervisor.** Each step of AGY-09 ended with "WAITING SPV" and a review row. SPV's reviews took 5–10 minutes, but SPV also polls only every 15, and AGY itself took 70 minutes (10:57 → 12:10) to pick up the step 3 row. One row authorising steps 3–5 in sequence, with a single review at the end, removed one wait per step. It should be the default for steps that do not depend on a decision.
6. **Stand-in files cause rework.** The first versions of videos 2 and 3 were calibrated on a stand-in Pico; the real files (CLD's) differed in size and position, and `verify.py` of both had to be rewritten. Anything one operator needs from another should be a real file, or a probe against a real one, before it is scripted.
7. **Rows are long.** Several rows of `SYNC.md` are 700–1500 characters of commands and conditions. Operators skim them, and the ones who answered did so to the first paragraph only. A row should carry the decision and a link; the procedure lives in a doc.
8. **Git friction for the writer.** SPV works on a feature branch and must switch branch (stash, checkout, pull, commit, push, back) to write one row; two pushes were rejected because another operator pushed in between. This is what pushed a subagent to use a separate worktree.
9. **Wrong hypotheses sent as instructions.** SPV told CLD "your kernel has no `map3d`" before verifying; CLD measured the opposite. The row was sent in 5 minutes and the check took CLD 20 minutes of a 70-minute window. A hypothesis should be labelled as one, and the check that would settle it written first.

## Proposals

| # | Change | Why | Status |
|---|---|---|---|
| 1 | A status block with ISO 8601 times *with zone* at the top of each `sync/<ID>.md`, written by `tools/sync_hdr.py`, and §1b **generated** from the blocks | one source of truth, no manual clock | done 2026-10-14 (`sync/operators.toml`, `tools/sync_hdr.py`); AGY already uses it, CLD not yet |
| 2 | Before any operation longer than 15 minutes, write the expected end time in the block (`eta:`); silence before it is not IDLE | the 60-minute rule alone cannot tell a long recording from a stuck one | asked of CLD and AGY (row 10:55) |
| 3 | A **read receipt**: `ack: <row time>` in the block (the last row of `SYNC.md` read) | SPV can see that a stop or a start was *read*, not only published; would have saved the 70 minutes of the obsolete recording | to do |
| 4 | **Short probe before every long run**, mandatory in the work order (a 20–50 s run that exercises the risky key or call and a verification) | the only cheap way to find errors that show up at the end of a 70-minute recording | in use at SPV; CLD asked to add it to `rec_qemu` |
| 5 | Authorise a **sequence** of steps in one row, with one review at the end, whenever the steps are independent of a decision | removes one wait per step | in use since 11:52 |
| 6 | Keep rows to ≤ 600 characters: decision, commit, check; procedure in `docs/design/…` or the work order | rows are read, not skimmed | to do |
| 7 | A rule for *predictions*: SPV reports an operator's state only as read from its file, with the time of the file; hypotheses are labelled | removes the false-IDLE and false-WORKING reports | in use |
| 8 | Capture the serial log of a kernel run by default in the recorder (`qemurec`), so a crash has a message | the crash of 131 s was a mystery for more than an hour after the recording ended | asked of CLD |
| 9 | The owner's signal: when an operator is blocked and silent, a message in its session is the only channel; make this explicit in `SYNC.md` | CLD (no timer) and AGY (idle) answered quickly once the owner opened their session | to write |
| 10 | Write rows from a **separate worktree** of `bm-core` (a subagent does it already) | no branch switching, no rejected pushes | partly |

## Numbers for the final report (so far, to be refined)

- Recording speed: CLD measures 15–19× real time for the 3D editors in QEMU `raspi0` (video 4: 200 s of video = 68 minutes); (the 2D videos were not measured).
- Videos 1, 2, 3, 5 recorded and verified; video 4 recorded twice in QEMU without a usable result (about 140 minutes), but ready in `bmhost`; video 6 ready (record + verify + docs); 7 and 8 not started at 13:50.
- Operators' updates: AGY mostly within 15–25 minutes once on the block format, with one gap of 64 minutes; CLD gaps of 30–73 minutes during recordings.
- Short probes by SPV found three defects in the same session (the 3D floor missing, the `lamp3d` slot crash of the editor preview, the editable-copy dialog that blocks `bmhost`); each would have shown only at the end of a 70-minute QEMU run.

See also [`design/agent-workflow.md`](../design/agent-workflow.md): draft Work Orders for a reviewer agent, a review script, a single barriers section and evidence-based confidence (2026-10-14).
