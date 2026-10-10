# riff: music written in patterns

Italian version: [RIFF-IT.md](RIFF-IT.md).

**riff** is bm's language for writing music in a few lines of Lua, in the style of
[TidalCycles](https://tidalcycles.org) and [Strudel](https://strudel.cc): rhythms and melodies
in a string (the *mini-notation*), changed by functions that chain together.

```lua
local R = require "riff"

R.code [[
setcpm(30)
drums = s "kick*4, ~ snare, hat*8" :gain(.9)
bass  = note "<c2 a1 f1 g1>" :s "acid" :lpf(sine:range(300, 1800):slow(4))
keys  = chord "<Cm7 Ab Eb Bb>" :s "epiano" :arp("updown") :fast(2) :room(.4)
]]

function _update()
  R.update()                    -- queue the notes of the next moments
end
```

It is a kernel library (`src/script/riff.lua`, like bmlib): it runs in `.bm` files, on the Pi
and on the RGB30, and in bmhost on the PC. It uses bm's synthesizer and instruments (`play()`,
`instruments()`) and the sounds of the cartridge's **bank** by name; you write it and hear it
live in **bm Code** (Ctrl+Enter), and in **bm Sound** (F7, *Riff...*) it becomes a song of the
bank.

## How it works

- Time is counted in **cycles**: a cycle is a bar of four beats, 2 seconds at 120 BPM
  (`setcps(0.5)`, the initial value). `setcpm(30)` = 30 cycles per minute; `bpm(120)` = 120
  beats, four per cycle.
- A **pattern** is a function from an interval of cycles to the events that fall in it: each
  event has its whole (start and end of the note), the part inside the interval and a value
  (`{s = "kick", note = 36, gain = .9, ...}`).
- `R.update()`, called every frame, asks the patterns for the events of a piece of cycle a
  little ahead of the sound clock (about 0.2 s) and queues the notes that start with
  **`play_at()`**: the sound interrupt starts them on time, within a 1.3 ms block, whatever
  the game's frame rate. A slow frame does not lose the beat; after a pause (the game
  suspended) the notes already past are skipped.
- Cost: ~700 Lua instructions per frame on average with four patterns, ~9000 in the frame
  that prepares a piece (every ¼ second): less than a millisecond on the Pi.
- Notes wait in a **queue** (up to 160) and the voice is chosen when the note starts: a free
  one, the tail of a sound, or riff's oldest note.
  It never takes the voices of a bank song or of a sound effect; `R.voices(4, 5, 6, 7)`
  limits the voices it can use.

## The mini-notation

A string is one cycle divided into equal parts:

| Written | What it does | Example |
|---|---|---|
| `a b c d` | a sequence: four parts of the cycle | `"kick hat snare hat"` |
| `~` or `-` | a rest | `"kick ~ kick ~"` |
| `[a b]` | a group: two parts in the space of one | `"kick [hat hat]"` |
| `a, b` | together (also inside `[ ]`) | `"kick*4, ~ snare"`, `"[c,e,g]"` |
| `<a b c>` | one per cycle, in turn | `"<c2 a1 f1 g1>"` |
| `a*3` | three times faster (also `a*<2 4>`) | `"hat*8"` |
| `a/2` | twice as slow | `"pad/2"` |
| `a!3`, `a !` | repeated (three times, once more) | `"kick!3 snare"` |
| `a@3`, `a _ _` | longer: weighs three parts | `"c@3 e"`, `"c _ _ e"` |
| `a?`, `a?0.3` | plays at random, 50% (or 70%) of the time | `"hat*16?"` |
| `a(3,8)`, `a(3,8,2)` | Euclidean rhythm: 3 hits spread over 8 steps (rotated by 2) | `"kick(3,8)"` |
| `{a b c}%4` | polymeter: 4 steps per cycle from the sequence of 3 | `"{kick hat snare}%4"` |
| `a \| b` | one of the two at random, every cycle | `"[snare \| clap]"` |
| `a . b c` | groups separated by a dot (`[a] [b c]`) | `"kick . hat hat hat"` |
| `kick:2` | a variant: two semitones up (for drums) | `"kick:2 kick"` |
| `kit:2` | a drum of the console's kit (samples): 0 `bd` … 7 `cb`, at its own speed | `"kit:0 kit:2"`, `"bd sd hh"` |

