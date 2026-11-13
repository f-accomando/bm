#!/usr/bin/env python3
"""
The sound bank of Overbit (the AUDIO section, as bmaudio.py packs it): the
console's synthesizer, 8 voices of square, triangle, saw, noise, sine and
metal with envelopes and pitch sweeps. The sound effects are in the order
of the names in src/90_audio.lua.

The effects are mostly 16-bit samples made here (numpy, a fixed seed: the same
bytes every run; WAV files beside OUT.json), played softly: the loudest peaks
sit at about -9 dB and the menu's sounds are a soft sine blip. A few synthesized
voices (alarms, hums, chords) stay, quieter than before.

  sounds.py OUT.json
"""
import json
import os
import struct
import sys
import wave as wavemod

import numpy as np

RATE = 22050


def _env(n, attack_ms, decay):
    t = np.arange(n) / RATE
    a = np.minimum(1.0, t / max(attack_ms / 1000.0, 1e-4))
    return a * np.exp(-t * decay)


def _lowpass(x, k):
    """one-pole low pass, k 0..1 (higher: brighter)"""
    out = np.zeros_like(x)
    y = 0.0
    for i, v in enumerate(x):
        y += k * (v - y)
        out[i] = y
    return out


def _norm(x, peak):
    return x / max(1e-9, np.max(np.abs(x))) * peak


def _sine(f, n, f_end=None):
    t = np.arange(n) / RATE
    fr = np.linspace(f, f_end if f_end else f, n)
    return np.sin(2 * np.pi * np.cumsum(fr) / RATE)


def make_samples():
    rng = np.random.default_rng(1337)
    N = lambda sec: int(RATE * sec)
    out = {}
    noise = lambda n: rng.uniform(-1, 1, n)
    # cannon / rifle crack: a short bright snap over a dull body
    n = N(0.28)
    out["blast"] = _norm(_lowpass(noise(n), 0.5) * _env(n, 1, 22) + _sine(120, n, 55) * _env(n, 2, 14) * 0.9, 0.62)
    # a low punch
    n = N(0.30)
    out["thump"] = _norm(_sine(95, n, 42) * _env(n, 3, 12) + _lowpass(noise(n), 0.08) * _env(n, 1, 40) * 0.4, 0.7)
    # explosion: a dark rolling noise with a sub under it
    n = N(0.9)
    out["boom"] = _norm(_lowpass(noise(n), 0.07) * _env(n, 4, 4.2) + _sine(60, n, 28) * _env(n, 6, 3.2) * 0.8, 0.7)
    # whoosh: band-limited air swelling up and falling away
    n = N(0.55)
    sw = _lowpass(noise(n), 0.22) - _lowpass(noise(n), 0.03)
    t = np.arange(n) / RATE
    out["whoosh"] = _norm(sw * np.sin(np.pi * np.minimum(1.0, t / 0.55)) ** 1.5, 0.5)
    # hit marker: a dry soft tick
    n = N(0.06)
    out["tick"] = _norm(_lowpass(noise(n), 0.35) * _env(n, 0.5, 90) + _sine(1800, n) * _env(n, 0.5, 80) * 0.5, 0.45)
    # glass: inharmonic partials
    n = N(0.6)
    g = sum(_sine(f, n) * a for f, a in ((2100, 1), (3330, .7), (4870, .5), (6210, .3)))
    out["glass"] = _norm(g * _env(n, 0.5, 9) + _lowpass(noise(n), 0.4) * _env(n, 0.3, 60) * 0.5, 0.5)
    # bell (root C6): soft sine partials
    n = N(0.9)
    b = sum(_sine(1046.5 * m, n) * a for m, a in ((1, 1), (2.0, .35), (2.76, .2), (4.1, .08)))
    out["bell"] = _norm(b * _env(n, 4, 4.5), 0.5)
    # the menu's blip (root C6): a sine with a slow attack, no edge
    n = N(0.11)
    out["ui"] = _norm((_sine(1046.5, n) + _sine(2093, n) * 0.15) * _env(n, 6, 28), 0.4)
    return out


