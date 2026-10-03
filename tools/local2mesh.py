#!/usr/bin/env python3
"""
local2mesh: a picture becomes a model for the console with an open
image-to-3D model running on this PC (no key, no credits, no cloud): the
backend makes a .glb, then the same conversion as meshy2mesh (the texture
on the sheet, the reducer down to the Pi's triangles) writes the .bm.

  tools/local2mesh.py --install triposr          once: the backend into ~/.bm/local3d
  tools/local2mesh.py hero.png -o hero.bm         a model (TripoSR by default)
  tools/local2mesh.py hero.png -o hero.bm --backend hunyuan3d
  tools/local2mesh.py hero.png -o hero.bm --backend command \\
      --command "my-tool {image} --out {out}"      any tool that writes a .glb
  tools/local2mesh.py --check                      what is installed

Backends:
  triposr    TripoSR (Stability AI / VAST, MIT): fast, runs on a GPU with
             6 GB or on the CPU (slow); textured with --bake-texture.
  hunyuan3d  Hunyuan3D 2 (Tencent): better shapes and textures, needs an
             NVIDIA GPU with 12 GB or more.
  command    your own: {image}, {out} (a .glb to write), {outdir}, {python}
             (the venv's python, if --install made one) in the template.
The weights are fetched from huggingface.co the first time a backend
runs (a few GB). BM_LOCAL3D in the environment moves the home folder.
"""
import argparse
import glob
import os
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))

HOME = os.environ.get("BM_LOCAL3D", os.path.join(os.path.expanduser("~"), ".bm", "local3d"))

HUNYUAN_DRIVER = '''
# local2mesh's driver for Hunyuan3D 2: a picture -> a textured .glb
import sys
from PIL import Image
image, out = sys.argv[1], sys.argv[2]
from hy3dgen.shapegen import Hunyuan3DDiTFlowMatchingPipeline
from hy3dgen.rembg import BackgroundRemover
im = Image.open(image).convert("RGBA")
if im.mode == "RGB" or im.getextrema()[3][0] == 255:
    im = BackgroundRemover()(im)
pipe = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained("tencent/Hunyuan3D-2")
mesh = pipe(image=im)[0]
try:
    from hy3dgen.texgen import Hunyuan3DPaintPipeline
    mesh = Hunyuan3DPaintPipeline.from_pretrained("tencent/Hunyuan3D-2")(mesh, image=im)
except Exception as e:
    print("local2mesh: no texture (%s): the shape alone" % e, file=sys.stderr)
mesh.export(out)
'''

BACKENDS = {
    "triposr": {
        "about": "TripoSR (MIT): fast, GPU 6 GB or CPU",
        "repo": "https://github.com/VAST-AI-Research/TripoSR",
        "install": ["{pip} install -r {dir}/requirements.txt"],
        "run": "{python} {dir}/run.py {image} --output-dir {outdir} --model-save-format glb --bake-texture",
        "result": "{outdir}/0/mesh.glb",
    },
    "hunyuan3d": {
        "about": "Hunyuan3D 2 (Tencent): better, NVIDIA GPU 12 GB+",
        "repo": "https://github.com/Tencent-Hunyuan/Hunyuan3D-2",
        "install": ["{pip} install -r {dir}/requirements.txt", "{pip} install -e {dir}",
                    "{pip} install -e {dir}/hy3dgen/texgen/custom_rasterizer",
                    "{pip} install -e {dir}/hy3dgen/texgen/differentiable_renderer"],
        "driver": HUNYUAN_DRIVER,
        "run": "{python} {home}/hunyuan3d_driver.py {image} {out}",
        "result": "{out}",
    },
}


def venv_python(home):
    for p in (os.path.join(home, "venv", "bin", "python"), os.path.join(home, "venv", "Scripts", "python.exe")):
        if os.path.exists(p):
            return p
    return None


def run(cmd, cwd=None):
    print("$ " + cmd, flush=True)
    r = subprocess.run(cmd, shell=True, cwd=cwd)
    if r.returncode != 0:
        raise SystemExit(f"local2mesh: the command failed ({r.returncode}): {cmd}")


