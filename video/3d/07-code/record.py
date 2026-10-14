#!/usr/bin/env python3
"""Tropic Trail, episode 7: the game logic in bm Code (docs/video/07-code/).

  python3 video/3d/07-code/record.py [--build-only] [--shots]      (run it in WSL/Linux: bmhost is a Linux binary)

Three recordings on the same SD (docs/video/07-code/input.md):
  R1 bm Code (bmhost-ai --tool): open TROPIC.BME, Ctrl+K cuts the preview code, the blocks of the script are typed without
     indentation (bm Code indents), F6 asks the assistant `third person camera` and Enter inserts block 4c, Ctrl+S.
  R3 bm SDK (editor.bm): Ctrl+O the project, Ctrl+B builds the game.
  R2 the game (bmhost-bin, no --tool): the built .bm, a scripted pad (the full run with the bot is video 8).
Needs out/start.bme = the project of video 6 (video/3d/06-sdk/out/sd/carts/TROPIC.BME)."""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "3d"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import lib3d                                    # noqa: E402
import bmres                                    # noqa: E402
from bmvideo import Script, FPS                 # noqa: E402

OUT = os.path.join(HERE, "out")
SD = os.path.join(OUT, "sd")
BIN = os.path.join(OUT, "bin")
SCRIPT = os.path.join(ROOT, "docs", "video", "07-code", "script.md")
OFFSET = 11                                     # R1 starts at 0:11 of the video (hook and title card before it)
R1_SECONDS, R3_SECONDS, R2_SECONDS = 205, 14, 20
OLD_LINES = 44                                  # the code of video 6 (lines to cut; more is harmless)
QUESTION = "third person camera"
# the blocks (script.md order) and the video second at which the narration of their group is said
GROUPS = [("1a 1b", 36, 2), ("1c", 50, 1), ("2a 2b 2c", 68, 3), ("2d", 90, 1), ("3a 3b 3c", 114, 1), ("3d 3e 3f", 134, 3),
          ("4a 4b", 158, 2), ("F6", 176, 0), ("4d 4e", 198, 2)]


class Ep(Script):
    late = []

    def until(self, sec, what=""):
        target = round(sec * FPS)
        if self.t > target + FPS:
            Ep.late.append("%s: wanted %ds, is %.1f s" % (what or "?", sec, self.t / FPS))
        elif self.t < target:
            self.t = target
        return self


def blocks():
    """the 16 ```lua blocks of script.md, in order, as lists of lines"""
    out, cur = [], None
    for line in open(SCRIPT, encoding="utf-8").read().splitlines():
        if line.startswith("```lua"):
            cur = []
        elif line.startswith("```") and cur is not None:
            out.append(cur)
            cur = None
        elif cur is not None:
            cur.append(line)
    return out


def narration():
    rows = []
    for line in open(SCRIPT, encoding="utf-8"):
        m = re.match(r"\|\s*(\d+):(\d\d)\s*\|(.*)\|(.*)\|\s*$", line)
        if m and "Narrazione" not in line and not m.group(4).strip().startswith("—"):
            rows.append((int(m.group(1)) * 60 + int(m.group(2)), m.group(4).strip()))
    return rows


def typed(s, lines):
    for line in lines:
        if line.strip():
            s.type_text(line.strip(), "typing code", per_key=1)
        s.key("enter", hold=2)
        s.wait(1)


def build_r1():
    s = Ep()
    rows = narration()
    B = blocks()
    assert len(B) == 16, len(B)

    def narrate(t):
        for u, text in rows:
            if u == t:
                nxt = [v for v, _ in rows if v > u]
                s.until(t - OFFSET, "narration %d" % t)
                s.say(text, frames=((nxt[0] if nxt else u + 10) - u) * FPS)

    s.chapter("bm Code")
    s.scene("Dev > bm Code", [("Ctrl+O", "open"), ("Ctrl+K", "cut a line"), ("F6", "assistant"), ("Ctrl+S", "save")])
    narrate(11)
    s.sec(1)
    s.raw("\\x0f", "Ctrl+O", "open a cartridge", 2)
    s.sec(3.5)
    s.key("enter", "Open TROPIC.BME")
    s.sec(2.5)
    s.until(26 - OFFSET, "Ctrl+K")
    narrate(26)
    for _ in range(OLD_LINES):
        s.raw("\\x0b", "Ctrl+K", "cut a line", 1)
        s.wait(2)
    s.sec(1)
    idx = 0
    for gname, sec, n in GROUPS:
        if gname == "F6":
            narrate(sec)
            s.key("f6", "the assistant")
            s.sec(2)
            s.type_text(QUESTION, "the question", per_key=4)
            s.sec(3)
            s.key("enter", "insert the code")
            s.sec(2)
            s.key("enter", hold=2)
            idx += 1
            continue
        narrate(sec)
        for _ in range(n):
            typed(s, B[idx])
            idx += 1
        s.sec(0.5)
    narrate(213)
    s.until(205 - 3, "Ctrl+S")
    s.chord("s", what="Save the code")
    s.sec(1.5)
    s.until(R1_SECONDS, "end of R1")
    s.end()
    return s


