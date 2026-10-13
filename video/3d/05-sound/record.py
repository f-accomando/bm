#!/usr/bin/env python3
"""Tropic Trail, video 5: bm Sound. The six sound effects (APPLE CRATE JUMP STEP SPIN HIT) and the song JUNGLE, made with the
console's own editor (carts/sound/main.lua) run by bmhost --tool on a clean SD that holds the cartridge of the chain.

  python3 video/3d/05-sound/record.py [--start forge|chain] [--build-only] [--stage N] [--no-video] [--log] [--cart FILE]

Writes video/3d/05-sound/out/: input.txt (the keys; frame = seconds * 60; frame 0 = 0:12 of the finished video),
overlay.json (key caps, scenes, narration, chapters), events.json (when each effect / the pattern / the song plays: verify.py
measures the volume there), shots/ (PNG of the key moments), sound.wav (what the console played, mono 48 kHz),
sd/carts/TROPIC.BME (what the editor saved) and the mp4 when ffmpeg is there (--no-video is the default where it is missing:
nothing else changes). Deterministic: the same script, the same run.

Where the file goes. bm Sound saves into a PROJECT: Ctrl+S on a .bm (a game, read only) asks "Save an editable copy:
TROPIC.BME?", and Enter answers yes. The question is a modal of the kernel (runtime.c copy_question -> sys_ask) that reads the
KEYBOARD (HID usage Enter held), not the typed-character queue: in the input file it is the line `FRAME keys 0x28`, then
`keys none`. A plain `type \\n` is never delivered and the run does not end (the wav keeps growing at 7 MB/s: this script runs
bmhost with a timeout). The chain (video/3d/chain/) keeps both files:
  05-sound.bme   the very file bm Sound saved (what a later editor opens: `.bm` are read only in the editors)
  05-sound.bm    the same bytes with the PROJECT flag (u16 at byte 18, bit 0) cleared: a cartridge whose sections are the
                 start cartridge's (MESH, ANIM, SHEET ... byte for byte) plus the AUDIO bank. No SDK, no mkbm: the editor's
                 copy already is a complete cartridge (cart_put_audio keeps every section and puts the bank last).
--start chain (default when video/3d/chain/03-animator.bm exists) starts from that file; --start forge from the reference
assets (video/3d/out/assets/tropic_assets.bm, made by `python video/3d/assets/build.py` when missing): the sound does not
depend on the models or on the map.
The editor cartridge is built here from carts/sound (out/sound.bm, scripts/mkbm.py): build/sound.bm is NOT used because the
Makefile rule passes bm.info as --lua (`$<` is the first prerequisite of the rule), which gives
"main.lua:1: unexpected symbol near '#'" in bmhost.
Cursors: the editor starts on sound 0, WAVE (reset_view). The keys that move on the SOUNDS page are planned here by a shortest
path over the editor's own `move` rules (sound_path), so the script never guesses a key count; the pattern and song pages
move one cell per arrow and are tracked by R.
"""
import argparse
import collections
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import lib3d                                         # noqa: E402  (also puts video/lib and scripts on sys.path)
from lib3d import ROOT, FPS                           # noqa: E402
from bmvideo import Script, KEYS, call, encode, write_ass, card_cmd, hook_cmd, concat_cmd   # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
SHOTS = os.path.join(OUT, "shots")
WAV = os.path.join(OUT, "sound.wav")
CARTS = os.path.join(SD, "carts")
BME = os.path.join(CARTS, "TROPIC.BME")
TITLE = "TROPIC TRAIL  ·  5  ·  bm Sound"
SUBTITLE = "bm · the bare-metal fantasy console"
CHAIN_IN = "03-animator.bm"
CHAIN_OUT = "05-sound.bm"
FORGE = os.path.join(ROOT, "video", "3d", "out", "assets", "tropic_assets.bm")

# ----------------------------------------------------------------------------------------------------- the SOUNDS page
# SGROUPS of carts/sound/main.lua: GROUPS[group][column] = the rows
GROUPS = [
    [["WAVE", "DUTY", "VOLUME", "DETUNE"], ["ATTACK", "DECAY", "SUSTAIN", "RELEASE"],
     ["BEND FROM", "BEND TIME", "VIBRATO", "VIB SPEED"]],
    [["CUTOFF", "RESONANCE", "MODE", "KEY TRACK"], ["FILT ENV", "ENV DECAY", "DRIVE", "NOISE"],
     ["LFO SPEED", "LFO CUT", "LFO DUTY", "8-BIT"]],
    [["WAVE SET 1", "WAVE SET 2", "FM FADE", "FM FEEDBACK"], ["PAN", "ROOM", "ECHO"]],
]
ALIAS = {"FM RATIO": "WAVE SET 1", "BRIGHT": "WAVE SET 1"}     # what "wave set 1" is called with FM and PLUCK
WHERE = {nm: (g, c, r) for g, grp in enumerate(GROUPS, 1) for c, col in enumerate(grp, 1) for r, nm in enumerate(col, 1)}


