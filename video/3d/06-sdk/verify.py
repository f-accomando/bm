#!/usr/bin/env python3
"""Checks what the recording of episode 6 left (out/sd/carts/TROPIC.BME, out/06-sdk.bm, out/shots/r2*.png), without looking at a picture.

  python3 video/3d/06-sdk/verify.py [SD_DIR]

 1. the project keeps the parts of start.bm: MESH, ANIM, SHEET, LAYERS, AUDIO byte for byte (the SDK does not touch them);
 2. the code: the five typed blocks are under their markers (sky3d, fog3d, local l / lamp3d, shadow3d, draw3d with flag 8), the markers stay;
 3. the screen is 480x270 (a frame of R2 has that size);
 4. R2 (the cartridge running): the top-left pixel has the fog colour 0xA8D0C0 (+-6), there are dark dithered shadow pixels under Pico
    in the three frames (compared with the same run without the shadow line, not done here: only 'some dark pixels in the lower half');
 5. 06-sdk.bm is the project without the project flag, with the same sections.
Exit 0 only when everything passes."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

ok_all = True
EXPECT = [("-- [sky]", "sky3d(0xFFE8B0, 0x80B8E8, 0x486038)"),
          ("-- [fog]", "fog3d(0xA8D0C0, 12, 40)"),
          ("-- [lamp]", "local l = lamps[1]"),
          ("-- [lamp]", "if l then lamp3d(1, l.x, l.y + 1.2, l.z, 4, 0.8, 0xFFC888) end"),
          ("-- [shadow]", "shadow3d(1)"),
          ("-- [shadow-draw]", "draw3d(pico, px - 0.75, py, pz, 0, 0, 0, 0.5, 8)")]


def check(cond, msg):
    global ok_all
    print("  %s  %s" % ("ok " if cond else "BAD", msg))
    if not cond:
        ok_all = False
    return cond


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = os.path.join(HERE, "out")
    sd = args[0] if args else os.path.join(out, "sd")
    path = os.path.join(sd, "carts", "TROPIC.BME")
    if not os.path.exists(path):
        print("no", path)
        return 1
    f = bmres.read(path)
    start = bmres.read(os.path.join(out, "start.bm"))
    print("project:", path)
    check(f.header[18] & 1 == 1, "it is a project (.bme flag)")

    print("parts kept from start.bm")
    for sec, name in ((bmres.SEC_MESH, "MESH"), (bmres.SEC_ANIM, "ANIM"), (bmres.SEC_LAYERS, "LAYERS"), (bmres.SEC_AUDIO, "AUDIO")):
        check(f.get(sec) == start.get(sec) and f.get(sec) is not None, "%s identical to start.bm" % name)
    sheet = bmres.SEC_SHEET if start.get(bmres.SEC_SHEET) is not None else bmres.SEC_SHEET8
    check(f.get(sheet) == start.get(sheet), "the sheet identical to start.bm")

    print("code")
    code = f.get(bmres.SEC_LUA).decode("utf-8").splitlines()
    for marker, text in EXPECT:
        mi = next((i for i, ln in enumerate(code) if marker in ln), None)
        pos = next((i for i, ln in enumerate(code) if ln.strip() == text), None)
        check(mi is not None and pos is not None and pos > mi and not any("-- [" in ln for ln in code[mi + 1:pos]),
              "%s under %s" % (text[:44], marker))

    print("R2 frames")
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for n in ("r2a", "r2b", "r2c"):
        p = os.path.join(out, "shots", n + ".png")
        if Image is None or not os.path.exists(p):
            check(False, "frame %s (PIL and out/shots)" % n)
            continue
        im = Image.open(p).convert("RGB")
        w, h = im.size
        px = im.getpixel((2, 2))
        want = (0xA8, 0xD0, 0xC0)
        check((w, h) == (480, 270), "%s is %dx%d (screen 480x270)" % (n, w, h)) if n == "r2a" else None
        check(all(abs(a - b) <= 6 for a, b in zip(px, want)), "%s top-left pixel %s is the fog colour" % (n, px))
        dark = sum(1 for y in range(h // 2, h) for x in range(0, w, 2) if sum(im.getpixel((x, y))) < 60)
        check(dark > 30, "%s: %d dark pixels in the lower half (the shadow)" % (n, dark))

    print("chain")
    g = bmres.read(os.path.join(out, "06-sdk.bm")) if os.path.exists(os.path.join(out, "06-sdk.bm")) else None
    check(g is not None and g.header[18] & 1 == 0 and g.sections == f.sections, "06-sdk.bm = the project without the project flag, same sections")
    print("RESULT:", "ALL OK" if ok_all else "FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
