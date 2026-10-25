#!/usr/bin/env python3
"""Lists the sprites (enemies and items) of the first level, from your own ROM in the emulator.

    python3 scripts/smw_spawns.py ROM.sfc OUTDIR

Runs the level to the right (Mario cannot be hurt). Each time a sprite slot starts a new sprite
it writes its number, where it first appeared and its speed. spawns.json:
    [{frame, slot, id, x, y, vx, vy, face}]   x, y in level pixels, speeds as the game keeps them.
WRAM per slot: $9E number, $14C8 status, $E4/$14E0 x, $D8/$14D4 y, $B6 x speed, $AA y speed,
$157C facing."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smw_capture import *


def s8(v):
    return v - 256 if v > 127 else v


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    os.makedirs(sys.argv[2], exist_ok=True)
    seen = {}               # slot -> (id, spawn counter)
    out = []
    screens = g.wram()[0x5E]
    for f in range(4000):
        g.poke(0x1497, 0x7F)
        g.step([RIGHT, Y] + ([B] if f % 40 < 24 else []), 3)
        w = g.wram()
        for i in range(12):
            st, num = w[0x14C8 + i], w[0x9E + i]
            key = (st != 0, num)
            if st != 0 and seen.get(i) != key:
                out.append({"frame": f, "slot": i, "id": num, "status": st,
                            "x": w[0xE4 + i] | w[0x14E0 + i] << 8, "y": w[0xD8 + i] | w[0x14D4 + i] << 8,
                            "vx": s8(w[0xB6 + i]), "vy": s8(w[0xAA + i]), "face": w[0x157C + i]})
            seen[i] = key if st != 0 else None
        camx = w[0x1A] | w[0x1B] << 8
        if camx >= (screens - 1) * 256 - 8 or w[0x100] != 0x14:
            break
    json.dump(out, open(sys.argv[2] + "/spawns.json", "w"), indent=0)
    ids = sorted({s["id"] for s in out})
    print(len(out), "spawns; sprite numbers", ", ".join("%02X" % i for i in ids))
    for s in out:
        print("%5d slot %2d id %02X status %X at %4d,%3d speed %d,%d face %d" % (s["frame"], s["slot"], s["id"], s["status"], s["x"], s["y"], s["vx"], s["vy"], s["face"]))


if __name__ == "__main__":
    main()
