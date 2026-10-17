# DIV-1 - divergences.toml: D03, D05 and D11 after the decisions of 17 Oct
SPV assigns it. Documents only (one TOML file). Independent of D05-STICK-a/b, but D05-STICK-b edits the same D05 block afterwards: DIV-1 goes first.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, then `git worktree add -b claude/spec-divergences-d03-d05-d11 C:\Users\Utente\Documents\GitHub\bm33-div1 origin/claude/integrazione-ottobre`); branch `claude/spec-divergences-d03-d05-d11`. Never push to bm-core; push only this branch. The worktree is NOT `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`.
Scope: `docs/spec/divergences.toml` only. DO NOT touch D09 and D10 (still to discuss), any other block, `docs/spec/CONSISTENCY.md`, `scripts/consistency.py`, `docs/spec/known.toml`.
Objective: the three blocks say what the user decided on 17 Oct 2026, and `python scripts/consistency.py check` exits 0.

## Proof (opened on `origin/claude/integrazione-ottobre`)
- Blocks: D03 is `docs/spec/divergences.toml:33-43` (`id` at `:34`), D05 `docs/spec/divergences.toml:56-66` (`id` at `:57`), D11 `docs/spec/divergences.toml:127-136` (`id` at `:128`). Re-find: `git grep -n '^id = "D\(03\|05\|11\)"' -- docs/spec/divergences.toml`.
- The rule: a hardware difference needs `capability` naming an `id` of `docs/spec/platforms.toml` (`scripts/consistency.py:231`); `status` must be one of open, suspended, untested, resolved (`scripts/consistency.py:233`); only the kinds history, todo, unknown block 1.0.0 (`scripts/consistency.py:275`). So "accepted" is written as `cause = "hardware"` + `capability` + `status = "open"` + `action = "none: ..."`, exactly as D30 (`docs/spec/divergences.toml:329-338`) and D31 do. Capability `pad-only` exists: `docs/spec/platforms.toml:32`.
- Evidence files must exist (`scripts/consistency.py:235-238`): keep each block's `evidence` as it is.

## The change (replace the block's fields as listed; keep `id`, `area`, `pi`, `evidence`)
D03 (accepted, hardware):
- `rgb30 = "one player only (its own pad); Settings shows only Player 1; a shared or split screen over HDMI may add local players later (a future exception)"`
- `cause = "hardware"`, add `capability = "pad-only"`, delete the `priority` line, `status = "open"`
- `note = "accepted 2026-10-17 (user): the RGB30 is one player locally. The exception, a shared screen or split screen over HDMI with more pads, is a future decision and gets its own divergence when it is started."`
- `action = "none: keep the docs honest"`

D05 (decided, to build with D05-STICK):
- `rgb30 = "no menu pointer (the kernel pointer is for a real mouse only); the stick is a pointer only in apps and games that call the stick API, when Settings > Controllers > Stick as pointer is on; Settings still offers the mouse rows"`
- `cause = "todo"`, `priority = 3`, `status = "open"`
- `note = "decided 2026-10-17 (user): the kernel pointer (M32, src/kernel/pointer.h) stays for the real mouse and its code stays; the RGB30 stick works as a pointer through an on|off option and only in apps and games that have the API. Built by WO D05-STICK-a (key and Settings row) and D05-STICK-b (API and docs); D05-STICK-b turns this block into cause hardware, capability pad-only, action none."`
- `action = "D05-STICK-a and D05-STICK-b"`

D11 (Send yes, Publish off):
- `rgb30` unchanged; `cause = "history"`, `priority = 6`, `status = "open"` unchanged
- add `note = "decided 2026-10-17 (user): the RGB30 gets Send to a nearby console; Publish stays off for now (it can be added later). The tool rows (Open in the tools, editable copy, Build) depend on D13."`
- `action = "add Send to a nearby console to the RGB30 game options; Publish stays off for now; the tool rows depend on D13"`
Field order as in the neighbouring blocks (id, area, pi, rgb30, cause, [capability], [priority], status, note, evidence, action).

## Verification
- `python scripts/consistency.py check` -> exit 0 (run it before the change too: note the "N drift, M accepted" line and compare). If it fails because `docs/spec/CONSISTENCY.md` lists D03/D05/D11 by hand (`docs/spec/CONSISTENCY.md:113`, `:115`, `:116`): report `BLOCKED: CONSISTENCY.md` instead of editing it.
- `python scripts/consistency.py gate` lists D05 and D11 as blockers and no longer D03 (hardware): paste the lines about D03/D05/D11.
- `git diff --stat`: 1 file; D09 and D10 blocks untouched (`git diff -U0 | grep -c 'D09\|D10'` -> 0).
- `python tools/wo_lines_check.py sync/wo/DIV-1.md --repo <export of the base branch>`: 0 errors.

## Delivery
`DELIVERED DIV-1 @ claude/spec-divergences-d03-d05-d11@<sha> · verifica: python scripts/consistency.py check -> exit 0 · 1 file · non verificato su hardware · <ISO>`. One commit `spec: divergences D03 accepted, D05 stick pointer, D11 Send` (last line `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`). Push only the branch.
Designated agent: bm-light (text given, a rule to respect). Estimate halved: 10 minutes real (5 edit, 5 check and delivery). Task <= 45 min.

## Open decisions (for SPV)
1. D05 stays `todo` (blocks 1.0.0) until D05-STICK-b flips it; if SPV prefers `hardware` at once, D05-STICK-b drops its step 3.
2. D03's future exception (HDMI shared or split screen) is only written in the note; it needs its own divergence when started.
3. D09 and D10 not touched, as asked.
