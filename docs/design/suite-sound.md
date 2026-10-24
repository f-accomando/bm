# Development Proposals: bm Sound (Audio Suite and Sequencer)

This document analyzes what **bm Sound** (`carts/sound`) lacks to integrate transparently with game gameplay, avoid duplicating audio code in Lua, and eliminate acoustic fatigue in action titles.

---

## 1. Current State of bm Sound

`bm Sound` is a complete and versatile 8-voice synthesizer and sequencer:
- **Main Pages**:
  - **SOUNDS**: Synth instrument editor (14 waveforms: square, triangle, sawtooth, noise, sine, metal, FM, pluck, supersaw, organ, PCM sample, pink/brown/crackle noises) with ADSR envelope, dynamic filter and echo.
  - **SFX**: Design of quick sound effects with pitch curves and effects (glide, bend, vibrato, tremolo, arpeggio).
  - **PATTERN**: 8-track step sequencer (one track per voice).
  - **SONG**: Pattern sequence for composing complex songs with repeat handling.
- **Music Assistant**: Opened with F6 to generate custom melodies, basslines and effects.

---

## 2. What Is Missing for Game Integration

Although sound generation is rich, the way game code communicates with the audio bank has historical limits typical of old retro engines:

### A. No Named SFX Dictionary (Named Sound Events)
- Sound effects in the audio bank are identified only by integer numbers (`0..63`).
- In almost all games in the repository (*Star Shooter*, *Skyvale World*), developers ended up **writing the sound effect code directly in Lua via `note()`**:
  ```lua
  local function sfx(kind)
    if kind == "slash" then note(0, 2600, 90, NOISE, 70)
    elseif kind == "gun" then note(0, 900, 160, NOISE, 170); note(2, 70, 120, SQUARE, 110)
    elseif kind == "hit" then note(2, 380, 110, NOISE, 130)
    ...
  ```
- This happens because using `sfx(14)` or `sfx(22)` in Lua code forces you to constantly consult bm Sound to remember which number corresponds to which effect, making the code opaque and prone to misalignment errors.

### B. "Machine Gun" Effect and Lack of Micro-Variations (Jitter)
- In high-action-frequency games (e.g. robot footsteps in *Titan Clash*), playing exactly the same sound effect dozens of times per second with identical frequency and volume generates monotony and ear fatigue.
- bm Sound currently has no **controlled randomization** parameter (pitch jitter / volume variation). The developer must manually program formulas such as `hz + math.random(-20, 20)` in Lua.

### C. Audio Channel Management and Priority (Voice Stealing & Ducking)
- The console has 8 polyphonic voices. When music plays on channels 3–7 and many simultaneous sound effects appear (explosions, lasers, coins), it is easy for a sound effect to "steal" a melodic voice or vice versa.
- Automatic **Audio Bus** management is missing:
  - *SFX Bus* (high priority, can interrupt other less important SFX).
  - *Music Bus* (low priority, protected from accidental interruptions).
  - *Audio Ducking*: automatic lowering of the music volume during important sounds (dialogue, boss roar, critical hit).

### D. Simplified Jingle Sequencer
- In games like *Star Shooter*, short melodies (e.g. picking up a power-up, leveling up, lighting a lantern) are written as dedicated mini-sequencers in Lua (`tune = { {523, 4}, {659, 4}, {784, 8} }`).
- There is no way to save these 1- or 2-voice "Jingles" in bm Sound, distinct from the full 8-track song patterns.

---

## 3. "Hand-Made" Complexity vs Integrated Solution

| Need | Current Approach ("By hand") | With the Evolution of bm Sound |
|---|---|---|
| **Calling Sound Effects** | Anonymous numeric `sfx(12)` or `note()` functions written by hand in the code | `sfx("hero_slash")` with a name label assigned directly in the editor |
| **Footstep/Hit Variation** | Math formulas in Lua to perturb the frequency | `Pitch Jitter (+/- 5%)` and `Vol Jitter` sliders in the SFX tab |
| **Music Ducking on Hits** | Writing manual envelopes on the music voices | Channel with flag `Duck Music by 40% on play` |
| **Victory / Retreat Jingle** | Note tables written by hand in the Lua file | Saving "Jingles" callable with `jingle("quest_complete")` |

---

## 4. Development Proposals & Roadmap for bm Sound

### Proposal 1: Named Sound Tags & Export
- In the **SFX** page, add a **Name / Tag** field (e.g. `coin_get`, `slash_light`, `hit_shield`, `laser`).
- bm Sound saves these labels in the `AUDIO` section of the `.bm` file.
- The runtime API extends `sfx()` to accept strings:
  ```lua
  sfx("slash_light") -- looks up the name in the bank dictionary and plays it
  ```
- This way the programmer no longer has to manage numeric tables or duplicate sounds in Lua.

### Proposal 2: Randomizer / Variation Sliders in the SFX Page
Add two optional parameters for each sound effect:
- **Pitch Variance (`0..20%`)**: on each playback, the fundamental frequency is slightly varied at random within this range.
- **Volume Variance (`0..15%`)**: slight gain variation to give the impact a natural feel.
- This feature instantly solves the repetitiveness of footsteps, shots, bounces and slashes without touching a line of code in the game.

### Proposal 3: Reserved Channels and Auto-Ducking
- In the bm Sound menu (or in the SONG page), add a default split of the voices:
  - **Voices 0–2**: *SFX Bus* (with smart stealing of the oldest voice if all 3 are busy).
  - **Voices 3–7**: *Music Bus*.
- Ability to flag an SFX with the **Ducking** attribute: when the effect starts, the music voices are automatically attenuated by a configurable amount for the duration of the sound.

### Proposal 4: "Audio Event" Integration with bm Animator and bm Pixel
- In the SFX page, a *Test on Animation* option: lets you preview a moving sprite (from bm Pixel or bm Animator) and play the sound effect to check by ear the perfect timing between visual impact and audio attack before launching the game.

---

## 5. Roadmap and Advanced Audio Tools (M46, R9, R17, R25, R31–R35)

- **WAV / PCM Import from SD (R9, R25)**:
  - (Done: R9, WAV files from `/docs` into a bank sample, Ctrl+W.)
  - Sample editor (R32): built-in functions for trimming, amplitude normalization, loop point setting; live recording from USB microphone or internal mix.
- **MIDI Import / Export (R17, R25)**:
  - (Done: R17, import of standard `.mid` files as a song with patterns, Ctrl+W.) Export is left.
- **Sample Management Across Banks (R33, R34)**:
  - (Done: R31, import of a `SAMPLE` instrument with its sample, renumbering `MOD1`.)
  - Distribution of `.bmau` v3 audio sample packs on the Market.
  - Support for multi-sample kits in `riff` (`s "mykit:3"`).
- **New Presets and Effects in the Music Assistant (R35)**:
  - Update of `ai.music` and `predict` with support for modern presets (`kit`, `choir`, `lush`, `bitbass`) and effects (`crush`, `vowel`, `duck` for sidechain).

