#!/usr/bin/env python3
"""The compatibility block of the .bm header (bytes 120..127, flag bit 4):
mkbm.py writes it, keeps it on --from, and a cartridge above 8 MB is built (no 8 MB limit)."""
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MKBM = os.path.join(ROOT, "scripts", "mkbm.py")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mkbm  # noqa: E402


def run(*args):
    return subprocess.run([sys.executable, MKBM, *args], capture_output=True, text=True, cwd=ROOT)


class Compat(unittest.TestCase):
    def test_old_file_has_zeros(self):
        d = mkbm.pack(b"x\n")
        self.assertEqual(d[120:128], b"\0" * 8)
        self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 0)

    def test_mark_compat_bytes(self):
        d = mkbm.mark_compat(mkbm.pack(b"x\n"), 3, 3, 1, 3, 700, 4, 45)
        self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 16)
        self.assertEqual(d[120:128], bytes([3, 3, 1, 3, 700 & 255, 700 >> 8, 4, 45]))

    def test_targets_must_be_named(self):
        with self.assertRaises(SystemExit):
            mkbm.mark_compat(mkbm.pack(b"x\n"), 0)

    def test_cli_writes_block_and_crc_ok(self):
        with tempfile.TemporaryDirectory() as tmp:
            lua = os.path.join(tmp, "main.lua")
            out = os.path.join(tmp, "g.bm")
            open(lua, "w").write("function _draw() cls(0) end\n")
            r = run("-o", out, "--lua", lua, "--targets", "pi,rgb30", "--input", "pad,keyboard",
                    "--aspect", "square", "--needs", "3d", "--min-ram", "300", "--min-spec", "4", "--min-width", "640")
            self.assertEqual(r.returncode, 0, r.stderr)
            d = open(out, "rb").read()
            self.assertEqual(d[120:128], bytes([3, 3, 2, 1, 300 & 255, 300 >> 8, 4, 40]))
            self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 16)
            import zlib
            self.assertEqual(zlib.crc32(d[128:]) & 0xFFFFFFFF, struct.unpack_from("<I", d, 20)[0])

    def test_cli_unknown_name_and_missing_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            lua = os.path.join(tmp, "main.lua")
            open(lua, "w").write("x\n")
            r = run("-o", os.path.join(tmp, "g.bm"), "--lua", lua, "--targets", "pi,moon")
            self.assertNotEqual(r.returncode, 0)
            r = run("-o", os.path.join(tmp, "g.bm"), "--lua", lua, "--input", "pad")
            self.assertNotEqual(r.returncode, 0)

    def test_from_keeps_block_and_clears_built_from(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "X.bm")
            dst = os.path.join(tmp, "X.b16")
            raw = bytearray(mkbm.mark_compat(mkbm.pack(b"x\n"), 2, 1, 0, 0, 0, 4, 0))
            raw[104:112] = b"PONG.BME"
            open(src, "wb").write(raw)
            r = run("--from", src, "--b16", "-o", dst)
            self.assertEqual(r.returncode, 0, r.stderr)
            d = open(dst, "rb").read()
            self.assertEqual(d[104:120], b"\0" * 16)
            self.assertEqual(d[120:128], bytes([2, 1, 0, 0, 0, 0, 4, 0]))
            self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 16)

    def test_cart_above_8_mib_is_built(self):
        with tempfile.TemporaryDirectory() as tmp:
            lua = os.path.join(tmp, "main.lua")
            open(lua, "w").write("-- " + "x" * (9 << 20) + "\n")
            for name in ("big.bm", "big.b16"):
                out = os.path.join(tmp, name)
                r = run("-o", out, "--lua", lua)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertGreater(os.path.getsize(out), 8 << 20)


if __name__ == "__main__":
    unittest.main()
