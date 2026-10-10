# Typing with the pad (assistive typing)

Italian version: [PADTYPE-IT.md](PADTYPE-IT.md).

Write **Italian, English and Lua with the controller alone**, in two ways:

- **Quick composition** (*compose*): the d-pad writes the consonants and the prediction finishes
  the syllable (↑ = "t", and the dictionary proposes "te", "ta", "tr"...); □ and △ rotate the
  syllable through the others; three whole words are always within reach of R2.
- **On-screen keyboard** (*keyboard*): a grid of keys, one character at a time with
  the d-pad, as on consoles.

**Share** switches from one to the other (in bm Code it turns typing on and off). While
composition is on, a **controller overlay** shows what the four
arrows and the four buttons would write; holding L2, R2 or both shows their proposals.

The examples on this page are mostly Italian words ("ciao", "che", "domani"...), since
Italian is the main language of the rules; they are kept as they are.

- **Where**: in **bm Code** (Dev tab), Share; in the **Pad Typing** cartridge (Games
  tab), which is for practising and measuring yourself; in every cartridge with
  `require "padtype"` (`src/ai/padtype.lua`, in the kernel).
- **Numbers** (section 6): on the exercise texts, with word suggestions, **1.09
  presses per character** in Italian, 1.06 in English, 1.33 in Lua; with the on-screen
  keyboard 3.8–4.0.

It takes up the idea of the `archive/pad-typing` branch (chords of d-pad and buttons), with new
rules decided by the user on 2026-10-04: only four directions, the double press, the
rotation of syllables, L1/R1 to move. The rules below are the starting ones:
the tables are at the top of `padtype.lua` (`CROSS`, `FACE`, `KB`) and are changed there.

## 1. Quick composition

### The rules

- **Only up, down, right, left.** Diagonals do not write: a press counts when
  the d-pad settles on a single direction (going from ↑ to → writes the →).
- **The press waits for its double.** An arrow writes after **X ms** (250 by
  default; in the cartridge from 150 to 600): if in the meantime the same arrow goes down again,
  it is the **double**, which gives the *alternative* consonant, never the same one twice (↑ = t,
  quick ↑↑ = d). Any other button does not wait: it fires the first one immediately. For two
  "t" in a row, wait a moment between one and the other (but "tt" is usually already proposed by
  the syllable: "tu" + ↑ = "tutto" → "tto").
- **The prediction completes the syllable.** A consonant also writes what the
  dictionary expects after it: its double, an h, the vowels up to the next
  consonant ("c" in "ciao" becomes "ciao", in "che" "che", "t" after "tu" "tto"). If
  the dictionary expects another consonant, the consonant stays alone ("t" of "tre").
- **□ and △ rotate the syllable** just written (□ the next one, △ the previous one): first the
  dictionary's proposals, from the most likely, then the consonant alone and the vowels in
  order (a e i o u), in Italian last the accented ones not proposed (ù ì ò é à è: △
  from the start gives è). The rotating syllable is light blue; the overlay shows its neighbours.
- **A vowel at the start of a word**: □ (or △) when there is no open syllable; R1 closes
  the open one if a new vowel is needed.
- **○ deletes the last thing written**: a syllable, a word taken from R2, the space, the
  punctuation (and puts back what was there before); if it does not know what, one character. Held,
  it goes on one character at a time.
- **✕ is space, ✕✕ the full stop** (". " with a capital after it; in code "." attached:
  "math" ✕✕ "floor").

### The d-pad

| D-pad | single | double | L2 | L2 double | R2 | R2 double | L2+R2 | L2+R2 double |
|---|---|---|---|---|---|---|---|---|
| ↑ | **t** | d | p | b | k | q | 1 | 5 |
| → | **n** | m | f | v | w | y | 2 | 6 |
| ↓ | **r** | l | g | h | j | _ | 3 | 7 |
| ← | **s** | c | z | x | [ | ] | 4 | 8 |

### The four buttons

| Button | single | double | L2 | L2 double | R2 | L2+R2 | L2+R2 double |
|---|---|---|---|---|---|---|---|
| ✕ | space | full stop | , | ; | 1st word | ( | ) |
| ○ | delete | | ? | ! | delete the word | 0 | |
| □ | next syllable | | ' | " | 2nd word | = | + |
| △ | previous syllable | | : | - | 3rd word | { | } |

The 0 is ○ with L2+R2 (it repeats: 100 = 1, ○, ○); the 9 is the 8 rotated with □ (or the 0 with △):
digits rotate too.

### The other buttons

| Button | Does |
|---|---|
| L1 | back one character (held: continues) |
| R1 | forward; with an open syllable it accepts it as it is |
| L1 or R1 held + d-pad | the cursor in the four directions (up and down the lines) |
| L1 + R1 held | new line (in code with indentation: two more spaces after `then`, `do`, `function(...)`, `{`; `end`, `else`, `until` go back by themselves) |
| L3 | capital for the next letter (on an open syllable it changes it); at the start of a sentence it is automatic |
| R3 | the on-screen keyboard for **one** character (rare symbols: `* / < > # % ~`...), then composition comes back |
| Share | the on-screen keyboard (in bm Code: turns off) |
| Start | left to the application (pause, menu) |

