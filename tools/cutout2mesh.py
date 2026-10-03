#!/usr/bin/env python3
"""
cutout2mesh: a model from a picture with no network and no AI, the
console's own way (src/bm/cutout.c, the same thing bm Studio does with
"m" on its models page): the picture's outline (its transparent or plain
background taken away) becomes a cutout with some thickness, the picture
on the front and mirrored on the back, or a lathe, the half-outline
turned around the vertical axis (vases, towers, rockets). The picture
goes on the sprite sheet of a new cartridge (with the viewer); into an
existing one the faces take flat colours.

  tools/cutout2mesh.py hero.png -o hero.bm [--lathe] [--depth 0.2] [--segments 12]
                       [--faces 1200] [--height 2] [--name NAME]
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import bmcutout  # noqa: E402
import meshy2mesh  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="a .png (transparent background) or .jpg (plain background)")
    ap.add_argument("-o", "--out", required=True, help="the .bm to write, or to add the model to")
    ap.add_argument("--lathe", action="store_true", help="turn the outline around the axis instead of cutting it out")
    ap.add_argument("--depth", type=float, default=0.2, help="cutout: the thickness as a fraction of the height")
    ap.add_argument("--segments", type=int, default=12, help="lathe: steps around")
    ap.add_argument("--faces", type=int, default=1200, help="triangles at most")
    ap.add_argument("--height", type=float, default=2.0, help="the model's height in blocks")
    ap.add_argument("--name", help="the model's name (default: the picture's)")
    a = ap.parse_args()
    name = (a.name or os.path.splitext(os.path.basename(a.image))[0])[:15]
    data = open(a.image, "rb").read()
    model, flat, sheet = bmcutout.cutout(data, name, a.lathe, a.height, a.depth, a.segments, a.faces)
    if os.path.exists(a.out):
        model, sheet = flat, None
    what = meshy2mesh.write_cart(a.out, model, sheet, name)
    print(f"cutout2mesh: {a.out}: {what} the model {name}: {len(model['verts'])} vertices, {len(model['faces'])} "
          f"triangles ({'lathe' if a.lathe else 'cutout'}{'' if sheet else ', flat colours'})")


if __name__ == "__main__":
    main()
