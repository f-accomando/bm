#!/usr/bin/env python3
"""tools/local2mesh.py with a stand-in backend (--backend command: a tool
that writes the test .glb of glbfix.py): the .bm comes out textured and
reduced, the model is added to an existing cartridge flat, --check and a
missing backend speak clearly.

  check_local2mesh.py build/local2mesh"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmmesh  # noqa: E402

out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)
tool = os.path.join(HERE, "..", "..", "tools", "local2mesh.py")
fix = os.path.join(HERE, "glbfix.py")
pic = os.path.join(out_dir, "hero.png")
import glbfix  # noqa: E402
open(pic, "wb").write(glbfix.png(4, 4, [120] * 48))
home = os.path.join(out_dir, "home")
cart = os.path.join(out_dir, "hero.bm")
if os.path.exists(cart):
    os.remove(cart)
cmd = f"{sys.executable} {fix} {{out}} png"
r = subprocess.run([sys.executable, tool, pic, "-o", cart, "--backend", "command", "--command", cmd, "--faces", "10",
                    "--home", home], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
assert "the model hero: " in r.stdout, r.stdout
secs = dict(bmmesh.cart_sections(open(cart, "rb").read()))
models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
assert models[0]["name"] == "hero" and 1 <= len(models[0]["faces"]) <= 10, models[0]["name"]
assert any(f[3] == bmmesh.TEXTURED for f in models[0]["faces"]) and (2 in secs or 5 in secs), "textured, the sheet"
print("local2mesh: a textured, reduced model from the stand-in backend ok")
# into the same cartridge again: flat colours, two models
r = subprocess.run([sys.executable, tool, pic, "-o", cart, "--backend", "command", "--command", cmd, "--name", "two",
                    "--home", home], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
models, _ = bmmesh.decode(dict(bmmesh.cart_sections(open(cart, "rb").read()))[bmmesh.SEC_MESH])
assert [m["name"] for m in models] == ["hero", "two"] and all(f[3] != bmmesh.TEXTURED for f in models[1]["faces"])
print("local2mesh: added flat to the cartridge ok")
# --check, a backend not installed, a command that writes nothing
r = subprocess.run([sys.executable, tool, "--check", "--home", home], capture_output=True, text=True)
assert r.returncode == 0 and "triposr: not installed" in r.stdout and "hunyuan3d: not installed" in r.stdout, r.stdout
r = subprocess.run([sys.executable, tool, pic, "-o", cart, "--home", home], capture_output=True, text=True)
assert r.returncode != 0 and "--install triposr" in r.stderr, r.stderr
r = subprocess.run([sys.executable, tool, pic, "-o", cart, "--backend", "command", "--command", "true", "--home", home],
                   capture_output=True, text=True)
assert r.returncode != 0 and "wrote no .glb" in r.stderr, r.stderr
print("local2mesh: --check and the messages ok")
