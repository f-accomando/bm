import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_log.py")
HEAD = "| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\r\n|---|---|---|---|---|\r\n"


def run(args, cwd):
    return subprocess.run([sys.executable, TOOL] + args, cwd=cwd, capture_output=True, text=True, encoding="utf-8")


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        self.f = os.path.join(self.d, "SYNC.md")
        with open(self.f, "wb") as fh:
            fh.write((HEAD + "| 2026-10-15T20:00+02:00 | SPV | `b` | `RULE` | x |\r\n").encode())

    def tearDown(self):
        self._td.cleanup()

    def test_log_quiet_prints_one_short_line(self):
        r = run(["log", "--who", "SPV", "--branch", "b", "--action", "RULE", "--text", "ciao",
                 "--file", self.f, "--quiet"], self.d)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, r"^log: riga scritta, \d+ caratteri\n$")
        self.assertEqual(r.stderr, "")
        with open(self.f, "rb") as fh:
            self.assertIn(b"| SPV | `b` | `RULE` | ciao |", fh.read())

    def test_log_without_quiet_still_prints_row(self):
        r = run(["log", "--who", "SPV", "--branch", "b", "--action", "RULE", "--text", "ciao",
                 "--file", self.f], self.d)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(r.stdout.startswith("| "))

    def test_log_quiet_keeps_error_visible(self):
        r = run(["log", "--who", "SPV", "--branch", "b", "--action", "RULE", "--text", "a" * 601,
                 "--file", self.f, "--quiet"], self.d)
        self.assertEqual(r.returncode, 2)
        self.assertIn("max 600", r.stderr)

    def test_inbox_is_quiet_by_default(self):
        ib = os.path.join(self.d, "inbox")
        r = run(["inbox", "--id", "AGX", "--file-dir", ib, "--line", "a", "--line", "b"], self.d)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "inbox AGX: 2 righe\n")
        r = run(["inbox", "--id", "AGX", "--file-dir", ib, "--line", "c", "--append"], self.d)
        self.assertEqual(r.stdout, "inbox AGX: 3 righe\n")

    def test_inbox_verbose_prints_text(self):
        ib = os.path.join(self.d, "inbox")
        r = run(["inbox", "--id", "AGX", "--file-dir", ib, "--line", "a", "--verbose"], self.d)
        self.assertEqual(r.stdout, "a\n")

    def test_inbox_quiet_keeps_warning_on_stderr(self):
        ib = os.path.join(self.d, "inbox")
        run(["inbox", "--id", "AGX", "--file-dir", ib, "--line", "a"], self.d)
        r = run(["inbox", "--id", "AGX", "--file-dir", ib, "--line", "z"], self.d)
        self.assertEqual(r.returncode, 0)
        self.assertIn("warning", r.stderr)
        self.assertEqual(r.stdout, "inbox AGX: 1 righe\n")

    def test_inbox_too_many_lines_error(self):
        ib = os.path.join(self.d, "inbox")
        r = run(["inbox", "--id", "AGX", "--file-dir", ib] + sum([["--line", "l%d" % i] for i in range(6)], []), self.d)
        self.assertEqual(r.returncode, 2)
        self.assertIn("max 5", r.stderr)


if __name__ == "__main__":
    unittest.main()
