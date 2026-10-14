# Multi-agent sync: what worked, what hurt, what to change

Notes from the Claude cloud operator (CLD) on the 3D video series, 2026-10-14. Purpose: improve the workflow for the next multi-agent job. Facts come from this run; the numbers are measured, the proposals are not yet tried.

## How the sync worked

- `SYNC.md` (written by SPV only) holds contracts, work orders, the status table and decisions. Each other operator writes only its own file in `sync/` (`sync/CLD.md`, `sync/AGY.md`) and pushes it straight to `bm-core`. Code stays on work branches.
- A timer wakes each operator every 20-30 minutes: `git fetch`, read `SYNC.md` and `sync/`, act, update the own file.
- The status vocabulary (`DONE`, `WORKING {task}`, `WAITING {who}`, `IDLE`) made the table quick to read.

## What worked

- One writer per file: no merge conflicts after the rule of 2026-10-14. Before it, shared `SYNC.md` rows conflicted at almost every merge.
- A dated "Ultimo aggiornamento" line in Rome time let SPV tell a quiet operator from a dead one.
- Requests written as "what I need / what I tried / what I measured" got precise answers (the lamp-slot cause was found within one round).
- Cheap probes before expensive runs: a 50 s QEMU take (19 min) found the real cause that a 68 min take had hidden.

## What hurt

1. **Latency of a round trip.** A question to SPV costs one timer period each way (up to 60 min). The video 4 crash took four rounds; most of the elapsed time was waiting, not working.
2. **Stale wake-ups.** Timers fired for runs that had already finished and re-asked for work done. A wake-up prompt that names a state ("the take is running") goes wrong when the state changes; it should say "check the state, then act".
3. **Lost timers.** After a long foreground wait the next timer was not re-armed once; the operator was silent for a period.
4. **Long jobs and the container.** The cloud container is recycled after about 30 minutes without activity and kills background jobs, so long takes must be waited on in the foreground in blocks of under 10 minutes. The operator cannot do anything else in the meantime unless a subagent waits for it.
5. **Hidden state in build outputs.** The kernel embeds the editor, so a `build/kernel.img` built before an editor fix silently contains the old editor. It cost two long takes. Nothing in the sync said which commit a binary came from.
6. **Drafts recorded before validation.** The first video 4 draft was recorded for 70 minutes before SPV had tried its keys in `bmhost`.
7. **Push races on `bm-core`.** A push of `sync/CLD.md` was rejected whenever another operator pushed first; each retry meant a new worktree and a rewrite of the file.
8. **Reports mixing operators.** Replies that summarised everyone's state were wrong when another operator had changed theirs. The rule "report only your own state" removed that.

## Proposals

- **Stamp provenance**: every deliverable and every request carries the commit it was made from (`feat/x @ abc1234`). For binaries, a `strings`/version check in the run script: `rec_qemu.py` should refuse to record when `build/kernel.img` is older than `carts/editor/` (or print a warning with both dates).
- **Probe first**: make a short take the default step before a long one (`--limit-s` exists; a `--probe` preset with a minimal input would remove the manual setup).
- **Always capture the kernel serial log** during a take (done: `out/qemu/serial.log`) and attach the last 15 lines to any failure report.
- **Wake-up prompts without state**: "fetch, read the files, decide" instead of "the run is in progress". Re-arm the timer as the first action of every wake-up, before any long job.
- **Retry helper for `bm-core` pushes**: a small script that fetches, rewrites only the own file in a fresh worktree and pushes with a bounded retry. (Not yet written; a pushing script was refused by the permission classifier, so it needs the user's approval.)
- **Delegate waiting and checking to `bm-ops`**: waits, `verify.py`, copying deliverables and the sync header. Decisions, commits and requests stay with the operator.
- **Estimate before starting**: write the expected duration in the own file when a job starts (the QEMU takes run about 15-19 times real time under `-icount`; video 4 took 68 min for 200 s of video).
- **Validate scripts before recording**: SPV marks a script "READY" only after a `bmhost` run and `verify.py` pass; CLD records only from READY. This was the rule in the end; it should be the rule from the start.

## Open questions

- Should operators be allowed to message each other directly for urgent blockers, instead of going through SPV and a timer?
- Is the 30-minute timer right while a 70-minute job runs? A single "job started, expected end HH:MM" line may replace several empty check-ins.
