# Sync & Multi-Agent Coordination

Architecture, state lifecycle, and operational findings from the multi-agent development workflow
connecting several operators (in the first project: Antigravity AGY, Claude Code supervisor SPV on Windows with WSL, Claude cloud
recorder CLD in QEMU, later a fourth on another Windows machine) through one dedicated **sync branch** per project.

On this branch `SYNC.md` is the live file of the first project (Tropic Trail); the **reusable, project-neutral template** is on the branch `sync-handshake` (`SYNC.md`, `sync/_template.md`, `CLAUDE.md` section 6): at the end of the project it is archived under `docs/archive/` and the template replaces it.

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
- In the template (branch `sync-handshake`) `SYNC.md` stays project-neutral: project contracts, work orders and log rows go in a copy for the project, on its own sync branch (archive it under `docs/archive/` when the project ends).
- Log rows <=600 characters; a status block carries ISO 8601 times with zone, `eta` before any operation >15 min and `ack` (last `SYNC.md` row read).
- **Un branch di sync per progetto** (`sync/<nome>`): `SYNC.md`, `sync/*` e `tools/sync_hdr.py` vivono e si pushano solo lì, non su `bm-core`; così sessioni di sync parallele (altri agenti, altri progetti) non si toccano. "Fai handshake `<branch>`" = la procedura sotto su quel branch (descritta in `CLAUDE.md` §6).
- Handshake = fetch del branch di sync, lettura di `SYNC.md`, creazione di `sync/<ID>.md` (stato + `## Profilo`, + `## Requisiti` se richiesti), poi `FREE`: senza handshake non si assegna lavoro.
- Dopo l'handshake: battiti brevi (3-5 min) finché l'operatore non ha preso in carico il primo work order (`ASSIGNED` letto, `ack`, `WORKING`), e SPV finché lo ha confermato; tetto di 6 battiti brevi; poi battito ogni 15 min.
- **Consegne**: una riga `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: ... → ... · {ISO}` in `## Consegne` del proprio file, poi `FREE`; `DONE` e solo lo stato terminale, non una consegna; SPV risponde con `REVIEW` e integra solo dopo `APPROVED`.
- **Caselle e controllo oggettivo**: compiti e urgenze in `sync/inbox/<ID>.md` (max 5 righe, solo SPV scrive); `ack` con `sync_hdr.py --ack auto`; `tools/sync_check.py` dice chi e vivo, chi non ha letto, quali consegne e righe di Log sono fuori formato (>600 caratteri). Lo stato all'utente si legge da li, mai da memoria. Campo `wakeup yes|no` nel profilo: agenti senza risveglio autonomo ricevono lavori con `eta` esplicita. Una modifica alle regole e una riga `RULE` con ack.
- **Sign-off**: SPV libera un operatore con la riga `SIGNOFF {ID}: motivo` (niente lavoro adatto, requisiti non soddisfatti, sostituzione, fine progetto); l'operatore consegna, scrive la chiusura, si segna `DONE` e smette i battiti; `operators.toml`: `active = false`, `signoff = "..."`. Senza risposta in 60 min: `IDLE`, avviso all'utente.
- No task open-ended: passi <=45 min; l'operatore tiene libero l'agente principale e supervisiona i propri subagent.

## Frase di avvio (da incollare nella sessione di un operatore)

> Sei `<ID>` nel sync del branch `<branch>`. Fai handshake `<branch>` (CLAUDE.md §6). Poi, a ogni battito (3-5 min finché non hai preso il primo work order, poi 15; se non ti svegli da solo, a ogni mio messaggio): `git fetch origin`; `git diff <ultimo hash visto> -- SYNC.md sync/`; leggi `sync/inbox/<ID>.md`; `python tools/sync_hdr.py <ID> --state ... --task ... --ack auto`; commit e push sul branch. Non iniziare altro lavoro che quello della tua casella; consegna con una riga `DELIVERED ...` nella sezione `## Consegne` del tuo file.
