#!/usr/bin/env python3
"""
The sound bank of Overbit (the AUDIO section, as bmaudio.py packs it): the
console's synthesizer, 8 voices of square, triangle, saw, noise, sine and
metal with envelopes and pitch sweeps. The sound effects are in the order
of the names in src/90_audio.lua.

  sounds.py OUT.json
"""
import json
import sys

# instruments: name, wave, duty, vol, adsr, pitch (semitones), pitch time (x 10 ms)
SOUNDS = [
    ("BLAST", "noise", 128, 230, [0, 14, 0, 6], 0, 0),       # 0 cannon crack
    ("THUMP", "sine", 128, 220, [0, 18, 0, 8], 24, 4),       # 1 low punch
    ("ZAP", "square", 48, 150, [0, 12, 0, 6], 12, 6),        # 2 pistol
    ("WHOOSH", "noise", 128, 120, [2, 40, 60, 20], 0, 0),    # 3 rockets, boost
    ("RUMBLE", "saw", 128, 110, [4, 60, 160, 40], -5, 30),   # 4 engines
    ("BOOM", "noise", 128, 255, [0, 90, 0, 60], 0, 0),       # 5 explosions
    ("SUB", "sine", 128, 255, [0, 80, 0, 50], 12, 25),       # 6 their bass
    ("BEEP", "square", 128, 140, [0, 8, 200, 4], 0, 0),      # 7 alarms, UI
    ("BELL", "sine", 128, 170, [0, 40, 0, 40], 0, 0),        # 8 kill chime
    ("TICK", "metal", 128, 140, [0, 4, 0, 3], 0, 0),         # 9 hit marker
    ("CHORD", "triangle", 128, 160, [1, 40, 140, 30], 0, 0), # 10 call mech
    ("HUM", "triangle", 128, 90, [3, 30, 200, 20], 0, 0),    # 11 field
    ("SWISH", "noise", 128, 170, [1, 16, 0, 10], 0, 0),      # 12 blades
    ("PLASMA", "saw", 96, 130, [0, 20, 60, 12], -7, 12),     # 13 plasma hum
    ("PEW", "square", 64, 140, [0, 7, 0, 4], 12, 3),         # 14 small guns
    ("GLASS", "metal", 128, 200, [0, 30, 0, 30], 0, 0),      # 15 shatter, ice
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
    ("UI", 30, ["C6 7 160", "off"]),
    ("UIBACK", 30, ["G5 7 140", "off"]),
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
]


def bank():
    return {
        "sounds": [{"name": n, "wave": w, "duty": d, "vol": v, "adsr": e, "pitch": p, "pitch_time": t,
                    "vibrato": [0, 0], "detune": 0} for n, w, d, v, e, p, t in SOUNDS],
        "sfx": [{"name": n, "ms": ms, "loop": [0, 0], "steps": st} for n, ms, st in SFX],
        "patterns": [],
        "songs": [],
    }


if __name__ == "__main__":
    with open(sys.argv[1], "w") as f:
        json.dump(bank(), f, indent=1)
