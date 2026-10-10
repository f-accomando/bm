# Word completion (M30)

Italian version: [PREDICT-IT.md](PREDICT-IT.md).

While you type a word on the keyboard, bm suggests the most likely one and shows **the rest
of it in blue-grey** after the cursor; **Tab** types it, and the typed part stays **green**
until the next key. If you don't need it, just keep typing: the suggestion changes with every
letter and disappears on its own.

- **Where**: in **bm Code** (Dev tab) in code, comments, strings and `#entry:` lines; in
  **Find** and **Replace**; in the question of the **assistant panel** (F6).
- **Status line** of bm Code: `Tab: word  (also: other, two)`, the word Tab would type and
  the others the dictionary had in mind (you get them by typing another letter).
- **Tab before a word** (start of line, after a space) indents as before; in the assistant
  panel, with no suggestion, it switches mode (code, sprite, any).
- **Menu** of bm Code (Esc): **Word completion** on/off, **Words in comments** it/en (the
  words of comments and strings). Both choices last for the session.
- **Module**: `require "predict"` (`src/ai/predict.lua`), any cartridge can use it; the
  dictionaries are `require "words"`, built by `scripts/mkwords.py`.

On an Italian text of 100 characters (100 keys, a capital letter with Shift counts as one)
completion saves **17**, on Lua code **19%**, on questions to the assistant **26%** (full
numbers below).

## 1. Where you type, which words

The examples of words below are kept in Italian, the main language of the dictionaries.
The dictionary changes by itself with the place where the cursor is:

| Where | Words | Examples |
|---|---|---|
| code in bm Code | Lua: keywords, API of the assistant's knowledge base, names from bm's games, names in the open tab | `f` → `function` on a non-indented line, `for` on an indented one; `if bt` → `btnp`; `x = ma` → `math.random`; `string.fo` → `string.format` |
| after `--` (comment) | Italian (or English, from the menu), some of the tab's names | `-- muovi il gioc` → gioco |
| in a string | Italian (or English) | |
| `#entry:` line | Italian + the knowledge base questions | `#entry: come faccio a sal` → salvare |
| assistant panel | Italian + the knowledge base questions | `co` → come |
| Find, Replace | Lua and the tab's names | `_up` → `_update` |
| Go to line, file names | none | |

In comments and strings the context starts at the mark (`--`, `"`): the first word does not
depend on the code before it. **Questions to the assistant** have their own dictionary: the
`ask:` lines, titles and keywords of the knowledge base (`src/ai/kb`, not `tests.txt`), mixed
with Italian at a 2 to 1 weight.

Rules for the typed word:

- **Accents**: not needed to find it, Tab adds them: `perch` → perché, `citt` → città.
- **Capitals**: a capital at the start stays (`Con` → Console); all caps stays all caps
  (`CA` → CASA). Proper names in the dictionary have their own capital (`marc` → Marco). In
  code the dictionary decides (Lua is case-sensitive).
- **Tab types only the word**: no space or parenthesis after it, so the next key is always
  the one you expect.
- `end`, `else`, `until` typed with Tab go back to their block's indentation, as when typed
  by hand; Tab and the word typed before it are a single Ctrl+Z step.

## 2. The model

An **n-gram**: how many times each word is written and which words follow it, counted by
`scripts/mkwords.py` (`make` redoes it when the texts change) on:

- **it**: the texts in `src/ai/words` written for bm (stories, messages, games, technical,
  letters, informative: 23 000 words) and the lexicon `it_lessico.txt` (6 500 forms missing
  from the texts): 8 450 words;
- **en**: the English texts in `src/ai/words` (4 500 words): 1 140 words;
- **lua**: the Lua of bm's games (without comments and strings), the knowledge base code and
  all the names of its APIs with their signature: 3 350 words;
- **ask**: the knowledge base questions: 1 770 words.

The score of a word that starts like the typed one is `0.4 × frequency + how many times it
follows the previous word`; in code, "the previous word" of a line that is starting is `^`
if the line is indented, `^0` if it is not (there one types `function` and `local`, inside
`for`, `if`, `local`). In code the names of the open tab are added, counted every 30
suggestions.

On the Pi: the dictionary is read **one piece per frame** from when bm Code opens
(`predict.preload`), so the first word finds it ready; a suggestion is a binary search in the
sorted table, and for one- or two-letter prefixes (which match a thousand words) the eight
most written are kept plus those following the previous word. On the PC: 0.02 ms per
suggestion, 14 ms to read the Lua dictionary (on the Pi Zero about twenty times more, spread
over the frames).

The format of `build/words.lua` (text in code page 437, like the console):

- `uni`: `word count[ f]` per line, sorted by key (lowercase, without accents); ` f` = a
  function;
- `big`: `prev total next n next n ...` per line, the 24 most frequent words after `prev`;
- `api` (lua only): `name signature` per line.

### From Lua

```lua
local predict = require "predict"
local c = predict.complete(testo_prima_del_cursore, { lang = "lua", words = predict.count_words(righe) })
if c then
  print(c.rest, x, y, predict.C_GHOST)      -- the rest, in blue-grey
  -- Tab: c.prefix (what is typed) becomes c.word
end
```

`lang` is a dictionary (`"it"`, `"en"`, `"lua"`, `"ask"`), a mix with weights
(`{it = 1, ask = 2}`) or `"none"`; `words` and `words_weight` the code's names; `min` the
shortest prefix (1). The result also has `list` (the first three) and `ending` (`(` after a
function, a space after a keyword or a text word), for those who want them. Other
functions: `candidates`, `word_at`, `count_words`, `preload`, `plain`, `from_utf8`.

## 3. Benchmark

