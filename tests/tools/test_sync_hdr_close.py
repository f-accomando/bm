import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_hdr.py")


def git(args, cwd):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t"] + args, cwd=cwd, check=True,
                          capture_output=True, text=True).stdout.strip()


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        git(["init", "-q"], self.d)
        for name, n in (("a.c", 3), ("b.md", 2), ("c.json", 4), ("Makefile", 1)):
            with open(os.path.join(self.d, name), "w") as f:
                f.write("x\n" * n)
        with open(os.path.join(self.d, "bin.dat"), "wb") as f:
            f.write(b"\0\1\2")
        git(["add", "."], self.d)
        git(["commit", "-qm", "c"], self.d)
        self.sha = git(["rev-parse", "HEAD"], self.d)
        self.ledger = os.path.join(self.d, "L.jsonl")

    def run_tool(self, extra):
        return subprocess.run([sys.executable, TOOL, "OP", "--repo", self.d, "--ledger", self.ledger] + extra,
                              capture_output=True, text=True)

    def rows(self):
        with open(self.ledger, encoding="utf-8") as f:
            return [json.loads(ln) for ln in f.read().splitlines()]

    def test_commit_stats_and_chat_line(self):
        r = self.run_tool(["--close", "WO-1", "--commit", self.sha, "--tokens", "100", "--min", "7", "--branch", "b"])
        self.assertEqual(r.returncode, 0, r.stderr)
        row = self.rows()[0]
        self.assertEqual((row["files"], row["add"], row["del"]), (5, 10, 0))
        self.assertEqual(row["lines"], {"code": 4, "docs": 2, "data": 4})
        self.assertEqual((row["schema"], row["wo"], row["branch"], row["tokens"], row["min"]), (1, "WO-1", "b", 100, 7))
        self.assertNotIn("read", row)
        self.assertRegex(row["iso"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d[+-]\d\d:\d\d$")
        self.assertEqual(r.stdout.strip(), "done WO WO-1 FREE: 5 files +10 -0, 100 tokens, 7 min")

    def test_waiting_append_and_manual_override(self):
        self.run_tool(["--close", "WO-1", "--commit", self.sha, "--tokens", "1", "--min", "1"])
        r = self.run_tool(["--close", "WO-2", "--files", "2", "--add", "5", "--del", "1", "--tokens", "9", "--min", "3",
                           "--read", "50", "--lines", "code=6,docs=0,data=0", "--state", "WAITING", "--waiting-on", "SPV"])
        self.assertEqual(r.stdout.strip(), "done WO WO-2 waiting on SPV: 2 files +5 -1, 9 tokens, 3 min")
        rows = self.rows()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1]["read"], 50)
        self.assertEqual(rows[1]["lines"]["code"], 6)

    def test_errors(self):
        r = self.run_tool(["--close", "WO-1", "--commit", self.sha, "--min", "1"])
        self.assertEqual(r.returncode, 2)
        r = self.run_tool(["--close", "WO-1", "--commit", "deadbeef", "--tokens", "1", "--min", "1"])
        self.assertEqual(r.returncode, 2)
        self.assertFalse(os.path.exists(self.ledger))

    def test_status_block_untouched(self):
        hdr = os.path.join(self.d, "OP.md")
        self.run_tool(["--close", "WO-1", "--commit", self.sha, "--tokens", "1", "--min", "1"])
        self.assertFalse(os.path.exists(hdr))

    def test_closed_line(self):
        hdr = os.path.join(self.d, "OP.md")
        with open(hdr, "w", encoding="utf-8", newline="") as f:
            f.write("---\nschema: 1\nid: OP\nstate: WORKING\ntask: t\nupdated: x\n---\n# OP\n\n## Progressi\n\n- a\n")
        base = ["OP", "--file", hdr, "--repo", self.d]
        r = subprocess.run([sys.executable, TOOL] + base + ["--closed", "nessuno"], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)  # --branch is required
        for _ in range(2):  # second run replaces the line, no duplicate
            r = subprocess.run([sys.executable, TOOL] + base + ["--closed", "nessuno", "--branch", "claude/x"],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
        with open(hdr, encoding="utf-8") as f:
            txt = f.read()
        short = git(["rev-parse", "--short", "HEAD"], self.d)
        self.assertIn("state: DONE", txt)
        self.assertEqual(txt.count("CLOSED @"), 1)
        self.assertRegex(txt, r"## Chiusura\n\nCLOSED @ claude/x@%s \u00b7 tree clean \u00b7 aperti: nessuno \u00b7 \d{4}-\d\d-\d\dT\d\d:\d\d" % short)
        self.assertLess(txt.index("## Chiusura"), txt.index("## Progressi"))


if __name__ == "__main__":
    unittest.main()