### Why these consonants

Counted as syllable starts (consonant + double + h + vowels, the composition
rule) on the texts of `src/ai/words` and on the games' Lua, weighted ½ Italian, ¼
English, ¼ Lua:

n 12.1% · t 11.7 · r 10.1 · s 8.7 · l 8.4 · c 7.0 · d 5.9 · p 4.8 · m 4.6 · f 3.0 · g 2.8 ·
v 2.5 · b 2.1 · w 1.2 · k 0.9 · x 0.9 · h 0.8 · z 0.6 · y 0.5 · q 0.5 · j 0.1 (a vowel
at the start of a syllable 10.6%).

- **Frequency**: the eight most written (n t r s l c d p: 69% of syllables) have no
  trigger; the first four with a single press, four with the double.
- **Intuitiveness**: the double is the *twin* of the single: t/d (the same one voiced),
  n/m (nasals), r/l (liquids), s/c (the "s" sound: *city*, "sc"). Each direction keeps
  its family across the levels: ↑ the plosives (t d · p b · k q), → the nasals and the soft
  sounds (n m · f v · w y), ↓ r l · g h · j, ← the sibilants (s c · z x). L2 are the
  next eight, R2 the rare letters of English and code, L2+R2 the numbers clockwise
  from ↑ (1 2 3 4, doubles 5 6 7 8).
- **Comfort**: L2 with the d-pad is done with one hand (left index finger and thumb); the
  words (R2 + ✕ □ △) with the right. The h is hardly needed alone: it comes with the syllable
  (che, chi, the, she). Italian double consonants come with the syllable (tto, zza, lla).

### The words (R2)

Holding R2, ✕ □ △ write the **1st, 2nd, 3rd proposed word** (the same ones are always in the
centre of the overlay, and the rest of the first one shows in blue-grey after the cursor):
those that finish the word being written or, after a space, those that most often
follow it ("Ciao Marco, doma" → domani). The word has its accent and its capital, and
a **"soft" space**: the punctuation that follows attaches to the word ("domani" +
L2 ✕ = "domani, "), ✕ after it does not add another one. In code, functions bring their
parenthesis (`cl` → `cls(`), keywords the space. R2 + ○ deletes the word
before the cursor.

The words come from bm Code's completion (`require "predict"`, guide in
[PREDICT.md](PREDICT.md)) and follow where the cursor is: in Lua code, after `--` and
in strings Italian or English, in `#entry:` lines the questions to the assistant.

## 2. The on-screen keyboard

