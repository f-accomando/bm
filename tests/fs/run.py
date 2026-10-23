#!/usr/bin/env python3
"""
FAT write test: builds an SD image (scripts/mksd.py), runs the host test
binary on it, then checks the result with independent tools: fsck.vfat
(filesystem consistency) and mtools (reads back what bm wrote). Twice: a
small card with 512-byte clusters, and one with 4 KiB clusters as a real
card has (the last cluster of a file written only where it has data).

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


def check(binary, size_mib, cluster_sectors):
    readme = os.path.join(HERE, "..", "..", "README.md")
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    what = f"{cluster_sectors * 512} B clusters"
    with tempfile.TemporaryDirectory() as tmp:
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(readme, "carts/README.TXT"),
                         (readme, "carts/Un gioco da cancellare.bm")], size_mib,
                   cluster_sectors=cluster_sectors)
        r = subprocess.run([binary, img, readme, "/carts/README.TXT"], cwd=tmp)
        if r.returncode:
            print(f"FAIL ({what})")
            return 1
        # the partition alone, for fsck and mtools
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(PART_OFFSET)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        if fsck.returncode != 0:
            print(f"FAIL fsck.vfat ({what}):\n" + fsck.stdout + fsck.stderr)
            return 1
        got = subprocess.run(["mtype", "-i", part, "::/BM/SAVE/SNAKE.SAV"],
                             capture_output=True, env=env).stdout
        with open(os.path.join(tmp, "snake.expected"), "rb") as f:
            want = f.read()
        if got != want:
            print(f"FAIL mtools reads {len(got)} bytes of SNAKE.SAV, expected {len(want)} ({what})")
            return 1
        cfg = subprocess.run(["mtype", "-i", part, "::/BM/CONFIG.TXT"],
                             capture_output=True, env=env).stdout
        if cfg != b"layout=it\ndraw=direct\n":
            print(f"FAIL mtools reads CONFIG.TXT as {cfg!r} ({what})")
            return 1
        listing = subprocess.run(["mdir", "-i", part, "::/BM/SAVE"],
                                 capture_output=True, text=True, env=env).stdout
        if "164 files" not in listing:          # 202 - 50 deleted + 10, "." and ".."
            print(f"FAIL mdir ({what}):\n" + listing)
            return 1
        carts = subprocess.run(["mdir", "-i", part, "::/CARTS"],
                               capture_output=True, text=True, env=env).stdout
        if "cancellare" in carts or "README" not in carts:
            print(f"FAIL mdir /carts after the delete ({what}):\n" + carts)
            return 1
        # what the writers at the ticks left (test_fat.c, nest_tick)
        mix = subprocess.run(["mdir", "-b", "-i", part, "::/MIX"],
                             capture_output=True, text=True, env=env).stdout.upper()
        for name in ("SAME.BIN", "OTHER.TXT", "OLD1.BIN", "ONE.BIN", "TAIL.BIN"):
            if name not in mix:
                print(f"FAIL mdir /mix: no {name} ({what}):\n" + mix)
                return 1
        if "GONE.BIN" in mix or "TWICE.BIN" in mix or "BIG.BIN" in mix:
            print(f"FAIL mdir /mix: a file that should be gone ({what}):\n" + mix)
            return 1
        # new names are written in lowercase (the NT flags of the entry): mtools shows them so
        lc = subprocess.run(["mdir", "-b", "-i", part, "::/LC"],
                            capture_output=True, text=True, env=env).stdout
        for name in ("upper.txt", "mixed.bin", "noext", "a1.dat", "x.123", "sub"):
            if not any(l.rstrip("/").endswith("/" + name) for l in lc.split()):
                print(f"FAIL mdir /lc: no lowercase {name} ({what}):" + chr(10) + lc)
                return 1
        if "lower.txt" in lc or "123.456" not in lc.upper():
            print(f"FAIL mdir /lc: wrong files ({what}):" + chr(10) + lc)
            return 1
    print(f"fat ({what}): fsck.vfat clean, mtools reads the files back")
    return 0


def main():
    binary = os.path.abspath(sys.argv[1])
    return check(binary, 64, 1) or check(binary, 512, 8)


if __name__ == "__main__":
    sys.exit(main())
