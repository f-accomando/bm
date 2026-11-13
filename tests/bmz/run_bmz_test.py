#!/usr/bin/env python3
"""Round trip and refusals of .bmz: core/scripts/bmz.py makes the files, build/host/test_bmz (core/src/bm/bmz.c) reads them.

  python3 tests/bmz/run_bmz_test.py build/host/test_bmz
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "scripts"))
import bmz  # noqa: E402

exe = sys.argv[1]
failed = 0


def cart():
    """A small valid-looking cartridge: the magic, then bytes with some structure"""
    return b"BMCART\0\0" + bytes(range(256)) * 40 + b"lua " * 3000


def res():
    return b"BMRES\0\0\0" + b"mesh" * 5000


def run(label, path, arg, want_ok=True):
    global failed
    r = subprocess.run([exe, path, arg], capture_output=True, text=True)
    good = r.returncode == 0
    print(("ok   " if good else "FAIL ") + label + ("" if good else "  " + r.stdout.strip()))
    failed += not good


def patched(z, off, new):
    b = bytearray(z)
    b[off:off + len(new)] = new
    return bytes(b)


with tempfile.TemporaryDirectory() as d:
    def put(name, data):
        p = os.path.join(d, name)
        open(p, "wb").write(data)
        return p

    # good files: every kind, written by the Python tool, read by the C reader
    for name, raw in (("pong.bm", cart()), ("proj.bme", cart()), ("knight.bmm", res()), ("kit.bmk", res()),
                      ("pic.bmi", res()), ("snd.bms", res()), ("map.bmt", res()), ("pal.bmc", res())):
        z = bmz.pack(raw, name)
        run("round trip " + name, put(name + ".z", z), put(name + ".raw", raw))
        back_name, back = bmz.unpack(z)
        if back_name != name or back != raw:
            print("FAIL python unpack " + name)
            failed += 1
    # a zlib level the C inflate must read too
    run("level 1", put("l1.z", bmz.pack(cart(), "l1.bm", 1)), put("l1.raw", cart()))
    run("level 0 (stored)", put("l0.z", bmz.pack(cart(), "l0.bm", 0)), put("l0.raw", cart()))

    good = bmz.pack(cart(), "pong.bm")
    raw = cart()
    # refusals: the C reader says why
    run("short", put("s.z", good[:20]), "SHORT")
    run("magic", put("m.z", patched(good, 0, b"XXXX")), "MAGIC")
    run("version 2", put("v.z", patched(good, 4, struct.pack("<H", 2))), "VERSION")
    run("flags set", put("f.z", patched(good, 6, struct.pack("<H", 1))), "VERSION")
    run("size zero", put("z0.z", patched(good, 8, struct.pack("<I", 0))), "SIZE")
    run("size too big", put("zb.z", patched(good, 8, struct.pack("<I", 200 << 20))), "SIZE")
    run("name upper case", put("n1.z", patched(good, 16, b"PONG.bm\0")), "NAME")
    run("name without extension", put("n2.z", patched(good, 16, b"pong\0\0\0\0")), "NAME")
    run("name with a path", put("n3.z", patched(good, 16, b"../x.bm\0")), "NAME")
    run("name unknown extension", put("n4.z", patched(good, 16, b"pong.exe\0")), "NAME")
    run("name not ended", put("n5.z", patched(good, 16, b"abcdefgh.bm12345")), "NAME")
    run("garbage after the NUL", put("n6.z", patched(good, 16, b"pong.bm\0x\0\0\0\0\0\0\0")), "NAME")
    run("size smaller than the data", put("sm.z", patched(good, 8, struct.pack("<I", len(raw) - 1))), "INFLATE")
    run("size bigger than the data", put("sb.z", patched(good, 8, struct.pack("<I", len(raw) + 1))), "INFLATE")
    run("crc", put("c.z", patched(good, 12, struct.pack("<I", zlib.crc32(raw) ^ 1))), "CRC")
    run("broken stream", put("b.z", good[:bmz.HEAD + 2] + bytes(len(good) - bmz.HEAD - 2)), "INFLATE")
    run("truncated stream", put("t.z", good[:len(good) - 30]), "INFLATE")
    # the bytes are what the CRC says, but they are not a cartridge: built by hand, the tool would refuse
    notcart = b"hello world, this is not a cartridge" * 20
    head = bmz.MAGIC + struct.pack("<HHII", 1, 0, len(notcart), zlib.crc32(notcart)) + b"pong.bm".ljust(16, b"\0")
    run("not a cartridge", put("k.z", head + zlib.compress(notcart)), "KIND")
    run("a cartridge named .bmm", put("k2.z", bmz.MAGIC + struct.pack("<HHII", 1, 0, len(raw), zlib.crc32(raw))
                                      + b"x.bmm".ljust(16, b"\0") + zlib.compress(raw)), "KIND")

    # the Python tool refuses the same things
    for label, name, data in (("python: upper case name", "PONG.bm", cart()), ("python: no extension", "pong", cart()),
                              ("python: not a cartridge", "pong.bm", b"hello" * 100),
                              ("python: long name", "toolongname.bm", cart()), ("python: empty", "pong.bm", b"")):
        try:
            bmz.pack(data, name)
            print("FAIL " + label)
            failed += 1
        except SystemExit:
            print("ok   " + label + " refused")

print("bmz: all passed" if not failed else f"bmz: {failed} failed")
sys.exit(1 if failed else 0)