```
1 2 3 4 5 6 7 8 9 0          ! " # $ % & / ( ) =
q w e r t y u i o p          + - * < > [ ] { } _
a s d f g h j k l '          à è é ì ò ù ; : @ \
z x c v b n m , . ?          | ^ ~ ` « » ° £ ± ß
shift  #+  space  del enter
```

The d-pad moves the lit key (held, it continues; at the edges it wraps), **✕** writes it,
**□** space, **○** deletes, **△** changes page (letters / symbols), **L2** (or L3)
capital, R2 + ✕ □ △ the words as in composition, L1 / R1 and L1 + R1 as above.
The capital at the start of a sentence is automatic. Share goes back to composition (R3 too).

## 3. The overlay

340 pixels wide and 132 high (`pt.size()`), drawn by the application wherever it wants
(`pt.draw(x, y)`):

- at the top L2, L1, R1, R2 (lit when held) and the level (abc, L2, R2 words,
  123 code);
- on the left the **d-pad**: for each arrow the syllable it would write now ("to", "sa":
  the prediction) and, small, the double; the arrow waiting for the double is yellow;
- on the right the **four buttons** in the colours of the DS4 symbols (△ green, ○ red, ✕ blue,
  □ pink), with what they do in the held level and, small, the double;
- in the centre the **three words** of R2;
- at the bottom the **rotating syllable** with its neighbours (□ forward, △ back), or the buttons
  L3, R3, L1+R1, Share.

With the on-screen keyboard the overlay is the keyboard, with the words below.

## 4. bm Code

**Share** turns on typing with the pad (or the menu, *Pad typing*): the overlay appears below
the code, the status line says `PAD compose lua` (or `it`, `en`, `ask` depending on the place),
at the cursor you see the press that is waiting (yellow), the rest of the word (blue-grey),
the rotating syllable (light blue) and what a word has just written (green). Start
opens the menu; Share turns it off and the d-pad goes back to moving the cursor. The PC keyboard
keeps working at the same time.

## 5. Pad Typing (the cartridge)

`carts/typing`, in the Games tab: typing practice and test.

- **Menu**: language (Italiano, English, Lua), initial mode (compose or keyboard), wait
  for the double (150–600 ms), text (eight per language, written for bm, or *free writing*),
  next key shown yes / no; below, the records of each language. Start begins.
- **Exercise**: the text to copy; what is written correctly turns white, an error
  red on a dark background, the rotating syllable light blue, the rest of the proposed word
  blue-grey. Below, **next:** the next key (the pad icons, ×2 for the double),
  computed by `pt.coach`. On the left mode, time, characters per minute, presses (and per
  character); on the right the main buttons.
- **End**: time, characters per minute, presses, presses per character, how many times ○
  deleted, with "best!" on the records (saved with `save()`). ✕ next text, △ again,
  ○ menu. Start during the exercise is the pause (Continue, Start again, Next text, Menu).

## 6. Numbers

`make test-padtype`: a simulated typist presses what `pt.coach` says
frame by frame (a two-frame press, trigger released between one button
and the next: every trigger counts), on all eight texts of each language:

| Language | Characters | Composition | On-screen keyboard |
|---|---|---|---|
| Italian | 837 | 909 presses, **1.09** per character | 3 202, 3.83 |
| English | 806 | 851, **1.06** | 2 968, 3.68 |
| Lua | 717 | 955, **1.33** | 2 887, 4.03 |

Without the R2 words composition does 1.27 in Italian: the words remove 15%.
Where the presses go in Italian: a third to rotating syllables (□ △), a third to
consonants (doubles and triggers included), the rest to R2 words, spaces and punctuation.
Pad Typing in bmhost, with the first text of each language: 1.04 (it), 1.18 (en), 1.08 (Lua)
presses per character.

## 7. From Lua

```lua
local pt = require "padtype"
pt.set({ delay = 0.25, fallback = "keyboard" })   -- o "off": Share spegne
pt.on("compose")                                   -- "keyboard", nil: spenta
local host = pt.text_host("it")                    -- un testo; o un host proprio:
--   host.before()  il testo prima del cursore, sulla sua riga
--   host.insert(s), host.erase(n), host.newline(), host.move(dir)
--   host.lang      "it", "en", "lua", "ask", {it = 1, ask = 2} o "none"
--   host.prose     maiuscole a inizio frase (di default: non per "lua")
--   host.words     i nomi del codice (predict.count_words)
function _update()
  if not pt.update(host) then
    -- il pad è dell'applicazione (spenta)
  end
end
function _draw()
  -- il testo; al cursore pt.pending() (P.C_PEND), pt.ghost() (C_GHOST),
  -- le pt.open_len() lettere prima (C_OPEN), le pt.flash() (C_PRED)
  pt.draw(150, 226)
end
```

Other functions: `pt.mode()`, `pt.presses()` / `pt.reset_count()`, `pt.clear()` (a new
text), `pt.idle(host)` (the frames in which a menu has the pad), `pt.commit()` (the
waiting press, right away), `pt.suggestions()`, `pt.syllables(host, prima, consonante)`,
`pt.coach(host, testo)` (the next key: `{hold, tap, double, both, wait, label}`),
`pt.align(scritto, testo)` (the Lua compared as if `end` were already dedented),
`pt.preload(lingue)`, `pt.TEXTS` (the exercise texts), `pt.CROSS`, `pt.FACE`, `pt.KB`.

## 8. Files and tests

- `src/ai/padtype.lua` (`require "padtype"`, in the kernel, in bmhost and in the RGB30
  image): rules, syllables, keyboard, overlay, `coach`, `text_host`, the texts.
- `carts/typing/main.lua` (Pad Typing, cover from `scripts/mkcovers.py`);
  `carts/code/main.lua` (bm Code: `pad_host`, Share).
- `tests/padtype/pad_test.lua` (`make test-padtype`, in `make test`): the rules one by
  one (wait and double, levels, syllables and rotation, ○, words, punctuation, L1/R1,
  L1+R1, L3, Share, R3, diagonals) and all the texts written to the end in both modes,
  with the limits on presses per character.
- `tests/padtype/script.lua`: the same typist writes bmhost's input
  script; `make test-padtype` makes Pad Typing copy the first text of each language up
  to the result (frames in `build/padtype/`).
- `tests/qemu_test.py`, `test_pad_typing`: a simulated DS4 in QEMU, Pad Typing from the
  Games tab (syllable, double, ○, numbers, full stop, on-screen keyboard, word, new line,
  pause, exercise) and Share in bm Code.

## 9. Limits and next steps

- To be tried on the Pi with a DS4: the right wait for the double (250 ms is a guess), the
  weight of rotations, whether the s/c and r/l doubles come naturally.
- The RGB30: the bits of `pad()` are the button positions, the overlay uses the DS4 colours;
  it has not been tried there yet.
- A kernel service to type from any screen (WiFi password, file
  names: roadmap idea R2) could use this keyboard and composition.
- The user's new words in the dictionary; the assistant panel and bm Code's
  prompts (Find, Go to line) still without typing with the pad.
