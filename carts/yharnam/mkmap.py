#!/usr/bin/env python3
"""Yharnam's drawn map: carts/yharnam/map_ground.csv and map_overlay.csv, the cartridge's two map layers
(`layers_yharnam` in the Makefile; 256 x 256 cells of 8 x 8, the whole hunt: 8 x 8 chunks of 16 x 16 tiles of
2 x 2 cells). "ground" is the ground, "overlay" what lies on it (kerbs, grass edges, puddles, leaves). The game
draws them instead of making the ground (MAP.drawn in main.lua); houses, trees, lamps, creatures, gates and
bosses are still made by the code.

    python3 carts/yharnam/mkmap.py [--luahost build/host/luahost]
        the map made from the street plan of main.lua (the first one, or to start again):
        tests/yharnam/mapgen.lua in luahost (make build/host/luahost)
    python3 carts/yharnam/mkmap.py --from /mnt/d/carts/YHARNAM.BME
        the map of a project (or a game) saved on the console: the SDK's map page, then Ctrl+S
    python3 carts/yharnam/mkmap.py --png map.png [--scale 0.5]
        a picture of the map (with sheet.png; needs Pillow and numpy)

--out DIR writes the CSV files elsewhere (default: this folder). The map's other layers, if the SDK added
some, are written as map_<name>.csv too: the build takes them once their names are in `layers_yharnam`.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SIZE = 256                                   # cells a side: MAP.size * CS * 2 (main.lua)
LAYERS = ("ground", "overlay")


def write_csv(path, name, w, cells, what):
    with open(path, "w", newline="\n") as f:
        f.write(f"# Yharnam's map, layer \"{name}\": {what}. {w} x {len(cells) // w} cells of 8 x 8 (sheet.png);\n")
        f.write("# drawn in the SDK (F3 F3), taken back with carts/yharnam/mkmap.py --from\n")
        for y in range(len(cells) // w):
            f.write(",".join(str(n) for n in cells[y * w:(y + 1) * w]) + "\n")


def take(src, out):
    """the layers of a .bme / .bm saved on the console -> map_<name>.csv"""
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import bmres
    f = bmres.read(src)
    if f.get(bmres.SEC_MAP) is None:
        raise SystemExit(f"{src}: no map")
    w, h, _ = bmres.map_get(f)
    layers = [(n, c) for n, c in bmres.layers_get(f) if not n.startswith("_")]
    names = [n for n, _ in layers]
    if (w, h) != (SIZE, SIZE) or names[:2] != list(LAYERS):
        raise SystemExit(f"{src}: a {w}x{h} map with the layers {names}, not Yharnam's ({SIZE}x{SIZE}: "
                         f"{', '.join(LAYERS)} first)")
    for name, cells in layers:
        path = os.path.join(out, f"map_{name}.csv")
        write_csv(path, name, w, cells, "the ground" if name == "ground" else
                  "what lies on the ground (kerbs, grass edges, puddles, leaves)" if name == "overlay" else
                  "a layer added in the SDK, drawn over the others")
        print(path)
    extra = names[2:]
    if extra:
        print("more layers than the build takes: add " + " ".join(extra) + " to layers_yharnam in the Makefile")


def generate(luahost, out):
    """the map the street plan makes (tests/yharnam/mapgen.lua)"""
    if not os.path.exists(luahost):
        raise SystemExit(f"{luahost}: not there (make build/host/luahost)")
    subprocess.run([luahost, os.path.join(ROOT, "tests", "yharnam", "mapgen.lua"),
                    os.path.join(HERE, "main.lua"), out], check=True)


def read_csv(path):
    rows = [[int(v) for v in line.split(",")] for line in open(path) if line.strip() and not line.startswith("#")]
    return rows


def picture(out_png, scale, src_dir):
    import numpy as np
    from PIL import Image
    sheet = np.asarray(Image.open(os.path.join(HERE, "sheet.png")).convert("RGBA"))
    per = sheet.shape[1] // 8
    img = np.zeros((SIZE * 8, SIZE * 8, 4), np.uint8)
    img[..., 3] = 255
    for name in LAYERS:
        cells = np.array(read_csv(os.path.join(src_dir, f"map_{name}.csv")), np.int64)
        used = np.unique(cells[cells > 0])
        for n in used:
            block = sheet[n // per * 8:n // per * 8 + 8, n % per * 8:n % per * 8 + 8]
            ys, xs = np.nonzero(cells == n)
            for y, x in zip(ys, xs):
                dst = img[y * 8:y * 8 + 8, x * 8:x * 8 + 8]
                on = block[..., 3] >= 128
                dst[on] = block[on]
    pic = Image.fromarray(img[..., :3])
    if scale != 1:
        pic = pic.resize((int(pic.width * scale), int(pic.height * scale)), Image.LANCZOS)
    pic.save(out_png)
    print(out_png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="src", help="a .bme or .bm saved on the console: its map's layers")
    ap.add_argument("--luahost", default=os.path.join(ROOT, "build", "host", "luahost"))
    ap.add_argument("--png", help="a picture of the map instead")
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--out", default=HERE, help="the folder of the CSV files (default: carts/yharnam)")
    a = ap.parse_args()
    if a.png:
        picture(a.png, a.scale, a.out)
    elif a.src:
        take(a.src, a.out)
    else:
        generate(a.luahost, a.out)


if __name__ == "__main__":
    main()
