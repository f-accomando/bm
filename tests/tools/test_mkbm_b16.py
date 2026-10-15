#!/usr/bin/env python3
"""
Unit test for scripts/mkbm.py b16_lint (M42): the sheet of a .b16 has at
most 256 colours, its Lua warns on the system calls of B16_LUA_CHECKS, and
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