def write_samples(folder):
    for name, x in make_samples().items():
        pcm = np.clip(x * 32767, -32768, 32767).astype("<i2")
        with wavemod.open(os.path.join(folder, name + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())
    return ["blast", "thump", "boom", "whoosh", "tick", "glass", "bell", "ui"]


# instruments: name, wave, duty, vol, adsr, pitch (semitones), pitch time (x 10 ms)
SOUNDS = [
    ("BLAST", "sample:blast", 128, 150, [0, 14, 0, 6], 0, 0),    # 0 cannon crack
    ("THUMP", "sample:thump", 128, 150, [0, 18, 0, 8], 0, 0),    # 1 low punch
    ("ZAP", "triangle", 48, 90, [0, 12, 0, 6], 12, 6),           # 2 pistol
    ("WHOOSH", "sample:whoosh", 128, 110, [2, 40, 60, 20], 0, 0),  # 3 rockets, boost
    ("RUMBLE", "triangle", 128, 70, [4, 60, 160, 40], -5, 30),   # 4 engines
    ("BOOM", "sample:boom", 128, 170, [0, 90, 0, 60], 0, 0),     # 5 explosions
    ("SUB", "sine", 128, 150, [0, 80, 0, 50], 12, 25),           # 6 their bass
    ("BEEP", "sine", 128, 90, [4, 8, 200, 6], 0, 0),             # 7 alarms
    ("BELL", "sample:bell", 128, 120, [0, 40, 0, 40], 0, 0),     # 8 kill chime (root C6)
    ("TICK", "sample:tick", 128, 120, [0, 4, 0, 3], 0, 0),       # 9 hit marker
    ("CHORD", "triangle", 128, 100, [6, 40, 140, 30], 0, 0),     # 10 call mech
    ("HUM", "triangle", 128, 55, [8, 30, 200, 20], 0, 0),        # 11 field
    ("SWISH", "sample:whoosh", 128, 100, [1, 16, 0, 10], 0, 0),  # 12 blades
    ("PLASMA", "triangle", 96, 80, [2, 20, 60, 12], -7, 12),     # 13 plasma hum
    ("PEW", "triangle", 64, 90, [0, 7, 0, 4], 12, 3),            # 14 small guns
    ("GLASS", "sample:glass", 128, 130, [0, 30, 0, 30], 0, 0),   # 15 shatter, ice
    ("UI", "sample:ui", 128, 100, [0, 10, 0, 6], 0, 0),          # 16 the menus (root C6)
]

SFX = [
    ("CANNON", 25, ["C4 0 255", "C4 1 200 bend-12", "off"]),
    ("PISTOL", 30, ["A6 2 220 bend-12", "off"]),
    ("ROCKET", 40, ["E5 3 180 bend+5", "G5 3 140 bend+5", "off"]),
    ("BOOST", 80, ["C3 4 200 bend+7", "G3 3 220", ".", ".", ".", ".", "C3 3 180 fadeout4", "off"]),
    ("BUMP", 40, ["C3 1 255 bend-12", "off"]),
    ("BOOM", 60, ["C3 5 255 fadeout4", "C2 6 255", ".", ".", "off"]),
    ("BOOMBIG", 90, ["C2 5 255", "G1 6 255 fadeout7", ".", ".", ".", ".", ".", "off"]),
    ("REDLINE", 120, ["A5 7 200", "E5 7 200", "A5 7 200", "E5 7 200", "A5 7 220", "E5 7 220",
                      "A5 7 240", "E5 7 240", "A5 7 255 retrig3", "A5 7 255 retrig7", "off"]),
    ("EJECT", 50, ["C4 3 220 bend+12", "C5 2 180 bend+12", "off"]),
    ("CALL", 90, ["C4 10 200 arp0", ".", "G4 10 220 arp0", ".", "C5 10 255", ".", "off"]),
    ("LAND", 60, ["C2 6 255", "C3 1 200 fadeout2", "off"]),
    ("HIT", 30, ["C7 9 200", "off"]),
    ("CRIT", 30, ["E7 9 220", "B7 9 220", "off"]),
    ("KILL", 70, ["C6 8 200", "G6 8 220", "C7 8 240 fadeout3", "off"]),
    ("STEP", 40, ["C2 1 150", "off"]),
    ("UI", 30, ["C6 16 210", "off"]),
    ("UIBACK", 30, ["G5 16 190", "off"]),
    ("FIELD", 60, ["C4 11 160 vib6", ".", ".", "off"]),
    ("SABER", 45, ["G5 12 200 bend-7", "C4 13 170 bend-5", "off"]),
    ("BARRIER", 70, ["C4 13 160 bend+12", "C5 11 170 vib4", ".", "off"]),
    ("SHATTER", 60, ["C7 15 255", "G6 15 230", "C6 5 220 fadeout3", "off"]),
    ("STRIKE", 50, ["C3 1 255 bend-12", "G3 13 220 bend-7", "off"]),
    ("REPEATER", 25, ["E6 14 170 bend-12", "off"]),
    ("LIMIT", 90, ["C3 13 220 bend+12", "G3 12 255", "C4 13 255 bend+12", "G4 12 240", "C5 5 220 fadeout4", "off"]),
    ("DASH", 50, ["C4 3 220 bend+7", "G4 4 160", "off"]),
    ("RIFLE", 25, ["C5 0 190", "G4 14 120 bend-12", "off"]),
    ("HELIX", 50, ["E5 3 200 bend+5", "C3 1 230 bend-12", "off"]),
    ("SPRINT", 40, ["C3 4 120 bend+5", "off"]),
    ("HEAL", 60, ["C5 10 180", "E5 10 180", "G5 10 200", "C6 10 200 fadeout2", "off"]),
    ("VISOR", 60, ["C6 7 180", "E6 7 190", "G6 7 200", "C7 7 220", "off"]),
    # the match (Control): the point opens, captured, lost, the end of a round
    ("OPEN", 80, ["G4 7 170", "C5 7 190", "G5 7 210", "off"]),
    ("CAPTURE", 90, ["C5 10 200", "E5 10 210", "G5 10 220", "C6 8 240 fadeout3", "off"]),
    ("LOST", 90, ["G4 10 200", "E4 10 190", "C4 10 180 fadeout3", "off"]),
    ("WIN", 130, ["C5 10 220", "E5 10 220", "G5 10 230", "C6 10 240", ".", "G5 8 220", "C6 8 255 fadeout4", "off"]),
    ("LOSE", 130, ["C5 10 200", "B4 10 190", "A4 10 180", "G4 10 170", ".", "C4 6 200 fadeout4", "off"]),
    ("TICK", 25, ["C6 9 150", "off"]),
]


SAMPLE_VOICES = (0, 1, 3, 5, 9, 12, 15)         # played at their own pitch: the note is C4


def _fix_step(st):
    parts = st.split()
    if len(parts) >= 3 and parts[1].isdigit() and int(parts[1]) in SAMPLE_VOICES:
        parts[0] = "C4"
    return " ".join(parts)


def bank(samples=()):
    sounds = []
    for n, w, d, v, e, p, t in SOUNDS:
        x = {"name": n, "wave": w, "duty": d, "vol": v, "adsr": e, "pitch": p, "pitch_time": t,
             "vibrato": [0, 0], "detune": 0}
        if w.startswith("sample:"):
            x["wave"], x["sample"] = "sample", w[7:]
        sounds.append(x)
    root = {"bell": "C6", "ui": "C6", "glass": "C6"}
    return {
        "sounds": sounds,
        "sfx": [{"name": n, "ms": ms, "loop": [0, 0], "steps": [_fix_step(x) for x in st]} for n, ms, st in SFX],
        "samples": [{"name": n, "wav": n + ".wav", "root": root.get(n, "C4")} for n in samples],
        "patterns": [],
        "songs": [],
    }


if __name__ == "__main__":
    folder = os.path.dirname(os.path.abspath(sys.argv[1]))
    with open(sys.argv[1], "w") as f:
        json.dump(bank(write_samples(folder)), f, indent=1)
