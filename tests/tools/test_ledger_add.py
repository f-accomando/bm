"""Tests for scripts/ledger_add.py: runs the script as a subprocess on a temp ledger."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ADD = os.path.join(ROOT, "scripts", "ledger_add.py")
AUDIT = os.path.join(ROOT, "scripts", "agent_audit.py")
HEADER = "# date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs"
BASE = ["--agent", "general-purpose", "--model", "sonnet", "--task", "review X",
        "--area", "sync", "--tokens", "100", "--tools", "7", "--secs", "66",
        "--date", "2026-10-15"]
LINE = "2026-10-15|general-purpose|sonnet|review X|sync|-|-|DONE|100|7|66"


def opt(args, key, value):
    out = list(args)
    out[out.index(key) + 1] = value
    return out


class LedgerAddTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "ledger.txt")
        self.write(HEADER + "\n")

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, text):
        with open(self.path, "wb") as fh:
            fh.write(text.encode())

    def read(self):
        with open(self.path, "rb") as fh:
            return fh.read().decode()

    def add(self, args=BASE, *extra):
        return subprocess.run([sys.executable, ADD, *args, "--file", self.path, *extra],
                              capture_output=True, text=True)

    def audit(self):
        return subprocess.run([sys.executable, AUDIT, "--ledger", self.path],
                              capture_output=True, text=True)

    def test_valid_row_added_and_audited(self):
        res = self.add()
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(res.stdout.strip(), LINE)
        self.assertEqual(self.read(), HEADER + "\n" + LINE + "\n")
        self.assertEqual(len(LINE.split("|")), 11)
        aud = self.audit()
        self.assertEqual(aud.returncode, 0, aud.stderr)
        self.assertEqual(aud.stderr, "")

    def test_unknown_agent(self):
        res = self.add(opt(BASE, "--agent", "generic"))
        self.assertEqual(res.returncode, 2)
        self.assertIn("bm-coder", res.stderr)
        self.assertEqual(self.read(), HEADER + "\n")

    def test_bad_outcome(self):
        res = self.add(BASE, "--outcome", "MAYBE")
        self.assertEqual(res.returncode, 2)
        self.assertEqual(self.read(), HEADER + "\n")

    def test_tokens_not_numeric(self):
        res = self.add(opt(BASE, "--tokens", "12x"))
        self.assertEqual(res.returncode, 2)
        self.assertEqual(self.read(), HEADER + "\n")

    def test_pipe_in_task(self):
        res = self.add(opt(BASE, "--task", "a|b"))
        self.assertEqual(res.returncode, 2)
        self.assertEqual(self.read(), HEADER + "\n")

    def test_duplicate_skipped_then_forced(self):
        self.assertEqual(self.add().returncode, 0)
        res = self.add()
        self.assertEqual(res.returncode, 0)
        self.assertIn("identical", res.stderr)
        self.assertEqual(self.read().count(LINE), 1)
        self.assertEqual(self.add(BASE, "--force").returncode, 0)
        self.assertEqual(self.read().count(LINE), 2)

    def test_missing_final_newline(self):
        self.write(HEADER)
        self.assertEqual(self.add().returncode, 0)
        self.assertEqual(self.read(), HEADER + "\n" + LINE + "\n")

    def test_crlf_kept(self):
        self.write(HEADER + "\r\n")
        self.assertEqual(self.add().returncode, 0)
        self.assertEqual(self.read(), HEADER + "\r\n" + LINE + "\r\n")

    def test_no_write(self):
        res = self.add(BASE, "--no-write")
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout.strip(), LINE)
        self.assertEqual(self.read(), HEADER + "\n")


if __name__ == "__main__":
    unittest.main()
