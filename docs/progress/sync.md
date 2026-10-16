# Sync & Multi-Agent Coordination

Architecture, state lifecycle, and operational findings from the multi-agent development workflow
connecting several operators (in the first project: Antigravity AGY, Claude Code supervisor SPV on Windows with WSL, Claude cloud
recorder CLD in QEMU, later a fourth on another Windows machine) through one dedicated **sync branch** per project.

`SYNC.md` at the repo root is the **reusable template** for a new project (copy it, fill the placeholders, empty the log);
the full record of the first project is in [`docs/archive/sync-tropic-trail.md`](../archive/sync-tropic-trail.md).

## How it is today

The coordination workflow relies on Git-backed asynchronous messaging and isolated authoring:

- **Isolated Operator Logs**: Each operator exclusively writes their own log file in `sync/`
  (`sync/<ID>.md`, from [`sync/_template.md`](../../sync/_template.md)).
  This guarantees zero Git merge conflicts when pushing concurrently to the sync branch.
- **Machine-Readable Status Block**: Operators maintain an ISO 8601 frontmatter header via
  [`tools/sync_hdr.py`](../../tools/sync_hdr.py) declaring `state`, `task`, `waiting_on`, `progress`,
  `eta`, and `updated` timestamp.
- **Central Supervisor Register**: The supervisor (SPV) periodically audits operator frontmatters,
  summarizes overall project state in table 1b of [`SYNC.md`](../../SYNC.md), and issues work orders.
- **Dedicated Feature Branches**: Code changes live on isolated branches (`agy/*`, `feat/claude-*`).
  Only sync logs touch the sync branch; reviewed code reaches `bm-core` by merge, by the owner.

### States and Lifecycle

The five standardized states defined in [`sync/operators.toml`](../../sync/operators.toml):

1. **`WORKING`**: Actively executing steps of an assigned work order. For tasks exceeding 15 min,
   the expected completion timestamp is declared in `eta`.
2. **`WAITING`**: Execution blocked on an external peer (declared in `waiting_on`: `utente`,
   `Claude local`, `Antigravity`, or `Claude cloud`).
3. **`FREE`**: Assigned tasks are 100% complete and pushed; the operator remains active on the
   heartbeat loop, ready to claim new assignments.
4. **`DONE`**: Terminal milestone reached; session concluded without claiming additional work.
5. **`IDLE`**: Unhealthy lack of communication (>60 min without an updated heartbeat or commit).

## Workflow findings and considerations

1. **Single-file vs Monolithic log**: Moving away from concurrent edits in [`SYNC.md`](../../SYNC.md)
   to individual operator files eliminated Git merge collisions completely.
2. **Heartbeat and autonomous liveness**: Maintaining active cron/schedule timers (e.g. 15-minute
   heartbeat interval) prevents false-positive `IDLE` detections while waiting for supervisor review.
3. **Sequential autonomy vs Micro-locking**: Requiring supervisor sign-off after every sub-step
   (<=45 min) introduces significant idle latency. Permitting operators to chain consecutive steps
   autonomously (e.g. AGY-09 steps 3 through 5) with independent commits accelerates throughput.
4. **Environment declarations**: Stating toolchain limitations early (e.g. Windows local without
   Linux host binary compilers vs QEMU virtualized environment) avoids assigning unexecutable tasks.
5. **Contract clarity**: Splitting frontmatter state from narrative logs allows programmatic
   parsing while preserving nuanced developer context.

## Rules (do not break)

