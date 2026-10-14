#!/usr/bin/env python3
"""Episode 3 of Tropic Trail: bm Animator. Pico gets a skeleton of 9 bones and five clips, made with the console's own
editor (carts/animator/main.lua) run by bmhost --tool. Standard library only (+ bm_core of bm-forge to read the model).

  python3 video/3d/03-animator/record.py [--start chain|forge] [--build-only] [--no-video] [--debug] [--seconds N]

Source of the keys and times: docs/video/03-animator/shot-list.md (one block of code per row, "# row N" comments) and
script.md (the narration). The recording starts at 0:08 of the final video (the 8 seconds before are the hook, a cut of the
last seconds of this recording): the times below are the video's, `at()` converts them. Corrections found by running the
editor are in NOTES.md (spin, signs of w/s, the dialog of the editable copy ...).

What it does
  1. builds the Script (keys, captions, scenes, narration) -> out/input.txt (the bmhost script, deterministic) and
     out/overlay.json (the key overlay data, lib3d.write_overlay);
  2. a clean SD (lib3d.fresh_sd) with the start cartridge as /carts/TROPIC.BM:
       --start chain  video/3d/chain/02-pixel.bm (default when it exists)
       --start forge  the stand-in: video/3d/out/assets/models.bm (python video/3d/assets/build.py): PICO, APPLE, CRATE
                      painted by bm-forge, NO skeleton, a .bm = a game, which the Animator opens read only
                      (the first Ctrl+S makes the editable copy TROPIC.BME, as in the video);
  3. bmhost --tool (lib3d.run_headless) with --shots: no video unless ffmpeg is there (then out/ep03_main.mp4);
  4. the project the editor saved (out/sd/carts/TROPIC.BME) goes to video/3d/chain/03-animator.bme; the .bm
     (chain/03-animator.bm) is that project built like bm SDK's Ctrl+B (src/bm/project.c bm_build: project bit cleared,
     "built from" name written, nothing else changed): the SDK is another program, so the same step is done here.
  5. verify.py is run on it.
The editor cartridge is compiled here (scripts/mkbm.py -> out/animator.bm): build/animator.bm is not needed.
--debug: the editor with a log of its state (dbg.lua) and bmhost without --quiet (to calibrate the numbers).
"""
import argparse
import math
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import lib3d as L  # noqa: E402  (also puts video/lib and scripts on the path)
from bmvideo import Script, FPS, write_ass, encode, call  # noqa: E402

sys.path.insert(0, os.path.join(L.ROOT, ".claude", "skills", "bm-forge"))
from bm_core import container  # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
TITLE = "Tropic Trail  ·  Episode 3: bm Animator"
HOOK_S = 8                       # seconds of the final video before the recording (the hook, cut from its last seconds)
TOTAL_VIDEO_S = 210              # 3:30

GAP = 9                          # frames between the presses of a series (the viewer sees each step)
FINAL_BONES = ["root", "spine", "head", "arm.L", "arm.R", "leg.L", "leg.R", "tail1", "tail2"]
CLIPS = ["idle", "run", "jump", "spin", "hit"]

# Pico of videos 1-2 is in grid units, x -1..4 (middle 1.5), z -3..1; the body axis is x = 1.5, z = 0.5
CX, CZ = 1.5, 0.5
KEYS_F1 = [("← →", "clip"), ("Space", "play / stop"), ("K", "skeleton"), ("B", "blend"), ("< >", "speed")]
KEYS_F2 = [("N", "bone"), ("↑ ↓", "choose"), ("W A S D R F", "move the end"), ("M", "mirror"), ("Enter", "rename"), ("K", "skin")]
KEYS_F3 = [("N", "clip"), ("W S A D", "turn the bone"), ("→ ←", "frame"), ("M", "mirror pose"), ("L", "loop"), ("Space", "play")]


def at_frame(video_time):
    """'m:ss' (or seconds) of the final video -> frame of the recording."""
    if isinstance(video_time, str):
        m, sec = video_time.split(":")
        video_time = int(m) * 60 + float(sec)
    return round((video_time - HOOK_S) * FPS)


