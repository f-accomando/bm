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

    def test_runner_and_harness_agents_accepted(self):
        for name in ("bm-runner", "claude", "Explore", "Plan"):
            res = self.add(opt(BASE, "--agent", name), "--no-write")
            self.assertEqual(res.returncode, 0, name + res.stderr)

    def test_counts_default_to_unknown_not_zero(self):
        args = list(BASE)
        for key in ("--tokens", "--tools", "--secs"):
            i = args.index(key)
            del args[i:i + 2]
        res = self.add(args, "--no-write")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(res.stdout.strip(), "2026-10-15|general-purpose|sonnet|review X|sync|-|-|DONE|-|-|-")

    def test_unknown_counts_written_and_audited(self):
        res = self.add(opt(opt(BASE, "--tokens", "-"), "--secs", "-"))
        self.assertEqual(res.returncode, 0, res.stderr)
        aud = self.audit()
        self.assertEqual(aud.returncode, 0, aud.stderr)
        self.assertIn("general-purpose", aud.stdout)

    def test_failed_alias(self):
        res = self.add(BASE, "--outcome", "FAILED", "--no-write")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("|FAILED x2|", res.stdout)

    def report(self, text):
        rp = os.path.join(self.tmp.name, "report.txt")
        with open(rp, "w", encoding="utf-8") as fh:
            fh.write(text)
        return rp

    REPORT = ("DONE\nrow:\n`python scripts/ledger_add.py --agent bm-runner --model sonnet "
              "--task \"fix a b\" --area tools --outcome DONE --tokens 0 --tools 3 --secs 9 --no-write`\n")

    def test_from_report_with_measured_counts_override(self):
        rp = self.report(self.REPORT)
        res = subprocess.run([sys.executable, ADD, "--from-report", rp, "--tokens", "5000",
                              "--tools", "-", "--date", "2026-10-16", "--file", self.path],
                             capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        want = "2026-10-16|bm-runner|sonnet|fix a b|tools|-|-|DONE|5000|-|9"
        self.assertEqual(res.stdout.strip(), want)
        self.assertEqual(self.read(), HEADER + "\n" + want + "\n")

    def test_from_report_stdin(self):
        res = subprocess.run([sys.executable, ADD, "--from-report", "-", "--no-write",
                              "--date", "2026-10-16"], input=self.REPORT,
                             capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertTrue(res.stdout.startswith("2026-10-16|bm-runner|"))

    def test_from_report_without_command(self):
        res = subprocess.run([sys.executable, ADD, "--from-report", self.report("DONE\n"),
                              "--file", self.path], capture_output=True, text=True)
        self.assertEqual(res.returncode, 2)
        self.assertIn("no ledger_add.py", res.stderr)
        self.assertEqual(self.read(), HEADER + "\n")

    def test_missing_required_field(self):
        res = subprocess.run([sys.executable, ADD, "--agent", "bm-coder", "--file", self.path],
                             capture_output=True, text=True)
        self.assertEqual(res.returncode, 2)
        self.assertIn("--model is required", res.stderr)


if __name__ == "__main__":
    unittest.main()
