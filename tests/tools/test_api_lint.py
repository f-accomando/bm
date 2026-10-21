"""Tests for scripts/api_lint.py."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "api_lint.py")


class ApiLintTest(unittest.TestCase):
    def test_fixture_lint(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_lua = os.path.join(tmp, "test.lua")
            content = (
                "local function helper(x)\n"
                "    return x + 1\n"
                "end\n"
                "local msg = \"call foo(1, 2) in string\"\n"
                "helper(10)\n"
                "cls(7)\n"
                "obj:method(42)\n"
                "unknown_func(123)\n"
            )
            with open(test_lua, "w", encoding="utf-8") as f:
                f.write(content)

            res = subprocess.run(
                [sys.executable, SCRIPT, test_lua],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("unknown API name 'unknown_func'", res.stdout)
            self.assertIn(":8:", res.stdout)
            self.assertNotIn("helper", res.stdout)
            self.assertNotIn("cls", res.stdout)
            self.assertNotIn("method", res.stdout)
            self.assertNotIn("foo", res.stdout)

    def test_clean_file_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_lua = os.path.join(tmp, "clean.lua")
            content = (
                "local function my_fun()\n"
                "    print('hello')\n"
                "end\n"
                "my_fun()\n"
                "cls()\n"
            )
            with open(test_lua, "w", encoding="utf-8") as f:
                f.write(content)

            res = subprocess.run(
                [sys.executable, SCRIPT, test_lua],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, res.stdout)

    def test_loadfile_dofile_known(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_lua = os.path.join(tmp, "std.lua")
            with open(test_lua, "w", encoding="utf-8") as f:
                f.write("local f = loadfile('a.lua')\ndofile('b.lua')\n")
            res = subprocess.run([sys.executable, SCRIPT, test_lua], cwd=ROOT,
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stdout)

    def test_markdown_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_md = os.path.join(tmp, "guide.md")
            content = (
                "# Guide\n"
                "Some prose mentioning bad_call(1) in text.\n"
                "```lua\n"
                "cls(0)\n"
                "non_existent_api(5)\n"
                "```\n"
            )
            with open(test_md, "w", encoding="utf-8") as f:
                f.write(content)

            res = subprocess.run(
                [sys.executable, SCRIPT, test_md],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("unknown API name 'non_existent_api'", res.stdout)
            self.assertIn(":5:", res.stdout)
            self.assertNotIn("bad_call", res.stdout)


if __name__ == "__main__":
    unittest.main()
