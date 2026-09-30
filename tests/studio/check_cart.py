#!/usr/bin/env python3
"""The .bm and .glb that bm Studio wrote (tests/studio/test_core.js), read
by the Python side of the build (scripts/bmmesh.py, used by mkbm.py): the
same models, and in the .glb the faces show the same side as in the .bm.

  tests/studio/check_cart.py build/studio-test.bm
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import bmmesh  # noqa: E402


def normal(m, f):
    a, b, c = (m["verts"][i] for i in f[:3])
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    s = sum(x * x for x in n) ** 0.5 or 1
    return [x / s for x in n]


def centre(m, f):
    return [sum(m["verts"][i][k] for i in f[:3]) / 3 for k in range(3)]


def main():
    path = sys.argv[1]
    fails = []
    data = open(path, "rb").read()
    secs = dict(bmmesh.cart_sections(data))
    models, inset = bmmesh.decode(secs[bmmesh.SEC_MESH])
    names = [m["name"] for m in models]
    if names != ["house", "sign"]:
        fails.append(f"names {names}")
    if abs(inset - 0.5) > 1e-6:
        fails.append(f"inset {inset}")
    house = models[0]
    tex = [f for f in house["faces"] if f[3] & bmmesh.TEXTURED]
    plain = [f for f in house["faces"] if not f[3] & bmmesh.TEXTURED]
    if len(tex) != 20 or len(plain) != 1 or plain[0][3] != 0x33CC66:
        fails.append(f"house: {len(tex)} textured, {len(plain)} plain")
    glb, _, ginset = bmmesh.glb_models(open(path[:-3] + ".glb", "rb").read())
    if [m["name"] for m in glb] != names or abs(ginset - 0.5) > 1e-6:
        fails.append("glb names or inset")
    for a, b in zip(models, glb):
        if len(a["faces"]) != len(b["faces"]):
            fails.append(f"{a['name']}: {len(a['faces'])} triangles in the .bm, {len(b['faces'])} in the .glb")
            continue
        for fa in a["faces"]:
            ca, na = centre(a, fa), normal(a, fa)
            match = [fb for fb in b["faces"] if max(abs(x - y) for x, y in zip(centre(b, fb), ca)) < 1e-4]
            if not any(sum(x * y for x, y in zip(normal(b, fb), na)) > 0.999 for fb in match):
                fails.append(f"{a['name']}: a face at {ca} shows another side in the .glb")
                break
    for f in fails:
        print("FAIL", f)
    print(f"studio cart: {'ok' if not fails else 'FAILED'} ({len(models)} models, {sum(len(m['faces']) for m in models)} triangles)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
