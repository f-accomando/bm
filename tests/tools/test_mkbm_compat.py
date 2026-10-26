#!/usr/bin/env python3
"""The compatibility block of the .bm header (bytes 120..127, flag bit 4):
mkbm.py writes it from the flags or from the keys of bm.info (a flag wins), keeps it on --from, and a
cartridge above 8 MB is built (no 8 MB limit)."""
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
            dst = os.path.join(tmp, "Y.bm")
            raw = bytearray(mkbm.mark_compat(mkbm.pack(b"x\n"), 2, 1, 0, 0, 0, 4, 0))
            raw[104:112] = b"PONG.BME"
            open(src, "wb").write(raw)
            r = run("--from", src, "-o", dst)
            self.assertEqual(r.returncode, 0, r.stderr)
            d = open(dst, "rb").read()
            self.assertEqual(d[104:120], b"\0" * 16)
            self.assertEqual(d[120:128], bytes([2, 1, 0, 0, 0, 0, 4, 0]))
            self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 16)

    def test_cart_above_8_mib_is_built(self):
        with tempfile.TemporaryDirectory() as tmp:
            lua = os.path.join(tmp, "main.lua")
            open(lua, "w").write("-- " + "x" * (9 << 20) + "\n")
            for name in ("big.bm",):
                out = os.path.join(tmp, name)
                r = run("-o", out, "--lua", lua)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertGreater(os.path.getsize(out), 8 << 20)

    def build_with_info(self, tmp, info, *flags):
        lua = os.path.join(tmp, "main.lua")
        inf = os.path.join(tmp, "bm.info")
        out = os.path.join(tmp, "g.bm")
        open(lua, "w").write("function _draw() cls(0) end\n")
        open(inf, "w").write(info)
        r = run("-o", out, "--lua", lua, "--info", inf, *flags)
        return r, out

    def test_info_keys_write_the_block(self):
        info = ("name: Keys\ntargets: pi, rgb30\ninput: pad,keyboard\naspect: square\nneeds: 3d\n"
                "min_ram: 300\nmin_spec: 4\nmin_width: 640\n")
        with tempfile.TemporaryDirectory() as tmp:
            r, out = self.build_with_info(tmp, info)
            self.assertEqual(r.returncode, 0, r.stderr)
            d = open(out, "rb").read()
            self.assertEqual(d[120:128], bytes([3, 3, 2, 1, 300 & 255, 300 >> 8, 4, 40]))
            self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 16)

    def test_info_targets_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, out = self.build_with_info(tmp, "name: T\ntargets: rgb30\n")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(open(out, "rb").read()[120:128], bytes([2, 0, 0, 0, 0, 0, 0, 0]))

    def test_flag_wins_over_info_key(self):
        info = "targets: pi\ninput: pad\naspect: wide\nmin_ram: 100\n"
        with tempfile.TemporaryDirectory() as tmp:
            r, out = self.build_with_info(tmp, info, "--targets", "rgb30", "--input", "keyboard",
                                          "--min-ram", "200")
            self.assertEqual(r.returncode, 0, r.stderr)
            # targets, input, min RAM from the flags; aspect from the key
            self.assertEqual(open(out, "rb").read()[120:128], bytes([2, 2, 1, 0, 200, 0, 0, 0]))

    def test_info_key_without_targets_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            for info in ("input: pad\n", "aspect: wide\n", "needs: 3d\n", "min_ram: 64\n",
                         "min_spec: 3\n", "min_width: 320\n"):
                r, _ = self.build_with_info(tmp, info)
                self.assertNotEqual(r.returncode, 0, info)
            r, out = self.build_with_info(tmp, "input: pad\n", "--targets", "pi")    # the flag gives the targets
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(open(out, "rb").read()[120:122], bytes([1, 1]))

    def test_info_without_block_keys_writes_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, out = self.build_with_info(tmp, "name: Plain\n")
            self.assertEqual(r.returncode, 0, r.stderr)
            d = open(out, "rb").read()
            self.assertEqual(d[120:128], b"\0" * 8)
            self.assertEqual(struct.unpack_from("<H", d, 18)[0] & 16, 0)

    def test_info_bad_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            for info in ("targets: moon\n", "targets: pi\nmin_ram: many\n", "targets: pi\ninput: joystick\n"):
                r, _ = self.build_with_info(tmp, info)
                self.assertNotEqual(r.returncode, 0, info)

    def test_info_text_is_kept_in_the_cartridge(self):
        with tempfile.TemporaryDirectory() as tmp:
            r, out = self.build_with_info(tmp, "name: Kept\ntargets: pi,rgb30\n")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn(b"targets: pi,rgb30", open(out, "rb").read())


if __name__ == "__main__":
    unittest.main()
