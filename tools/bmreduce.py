#!/usr/bin/env python3
"""
bmreduce: fewer triangles for the 3D models of a .bm, on the PC, with the
console's own reducer (src/bm/decimate.c: quadric edge collapse; bm Studio
does the same with "reduce" on its models page). Borders, colour lines and
texture seams stay where they are; the skeletons follow.

  tools/bmreduce.py CART.bm --faces 1200 [-o OUT.bm] [--model NAME] [--max-err E]
  tools/bmreduce.py CART.bm --ratio 0.5

--faces: the triangles each model should have at most; --ratio: a fraction
of what it has. Without -o the file is rewritten in place.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import bmdecimate  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cart")
    ap.add_argument("-o", "--out", help="the file to write (default: the cartridge itself)")
    ap.add_argument("--faces", type=int, help="triangles per model, at most")
    ap.add_argument("--ratio", type=float, help="a fraction of the triangles each model has (0.5: half)")
    ap.add_argument("--model", help="only this model")
    ap.add_argument("--max-err", type=float, default=0.0,
                    help="stop before a collapse costing more than this (quadric error; 0: no limit)")
    a = ap.parse_args()
    if (a.faces is None) == (a.ratio is None):
        ap.error("--faces N or --ratio R, one of the two")

    def target_of(name, nf):
        if a.model and name != a.model:
            return None
        return a.faces if a.faces is not None else max(1, round(nf * a.ratio))

    data = open(a.cart, "rb").read()
    out, report = bmdecimate.reduce_cart(data, target_of, a.max_err)
    if a.model and not report:
        raise SystemExit(f"{a.cart}: no model {a.model}")
    for name, before, after in report:
        print(f"{name}: {before} -> {after} triangles")
    with open(a.out or a.cart, "wb") as f:
        f.write(out)
    print(a.out or a.cart)


if __name__ == "__main__":
    main()