# ----------------------------------------------------------------------- the model
def pico_info(path):
    """(index of PICO among the models, lo, hi of its faces): as T.bounds_of of bm3d.lua."""
    cart = container.read_file(path)
    mesh = cart.mesh()
    names = [m.name for m in mesh.models]
    if "PICO" not in names:
        raise SystemExit("the start cartridge has no model PICO: %s" % names)
    idx = names.index("PICO")
    m = mesh.models[idx]
    used = sorted({i for f in m.faces for i in (f.a, f.b, f.c)})
    lo = [min(m.verts[i][k] for i in used) for k in range(3)]
    hi = [max(m.verts[i][k] for i in used) for k in range(3)]
    return idx, names, lo, hi


def g8(v):
    """the editor's round to 1/8 (floor(v + 0.5))."""
    return math.floor(v * 8 + 0.5) / 8


def mirror_name(n):
    if n.endswith(".L"):
        return n[:-2] + ".R"
    if n.endswith(".R"):
        return n[:-2] + ".L"
    return None


# ----------------------------------------------------------------------- F2: the bones
class RigDriver:
    """Presses the keys of F2 and keeps the same numbers as the editor (new_bone, rig_move, mirror_bones of main.lua)
    so that every count of presses is computed, not guessed."""

    def __init__(self, s, lo, hi):
        self.s, self.lo, self.hi = s, lo, hi
        self.bones = []          # dicts: name, parent (1-based, 0 none), head, tail
        self.sel = 0             # chosen bone, 1-based
        self.tail = True         # the end we move

    def idx(self, name):
        return [b["name"] for b in self.bones].index(name) + 1

    def press(self, k, n=1, what="", gap=GAP, cap=None):
        for i in range(n):
            self.s.key(k, what if i == 0 else "", cap=cap, hold=gap)

    def new(self, what="N · a bone under the chosen one"):
        s = self.s
        if not self.bones:
            cx, cz = (self.lo[0] + self.hi[0]) / 2, (self.lo[2] + self.hi[2]) / 2
            self.bones.append({"name": "root", "parent": 0, "head": [g8(cx), g8(self.lo[1]), g8(cz)],
                               "tail": [g8(cx), g8(self.lo[1] + max(0.25, (self.hi[1] - self.lo[1]) * 0.5)), g8(cz)]})
            self.sel = 1
        else:
            p = self.bones[self.sel - 1]
            d = [p["tail"][k] - p["head"][k] for k in range(3)]
            ln = math.sqrt(sum(x * x for x in d))
            d = [x * 0.5 / ln for x in d] if ln > 1e-6 else [0, 0.5, 0]
            tail = [p["tail"][k] + g8(d[k]) for k in range(3)]
            self.bones.append({"name": "bone%d" % (len(self.bones) + 1), "parent": self.sel,
                               "head": list(p["tail"]), "tail": tail})
            self.sel = len(self.bones)
            self.tail = True
        s.key("n", what, hold=GAP)

    def rename(self, name, what):
        """Enter, the proposed name 'boneN' is already written and the text is appended: 5 Backspace (bm3d.lua input_key)."""
        s = self.s
        assert len(self.bones[self.sel - 1]["name"]) == 5, "the proposed name is not 5 letters"
        s.key("enter", what, hold=GAP)
        for _ in range(5):
            s.key("bksp", hold=4)
        s.type_text(name, "name")
        s.key("enter", hold=GAP)
        self.bones[self.sel - 1]["name"] = name

    def choose(self, name, what=""):
        """up / down to the bone (the list wraps: the shorter way)."""
        want, n = self.idx(name), len(self.bones)
        up = (self.sel - want) % n
        down = (want - self.sel) % n
        if up <= down:
            self.press("up", up, what, gap=8)
        else:
            self.press("down", down, what, gap=8)
        self.sel = want

    def move_to(self, target, what="", gap=GAP, order=(0, 1, 2)):
        """w a s d r f from the chosen end to `target` (x y z), in steps of 1/8; the ends at the same point go together,
        so the path must not cross another joint (a tail going down through the hips would drag them): `order` is the axes."""
        b = self.bones[self.sel - 1]
        key = "tail" if self.tail else "head"
        cur = list(b[key])
        steps = [round((target[k] - cur[k]) / 0.125) for k in range(3)]
        if gap == GAP and sum(abs(n) for n in steps) > 8:
            gap = 4                                   # a long run of the same key goes faster: the viewer sees the count, not each press
        first = True
        for axis in order:
            plus, minus = (("d", "a"), ("w", "s"), ("r", "f"))[axis]
            n = steps[axis]
            if n:
                self.press(plus if n > 0 else minus, abs(n), what if first else "", gap)
                first = False
        new = [cur[k] + steps[k] * 0.125 for k in range(3)]
        for o in self.bones:
            for e in ("head", "tail"):
                if sum((o[e][k] - cur[k]) ** 2 for k in range(3)) < 1e-8:
                    o[e] = list(new)

    def toggle_end(self, what=""):
        self.s.key("tab", what, hold=GAP)
        self.tail = not self.tail

    def mirror(self, what):
        chain = [self.sel]
        for k, b in enumerate(self.bones, 1):
            if b["parent"] in chain:
                chain.append(k)
        mp = {}
        for k in chain:
            b = self.bones[k - 1]
            name = mirror_name(b["name"])
            assert name, "a bone without a side"
            self.bones.append({"name": name, "parent": mp.get(b["parent"], b["parent"]),
                               "head": [-b["head"][0], b["head"][1], b["head"][2]],
                               "tail": [-b["tail"][0], b["tail"][1], b["tail"][2]]})
            mp[k] = len(self.bones)
        self.s.key("m", what, hold=GAP)


