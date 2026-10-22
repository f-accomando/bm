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
    r = run(exe, "raw")
    print(r.stdout.strip())
    r = run(exe, "rle")
    print(r.stdout.strip())
    run(exe, "dump", work)
    out = os.path.join(work, "clip.bmv")
    run(sys.executable, mkvid, "-o", out, "--jpegs", work, "--fps", "15", "--pcm", os.path.join(work, "a.raw"),
        "--rate", "22050", "--channels", "1", "--bits", "16")
    r = run(exe, "check", out, "1", "16")
    print(r.stdout.strip())
    out2 = os.path.join(work, "clip2.bmv")
    run(sys.executable, mkvid, "-o", out2, "--jpegs", work, "--fps", "15", "--pcm", os.path.join(work, "a.raw"),
        "--rate", "22050", "--channels", "2", "--bits", "8")
    run(exe, "check", out2, "2", "8")

    # refusals of the tool
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", work, "--fps", "0", ok=False)
    empty = os.path.join(work, "empty")
    os.makedirs(empty, exist_ok=True)
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", empty, "--fps", "15", ok=False)
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", work, "--fps", "15",
        "--pcm", os.path.join(work, "a.raw"), "--rate", "100", ok=False)
    # a picture over 1920 wide, and a lossless/arithmetic frame, are refused
    big = os.path.join(work, "big")
    os.makedirs(big, exist_ok=True)
    data = bytearray(open(os.path.join(work, "f0.jpg"), "rb").read())
    i = data.index(b"\xff\xc0")
    data[i + 7:i + 9] = (3840).to_bytes(2, "big")
    open(os.path.join(big, "f0.jpg"), "wb").write(data)
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", big, "--fps", "15", ok=False)
    lossless = os.path.join(work, "lossless")
    os.makedirs(lossless, exist_ok=True)
    data = bytearray(open(os.path.join(work, "f0.jpg"), "rb").read())
    i = data.index(b"\xff\xc0")
    data[i + 1] = 0xC3
    open(os.path.join(lossless, "f0.jpg"), "wb").write(data)
    run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", lossless, "--fps", "15", ok=False)
    # a progressive frame (SOF2) is refused
    prog = os.path.join(work, "prog")
    os.makedirs(prog, exist_ok=True)
    data = bytearray(open(os.path.join(work, "f0.jpg"), "rb").read())
    i = data.index(b"\xff\xc0")
    data[i + 1] = 0xC2
    open(os.path.join(prog, "f0.jpg"), "wb").write(data)
    r = run(sys.executable, mkvid, "-o", os.path.join(work, "x.bmv"), "--jpegs", prog, "--fps", "15", ok=False)
    assert "not baseline" in r.stderr, r.stderr
    print("run_vid_test: all passed")


main()
