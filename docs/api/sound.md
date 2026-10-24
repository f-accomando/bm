# API: Sound

Part of the game API. Index and basics: [API.md](../API.md).

### Sound

Audio goes out in **stereo** at 48 kHz (on the Pi over HDMI, the monitor's speakers; on
the RGB30 from the headphones or the speaker) and is generated in an interrupt: it costs
nothing to your `_update`. The synthesizer is **hi-fi** (2026-10-06): waves without
aliasing, exponential envelopes, a resonant filter, a room (reverb) and an echo, a light
compressor on the output; the **8-bit** voice of before stays (`retro(true)`, `tone()`'s
`raw`, or *Settings > Screen and sound > Sound style*). There are two ways to use it, also
together:

- **the sound bank** of the cartridge (sound effects and music made with the Sound editor
  of the Dev tab): `sfx(n)` and `music(n)`;
- **notes** played by the code, one voice at a time: `note`, `slide`, `arp`...

Eight voices (0–7). Waveforms: `SQUARE` (with `duty`), `TRIANGLE`, `SAW`, `NOISE`, `SINE`,
`METAL` (short metallic noise: cymbals, bells), `FM` (two sines, one bending the other:
electric piano, bells, basses, brass), `PLUCK` (a plucked string: guitars, harps),
`SUPERSAW` (three saws a little out of tune: pads, wide leads), `ORGAN` (four harmonics:
organs, flutes), `SAMPLE` (a recorded sample: the bank's or one of the console's eight
drums, below), `PINK`, `BROWN` (softer noises: wind, sea, hiss) and `CRACKLE` (random
clicks: an old record, a fire). Each voice has an ADSR envelope, a filter, a place between
left and right, how much goes to the room, the echo and the chorus, and the effects of
Strudel's superdough (bit crush, a lower rate, vowels, drive curves, tremolo, ducking:
`tone()`). Pitches
are in **Hz** (with decimals too: `261.63`) or a **note name**: `"C4"` (middle C), `"A4"`
(440 Hz), `"F#3"`, `"Bb2"`.

#### Sound effects and music (the cartridge's bank)

| Function | Description |
|---|---|
| `sfx(n, [v], [semitones], [vol])` | plays sound effect `n` of the bank; without `v` it chooses a free voice, preferring those the music leaves empty. `semitones` transposes it (`sfx(0, nil, 12)`: an octave up), `vol` 0–1. Returns the voice, or `nil` (no bank, a number that is not there) |
| `sfx(-1, [v])` | stops the effect of voice `v`, or all |
| `sfxpos(v)` | the effect playing on voice `v` and its step, or `nil` |
| `music(n, [fade_ms], [pos])` | plays song `n` from the start (or from position `pos` of its sequence), fading in over `fade_ms` |
| `music(-1, [fade_ms])` | stops the music, fading it |
| `music()` | while it plays: song, position, step, pattern (to keep in time); otherwise `nil` |
| `tempo(x)` | the music goes `x` times faster (1 = as written): speed up when the game gets hard |
| `mute(track, [on])` | turns off (`on`, the default) or back on a track of the music: layers that come and go |
| `volume([level])` | the general volume 0–10 (with `level` it changes it). It is the console's: it holds for every game and stays in `bm/config.txt` |

Track `t` of a pattern plays on voice `t`. While a sound effect uses a voice, the music's
track on that voice is silent. A bank with a song that uses tracks 0–5 leaves voices 6 and
7 free for the effects.

```lua
function _init()
  music(0)                         -- song 0, looping as decided in the editor
end
function _update()
  if btnp(4) then sfx(1) end       -- jump
  if got_coin then sfx(0) end
  if boss then tempo(1.2) end      -- faster
  if btnp(8) then music(-1, 500) end
end
```

#### Notes from the code

| Function | Description |
|---|---|
| `note(v, hz, [ms], [wave], [vol])` | plays a note on voice `v` (the envelope starts again); with `ms` it ends by itself, without it it stays on until `noteoff(v)`. `hz` in Hz or a name (`"C4"`). `vol` 0–255 (default 128) |
| `noteoff(v)` | releases the note (the release phase starts) |
| `freq(v, hz)` | changes the pitch without starting again |
| `slide(v, hz, [ms])` | the note slides to `hz` in `ms` (default 100): glissandos, sirens, lasers |
| `vibrato(v, [semitones], [hz])` | a vibrato `semitones` wide (for example 0.3) at `hz` swings a second (default 6); `vibrato(v)` takes it away |
| `arp(v, chord, [ms])` | the note runs through a chord, `ms` per note (default 50): `"major"`, `"minor"`, `"maj7"`, `"min7"`, `"7"`, `"sus2"`, `"sus4"`, `"dim"`, `"aug"`, `"power"`, `"octave"`, or a table of semitones (`{0, 4, 7, 12}`); `arp(v)` takes it away |
| `hz(note)` | the frequency of a note: a MIDI number (60 = middle C, 69 = A 440) or a name (`hz("A4")` = 440) |
| `envelope(v, a, d, s, r)` | the voice's envelope: attack, decay and release are times 0–255 (0 = instant, 255 = 2 s), sustain is a level 0–255. Default `1, 0, 255, 10` |
| `duty(v, d)` | the width of the square wave, 0–255 (128 = 50%; 32–64 sounds more "nasal") |
| `playing(v)` | `true` while the voice sounds (release included) or an effect / the music holds it |
| `apu(v, reg, [value])` | reads or writes a raw register of the voice (32 bytes per voice, 0–31: `src/audio/synth.h`; register 10 is the 1/256 of Hz, from 11 the tone: filter, place, room, echo, LFO) |

