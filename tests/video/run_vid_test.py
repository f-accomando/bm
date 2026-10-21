#!/usr/bin/env python3
"""The .bmv tool and reader: test_vid dump makes the frames, scripts/mkvid.py packs them,
test_vid check reads the clip back. Also the refusals of mkvid.py.

  run_vid_test.py BUILD/host/test_vid WORKDIR
"""
import os
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
mkvid = os.path.join(root, "scripts", "mkvid.py")


def run(*a, ok=True):
    r = subprocess.run(list(a), capture_output=True, text=True)
    if (r.returncode == 0) != ok:
        sys.exit(f"FAIL: {a}\n{r.stdout}{r.stderr}")
    return r


def main():
    exe, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    run(exe, "dump", work)
    out = os.path.join(work, "clip.bmv")
    run(sys.executable, mkvid, "-o", out, "--jpegs", work, "--fps", "15", "--pcm", os.path.join(work, "a.raw"),
        "--rate", "22050", "--channels", "1", "--bits", "16")
    r = run(exe, "check", out)
    print(r.stdout.strip())

    # refusals of the tool
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", work, "--fps", "0", ok=False)
    empty = os.path.join(work, "empty")
    os.makedirs(empty, exist_ok=True)
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", empty, "--fps", "15", ok=False)
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", work, "--fps", "15",
        "--pcm", os.path.join(work, "a.raw"), "--rate", "100", ok=False)
    # a progressive frame (SOF2) is refused
    prog = os.path.join(work, "prog")
    os.makedirs(prog, exist_ok=True)
    data = bytearray(open(os.path.join(work, "f0.jpg"), "rb").read())
    i = data.index(b"\xff\xc0")
    data[i + 1] = 0xC2
    open(os.path.join(prog, "f0.jpg"), "wb").write(data)
    r = run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", prog, "--fps", "15", ok=False)
    assert "progressive" in r.stderr, r.stderr
    print("run_vid_test: all passed")


main()
