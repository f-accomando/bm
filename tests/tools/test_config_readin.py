"""Tests for scripts/config_readin.py."""

import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPTS = os.path.join(ROOT, "scripts")
if SCRIPTS not in sys.path:
    sys.path.insert(0, SCRIPTS)

import config_readin as mod


class ConfigReadinTest(unittest.TestCase):
    def setUp(self):
        self.orig_root = mod.ROOT
        self.tmp = tempfile.TemporaryDirectory()
        mod.ROOT = self.tmp.name

    def tearDown(self):
        mod.ROOT = self.orig_root
        self.tmp.cleanup()

    def _setup_files(self, toml_content, c_content):
        spec_dir = os.path.join(self.tmp.name, "docs", "spec")
        os.makedirs(spec_dir, exist_ok=True)
        with open(os.path.join(spec_dir, "config-keys.toml"), "w", encoding="utf-8") as f:
            f.write(toml_content)

        src_dir = os.path.join(self.tmp.name, "src")
        os.makedirs(src_dir, exist_ok=True)
        with open(os.path.join(src_dir, "x.c"), "w", encoding="utf-8") as f:
            f.write(c_content)

    def test_ok(self):
        toml = """[[key]]
name = "foo"
read_in = "src/x.c:2"
"""
        c = "line 1\n\"foo\"\nline 3\n"
        self._setup_files(toml, c)
        lines, stale = mod.check()
        self.assertEqual(stale, 0)
        self.assertEqual(lines, [])

    def test_stale(self):
        toml = """[[key]]
name = "foo"
read_in = "src/x.c:2"
"""
        c = "line 1\nother\n\"foo\"\nother\nconfig_get(\"foo\")\n"
        self._setup_files(toml, c)
        lines, stale = mod.check()
        self.assertEqual(stale, 1)
        self.assertEqual(lines, ["STALE foo: src/x.c:2 -> 5"])

    def test_unchecked(self):
        toml = """[[key]]
name = "bar"
read_in = "src/x.c:2"
"""
        c = "line 1\nother\nline 3\n"
        self._setup_files(toml, c)
        lines, stale = mod.check()
        self.assertEqual(stale, 0)
        self.assertEqual(lines, ["UNCHECKED bar: src/x.c:2"])


if __name__ == "__main__":
    unittest.main()
