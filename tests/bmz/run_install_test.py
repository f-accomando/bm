#!/usr/bin/env python3
"""
The console's extraction of /bmz (src/kernel/bmz_install.c) on a FAT image: good files go to /bm, /bme, /bmk and are
deleted from /bmz; broken files stay; a file of the same name is replaced. Checked with fsck.vfat and mtools.

  python3 tests/bmz/run_install_test.py build/host/test_install
"""
import os
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmz  # noqa: E402
import mksd  # noqa: E402

PART_OFFSET = 2048 * 512
ENV = dict(os.environ, MTOOLS_SKIP_CHECK="1")


def cart(tag):
    return b"BMCART\0\0" + tag * 2000 + bytes(range(256)) * 30


def res(tag):
    return b"BMRES\0\0\0" + tag * 3000


def main():
    exe = os.path.abspath(sys.argv[1])
    old_pong = cart(b"old ")
    new_pong, proj, kit = cart(b"new "), cart(b"prj "), res(b"kit ")
    big = cart(b"big ") * 400                       # a few MB: more than one run of clusters
    bad_crc = bytearray(bmz.pack(cart(b"bad "), "badcrc.bm"))
    bad_crc[12] ^= 1
    bad_kind = bmz.MAGIC + bytes([1, 0, 0, 0]) + len(b"x" * 100).to_bytes(4, "little") \
        + zlib.crc32(b"x" * 100).to_bytes(4, "little") + b"nokind.bm".ljust(16, b"\0") + zlib.compress(b"x" * 100)
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        def put(name, data):
            p = os.path.join(tmp, name)
            open(p, "wb").write(data)
            return p

        files = [
            (put("old.bm", old_pong), "bm/PONG.BM"),                                  # replaced
            (put("a.bmz", bmz.pack(new_pong, "pong.bm")), "bmz/pong.bmz"),
            (put("b.bmz", bmz.pack(proj, "proj.bme")), "bmz/proj.bmz"),
            (put("c.bmz", bmz.pack(kit, "kit.bmk")), "bmz/kit.bmz"),
            (put("d.bmz", bmz.pack(big, "big.bm")), "bmz/big.bmz"),
            (put("e.bmz", bytes(bad_crc)), "bmz/badcrc.bmz"),
            (put("f.bmz", bad_kind), "bmz/nokind.bmz"),
            (put("g.bmz", b"not a bmz file at all, just text" * 4), "bmz/junk.bmz"),
            (put("h.txt", b"hello"), "bmz/notes.txt"),                                # not a .bmz: left alone
        ]
        img = os.path.join(tmp, "sd.img")
        mksd.build(img, files, 512, cluster_sectors=8)
        r = subprocess.run([exe, img, "4", "3"], cwd=tmp)       # good: pong, proj, kit, big; bad: badcrc, nokind, junk
        if r.returncode:
            print("FAIL test_install")
            return 1
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as f, open(part, "wb") as o:
            f.seek(PART_OFFSET)
            o.write(f.read())
        fsck = subprocess.run(["fsck.vfat", "-n", part], capture_output=True, text=True)
        if fsck.returncode:
            print("FAIL fsck.vfat:\n" + fsck.stdout + fsck.stderr)
            return 1
        print("ok   fsck.vfat clean")

        def read(path):
            return subprocess.run(["mtype", "-i", part, "::" + path], capture_output=True, env=ENV).stdout

        for path, want in (("/BM/PONG.BM", new_pong), ("/BME/PROJ.BME", proj), ("/BMK/KIT.BMK", kit),
                           ("/BM/BIG.BM", big)):
            got = read(path)
            ok = got == want
            print(("ok   " if ok else "FAIL ") + f"{path} {len(want)} bytes")
            failed += not ok
        listing = subprocess.run(["mdir", "-i", part, "-b", "::/BMZ"], capture_output=True, text=True, env=ENV).stdout
        left = sorted(os.path.basename(x).upper() for x in listing.split())
        want_left = ["BADCRC.BMZ", "JUNK.BMZ", "NOKIND.BMZ", "NOTES.TXT"]
        ok = left == want_left
        print(("ok   " if ok else "FAIL ") + f"/bmz keeps {left}")
        failed += not ok
    print("bmz install: all passed" if not failed else f"bmz install: {failed} failed")
    return 1 if failed else 0


sys.exit(main())
