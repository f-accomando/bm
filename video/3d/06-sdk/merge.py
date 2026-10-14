#!/usr/bin/env python3
"""start.bm of video 6 = the map of video 4 + the sound of video 5 + preview.lua.

  python3 video/3d/06-sdk/merge.py [MAP.bm] [SOUND.bm] [-o out/start.bm]

Defaults: video/3d/chain/04-map.bm (else the video 4 run, video/3d/04-map/out/sd/carts/TROPIC.BM) and video/3d/chain/05-sound.bm.
Only the AUDIO section of the sound file is taken and the LUA section is replaced; every other section stays byte for byte.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402


def pick(*paths):
    for p in paths:
        if os.path.exists(p):
            return p
    raise SystemExit("none of: " + ", ".join(paths))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    out = os.path.join(HERE, "out", "start.bm")
    if "-o" in sys.argv:
        out = sys.argv[sys.argv.index("-o") + 1]
        args = [a for a in args if a != out]
    chain = os.path.join(ROOT, "video", "3d", "chain")
    mp = args[0] if args else pick(os.path.join(chain, "04-map.bm"),
                                   os.path.join(ROOT, "video", "3d", "chain-delivered", "04-map.bm"),
                                   os.path.join(ROOT, "video", "3d", "04-map", "out", "sd", "carts", "TROPIC.BM"))
    sp = args[1] if len(args) > 1 else pick(os.path.join(chain, "05-sound.bm"),
                                             os.path.join(ROOT, "video", "3d", "chain-delivered", "05-sound.bm"))
    base = bmres.read(mp)
    snd = bmres.read(sp)
    audio = snd.get(bmres.SEC_AUDIO)
    if audio is None:
        raise SystemExit("no AUDIO section in " + sp)
    base.put(bmres.SEC_AUDIO, audio)
    base.put(bmres.SEC_LUA, open(os.path.join(HERE, "preview.lua"), "rb").read())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bmres.write(out, base)
    base.header = base.header[:18] + bytes([base.header[18] | 1]) + base.header[19:]                         # the same file as a project (.bme): the video opens the project, a game would ask for a copy
    bmres.write(os.path.splitext(out)[0] + ".bme", base)
    print("start.bm:", out, "= map", mp, "+ audio", sp)
    for sec in base.sections:
        print("  section %-6s %d bytes" % (bmres.SEC_NAMES.get(sec[0], sec[0]), len(sec[1])))


if __name__ == "__main__":
    main()
