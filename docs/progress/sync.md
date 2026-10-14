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
- **Un branch di sync per progetto** (`sync/<nome>`): `SYNC.md`, `sync/*` e `tools/sync_hdr.py` vivono e si pushano solo lì, non su `bm-core`; così sessioni di sync parallele (altri agenti, altri progetti) non si toccano. "Fai handshake `<branch>`" = la procedura sotto su quel branch (descritta in `CLAUDE.md` §6).
- Handshake = fetch del branch di sync, lettura di `SYNC.md`, creazione di `sync/<ID>.md` (stato + `## Profilo`, + `## Requisiti` se richiesti), poi `FREE`: senza handshake non si assegna lavoro.
- Dopo l'handshake: battiti brevi (3-5 min) finché l'operatore non ha preso in carico il primo work order (`ASSIGNED` letto, `ack`, `WORKING`), e SPV finché lo ha confermato; tetto di 6 battiti brevi; poi battito ogni 15 min.
- No task open-ended: passi <=45 min; l'operatore tiene libero l'agente principale e supervisiona i propri subagent.