- Never edit another operator's file under `sync/`; edit only your assigned `sync/<ID>.md`.
- Always update frontmatter headers with [`tools/sync_hdr.py`](../../tools/sync_hdr.py) before pushing.
- Always use `git pull --rebase origin <sync branch>` before pushing sync log updates to prevent merge bubbles.
- `SYNC.md` stays a project-neutral template: project contracts, work orders and log rows go in a copy for the project (archive it under `docs/archive/` when the project ends).
- Log rows <=600 characters; a status block carries ISO 8601 times with zone, `eta` before any operation >15 min and `ack` (last `SYNC.md` row read).
- **One sync branch per project** (`sync/<name>`): `SYNC.md`, `sync/*` and `tools/sync_hdr.py` live and are pushed only there, not on `bm-core`; this way parallel sync sessions (other agents, other projects) do not touch each other. "Fai handshake `<branch>`" = the procedure below on that branch (described in `CLAUDE.md` §6).
- Handshake = fetch of the sync branch, reading `SYNC.md`, creating `sync/<ID>.md` (status + `## Profilo`, + `## Requisiti` if required), then `FREE`: without a handshake no work is assigned.
- Event-driven heartbeat with no timer (2026-10-15): at every completed task; long tasks split by the operator with progress lines; problems and requests for intervention = a pushed line + one sentence in chat to the user. SPV judges from the `eta` (IDLE beyond `eta`+10 min, 60 min without `eta`; `FREE` is never IDLE) and checks only when woken. A handshake `eta` = "reply expected within N min" (min 3, max 3 short checks). Prompts between operators: a block `da <ID> a <ID>: ...` in chat + a row in the inbox.
- **Deliveries**: one line `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: ... → ... · {ISO}` in `## Consegne` of your own file, then `FREE`; `DONE` is only the terminal state, not a delivery; SPV replies with `REVIEW` and integrates only after `APPROVED`.
- **Review of a delivery**: SPV has each `DELIVERED` reviewed by an independent subagent (perimeter, commands re-run distinguishing pre-existing from introduced, reading the code, outcome per WO) before `REVIEW: APPROVED`; integration only on `APPROVED`. Details in `SYNC.md` §3c.
- **Operators without a direct channel** (e.g. AGY): SPV prepares for the user a single self-sufficient prompt (ID, sync branch, handshake, inbox, work order, branch and perimeter, `--wakeup`, delivery) and writes it in chat; the user pastes it and that is all. Details in `SYNC.md` §3c. For the operators that SPV starts itself the prompt is the `start_session` brief.
- **Inboxes and objective check**: tasks and urgencies in `sync/inbox/<ID>.md` (max 5 lines, only SPV writes); `ack` with `sync_hdr.py --ack auto`; `tools/sync_check.py` says who is alive, who has not read, which deliveries and Log rows are out of format (>600 characters). The status to the user is read from there, never from memory. Profile field `wakeup yes|no`: agents with no autonomous wake-up receive work with an explicit `eta`. A rule change is a `RULE` line with ack. `--ack auto` only confirms delivery of the last line after the fetch, not that it was understood: whoever acts on a line writes `letto <ora riga>` (letto = read; ora riga = the time of that line) in their own Log. A "blocked and silent" operator (`IDLE` for `sync_check`, or a line/inbox not acknowledged for >30 min and not `WAITING utente`): SPV tells the user in chat with the ID and the expected line (`SYNC.md` §3b, Chiusura 5).
- **Heartbeat cost**: prefer a no-model beat (`tools/sync_watch.py`) or a small subagent; the main agent pays the whole conversation at every beat (section "Cost of the heartbeats").
- **Sign-off**: SPV releases an operator with the line `SIGNOFF {ID}: motivo` (motivo = the reason: no suitable work, requirements not met, replacement, end of project); the operator delivers, writes the closure, marks itself `DONE` and stops the heartbeats; `operators.toml`: `active = false`, `signoff = "..."`. No reply within 60 min: `IDLE`, notice to the user.
- No open-ended tasks: steps <=45 min; the operator keeps the main agent free and supervises their own subagents.

## Start phrase (to paste into an operator's session)

> You are `<ID>` in the sync of branch `<branch>`. Fai handshake `<branch>` (CLAUDE.md §6). Then, at each heartbeat (3-5 min until you have taken the first work order, then 15; if you do not wake up on your own, at each user message): `git fetch origin`; `git diff <ultimo hash visto> -- SYNC.md sync/`; read `sync/inbox/<ID>.md`; `python tools/sync_hdr.py <ID> --state ... --task ... --ack auto`; commit and push to the branch. Do not start any work other than that in your inbox; deliver with a `DELIVERED ...` line in the `## Consegne` section of your file.

## Cost of the heartbeats (what a beat costs, and the cheapest way)

A heartbeat is only `git fetch`, a look at the inbox and `sync_hdr.py <ID> --ack auto`. What costs is *who* does it (order of magnitude, no measure yet: the cost ledger is still missing):

| Who does the beat | Cost of one beat | Note |
|---|---|---|
| The **main agent** (a timer that re-sends a prompt) | re-reads the whole conversation: hundreds of thousands of tokens read from the cache (a tenth of the price) plus a few thousand of output and the output of the commands; the full price if the cache has expired | the most expensive; it also breaks if the session has no timer |
| A **small subagent** (e.g. Haiku, empty context, a short prompt) | about 5-15 thousand tokens | tens of times cheaper; the protocol default for agents with subagents |
| A **script, no model**: `tools/sync_watch.py` | 0 tokens | the model wakes up only if the inbox or a row addressed to the operator changes |

`python tools/sync_watch.py --id <ID> [--branch <sync branch>] [--every 5] [--beat 15] [--wake-cmd "..."]`, run in the operator's own clone (a terminal or a background process): every `--every` minutes it fetches, compares the inbox and the Log rows addressed to the operator with the last look, and **only if there is news** writes `.sync_wake`, prints it and runs `--wake-cmd`; every `--beat` minutes it writes the heartbeat itself (`updated` and `ack`; state and task stay as the agent wrote them), commits and pushes. `--once --no-push` is a dry run. It does not replace the agent's own state changes (`WORKING`, `DELIVERED`, `FREE`): those stay the agent's job. An agent without a timer (profile `wakeup no`) still needs something to start it: the watcher's `--wake-cmd`, or the user.
