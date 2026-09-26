#!/usr/bin/env python3
"""
Builds an SD card image for QEMU: MBR + one FAT32 partition (type 0x0C)
starting at 1 MiB, filled with the given files. Needs mkfs.vfat and mtools.

  tests/mksd.py OUT.img [--size-mib 128] [SRC=DEST ...]
"""
import argparse
import os
import struct
import subprocess
import tempfile


def build(out, files, size_mib=128):
    start = 2048                                    # sectors
    total = size_mib * 2048                         # QEMU wants a power of 2
    part_sectors = total - start
    with tempfile.TemporaryDirectory() as tmp:
        part = os.path.join(tmp, "part.img")
        with open(part, "wb") as f:
            f.truncate(part_sectors * 512)
        small = ["-s", "1"] if size_mib <= 256 else []   # FAT32 needs >= 65525 clusters
        subprocess.run(["mkfs.vfat", "-F", "32", *small, "-n", "BM33SD", part],
                       check=True, stdout=subprocess.DEVNULL)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        dirs = set()
        for src, dest in files:
            d = os.path.dirname(dest.strip("/"))
            parts = d.split("/") if d else []
            for i in range(len(parts)):
                sub = "/".join(parts[:i + 1])
                if sub not in dirs:
                    subprocess.run(["mmd", "-i", part, "::/" + sub], check=True, env=env)
                    dirs.add(sub)
            subprocess.run(["mcopy", "-i", part, src, "::/" + dest.strip("/")], check=True, env=env)
        mbr = bytearray(512)
        entry = struct.pack("<B3sB3sII", 0x00, b"\xfe\xff\xff", 0x0C, b"\xfe\xff\xff",
                            start, part_sectors)
        mbr[446:462] = entry
        mbr[510:512] = b"\x55\xaa"
        with open(out, "wb") as f:
            f.write(mbr)
            f.write(b"\0" * (start * 512 - 512))
            with open(part, "rb") as p:
                while True:
                    chunk = p.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("files", nargs="*", help="SRC=DEST")
    ap.add_argument("--size-mib", type=int, default=128)
    a = ap.parse_args()
    build(a.out, [tuple(f.split("=", 1)) for f in a.files], a.size_mib)


if __name__ == "__main__":
    main()
