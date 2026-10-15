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
- Battito a eventi senza timer (2026-10-15): a ogni task conclusa; task lunghe spezzate dall'operatore con righe di progress; problemi e richieste di intervento = riga pushata + una frase in chat all'utente. SPV giudica dall'`eta` (IDLE oltre `eta`+10 min, senza `eta` 60 min; `FREE` mai IDLE) e controlla solo quando è svegliato. `eta` di handshake = "risposta attesa tra N min" (min 3, max 3 controlli brevi). Prompt tra operatori: blocco `da <ID> a <ID>: ...` in chat + riga nella casella.
- Operatori non SPV muti in chat (2026-10-15): niente report di avanzamento; scrivono all'utente solo una richiesta breve se serve una sua azione o un prompt pronto da copiare. SPV comunica con l'utente.
- **Consegne**: una riga `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: ... → ... · {ISO}` in `## Consegne` del proprio file, poi `FREE`; `DONE` e solo lo stato terminale, non una consegna; SPV risponde con `REVIEW` e integra solo dopo `APPROVED`.
- **Review di una consegna**: SPV fa rivedere ogni `DELIVERED` a un subagent indipendente (perimetro, comandi rieseguiti con distinzione preesistente/introdotto, lettura del codice, esito per WO) prima di `REVIEW: APPROVED`; l'integrazione solo su `APPROVED`. Dettaglio in `SYNC.md` §3c.
- **Operatori senza canale diretto** (es. AGY): SPV prepara per l'utente un prompt unico e auto-sufficiente (ID, branch di sync, handshake, casella, work order, branch e perimetro, `--wakeup`, consegna) e lo scrive in chat; l'utente lo incolla e basta. Dettaglio in `SYNC.md` §3c. Per gli operatori che SPV avvia da sé il prompt è il brief di `start_session`.
- **Caselle e controllo oggettivo**: compiti e urgenze in `sync/inbox/<ID>.md` (max 5 righe, solo SPV scrive); `ack` con `sync_hdr.py --ack auto`; `tools/sync_check.py` dice chi è vivo, chi non ha letto, quali consegne e righe di Log sono fuori formato (>600 caratteri). Lo stato all'utente si legge da lì, mai da memoria. Campo `wakeup yes|no` nel profilo: agenti senza risveglio autonomo ricevono lavori con `eta` esplicita. Una modifica alle regole è una riga `RULE` con ack. `--ack auto` conferma solo la consegna dell'ultima riga dopo il fetch, non che sia stata capita: chi agisce su una riga scrive nel proprio Log `letto <ora riga>`. Operatore "bloccato e muto" (`IDLE` per `sync_check`, o riga/casella non confermata da >30 min e non `WAITING utente`): SPV lo dice all'utente in chat con ID e riga attesa (`SYNC.md` §3b, Chiusura 5).
- **Heartbeat cost**: prefer a no-model beat (`tools/sync_watch.py`) or a small subagent; the main agent pays the whole conversation at every beat (section "Cost of the heartbeats").
- **Sign-off**: SPV libera un operatore con la riga `SIGNOFF {ID}: motivo` (niente lavoro adatto, requisiti non soddisfatti, sostituzione, fine progetto); l'operatore consegna, scrive la chiusura, si segna `DONE` e smette i battiti; `operators.toml`: `active = false`, `signoff = "..."`. Senza risposta in 60 min: `IDLE`, avviso all'utente.
- No task open-ended: passi <=45 min; l'operatore tiene libero l'agente principale e supervisiona i propri subagent.

## Frase di avvio (da incollare nella sessione di un operatore)

> Sei `<ID>` nel sync del branch `<branch>`. Fai handshake `<branch>` (CLAUDE.md §6). Poi, a ogni battito (3-5 min finché non hai preso il primo work order, poi 15; se non ti svegli da solo, a ogni mio messaggio): `git fetch origin`; `git diff <ultimo hash visto> -- SYNC.md sync/`; leggi `sync/inbox/<ID>.md`; `python tools/sync_hdr.py <ID> --state ... --task ... --ack auto`; commit e push sul branch. Non iniziare altro lavoro che quello della tua casella; consegna con una riga `DELIVERED ...` nella sezione `## Consegne` del tuo file.

## Cost of the heartbeats (what a beat costs, and the cheapest way)

A heartbeat is only `git fetch`, a look at the inbox and `sync_hdr.py <ID> --ack auto`. What costs is *who* does it (order of magnitude, no measure yet: the cost ledger is still missing):

| Who does the beat | Cost of one beat | Note |
|---|---|---|
| The **main agent** (a timer that re-sends a prompt) | re-reads the whole conversation: hundreds of thousands of tokens read from the cache (a tenth of the price) plus a few thousand of output and the output of the commands; the full price if the cache has expired | the most expensive; it also breaks if the session has no timer |
| A **small subagent** (e.g. Haiku, empty context, a short prompt) | about 5-15 thousand tokens | tens of times cheaper; the protocol default for agents with subagents |
| A **script, no model**: `tools/sync_watch.py` | 0 tokens | the model wakes up only if the inbox or a row addressed to the operator changes |

`python tools/sync_watch.py --id <ID> [--branch <sync branch>] [--every 5] [--beat 15] [--wake-cmd "..."]`, run in the operator's own clone (a terminal or a background process): every `--every` minutes it fetches, compares the inbox and the Log rows addressed to the operator with the last look, and **only if there is news** writes `.sync_wake`, prints it and runs `--wake-cmd`; every `--beat` minutes it writes the heartbeat itself (`updated` and `ack`; state and task stay as the agent wrote them), commits and pushes. `--once --no-push` is a dry run. It does not replace the agent's own state changes (`WORKING`, `DELIVERED`, `FREE`): those stay the agent's job. An agent without a timer (profile `wakeup no`) still needs something to start it: the watcher's `--wake-cmd`, or the user.