def sound_move(st, d):
    """`move(dx, dy)` of P[1] in carts/sound/main.lua on the state (group, column, row; row 0 is the name)."""
    g, c, r = st
    dx, dy = {"left": (-1, 0), "right": (1, 0), "up": (0, -1), "down": (0, 1)}[d]
    if dy:
        n = len(GROUPS[g - 1][c - 1]) if c <= len(GROUPS[g - 1]) else 0
        nr = r + dy
        if nr > n:
            if g < len(GROUPS):
                g, nr = g + 1, 1
            else:
                nr = n
        elif nr < 1 and g > 1:
            g -= 1
            nr = len(GROUPS[g - 1][min(c, len(GROUPS[g - 1])) - 1])
        elif nr < 0:
            nr = 0
        r = nr
    if dx and r > 0:
        c += dx
    c = max(1, min(c, len(GROUPS[g - 1])))
    if r > 0:
        r = max(1, min(r, len(GROUPS[g - 1][c - 1])))
    return (g, c, r)


def sound_path(a, b):
    """The shortest list of arrows from state a to state b (breadth first, ties: up down left right), replayed to be sure."""
    if a == b:
        return []
    parent = {a: None}
    q = collections.deque([a])
    while q and b not in parent:
        st = q.popleft()
        for d in ("up", "down", "left", "right"):
            n = sound_move(st, d)
            if n not in parent:
                parent[n] = (st, d)
                q.append(n)
    out, n = [], b
    while parent[n]:
        n, d = parent[n]
        out.append(d)
    out.reverse()
    st = a
    for d in out:
        st = sound_move(st, d)
    assert st == b, (a, b, out)
    return out


# ------------------------------------------------------------------------------------------------------------- the script
SCENES = {
    "open": ("Dev ▸ bm Sound", [("Ctrl+O", "open a cartridge"), ("Enter", "choose it"), ("F1 - F4", "the four pages"),
                               ("Ctrl+S", "save")]),
    1: ("bm Sound ▸ F1 SOUNDS", [("↑ ↓ ← →", "choose a value"), ("= -", "value +1 / -1"), ("+ _", "value +/- a step"),
                                ("I", "listen"), ("PgDn", "next sound"), ("Enter", "rename")]),
    2: ("bm Sound ▸ F2 SFX", [("z s x d c v ...", "piano: a note"), (", .", "piano octave"), ("Space", "play"),
                             ("↑ Enter", "name the effect"), ("PgDn", "next effect")]),
    3: ("bm Sound ▸ F3 PATTERN", [("← → ↑ ↓", "move"), ("Enter", "note on / off"), ("z s x d c ...", "piano keys"),
                                 ("= -", "sound of the track"), ("Space", "play / stop"), ("Ctrl+C Ctrl+V", "copy / paste")]),
    4: ("bm Sound ▸ F4 SONG", [("Enter", "add the first slot"), ("i", "copy the slot"), ("= -", "pattern number"),
                              ("↑", "the song's header"), ("Space", "play / stop")]),
    "save": ("bm Sound ▸ save", [("Ctrl+S", "save"), ("Enter", "yes: an editable copy")]),
}


