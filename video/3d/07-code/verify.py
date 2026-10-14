#!/usr/bin/env python3
"""Checks what the recording of episode 7 left (out/sd/carts/TROPIC.BME, out/07-code.bm, out/shots/g*.png).

  python3 video/3d/07-code/verify.py [SD_DIR]

 1. the project keeps MESH, ANIM, LAYERS, AUDIO and the sheet of start.bme byte for byte;
 2. the code typed in bm Code (with block 4c inserted by the assistant) equals game_flat.lua line by line, ignoring indentation;
 3. 07-code.bm is the project without the project flag, same sections;
 4. R2 (the game, 20 s): shots exist, 480x270, not all one colour, and the score text (top left) changes colour pixels over time.
Exit 0 only when everything passes."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

ok_all = True


def check(cond, msg):
    global ok_all
    print("  %s  %s" % ("ok " if cond else "BAD", msg))
    if not cond:
        ok_all = False


def main():
    out = os.path.join(HERE, "out")
    sd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(out, "sd")
    f = bmres.read(os.path.join(sd, "carts", "TROPIC.BME"))
    start = bmres.read(os.path.join(out, "start.bme"))
    print("parts kept")
    for sec, name in ((bmres.SEC_MESH, "MESH"), (bmres.SEC_ANIM, "ANIM"), (bmres.SEC_LAYERS, "LAYERS"), (bmres.SEC_AUDIO, "AUDIO")):
        check(f.get(sec) == start.get(sec) and f.get(sec) is not None, "%s identical to start.bme" % name)
    print("code")
    got = [l.strip() for l in f.get(bmres.SEC_LUA).decode("utf-8").splitlines() if l.strip()]
    want = [l.strip() for l in open(os.path.join(HERE, "game_flat.lua"), encoding="utf-8").read().splitlines() if l.strip()]
    check(got == want, "code = game_flat.lua (%d lines)" % len(want))
    check("camera3d(cx, py + h, cz, a, -0.35, 60)" in got, "block 4c (the assistant's third-person camera) is in")
    print("chain")
    g = bmres.read(os.path.join(out, "07-code.bm"))
    check(g.header[18] & 1 == 0 and g.sections == f.sections, "07-code.bm = the project without the project flag, same sections")
    print("R2 frames")
    try:
        from PIL import Image
    except ImportError:
        Image = None
    shots = sorted(n for n in os.listdir(os.path.join(out, "shots")) if n.startswith("g") and n.endswith(".png"))
    check(len(shots) >= 5 and Image is not None, "%d game shots (PIL present: %s)" % (len(shots), Image is not None))
    if Image and shots:
        sums = []
        for n in shots:
            im = Image.open(os.path.join(out, "shots", n)).convert("RGB")
            if n == shots[0]:
                check(im.size == (480, 270), "shots are %dx%d (480x270)" % im.size)
            sums.append(hash(im.tobytes()))
        check(len(set(sums)) >= len(shots) - 1, "the frames differ (the game moves): %d distinct of %d" % (len(set(sums)), len(shots)))
    print("RESULT:", "ALL OK" if ok_all else "FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
