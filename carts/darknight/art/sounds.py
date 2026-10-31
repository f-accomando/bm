#!/usr/bin/env python3
"""
The sound bank of Dark Night (the AUDIO section, as bmaudio.py packs it; the Makefile runs this script and mkbm.py puts
the result into the cartridge with --audio).

The sound effects are short 16-bit samples made here with numpy (a fixed seed: the same bytes every run; WAV files beside
OUT.json), so they sound like things and not like a chip: the whoosh of the saw, a wet chop, the clack of the saw's hinge,
the pistol, a boot on stone, cloth, a growl, a creaking door, a bell, a gong. Their order is SFX below; main.lua names
them in its SFX table by the same numbers.

The music is played by the synthesizer from patterns: two songs in D minor. Song 0 (CLINIC) is slow and sparse: a low
cello, a cold string pad, a music box and a far choir. Song 1 (HUNT) is the same key with timpani, an ostinato in the
strings and the choir in front. The tracks use voices 0 to 4; the effects take 5, 6 and 7.

  sounds.py OUT.json
"""
import json
import os
import sys
import wave as wavemod

import numpy as np

RATE = 22050


def _t(n):
    return np.arange(n) / RATE


def _env(n, attack_ms, decay):
    t = _t(n)
    a = np.minimum(1.0, t / max(attack_ms / 1000.0, 1e-4))
    return a * np.exp(-t * decay)


def _lowpass(x, k):
    """one pole low pass, k 0..1 (higher: brighter)"""
    out = np.zeros_like(x)
    y = 0.0
    for i, v in enumerate(x):
        y += k * (v - y)
        out[i] = y
    return out


def _highpass(x, k):
    return x - _lowpass(x, k)


def _norm(x, peak):
    return x / max(1e-9, np.max(np.abs(x))) * peak


def _sine(f, n, f_end=None, phase=0.0):
    fr = np.linspace(f, f_end if f_end else f, n)
    return np.sin(phase + 2 * np.pi * np.cumsum(fr) / RATE)


def _partials(n, parts, decay):
    """a struck metal: (frequency, amplitude, decay multiplier) partials"""
    out = np.zeros(n)
    for f, a, d in parts:
        out += a * _sine(f, n) * np.exp(-_t(n) * decay * d)
    return out