class R:
    """A Script plus the state of the editor (the cursors) and the time anchors of the shot list."""

    def __init__(self):
        self.s = Script()
        self.late = []
        self.events = []                    # what plays, for verify.py
        self.sp = (1, 1, 1)                 # SOUNDS: group, column, row (0 = the name)
        self.octave = 4
        self.ptrack, self.pstep = 0, 0      # PATTERN: track and step (-1 = the track header)
        self.fast = 2                       # frames between the keys of a series (time-lapse: 2-3)

    # ---- time
    def at(self, t):
        """Wait until the second `t` of the finished video (frame 0 = 0:12); note it when we are late."""
        f = round((t - 12) * FPS)
        if self.s.t < f:
            self.s.t = f
        elif self.s.t - f > FPS:
            self.late.append((t, (self.s.t - f) / FPS))

    def sec(self, x):
        self.s.sec(x)

    # ---- keys
    def key(self, name, what="", hold=None, cap=None):
        self.s.key(name, what, cap=cap, hold=hold if hold is not None else self.fast)

    def quiet(self, name, hold=None):
        """A key with no keycap of its own (a series: the first one carries the caption)."""
        code = KEYS.get(name, name)
        self.s.raw("\\x5c" if code == "\\" else code, None, "", hold if hold is not None else self.fast)

    def rep(self, name, n, what="", cap=None):
        for i in range(n):
            if i == 0:
                self.key(name, what, cap=cap)
            else:
                self.quiet(name)

    def name_it(self, text, what):
        """Enter, the 8 characters (the box overwrites them: pad with spaces), Enter."""
        assert len(text) == 8
        self.key("enter", what, hold=8)
        self.s.type_text(text, what, per_key=4)
        self.key("enter", "done", hold=8)

    def shot(self, name):
        self.s.shot(name)

    def event(self, kind, name, frame=None):
        self.events.append({"kind": kind, "name": name, "frame": self.s.t if frame is None else frame})

    def page(self, n, what="", hold=6):
        self.s.scene(*SCENES[n])
        self.key("f%d" % n, what, hold=hold)

    # ---- the SOUNDS page
    def goto(self, name, what=""):
        """Arrows to the value `name`; NAME is row 0 of whichever column is the cheapest to reach (it belongs to group 1)."""
        name = ALIAS.get(name, name)
        if name == "NAME":
            tgt = min(((1, c, 0) for c in (1, 2, 3)), key=lambda st: len(sound_path(self.sp, st)))
        else:
            tgt = WHERE[name]
        for i, d in enumerate(sound_path(self.sp, tgt)):
            if i == 0 and what:
                self.key(d, what)
            else:
                self.quiet(d)
        self.sp = tgt

    def setv(self, name, key, n, what=""):
        self.goto(name)
        self.rep(key, n, what)

    def octave_to(self, n):
        while self.octave < n:
            self.key(".", "piano: octave up", hold=6)
            self.octave += 1
        while self.octave > n:
            self.key(",", ", : piano octave down", hold=6)
            self.octave -= 1

    def piano(self, note, octave, what=""):
        self.octave_to(octave)
        self.key(note, what, hold=14)

    def listen(self, what, name):
        self.key("i", what, hold=50)
        self.event("listen", name, self.s.t - 50)

    def play(self, what, name):
        self.event("sfx", name)
        self.key("space", what, hold=6)
        self.sec(1.0)

    # ---- the PATTERN page (one cell per arrow; up/down keep the step)
    def pgoto(self, track, step):
        while self.ptrack < track:
            self.quiet("down")
            self.ptrack += 1
        while self.ptrack > track:
            self.quiet("up")
            self.ptrack -= 1
        while self.pstep < step:
            self.quiet("right")
            self.pstep += 1
        while self.pstep > step:
            self.quiet("left")
            self.pstep -= 1

    def header(self, track, key, n, what):
        self.pgoto(track, -1)
        self.rep(key, n, what)

    def cell(self, step1, note, what="", track=None):
        """A piano note on the step (1-based): the cursor goes there, the note goes in, the cursor moves on."""
        self.pgoto(self.ptrack if track is None else track, step1 - 1)
        self.key(note, what, hold=8)
        self.pstep += 1

    def cell_enter(self, step1, what=""):
        self.pgoto(self.ptrack, step1 - 1)
        self.key("enter", what, hold=8)