def build_r3():
    s = Ep()
    s.chapter("bm SDK")
    s.scene("bm SDK > build", [("Ctrl+O", "open"), ("Ctrl+B", "build")])
    s.sec(1)
    s.raw("\\x0f", "Ctrl+O", "open a project", 2)
    s.sec(3.5)
    s.key("enter", "Open TROPIC.BME")
    s.sec(2.5)
    s.chord("b", what="Build the game")
    s.sec(4)
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
    lib3d.write_overlay(s, os.path.join(OUT, "overlay-%s.json" % name), "Tropic Trail  ·  Episode 7: code the game")
    print("%s: %d frames, %.1f s" % (inp, s.t, s.t / FPS))


def game_pad():
    L = ["60 pad 1 a"]
    L += ["%d pad 1 up" % fr for fr in range(120, R2_SECONDS * 60, 3)]
    L += ["%d pad 1 a" % fr for fr in (400, 700, 1000)]
    L += ["%d pad 1 b" % fr for fr in (550, 850)]
    L += ["%d shot g%04d" % (fr, fr) for fr in range(90, R2_SECONDS * 60, 200)]
    L.sort(key=lambda x: int(x.split()[0]))
    with open(os.path.join(OUT, "game.txt"), "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")


def main():
    args = sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, "video", "3d", "06-sdk", "out", "sd", "carts", "TROPIC.BME")
    if not os.path.exists(os.path.join(OUT, "start.bme")):
        shutil.copy(src, os.path.join(OUT, "start.bme"))
    r1, r3 = build_r1(), build_r3()
    write(r1, "r1")
    write(r3, "r3")
    game_pad()
    for w in Ep.late:
        print("LATE:", w)
    if "--build-only" in args:
        return 0
    os.makedirs(BIN, exist_ok=True)
    for name, cart in (("code.bm", "code"), ("editor.bm", "editor")):
        subprocess.check_call([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", os.path.join(BIN, name), "--lua", os.path.join(ROOT, "carts", cart, "main.lua"),
                               "--cover", os.path.join(ROOT, "carts", cart, "icon.png"), "--info", os.path.join(ROOT, "carts", cart, "bm.info")], cwd=ROOT)
    lib3d.fresh_sd(SD, [(os.path.join(OUT, "start.bme"), "TROPIC.BME")])
    shots = os.path.join(OUT, "shots")
    os.makedirs(shots, exist_ok=True)
    r = lib3d.run_headless(os.path.join(BIN, "code.bm"), os.path.join(OUT, "input-r1.txt"), R1_SECONDS + 3, SD, shots=shots if "--shots" in args else None, ai=True)
    print("r1", (r.stderr or r.stdout)[-200:].strip())
    r = lib3d.run_headless(os.path.join(BIN, "editor.bm"), os.path.join(OUT, "input-r3.txt"), R3_SECONDS + 3, SD, shots=shots if "--shots" in args else None)
    print("r3", (r.stderr or r.stdout)[-200:].strip())
    f = bmres.read(os.path.join(SD, "carts", "TROPIC.BME"))
    f.header = f.header[:18] + bytes([f.header[18] & ~1 & 255]) + f.header[19:]
    bmres.write(os.path.join(SD, "carts", "TROPIC.BM"), f)
    bmres.write(os.path.join(OUT, "07-code.bm"), f)
    r = lib3d.run_headless(os.path.join(SD, "carts", "TROPIC.BM"), os.path.join(OUT, "game.txt"), R2_SECONDS, SD, shots=os.path.join(OUT, "shots"), tool=False)
    print("r2", (r.stderr or r.stdout)[-200:].strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
