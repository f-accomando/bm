"""Tests for scripts/ledger_repeats.py: runs the script as a subprocess on a temp ledger."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "ledger_repeats.py")
HEADER = "# date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs"


def row(task, agent="general-purpose", tok=100):
    return "2026-10-15|%s|sonnet|%s|sync|-|-|DONE|%d|7|66" % (agent, task, tok)


class RepeatsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "ledger.txt")

    def tearDown(self):
        self.tmp.cleanup()

    def ledger(self, *lines):
        with open(self.path, "w", encoding="utf-8", newline="") as fh:
            fh.write("\n".join((HEADER,) + lines) + "\n")

    def run_(self, *args, path=None):
        return subprocess.run([sys.executable, SCRIPT, "--file", path or self.path] + list(args),
                              capture_output=True, text=True)

    def test_group_of_three(self):
        self.ledger(row("review AGY-1 X"), row("review AGY-2 Y"),
                    row("review SDLOG-FIX (AGY)"), row("translate foo"))
        r = self.run_()
        self.assertEqual(r.returncode, 0)
        out = r.stdout.splitlines()
        self.assertTrue(out[0].startswith("3 x review ID (agenti: general-purpose; token totali 300"), out)
        self.assertNotIn("translate", r.stdout)
        self.assertIn("note it in the ledger notes", out[-1])

    def test_min_two(self):
        self.ledger(row("wo AGY-1"), row("wo AGY-2"), row("solo thing"))
        self.assertIn("no repeated tasks", self.run_().stdout)
        r = self.run_("--min", "2")
        self.assertTrue(r.stdout.startswith("2 x wo ID"), r.stdout)
        self.assertEqual(self.run_("--min", "1").returncode, 2)

    def test_malformed_skipped(self):
        self.ledger(row("review A-1"), row("review A-2"), row("review A-3"), "broken|line")
        r = self.run_()
        self.assertEqual(r.returncode, 0)
        self.assertIn("line 5:", r.stderr)
        self.assertIn("skipped 1", r.stderr)
        self.assertTrue(r.stdout.startswith("3 x review ID"))

    def test_missing_file(self):
        self.assertEqual(self.run_(path=os.path.join(self.tmp.name, "nope.txt")).returncode, 2)

    def test_no_groups(self):
        self.ledger(row("a one"), row("b two"))
        r = self.run_()
        self.assertEqual((r.returncode, r.stdout.strip()), (0, "no repeated tasks"))


if __name__ == "__main__":
    unittest.main()
