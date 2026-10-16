# The PDF manuals

Manuals for the **people who use bm**, not instructions for an AI. Each one exists in English
and in Italian with the same content. Look: theme A (paper / orange); the content never names a
colour or a font.

```
manuals/manuals.toml          the list: id, order, the repository files each one comes from
manuals/content/en/<id>.md    the text, English
manuals/content/it/<id>.md    the text, Italian: the same chapters, steps, tables, boxes, pictures
manuals/themes/<theme>.toml   the look (colours, fonts, sizes, shape of the boxes): paper is the chosen one
manuals/shots/                pictures (console screenshots: docs/SCREENSHOTS.md)
manuals/pdf/                  the finished PDFs, <id>.en.pdf and <id>.it.pdf (make manuals)
manuals/sync.json             what en and it said when they were last stamped as equal
scripts/mkmanual.py           one content file -> PDF
scripts/manuals.py            status, check, stamp, build for all the manuals
```

| Manual (`id`) | For |
|---|---|
| `quickstart` | the first game in one page: title, 3 stages, ending, on the RGB30 and the Pi |
| `install-sd` | putting bm on an SD card: Pi Zero W and RGB30 |
| `boot-os` | what bm is: the boot, the kernel, the menu, the system keys, the files on the card |
| `pi-zero-w` | the Pi Zero W 1.1 as bm uses it: the ARM1176, the VideoCore GPU, the bm3d 2D and 3D drivers |
| `riff` | the RIFF music format |
| `api-core` `api-sound` `api-3d` `api-lib` `api-tools` | the cartridge API in five volumes: drawing, input and system; sound; 3D; bmlib and bmnet; tools and resources |
| `editor-sdk` `editor-code` `editor-pixel` `editor-studio` `editor-animator` `editor-mesh` `editor-sound` `editor-write` | the editor of each app of the suite, one manual each |

## Commands

```
python3 scripts/manuals.py status             what is there, the gaps, what is behind
python3 scripts/manuals.py check              en and it: same shape, neither behind the other
python3 scripts/manuals.py stamp <id>         "en and it say the same now" (after check passes)
python3 scripts/manuals.py build [<id>]       PDFs (default build/manuals; the finished ones are in manuals/pdf: make manuals)
python3 scripts/mkmanual.py <file.md> --all-themes -o DIR     one file in every theme
```

## Changing the text: the flow that keeps the languages together

1. Change one language (`en` is the one to start from).
2. `scripts/manuals.py check` says, chapter by chapter, what is behind: *"it is behind (en changed
   since the stamp)"*, or *"en is behind"*, or that both changed.
3. Bring the other language to the same text: the same chapters, sections, steps, tables, boxes,
   pictures, code and gaps. In code, comments, the text of strings and the values of `key=value` lines may be
   translated. Inline `code` that is an identifier, a file name, a path or a call (`play_at()`, `bm/shots/`)
   must be the same; placeholders (`<NAME>`), examples with spaces and key names may be translated.
4. `check` passes: `scripts/manuals.py stamp <id>`. Commit the two `.md` files and `sync.json` together.
5. `status` also says when a repository file the manual comes from (`sources`) changed after the
   stamp: then check the manual still tells the truth.

## The content format

First lines `key: value`: `title`, `accent` (the word of the title that is highlighted), `kicker`,
`subtitle`, `footer`, `lang` (`en` or `it`). Then:

| Write | Result |
|---|---|
| `# Chapter` `## Section` `### Part` | headings; the contents and the PDF outline are made from them |
| a paragraph; `- item`; `1. step` | text, list, numbered steps |
| `\| a \| b \|` rows | a table |
| three backticks | a code block |
| `![caption](shots/name.png)` | a picture (a gap is shown if the file is missing) |
| `::: readme` `::: note` `::: try` `::: warn` ... `:::` | boxes (readme = the cover box; try = an exercise; warn = a danger) |
| `::: copy` ... `:::` | what the reader copies as it is: commands, paths, code |
| `**bold**` `*italic*` and backticks | inline |
| `[TO FILL: what is missing]` (it: `[DA COMPLETARE: ...]`) | a visible gap, listed in the last chapter "Open points" |

### Rules of the text

- **Only what the repository says.** Where a fact is missing or not tried on the hardware, write a
  gap. Never invent a number, a key or a behaviour.
- **Guided, not approximate.** A "how to" is numbered steps; each step is one action with the key or
  the menu entry named, and says what the reader should see afterwards. Say where to start (which
  console, which screen). Name what can go wrong and what to do.
- **Chapter pattern** for a tool or a feature: what it does, why it matters, good to know, caveats,
  try it step by step (with a `::: try`), then the commands to copy.
- **Writing for the reader**: second person, short sentences, the words of the screens and keys as
  they appear (`Print Screen`, `Select`). No internal milestone codes (M47, R4) in the text.
- **Both languages**: translate the sentences, not the identifiers: file names, function names, keys
  and menu entries stay as they are on the console.

## Open decisions

See [open-decisions.md](open-decisions.md), sections "Theme" and "Manuals structure".
