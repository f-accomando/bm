# bm (BareMetal) — Session Operating Notes

Bare metal kernel for Raspberry Pi Zero W (BCM2835, ARM1176) and PowKiddy RGB30 (RK3566, AArch64, `make TARGET=rgb30`). `.bm` cartridges; `bm/` folder on the SD. Repo `f-accomando/bm`, main branch `bm-core`.

## 1. Targeted Information Retrieval 

Read files in targeted slices (grep, `offset`/`limit`). All current state is tracked in [`docs/progress.md`](docs/progress.md) and in the area sheets in `docs/progress/<area>.md`.

### Quick Lookup Commands
| Need | Command to run |
|---|---|
| What is left to do in an area | `grep -n -A 15 '^## \(Open work\|To do\)' docs/progress/<area>.md` |
| Binding rules of an area | `grep -n -A 20 '^## Rules' docs/progress/<area>.md` |
| Find code in `runtime.c` (350 KB) | `python scripts/cindex.py src/bm/runtime.c <simbolo>` |
| Look up game API | `grep -n -C 2 '<funzione>' docs/api/<area>.md` (index: `docs/API.md`) |
| Game architecture & patterns | `docs/design/games-architecture.md` |

## 2. Rules of Engagement and Development

- **Branch** (avoid orphan branches):
  - **When**: open a branch only if the user asks to develop (feature, fix, refactor). Questions, analysis, research and reads stay on the current branch.
  - **Name**: `claude/<descriptive-name>`, descriptive also when I start on my own initiative; if purpose, area or name is ambiguous, ask the user for the name before creating the branch or modifying files.
  - **Base**: always starts from an up-to-date `bm-core`.
  - **Exception**: if the session explicitly imposes a branch, use that one only.
- **Selective Tests**: Run only the tests touched by the change (`tests/qemu_test.py -k <nome>`, `make test-b3d` for the GPU, specific unit tests). `kernel7` is disabled.
- **Pi & RGB30 Hardware**: Test without serial by copying `dist/kernel.img` to the SD with `./easy_install.sh`; numeric reports come from the console on the `reports` branch.
- **Licenses & Security**: Bare-metal sources hold only original or permissively licensed code (GPU structures from Mesa, MIT). Secrets such as `*_KEY` and tokens live outside the repo.
- **Separate `.bm` and `.b16` Editions**: A `.bm` and a `.b16` are distinct editions with their own Market channels and builds; the `.b16` is built from its own sources and leaves the `.bm` sources as they are.
- **Projects vs Games (M47)**: Editors modify only `.bme` projects. `.bm` files are read-only; `Ctrl+B` compiles the final `.bm`.
- **Writing rules**: State each rule as what to do ("do it this way"), not as a prohibition. When a rule is retired, delete it; do not replace it with its negation, because a "do not X" line keeps the old behavior in view and reads as a new rule. Add an explicit prohibition only as an exception, when Claude keeps repeating something that harms development despite the positive rule, and note the harm next to it.

## 4. Global Invariants

- **Cartridge Sections** (`src/bm/bm.h`): 6 AUDIO (`BMAU`), 8 MESH, 9 ANIM, 10 INFO, 11 SPRITES, 12 LAYERS, 13 FLAGS, 14 BOXES.
- **Tile Flags (bmlib)**: 0 solid, 1 platform, 2 ladder, 3 water, 4 hurts.
- **Per-Device UI Reference**: Keys, menus, and screen rules per device live in [`docs/spec/ui-reference.md`](docs/spec/ui-reference.md).
- **Module-Specific Rules**: Each area (`system`, `v3d`, `graphics`, `lua`, `audio`, `platforms`, `tools`) lists its own non-negotiable rules in the final `## Rules (do not break)` section of its file in `docs/progress/<area>.md`.

## 5. Task Closure & Continuous Scaffolding Maintenance