Waveform and volume stay those of the voice's last note, so giving them once is enough. At
the start and at the end of the cartridge the voices go off and back to their defaults.
Several loud voices together add up: a limiter keeps them under the top, but keep the
volumes around 100–130. Examples:

```lua
note(0, 880, 60, SQUARE, 100)          -- a 60 ms "blip"
note(0, "C5", 60, SQUARE, 100)         -- the same with the note's name
envelope(2, 0, 60, 0, 30)              -- an explosion that fades...
note(2, 2500, 300, NOISE, 130)         -- ...with noise
note(1, 1300, 300, SQUARE, 70)         -- laser: starts high...
slide(1, 300, 250)                     -- ...and goes down
note(3, "C4", 600, SQUARE, 90)         -- an arpeggiated major chord
arp(3, "major", 40)
```

Pong, Snake and Star Shooter in `carts/` use notes (a 10-line `jingle` function for the
tunes; now there is `lib.jingle` in bmlib); the Sound editor's demo project
(`carts/sound/demo.json`) has sound effects and three songs to listen to and copy (HIFI
uses the new voices).

#### Instruments, tone, room

| Function | Description |
|---|---|
| `tone(v, sound)` | the voice's tone for the `note()`s that follow: the name of a **ready-made instrument** (`"epiano"`, `"pluck"`, `"pad"`, `"kick"`… `instruments()` lists them) or a table in plain units (below). `tone(v)` goes back to the square of a fresh voice |
| `play([v], sound, [note], [ms], [vol])` | plays an instrument as the music plays it, with its pitch envelope and vibrato (the kick that falls, the laser): a name, a table as `tone()`'s (plus `pitch` semitones to start from, `ptime` ms to get there, `vib` cents, `vibhz`, `detune` cents) or a sound of the bank (a number; a name is first a sound of the bank with that name, then a ready-made instrument, then a drum of the kit: `"bd"`, `"sd"`, `"hh"`, `"oh"`, `"cp"`, `"cb"`). `"NAME:N"` is a sample's sound on its N-th sample after its own (`"kit:2"` the kit's third drum, the hi-hat; for a sound of the bank the bank's samples; wrapping). `note`: a MIDI number or a name (`"C4"`, default 60), `ms` 0 = held until `noteoff`, `vol` 0–1; without `v` a free voice. Returns the voice |
| `instruments([kind])` | the ready-made instruments: `{ {name=, kind=, about=}, … }`; `kind`: `"drum"`, `"bass"`, `"keys"`, `"pad"`, `"pluck"`, `"lead"`, `"fx"` |
| `instrument(name)` | an instrument with all its values, as bm Sound keeps a sound (`wave`, `a`, `d`, `s`, `r`, `pitch`, `tone` = the tone's 21 bytes…), or `nil`; also a drum of the kit (`"sd"`) or `"kit:3"` |
| `reverb([size], [damp], [wet])` | the room the voices play in (0–1: a small room to a hall, bright to dull, how much is heard); returns the three. Each voice sends to it what its `reverb` says (a little by default: 0.16) |
| `echo([ms], [feedback], [wet])` | the ping-pong echo (left, right): the time between repeats (680 ms at most), how much comes back (0–0.95), how much is heard; returns the three. What goes in is each voice's `echo` |
| `chorus([rate_hz], [depth_ms], [wet])` | the chorus (two delays that swing, left and right: width for pads and keys): how fast it swings (0.05–5 Hz, default 0.8), how far (0–8 ms, default 2.5), how much is heard (0–1, default 1); returns the three. What goes in is each voice's `chorus` |
| `audio_depth()` | `bits, out`: the bits of each sample the console sends (16, 24 or 32: `sound_depth` in `bm/config.txt`, *Settings > Screen and sound > Bit depth*, 24 by default) and what the output asks for now (0: none). A game reads it, it does not change it |
| `audio_samples(["kit"])` | the samples of the cartridge's bank (entry `n + 1` is the one `sample = n` plays) or, with `"kit"`, the kit's eight drums: `{ {name=, frames=, rate=, channels=, root=, fine=, loop=, loop_start=, loop_end=, peak=, meter=}, … }`. `rate` Hz, `root` MIDI note, `fine` cents, `loop` 0 none, 1 forward, 2 ping-pong (`loop_start`, `loop_end` in frames), `peak` 0–1 of full scale, `meter` 32 peaks 0–1 from its start to its end |
| `retro([on])` | every voice **8-bit** as in the first versions (naive waves, straight envelopes, no room nor echo) while the game runs; returns whether they are (Settings can ask for it too) |
| `audio_time()` | the sound's clock in seconds (it moves with the samples played) |
| `play_at(t, sound, [note], [ms], [vol], [tag])` | a note at the time `t` of `audio_time()`: the sound's interrupt starts it, within 1.3 ms, whatever the frame rate. `sound` as `play()`'s, `ms` how long it is held (250 by default), `vol` 0–1, `tag` 1–255 (a group for `play_cancel`, 1 by default). The voice is chosen when it starts (a free one, a tail, the oldest note of `play_at`; never one of a song or of an effect). `true`, or `false` when the queue (160 notes) is full. riff uses it |
| `play_cancel([tag])` | forgets the notes of `play_at` waiting with that `tag` (none: all) and releases those that sound |
| `play_voices([v, …])` | the voices `play_at` may take (none: all 8); returns how many notes wait |

The keys of `tone()` (all optional; the missing ones stay as they are):

| Key | Value |
|---|---|
| `preset` | a ready-made instrument to start from |
| `wave` | `"square"`, `"triangle"`, `"saw"`, `"noise"`, `"sine"`, `"metal"`, `"fm"`, `"pluck"`, `"supersaw"`, `"organ"`, `"sample"`, `"pink"`, `"brown"`, `"crackle"` |
| `vol`, `duty`, `sustain` | 0–1 |
| `attack`, `decay`, `release` | ms (up to 2000) |
| `cutoff` | the **filter**: Hz (0 = no filter) |
| `res`, `filter` | resonance 0–1; `"lp"` low pass, `"bp"` band pass, `"hp"` high pass, `"notch"` |
| `keytrack` | `true`: the cutoff follows the note (at middle C it is the one given) |
| `fenv`, `fdecay` | the filter's envelope: octaves that open it (negative too), ms to close again |
| `lfo`, `wah`, `pwm` | a slow wobble (Hz) that moves the filter (`wah` octaves) and the square's width (`pwm` 0–1) |
| `drive`, `noise` | soft saturation before the filter, white noise added (0–1) |
| `pan` | −1 left, 0 middle, 1 right |
| `reverb`, `echo` | how much the voice sends to the room and to the echo (0–1) |
| `ratio`, `depth`, `mdecay`, `feedback` | FM: the modulator's ratio (1, 2, 3.5…), depth (radians, 0–8), in how many ms it fades, feedback 0–1 |
| `bright`, `ring` | PLUCK: how bright the string is and how long it rings (0–1) |
| `spread` | SUPERSAW: how out of tune the three saws are (0–1) |
| `bars` | ORGAN: the four drawbars `{8, 6, 3, 2}` (0–15) |
| `sample`, `begin`, `reverse` | SAMPLE: which sample (a number of the bank's, a name of the bank's, a drum of the kit `"bd"` `"sd"` `"hh"` `"oh"` `"cp"` `"rim"` `"tom"` `"cb"`, or `"kit:N"`), where it starts (0–1 of its length), `true` backwards. The note against the sample's root sets its speed |
| `density` | CRACKLE: how many clicks (0–1) |
| `color` | the colour of the `noise` mixed in: `"white"`, `"pink"`, `"brown"`, `"crackle"` |
| `curve` | the `drive`'s curve: `"soft"` (as before), `"hard"`, `"fold"`, `"sine"`, `"asym"` (even harmonics, a tube), `"cubic"` |
| `vowel` | the formants of a vowel after the filter: `"a"`, `"e"`, `"i"`, `"o"`, `"u"`, `""` none |
| `crush`, `coarse` | bit crush: the bits kept (1–15; 0 none); the rate divided (1–16: each value held that many samples) |
| `trem` | tremolo: the `lfo` on the volume, how deep (0–1) |
| `duck` | ducking: this voice pushes the others down while it sounds (0–1): a kick that pumps a pad, an effect that lowers the music |
| `chorus` | how much the voice sends to the chorus (0–1; `chorus()` sets it) |
| `raw` | `true`: this voice is 8-bit (the effects above are ignored, as in `retro`) |

Every new key at 0 (or missing) is the sound as it was. `play()` and `play_at()` take the
same keys in their table.

```lua
tone(0, "epiano")                                   -- an electric piano for note()
note(0, "E4", 400)
tone(1, { preset = "bass", cutoff = 300, res = 0.7 })   -- a darker, ringing bass
play(nil, "kick", "C2")                             -- the kick, on a free voice
play(nil, { wave = "noise", cutoff = 900, fenv = -2, fdecay = 300, decay = 400, sustain = 0 }, "C3")
play(nil, "sd")                                     -- the kit's snare (a sample)
play(nil, { preset = "kit", sample = "hh", reverse = true })  -- the hi-hat backwards
tone(2, { preset = "lead", crush = 4, coarse = 3 }) -- a broken lead, 4 bits
tone(3, { preset = "pad", vowel = "o", chorus = 0.6 })  -- a choir singing "oh"
play(nil, { preset = "kick", duck = 0.8 }, "C2")    -- the kick lowers everything else
reverb(0.8, 0.5, 1)                                 -- a cathedral (Yharnam)
echo(375, 0.4, 1)                                   -- an echo on the beat at 80 BPM
chorus(0.5, 4, 1)                                   -- a slower, wider chorus
retro(true)                                         -- the 8-bit sound of before
```

**Ready-made instruments** (`src/audio/presets.c`, 50): drums `kick`, `punch`, `snare`, `clap`,
`hat`, `openhat`, `tom`, `rim`, `crash`, `cowbell`, `shaker` (and `chipkick`, `chipsnr`,
`chiphat` in 8 bits), `kit` (the kit's drums as samples: `"kit:0"`…`"kit:7"`), `pump` (the
kit's kick ducking the rest: a sidechain pump); basses `bass`, `acid`, `sub`, `fmbass`,
`pickbass`, `bitbass` (crushed to 4 bits); keys `epiano`, `organ`, `bell`, `marimba`,
`glock`, `rhodes` (with tremolo and chorus); pads `pad`, `strings`, `warm`, `glass`, `lush`
(supersaw in the chorus), `choir` (singing "aah"); strings `pluck`, `guitar`, `harp`; leads
`lead`, `sawlead`, `flute`, `brass`, `triangle`, `chip`, `chiptri`, `solo` (warm drive,
vibrato, echo); effects `laser`, `blip`, `boom`, `wind`, `vinyl` (a record's crackle). The
same in bm Sound's *Instrument...* menu.

**The console's kit**: eight drums made by the synthesizer when the console starts (nothing
recorded), played as samples: `bd` kick, `sd` snare, `hh` closed and `oh` open hi-hat, `cp`
clap, `rim`, `tom`, `cb` cowbell (`"kit:0"`…`"kit:7"` in this order). `play(nil, "cp")`,
`tone(v, { wave = "sample", sample = "oh" })`; `rim` and `tom` by name are the synthesizer's
instruments (the kit's: `"kit:5"`, `"kit:6"`). In the 8-bit mode they still sound like
drums (8 bits, the nearest frame).

#### The bank: format and tools

The bank is the **AUDIO** section of the `.bm` (format in `src/audio/player.h`, version 2,
or 3 with samples; version 1 is still read): up to 32 sounds (instruments, each with its
tone: filter, place, room, echo, LFO, effects), 64 sound effects, 64 patterns, 8 songs (each
with the echo on the beat, in steps, and the room's size) and up to 64 **samples** (8, 16,
24, 32-bit or float, mono or stereo, any rate, with a root note and a loop: forward or
ping-pong; 16 MB a bank, about 174 s of mono at 48 kHz). It is made with the
**Sound editor** (Dev tab), which opens a game and saves its sounds right inside it; its
menu's *Samples...* lists the bank's samples and the kit's with their length and peak and
puts one into the sound (the SAMPLE wave, with SAMPLE and START among its settings), and the
menu's footer shows the output's depth (`output 24-bit`). On the PC: `scripts/bmaudio.py
unpack game.bm -o sounds.json` extracts it as readable JSON (the samples as WAV files beside
it), `mkbm.py --audio sounds.json` puts it back in a cartridge, `make wav BANK=sounds.json
SONG=0` plays it into a WAV (`BITS=24`, `32` or `f32` for a deeper file; 16 by default). A
WAV file goes into a bank from the JSON: `"samples": [{"name": "voice", "wav": "voice.wav",
"root": "C4", "loop": "fwd"}]`, and a sound plays it with `"wave": "sample", "sample":
"voice"`. `bmhost --wav-bits 24` records a game's sound at 24 bits.

**Bit depth.** The mix is in floats and is rounded once, with dither, to the depth the
output carries: `sound_depth=16|24|32` in `bm/config.txt` (*Settings > Screen and sound >
Bit depth*, 24 by default). The Pi's HDMI and the RGB30's codec convert at most 24 bits;
`16` is the old path exactly. `audio_depth()` tells a game which it is.
