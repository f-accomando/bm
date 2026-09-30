#!/usr/bin/env python3
"""
FAT write test: builds an SD image (scripts/mksd.py), runs the host test
binary on it, then checks the result with independent tools: fsck.vfat
(filesystem consistency) and mtools (reads back what bm33 wrote).

  tests/fs/run.py BUILD/host/test_fat
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mksd  # noqa: E402

PART_OFFSET = 2048 * 512


def main():
    binary = os.path.abspath(sys.argv[1])
    readme = os.path.join(HERE, "..", "..", "README.md")
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    with tempfile.TemporaryDirectory() as tmp:
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(readme, "carts/README.TXT"),
                         (readme, "carts/Un gioco da cancellare.bm")], 64)
        r = subprocess.run([binary, img, readme, "/carts/README.TXT"], cwd=tmp)
        if r.returncode:
            return 1
        # the partition alone, for fsck and mtools
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(PART_OFFSET)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        if fsck.returncode != 0:
            print("FAIL fsck.vfat:\n" + fsck.stdout + fsck.stderr)
            return 1
        got = subprocess.run(["mtype", "-i", part, "::/BM33/SAVE/SNAKE.SAV"],
                             capture_output=True, env=env).stdout
        with open(os.path.join(tmp, "snake.expected"), "rb") as f:
            want = f.read()
        if got != want:
            print(f"FAIL mtools reads {len(got)} bytes of SNAKE.SAV, expected {len(want)}")
            return 1
        cfg = subprocess.run(["mtype", "-i", part, "::/BM33/CONFIG.TXT"],
                             capture_output=True, env=env).stdout
        if cfg != b"layout=it\ndraw=direct\n":
            print(f"FAIL mtools reads CONFIG.TXT as {cfg!r}")
            return 1
        listing = subprocess.run(["mdir", "-i", part, "::/BM33/SAVE"],
                                 capture_output=True, text=True, env=env).stdout
        if "164 files" not in listing:          # 202 - 50 deleted + 10, "." and ".."
            print("FAIL mdir:\n" + listing)
            return 1
        carts = subprocess.run(["mdir", "-i", part, "::/CARTS"],
                               capture_output=True, text=True, env=env).stdout
        if "cancellare" in carts or "README" not in carts:
            print("FAIL mdir /carts after the delete:\n" + carts)
            return 1
    print("fat: fsck.vfat clean, mtools reads the files back")
    return 0


if __name__ == "__main__":
    sys.exit(main())
