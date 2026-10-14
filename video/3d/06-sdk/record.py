#!/usr/bin/env python3
"""Tropic Trail, episode 6: light, fog and shadow in bm SDK (docs/video/06-sdk/).

  python3 video/3d/06-sdk/record.py [--build-only] [--shots]

The video is three recordings of bmhost --tool (docs/video/06-sdk/input.md): R1 the editor (112 s), R3 the dev kit and Ctrl+B (38 s),
R2 the cartridge running by itself (12 s, no keys). This script writes the key scripts and overlays of R1 and R3 and runs them:
  - out/start.bm              the map of video 4 + the sound of video 5 + preview.lua (merge.py)
  - out/input-r1.txt, out/overlay-r1.json, out/input-r3.txt, out/overlay-r3.json
  - out/sd/                   a clean SD card with start.bme (the project) as /carts/TROPIC.BME (R1 and R3 share it)
The cursor moves to each marker of preview.lua by counting its lines, so the six lines go under the right marker.
Keys proven with bmhost screenshots (proto.py): Ctrl+O Enter, F1, F2, End, Enter, arrows, typed text."""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "3d"))

import lib3d                                    # noqa: E402
from bmvideo import Script, FPS                 # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
BIN = os.path.join(OUT, "bin")
R1_SECONDS, R3_SECONDS = 112, 38
OFFSET = 10                                     # the video starts R1 at 0:10 (hook and title before it)

LINES = {                                       # what is typed under each marker of preview.lua
    "-- [sky]": ["sky3d(0xFFE8B0, 0x80B8E8, 0x486038)"],
    "-- [fog]": ["fog3d(0xA8D0C0, 12, 40)"],
    "-- [lamp]": ["local l = lamps[1]", "if l then lamp3d(1, l.x, l.y + 1.2, l.z, 4, 0.8, 0xFFC888) end"],
    "-- [shadow]": ["shadow3d(1)"],
    "-- [shadow-draw]": ["draw3d(pico, px - 0.75, py, pz, 0, 0, 0, 0.5, 8)"],
}
ORDER = ["-- [sky]", "-- [fog]", "-- [lamp]", "-- [shadow]", "-- [shadow-draw]"]
SECONDS = {"-- [sky]": 44, "-- [fog]": 58, "-- [lamp]": 72, "-- [shadow]": 88, "-- [shadow-draw]": 98}


class Ep(Script):
    late = []

    def until(self, sec, what=""):
        target = round(sec * FPS)
        if self.t > target + FPS:
            Ep.late.append("%s: wanted %ds, is %.1f s" % (what or "?", sec, self.t / FPS))
        elif self.t < target:
            self.t = target
        return self


def narration():
    """[(video seconds, English narration)] from the table of docs/video/06-sdk/script.md"""
    rows = []
    for line in open(os.path.join(ROOT, "docs", "video", "06-sdk", "script.md"), encoding="utf-8"):
        m = re.match(r"\|\s*(\d+):(\d\d)\s*\|(.*)\|(.*)\|\s*$", line)
        if m and "Narrazione" not in line and not m.group(4).strip().startswith("("):
            rows.append((int(m.group(1)) * 60 + int(m.group(2)), m.group(4).strip()))
    return rows


def k(s, key, what="", cap=None, w=20):
    s.key(key, what=what, cap=cap, hold=8)
    s.wait(w)


def move(s, key, n):
    for _ in range(n):
        s.key(key, hold=8)
        s.wait(4)
    s.wait(12)


