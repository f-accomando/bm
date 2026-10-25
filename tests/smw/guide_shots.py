#!/usr/bin/env python3
"""Screenshots of the guide edition (the real level, redrawn graphics) for the PDF guide.

    python3 tests/smw/guide_shots.py          # the redrawn edition, into manuals/shots (tracked)
    python3 tests/smw/guide_shots.py --real   # the real build, into carts/smw/extracted/shots (ignored)

Builds carts/smw/guide/build.py (build/smw-guide/guide.b16), plays a few input scripts in bmhost
(make build/host/bmhost-bin first) and writes manuals/shots/smw-guide-NN-name.png."""
import os, shutil, subprocess, sys, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REAL = "--real" in sys.argv
CART = ROOT + ("/build/smw/smw.b16" if REAL else "/build/smw-guide/guide.b16")
OUT = ROOT + ("/carts/smw/extracted/shots" if REAL else "/manuals/shots")
PREFIX = "smw-real" if REAL else "smw-guide"
# name -> (input script, frame to shoot). Frames are 60 a second; the game ticks at 50 Hz.
SCENES = {
    "01-start":        ("", 20),
    "02-walking":      ("30 pad 1 right", 80),
    "03-run-pmeter":   ("30 pad 1 right y", 140),
    "04-mid-jump":     ("30 pad 1 right y\n80 pad 1 right y b", 108),
    "05-landing":      ("30 pad 1 right y\n80 pad 1 right y b", 130),
    "06-camera":       ("30 pad 1 right y\n150 pad 1 right y b\n200 pad 1 right y", 232),
    "07-bump":         ("30 pad 1 right y\n100 pad 1 right y b", 123),
}


def main():
    builder = "/carts/smw/build/build.py" if REAL else "/carts/smw/guide/build.py"
    subprocess.run([sys.executable, ROOT + builder], check=True, capture_output=True)
    os.makedirs(OUT, exist_ok=True)
    for name, (script, frame) in SCENES.items():
        tmp = tempfile.mkdtemp()
        inp = tmp + "/in.txt"
        open(inp, "w").write(script + "\n%d shot s\n" % frame)
        subprocess.run([ROOT + "/build/host/bmhost-bin", CART, "--square", "--seconds", str(frame / 60 + 1),
                        "--input", inp, "--shots", tmp, "--quiet"], check=True, capture_output=True)
        shutil.copy(tmp + "/s.png", "%s/%s-%s.png" % (OUT, PREFIX, name))
        if name == "03-run-pmeter":          # the HUD line, two times bigger
            im = Image.open(tmp + "/s.png").convert("RGB").crop((0, 0, 180, 30))
            im.resize((im.width * 4, im.height * 4), Image.NEAREST).save("%s/%s-08-hud.png" % (OUT, PREFIX))
        shutil.rmtree(tmp)
        print("wrote", name)


if __name__ == "__main__":
    main()