def make_samples():
    rng = np.random.default_rng(2026)
    N = lambda sec: int(RATE * sec)                                 # noqa: E731
    noise = lambda n: rng.uniform(-1, 1, n)                         # noqa: E731
    out = {}

    # the saw cutting the air: a band of noise that rises and falls, a little whistle in it
    n = N(0.34)
    sw = np.sin(np.pi * np.minimum(1.0, _t(n) / 0.34))
    band = _highpass(_lowpass(noise(n), 0.35), 0.03) * sw ** 1.4
    out["whoosh"] = _norm(band + 0.08 * _sine(900, n, 1500) * sw ** 3, 0.5)

    # a wet chop: a thud, a splash of noise, a short wet tail
    n = N(0.42)
    thud = _sine(150, n, 60) * _env(n, 1, 24)
    splash = _lowpass(noise(n), 0.55) * _env(n, 1, 16)
    wet = _lowpass(noise(n), 0.12) * _env(n, 30, 9) * 0.6
    out["chop"] = _norm(thud * 1.1 + splash * 0.7 + wet, 0.7)

    # a heavier chop (the charged cut): deeper, longer
    n = N(0.6)
    out["cleave"] = _norm(_sine(110, n, 40) * _env(n, 1, 12) * 1.2 + _lowpass(noise(n), 0.5) * _env(n, 1, 10) * 0.8
                          + _lowpass(noise(n), 0.1) * _env(n, 40, 6) * 0.7, 0.78)

    # the hinge of the saw: two metal clacks (open: the second higher; closed: the second lower, by the note)
    n = N(0.3)
    clack = _partials(n, [(1800, 1.0, 1.0), (2700, 0.6, 1.3), (4100, 0.4, 1.8), (620, 0.5, 0.8)], 40) * _env(n, 0.5, 30)
    clack2 = np.zeros(n)
    k = N(0.09)
    clack2[k:] = (_partials(n - k, [(2300, 1.0, 1.0), (3500, 0.5, 1.4), (760, 0.4, 0.8)], 36) * _env(n - k, 0.5, 26))
    out["clack"] = _norm(clack + 0.9 * clack2 + _highpass(noise(n), 0.4) * _env(n, 0.5, 60) * 0.3, 0.55)

    # the pistol: a crack, a low boom, the room's tail
    n = N(0.7)
    crack = _highpass(noise(n), 0.5) * _env(n, 0.3, 70)
    boom = _sine(95, n, 45) * _env(n, 1, 9)
    tail = _lowpass(noise(n), 0.2) * _env(n, 10, 5) * 0.5
    out["shot"] = _norm(crack * 1.0 + boom * 1.1 + tail, 0.8)

    # a boot on stone: a short dull knock
    n = N(0.14)
    out["step"] = _norm(_lowpass(noise(n), 0.22) * _env(n, 0.5, 45) + _sine(140, n, 90) * _env(n, 0.5, 60) * 0.5, 0.42)

    # the Hunter is hit: a thump with a breath in it
    n = N(0.4)
    out["hurt"] = _norm(_sine(120, n, 70) * _env(n, 1, 14) + _lowpass(noise(n), 0.3) * _env(n, 4, 12) * 0.5
                        + _lowpass(noise(n), 0.08) * _env(n, 60, 7) * 0.7, 0.62)

    # a dodge: the coat
    n = N(0.28)
    out["cloth"] = _norm(_lowpass(_highpass(noise(n), 0.1), 0.25) * np.sin(np.pi * np.minimum(1.0, _t(n) / 0.28)) ** 1.2, 0.36)

    # the lantern: a bell with a slow bloom and a crackle of fire under it
    n = N(1.8)
    bell = _partials(n, [(523.3, 1.0, 1.0), (1046.5, 0.5, 1.4), (1318.5, 0.35, 1.9), (2093, 0.2, 2.6), (784, 0.25, 1.2)], 2.2)
    fire = _lowpass(noise(n), 0.5) * (rng.uniform(0, 1, n) > 0.985) * 0.5 * np.exp(-_t(n) * 1.5)
    out["bell"] = _norm(bell * _env(n, 2, 0.6) + fire, 0.5)

    # the dog: a growl (a low buzz, shaken) and a bite (a snap of teeth)
    n = N(0.7)
    buzz = np.sign(_sine(62, n, 48)) * 0.5 + _sine(124, n, 96) * 0.5
    shake = 0.6 + 0.4 * np.sin(2 * np.pi * 23 * _t(n))
    out["growl"] = _norm(_lowpass(buzz * shake + noise(n) * 0.35, 0.18) * _env(n, 30, 4), 0.55)
    n = N(0.22)
    out["snap"] = _norm(_highpass(noise(n), 0.3) * _env(n, 0.3, 50) + _sine(300, n, 120) * _env(n, 0.3, 40) * 0.6, 0.6)

    # an enemy swings: a lower whoosh
    n = N(0.4)
    sw = np.sin(np.pi * np.minimum(1.0, _t(n) / 0.4))
    out["swing"] = _norm(_lowpass(noise(n), 0.22) * sw ** 1.5, 0.45)

    # an enemy dies: a groan and a fall
    n = N(0.9)
    groan = _lowpass(np.sign(_sine(90, n, 55)) * 0.5 + noise(n) * 0.4, 0.1) * _env(n, 40, 5)
    fall = np.zeros(n)
    k = N(0.5)
    fall[k:] = _lowpass(noise(n - k), 0.2) * _env(n - k, 1, 20) * 0.9
    out["groan"] = _norm(groan + fall, 0.6)

    # a door: wood that creaks, the pitch wandering
    n = N(0.9)
    wob = 180 + 40 * np.sin(2 * np.pi * 1.7 * _t(n)) + 15 * np.sin(2 * np.pi * 9 * _t(n))
    saw = 2 * (np.cumsum(wob) / RATE % 1.0) - 1
    out["creak"] = _norm(_lowpass(saw, 0.08) * np.sin(np.pi * np.minimum(1.0, _t(n) / 0.9)) ** 0.7 + _lowpass(noise(n), 0.05) * 0.2, 0.4)

    # the death: a low gong
    n = N(3.0)
    out["gong"] = _norm(_partials(n, [(98, 1.0, 1.0), (147, 0.6, 1.1), (196, 0.5, 1.3), (262, 0.3, 1.7), (330, 0.2, 2.2)], 1.1) * _env(n, 8, 0.4)
                        + _lowpass(noise(n), 0.4) * _env(n, 1, 12) * 0.5, 0.7)

    # the menu: a soft wooden tick
    n = N(0.08)
    out["tick"] = _norm(_lowpass(noise(n), 0.5) * _env(n, 0.3, 90) + _sine(1200, n, 800) * _env(n, 0.3, 80) * 0.4, 0.3)

    # the charge: a rising breath of air (looped by the effect while the key is held)
    n = N(0.5)
    out["draw"] = _norm(_lowpass(noise(n), 0.15) * np.linspace(0.2, 1.0, n), 0.3)

    # a timpani for the music: a drum skin with a drop in pitch
    n = N(0.9)
    out["timp"] = _norm(_sine(82, n, 62) * _env(n, 1, 6) + _lowpass(noise(n), 0.3) * _env(n, 1, 30) * 0.4, 0.75)
    return out