def build_r1():
    s = Ep()
    rows = narration()
    src = open(os.path.join(HERE, "preview.lua"), encoding="utf-8").read().splitlines()

    def narrate(t, at):
        """the narration of the table at video second t, said at second `at` of R1"""
        for u, text in rows:
            if u == t:
                nxt = [v for v, _ in rows if v > u]
                s.until(at, "narration %d" % t)
                s.say(text, frames=((nxt[0] if nxt else u + 10) - u) * FPS)

    s.chapter("The editor")
    s.scene("bm SDK > open TROPIC.BME", [("Ctrl+O", "open")])
    narrate(10, 0)
    s.sec(0.5)
    s.chord("o", what="Open")
    s.wait(25)
    k(s, "enter", "Open /carts/TROPIC.BME", "Enter", 40)
    s.until(4, "F1")
    k(s, "f1", "The project page", "F1", 30)
    s.until(7, "F4")
    k(s, "f4", "Models and animations", "F4", 25)
    move(s, "down", 2)
    k(s, "right", "Next animation", "Right", 40)
    s.until(11, "map")
    k(s, "f3", "", "F3", 15)
    k(s, "f3", "The map layers", "F3", 25)
    for _ in range(3):
        k(s, "l", "Next layer", "l", 25)
    s.until(14, "Ctrl+N")
    narrate(22, 14)
    s.chord("n", what="New project from a template")
    s.wait(40)
    move(s, "down", 7)
    k(s, "esc", "Close the list", "Esc", 30)
    s.until(20, "F2")
    k(s, "f2", "The code", "F2", 60)
    narrate(34, 25)
    s.until(25, "F1")
    k(s, "f1", "The project page", "F1", 25)
    s.until(27, "r")
    k(s, "r", "Screen size 480x270", "r", 60)
    s.until(36, "F2 again")
    k(s, "f2", "The code", "F2", 40)
    cursor = 1
    said = {"-- [sky]": (46, 40), "-- [fog]": (64, 54), "-- [lamp]": (80, 68), "-- [shadow]": (96, 84)}
    for marker in ORDER:
        s.until(SECONDS[marker] - 4, marker)
        if marker in said:
            narrate(*said[marker])
        idx = next(i + 1 for i, ln in enumerate(src) if marker in ln)
        d = idx - cursor
        if d:
            move(s, "down" if d > 0 else "up", abs(d))
        k(s, "end", "", "End", 10)
        k(s, "enter", "A new line", "Enter", 10)
        for j, text in enumerate(LINES[marker]):
            s.type_text(text, what="Type " + text.split("(")[0], per_key=4)
            if j < len(LINES[marker]) - 1:
                k(s, "enter", "", "Enter", 10)
        s.wait(40)
        for j, text in enumerate(LINES[marker]):
            src.insert(idx + j, "  " + text)
        cursor = idx + len(LINES[marker])
    narrate(114, 104)
    s.until(104, "save as")
    s.chord("s", what="Save the project")
    s.wait(90)
    s.until(110, "F5")
    s.caps.append((s.t, "F5", "Save and run (overlay only: F5 would start the cartridge and end the recording)"))
    s.wait(60)
    s.until(R1_SECONDS, "end of R1")
    s.end()
    return s


def build_r3():
    s = Ep()
    s.chapter("The dev kit")
    s.scene("bm SDK > dev kit", [("F1", "project"), ("F1", "dev kit"), ("Ctrl+B", "build")])
    k(s, "f1", "The project page: LAST TRY", "F1", 60)
    s.until(4, "F1")
    k(s, "f1", "The dev kit", "F1", 60)
    s.until(16, "Ctrl+B")
    s.chord("b", what="Build the game")
    s.wait(120)
    s.until(R3_SECONDS, "end of R3")
    s.end()
    return s


def write(s, name):
    inp = os.path.join(OUT, "input-%s.txt" % name)
    s.write_input(inp)
    with open(inp, "rb") as f:
        data = f.read().replace(b"\r\n", b"\n")
    with open(inp, "wb") as f:
        f.write(data)
    lib3d.write_overlay(s, os.path.join(OUT, "overlay-%s.json" % name), "Tropic Trail  ·  Episode 6: light, fog and shadow")
    print("%s: %d frames, %.1f s" % (inp, s.t, s.t / FPS))


def main():
    args = sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    subprocess.check_call([sys.executable, os.path.join(HERE, "merge.py")])
    r1, r3 = build_r1(), build_r3()
    write(r1, "r1")
    write(r3, "r3")
    for w in Ep.late:
        print("LATE:", w)
    if "--build-only" in args:
        return 0
    os.makedirs(BIN, exist_ok=True)
    subprocess.check_call([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", os.path.join(BIN, "editor.bm"), "--lua", os.path.join(ROOT, "carts", "editor", "main.lua"),
                           "--cover", os.path.join(ROOT, "carts", "editor", "icon.png"), "--info", os.path.join(ROOT, "carts", "editor", "bm.info")], cwd=ROOT)
    lib3d.fresh_sd(SD, [(os.path.join(OUT, "start.bme"), "TROPIC.BME")])
    shots = os.path.join(OUT, "shots") if "--shots" in args else None
    for name, secs in (("r1", R1_SECONDS), ("r3", R3_SECONDS)):
        r = lib3d.run_headless(os.path.join(BIN, "editor.bm"), os.path.join(OUT, "input-%s.txt" % name), secs + 3, SD, shots=shots)
        print(name, (r.stderr or r.stdout)[-160:].strip())
    # Ctrl+B is not built by bmhost --tool: the game of the project is the same file without the project flag (R2 runs it)
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import bmres
    f = bmres.read(os.path.join(SD, "carts", "TROPIC.BME"))
    f.header = f.header[:18] + bytes([f.header[18] & ~1 & 255]) + f.header[19:]
    bmres.write(os.path.join(SD, "carts", "TROPIC.BM"), f)
    bmres.write(os.path.join(OUT, "06-sdk.bm"), f)
    with open(os.path.join(OUT, "r2-input.txt"), "w", newline=chr(10)) as g:
        g.write(chr(10).join(["60 shot r2a", "300 shot r2b", "600 shot r2c"]) + chr(10))
    r = lib3d.run_headless(os.path.join(SD, "carts", "TROPIC.BM"), os.path.join(OUT, "r2-input.txt"), 12, SD, shots=os.path.join(OUT, "shots"), tool=False)
    print("r2", (r.stderr or r.stdout)[-160:].strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
