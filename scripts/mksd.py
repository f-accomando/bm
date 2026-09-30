#!/usr/bin/env python3
"""
Builds an SD card image: MBR + one FAT32 partition (type 0x0C) starting at
1 MiB, filled with the given files. Used by `make image` (bm33.img, ready
for Raspberry Pi Imager / balenaEtcher / dd) and by the QEMU tests.
Needs mkfs.vfat (dosfstools) and mtools.

  scripts/mksd.py OUT.img [--size-mib 128] [--label BM33SD] [SRC=DEST ...]
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile


def tool(name):
    # mkfs.vfat lives in /usr/sbin, which is not on every user's PATH
    path = shutil.which(name) or shutil.which(name, path="/usr/sbin:/sbin")
    if not path:
        pkg = "dosfstools" if name.startswith("mkfs") else "mtools"
        sys.exit(f"mksd.py: {name} not found: sudo apt install dosfstools mtools ({pkg})")
    return path


def build(out, files, size_mib=128, label="BM33SD"):
    start = 2048                                    # sectors
    total = size_mib * 2048                         # QEMU wants a power of 2
    part_sectors = total - start
    with tempfile.TemporaryDirectory() as tmp:
        part = os.path.join(tmp, "part.img")
        with open(part, "wb") as f:
            f.truncate(part_sectors * 512)
        small = ["-s", "1"] if size_mib <= 256 else []   # FAT32 needs >= 65525 clusters
        subprocess.run([tool("mkfs.vfat"), "-F", "32", *small, "-n", label, part],
                       check=True, stdout=subprocess.DEVNULL)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        dirs = set()
        for src, dest in files:
            d = os.path.dirname(dest.strip("/"))
            parts = d.split("/") if d else []
            for i in range(len(parts)):
                sub = "/".join(parts[:i + 1])
                if sub not in dirs:
                    subprocess.run([tool("mmd"), "-i", part, "::/" + sub], check=True, env=env)
                    dirs.add(sub)
            subprocess.run([tool("mcopy"), "-i", part, src, "::/" + dest.strip("/")], check=True, env=env)
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
    ap.add_argument("--label", default="BM33SD")
    a = ap.parse_intermixed_args()
    build(a.out, [tuple(f.split("=", 1)) for f in a.files], a.size_mib, a.label)
    print(f"{a.out}: {a.size_mib} MiB, {len(a.files)} files")


if __name__ == "__main__":
    main()
