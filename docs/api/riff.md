# API: riff

Part of the game API. Index and basics: [API.md](../API.md).

## riff: music as patterns

`local R = require "riff"` (2026-10-06, the whole guide in [RIFF.md](../RIFF.md), in
Italian): rhythms and melodies written in a line, as in TidalCycles and Strudel, played on
time by the sound's interrupt (`play_at`) with the ready-made instruments and the bank's
sounds by name. Time is counted in **cycles** (a bar of four beats; 2 s at first).

```lua
local R = require "riff"

function _init()
  R.setcpm(30)
  R.play("drums", R.s "kick*4, ~ snare, hat*8")
  R.play("bass", R.note "<c2 a1 f1 g1>" :s "acid" :lpf(R.sine:range(300, 1800):slow(4)))
end

function _update()
  R.update()                      -- every frame: the notes of the next moments into the queue
end
```

| Mini-notation | What it does |
|---|---|
| `a b c d` | a sequence in the cycle |
| `~` `-` | a rest |
| `[a b]` | a group in one part |
| `a, b` | together |
| `<a b>` | one a cycle |
| `a*2`, `a/2` | faster, slower (also `a*<2 4>`) |
| `a!3`, `a@3`, `a _ _` | repeated, longer |
| `a?`, `a?0.3` | at random |
| `a(3,8,2)` | a Euclidean rhythm |
| `{a b c}%4` | polymeter |
| `a \| b` | one of the two, each cycle |
| `kick:2` | a variant (two semitones up) |
| `kit:2`, `bd sd hh` | the kit's drum 2 (samples, at their own speed); the kit's drums by name, as in Strudel |

| Function | Description |
|---|---|
| `R.s(p)`, `R.note(p)`, `R.n(p)`, `R.chord(p)` | instruments, notes (`c4` = 60, octave 3 when missing), degrees of a scale, chords (`"<Am F C G7>"`) |
| `R.seq`, `R.cat`, `R.stack`, `R.timecat`, `R.arrange`, `R.run(n)` | in sequence, a cycle each, together, with weights, sections of several cycles, `0..n-1` |
| `R.sine`, `R.cosine`, `R.saw`, `R.tri`, `R.square`, `R.rand`, `R.perlin`, `R.irand(n)`, `R.choose(…)` | signals 0..1 |
| `:fast`, `:slow`, `:early`, `:late`, `:rev`, `:ply`, `:iter`, `:palindrome` | time |
| `:every(n, f)`, `:lastOf`, `:sometimes(f)`, `:often`, `:rarely`, `:degradeBy(x)`, `:someCycles` | changes now and then, at random |
| `:euclid(k, n, r)`, `:struct(p)`, `:mask(p)`, `:segment(n)`, `:chunk(n, f)`, `:linger(x)`, `:swing(n)` | structure |
| `:off(t, f)`, `:superimpose(f)`, `:layer(…)`, `:jux(f)` | layers, left and right |
| `:add`, `:sub`, `:mul`, `+`, `:transpose`, `:scale("C:minor")`, `:arp("updown")`, `:range(a, b)` | notes and numbers |
| `:s`, `:gain`, `:legato`, `:lpf`, `:hpf`, `:bpf`, `:res`, `:pan`, `:room`, `:delay`, `:attack`, `:decay`, `:sustain`, `:release`, `:shape`, `:vib`, `:fm`, `:raw`, `:crush`, `:coarse`, `:vowel`, `:chorus`, `:trem`, `:duck`, `:begin`, `:tone{…}` | the sound of each note (the units of `tone()`; `:tone{curve = "fold", color = "pink"}` for the rest) |
| `R.play(name, p)`, `R.stop(name)`, `R.hush()`, `R.update()` | play (with a name), stop, to call in `_update` |
| `R.setcps(x)`, `R.setcpm(x)`, `R.bpm(x)` | the speed |
| `R.code(text)` | live code: riff's functions are globals, every global given a pattern plays under its name, the earlier ones not named again stop; `true` or `nil` and the error |
| `R.bake(p, {cycles=, steps=16})`, `R.piece(t)` | the pattern as a piece for the bank (the shape of `ai.music`), a piece of `ai.music` as a pattern |
| `R.playing()`, `R.get([name])`, `R.errors()`, `R.active()`, `R.voices(v, …)` | the names playing, their patterns, the errors, where the notes sounding are written, the voices it may use |

```lua
R.code [[
setcpm(28)
local verse = chord "<Am F C G>"
pad   = verse :s "pad" :room(.6) :gain(.7)
arp   = verse :s "pluck" :arp("updown") :fast(4)
drums = s "kick ~ ~ kick, ~ snare, hat*8?" :every(4, fast(2))
]]
```

```lua
R.code [[
beat  = s "bd ~ [~ bd] ~, ~ cp, hh*8" :crush(8)        -- the kit's samples, 8 bits
vox   = note "<a3 f3>" :s "choir" :vowel("o") :chorus(.5)
]]
```

In **bm Code** Ctrl+Enter plays the code (the tab, or the `riff.code [[ ]]` block under
the cursor) and lights up the words of the notes sounding, Ctrl+. stops it; in **bm
Sound** F7 (*Riff...*) plays a line and Ctrl+Enter puts it into the bank as a song. Tests:
`make test-riff`, QEMU `test_riff_code`, `test_sound_riff`.
