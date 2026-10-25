#!/usr/bin/env python3
"""
Unit test for scripts/mkbm.py b16_lint: the sheet of a .b16 has at
most 256 colours, the Lua of a .b16 or of a cartridge that lists the RGB30 warns on the
system calls of B16_LUA_CHECKS, any screen size and any list of kernels is quiet, and
the CLI (--from X.bm --b16) leaves the .bm untouched.
"""
import contextlib
import hashlib
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MKBM_PATH = os.path.join(ROOT, "scripts", "mkbm.py")

sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402


def sheet_of(n):
    """a 1-pixel-high sheet of n distinct opaque colours"""
    rgba = bytes(v for i in range(n) for v in (i % 256, i // 256, 0, 255))
    return (n, 1, rgba)


def lint(lua):
    """the stderr of b16_lint on a cartridge with this Lua"""
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        mkbm.b16_lint(mkbm.pack(lua, res=(360, 360)), "x.b16")
    return err.getvalue()


class B16LintTest(unittest.TestCase):

    def test_sheet_over_256_colours_is_refused(self):
        data = mkbm.pack(b"", sheet=sheet_of(300), res=(360, 360))
        with self.assertRaises(SystemExit) as cm:
            with contextlib.redirect_stderr(io.StringIO()):
                mkbm.b16_lint(data, "x.b16")
        self.assertIn("more than 256 colours", str(cm.exception))

    def test_sheet_of_256_colours_passes(self):
        data = mkbm.pack(b"", sheet=sheet_of(256), res=(360, 360))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            mkbm.b16_lint(data, "x.b16")
        self.assertEqual(err.getvalue(), "")

    def test_lua_system_call_warns_with_line(self):
        out = lint(b"local x = math.random(3)\n")
        self.assertIn("warning: x.b16:1:", out)
        self.assertIn("math.random", out)

    def test_warning_line_after_the_first(self):
        out = lint(b"local a = 1\nlocal b = 2\nlocal c = math.random(3)\n")
        self.assertIn("warning: x.b16:3:", out)

    def test_line_comment_does_not_warn(self):
        self.assertEqual(lint(b"-- math.random\nlocal x = 1\n"), "")

    def test_block_comment_hides_calls_and_keeps_line_numbers(self):
        out = lint(b"--[[ math.random(1)\nls() ]]\nlocal c = time()\n")
        self.assertEqual(out.count("warning"), 1)
        self.assertIn("x.b16:3:", out)
        self.assertIn("time()", out)

    def test_dashes_inside_a_string_do_not_start_a_comment(self):
        out = lint(b'local s = "--" .. tostring(math.random(2))\n')
        self.assertIn("x.b16:1:", out)
        self.assertIn("math.random", out)

    def test_call_inside_a_string_does_not_warn(self):
        self.assertEqual(lint(b'local s = "math.random(1) -- ls("\nlocal t = [[ time() ]]\n'), "")

    def test_one_warning_per_pattern_first_occurrence(self):
        out = lint(b"math.random(1)\nmath.random(2)\nls()\nls()\n")
        self.assertEqual(out.count("math.random"), 1)
        self.assertEqual(out.count("ls()"), 1)
        self.assertIn("x.b16:1:", out)
        self.assertIn("x.b16:3:", out)

    def test_every_lua_section_is_checked(self):
        one = mkbm.pack(b"local a = 1\n", res=(360, 360))
        two = mkbm.pack(b"local b = math.random(1)\n", res=(360, 360))
        secs = bmmesh.cart_sections(one) + bmmesh.cart_sections(two)
        table, bodies, off = b"", b"", 128 + 16 * len(secs)
        for t, body in secs:
            table += struct.pack("<IIII", t, off + len(bodies), len(body), 0)
            bodies += body + bytes((-len(body)) % 4)
        data = bytearray(one[:128]) + table + bodies
        data[17] = len(secs)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            mkbm.b16_lint(bytes(data), "x.b16")
        self.assertIn("math.random", err.getvalue())

    def test_cli_sheet8_with_b16(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "X.bm")
            dst = os.path.join(tmp, "X.b16")
            with open(src, "wb") as f:
                f.write(mkbm.pack(b"local x = 1\n", sheet=sheet_of(3), res=(640, 360)))
            proc = subprocess.run(
                [sys.executable, MKBM_PATH, "--from", src, "--b16", "--sheet8", "-o", dst],
                capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with open(dst, "rb") as f:
                types = [t for t, _ in bmmesh.cart_sections(f.read())]
            self.assertIn(mkbm.SEC_SHEET8, types)
            self.assertNotIn(mkbm.SEC_SHEET, types)

    def test_profile_any_screen_is_quiet(self):
        """bars make any size valid (W4 of docs/design/b16-removal.md): no warning for the size"""
        for res in ((640, 360), (480, 270), (360, 360), (720, 720), (256, 256)):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                mkbm.b16_lint(mkbm.pack(b"local a = 1\n", res=res), "x.b16")
            self.assertEqual(err.getvalue(), "", res)

    def test_profile_targets_of_one_kernel_are_quiet(self):
        for targets in (1, 2, 3):
            data = mkbm.mark_compat(mkbm.pack(b"", res=(360, 360)), targets)
            self.assertEqual(mkbm.b16_profile_warnings(data), [], targets)

    def test_lists_rgb30(self):
        plain = mkbm.pack(b"", res=(360, 360))
        self.assertFalse(mkbm.lists_rgb30(plain))
        self.assertFalse(mkbm.lists_rgb30(mkbm.mark_compat(plain, 1)))
        self.assertTrue(mkbm.lists_rgb30(mkbm.mark_compat(plain, 2)))
        self.assertTrue(mkbm.lists_rgb30(mkbm.mark_compat(plain, 3)))

    def test_profile_both_targets_are_quiet(self):
        data = mkbm.mark_compat(mkbm.pack(b"", res=(360, 360)), 3, input_=1, aspect=2)
        self.assertEqual(mkbm.b16_profile_warnings(data), [])

    def test_profile_mouse_hardware_need_warns(self):
        data = mkbm.mark_compat(mkbm.pack(b"", res=(360, 360)), 3, needs=8)
        self.assertIn("mouse_hw", mkbm.b16_profile_warnings(data)[0])

    def build(self, tmp, out, *flags, lua=b"local x = math.random(3)\n"):
        src = os.path.join(tmp, "m.lua")
        with open(src, "wb") as f:
            f.write(lua)
        return subprocess.run(
            [sys.executable, MKBM_PATH, "--lua", src, "--res", "360x360", *flags, "-o", os.path.join(tmp, out)],
            capture_output=True, text=True, cwd=ROOT)

    def test_cli_lint_runs_for_a_bm_that_lists_the_rgb30(self):
        with tempfile.TemporaryDirectory() as tmp:
            for flags in (("--targets", "rgb30"), ("--targets", "pi,rgb30")):
                proc = self.build(tmp, "X.bm", *flags)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertIn("math.random", proc.stderr, flags)

    def test_cli_lint_is_quiet_for_a_bm_of_the_pi_or_with_no_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            for flags in (("--targets", "pi"), ()):
                proc = self.build(tmp, "X.bm", *flags)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertNotIn("warning", proc.stderr, flags)

    def test_cli_b16_output_still_lints_and_does_not_warn_for_the_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = self.build(tmp, "X.b16", "--targets", "pi", lua=b"local x = 1\n")
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(proc.stderr, "")
            proc = self.build(tmp, "Y.b16")
            self.assertIn("math.random", proc.stderr)

    def test_bm_with_a_true_colour_sheet_is_not_refused(self):
        data = mkbm.mark_compat(mkbm.pack(b"", sheet=sheet_of(300), res=(360, 360)), 3)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            mkbm.b16_lint(data, "x.bm")
        self.assertEqual(err.getvalue(), "")

    def test_cli_leaves_bm_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "X.bm")
            dst = os.path.join(tmp, "X.b16")
            with open(src, "wb") as f:
                f.write(mkbm.pack(b"local x = math.random(3)\n", res=(640, 360)))
            with open(src, "rb") as f:
                before = hashlib.sha256(f.read()).hexdigest()
            proc = subprocess.run(
                [sys.executable, MKBM_PATH, "--from", src, "--b16", "-o", dst],
                capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with open(src, "rb") as f:
                after = hashlib.sha256(f.read()).hexdigest()
            self.assertEqual(before, after)
            self.assertTrue(os.path.exists(dst))


if __name__ == "__main__":
    unittest.main()
