# bm (BareMetal): session notes

bm is a bare-metal kernel. It runs on the Raspberry Pi Zero W (BCM2835, ARM1176) and on the PowKiddy RGB30 (RK3566, AArch64; build with `make TARGET=rgb30`). Cartridges are `.bm` files in the `bm/` folder on the SD card. The repository is `f-accomando/bm`. The main branch is `bm-core`.

## 1. Find information

Read files in small slices (`grep`, or `offset` and `limit`). The current state is in [`docs/progress.md`](docs/progress.md) and in the area sheets `docs/progress/<area>.md`.

| Need | Command or file |
|---|---|
| What is left to do in an area | `grep -n -A 15 '^## \(Open work\|To do\)' docs/progress/<area>.md` |
| Binding rules of an area | `grep -n -A 20 '^## Rules' docs/progress/<area>.md` |
| Code in `runtime.c` (350 KB) | `python scripts/cindex.py src/bm/runtime.c <symbol>` |
| Game API | `grep -n -C 2 '<function>' docs/api/<area>.md` (index: `docs/API.md`) |
| Game architecture and patterns | `docs/design/games-architecture.md` |
| Cartridge format and section numbers | `docs/BM-FORMAT.md`; constants: `grep -n 'BM_SEC_' src/bm/bm.h` |
| Tile flags | `grep -n -A 5 'The tiles. flags' docs/api/draw.md` |
| Keys, menus, and screen rules per device | `docs/spec/ui-reference.md` |

## 2. Rules

**Branches**
- When: Open a branch only when the user asks for development work (feature, fix, or refactor). For questions, analysis, research, and reads, stay on the current branch.
- Name: Use `claude/<descriptive-name>`. Make the name descriptive, even when you start the work on your own initiative. If the purpose, area, or name is not clear, ask the user for the name before you create the branch or change files.
- Base: Start from an up-to-date `bm-core`.
- Exception: If the session names a branch, use only that branch.

**Selective tests.** Run only the tests that the change affects. Examples: `tests/qemu_test.py -k <name>`, `make test-b3d` (GPU), or a specific unit test. The `kernel7` target is disabled.

**Pi and RGB30 hardware.** Test without a serial connection. Copy `dist/kernel.img` to the SD card with `./easy_install.sh`. The console sends numeric reports to the `reports` branch.

**Licenses and security.** Bare-metal sources contain only original code or code with a permissive license. You can use GPU structures from Mesa (MIT). Keep secrets, such as `*_KEY` and tokens, outside the repository.

**`.bm` and `.b16` editions.** A `.bm` and a `.b16` are different editions. Each edition has its own Market channel and its own build. Build the `.b16` from its own sources. Leave the `.bm` sources unchanged.

**Projects.** Change only `.bme` projects. `.bm` files are read-only.

**Writing rules.** State each rule as an action ("do it this way"). Delete a retired rule. A negation of it keeps the old behavior in view. Add an explicit prohibition only when an action keeps harming development despite the positive rule. Write the harm next to the prohibition.

## 3. Task closure

Before you declare a task complete, do these steps:

1. Update the status. Remove the completed items and update the partial items in `## Open work` of the area file `docs/progress/<area>.md`.
2. Update the rules. If you add or change an architectural constraint, record it in `## Rules (do not break)` of the same file.
3. Check integrity. Run `python scripts/docs_audit.py score`. Fix each broken link and each orphan file that the command reports.
4. Check consistency. If you change formats, APIs, `config.txt` keys, the menu, Settings, or the behavior of one kernel (Pi or RGB30):
   - Run `python scripts/consistency.py check`.
   - Update `docs/spec/` (`specs.toml`, `config-keys.toml`, `divergences.toml`). The rules are in `docs/spec/CONSISTENCY.md`.
   - Before version 1.0.0, `python scripts/consistency.py gate` must pass.
5. Check CI. If you change `.github/workflows/` or test timeouts, run `python scripts/ci_audit.py`.

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
