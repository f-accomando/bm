---
name: bm-manuals
description: Creates, edits and updates the bm PDF manuals (English and Italian, same content, theme A) from manuals/content/. Use when asked to write or change a manual (editors, boot/OS, Pi Zero W, RIFF, API, SD install), to rebuild the PDFs, to change the look of the PDFs (themes), to see which language is behind, or to add a screenshot to a manual. Not for PDFs unrelated to bm's manuals (use the generic pdf skill).
---

# bm manuals (PDF)

The manuals are for **bm's users**, not instructions for Claude. Everything is in `docs/MANUALS.md`
(format, rules of the text, flow); read it before writing. The tools are `scripts/mkmanual.py`
(one file → PDF) and `scripts/manuals.py` (all manuals: `status`, `check`, `stamp`, `build`).

## Edit a manual
1. Read `manuals/manuals.toml` (which `id`, and the repository files it comes from) and the
   two files `manuals/content/{en,it}/<id>.md`.
2. Change the text from the repository (read the real code or doc; `docs/progress.md` says where):
   a fact you cannot confirm is `[TO FILL: ...]` / `[DA COMPLETARE: ...]`, never a guess.
3. Do the same change in the other language: same chapters, steps, tables, boxes, pictures,
   code and inline `code`; translate sentences, not identifiers, file names or key names.
4. `python3 scripts/manuals.py check <id>` until it passes; then `python3 scripts/manuals.py stamp <id>`.
5. `python3 scripts/manuals.py build <id>` and look at the PDF (`pdftoppm -r 60 -png` and Read the
   PNG) before saying it is done. Commit the two `.md` files and `manuals/sync.json` together.

## Change the look
Only `manuals/themes/*.toml` (colours, fonts, sizes, shape of the boxes); never the content.
`scripts/mkmanual.py <file> --all-themes -o DIR` shows one file in every theme. A new look is a
new `.toml`; the default is `paper` (theme A, chosen by the user).

## A picture
Take it on the console (`Print Screen`, `bm/shots/`: `docs/SCREENSHOTS.md`) or in `bmhost`
(`--input` with `N keys 0x46`), copy it to `manuals/shots/<manual>-<what>.png`; the manual cites
`![caption](shots/<manual>-<what>.png)`. A picture that does not exist yet is listed in "Open points".

## Never
- Invent keys, menu entries, numbers or behaviours; milestone codes or dates in the text.
- Let one language get ahead without saying so: `check` must say what is behind.
- Put a colour, a font or a size in a content file.
