#!/usr/bin/env python3
"""
The files the console's tools wrote in their host tests, read back by the
Python of the build (scripts/bmres.py, scripts/bmmesh.py):

- bm Studio and bm Animator (tests/studio/tools3d_host.lua):
  BLOCKS.BM, a new project: a block, a floor tile painted red beside it, a
  tile on the far wall, each face turned to the side it is seen from; the
  skeleton and the animation; COPY3D.BM, the village saved with no edits.
- bm Mesh (tests/studio/mesh_host.lua): ASTROWING.BM with the models made
  from the game's meshes and with the tools, the villager edited with its
  skeleton, MESHCOPY.BM the village as saved.
- bm Pixel (tests/studio/pixel_host.lua): the village's sheet 64 pixels
  taller, what was not drawn on with its 24 bits; NEWSPR.BM, a new sheet.

  python3 tests/studio/check_files.py STUDIO3D_SD MESH_SD PIXEL_SD VILLAGE.bm
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import bmmesh  # noqa: E402
import bmres  # noqa: E402

checks = fails = 0


def check(ok, msg):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        print("FAIL " + msg)


def read(path):
    try:
        return bmres.read(path)
    except bmres.ResError as e:
        check(False, f"{path}: {e}")
        raise SystemExit(1)


def models(f):
    body = f.get(bmres.SEC_MESH)
    return bmmesh.decode(body)[0] if body is not None else []


def rigs(f):
    body = f.get(bmres.SEC_ANIM)
    return {r[0]: r for r in bmmesh.decode_anim(body)} if body is not None else {}


def sane(name, f):
    """what the kernel would refuse: a corner out of the model, two models
    with one name, a skeleton that does not fit its model"""
    ms = models(f)
    names = [m["name"] for m in ms]
    check(all(names) and len(set(names)) == len(names), f"{name}: models with a name each: {names}")
    for m in ms:
        nv = len(m["verts"])
        check(all(max(t[:3]) < nv for t in m["faces"]), f"{name}: {m['name']}: corners in the model")
    for rname, (_, bones, vb, clips) in rigs(f).items():
        m = next((m for m in ms if m["name"] == rname), None)
        check(m is not None and len(vb) == len(m["verts"]) and all(b < len(bones) for b in vb),
              f"{name}: the skeleton of {rname} fits its model")
        check(all(b["parent"] < i for i, b in enumerate(bones)), f"{name}: {rname}: parents before their bones")


def normal(v, t):
    a, b, c = (v[i] for i in t[:3])
    u = [b[k] - a[k] for k in range(3)]
    w = [c[k] - a[k] for k in range(3)]
    return (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])


def centre(v, t):
    return [sum(v[i][k] for i in t[:3]) / 3 for k in range(3)]


def studio3d(sd, village):
    f = read(os.path.join(sd, "carts", "blocks.bme"))
    ms = models(f)
    check([m["name"] for m in ms] == ["model"], "BLOCKS.BM: one model, \"model\"")
    m = ms[0]
    v, tris = m["verts"], m["faces"]
    red = [t for t in tris if t[3] & 0xFFFFFF == 0xE84A5A and not t[3] >> 31]
    block = [t for t in tris if all(0 <= v[i][k] <= 1 for i in t[:3] for k in range(3))]
    wall = [t for t in tris if t not in red and t not in block]
    check(len(tris) == 16 and len(block) == 12 and len(red) == 2 and len(wall) == 2,
          f"a block, a red floor tile, a wall tile: {len(block)}, {len(red)}, {len(wall)} of {len(tris)} triangles")
    grass = all(t[4] == (0.0, 16.0, 0.0, 0.0, 16.0, 0.0) or t[4] == (0.0, 16.0, 16.0, 0.0, 16.0, 16.0)
                for t in block + wall)
    check(grass, "the block and the wall tile: the grass tile (0, 0, 16, 16)")
    out = all(sum(n * (c - 0.5) for n, c in zip(normal(v, t), centre(v, t))) > 0 for t in block)
    check(out, "the block's faces turned outwards (seen from the side they are clockwise from)")
    check(all(all(abs(v[i][1]) < 1e-6 and 1 <= v[i][0] <= 2 and 0 <= v[i][2] <= 1 for i in t[:3]) and
              normal(v, t)[1] > 0 for t in red), "the red tile on the floor beside the block, facing up")
    check(all(all(abs(v[i][2] - 1) < 1e-6 and 1 <= v[i][0] <= 2 for i in t[:3]) and normal(v, t)[2] < 0
              for t in wall), "the wall tile at z = 1, facing the camera")
    r = rigs(f).get("model")
    check(r and [b["name"] for b in r[1]] == ["root", "bone2"] and r[1][1]["parent"] == 0, "the skeleton: root and bone2")
    check(r and all(b in (0, 1) for b in r[2]), "every corner follows a bone")
    clip = r and r[3][0]
    check(clip and clip["name"] == "anim1" and len(clip["keys"]) == 2 and not clip["loop"] and
          abs(clip["length"] - 13 / 12) < 1e-5, "the animation: anim1, 2 keyframes, no loop, 13 frames")
    if clip:
        k = clip["keys"][1]
        h = 15 * math.pi / 360
        want = (math.sin(h), 0, 0, math.cos(h))
        check(abs(k["t"] - 0.25) < 1e-5 and all(abs(a - b) < 1e-5 for a, b in zip(k["pose"][0]["q"], want)),
              f"at 0.25 s the root is turned 15 degrees around x: {k}")
    sane("BLOCKS.BM", f)

    a, b = read(os.path.join(sd, "carts", "copy3d.bme")), read(village)
    check([(m["name"], len(m["faces"])) for m in models(a)] == [(m["name"], len(m["faces"])) for m in models(b)],
          "COPY3D.BM has the models of the village")
    check(a.get(bmres.SEC_ANIM) == b.get(bmres.SEC_ANIM), "and the same skeletons")
    check(a.name == b.name and a.get(bmres.SEC_LUA) == b.get(bmres.SEC_LUA), "and the same title and code")


def mesh(sd, village):
    files = {n: read(os.path.join(sd, "carts", n)) for n in ("astrowin.bme", "village.bme", "meshcopy.bme")}
    for n, f in files.items():
        sane(n, f)
    a = files["astrowin.bme"]
    check([m["name"] for m in models(a)] == ["hero", "cube", "plane"],
          f"ASTROWING.BM: hero, cube and plane: {[m['name'] for m in models(a)]}")
    check(not rigs(a), "no skeletons there")
    check(b"[bm Mesh begin]" not in (a.get(bmres.SEC_LUA) or b""), "no code meshes left in its code")
    v, w = files["village.bme"], read(village)
    r, r0 = rigs(v).get("villager"), rigs(w).get("villager")
    check(r and len(r[1]) == 7 and len(r[3]) == 3, "the villager keeps 7 bones and 3 animations")
    check(r and r0 and r[3] == r0[3], "the animations are the same")
    check(r and all(b < 7 for b in r[2]), "every corner follows a bone")
    check([m["name"] for m in models(v)] == [m["name"] for m in models(w)], "the village has its models")
    c = files["meshcopy.bme"]
    check(all(c.get(t) == v.get(t) for t in (bmres.SEC_MESH, bmres.SEC_ANIM, bmres.SEC_LUA)),
          "MESHCOPY.BM is the village as saved")


def pixel(sd, village):
    f, f0 = read(os.path.join(sd, "carts", "village.bme")), read(village)
    w, h, px = bmres.sheet_get(f)
    w0, h0, px0 = bmres.sheet_get(f0)
    check(w == w0 and h == h0 + 64, f"the sheet: {w}x{h}")
    # what was not drawn on stays, 24 bits: the test drew in the sprites
    # of 16x16 at (16,48), (0,0) and (16,0)
    kept = changed = inside = 0
    for y in range(h0):
        for x in range(w0):
            o, n = (y * w0 + x) * 4, (y * w + x) * 4
            a, b = px0[o:o + 4], px[n:n + 4]
            if a[3] < 128 and b[3] < 128:
                continue
            if (x < 32 and y < 16) or (16 <= x < 32 and 48 <= y < 64):
                inside += a != b
            elif a == b:
                kept += 1
            else:
                changed += 1
    check(kept > 10000 and changed == 0 and inside > 0,
          f"the pixels not drawn on keep their 24 bits ({kept} the same, {changed} not; {inside} drawn)")
    check(all(px[i] < 128 for i in range(w0 * h0 * 4 + 3, len(px), 4)), "the 64 new rows are transparent")
    s8, s80 = f.get(bmres.SEC_SHEET8), f0.get(bmres.SEC_SHEET8)
    check(s8 is not None and f.get(bmres.SEC_SHEET) is None, "a SHEET8, no SHEET")
    if s8 is not None and s80 is not None:
        n0 = struct.unpack_from("<H", s80, 4)[0]
        first = 0
        for i in range(n0):
            if s80[8 + i * 4 + 3] < 128:
                break
            first += s8[8 + i * 4:12 + i * 4] == s80[8 + i * 4:12 + i * 4]
        check(first > 0, f"the palette begins as the village's did ({first} colours)")
    for t in (bmres.SEC_LUA, bmres.SEC_COVER, bmres.SEC_MESH, bmres.SEC_ANIM):
        check(f.get(t) is not None and f.get(t) == f0.get(t), f"section {t} byte for byte as it was")
    n = read(os.path.join(sd, "carts", "newspr.bme"))
    w, h, px = bmres.sheet_get(n)
    check(b"bm Pixel: a new sprite sheet" in (n.get(bmres.SEC_LUA) or b"") and (w, h) == (256, 256),
          "NEWSPR.BM: the viewer and a 256x256 sheet")
    opaque = sum(px[i] >= 128 for i in range(3, len(px), 4))
    check(opaque == 1, f"one pixel drawn ({opaque})")


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    studio3d_sd, mesh_sd, pixel_sd, village = sys.argv[1:]
    studio3d(studio3d_sd, village)
    mesh(mesh_sd, village)
    pixel(pixel_sd, village)
    print(f"the tools' files: {checks - fails}/{checks} checks passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
