# Reading big files: search first, read a stretch

Rule: find the line with a search command, then `Read` with `offset`/`limit` on ±60 lines.
A file read once in a session is not read again. Wide questions ("where is X used") go to an
`Explore` agent that returns the conclusion.

## Commands per file

| File | Find | Then |
|---|---|---|
| `src/bm/runtime.c` | [runtime](runtime.md) | `Read` the stretch |
| any `.c`/`.lua` file | `python scripts/cindex.py <file> [word]`: banners and functions with line | `Read` offset/limit |
| `src/kernel/menu_ui.c`, `settings.c` | `Grep -n '"<visible text>"'` or `Grep -n '^static .*(' <file>` | ±60 lines |
| `src/script/bm3d.lua`, `bmlib.lua`, `bmui.lua` | `Grep -n '^function\|^local function\|^P\.'` | the function |
| any `docs/*.md`, `sdk/README.md` | `Grep -n '^#'` | the section |
| `docs/ROADMAP.md` | `Grep -n '^## M47'` (milestone) or `'^## R'` (ideas) | the section |
| API of a game function | `Grep -n '<name>' docs/api/` | the area file section |

## Search habits

- `Grep` with `-n`, a `glob` or `type`, and `head_limit`; never `Read` to look for something.
- One search with an alternation (`a\|b`) beats several.
- Before opening a doc, `Grep -n '^#'` it; open only the section.
- Tests: run the one test the change touches, not the suite.

## Keeping this file

One row per file that is too big to read whole (over ~15 KB). No history.
