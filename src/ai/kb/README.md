# Assistant knowledge base (M30)

Italian version: [README-IT.md](README-IT.md).

What the assistant knows: one `.txt` file per topic, compiled into the kernel by
`scripts/mkassist.py` together with the trained network (`src/ai/assist.weights`).
`tests.txt` contains the test questions (never used for training).

## An entry

```
== move_arrows
kind: howto
title: Muovere un personaggio con le frecce
ask: come muovo il personaggio con le frecce | muovere uno sprite con i tasti
ask: move the player with the arrow keys
keys: muovere, frecce, move
see: spr, move_8dir
text:
btn(0..3) sono sinistra, destra, su, giù: finché il tasto è premuto si
cambia la posizione.
code:
if btn(0) then x = x - 2 end
if btn(1) then x = x + 2 end
```

| Field | What |
|---|---|
| `== id` | start of the entry: lowercase letters, digits, `_` and `.`, unique |
| `kind:` | `api` (a function), `howto` (an example with code), `error` (a Lua error message), `tip` (a piece of advice), `guide` (how to make a 2D or 3D game with the SDK, in steps: one paragraph per step, code optional; `guide_sdk.txt`, the panel's `guide` mode that the SDK opens from the project page), `sprite` (a sprite recipe), `mesh` (a 3D recipe: bm Studio, bm Animator), `action` (something to do on the code: the `#entry:` lines), `none` (off-topic questions: not shown, they serve to say "I don't know") |
| `name:` | for `api` entries: the function name. If it is written in the question or is the word under the cursor, the entry goes to the top (a name that is also a common word, like `tempo`, counts only under the cursor or written as a call, `tempo(`: list in `src/ai/assist.c`) |
| `title:` | the line in the list of answers |
| `title_en:` | the same in English (R18): required for every entry that has `title:` |
| `ask:` | questions that lead here, separated by `\|`, in Italian and in English: they are the examples the network is trained on. The more and the more varied, the better (6-12) |
| `keys:` | words that, if present in the question, raise the score a little |
| `see:` | other linked entries (they must exist): shown below the answer |
| `text:` | the explanation, over several lines, up to the next field; accented letters are fine (the font is CP437; `È` becomes `E'`) |
| `text_en:` | the explanation in English, like `text:` (same paragraphs): required when there is `text:`. The panel answers in the language of the question (`ai_lang_of` in `assist.c`), Ctrl+E switches it, `assist_lang` in `bm/config.txt` fixes it; ASCII only |
| `code:` | the code that "Enter" inserts into the editor: ASCII, lines of at most 72 characters, must compile with Lua 5.4 (`make test-ai` checks it) |
| `gen:` | for `sprite` entries: the recipe in `src/ai/sprite.c` (`ship`, `slime`, `coin`...); for `mesh` entries: the one in `src/ai/mesh.c` (`cube`, `house`, `hero`, `mech`...) |

## After a change

- `make`: rebuilds `build/assist.bin`. A new entry is found right away through its
  words (`name`, `keys`), not yet through the network.
- `make ai-model` (needs numpy: `pip install numpy`): retrains the network on all
  entries, in about a minute, and prints how many questions in `tests.txt` get the
  right answer in first place. Then `make` and commit `src/ai/assist.weights`.
- `make test-ai`: the C network identical to the Python one bit for bit, at least 90%
  of the test questions with the right answer among the first three, every code
  example compiling, the panel.