SAMPLE_ROOT = {"bell": "C5", "tick": "C5"}


def write_samples(folder):
    names = []
    for name, x in make_samples().items():
        pcm = np.clip(x * 32767, -32768, 32767).astype("<i2")
        with wavemod.open(os.path.join(folder, name + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())
        names.append(name)
    return names


# instruments: the samples (0-17), then the music's voices (18-23)
SOUNDS = [
    {"name": "WHOOSH", "wave": "sample", "sample": "whoosh", "vol": 150, "adsr": [0, 0, 255, 8], "reverb": 50},    # 0
    {"name": "CHOP", "wave": "sample", "sample": "chop", "vol": 190, "adsr": [0, 0, 255, 10], "reverb": 70},        # 1
    {"name": "CLEAVE", "wave": "sample", "sample": "cleave", "vol": 210, "adsr": [0, 0, 255, 12], "reverb": 90},    # 2
    {"name": "CLACK", "wave": "sample", "sample": "clack", "vol": 150, "adsr": [0, 0, 255, 8], "reverb": 60},       # 3
    {"name": "SHOT", "wave": "sample", "sample": "shot", "vol": 220, "adsr": [0, 0, 255, 14], "reverb": 120},       # 4
    {"name": "STEP", "wave": "sample", "sample": "step", "vol": 110, "adsr": [0, 0, 255, 6], "reverb": 40},         # 5
    {"name": "HURT", "wave": "sample", "sample": "hurt", "vol": 190, "adsr": [0, 0, 255, 10], "reverb": 60},        # 6
    {"name": "CLOTH", "wave": "sample", "sample": "cloth", "vol": 120, "adsr": [0, 0, 255, 8], "reverb": 30},       # 7
    {"name": "BELL", "wave": "sample", "sample": "bell", "vol": 170, "adsr": [0, 0, 255, 60], "reverb": 160},       # 8
    {"name": "GROWL", "wave": "sample", "sample": "growl", "vol": 150, "adsr": [0, 0, 255, 14], "reverb": 50},      # 9
    {"name": "SNAP", "wave": "sample", "sample": "snap", "vol": 170, "adsr": [0, 0, 255, 8], "reverb": 50},         # 10
    {"name": "SWING", "wave": "sample", "sample": "swing", "vol": 140, "adsr": [0, 0, 255, 8], "reverb": 50},       # 11
    {"name": "GROAN", "wave": "sample", "sample": "groan", "vol": 170, "adsr": [0, 0, 255, 14], "reverb": 80},      # 12
    {"name": "CREAK", "wave": "sample", "sample": "creak", "vol": 140, "adsr": [0, 0, 255, 20], "reverb": 90},      # 13
    {"name": "GONG", "wave": "sample", "sample": "gong", "vol": 220, "adsr": [0, 0, 255, 80], "reverb": 200},       # 14
    {"name": "TICK", "wave": "sample", "sample": "tick", "vol": 110, "adsr": [0, 0, 255, 4], "reverb": 20},         # 15
    {"name": "DRAW", "wave": "sample", "sample": "draw", "vol": 120, "adsr": [0, 0, 255, 20], "reverb": 40},        # 16
    {"name": "TIMP", "wave": "sample", "sample": "timp", "vol": 200, "adsr": [0, 0, 255, 30], "reverb": 150},       # 17
    # the music
    {"name": "CELLO", "wave": "saw", "vol": 120, "adsr": [40, 60, 200, 60], "cutoff": 70, "resonance": 30, "reverb": 120,
     "chorus": 60, "vibrato": [6, 50]},                                                                              # 18
    {"name": "STRINGS", "wave": "supersaw", "vol": 70, "adsr": [120, 80, 220, 120], "cutoff": 95, "resonance": 20,
     "mod1": 110, "reverb": 180, "chorus": 120, "pan": -30},                                                        # 19
    {"name": "CHOIR", "wave": "organ", "vol": 80, "adsr": [110, 90, 210, 110], "cutoff": 120, "vowel": "o", "reverb": 200,
     "chorus": 140, "pan": 30, "mod1": 0xA8, "mod2": 0x64},                                                         # 20
    {"name": "MUSICBOX", "wave": "fm", "vol": 110, "adsr": [0, 160, 0, 120], "mod1": 56, "mod2": 60, "moddecay": 60,
     "reverb": 170, "echo": 90, "pan": 20},                                                                          # 21
    {"name": "HARP", "wave": "pluck", "vol": 100, "adsr": [0, 0, 255, 80], "mod1": 120, "mod2": 170, "reverb": 150,
     "echo": 70, "pan": -15},                                                                                        # 22
    {"name": "LOWSTR", "wave": "supersaw", "vol": 85, "adsr": [20, 60, 200, 60], "cutoff": 80, "resonance": 40,
     "mod1": 80, "reverb": 120, "chorus": 50},                                                                       # 23
]

# the effects: name, ms a step, steps. A sample plays at C4 (its own speed); a note moves it.
SFX = [
    ("SAWSWING", 30, ["C4 0 230", "off"]),            # 0  the closed saw cuts the air
    ("SAWHIT", 30, ["C4 1 255", "off"]),              # 1  the saw hits flesh
    ("CLEAVE", 30, ["C4 2 255", "off"]),              # 2  the charged cut lands
    ("SAWOPEN", 30, ["C4 3 220", "off"]),             # 3  the saw opens
    ("SAWCLOSE", 30, ["A3 3 220", "off"]),            # 4  the saw closes (a lower clack)
    ("PISTOL", 30, ["C4 4 255", "off"]),              # 5
    ("STEP", 30, ["C4 5 170", "off"]),                # 6  a step (the game moves the pitch)
    ("HURT", 30, ["C4 6 240", "off"]),                # 7
    ("DODGE", 30, ["C4 7 200", "off"]),               # 8
    ("LANTERN", 30, ["C5 8 230", "off"]),             # 9
    ("GROWL", 30, ["C4 9 200", "off"]),               # 10
    ("BITE", 30, ["C4 10 230", "off"]),               # 11
    ("ESWING", 30, ["C4 11 200", "off"]),             # 12 an enemy's weapon in the air
    ("EDIE", 30, ["C4 12 230", "off"]),               # 13
    ("DOOR", 30, ["C4 13 200", "off"]),               # 14
    ("DIED", 30, ["C4 14 255", "off"]),               # 15
    ("MENU", 30, ["C5 15 170", "off"]),               # 16
    ("CHARGE", 30, ["C4 16 180", "off"]),             # 17
    ("BIGSWING", 30, ["G3 0 255", "off"]),            # 18 the open saw cuts the air (lower, longer)
    ("HEADSHOT", 30, ["C4 4 255", "E5 15 200", "off"]),  # 19
]


# ---------------------------------------------------------------- the music

def track(steps, events):
    """events: (step, note or "off", sound, vol) -> the step strings of one track"""
    out = ["."] * steps
    for st, note, snd, vol in events:
        out[st] = note if note == "off" else "%s %d %d" % (note, snd, vol)
    return out


def chord(steps, notes, snd, vol, at=0, off=None):
    """several tracks are one voice each: a chord is one note a track; here one track holds one note"""
    return track(steps, [(at, notes, snd, vol)] + ([(off, "off", 0, 0)] if off is not None else []))


def pattern(steps, tracks):
    return {"steps": steps, "tracks": {str(k): v for k, v in tracks.items()}}


CELLO, STRINGS, CHOIR, BOX, HARP, LOWSTR, TIMP = 18, 19, 20, 21, 22, 23, 17


def songs():
    S = 16                                                            # a pattern: four beats of four steps
    pats = []
    # --- CLINIC (song 0): i VI iv V in D minor, a bar each; the cello on the roots, the pad a fifth, the music box asks
    # a question in bars 3 and 4, the choir answers in the second half. Order: 0 1 2 3, loops.
    roots = [("D2", "D3", "A3"), ("Bb1", "Bb2", "F3"), ("G2", "G3", "D4"), ("A2", "A3", "E4")]
    box_lines = [[], [], [(0, "D5"), (4, "F5"), (8, "E5"), (12, "D5")], [(0, "A4"), (6, "C5"), (10, "Bb4"), (14, "A4")]]
    choir_notes = [None, None, "D4", "C#4"]
    for i in range(4):
        r, p5a, p5b = roots[i]
        tr = {
            0: track(S, [(0, r, CELLO, 200), (12, "off", 0, 0)] + ([(14, r, CELLO, 120)] if i % 2 else [])),
            1: track(S, [(0, p5a, STRINGS, 150), (15, "off", 0, 0)]),
            2: track(S, [(0, p5b, LOWSTR, 110), (15, "off", 0, 0)]),
            3: track(S, [(st, n, BOX, 180) for st, n in box_lines[i]] + ([(15, "off", 0, 0)] if box_lines[i] else [])),
            4: track(S, [(2, choir_notes[i], CHOIR, 120), (15, "off", 0, 0)] if choir_notes[i] else []),
        }
        pats.append(pattern(S, tr))
    # a second pass of the same four bars with the harp: order 4 5 6 7
    harp_lines = [[(0, "D4"), (2, "F4"), (4, "A4"), (6, "D5"), (8, "A4"), (10, "F4"), (12, "D4"), (14, "A3")],
                  [(0, "Bb3"), (2, "D4"), (4, "F4"), (6, "Bb4"), (8, "F4"), (10, "D4"), (12, "Bb3"), (14, "F3")],
                  [(0, "G3"), (2, "Bb3"), (4, "D4"), (6, "G4"), (8, "D4"), (10, "Bb3"), (12, "G3"), (14, "D3")],
                  [(0, "A3"), (2, "C#4"), (4, "E4"), (6, "A4"), (8, "E4"), (10, "C#4"), (12, "A3"), (14, "E3")]]
    for i in range(4):
        r, p5a, p5b = roots[i]
        tr = {
            0: track(S, [(0, r, CELLO, 190), (12, "off", 0, 0)]),
            1: track(S, [(0, p5a, STRINGS, 140), (15, "off", 0, 0)]),
            2: track(S, [(st, n, HARP, 150) for st, n in harp_lines[i]]),
            3: track(S, [(st, n, BOX, 170) for st, n in box_lines[i]] + ([(15, "off", 0, 0)] if box_lines[i] else [])),
            4: track(S, [(0, choir_notes[i], CHOIR, 130), (15, "off", 0, 0)] if choir_notes[i] else []),
        }
        pats.append(pattern(S, tr))
    # --- HUNT (song 1): the same four chords, faster, timpani on the beats, the strings in an ostinato, the choir in front
    for i in range(4):
        r, p5a, p5b = roots[i]
        lo = r
        ost = [(0, p5a), (2, p5a), (4, p5a), (6, p5b), (8, p5a), (10, p5a), (12, p5b), (14, p5a)]
        timp = [(0, "C4", TIMP, 255), (6, "C4", TIMP, 180), (8, "C4", TIMP, 255), (12, "C4", TIMP, 200), (14, "C4", TIMP, 150)]
        if i == 3:
            timp += [(13, "C4", TIMP, 160), (15, "C4", TIMP, 220)]
        tr = {
            0: track(S, timp),
            1: track(S, [(0, lo, CELLO, 210), (7, "off", 0, 0), (8, lo, CELLO, 210), (15, "off", 0, 0)]),
            2: track(S, [(st, n, LOWSTR, 150) for st, n in ost]),
            3: track(S, [(0, choir_notes[i] or (p5b), CHOIR, 170), (15, "off", 0, 0)]),
            4: track(S, [(st, n, BOX, 160) for st, n in box_lines[i]] + [(15, "off", 0, 0)]),
        }
        pats.append(pattern(S, tr))
    return pats, [
        {"name": "CLINIC", "bpm": 58, "swing": 0, "loop": 0, "echo": 4, "room": 200, "order": [0, 1, 2, 3, 4, 5, 6, 7]},
        {"name": "HUNT", "bpm": 92, "swing": 0, "loop": 0, "echo": 2, "room": 160, "order": [8, 9, 10, 11]},
    ]


def bank(samples=()):
    pats, sgs = songs()
    return {
        "sounds": SOUNDS,
        "sfx": [{"name": n, "ms": ms, "loop": [0, 0], "steps": st} for n, ms, st in SFX],
        "samples": [{"name": n, "wav": n + ".wav", "root": SAMPLE_ROOT.get(n, "C4")} for n in samples],
        "patterns": pats,
        "songs": sgs,
    }


if __name__ == "__main__":
    folder = os.path.dirname(os.path.abspath(sys.argv[1]))
    os.makedirs(folder, exist_ok=True)
    with open(sys.argv[1], "w") as f:
        json.dump(bank(write_samples(folder)), f, indent=1)
    print("wrote", sys.argv[1])