# ----------------------------------------------------------------------- F3: the clips
class AnimDriver:
    def __init__(self, s, names):
        self.s, self.names = s, names
        self.bone = 1            # chosen bone, 1-based (an.bone)
        self.frame = 0           # the time cursor, in frames

    def press(self, k, n=1, what="", gap=GAP, cap=None):
        for i in range(n):
            self.s.key(k, what if i == 0 else "", cap=cap, hold=gap)

    def choose(self, name, what=""):
        n = len(self.names)
        want = self.names.index(name) + 1
        up, down = (self.bone - want) % n, (want - self.bone) % n
        if up <= down:
            self.press("up", up, what, gap=5)
        else:
            self.press("down", down, what, gap=5)
        self.bone = want

    def new_clip(self, name, what):
        s = self.s
        s.key("n", what, hold=GAP)
        s.key("enter", "Enter · name: " + name, hold=GAP)
        for _ in range(5):
            s.key("bksp", hold=4)
        s.type_text(name, "name")
        s.key("enter", hold=GAP)
        self.frame = 0

    def go(self, to, what=""):
        """frames: right / left arrows, or Home / End."""
        if to == "home":
            self.s.key("home", what, hold=GAP); self.frame = 0
        elif to == "end":
            self.s.key("end", what, hold=GAP); self.frame = None
        else:
            d = to - self.frame
            self.press("right" if d > 0 else "left", abs(d), what, gap=5)
            self.frame = to

    def turn(self, bone, key, n, what="", gap=GAP, cap=None):
        self.choose(bone)
        self.press(key, n, what, gap, cap)