Words are notes (`c`, `eb4`, `f#2`: octave 3 if missing, `c3` = 48, `c4` = 60 middle C, as in
Strudel), numbers (`0 2 -1 0.5`) or instrument names. An error says where:
`riff: missing ] at 6 in "a [b c"`.

## Building patterns

Some examples below keep Italian words (`strofa` = verse, `ritornello` = chorus,
`"ritmo rock"` = rock rhythm).

| Function | What it does |
|---|---|
| `s(p)`, `sound(p)` | instruments: a name from the cartridge's bank, a preset (`"kick"`, `"epiano"`, `instruments()`), a number (the bank's sound) |
| `note(p)` | notes: names or MIDI numbers |
| `n(p)` | degrees of a scale (`:scale`); without a scale C major from C3 |
| `chord(p)` | chords: `"<C Am F G7>"`, `"Fmaj7"`, `"Dm7"`, `"Bb"`, `"F#m"`, `"Csus4"`, `"Edim"`... in a voicing close to middle C |
| `seq(a, b, ...)`, `cat(a, b, ...)`, `stack(a, b, ...)` | in sequence within the cycle, one cycle each, together |
| `timecat({3, a}, {1, b})` | in sequence with weights |
| `arrange({4, strofa}, {4, ritornello})` | sections of several cycles, one after the other |
| `run(n)` | `0 1 2 ... n-1` |
| `pure(v)`, `silence`, `mini(s)` | one value per cycle, nothing, a mini-notation string |
| `piece(t)` | a piece from `ai.music` (or from `R.bake`) as a pattern: one cycle every 16 steps |

An argument that is a string is always mini-notation, so almost anything can be a pattern:
`:lpf("<400 2000>")`, `:fast("<1 2>")`, `:scale("<C:major A:minor>")`.

## Changing them

Methods chain with the colon (in Lua `p:fast(2)`; with a string the parentheses can be
omitted: `note "c e g" :s "pluck"`):

| Method | What it does |
|---|---|
| `:fast(n)`, `:slow(n)` | faster, slower |
| `:early(t)`, `:late(t)` | earlier, later (in cycles) |
| `:rev()`, `:palindrome()` | reversed, every other cycle |
| `:ply(n)` | each event repeated n times |
| `:iter(n)`, `:iterBack(n)` | each cycle starts one n-th further on |
| `:every(n, f)`, `:lastOf(n, f)` | f on the first (or last) of every n cycles |
| `:sometimes(f)`, `:often(f)`, `:rarely(f)`, `:almostNever(f)`, `:almostAlways(f)`, `:sometimesBy(x, f)`, `:someCycles(f)` | f on a part of the events (or cycles) at random |
| `:degrade()`, `:degradeBy(x)` | randomly removes half (or a part x) of the events |
| `:euclid(k, n, [rot])`, `:struct("x ~ x x")`, `:mask("1 0 1 1")` | the structure of a rhythm, or only the events where the mask is true |
| `:off(t, f)`, `:superimpose(f)`, `:layer(f, g, ...)` | together with a changed copy (shifted by t) |
| `:jux(f)`, `:juxBy(w, f)` | the pattern on the left, f of the pattern on the right |
| `:chunk(n, f)` | f on one n-th of the cycle, a different one each time |
| `:linger(x)` | repeats the first part x of the cycle |
| `:inside(n, f)`, `:outside(n, f)` | f on a piece of 1/n (or n) cycles |
| `:swing(n)`, `:swingBy(x, n)` | the even steps of every 1/n a little late |
| `:segment(n)` | n events per cycle from a signal |
| `:range(a, b)`, `:rangex(a, b)` | from 0..1 to a..b (exponential) |
| `:add(x)`, `:sub(x)`, `:mul(x)`, `:div(x)`, `+`, `-`, `*` | on numbers, on the note (or on `n`) |
| `:transpose(x)`, `:scale("C:minor")` | semitones; the `n` degrees on a scale |
| `:arp("up")` | the notes of a chord one after the other: `up`, `down`, `updown`, `downup`, `converge`, `diverge`, `thumbup` |
| `:fmap(f)` | a function on the values |

