# bm (BareMetal): session notes

bm is a bare-metal kernel. It runs on the Raspberry Pi Zero W (BCM2835, ARM1176) and on the PowKiddy RGB30 (RK3566, AArch64; build with `make TARGET=rgb30`). Cartridges are `.bm` files in the `bm/` folder on the SD card. The repository is `f-accomando/bm`.

## 1. Find information

Read files in small slices (`grep`, or `offset` and `limit`). The current state is in [`docs/progress.md`](docs/progress.md) and in the area sheets `docs/progress/<area>.md`.

| Need | Command or file |
|---|---|
| State of `bm-core` (fetch, ahead/behind, clean tree) | `core/scripts/sync-core.sh` (`--ff` to fast-forward `bm-core`); a session hook already fetches once at start, so do not run `git fetch` again |
| What is left to do in an area | `grep -n -A 15 '^## \(Open work\|To do\)' docs/progress/<area>.md` |
| Binding rules of an area | `grep -n -A 20 '^## Rules' docs/progress/<area>.md` |
| Code in `runtime.c` (350 KB) | `python core/scripts/cindex.py core/src/bm/runtime.c <symbol>` |
| Game API | `grep -n -C 2 '<function>' docs/api/<area>.md` (index: `docs/API.md`; patterns: `docs/design/games-architecture.md`) |
| Cartridge format and section numbers | `docs/BM-FORMAT.md`; constants: `grep -n 'BM_SEC_' core/src/bm/bm.h` |
| Keys, menus, and screen rules per device | `docs/spec/ui-reference.md` |

## 2. Rules

**Branches**
- When: Open a branch only when the user asks for development work (feature, fix, or refactor). For questions, analysis, research, and reads, stay on the current branch.
- Name: Use `claude/<descriptive-name>`, even when you start the work on your own initiative. If the purpose, area, or name is not clear, ask the user before you create the branch or change files. If the session names a branch, use only that branch.
- Base: Start from an up-to-date `bm-core` (the main branch).
- Integrate: When the work is done and the build and selective tests pass, merge the branch into `bm-core` with a fast-forward and push `bm-core`. Do not open a pull request. Then delete the branch (remote and local) and its worktree, each by name.

**Selective tests.** Run only the tests that the change affects. Examples: `tests/qemu_test.py -k <name>`, `make test-b3d` (GPU), or a specific unit test. The `kernel7` target is disabled.

**Pi and RGB30 hardware.** Test without a serial connection. Copy `dist/kernel.img` to the SD card with `./easy_install.sh`. The console sends numeric reports to the `reports` branch.

**Licenses and security.** Bare-metal sources contain only original code or code with a permissive license. You can use GPU structures from Mesa (MIT). Keep secrets, such as `*_KEY` and tokens, outside the repository.

**One cartridge type.** The only cartridge type is `.bm`. The header says the kernels (`targets` in `bm.info`). Do not write a `.b16`. The kernels read an old `.b16` as a cartridge for the Pi and the RGB30.

**Projects (`.bme`).** Change only `.bme` projects. `.bm` files are read-only. Start each game or app change from its `.bme` and keep the `.bme` in the repository. Build the `.bm` from the `.bme`. Upload the `.bm` to the Market if possible. If not, send it to the device. If a `.bm` has no `.bme`, read the `.bm` and create its `.bme` first. When you change a `.bme`, update it to the current standards and APIs that it does not use yet. Tell the user in the chat what you integrated.

**Units.** Write sizes as KB, MB, and GB. Each unit is a power of 1024 (1 MB = 1048576 bytes). Do not write KiB, MiB, or GiB.

**Writing rules.** State each rule as an action. Delete a retired rule. A negation keeps the old behavior in view. Add a prohibition only when an action keeps harming development despite the positive rule, and write the harm next to it. Write each new or changed line of this file, and of the files it points to, in ASD-STE100: short sentences, one instruction per sentence, active voice, imperative verbs.

## 3. Task closure

Before you declare a task complete, do these steps:

1. Update the status. Remove the completed items and update the partial items in `## Open work` of the area file `docs/progress/<area>.md`.
2. Update the rules. If you add or change an architectural constraint, record it in `## Rules (do not break)` of the same file.
3. Check integrity. Run `python core/scripts/docs_audit.py score`. Fix each broken link and each orphan file that the command reports.
4. Check consistency. If you change formats, APIs, `config.txt` keys, the menu, Settings, or the behavior of one kernel (Pi or RGB30), run `python core/scripts/consistency.py check` and update `docs/spec/` (rules: `docs/spec/CONSISTENCY.md`). Before version 1.0.0, `python core/scripts/consistency.py gate` must pass.
5. Check CI. If you change `.github/workflows/` or test timeouts, run `python core/scripts/ci_audit.py`.

## Glossary

Run `grep -n -A 14 '^### <Heading>' docs/agents/routing.md`. Read only that section.

- `Roles`: model and task of each agent (`.claude/agents/`).
- `Readiness Gate`: 5 questions to ask before you give a task to `bm-mechanic`, `bm-coder`, or `bm-light`. One "no" sends the task to `bm-planner`.
- `bm-deep`: rules for the heavy agent.
- `Necessity`: score 1-5 to declare before you use an expensive model.
- `Escalation`: closing status of an agent (`DONE`, `BLOCKED`, or `FAILED x2`).
- `Work Order Format`: order format.
- `Ledger`: cost rows.

**Autonomy threshold = 4** (user preference). With N = 1 to 3, use the standard model. With N = 4 or 5, switch to the expensive model without asking. State the score and the reason in the chat. To change the threshold, edit this line. The score is an estimate. The ledger gives measured values.
