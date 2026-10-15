# Next session (written by SPV, 2026-10-15)

Single writer: SPV. Read this first, then `SYNC.md` Log (last rows) and `python tools/sync_check.py`.

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