The functions to pass to `every`, `sometimes`, `off`... are Lua functions
(`function(p) return p:fast(2) end`), but there are also Strudel's shortcuts:
`rev`, `fast(2)`, `slow(2)`, `ply(2)`, `late(0.25)`, `add(12)`, `degrade`:
`:every(4, fast(2))`, `:sometimes(add(12))`.

**Scales**: `"C:major"`, `"A:minor"`, `"D4:dorian"`, and `phrygian`, `lydian`, `mixolydian`,
`locrian`, `harmonic`, `melodic`, `pentatonic`, `minor_pentatonic`, `blues`, `chromatic`,
`wholetone` (the root at octave 3 if missing).

**Signals** (0..1 over each cycle, continuous: used as values or with `:segment`): `sine`,
`cosine`, `saw`, `isaw`, `tri`, `square`, `rand`, `perlin`, `irand(n)`, `choose(a, b, ...)`.

## The sound of each note

| Control | What it does |
|---|---|
| `:s(x)` | the instrument (also a pattern: `:s("<pluck epiano>")`) |
| `:note(x)`, `:n(x)` | note, degree |
| `:gain(x)`, `:velocity(x)` | the volume (0..1) |
| `:legato(x)`, `:clip(x)` | how long the note lasts relative to its space (1 = all of it) |
| `:lpf(hz)`, `:hpf(hz)`, `:bpf(hz)`, `:cutoff(hz)`, `:res(x)` | the filter (low-pass, high-pass, band-pass) and resonance 0..1 |
| `:pan(x)` | −1 left, 1 right |
| `:room(x)`, `:delay(x)` | how much goes to the room and to the echo (`reverb()`, `echo()` set them) |
| `:attack(ms)`, `:decay(ms)`, `:sustain(x)`, `:release(ms)` | the envelope |
| `:shape(x)`, `:drive(x)`, `:noise(x)` | saturation, noise |
| `:fenv(octaves)`, `:fdecay(ms)`, `:lfo(hz)`, `:wah(octaves)`, `:pwm(x)`, `:duty(x)` | filter envelope, slow oscillation |
| `:wave(name)` | the waveform (`"saw"`, `"fm"`, `"pluck"`...) |
| `:vib(cent)`, `:vibhz(hz)`, `:detune(cent)`, `:pitch(semitones)`, `:ptime(ms)` | vibrato and pitch envelope |
| `:fm(x)`, `:fmh(x)` | FM depth and ratio |
| `:raw(true)` | the 8-bit voice |
| `:crush(bits)`, `:coarse(n)` | bit crush (1..15 bits kept), the rate divided (1..16) |
| `:vowel("a")` | the formants of a vowel: `"a"`, `"e"`, `"i"`, `"o"`, `"u"` |
| `:chorus(x)`, `:trem(x)`, `:duck(x)` | the chorus send, the tremolo (on `:lfo`), the ducking of the other voices (0..1) |
| `:begin(x)` | where a sample starts (0..1 of its length) |
| `:tone{...}` | any key of `tone()`: `:tone{curve = "fold", color = "pink", density = .2, reverse = true}` |

The units are those of `tone()` (milliseconds, pan −1..1, resonance 0..1): they are not
Strudel's, where attack is in seconds and pan goes from 0 to 1. An instrument without a name
is `"triangle"`; drums without a note play at their own (kick 36, snare 50, hat 72), the rest
at C3.

**The kit.** `s "kit:2"` (or `s "kit" :n "<0 2>"`) picks a drum of the console's kit, played
at its own speed (C4 when no note is given); `pump` (the kit's kick that ducks the rest)
too. The drums by name, as in Strudel: `s "bd sd hh oh cp cb"` (`sd:2` two semitones up, as
for every drum); `rim` and `tom` are the synthesizer's presets (the kit's are `kit:5`,
`kit:6`). `R.bake` keeps `kit:0`, `kit:1`... as instruments of their own.

```lua
beat = s "bd [~ bd] ~ bd, ~ cp, hh*8?" :crush(8)
pad  = chord "<Am F>" :s "choir" :vowel("<a o>") :chorus(.5) :trem(.3)
```

## Playing

