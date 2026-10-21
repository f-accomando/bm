import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_log.py")


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        self.f = os.path.join(self.d, "X.md")

    def run_tool(self, lines, append=False):
        cmd = [sys.executable, TOOL, "inbox", "--id", "X", "--file-dir", self.d]
        for ln in lines:
            cmd += ["--line", ln]
        if append:
            cmd.append("--append")
        return subprocess.run(cmd, capture_output=True, text=True)

    def content(self):
        with open(self.f, encoding="utf-8") as f:
            return f.read()

    def test_append_adds_after_existing(self):
        self.run_tool(["a", "b"])
        r = self.run_tool(["c"], append=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.content(), "a\nb\nc\n")

    def test_append_over_limit_leaves_file(self):
        self.run_tool(["a", "b", "c", "d"])
        r = self.run_tool(["e", "f"], append=True)
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.content(), "a\nb\nc\nd\n")

    def test_overwrite_warns_when_nonempty(self):
        self.run_tool(["a"])
        r = self.run_tool(["z"])
        self.assertEqual(r.returncode, 0)
        self.assertIn("warning", r.stderr)
        self.assertIn("X.md", r.stderr)
        self.assertEqual(self.content(), "z\n")

    def test_overwrite_silent_when_new(self):
        r = self.run_tool(["z"])
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stderr, "")


if __name__ == "__main__":
    unittest.main()
