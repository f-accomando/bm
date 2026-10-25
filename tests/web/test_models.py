#!/usr/bin/env python3
"""The test catalog web/models (mock content): the committed files are the ones web/models/build.py makes,
each .bmm passes the BMRES validator (scripts/bmres.py), the pages link only to files that are there,
and the names follow the /bmm rule. The kernel's own reader runs in run_bmm_test.py (file_write)."""
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
MODELS = os.path.join(ROOT, "web", "models")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL", msg)


names = ["house", "tree", "crate", "walker"]
for n in names:
    check(re.fullmatch(r"[a-z0-9_-]{1,8}", n) is not None, n + ": name rule")
    data = open(os.path.join(MODELS, n + ".bmm"), "rb").read()
    try:
        f = bmres.load(data, n + ".bmm")
        bmres.check(f, n + ".bmm")
        check(f.kind == 1, n + ": kind is model")
        check(f.get(bmres.SEC_MESH) is not None, n + ": has MESH")
        check(bmres.dump(f) == data, n + ": dump(load(x)) == x")
    except bmres.ResError as e:
        check(False, "%s: %s" % (n, e))
    png = open(os.path.join(MODELS, n + ".png"), "rb").read()
    check(png[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", png[16:24]) == (256, 256), n + ": 256x256 PNG")

pages = {p: open(os.path.join(MODELS, p), encoding="utf-8").read() for p in ["index.html"] + [n + ".html" for n in names]}
for p, text in pages.items():
    check("<title>" in text and "<h1>" in text, p + ": title and h1")
    check("<script" not in text.lower(), p + ": no script")
    for ref in re.findall(r'(?:href|src)="([^"]+)"', text):
        if ref.startswith("../"):
            continue
        check(os.path.exists(os.path.join(MODELS, ref)), "%s: link %s is not a file" % (p, ref))
for n in names:
    check('<a href="%s.bmm">Download</a>' % n in pages[n + ".html"], n + ": plain Download link")
    check('<img src="%s.png"' % n in pages[n + ".html"], n + ": preview")
    check("CC0" in pages[n + ".html"], n + ": licence note")
    check('href="%s.html"' % n in pages["index.html"], n + ": in the index")
robots = open(os.path.join(MODELS, "robots.txt")).read()
check("crawl-delay" not in robots.lower(), "robots.txt: no Crawl-delay")

r = subprocess.run([sys.executable, os.path.join(MODELS, "build.py"), "--check"], capture_output=True, text=True)
check(r.returncode == 0, "build.py --check: " + r.stdout.strip())

print("test-models: %s" % ("FAIL (%d)" % len(fails) if fails else "ok"))
sys.exit(1 if fails else 0)