def install(name, home):
    b = BACKENDS.get(name)
    if not b or "repo" not in b:
        raise SystemExit(f"local2mesh: nothing to install for {name!r} (triposr or hunyuan3d)")
    os.makedirs(home, exist_ok=True)
    d = os.path.join(home, name)
    if not os.path.exists(d):
        run(f"git clone --depth 1 {b['repo']} {shlex.quote(d)}")
    else:
        print(f"local2mesh: {d} is there already")
    py = venv_python(home)
    if not py:
        run(f"{shlex.quote(sys.executable)} -m venv {shlex.quote(os.path.join(home, 'venv'))}")
        py = venv_python(home)
    pip = f"{shlex.quote(py)} -m pip"
    run(f"{pip} install --upgrade pip")
    # torch first: the CUDA build when there is an NVIDIA card, else the CPU one
    cuda = subprocess.run("nvidia-smi -L", shell=True, capture_output=True).returncode == 0
    run(f"{pip} install torch torchvision" + ("" if cuda else " --index-url https://download.pytorch.org/whl/cpu"))
    for step in b["install"]:
        run(step.format(pip=pip, dir=shlex.quote(d), home=shlex.quote(home)))
    if "driver" in b:
        with open(os.path.join(home, name + "_driver.py"), "w") as f:
            f.write(b["driver"])
    print(f"local2mesh: {name} installed in {home} ({'with' if cuda else 'without'} an NVIDIA card)."
          f" The weights come from huggingface.co the first time.")


def check(home):
    py = venv_python(home)
    print(f"home: {home}\nvenv python: {py or 'none (--install makes it)'}")
    for name, b in BACKENDS.items():
        if "repo" in b:
            d = os.path.join(home, name)
            print(f"  {name}: {'installed' if os.path.exists(d) else 'not installed'} ({b['about']})")
    print("  command: your own tool with --command")


def make_glb(image, backend, command, home, outdir):
    """runs the backend: the .glb's path"""
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, "model.glb")
    py = venv_python(home) or sys.executable
    fields = {"image": shlex.quote(os.path.abspath(image)), "out": shlex.quote(out), "outdir": shlex.quote(outdir),
              "python": shlex.quote(py), "home": shlex.quote(home), "dir": shlex.quote(os.path.join(home, backend))}
    if backend == "command":
        if not command:
            raise SystemExit("local2mesh: --backend command wants --command \"tool {image} --out {out}\"")
        cmd, result = command.format(**fields), out
    else:
        b = BACKENDS[backend]
        if not os.path.exists(os.path.join(home, backend)):
            raise SystemExit(f"local2mesh: {backend} is not installed: tools/local2mesh.py --install {backend}")
        cmd = b["run"].format(**fields)
        result = b["result"].format(outdir=outdir, out=out)
    t0 = time.time()
    run(cmd)
    print(f"local2mesh: the backend took {time.time() - t0:.0f} s", flush=True)
    if not os.path.exists(result):
        found = sorted(glob.glob(os.path.join(outdir, "**", "*.glb"), recursive=True), key=os.path.getmtime)
        if not found:
            raise SystemExit(f"local2mesh: the backend wrote no .glb in {outdir}")
        result = found[-1]
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", help="the picture (png or jpg): one subject on a clean background")
    ap.add_argument("-o", "--out", help="the .bm to write (new, with the viewer) or add the model to")
    ap.add_argument("--backend", default="triposr", choices=sorted(BACKENDS) + ["command"])
    ap.add_argument("--command", help="the tool for --backend command, with {image} and {out}")
    ap.add_argument("--name", help="the model's name (default: the picture's)")
    ap.add_argument("--faces", type=int, default=1200, help="triangles at most (the reducer; default 1200)")
    ap.add_argument("--height", type=float, default=2.0, help="the model's height in blocks (default 2)")
    ap.add_argument("--flat", action="store_true", help="one colour a face instead of the texture")
    ap.add_argument("--keep", help="keep the backend's files in this folder")
    ap.add_argument("--install", metavar="BACKEND", help="install a backend (triposr, hunyuan3d)")
    ap.add_argument("--check", action="store_true", help="what is installed")
    ap.add_argument("--home", default=HOME, help=f"where the backends live (default {HOME})")
    a = ap.parse_args()
    if a.install:
        return install(a.install, a.home)
    if a.check:
        return check(a.home)
    if not a.image or not a.out:
        ap.error("the picture and -o OUT.bm (or --install / --check)")
    import tempfile
    import meshy2mesh
    outdir = a.keep or tempfile.mkdtemp(prefix="local2mesh-")
    glb = make_glb(a.image, a.backend, a.command, a.home, outdir)
    name = (a.name or os.path.splitext(os.path.basename(a.image))[0])[:15]
    data = open(glb, "rb").read()
    model, sheet = meshy2mesh.convert(data, name, a.height, a.flat or os.path.exists(a.out), a.faces, 32)
    what = meshy2mesh.write_cart(a.out, model, sheet, name)
    print(f"local2mesh: {a.out}: {what} the model {name}: {len(model['verts'])} vertices, {len(model['faces'])} triangles"
          f"{'' if sheet else ' (flat colours)'}")
    if not a.keep:
        import shutil
        shutil.rmtree(outdir, ignore_errors=True)


if __name__ == "__main__":
    main()