Before finishing any activity or declaring a task complete:
1. **Update the Status**: Remove or update completed items in `## Open work` of the relevant `docs/progress/<area>.md`.
2. **Update the Rules**: If an architectural constraint was introduced or changed, record it in `## Rules (do not break)` of the corresponding area file.
3. **Integrity Check**: Run `python scripts/docs_audit.py score` to verify there are no broken links or orphan files before finishing.
4. **Consistency**: if you touch formats, APIs, `config.txt` keys, menu/Settings or the behavior of a single kernel (Pi or RGB30), run `python scripts/consistency.py check` and update `docs/spec/` (`specs.toml`, `config-keys.toml`, `divergences.toml`); rules in `docs/spec/CONSISTENCY.md`. Everything must converge before 1.0.0 (`consistency.py gate`).
5. **CI**: if you touch `.github/workflows/` or test timeouts, run `python scripts/ci_audit.py`.

## Glossary

For a Glossary term, run its command and read only that section.

| Term | Meaning | Command |
|---|---|---|
| Roles | agent table | `grep -n -A 14 '^### Roles' docs/agents/routing.md` |
| Readiness Gate | 5-question check | `grep -n -A 14 '^### Readiness Gate' docs/agents/routing.md` |
| bm-deep | heavy agent rules | `grep -n -A 14 '^### bm-deep' docs/agents/routing.md` |
| Necessity | score 1-5 | `grep -n -A 14 '^### Necessity' docs/agents/routing.md` |
| Escalation | closing status | `grep -n -A 14 '^### Escalation' docs/agents/routing.md` |
| Work Order | order format | `grep -n -A 14 '^### Work Order Format' docs/agents/routing.md` |
| Ledger | cost rows | `grep -n -A 14 '^### Ledger' docs/agents/routing.md` |
| Handshake | join a sync | `grep -n -A 14 '^### Handshake' docs/progress/sync.md` |
| Delivery | hand-over line | `grep -n -A 14 '^### Delivery' docs/progress/sync.md` |
| Inbox | inbox and ack | `grep -n -A 14 '^### Inbox and check' docs/progress/sync.md` |
| Heartbeat | beat cost | `grep -n -A 14 '^### Heartbeat cost' docs/progress/sync.md` |
| Closeout | CLOSEOUT phases C0-C6 | `grep -n -A 14 '^### Closeout' docs/progress/sync.md` |
| Sync rules | writers, beat | `grep -n -A 14 '^### Fixed rules' docs/progress/sync.md` |

Roles (`.claude/agents/`): `bm-planner` Opus, plans. `bm-coder` Haiku, code from a Work Order. `bm-mechanic` Haiku, zero-risk edits. `bm-light` Sonnet, simple fixes. `bm-deep` Opus xhigh, one critical problem. `bm-research` Haiku, read-only search. `bm-ops` Haiku, shell commands.

**User preference (autonomy threshold = 4)**: with N=1-3 the expensive model is not used; with N=4 or 5 the agent switches to the expensive model **autonomously**, without asking, stating score and reason in chat. To change the threshold, edit this line. The score is an estimate, not a measurement (measurable with the ledger).

**"Fai handshake `<branch>`"** (from the user) = join the sync of that branch with no further explanation:
1. `git fetch origin`, then checkout `<branch>` in a separate worktree or clone (not on the working branch) and `git pull --rebase`.
2. Read `SYNC.md` (roles §1, contracts §2, work orders §3, rules §3b/§3c) and `sync/operators.toml`; work out who the supervisor (SPV) is and which ID is yours (if there is none, propose one to SPV).
3. Create **only** `sync/<ID>.md` from `sync/_template.md`: status block written with `python tools/sync_hdr.py <ID> --state FREE --task handshake`, `## Profilo` (where you run and breaking points, models, subagents, tools with version) and `## Requisiti` if §2 lists them.
4. Commit and `git pull --rebase origin <branch> && git push origin HEAD:<branch>`; then wait for the assignment written by SPV in `SYNC.md`. Start work only after that assignment.

**Delivery line** (section `## Consegne` of your own file, then `FREE`): `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}` Before it, close the WO with `python tools/sync_hdr.py <ID> --close <WO> --commit <sha> --tokens T --min M` (appends the row to `sync/<ID>.ledger.jsonl` and prints the `done WO` chat line).
