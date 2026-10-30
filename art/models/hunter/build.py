"""build.py: Hunter models + clips -> BMRES `.bmm` files (bm Mesh and bm Animator can open them).

  python3 build.py OUT_DIR        writes OUT_DIR/hunter.bmm (high poly) and OUT_DIR/hunter_lo.bmm (about 1000 triangles)
  python3 build.py OUT_DIR hi|lo  only one of them

The clips are the same for both: hunter_clips.py. The pistol has its own clip `fire` (hammer and recoil).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def build_one(lod, out):
    os.environ["HUNTER_LOD"] = lod
    sys.path.insert(0, HERE)
    import hunter_build as B
    import hunter_clips as C
    from bm_core import anim as A, animgen, export, mesh as M
    models, rigs = B.build_all()
    descs = C.clips()
    rigs[0] = animgen.add_clips(rigs[0], descs)
    f = lambda a, b=0, c=0: [round(x, 6) for x in animgen.euler_quat(a, b, c)]          # noqa: E731
    fire = {"name": "fire", "loop": False, "frames": 8, "curve": "linear", "keys": [
        {"frame": 0, "pose": {"pistol": {"q": f(0)}, "hammer": {"q": f(0)}}},
        {"frame": 1, "pose": {"pistol": {"q": f(0)}, "hammer": {"q": f(-38)}}},
        {"frame": 2, "pose": {"pistol": {"q": f(0)}, "hammer": {"q": f(-38)}}},
        {"frame": 3, "pose": {"pistol": {"q": f(0)}, "hammer": {"q": f(14)}}},
        {"frame": 4, "pose": {"pistol": {"q": f(16), "move": [0, 0.01, 0.045]}, "hammer": {"q": f(14)}}},
        {"frame": 5, "pose": {"pistol": {"q": f(8), "move": [0, 0.006, 0.025]}, "hammer": {"q": f(8)}}},
        {"frame": 7, "pose": {"pistol": {"q": f(0)}, "hammer": {"q": f(0)}}},
    ]}
    rigs[3] = animgen.add_clips(rigs[3], [fire])
    title = "Hunter (high poly)" if lod == "hi" else "Hunter (low poly)"
    export.export_bmm(out, M.Mesh(models), anim=A.Anim(rigs), title=title)
    for m in models:
        print(f"  {m.name}: {len(m.verts)} verts, {len(m.faces)} tris")
    print("wrote", out)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out_dir = argv[1]
    if len(argv) > 2:
        build_one(argv[2], os.path.join(out_dir, "hunter.bmm" if argv[2] == "hi" else "hunter_lo.bmm"))
        return 0
    for lod in ("hi", "lo"):
        subprocess.check_call([sys.executable, os.path.abspath(__file__), out_dir, lod])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