def build(info):
    pico_idx, names, lo, hi = info
    s = Script()
    warn = []

    def at(video_time, what=""):
        """wait until the recording frame of `video_time`; a row that runs over its slot is reported."""
        f = at_frame(video_time)
        if s.t > f + 60:
            warn.append("row at %s starts %.1f s late (%s)" % (video_time, (s.t - f) / FPS, what))
        elif s.t < f:
            s.wait(f - s.t)

    rig = RigDriver(s, lo, hi)
    an = AnimDriver(s, FINAL_BONES)
    P = rig.press

    # ================================================================ 0:08 the tool (rows 2, 3)
    s.chapter("What bm Animator does")
    s.scene("Dev ▸ bm Animator", [("↑ ↓", "choose a file"), ("Enter", "open it"), ("Esc", "back")])
    s.say("bm Animator lives in the Dev tab. It does not build models: it opens the ones you made in bm Studio. "
          "Here is Pico, still a statue.")
    s.mark("Dev ▸ bm Animator", "Dev tab ▸ bm Animator")                     # row 2: a card, bmhost has no Dev tab
    at("0:11", "row 3")
    s.key("enter", "Open the cartridge", hold=GAP)                           # row 3: the list has one file, already chosen
    s.shot("r03_open")
    # ================================================================ 0:14 F1 (rows 4, 5)
    at("0:14", "row 4")
    s.scene("bm Animator ▸ F1 play", [("F1", "play"), ("F2", "rig"), ("F3", "animate"), ("F4", "sprites"), ("] [", "model")])
    s.shot("r04_statue")
    at("0:17", "row 5")
    s.key("]", "] [ : next / previous model", hold=60)
    s.key("[", hold=60)
    for _ in range(pico_idx):                                                # PICO is the first model of the stand-in
        s.key("]", hold=20)
    # ================================================================ 0:22 the pages (rows 6-9)
    at("0:22", "row 6")
    s.chapter("The four pages")
    s.say("Four pages. F1 plays, F2 builds the skeleton, F3 records clips, and F4 bakes sprites. We need the first three.")
    s.key("f2", "F2 · rig: bones and skin", hold=1)
    s.scene("bm Animator ▸ F2 rig", KEYS_F2)
    s.shot("r06_noskel")
    at("0:24", "row 7")
    s.key("f3", "F3 · animate: keyframes", hold=1)
    s.scene("bm Animator ▸ F3 animate", KEYS_F3)
    at("0:26", "row 8")
    s.key("f4", "F4 · sprites (not used today)", hold=1)
    s.scene("bm Animator ▸ F4 sprites", [("↑ ↓", "setting"), ("← →", "change"), ("Enter", "bake")])
    at("0:28", "row 9")
    s.key("f2", "Back to F2", hold=1)
    s.scene("bm Animator ▸ F2 rig", KEYS_F2)
    # ================================================================ 0:30 the bones (rows 10-27)
    at("0:30", "row 10")
    s.chapter("Build the skeleton")
    s.say("On F2, press N. The first bone is called root. Press W to lift its tail to Pico's hips: "
          "each press moves it an eighth of a unit.")
    rig.new("N · make the skeleton")                                         # root, from the floor, tail half way up
    s.shot("r10_root")
    at("0:33", "row 11")
    rig.move_to([rig.bones[0]["tail"][0], 1.5, rig.bones[0]["tail"][2]], "W S · move the tail (1/8 unit per press)")
    rig.move_to([CX, 1.5, CZ], "R F · to the middle of the body")           # Pico's box is not centred on z: tail ...
    rig.toggle_end("Tab · work on the head")
    rig.move_to([CX, 0.0, CZ], "R F · ... and head on the axis")
    rig.toggle_end("Tab · back to the tail")
    s.shot("r11_hips")

    at("0:42", "row 12")
    s.say("N again adds a bone under the chosen one. The spine rises from the hips to the shoulders, and the head sits on top. "
          "Enter renames a bone.")
    rig.new("N · a bone under the chosen one")
    at("0:44", "row 13")
    rig.rename("spine", "Enter · rename the bone")
    at("0:47", "row 14")
    rig.move_to([CX, 2.5, CZ], "W S · tail to the shoulders")
    at("0:50", "row 15")
    rig.new("N · Enter · the head")
    rig.rename("head", "Enter · rename")
    rig.move_to([CX, 4.0, CZ], "W S · the top of the head")
    s.shot("r15_head")

    at("0:54", "row 16")
    s.say("Choose the spine and add an arm. The names end in dot L and dot R. M copies a bone across the middle of the world, "
          "but Pico stands off to one side, so each right bone is made by hand. Choose the root and do the same for the legs.")
    rig.choose("spine", "↑ ↓ · choose the bone")
    at("0:57", "row 17")
    rig.new("N · arm.L")
    rig.rename("arm.L", "Enter · rename")
    at("1:00", "row 18")
    rig.move_to([CX + 2.25, 2.25, CZ], "D · place the hand")
    at("1:03", "row 19")
    rig.choose("spine", "↑ ↓ · choose the spine")
    rig.new("N · arm.R")
    rig.rename("arm.R", "Enter · rename")
    rig.move_to([CX - 2.25, 2.25, CZ], "A · the other hand")
    s.shot("r19_arms")
    at("1:05", "row 20")
    rig.choose("root", "Choose root · N · leg.L")
    rig.new("N · leg.L")
    rig.rename("leg.L", "Enter · rename")
    at("1:08", "row 21")
    rig.move_to([CX + 1.0, 0.375, CZ], "S D · place the foot")
    at("1:10", "row 22")
    rig.choose("root", "Choose root · N · leg.R")
    rig.new("N · leg.R")
    rig.rename("leg.R", "Enter · rename")
    rig.move_to([CX - 1.0, 0.375, CZ], "S A · the other foot")
    s.shot("r22_legs")

    at("1:12", "row 23")
    s.say("The tail is two bones on the root. Nine bones in all. The limit is sixty-four, so there is plenty of room.")
    rig.choose("root", "Choose root · N · tail1")
    rig.new("N · tail1")
    rig.rename("tail1", "Enter · rename")
    at("1:15", "row 24")
    rig.move_to([CX, 1.625, -1.5], "F · tail backwards", order=(2, 1, 0))
    at("1:17", "row 25")
    rig.new("N · tail2")
    rig.rename("tail2", "Enter · rename")
    rig.move_to([CX, 1.75, -2.875], "F · the tip of the tail", order=(2, 1, 0))
    at("1:19", "row 26")
    s.mark("9", "9 bones (the limit is 64)")
    s.shot("r26_bones")
    s.wait(60)

    # ================================================================ 1:20 skin (rows 27-34)
    at("1:20", "row 27")
    s.chapter("Skin the model")
    s.say("Now the skin. Press K, and every face follows its nearest bone. That is right for a blocky gecko.")
    s.key("k", "K · each face follows its nearest bone", hold=GAP)
    s.shot("r27_skin")
    at("1:24", "row 28")
    s.scene("bm Animator ▸ F1 play", KEYS_F1)
    s.key("f1", "F1 · play", hold=1)
    at("1:26", "row 29")
    s.key("k", "K · show the skeleton", hold=GAP)
    s.shot("r29_skeleton")
    at("1:30", "row 30")
    s.say("Capital K splits the faces by corner, so joints stretch, but a corner always follows one bone, never a mix. "
          "If a part follows the wrong bone, V lets you choose its faces by hand.")
    s.scene("bm Animator ▸ F2 rig", KEYS_F2)
    s.key("f2", "F2", hold=1)
    s.key("K", "Shift+K · each corner follows one bone", cap="Shift+K", hold=GAP)
    at("1:34", "row 31")
    s.chord("z", "Ctrl+Z · undo")
    at("1:37", "row 32")
    s.key("v", "V · choose faces by hand", hold=GAP)
    at("1:40", "row 33")
    s.key("right", "Arrows · Space: mark a face", hold=GAP)
    s.key("space", hold=GAP)
    s.shot("r33_mark")
    at("1:43", "row 34")
    s.key("bksp", "Backspace · clear", hold=GAP)
    s.key("v", "V · back to the bones", hold=GAP)

    # ================================================================ 1:45 idle (rows 35-40)
    at("1:45", "row 35")
    s.chapter("Idle")
    s.say("F3 is the timeline, twelve frames a second. N makes a clip, Enter names it. Idle is a quiet breath: "
          "turn the spine a little and a keyframe appears.")
    s.scene("bm Animator ▸ F3 animate", KEYS_F3)
    s.key("f3", "F3", hold=GAP)
    an.new_clip("idle", "N · new clip")
    s.shot("r36_idle")
    at("1:50", "row 37")
    an.press(">", 12, "> · longer by one frame", gap=6)
    at("1:53", "row 38")
    an.choose("spine", "↓ · the spine")
    an.go(12, "→ · next frame")
    at("1:55", "row 39")
    s.key("W", "Shift+W · turn 5 degrees", cap="Shift+W", hold=GAP)
    s.shot("r39_key")
    at("1:57", "row 40")
    s.key("space", "Space · play", hold=3 * FPS)
    s.key("space", "Space · stop", hold=GAP)

    # ================================================================ 2:00 run (rows 41-50)
    at("2:00", "row 41")
    s.chapter("Run (loop)")
    s.say("Run is eight frames and it loops. Pose one: arms and legs opposed. Go to frame four and press M to mirror the pose. "
          "Space plays it, and the loop closes by itself.")
    an.new_clip("run", "N · new clip")
    at("2:04", "row 42")
    an.press("<", 4, "< · shorter", gap=5)
    at("2:06", "row 43")
    an.go("home", "Home · frame 0")
    an.turn("leg.L", "s", 2, "S · 30 degrees: the left leg forward (Pico faces +z)")
    at("2:08", "row 44")
    an.turn("leg.R", "w", 2, "W · the other way")
    at("2:10", "row 45")
    an.turn("arm.L", "w", 2, "Arms against legs")
    an.turn("arm.R", "s", 2)
    s.shot("r45_pose1")
    at("2:12", "row 46")
    an.go(4, "→ ×4 · frame 4")
    at("2:14", "row 47")
    s.key("m", "M · mirror the pose", hold=GAP)
    s.shot("r47_pose2")
    at("2:15", "row 48")
    s.key("space", "Space · play", hold=2 * FPS)
    at("2:17", "row 49")
    an.press("i", 3, "I · linear / smooth / step (back to smooth)", gap=30)
    at("2:18", "row 50")
    s.key("o", "O · onion skin", hold=GAP)
    s.shot("r50_onion")
    s.wait(90)
    s.key("o", "O · onion skin off", hold=GAP)

    # ================================================================ 2:20 jump (rows 51-56)
    at("2:20", "row 51")
    s.chapter("Jump")
    s.say("Jump does not loop, so press L. A crouch, a stretch, and a landing.")
    an.new_clip("jump", "N · new clip")
    at("2:23", "row 52")
    s.key("l", "L · loop off", hold=GAP)
    at("2:25", "row 53")
    an.go("home", "Home · frame 0")
    an.turn("leg.L", "s", 2, "S · crouch: the thighs forward")
    an.turn("leg.R", "s", 2)
    an.turn("spine", "w", 1)
    s.shot("r53_crouch")
    at("2:27", "row 54")
    an.go(4, "→ ×4 · frame 4")
    an.turn("leg.L", "r", 1, "R · rest")
    an.turn("leg.R", "r", 1)
    an.turn("arm.L", "s", 8, "S · arms up", gap=4)
    an.turn("arm.R", "s", 8, gap=4)
    s.shot("r54_stretch")
    at("2:30", "row 55")
    an.go(8, "→ ×4 · frame 8")
    for b in ("arm.R", "arm.L", "leg.R", "leg.L", "spine"):
        an.turn(b, "r", 1, "R · back to rest" if b == "arm.R" else "")
    s.key("k", "K · keyframe", hold=GAP)
    at("2:32", "row 56")
    s.key("space", "Space · play", hold=2 * FPS)

    # ================================================================ 2:33 spin (rows 57-61)
    at("2:33", "row 57")
    s.chapter("Spin")
    s.say("Spin turns the root a full circle: a third of a turn, three times. Three keyframes make the whole circle.")
    an.new_clip("spin", "N · new clip")
    at("2:36", "row 58")
    s.key("l", "L · loop off", hold=GAP)
    an.press("<", 6, "< ×6 · 6 frames", gap=5)
    at("2:38", "row 59")
    an.choose("root", "↑ · the root")
    an.go("home", "Home · frame 0")
    an.go(2, "→ ×2 · frame 2")
    an.press("a", 8, "A · turn around y (15° per press): 120°", gap=6)
    at("2:41", "row 60")
    an.go(4, "→ ×2 · frame 4")
    an.press("a", 8, "A · another 120°", gap=6)
    an.go("end", "End · the last frame")
    an.press("a", 8, "A · and the last 120°: a full circle", gap=6)
    s.shot("r60_circle")
    at("2:43", "row 61")
    s.key("space", "Space · play", hold=2 * FPS)

    # ================================================================ 2:45 hit (rows 62-66)
    at("2:45", "row 62")
    s.chapter("Hit")
    s.say("Hit is a short lean back, and back to rest. It does not loop either.")
    s.key("space", hold=GAP)
    an.new_clip("hit", "N · new clip")
    at("2:48", "row 63")
    s.key("l", "L · loop off", hold=GAP)
    an.press("<", 6, "< ×6 · 6 frames", gap=5)
    at("2:50", "row 64")
    an.go("home", "Home")
    an.go(2, "→ ×2 · frame 2")
    an.turn("spine", "s", 2, "S · lean back")
    an.turn("head", "s", 1)
    s.shot("r64_lean")
    at("2:53", "row 65")
    an.go("end", "End · the last frame")
    an.turn("head", "r", 1, "R · rest")
    an.turn("spine", "r", 1)
    s.key("k", "K · keyframe", hold=GAP)
    at("2:54", "row 66")
    s.key("space", "Space · play", hold=FPS)

    # ================================================================ 2:55 preview (rows 67-72)
    at("2:55", "row 67")
    s.chapter("Preview the clips")
    s.say("Back on F1, left and right change the clip, Space plays it. K shows the skeleton. B blends one clip into the next.")
    s.scene("bm Animator ▸ F1 play", KEYS_F1)
    s.key("f1", "F1 · play", hold=GAP)
    at("2:57", "row 68")
    s.key("right", "← → · previous / next clip", hold=45)
    s.key("left", hold=45)
    at("3:00", "row 69")
    s.key("space", "Space · play, stop", hold=60)
    s.key("space", hold=GAP)
    at("3:02", "row 70")
    s.key("k", "K · hide the skeleton", hold=45)
    s.key("k", "K · show the skeleton", hold=GAP)
    at("3:04", "row 71")
    s.key("b", "B · blend: 0, 25, 50, 75 %", hold=50)
    s.key("b", hold=50)
    s.shot("r71_blend")
    for _ in range(3):
        s.key("b", hold=6)
    at("3:07", "row 72")
    s.key(">", "< > · slower / faster", hold=40)
    s.key(">", hold=40)
    s.key("<", hold=40)
    s.key("<", hold=GAP)

    # ================================================================ 3:10 save (rows 73-76)
    at("3:10", "row 73")
    s.chapter("Save")
    s.say("Control S saves. A cartridge is only read, so the first save makes an editable copy beside it.")
    s.scene("bm Animator ▸ Save", [("Ctrl+S", "save"), ("Esc", "the menu")])
    s.chord("s", "Ctrl+S · save")
    s.shot("r73_saved")
    at("3:14", "row 74")
    s.pad("a", "An editable copy, next to the game", cap="Enter")   # Copy: Enter or Space on the keyboard, A on the pad
    at("3:17", "row 75")
    s.key("esc", "Esc · the menu", hold=GAP)
    s.shot("r75_menu")
    at("3:19", "row 76")
    s.key("esc", "Esc · back", hold=GAP)

    # ================================================================ 3:20 the end (rows 77, 78)
    at("3:20", "row 77")
    s.say("Pico moves. The next video receives this file, with the skeleton and five clips, and we build the trail he runs along.")
    s.scene("bm Animator ▸ F1 play", KEYS_F1)
    s.key("right", "→ · run", hold=GAP)
    s.shot("r77_run")
    at("3:25", "row 78")
    s.mark("Next: the trail", "Next: the trail (map editor)")
    at(TOTAL_VIDEO_S, "the end")
    s.end()
    return s, warn