def build():
    r = R()
    s = r.s

    def sfx_header(text, speed_key=None, speed_n=0, speed_what=""):
        """From the steps: up to the header, name it, SPEED (→ then + or _), down to the steps."""
        r.key("up", "Name the effect")
        r.name_it(text, "Name the effect")
        if speed_key:
            r.key("right", speed_what)
            r.rep(speed_key, speed_n)
        r.key("down", "back to the steps")

    # ------------------------------------------------------------------------------------------ open (0:12-0:34)
    s.chapter("Open the sound bank")
    s.scene(*SCENES["open"])
    s.say("bm Sound edits the sound bank of a cartridge. Control O opens the one we saved before.")
    r.at(12)
    s.raw("\\x0f", "Ctrl+O", "Ctrl+O: open a cartridge", 40)
    r.at(15)
    r.key("enter", "Enter: open TROPIC", hold=6)
    r.at(20)
    s.say("Four pages: F1 sounds, F2 effects, F3 patterns, F4 songs.")
    r.page(1, "F1: SOUNDS, the instruments")
    r.at(22)
    r.page(2, "F2: SFX, the effects")
    r.at(24)
    r.page(3, "F3: PATTERN, eight tracks")
    r.at(26)
    r.page(4, "F4: SONG, the order of patterns")
    r.shot("r06")
    r.at(30)
    s.chapter("Six soft sound effects")
    s.say("An effect is an instrument plus a few notes. Instruments first.")
    r.page(1, "An effect = an instrument + notes")

    # ---------------------------------------------------------------------------------------------------- APPLE
    s.say("APPLE: a soft bell. Wave FM, a short decay, no sustain, a little room and echo. Two notes, a fifth apart.")
    r.at(34)
    r.key("f1", "Sound 00: APPLE", hold=6)
    r.at(35)
    r.goto("NAME", "Name: 8 characters")
    r.name_it("APPLE   ", "Name: 8 characters")
    r.at(36)
    r.setv("WAVE", "=", 6, "WAVE: FM")
    r.setv("VOLUME", "_", 9, "VOLUME 22%: soft")
    r.setv("DECAY", "+", 4, "DECAY 314 ms")
    r.at(37)
    r.setv("SUSTAIN", "_", 16, "SUSTAIN 0%")
    r.setv("RELEASE", "+", 2, "RELEASE 235 ms")
    r.at(38)
    r.setv("FM RATIO", "+", 2, "FM RATIO 2, FM FADE 157 ms")
    r.setv("FM FADE", "+", 2)
    r.at(39)
    r.setv("ROOM", "+", 4, "ROOM 41%, ECHO 25%")
    r.setv("ECHO", "+", 4)
    r.at(40)
    r.listen("I: listen", "APPLE")
    r.shot("r16")
    r.at(41)
    r.page(2, "F2: sound effect 00")
    r.piano("e", 4, "E5 then B5")
    r.piano("u", 4)
    r.at(42)
    sfx_header("APPLE   ", "+", 2, "SPEED 80 ms")
    r.at(44)
    r.play("Space: play", "APPLE")
    r.shot("r21")

    # ---------------------------------------------------------------------------------------------------- CRATE
    s.say("CRATE: filtered noise. A band-pass filter that closes fast makes the wooden crack. Three quick hits.")
    r.at(47)
    r.page(1)
    r.key("pgdn", "PgDn: next sound")
    r.at(48)
    r.goto("NAME")
    r.name_it("CRATE   ", "Name: CRATE")
    r.at(49)
    r.setv("WAVE", "=", 3, "WAVE: NOISE, VOLUME 66%")
    r.setv("VOLUME", "_", 8)
    r.at(50)
    r.setv("DECAY", "+", 2, "Short decay, no sustain")
    r.setv("SUSTAIN", "_", 16)
    r.at(51)
    r.setv("CUTOFF", "+", 11, "Band-pass at 706 Hz")
    r.setv("RESONANCE", "+", 6)
    r.setv("MODE", "=", 1)
    r.at(52)
    r.setv("FILT ENV", "_", 3, "The filter closes fast")
    r.setv("ENV DECAY", "+", 2)
    r.setv("DRIVE", "+", 2)
    r.at(53)
    r.setv("ROOM", "+", 2, "ROOM 28%")
    r.at(54)
    r.listen("I: listen", "CRATE")
    r.at(55)
    r.page(2)
    r.key("pgdn", "PgDn: next effect")
    r.rep("left", 8, "Three hits")
    r.piano("z", 4)
    r.piano("c", 4)
    r.piano("v", 4)
    r.at(57)
    sfx_header("CRATE   ", "_", 2, "SPEED 40 ms")
    r.at(59)
    r.play("Space: play", "CRATE")
    r.shot("r33")

    # ---------------------------------------------------------------------------------------------------- JUMP
    s.say("JUMP: a pure sine that glides up. Bend from minus twelve semitones, over a tenth of a second.")
    r.at(60)
    r.page(1)
    r.key("pgdn", "Sound 02: JUMP")
    r.at(61)
    r.goto("NAME")
    r.name_it("JUMP    ", "Name: JUMP")
    r.at(62)
    r.setv("WAVE", "=", 4, "WAVE: SINE")
    r.setv("VOLUME", "_", 11)
    r.setv("RELEASE", "+", 1)
    r.at(63)
    r.setv("BEND FROM", "_", 1, "BEND FROM -12, TIME 100 ms")
    r.setv("BEND TIME", "+", 1)
    r.at(64)
    r.setv("ROOM", "+", 1, "ROOM 22%")
    r.at(65)
    r.listen("I: listen to the glide", "JUMP")
    r.at(67)
    r.page(2)
    r.key("pgdn", "PgDn: next effect")
    r.rep("left", 8, "G4 then C5")
    r.piano("b", 4)
    r.piano("q", 4)
    r.at(69)
    sfx_header("JUMP    ", "_", 1, "SPEED 50 ms")
    r.at(71)
    r.play("Space: play", "JUMP")
    r.shot("r43")

    # ---------------------------------------------------------------------------------------------------- STEP
    s.say("STEP: brown noise through a low-pass. Very short, very quiet. You will hear it a lot, so keep it soft.")
    r.at(73)
    r.page(1)
    r.key("pgdn", "Sound 03: STEP")
    r.at(74)
    r.goto("NAME")
    r.name_it("STEP    ", "Name: STEP")
    r.at(75)
    r.setv("WAVE", "=", 12, "WAVE: BROWN, VOLUME 53%")
    r.setv("VOLUME", "_", 5)
    r.at(76)
    r.setv("DECAY", "+", 1, "Very short")
    r.setv("SUSTAIN", "_", 16)
    r.at(77)
    r.setv("CUTOFF", "+", 8, "Low-pass at 265 Hz")
    r.at(78)
    r.listen("I: listen", "STEP")
    r.at(79)
    r.page(2)
    r.key("pgdn", "PgDn: next effect")
    r.rep("left", 8, "One step")
    r.piano("z", 4)
    r.at(81)
    sfx_header("STEP    ")
    r.at(83)
    r.play("Space: play", "STEP")
    r.shot("r53")

    # ---------------------------------------------------------------------------------------------------- SPIN
    s.say("SPIN: pink noise through a sweeping band-pass, four soft pulses: a whoosh.")
    r.at(86)
    r.page(1)
    r.key("pgdn", "Sound 04: SPIN")
    r.at(87)
    r.goto("NAME")
    r.name_it("SPIN    ", "Name: SPIN")
    r.at(88)
    r.setv("WAVE", "=", 11, "WAVE: PINK")
    r.setv("VOLUME", "_", 6)
    r.at(89)
    r.setv("ATTACK", "+", 2, "A soft swell")
    r.setv("RELEASE", "+", 2)
    r.at(90)
    r.setv("CUTOFF", "+", 12, "Band-pass at 978 Hz")
    r.setv("RESONANCE", "+", 5)
    r.setv("MODE", "=", 1)
    r.at(91)
    r.setv("FILT ENV", "+", 4, "The filter sweeps")
    r.setv("ENV DECAY", "+", 5)
    r.at(92)
    r.setv("ROOM", "+", 2, "ROOM 28%, ECHO 19%")
    r.setv("ECHO", "+", 3)
    r.at(93)
    r.listen("I: listen", "SPIN")
    r.at(94)
    r.page(2)
    r.key("pgdn", "PgDn: next effect")
    r.rep("left", 8, "Four pulses")
    for _ in range(4):
        r.piano("n", 4)
    r.at(96)
    sfx_header("SPIN    ", "+", 2, "SPEED 80 ms")
    r.at(98)
    r.play("Space: play", "SPIN")
    r.shot("r65")

    # ---------------------------------------------------------------------------------------------------- HIT
    s.say("HIT: a falling triangle with a little noise. Gentle, because Pico should never feel punished.")
    r.at(99)
    r.page(1)
    r.key("pgdn", "Sound 05: HIT")
    r.at(100)
    r.goto("NAME")
    r.name_it("HIT     ", "Name: HIT")
    r.at(101)
    r.setv("WAVE", "=", 1, "WAVE: TRIANGLE")
    r.setv("VOLUME", "_", 6)
    r.setv("DECAY", "+", 3)
    r.setv("SUSTAIN", "_", 16)
    r.setv("RELEASE", "+", 1)
    r.at(102)
    r.setv("BEND FROM", "+", 1, "It falls: BEND FROM +12")
    r.setv("BEND TIME", "+", 2)
    r.at(103)
    r.setv("CUTOFF", "+", 11, "A little noise")
    r.setv("NOISE", "+", 2)
    r.at(104)
    r.setv("ROOM", "+", 2, "ROOM 28%")
    r.at(105)
    r.listen("I: listen", "HIT")
    r.at(106)
    r.page(2)
    r.key("pgdn", "PgDn: next effect")
    r.rep("left", 8, "E3 then C3")
    r.piano("c", 3)
    r.piano("z", 3)
    r.at(109)
    sfx_header("HIT     ", "+", 3, "SPEED 90 ms")
    r.at(111)
    r.play("Space: play", "HIT")
    r.shot("r76")

    # --------------------------------------------------------------------------------------- instruments (1:52-2:08)
    s.chapter("Instruments for the song")
    s.say("Now the song. Five new instruments: kick, shaker, bass, marimba and a pad. The bell is the apple sound again. "
          "Each one is a wave, an envelope, a filter and a little room.")
    r.at(112)
    r.page(1, "KICK: sine + fast bend down")
    r.key("pgdn")
    r.goto("NAME")
    r.name_it("KICK    ", "KICK: sine + fast bend down")
    r.setv("WAVE", "=", 4)
    r.setv("VOLUME", "_", 12)
    r.setv("DECAY", "+", 3)
    r.setv("SUSTAIN", "_", 16)
    r.setv("BEND FROM", "+", 2, "BEND FROM +24, TIME 40 ms")
    r.setv("BEND TIME", "=", 4)
    r.at(116)
    r.key("pgdn", "SHAKER: high-passed noise")
    r.goto("NAME")
    r.name_it("SHAKER  ", "SHAKER: high-passed noise")
    r.setv("WAVE", "=", 3)
    r.setv("VOLUME", "_", 12)
    r.setv("DECAY", "=", 6)
    r.setv("SUSTAIN", "_", 16)
    r.setv("CUTOFF", "+", 16)
    r.setv("MODE", "=", 2)
    r.setv("ROOM", "_", 1)
    r.at(119)
    r.key("pgdn", "BASS: sine + a little drive")
    r.goto("NAME")
    r.name_it("BASS    ", "BASS: sine + a little drive")
    r.setv("WAVE", "=", 4)
    r.setv("VOLUME", "_", 12)
    r.setv("ATTACK", "=", 1)
    r.setv("RELEASE", "+", 2)
    r.setv("CUTOFF", "+", 12)
    r.setv("DRIVE", "+", 2)
    r.setv("ROOM", "_", 1)
    r.at(122)
    r.key("pgdn", "MARIMBA: pluck, a bit left")
    r.goto("NAME")
    r.name_it("MARIMBA ", "MARIMBA: pluck, a bit left")
    r.setv("WAVE", "=", 7)
    r.setv("VOLUME", "_", 12)
    r.setv("BRIGHT", "+", 6)
    r.setv("RELEASE", "+", 2)
    r.setv("PAN", "_", 2)
    r.setv("ROOM", "+", 2)
    r.setv("ECHO", "+", 3)
    r.at(125)
    r.key("pgdn", "PAD: soft supersaw")
    r.goto("NAME")
    r.name_it("PAD     ", "PAD: soft supersaw")
    r.setv("WAVE", "=", 8)
    r.setv("VOLUME", "_", 12)
    r.setv("ATTACK", "+", 4)
    r.setv("RELEASE", "+", 5)
    r.setv("CUTOFF", "+", 10)
    r.setv("RESONANCE", "+", 2)
    r.setv("PAN", "+", 2)
    r.setv("ROOM", "+", 6)
    r.shot("r82")

    # ---------------------------------------------------------------------------------------------- patterns (2:08-2:44)
    s.chapter("Patterns: one bar of jungle")
    s.say("A pattern is one bar: sixteen steps, eight tracks. We use six, one per voice. Kick, shaker, bass and pad first.")
    r.at(128)
    r.page(3, "One pattern = one bar")
    r.at(129)
    r.octave_to(2)                                           # `,`: OCT 3 -> 2 (the bar at the top says OCT)
    r.at(130)
    # track 1: KICK on 1, 7, 9 (the track starts with sound 0: six `=` reach sound 6)
    r.header(0, "=", 6, "Track 1: KICK on 1, 7, 9")
    r.key("z", "the note: C2", hold=14)
    r.cell_enter(1)
    r.cell_enter(7)
    r.cell_enter(9)
    r.at(132)
    r.header(1, "=", 6, "Track 2: SHAKER, off-beats")
    r.key("z", "", hold=14)
    for st in (3, 7, 11, 15):
        r.cell_enter(st)
    r.at(134)
    r.header(2, "=", 6, "Track 3: BASS, C C G A A G")
    for st, k in ((1, "z"), (4, "z"), (7, "b"), (9, "n"), (12, "n"), (15, "b")):
        r.cell(st, k)
    r.at(137)
    r.octave_to(4)                                           # `.` x2
    r.header(4, "=", 6, "Track 5: PAD holds E4")
    r.cell(1, "c")
    r.at(139)
    r.event("pattern", "00")
    r.key("space", "Space: play the bar", hold=6)
    r.at(141)
    r.key("space", "Space: stop", hold=6)
    r.at(142)
    s.say("Copy it with Control C and Control V, then add the marimba melody.")
    r.s.chord("c", "Ctrl+C: copy the pattern")
    r.at(143)
    r.key("pgdn", "PgDn: next pattern", hold=6)
    r.at(144)
    r.s.chord("v", "Ctrl+V: paste over")
    r.at(145)
    r.header(3, "=", 6, "Track 4: MARIMBA melody")
    for st, k in ((1, "e"), (4, "t"), (6, "y"), (8, "t"), (9, "e"), (12, "w"), (14, "q"), (15, "w")):
        r.cell(st, k)
    r.at(150)
    r.event("pattern", "01")
    r.key("space", "Space: play, Space: stop", hold=6)
    r.sec(3)
    r.key("space", "", hold=6)
    r.at(154)
    s.say("A third bar changes the pad and adds the apple bell as a sparkle.")
    r.s.chord("c", "Copy, next, paste")
    r.key("pgdn", "", hold=6)
    r.s.chord("v", "")
    r.at(156)
    r.cell(1, "b", "Pad moves to G4", track=4)                # the pad is track 5
    r.at(158)
    r.header(5, "-", 5, "Track 6: APPLE sparkle")
    r.cell(8, "u")
    r.cell(16, "e")
    r.at(161)
    r.event("pattern", "02")
    r.key("space", "Space: listen", hold=6)
    r.sec(2)
    r.key("space", "", hold=6)

    # ---------------------------------------------------------------------------------------------------- song
    s.chapter("The song JUNGLE")
    s.say("On F4 the song is a list of patterns. I copies a slot. Name it JUNGLE, a hundred beats a minute, a little swing, "
          "echo and room.")
    r.at(164)
    r.page(4, "F4: the song")
    r.at(165)
    # a new bank's song 0 already has its first slot (pattern 00): Enter there would open the pattern (NOTES.md)
    r.sec(0.5)
    target = [0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0]
    when = {1: 166, 6: 167, 8: 168, 12: 169, 16: 170, 20: 171}
    cur = 0
    for i in range(1, len(target)):
        if i in when:
            r.at(when[i])
        r.key("i", "i: copy the slot" if i == 1 else "", hold=2)
        d = target[i] - cur
        if d:
            r.rep("=" if d > 0 else "-", abs(d), "= : next pattern number" if d > 0 else "- : previous pattern number")
        cur = target[i]
    r.at(172)
    r.key("up", "SWING 20%", hold=3)
    r.quiet("up")
    r.rep("+", 2)
    r.at(173)
    r.key("left", "TEMPO 100 BPM")
    r.rep("_", 2)
    r.at(174)
    r.key("left", "Name: JUNGLE")
    r.name_it("JUNGLE  ", "Name: JUNGLE")
    r.at(175)
    r.rep("right", 4, "ECHO 3/16, ROOM 50%")
    r.rep("=", 3)
    r.key("right")
    r.rep("+", 2)
    r.shot("r111")
    r.at(176)
    r.event("song", "JUNGLE")
    r.key("space", "Space: play the song", hold=6)
    r.at(185)
    s.chapter("Honest limits")
    s.say("Honest limits: eight voices only. An effect borrows one and mutes the music on it, so the song uses six tracks. "
          "No WAV import here.")
    s.mark("8 voices", "Honest limits: eight voices; an effect borrows one")
    r.shot("r112")
    r.at(197)
    s.chapter("Save")
    s.say("Control S saves it into an editable copy.")
    s.scene(*SCENES["save"])
    r.event("song_stop", "JUNGLE")
    r.key("space", "Space: stop", hold=6)
    r.sec(0.3)
    s.chord("s", "Ctrl+S: save", )
    r.sec(0.8)
    r.shot("r114")
    # the kernel's question reads the keyboard (HID usage Enter), not the typed queue: `keys 0x28`
    s.lines.append("%d keys 0x28" % s.t)
    s.caps.append((s.t, "Enter", "Saved into an editable copy"))
    s.t += 8
    s.lines.append("%d keys none" % s.t)
    s.t += 2
    r.at(199)
    r.shot("r115")
    r.at(202)
    s.end()
    return r


