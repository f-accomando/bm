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
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MKBM_PATH = os.path.join(ROOT, "scripts", "mkbm.py")

sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mkbm  # noqa: E402


def sheet_of(n):
    """a 1-pixel-high sheet of n distinct opaque colours"""
    rgba = bytes(v for i in range(n) for v in (i % 256, i // 256, 0, 255))
    return (n, 1, rgba)


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
        data = mkbm.pack(b"local x = math.random(3)\n", res=(360, 360))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            mkbm.b16_lint(data, "x.b16")
        out = err.getvalue()
        self.assertIn("warning: x.b16:1:", out)
        self.assertIn("math.random", out)

    def test_lua_comment_does_not_warn(self):
        data = mkbm.pack(b"-- math.random\nlocal x = 1\n", res=(360, 360))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            mkbm.b16_lint(data, "x.b16")
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
