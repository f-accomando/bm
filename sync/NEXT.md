# Next session (written by SPV, 2026-10-15)

Single writer: SPV. Read this first, then `SYNC.md` Log (last rows) and `python tools/sync_check.py`.

**Section 5 (end of day, 18:15) supersedes sections 1-2 where they differ.**

**Section 6 (end of day, 21:50) supersedes section 5 where they differ. Read section 6 first.**

## 1. State at the end of today

| Item | State |
|---|---|
| Integration `claude/integrazione-ottobre` (0c8556a9, pushed) | Contains b16-profile, net-test-err, sdlog, D28, sdk-games-message, sdlog-prev-fix, net-keys, **FRZ-1..4,6,7**, **R-NET4**. Not in `bm-core`. |
| CI | Red on `bm-core` before today. `claude/ci-repair` (c38271f7) still red: after the `ZOOM_STEPS` fix the `host` job now fails on `make test-prompts` (9 FAIL, `(null)` names). op-ci has the message. |
| Operators | NET, B16, AGY FREE. CI WAITING (silent since 16:14, eta 14:45 overdue). New chat rule (see Log): `taken WO X` / `done WO X ...: files, tokens, minutes`. |
| Pending review | `ci-repair` D-RUN2 and D-IMP (CI must be green first). |
| Docs | `claude/docs-english` (40 files in English + positive rules) and `claude/roadmap-restructure` (levels + Beta section, now in English) pushed, **no PR yet**. `SYNC.md`, `sync/`, `docs/progress/sync.md` still Italian. |
| Meshy | Secret `MESHY_API_KEY` deleted, workflows removed on `claude/remove-meshy-workflows` (PR https://github.com/f-accomando/bm/pull/18, red only because `bm-core` is red). **User: revoke the key in the Meshy dashboard.** |
| Other repos | `scaffold`: PR #1 open (`claude/workflow-template`). `baremetal`: local clean export being built by an agent, not pushed. |
| PR 17 | `claude/exciting-galileo-9gevjm` (ctx_guard, /cp): red only because `bm-core` is red. Do not touch. |

## 2. Unfinished work (first hours)

1. **CI green** (op-ci): fix `test-prompts`, read the run, then SPV reviews D-RUN2/D-IMP, then integrate `claude/ci-repair` into the integration branch.
2. **PRs, only with the user's go**: integration -> `bm-core`; `claude/docs-english`; `claude/roadmap-restructure`; `claude/remove-meshy-workflows`. Order: remove-meshy, docs-english, roadmap, integration (the last one when CI is green).
3. **User hardware tests** (they unblock SYS-2, SYS-3): watchdog (monitor `X` then `f`, reboot in about 3 s, report "froze last time"), `pi.py logs bootprev` after a volunteer crash, D43 on the console (screenshot and err), D-RGB2 after ci-repair. Then divergences D19 and D43 to `resolved`.
4. **Translate to English** when operators are FREE: `SYNC.md`, `sync/*`, `docs/progress/sync.md`, tool strings of the sync protocol (`## Consegne`, `SIGNOFF`, ...), with a `RULE` row.
5. **Decisions the user owes**: FRZ-5 (fixed `.noinit` address; SPV recommends not doing it), watchdog duration (3 s or 8 s), source of the four fake freezes (PM_RSTS), SIGNOFF of op-b16 and op-net, old remote branches (`v3d-async-proposal`, `forge-organic-meshes`, `organic-recipes`, `meshy-out`), queued CI runs to cancel, the wrong report on `reports` (`reports/unknown/20261015-113845_log_pi-zero-w_dev.txt`, password masked), the broken host test `sdk_b16`.
6. **baremetal**: when the agent delivers, review the tree (no games, no workflow, no AI files, no secrets), then push to `f-accomando/baremetal` only with the user's go (public repo). Regenerate later with `baremetal-export/export_baremetal.py` once the integration is in `bm-core`.

## 3. Work for the next 8-10 hours (toward the Beta, see `docs/ROADMAP.md` section Beta)

Order matters; each item is one operator, one feature. The planner (`bm-planner`) writes the work orders first.

| Hour | Item | Roadmap | Who | Done when |
|---|---|---|---|---|
| 0-1 | CI green, ci-repair integrated, PR sequence above | SYS-1, BETA-1 | op-ci, SPV | all jobs of `ci.yml` green on `bm-core` |
| 0-1 | Planner: work orders for APP-4, APP-5, API-1, APP-6 (Readiness Gate) | BETA-2..4 | `bm-planner` | four WO files in `sync/wo/` with verification commands |
| 1-3 | **APP-4 template "Stages"** (title, 3 stages, ending, save) in the bm SDK | APP-4, BETA-2 | op-b16 | new project compiles to `.bm`, runs in QEMU and the RGB30 virt build; game state machine from `games-architecture.md` |
| 1-3 | **APP-5 API help and lint in bm Code** (one API source generated from `docs/api/`, also used by `predict`) | APP-5, BETA-3 | AGY or op-net | key shows signature and text; lint flags an unknown API name; `predict` reads the same source |
| 3-5 | **API-1 performance budget** (2D sprites/tiles, 3D meshes, particles) measured on Pi and RGB30, `profile(true)` explained | API-1, BETA-4 | op-net (measures) + user (RGB30 numbers) | table in `docs/PERFORMANCE.md`; RGB30 column filled |
| 3-6 | **APP-6 dev without PC** (RGB30 Lib tab, resources on device) | APP-6, BETA-2 | op-b16 | create, save and build a project with sprite, map, sound and code on RGB30 |
| 5-7 | **BETA-6 consistency triage**: classify D01-D13, D28, D30-D36; `consistency.py gate` shows only accepted items for menu, input, Games, SDK, saves | BETA-6 | SPV + planner, user tests | list of blockers cleared or accepted in writing |
| 6-8 | **Acceptance run (2D)**: someone who does not know the code builds a Stages game with the suite only | BETA-2 | user | the acceptance test of the Beta section passes |
| 7-9 | **3D version of the acceptance game** (V3D on Pi, Mali on RGB30), budget respected | BETA-5, M36, M41 | op-b16 + user | 3D game with 3 stages at the declared frame rate |
| any | Hygiene: R0 `agent_audit.py` `_typed` loses task/date/model/area; P1 `tools/sync_perimeter.py`; ledger baseline (D-R2); `tests/quarantine.txt` (D-R3) | SYS-4 | bm-light | scripts pass their tests; `agent_audit.py` output complete |
| end | Beta quick start page (1 page) + `GAME-GUIDE.md` update | BETA-7, APP-3 | bm-light | a new developer reaches the first game from the page |

Rules for the session: one operator per feature; user approval before any push to `bm-core`, merge or PR; `bm-deep` only with necessity 4-5 stated; the Pi is crashed or flashed only after an explicit "ok"; notify the user before prompting an operator.

## 4. Housekeeping done at the end of today
- Removed (merged into the integration): remote and local branches `claude/b16-profile`, `claude/net-test-err`, `agy/sdlog`, `agy/d28-games-carts`, `agy/sdlog-prev-fix`, `claude/sdk-games-message`, `claude/net-keys`, `agy/freeze-reports`, `claude/net-logs`, and their worktrees.
- Kept: worktrees of `ci-repair`, the integration, roadmap, docs-english, remove-meshy, the sync worktrees of op-ci / op-net / AGY (reused next session).
- Local only: `bm-core` is ahead of origin by ledger rows (`docs/agents/ledger.txt`); push them with the next protocol commit on `bm-core`.

## 5. Update at the close of the session (18:15, supersedes sections 1-2)

**Merged into `bm-core` today (user-approved):** PR 17 (ctx_guard, /cp), 18 (Meshy workflows removed), 19 (docs in English, positive rules), 20 (ROADMAP with levels and Beta section). The docs-english, roadmap and remove-meshy branches and worktrees are removable.

**Integration `claude/integrazione-ottobre` (0c8556a9):** still NOT in `bm-core`, no PR. Needs CI green and the user's go. It contains FRZ-1..4,6,7 and R-NET4 (both APPROVED).

**Hardware proof done today (Pi Zero W, integration kernel, user + bridge):**
- Boot log: `watchdog: on, 3000 ms (loaded 196608, after 20 ms 195358)`.
- Monitor `X f`: the Pi restarted in 2-3 s; next boot says "the Pi froze last time and the watchdog restarted it".
- `bootprev.txt` describes the frozen run ("freeze test (X f), 296 s after boot"), not the new boot: the SDLOG-FIX bug is closed.
- To do tomorrow (SPV, on the integration branch, with the user's go to push): set D19 to `resolved` in `docs/spec/divergences.toml`; remove "to test on the Pi" from SYS-2 and SYS-3 in `docs/ROADMAP.md`. Still untested: D43, D-RGB2, the voluntary crashes (`keys crash-*`).
- The bridge (`tools/pi_bridge.py`, started from `bm33-int`, host 192.168.1.108) was restarted by the user with the new code (`logs`, `keys`, `kernel`). It dies with the shell; the password is typed by the user each time.

**New work for tomorrow, R-NET5 (op-net, low-medium priority):** a durable bridge for tests: named keys for `X f` (freeze), reboot and a quick status; clarify whether the bridge can survive a Pi restart without storing the password; keep the fixed tables (no free keys). SPV writes the WO first (Readiness Gate).

**AGY-1..5 (assigned 17:48, `sync/wo/AGY-1.md` ... `AGY-5.md`, pushed f9a780e1):** five low-priority, non-graphics WOs to run one after the other (API-SRC, API-LINT, TRIAGE, AUDIT, PERIM). The user has the start prompt; AGY has not read the inbox yet (unread 1). Tomorrow: read AGY's DELIVERED rows, review each (independent subagent, no verdict handed over). Decisions taken: APP-5 data part goes to AGY, `src/ai/api.tsv` is the single API source (switching `predict`/`mkwords.py` to it is a later WO), `sync_perimeter.py` lives on `bm-core` with the other `sync_*.py`.

**Protocol rows added today:** `eta` is now a plain time (`--eta 18:00`, `sync_hdr.py` completes date and offset from the machine clock; a UTC host falls back to +02:00, wrong after the clock change on 25 Oct), pushed c50bff94. Operators must `ack`. sync_check still flags NET's non-standard DELIVERED line (R-NET4 rev. 2).

**Agents left running (let them finish, then collect):** the CI diagnosis agent (reproduces every failing target of the `host` job locally; expected table: target, cause, class, minimal fix, pre-existing on bm-core, repro command) and the `baremetal` export agent (local repo `C:\Users\Utente\Documents\GitHub\baremetal`). First thing tomorrow: read both reports; then one agent per CI failure with disjoint file perimeters, fixes only on `claude/ci-repair`, verified locally before the push. Push of `baremetal` to `f-accomando/baremetal` only with the user's go, after reviewing the tree.

**Local housekeeping:** `bm-core` in the main checkout is behind origin and has unpushed ledger rows (up to the planner row for the AGY WOs): `pull --rebase` and push with the next protocol commit. Planner row for the AGY WOs and the earlier subagents are in the ledger.

**Still open with the user (unchanged):** revoke the Meshy key in the dashboard; scaffold PR 1 waits for the go; FRZ-5 (SPV recommends not doing it); watchdog duration; SIGNOFF of op-b16 and op-net; old remote branches; queued CI runs to cancel; the wrong report in `reports/unknown/20261015-113845_log_pi-zero-w_dev.txt`; broken host test `sdk_b16`; English translation of `SYNC.md`, `sync/*`, `docs/progress/sync.md` and the protocol strings when operators are FREE.

## 6. Update at the close of the session (21:50, supersedes section 5)

**CI (the big change).** `claude/ci-repair` @ `065f102b`: run 38759439417 fully GREEN. Tip `0c494f0a` (test-nano8 removed from `HOST_TESTS`, user's call): host, qemu 1/2/4, rgb30 green; only `qemu (3/4)` red on `test_editor_mouse` ("'menu' not on screen"), a test that passed on `065f102b` with no code change in between: probably flaky. First thing: rerun `qemu (3/4)` (`gh run rerun <id> --failed`, needs the user's go), then decide: quarantine it with `tests/quarantine.txt` (AGY-5 tool) or fix. `ci-repair` is 32 commits behind `bm-core`: rebase/merge, run `make -k test-host` locally (skill `bm-ci-triage`), then CI. Integration `claude/integrazione-ottobre` (0c8556a9) is still NOT in `bm-core`, no PR (needs green CI + the user's go).

**Open PRs (user approved opening them, not merging):** bm PR 21 `claude/skills-review-ci-triage` (skills bm-review, bm-ci-triage; bm-work-order section 4 for sync WOs) and PR 22 `claude/ledger-add` (`scripts/ledger_add.py`, `scripts/ledger_repeats.py`, CLAUDE.md ledger line). Their `host` check is red from the pre-existing `bm-core` failures, not from them. Pushed, no PR yet: `claude/translate-skill` (skill `bm-translate`, `scripts/translate_check.py`, 72-pair glossary): ask the user for the PR. scaffold PR 1 is already merged.

**New tools (all tested in PowerShell, Git Bash and WSL):** `tools/sync_log.py` (sync branch; Log rows and inboxes with the real time, `--push`; the 600 limit is the WHOLE row, as `sync_check.py` counts it), `scripts/ledger_add.py`, `scripts/ledger_repeats.py` (rule "third repetition = skill or script"; ledger today: `review ID` x15, `translate` x6, `wo` x10). Use them instead of inline heredocs.

**Protocol rows added today (operators must `ack`):** chat message per WO in real time; `preso WO <X> eta HH:MM` and `concluso WO <X>: ...`; `in attesa di <Y>` only for third-party input (not for the next WO); `eta` as plain time. `sync_check` flags 5 DELIVERED rows as non-standard: NET (R-NET4 rev. 2) and AGY-6b, 6c, 8, 9 lack the `->` between command and result in `verifica:` (pattern `comando -> risultato`); tell AGY.

**AGY (FREE):** AGY-1..4, 6a, 6c APPROVED; AGY-7 (scaffold-workflow-IT.pdf, `agy/scaffold-workflow-it`@0a3b76d) APPROVED with minor fixes; AGY-8 (`scaffold_prompt.md`, `agy/scaffold-prompt`@68e7d53) CHANGES REQUESTED (default ref must be `main`, include `examples/demo` for selftest, sync only if asked, operational coverage, existing-repo flow in one message). AGY-6b -> AGY-9 (`agy/beta6-triage`@fd80458b) NOT yet reviewed: use `bm-review` (open the ~44 `file:line` citations); the D03/D04 classes are for SPV and the user. `sync/wo/AGY-10.md` (fixes for AGY-7 and AGY-8) is written and announced in AGY's inbox but not started. Nothing of AGY is in `bm-core`; integrate on the user's go, in this order: `agy/api-source` (APP-5 data), `agy/api-lint`, `agy/agent-audit-baseline`, `agy/sync-perimeter-quarantine`, `agy/beta6-triage` (after its review).

**Draft WOs for the other operators (`sync/wo/drafts/`, NOT assigned; the user asked to keep them for the next session):** `R-NET5` (op-net: bridge `freeze`/`reboot` keys, `status`, `--detach`; decisions D1-D4; D4 = the `line` filter lets `set wifi_psk=x` through: separate WO R-NET6), `APP-4` (op-b16: "Stages 2D" template in the RGB30 SDK; D1-D8), `APP-5b` (op-net: help and lint in bm Code from `api.tsv`; D1-D5; needs agy/api-source and agy/api-lint integrated first), `API-1` (op-net: `carts/budget` + `scripts/perf_table.py` + PERFORMANCE.md; D1-D6; RGB30 numbers are the user's), `APP-6` (op-b16, after APP-4: pad-only sprite/map/code/sound; D1-D6). Proposed order for op-net: R-NET5, API-1, APP-5b. Next: the user confirms the decisions, SPV moves the drafts to `sync/wo/<ID>.md`, assigns them with `sync_log.py` rows and tells the user before prompting each operator. Hardware proofs stay the user's.

**To do on the integration branch (push only with the user's go):** D19 -> `resolved` in `docs/spec/divergences.toml`; remove "to test on the Pi" from SYS-2 and SYS-3 in `docs/ROADMAP.md`. Untested on hardware: D43, D-RGB2, voluntary crashes.

**Housekeeping:** worktrees `bm33-roadmap`, `bm33-docs-en`, `bm33-nomeshy` and branches `claude/roadmap-restructure`, `claude/docs-english`, `claude/remove-meshy-workflows` are removable (remote deletes need the user's go); worktrees `bm33-skills`, `bm33-ledger`, `bm33-translate` hold the three open branches. The local `bm-core` checkout is behind `origin` and has uncommitted ledger rows (today's subagents, written with `ledger_add.py`): commit and push them with the next protocol commit. At the end of a session with all operators FREE: translate `SYNC.md`, `sync/*`, `docs/progress/sync.md` and the protocol strings to English with `bm-translate` (strings read by `sync_*.py` are listed in the skill) plus one RULE row.

**Still open with the user:** revoke the Meshy key; FRZ-5 (SPV recommends not doing it); watchdog duration; SIGNOFF of op-b16 and op-net; old remote branches; queued CI runs to cancel; the wrong report in `reports/unknown/20261015-113845_log_pi-zero-w_dev.txt`; broken host test `sdk_b16`; public repo `baremetal` (local `C:/Users/Utente/Documents/GitHub/baremetal`, commit `0f40e87`, no remote): review the tree first (it tracks `.github/workflows/ci.yml`, `keys/*.pem`, `boot/ca.pem`, game folders under `carts/`, 34 media files, while the request was kernel + system + suites + PDF manuals only) and push only with the user's go.

## 7. Update 16 Oct (morning, supersedes section 6 where they differ)

**Rules from the user for the day:** minimal chat; tell the user only when a WO is ready, taken, delivered, reviewed, or when a branch is opened/closed/merged (one short line each); prepare the WOs, assign the ready ones, send AGY prompts in chat; hardware proofs together at the end (the user asks); on blockers warn or find a solution; if development time is high, look for alternative routes.

**Assigned today (decisions of the planners adopted, see line 2 of each WO):** NET: R-NET5, API-1, APP-5b, R-NET6 (in this order). B16: APP-4, APP-6. AGY: AGY-10 (fixes of AGY-7 and AGY-8), then BETA-7 (quickstart manual en+it, `scripts/doc_refs.py`; 7a-7d now, 7e after APP-4, APP-5b, API-1). Operators were messaged directly (op-net, op-b16); AGY gets its prompt from the user in chat. Status seen: B16 took APP-4 (eta 12:30); NET is on API-1 (API-1c passes) and was seen on branch `claude/net-test-err-16e4ba`. Check with `python tools/sync_check.py` and the DELIVERED rows. Reviews: use skill `bm-review`, independent `general-purpose` reviewers, then `sync_log.py` REVIEW row and `ledger_add.py` row.

**Reviews done:** AGY-9 (`agy/beta6-triage`@fd80458b) APPROVED (D03/D04 classes left to SPV and the user). AGY-7 APPROVED with minors, AGY-8 CHANGES REQUESTED (both in AGY-10).

**CI:** flaky `test_editor_mouse` (qemu 3/4) diagnosed: timing race at tests/qemu_test.py ~6540; fix pushed on `claude/ci-repair` (`6e0b5b40`, same-dump `look(q, ["menu"], ...)`); CI run triggered; local repro impossible (QEMU did not start in WSL: ConnectionRefusedError, unexplained). If green: rebase/merge `ci-repair` on `bm-core` (32 behind), `make -k test-host`, then CI, then (user's go) integrate; then re-evaluate PR 21 and PR 22 (red only from pre-existing bm-core failures).

**baremetal (public repo `f-accomando/baremetal`, empty on GitHub):** the local repo has a clean commit `91c970d` on top of `0f40e87` (games pong/snake/village/typing, 26 media files, CI workflows removed; 1177 tracked files; `make` builds the Pi kernel; no secrets; public keys kept). Agent `a39d44e1901dc3d11` is fixing the Makefile test targets that still cite the removed games. Not pushed, no remote: the user's go is needed (push to a public repo).

**Pending with the user:** D03/D04 classes of the triage; baremetal push; PR for `claude/translate-skill`; rerun/cancel of CI runs; integration order of the AGY branches into bm-core; hardware proofs at the end of the day (list them from the delivered WOs).

**Housekeeping:** ledger rows of 15-16 Oct are uncommitted in the local `bm-core` checkout (written with `scripts/ledger_add.py` from `..\bm33-ledger`); commit/push with the next protocol commit. `sync_log.py` is the way to write Log rows and inboxes.

## 8. Update 16 Oct (evening, supersedes sections 6-7 where they differ)
Session opens when the user says "buongiorno". The user runs the compact prompt, then logs off; tomorrow restarts with "buongiorno".

**Operators at the close (all FREE, SIGNOFF sent):** NET, B16, AGY, AGX. CI idle (stale 550 min).

**Reviews to do first thing (Haiku for docs and host Python, Sonnet for C/Lua/QEMU):** AGY-29 fix (313d1b41), AGY-38 (726e4718, QEMU), AGY-39 (4123405b), AGY-40 (f15ff570); AGX-3, 5, 6 (not reviewed today, user decision; AGX-4 done). Corrections owed: APP-8 (text only), R-NET7a (test_monitor_crc flaky: find why two `c` presses give different screens; screendump or short sleep).

**WO drafts for the day (`sync/wo/drafts/`, not assigned):** R-NET10 (bridge reads the password from a file outside the repo, mode 0600, user writes it; the user already created `~/.pi_bridge.pw`), QEMU-FLAKE (ConnectionRefused in `Qemu.__init__` + stages_build banner timeout; after AGX-3), R-NET8b, AGX-14 test with `keys(pause=0.3)` (`tests/rgb30/qemu_test.py:592-594`), AGY-26 note: `AI.md:3` points to untracked `AI-IT.md`, D12 note cites `menu_ui.c:1465` (the condition is at :1463), `ZOOM_STEPS` undefined in `carts/editor/main.lua:1799` (`make test-studio` EXIT=2, pre-existing). Renumber D43/D44 -> D45 at integration.

**User proofs (all moved to tomorrow):** `:crc` (R-NET7), `pi_soak` (R-NET8), long watchdog, budget API-1, Stages 2D template, APP-6 pad only, APP-5b on screen, BETA-HWCARD (AGY-39 card). Done today with the user's OK: R-NET9 on the Pi (watchdog on 3000 ms, voluntary freeze reboots after ~10 s, normal kernel restored).

**Standing user decisions (16 Oct):** permanent OK for upload/crash/reboot/pi_soak on Pi 1 (`set`/`save` stay refused by the bridge); password rule "never on disk" relaxed for the bridge file only (R-NET10); Haiku medium for reviews of docs and host Python (`bm-reviewer` on `claude/bm-reviewer-agent@7e7427f8`, not merged).

**Merges still waiting for the user's go:** PR 25 (CI red: test_chainloader fixed by R-NET9 but not in the PR yet; flaky meshy2mesh/editor_mouse), approved branches (see Log), `claude/bm-reviewer-agent`.

**Warning:** reviewers deleted `bm*` dirs in WSL `/tmp` (not only theirs). Tell reviewers to remove only dirs they created.

## 9. Decisioni utente 17 Oct (divergenze, BETA-6)
- D03: accepted. RGB30 un giocatore locale; eccezione futura: schermo condiviso/split screen se l'RGB30 e' collegato via HDMI a una TV.
- D05: RGB30 senza mouse, ma lo stick fa da puntatore in app/feature specifiche. Tenere il codice mouse; SPV verifica se coincide con la funzione dello stick (ricerca lanciata, esito da riportare).
- D09: non chiusa. Da discutere con l'utente: cosa e' gioco/app/utility, cosa sta nel kernel e cosa nel market; distinzione .b16 / .bm / .bme e cartelle.
- D10: non chiusa. Definizione di sistema: fiber su ARM6 (Pi Zero), multitask vero su ARM7/8 multicore; quando entra in gioco l'uno e quando l'altro (vedi progress/multitasking.md).
- D11: Send si' (condividere giochi e app con amici), Publish spento per ora (da considerare in futuro); l'RGB30 non e' per sviluppo, il .b16 dell'SDK e' solo per test.
- Seguito: WO per divergences.toml (D03 accepted, D05, D11) dopo la verifica D05; D09 e D10 = sessioni di discussione/progetto.
- D05 verifica (17 Oct, git grep su integrazione-ottobre): il puntatore del kernel (`src/kernel/pointer.h`, M32) e' solo per un mouse vero (USB, BT classic, BLE), mai da un controller (decisione utente 2026-10-05: stick e R2 lo facevano apparire come un mouse). Lo stick-cursore esiste solo nelle cartucce (nano8 `40_vm.lua:232`: il cursore segue lo stick sinistro; overbit). Sull'RGB30 il mouse BT funziona (`bm_input.c:90`, `main.c:253`), la porta USB no (`bm_port.c:185`). Quindi sono due codici distinti: tenere il puntatore del kernel; se l'utente vuole lo stick come puntatore nel menu o negli strumenti e' una funzione nuova che ribalta la decisione del 5 ott.
- R2 (17 Oct): approvati tutti gli sviluppi 3D programmati e consigliati per BETA-5 (piano W01..W17, H1..H8 nel Log del 17 ott). M44 (QPU) rientra; audio QPU e hit3d fuori. Mali fasi 2-4: si scrive senza aspettare H1. Assegnazione dei WO: decide SPV.
- D1 (17 Oct): budget 3D. RGB30 360x360 (opzionale 720x720); Pi 640x360 (test opzionali a 720p e 1080p). Frame rate: Pi 60 fps; RGB30 30 fps finche' H6 non da numeri.
- D2 (17 Oct): risoluzioni diverse (Pi 640x360, RGB30 360x360) -> una sola cartuccia non basta: SPV decide logica di gioco condivisa (moduli Lua comuni) + front per edizione (vista/HUD/risoluzione), .bm per il Pi e .b16 per l'RGB30, in due tempi (prima il Pi).
