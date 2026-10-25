#!/usr/bin/env python3
"""
Steps of a queued file_write: builds an SD image with 4 KB clusters and one with 32 KB clusters
(a real card's; the 32 KB card has 2 GB so that FAT32 has 65525 clusters), runs the host test binary on each, then checks the result with fsck.vfat.

  tests/fs/run_queue.py BUILD/host/test_fat_queue
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
    what = f"{cluster_sectors * 512} B clusters"
    with tempfile.TemporaryDirectory() as tmp:
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, [(readme, "carts/README.TXT")], size_mib, cluster_sectors=cluster_sectors)
        print(f"-- {what}")
        if subprocess.run([binary, img], cwd=tmp).returncode:
            print(f"FAIL ({what})")
            return 1
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:      # sparse: a 32 KB cluster card is 2 GB
            f.seek(PART_OFFSET)
            while True:
                blk = f.read(1 << 20)
                if not blk:
                    break
                if blk.count(0) == len(blk):
                    o.seek(len(blk), 1)
                else:
                    o.write(blk)
            o.truncate()
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        if fsck.returncode != 0:
            print(f"FAIL fsck.vfat ({what}):\n" + fsck.stdout + fsck.stderr)
            return 1
    return 0


def main():
    binary = os.path.abspath(sys.argv[1])
    return check(binary, 512, 8) or check(binary, 2112, 64)


if __name__ == "__main__":
    sys.exit(main())
