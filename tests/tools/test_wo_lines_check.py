import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "wo_lines_check.py")


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        os.makedirs(os.path.join(self.d, "src"))
        with open(os.path.join(self.d, "src", "a.c"), "w") as f:
            f.write("x\n" * 10)
        self.wo = os.path.join(self.d, "WO.md")

    def run_tool(self, body):
        with open(self.wo, "w", encoding="utf-8") as f:
            f.write(body)
        return subprocess.run([sys.executable, TOOL, self.wo, "--repo", self.d], capture_output=True, text=True)

    def test_ok(self):
        r = self.run_tool("see `src/a.c:10` and `src/a.c:2-9` and plain src/zz.c:99\n")
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("2 citation(s), 0 failure(s)", r.stdout)

    def test_line_too_large(self):
        r = self.run_tool("`src/a.c:11`\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("src/a.c:11", r.stdout)

    def test_range_end_too_large(self):
        self.assertEqual(self.run_tool("`src/a.c:5-12`\n").returncode, 1)

    def test_missing_file(self):
        r = self.run_tool("`src/none.c:1`\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("file not found", r.stdout)


if __name__ == "__main__":
    unittest.main()