| Function | What it does |
|---|---|
| `R.play(name, p)`, `p:play(name)` | plays p under that name (replacing what was there) |
| `R.stop(name)`, `R.hush()` | stops one, all |
| `R.update()` | to call in `_update`: queues the notes of the next moments |
| `R.setcps(x)`, `R.setcpm(x)`, `R.bpm(x, [beats])` | the speed (changes without skipping) |
| `R.playing()`, `R.get([name])` | the names playing; the pattern of a name (without a name: all together) |
| `R.now()` | the cycle now |
| `R.voices(v, ...)` | the voices it can use (none given: all 8) |
| `R.errors()` | the patterns' errors (a pattern with an error stops) |
| `R.code(text)` | live code (below) |

### Live code: `R.code`

`R.code(text)` runs a piece of Lua where riff's functions are global (`note`, `s`, `stack`,
`sine`, `setcpm`, `hush`...) and then the game's. **Every global given a pattern plays under
that name** (`drums = s "kick*4"`); names starting with `_` do not (`_drums = ...` silences
it, like `_$:` in Strudel); the patterns of a previous `R.code` that no longer appear stop;
`local`s are just pieces. It returns `true`, or `nil` and the error: the previous music keeps
playing. Before playing it tries each pattern on its first cycle, so an error (a note that
does not exist, a wrong scale) shows up at once.

```lua
R.code [[
setcpm(28)
local verse  = chord "<Am F C G>"
pad   = verse :s "pad" :room(.6) :gain(.7)
arp   = verse :s "pluck" :arp("updown") :fast(4) :pan(sine:range(-.5, .5))
drums = s "kick ~ ~ kick, ~ snare, hat*8?" :every(4, fast(2))
]]
```

The code is Lua text compiled by the kernel (never bytecode): it cannot do anything the
cartridge's code could not already do.

## In bm Code

**Ctrl+Enter** plays the code: in a tab of only patterns the whole tab, in a game the
`riff.code [[ ... ]]` block under the cursor (or the first one). The words of the notes
playing **light up** (as long as the text is the one played); an error marks its line and
says so in the status line, the music goes on; the status line says `MUSIC n`.
**Ctrl+.** stops everything. The same items are in the menu (*Play the music*, *Stop the
music*). From the serial port: `ESC [ 28 ~` and `ESC [ 29 ~`.

## In bm Sound

**F7** (or *Riff...* in the menu) opens a line where you write a pattern
(`s "kick*4, ~ snare"`, or an assignment `drums = ...`), with eight examples (up/down).
**Enter** plays it with the sounds of the open bank (by name) and the instruments;
**Ctrl+Enter** puts 1, 2, 4, 8 or 16 bars of it (Tab) in the bank as a **new song** with its
patterns, at the riff's tempo (16 steps per cycle). Instruments the bank lacks become bank
sounds. One note per step and per track (up to 8 tracks): filter, position and room of each
note stay those of the instrument. With the pad: up/down the examples, left/right the bars,
A plays, X to the bank, B closes.

`R.bake(p, {cycles = 4, steps = 16, name = "RIFF"})` does the same from code: it returns the
piece in the form of `ai.music` (`bpm`, `instruments`, `patterns`). Conversely
`R.piece(ai.music("ritmo rock"))` plays a piece from the assistant as a pattern, to be
changed with everything above.

## Differences from Strudel

- Lua: methods with the colon (`:fast(2)`), anonymous functions with `function(p) ... end`;
  no `$:` (patterns are given names).
- The sounds are those of bm's synthesizer, no samples: `s "bd"` does not exist, `s "kick"`
  does (`instruments("drum")`).
- `tone()` units; `pan` −1..1; the default note is C3.
- The cycle starts from 0 when the first pattern starts and goes back to 0 when everything
  stops.

## Tests

- `make test-riff`: mini-notation, functions, controls, the scheduler on the sound clock,
  live code, `bake` and `piece` in bmhost (`tests/riff/cart.lua`; the sound in
  `build/riff/riff.wav`).
- `make test-audio`: the queue of timed notes (`player_at` in `src/audio/player.c`).
- QEMU: `test_riff_code` (bm Code), `test_sound_riff` (bm Sound), `test_audio` (`play_at`).