# --------------------------------------------------------------------------------------------------------------- run
def make_cart(src_lua=None):
    """The bm Sound cartridge: made here from carts/sound (build/sound.bm has a wrong Lua, see the docstring)."""
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, "carts", "sound")
    out = os.path.join(OUT, "sound.bm")
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", out, "--lua",
                    src_lua or os.path.join(src, "main.lua"), "--cover", os.path.join(src, "icon.png"),
                    "--audio", os.path.join(src, "demo.json"), "--info", os.path.join(src, "bm.info")],
                   check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    return out


def start_cart(which):
    chain = os.path.join(lib3d.CHAIN, CHAIN_IN)
    if which is None:
        which = "chain" if os.path.exists(chain) else "forge"
    if which == "chain":
        return lib3d.chain_get(CHAIN_IN)
    if not os.path.exists(FORGE):
        subprocess.run([sys.executable, os.path.join(ROOT, "video", "3d", "assets", "build.py")], check=True, cwd=ROOT,
                       stdout=subprocess.DEVNULL)
    return FORGE


def project_to_bm(bme, dst):
    """The saved project as a cartridge: the PROJECT flag (u16 at byte 18, bit 0) off. The CRC covers what follows the header."""
    data = bytearray(open(bme, "rb").read())
    data[18] &= 0xFE
    open(dst, "wb").write(bytes(data))


