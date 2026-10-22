#!/usr/bin/env python3
"""
Stage 2 of the web player (docs/design/bm-web-player.md): the console's runtime built to
WebAssembly (make bmhost-wasm) draws the same frames as the PC build (make bmhost).

  run_wasm.py BUILD_DIR

Packs tests/webplayer/cart.lua, runs build/web/bmhost.js under Node, and checks that
(1) the run ends ok, (2) the two frames are equal to the expected SHA-256 in
tests/webplayer/expected.sha256, (3) when build/host/bmhost-bin exists, its frames are
byte-equal to the WebAssembly ones, then (4) the browser runner (make web-player) in test_bmweb.js. Exit 0 and a message "skipped" when Node is missing.
"""
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    build = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build"))
    node = shutil.which("node")
    js = os.path.join(build, "web", "bmhost.js")
    if not node:
        print("test-wasm: skipped (no node: source ~/emsdk/emsdk_env.sh)")
        return 0
    if not os.path.exists(js):
        print("test-wasm: FAIL " + js + " is missing (make bmhost-wasm)")
        return 1
    work = os.path.join(build, "webtest")
    shutil.rmtree(work, ignore_errors=True)
    for d in ("wasm", "native", "sd", "nsd"):
        os.makedirs(os.path.join(work, d))
    cart = os.path.join(work, "cart.bm")
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", cart,
                    "--lua", os.path.join(HERE, "cart.lua"), "--title", "web test", "--author", "tests",
                    "--res", "360x360"], check=True, stdout=subprocess.DEVNULL)
    args = [cart, "--quiet", "--seconds", "5", "--input", os.path.join(HERE, "input.txt")]
    r = subprocess.run([node, js] + args + ["--shots", os.path.join(work, "wasm"), "--sd", os.path.join(work, "sd")],
                       capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0 or ", ok," not in out:
        print("test-wasm: FAIL the wasm run:\n" + out)
        return 1
    expected = {}
    with open(os.path.join(HERE, "expected.sha256")) as f:
        for line in f:
            if line.strip():
                h, n = line.split()
                expected[n.lstrip("*")] = h
    fails = 0
    for name in ("first.png", "second.png"):
        got = sha(os.path.join(work, "wasm", name))
        if got != expected.get(name):
            print("test-wasm: FAIL %s sha256 %s, expected %s" % (name, got, expected.get(name)))
            fails += 1
    native = os.path.join(build, "host", "bmhost-bin")
    how = "no native build to compare"
    if os.path.exists(native):
        subprocess.run([native] + args + ["--shots", os.path.join(work, "native"), "--sd", os.path.join(work, "nsd")],
                       capture_output=True, check=True)
        for name in ("first.png", "second.png"):
            if sha(os.path.join(work, "native", name)) != sha(os.path.join(work, "wasm", name)):
                print("test-wasm: FAIL %s differs from the PC build" % name)
                fails += 1
        how = "equal to the PC build"
    if fails:
        return 1
    print("test-wasm: ok, 2 frames match the expected hashes (%s)" % how)
    # the browser runner (web/runner/bmweb.c) under Node, with the hooks of web/player.js
    if not os.path.exists(os.path.join(build, "web", "player", "bmweb.js")):
        print("test-wasm: skipped the browser runner (make web-player)")
        return 0
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", os.path.join(work, "play.bm"),
                    "--lua", os.path.join(HERE, "play.lua"), "--title", "play test", "--author", "tests",
                    "--res", "360x360"], check=True, stdout=subprocess.DEVNULL)
    r = subprocess.run([node, os.path.join(HERE, "test_bmweb.js"), build], capture_output=True, text=True)
    print((r.stdout + r.stderr).strip())
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