# ----------------------------------------------------------------------- running
def start_cart(which):
    chain = os.path.join(L.CHAIN, "02-pixel.bm")
    forge = os.path.join(L.ROOT, "video", "3d", "out", "assets", "models.bm")
    if which == "chain" or (which is None and os.path.exists(chain)):
        return L.chain_get("02-pixel.bm")
    if not os.path.exists(forge):
        subprocess.run([sys.executable, os.path.join(L.ROOT, "video", "3d", "assets", "build.py")], check=True)
    return forge


def build_editor(debug):
    """The editor cartridge, compiled here (the same call as the Makefile's build/animator.bm rule)."""
    os.makedirs(OUT, exist_ok=True)
    lua = os.path.join(L.ROOT, "carts", "animator", "main.lua")
    cart = os.path.join(OUT, "animator.bm")
    if debug:
        src = open(lua, encoding="utf-8").read()
        src = src.replace("local rg = {", "rg = {", 1).replace("local an = {", "an = {", 1)
        lua = os.path.join(OUT, "main_dbg.lua")
        with open(lua, "w", encoding="utf-8") as f:
            f.write(src + open(os.path.join(HERE, "dbg.lua"), encoding="utf-8").read())
        cart = os.path.join(OUT, "animator_dbg.bm")
    subprocess.run([sys.executable, os.path.join(L.ROOT, "scripts", "mkbm.py"), "-o", cart, "--lua", lua, "--cover",
                    os.path.join(L.ROOT, "carts", "animator", "icon.png"), "--info",
                    os.path.join(L.ROOT, "carts", "animator", "bm.info")], check=True, cwd=L.ROOT, stdout=subprocess.DEVNULL)
    return cart


