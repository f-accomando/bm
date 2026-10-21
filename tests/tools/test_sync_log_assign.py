import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_log.py")
ROW = re.compile(r"^\| \d{4}-\d\d-\d\dT\d\d:\d\d[+-]\d\d:\d\d \| SPV \| `b/x` \| `ASSIGNED` \| NET: W1, W2 \|$")


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        self.log = os.path.join(self.d, "SYNC.md")
        self.ib = os.path.join(self.d, "NET.md")
        with open(self.log, "w", encoding="utf-8", newline="") as f:
            f.write("| head |\n")

    def run_tool(self, wos, branch="b/x"):
        cmd = [sys.executable, TOOL, "assign", "--id", "NET", "--branch", branch,
               "--file", self.log, "--file-dir", self.d]
        for w in wos:
            cmd += ["--wo", w]
        return subprocess.run(cmd, capture_output=True, text=True)

    def read(self, p):
        with open(p, "rb") as f:
            return f.read()

    def test_row_and_two_inbox_lines(self):
        r = self.run_tool(["W1", "W2"])
        self.assertEqual(r.returncode, 0, r.stderr)
        last = self.read(self.log).decode().splitlines()[-1]
        self.assertRegex(last, ROW)
        lines = self.read(self.ib).decode().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertTrue(lines[0].startswith("ASSIGNED W1 @ b/x "))
        self.assertTrue(lines[1].startswith("ASSIGNED W2 @ b/x "))

    def test_over_five_lines_leaves_both_files(self):
        with open(self.ib, "w", encoding="utf-8", newline="") as f:
            f.write("a\nb\nc\nd\n")
        before = (self.read(self.log), self.read(self.ib))
        r = self.run_tool(["W1", "W2"])
        self.assertEqual(r.returncode, 2)
        self.assertEqual((self.read(self.log), self.read(self.ib)), before)

    def test_long_row_exits_2(self):
        before = self.read(self.log)
        r = self.run_tool(["W1"], branch="b" * 580)
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.read(self.log), before)
        self.assertFalse(os.path.exists(self.ib))

    def test_bad_wo_exits_2(self):
        before = self.read(self.log)
        r = self.run_tool(["W|1"])
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.read(self.log), before)
        self.assertFalse(os.path.exists(self.ib))


if __name__ == "__main__":
    unittest.main()