`make predict-bench` (`tests/predict/bench.lua`): the keys to type the texts of
`tests/predict/texts.lua` with the bm Code keyboard, first without and then with Tab (pressed
when the suggestion is the word of the text, or its start, and types at least two letters).
In code Enter keeps the indentation.

| Text | Characters | Keys | Keys with Tab | Tab | Saving |
|---|---|---|---|---|---|
| italiano | 100 | 100 | 83 | 7 | 17% |
| english | 100 | 100 | 88 | 7 | 12% |
| lua | 100 | 94 | 76 | 7 | 19% |

- The **155 never-seen questions to the assistant** (`src/ai/kb/tests.txt`, 5 332
  characters): 1 key per character without completion, **0.885** with the Italian
  dictionary, **0.738** with Italian + questions (26% fewer).
- The **texts in `src/ai/words` the dictionary has not seen** (each group removed in turn
  from the dictionary, 136 245 characters): **0.868** keys per character, 13% fewer. On seen
  texts the saving is higher, but it does not count.

The first 10 words of the Italian text (57 characters, 47 keys, 4 Tab):

| Word | Keys | How many |
|---|---|---|
| Ciao | C i a o | 4 of 4 |
| Marco, | M a r c o , | 6 of 6 |
| domani | d o m Tab (ani) | 4 of 6 |
| sera | s e Tab (ra) | 3 of 4 |
| giochiamo | g i o c h i Tab (amo) | 7 of 9 |
| da | d a | 2 of 2 |
| me? | m e ? | 3 of 3 |
| La | L a | 2 of 2 |
| console | c Tab (onsole) | 2 of 7 |
| nuova | n u o v a | 5 of 5 |

The space after each word is one key.

## 4. Italian syllables

`make syllables` (`scripts/syllables.py`) counts them, accents excluded, on the same texts
(23 118 words) and on the lexicon (8 523 distinct words), split with the spelling rules
(ca-sa, piz-za, can-to, pa-dre, pa-e-se, pia-no):

- **In theory**: 16 Italian consonants (b c d f g h l m n p q r s t v z) times 5 vowels make
  **80 consonant + vowel syllables**, **85** with the vowels alone; plus **31** written with
  two or three letters for one sound (che chi ghe ghi; gna gne gni gno gnu;
  glia glie gli glio gliu; sce sci scia scio sciu sche schi; cia cio ciu gia gio giu;
  qua que qui quo).
- **In the texts**: 46 251 syllables (2 per word), **1 078 distinct** (1 277 with the
  lexicon).
- **A few do almost everything**: the **35** most frequent are **half** of the written
  syllables, 130 are 80%, 247 are 90%, 382 are 95%, 727 are 99%.
- **Consonant + vowel** is **52.8%** of syllables, a lone vowel 6.4%.
- Vowels: a 25%, e 24%, o 22%, i 18%, u 5%, then the diphthongs ia, io, ie, uo. End of the
  syllable: open 75%, n 8%, l 5%, r 4.5%.

In groups of four, by frequency:

| Group | Syllables | Together | Total |
|---|---|---|---|
| 1 | e 2.8%, la 2.7%, to 2.6%, re 2.3% | 10.4% | 10.4% |
| 2 | di 2.3%, no 2.0%, ti 1.9%, ta 1.9% | 8.1% | 18.4% |
| 3 | na 1.7%, le 1.6%, co 1.5%, a 1.5% | 6.4% | 24.8% |
| 4 | che 1.5%, si 1.5%, te 1.3%, ra 1.3% | 5.6% | 30.4% |

| Group | Initial consonant | Together | Total |
|---|---|---|---|
| 1 | s 12.4%, t 12.1%, c 10.3%, n 9.0% | 43.9% | 43.9% |
| 2 | l 8.3%, r 7.8%, d 7.8%, p 7.5% | 31.3% | 75.2% |
| 3 | m 6.7%, v 4.8%, g 4.5%, f 2.5% | 18.5% | 93.7% |
| 4 | b 2.3%, z 1.5%, q 1.2%, h 0.9% | 6.0% | 99.7% |

(the script also gives the groups of consonant + vowel syllables only, and the most frequent
syllable starts and ends).

## 5. Files and tests

- `src/ai/predict.lua` (`require "predict"`): words, dictionaries, suggestions.
- `src/ai/words/`: the Italian and English texts written for bm and the Italian lexicon
  (`README.md`); `scripts/mkwords.py` → `build/words.lua` (`require "words"`, in the kernel,
  ~360 KB of text).
- `carts/code/main.lua`: completion in bm Code (`place_at` picks code, comment, string or
  `#entry:`); `src/ai/assist.lua`: the question's one.
- `tests/predict/predict_test.lua` (`make test-predict`, inside `make test`): words before
  the cursor, dictionaries, accents and capitals, the tab's names, the three texts retyped
  with Tab (`tests/predict/typist.lua`) with fewer keys.
- `tests/predict/bench.lua` (`make predict-bench`): the table, the questions, the first 10
  words and the corpus with the groups removed in turn.
- `tests/ai/panel_test.lua` (`make test-ai`): the question completed with Tab in the panel.
- `tests/qemu_test.py`, `test_code_completion`: bm Code in QEMU (code, comment, `#entry:`,
  Tab that indents, Find, assistant panel; the colours on screen).

## 6. Limits and next steps

- The dictionary comes from ~28 000 words: good for messages, comments and games, less so
  for long technical texts. It grows by adding texts to `src/ai/words` (then `make`).
- A word that differs only by its final accent (`e` → è) is not completed: it has the same
  length, there is no rest to show.
- Next: new words typed by the user in the dictionary (saved), completion in the Sound
  editor and in the 3D studio (names, texts).