def write_input(s, path):
    """Script.write_input with LF line ends whatever the OS (bmhost types a stray CR, an Enter, at the end of a CRLF line)."""
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(s.lines) + "\n")


def run_bmhost(cart, inp, seconds, raw=None, log=False):
    cmd = [lib3d.BMHOST, cart, "--tool", "--sd", SD, "--seconds", str(seconds), "--input", inp, "--shots", SHOTS,
           "--wav", WAV]
    if raw:
        cmd += ["--video", raw]
    if not log:
        cmd.append("--quiet")
    print("+", " ".join(os.path.relpath(c, ROOT) if os.path.isabs(c) and c.startswith(ROOT) else c for c in cmd),
          file=sys.stderr)
    try:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=240)
    except subprocess.TimeoutExpired:
        if os.path.exists(WAV):
            os.remove(WAV)                  # a run that does not end writes gigabytes of silence
        raise SystemExit("bmhost did not end in 240 s: a modal question without an answer? (docstring: `keys 0x28`)")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--start", choices=("forge", "chain"), help="the cartridge to start from (default: chain if there)")
    ap.add_argument("--build-only", action="store_true", help="only write input.txt, overlay.json, events.json")
    ap.add_argument("--stage", type=float, help="stop the run at this second of the recording")
    ap.add_argument("--no-video", action="store_true", help="no mp4 (the default where ffmpeg is missing)")
    ap.add_argument("--log", action="store_true", help="bmhost without --quiet (the editor's log lines)")
    ap.add_argument("--cart", help="a bm Sound cartridge to use instead of the one made here")
    a = ap.parse_args()
    r = build()
    s = r.s
    os.makedirs(OUT, exist_ok=True)
    inp = os.path.join(OUT, "input.txt")
    write_input(s, inp)
    lib3d.write_overlay(s, os.path.join(OUT, "overlay.json"), TITLE)
    json.dump({"fps": FPS, "frames": s.t, "events": r.events}, open(os.path.join(OUT, "events.json"), "w"), indent=1)
    for sec, over in r.late:
        print("LATE: the row of %.0f:%02d starts %.1f s late" % (sec // 60, sec % 60, over), file=sys.stderr)
    print("script: %d frames = %.1f s (target 190 s)" % (s.t, s.t / FPS))
    if a.build_only:
        return 0
    seconds = a.stage or (s.t + 60) / FPS
    video = not a.no_video and lib3d.have_ffmpeg()
    shutil.rmtree(SHOTS, ignore_errors=True)
    os.makedirs(SHOTS)
    lib3d.fresh_sd(SD, [(start_cart(a.start), "TROPIC.BM")])
    cart = a.cart or make_cart()
    raw = os.path.join(OUT, "raw.rgb") if video else None
    p = run_bmhost(cart, inp, seconds, raw, a.log)
    print((p.stdout + p.stderr)[-1500:])
    if p.returncode:
        return p.returncode
    if not os.path.exists(BME):
        print("no TROPIC.BME on the SD: the save did not happen", file=sys.stderr)
        return 1
    lib3d.chain_put(BME, "05-sound.bme")
    bm = os.path.join(OUT, "05-sound.bm")
    project_to_bm(BME, bm)
    lib3d.chain_put(bm, CHAIN_OUT)
    if video:
        total = s.t + 30
        write_ass(s, os.path.join(OUT, "overlay.ass"), TITLE, SUBTITLE, total)
        main_mp4 = os.path.join(OUT, "main.mp4")
        call(encode(raw, os.path.join(OUT, "overlay.ass"), main_mp4, audio=WAV))
        os.remove(raw)
        at = next(e["frame"] for e in r.events if e["kind"] == "song") / FPS + 2
        intro = [("TROPIC TRAIL", 96, "0xffc050", 380), ("Episode 5  ·  bm Sound", 56, "0xe0e4f0", 520),
                 ("Six sounds and a jungle song", 34, "0x9098b0", 640)]
        outro = [("Next: bm SDK", 56, "0xffc050", 400), ("Light, fog and shadow", 38, "0xe0e4f0", 520),
                 ("github.com/f-accomando/bm", 34, "0x9098b0", 660)]
        for cmd in (hook_cmd(main_mp4, os.path.join(OUT, "hook.mp4"), at, 8, "TODAY: SOUND FOR TROPIC TRAIL"),
                    card_cmd(os.path.join(OUT, "intro.mp4"), 4, intro, silent=True),
                    card_cmd(os.path.join(OUT, "outro.mp4"), 8, outro, silent=True)):
            call(cmd)
        call(concat_cmd([os.path.join(OUT, n) for n in ("hook.mp4", "intro.mp4", "main.mp4", "outro.mp4")],
                        os.path.join(OUT, "ep05.mp4"), audio=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
