"""Tests for scripts/agent_audit.py: the script runs as a subprocess on fake ledgers."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "agent_audit.py")
HEADER = "# date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs"


def row(agent="bm-coder", gate="1", outcome="DONE", tokens=100, necessity="-"):
    return f"2026-10-14|{agent}|claude-haiku-5-5|task-x|tools|{gate}|{necessity}|{outcome}|{tokens}|3|12"


class AgentAuditTest(unittest.TestCase):
    def run_audit(self, lines, *extra):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "ledger.txt")
            with open(path, "w", encoding="utf8", newline="\n") as fh:
                fh.write("\n".join([HEADER] + lines) + "\n")
            return subprocess.run(
                [sys.executable, SCRIPT, "--ledger", path, *extra],
                capture_output=True, text=True)

    def test_valid_ledger_two_agents(self):
        res = self.run_audit([row("bm-coder"), row("bm-planner", outcome="BLOCKED")])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("bm-coder", res.stdout)
        self.assertIn("bm-planner", res.stdout)

    def test_wrong_field_count_reports_line(self):
        bad = "2026-10-14|bm-coder|claude-haiku-5-5|task-x|tools|1|-|DONE|100|3"  # 10 fields
        res = self.run_audit([row("bm-coder"), bad])
        self.assertEqual(res.returncode, 1)
        self.assertIn("line 3", res.stderr)

    def test_invalid_outcome_exit_1(self):
        res = self.run_audit([row("bm-coder", outcome="OK")])
        self.assertEqual(res.returncode, 1)

    def test_warning_when_gate_low_fails(self):
        lines = [row(gate="1", outcome="FAILED x2")] * 2 + [row(gate="1", outcome="DONE")] * 3
        res = self.run_audit(lines)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("gate troppo ottimista", res.stdout)

    def test_no_warning_at_exactly_20_percent(self):
        lines = [row(gate="2", outcome="BLOCKED")] + [row(gate="2", outcome="DONE")] * 4
        res = self.run_audit(lines)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertNotIn("gate troppo ottimista", res.stdout)

    def test_no_warning_with_fewer_than_five_low_gate_runs(self):
        lines = [row(gate="1", outcome="FAILED x2")] * 4
        res = self.run_audit(lines)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertNotIn("gate troppo ottimista", res.stdout)

    def test_agent_filter(self):
        res = self.run_audit([row("bm-coder"), row("bm-planner")], "--agent", "bm-coder")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("bm-coder", res.stdout)
        self.assertNotIn("bm-planner", res.stdout)

    def test_header_only_no_rows(self):
        res = self.run_audit([])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(res.stdout.strip(), "no rows")


if __name__ == "__main__":
    unittest.main()
