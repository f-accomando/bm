---
name: bm-review
description: Independent review of an operator's delivery (DELIVERED row, branch@commit) against its work order. Use when SPV must verify a delivery before writing REVIEW: APPROVED or CHANGES REQUESTED, or when asked to review a branch, a commit or a work order result.
---

# bm Review: independent check of a delivery

A review answers one question: does the delivered code do what the work order (WO) asks, and does the evidence hold? The reviewer works from the code and the commands it runs itself, never from the delivery note.

## 1. The brief (what the reviewer receives)
Give the reviewer: the WO file path, the delivery (`branch@commit`), and the repository. Keep it to these facts. The reviewer reaches its own verdict, so the brief carries the facts and leaves the verdict out.
- Use a `general-purpose` subagent; the cheaper `bm-*` agents have no shell.
- Reviewer works in a temporary worktree outside the repository (`git worktree add ../rv-<id> <commit> --detach`) and removes it at the end. It pushes nothing and leaves the main checkout as it found it.
- Python from PowerShell, Git Bash or WSL (`wsl -d Ubuntu`); in Git Bash prefix `git show <ref>:<path>` with `MSYS_NO_PATHCONV=1`.

## 2. The checklist (the reviewer runs all of it)
1. **Perimeter**: `git diff --name-only <base>...<commit>` stays inside the WO's `Perimetro` and touches nothing under `NON toccare`. Once `tools/sync_perimeter.py` is on `bm-core` (AGY-5), `python tools/sync_perimeter.py --wo <WO> --section <ID> --diff <base>...<commit>` gives the same answer mechanically.
2. **Every WO point is done**, one by one, and not only the verification commands. List each point with the file and line that realises it.
3. **Run the WO's verification commands** and report the real output and exit code.
4. **Read the code for flaws**: edge cases, tests that pass without proving anything (an expected value filled in from the output), checks that can be bypassed, a regex or filter that is too wide or too narrow.
5. **Open the evidence**: every `file:line` citation is opened with 3 lines around it and must say what the claim says; sample at least 8, or all when there are fewer than 20. A citation that points at a blank line, a closing brace or unrelated code counts as a defect.
6. **Compare the time with the estimate**: a delivery 10 times faster than the WO's estimate is looked at twice. Find what was skipped.
7. **Mutation check** for code with tests: restore the old code (or break the fix) in the worktree and rerun the new tests; the tests that guard the fix must fail. Say which ones do not.
8. **Hardware claims**: a pass in QEMU does not prove behaviour that depends on RAM that survives, timing, or real devices. The review lists what needs the real board, for the user.

## 3. The answer (at most 25 lines, in the user's language)
- Verdict: `APPROVED` or `CHANGES REQUESTED`, first line.
- Defects with `file:line`, most serious first; minor notes separately, marked non-blocking.
- Commands run and their results.
- Items to prove on hardware, if any.

## 4. What SPV does with it
- Writes one `REVIEW:` row in `SYNC.md` (Log row at most 600 characters) with the verdict and the defects; fixes go to a correction WO (`AGY-6` is the model: one section per delivery, same branch).
- Appends one ledger row per reviewer (`docs/agents/ledger.txt`: tokens, tools, seconds from the notification).
- Before the second review of the same delivery, checks that the fix is verified by running the tests against the old code (item 7).