def bme_to_bm(src, dst):
    """What bm SDK's Ctrl+B does to a project (src/bm/project.c bm_build): project bit off, 'built from' = the project's name."""
    data = bytearray(open(src, "rb").read())
    flags = (data[18] | data[19] << 8) & ~1
    data[18], data[19] = flags & 255, flags >> 8
    data[104:128] = b"\0" * 24
    name = os.path.basename(src).upper().encode()[:15]
    data[104:104 + len(name)] = name
    with open(dst, "wb") as f:
        f.write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--start", choices=("chain", "forge"))
    ap.add_argument("--build-only", action="store_true", help="only input.txt and overlay.json")
    ap.add_argument("--no-video", action="store_true", help="no mp4 even if ffmpeg is there")
    ap.add_argument("--debug", action="store_true", help="the editor with a log of its state, bmhost not quiet")
    ap.add_argument("--seconds", type=float, help="stop the run early (a test)")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    start = start_cart(args.start)
    info = pico_info(start)
    s, warn = build(info)
    total = s.t
    s.write_input(os.path.join(OUT, "input.txt"))
    L.write_overlay(s, os.path.join(OUT, "overlay.json"), TITLE)
    print("start: %s   PICO is model %d of %s, bounds %s .. %s" % (os.path.relpath(start, L.ROOT), info[0] + 1, info[1],
                                                                  [round(v, 3) for v in info[2]], [round(v, 3) for v in info[3]]))
    print("script: %d frames = %.1f s (target: %d s of video - %d s of hook = %d s)" %
          (total, total / FPS, TOTAL_VIDEO_S, HOOK_S, TOTAL_VIDEO_S - HOOK_S))
    for w in warn:
        print("WARNING:", w)
    if args.build_only:
        return 0

    cart = build_editor(args.debug)
    sd = L.fresh_sd(SD, [(start, "TROPIC.BM")])
    shots = os.path.join(OUT, "shots")
    shutil.rmtree(shots, ignore_errors=True)
    seconds = args.seconds or total / FPS + 1
    video = L.have_ffmpeg() and not args.no_video and not args.debug
    if video:
        ass = os.path.join(OUT, "overlay.ass")
        write_ass(s, ass, TITLE, "bm · the bare-metal fantasy console", total)
        raw = os.path.join(OUT, "raw.fifo")
        if os.path.exists(raw):
            os.remove(raw)
        os.mkfifo(raw)
        enc = subprocess.Popen(encode(raw, ass, os.path.join(OUT, "ep03_main.mp4")))
        os.makedirs(shots, exist_ok=True)
        r = subprocess.run([L.BMHOST, cart, "--tool", "--sd", sd, "--input", os.path.join(OUT, "input.txt"), "--seconds",
                            str(seconds), "--video", raw, "--quiet", "--shots", shots], cwd=L.ROOT, capture_output=True, text=True)
        enc.wait()
    elif args.debug:
        os.makedirs(shots, exist_ok=True)
        r = subprocess.run([L.BMHOST, cart, "--tool", "--sd", sd, "--input", os.path.join(OUT, "input.txt"), "--seconds",
                            str(seconds), "--shots", shots], cwd=L.ROOT, text=True)
    else:
        r = L.run_headless(cart, os.path.join(OUT, "input.txt"), seconds, sd, shots=shots)
    if not args.debug:
        print((r.stdout or "").strip()[-400:], (r.stderr or "").strip()[-400:])
    if r.returncode:
        print("bmhost failed:", r.returncode)
        return 1

    bme = os.path.join(sd, "carts", "TROPIC.BME")
    if not os.path.exists(bme):
        print("no TROPIC.BME on the SD: the editor did not save; files:", os.listdir(os.path.join(sd, "carts")))
        return 1
    L.chain_put(bme, "03-animator.bme")
    bme_to_bm(bme, os.path.join(L.CHAIN, "03-animator.bm"))
    print("chain: video/3d/chain/03-animator.bm (the project built like bm SDK's Ctrl+B)")
    return subprocess.call([sys.executable, os.path.join(HERE, "verify.py"), bme, "--start", start])


if __name__ == "__main__":
    sys.exit(main())
