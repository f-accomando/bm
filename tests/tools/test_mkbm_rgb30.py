#!/usr/bin/env python3
"""
Unit test for scripts/mkbm.py and the RGB30: a cartridge lists the RGB30 in its header block, a
need of a mouse device warns, any screen size and any list of kernels is quiet, and the tool
no longer writes a .b16 (one cartridge type, .bm).
"""
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MKBM_PATH = os.path.join(ROOT, "scripts", "mkbm.py")

sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mkbm  # noqa: E402


class Rgb30Test(unittest.TestCase):
    def warnings(self, data):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            mkbm.rgb30_warnings(data, "x.bm")
        return err.getvalue()

    def test_any_screen_is_quiet(self):
        for res in ((640, 360), (480, 270), (360, 360), (720, 720), (256, 256)):
            self.assertEqual(self.warnings(mkbm.pack(b"local a = 1\n", res=res)), "", res)

    def test_targets_of_one_kernel_are_quiet(self):
        for targets in (1, 2, 3):
            data = mkbm.mark_compat(mkbm.pack(b"", res=(360, 360)), targets)
            self.assertEqual(self.warnings(data), "", targets)

    def test_lists_rgb30(self):
        plain = mkbm.pack(b"", res=(360, 360))
        self.assertFalse(mkbm.lists_rgb30(plain))
        self.assertFalse(mkbm.lists_rgb30(mkbm.mark_compat(plain, 1)))
        self.assertTrue(mkbm.lists_rgb30(mkbm.mark_compat(plain, 2)))
        self.assertTrue(mkbm.lists_rgb30(mkbm.mark_compat(plain, 3)))

    def test_mouse_hardware_need_warns(self):
        data = mkbm.mark_compat(mkbm.pack(b"", res=(360, 360)), 3, needs=8)
        self.assertIn("mouse_hw", self.warnings(data))

    def build(self, tmp, out, *flags):
        src = os.path.join(tmp, "m.lua")
        with open(src, "wb") as f:
            f.write(b"local x = math.random(3)\n")
        return subprocess.run(
            [sys.executable, MKBM_PATH, "--lua", src, "--res", "360x360", *flags, "-o", os.path.join(tmp, out)],
            capture_output=True, text=True, cwd=ROOT)

    def test_cli_has_no_system_call_lint(self):
        with tempfile.TemporaryDirectory() as tmp:
            for flags in (("--targets", "rgb30"), ("--targets", "pi,rgb30"), ("--targets", "pi"), ()):
                proc = self.build(tmp, "X.bm", *flags)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertEqual(proc.stderr, "", flags)

    def test_cli_refuses_to_write_a_b16(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = self.build(tmp, "X.b16")
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("not written any more", proc.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, "X.b16")))


if __name__ == "__main__":
    unittest.main()
