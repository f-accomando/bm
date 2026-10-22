"""Tests for scripts/agent_audit.py: the script runs as a subprocess on fake ledgers."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "agent_audit.py")
HEADER = "# date|agent|model|task|area|gate|necessity|outcome|tokens|tools|secs"


def row(agent="bm-coder", gate="1", outcome="DONE", tokens=100, necessity="-", date="2026-10-14", model="claude-haiku-5-5", area="tools"):
    return f"{date}|{agent}|{model}|task-x|{area}|{gate}|{necessity}|{outcome}|{tokens}|3|12"


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

    def test_model_and_area_tables(self):
        res = self.run_audit([row("bm-coder", model="claude-haiku-5-5", area="tools")])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("claude-haiku-5-5", res.stdout)
        self.assertIn("tools", res.stdout)
        self.assertIn("model", res.stdout)
        self.assertIn("area", res.stdout)

    def test_area_filter(self):
        res = self.run_audit(
            [row("bm-coder", area="tools"), row("bm-planner", area="platforms")],
            "--area", "tools"
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("bm-coder", res.stdout)
        self.assertNotIn("bm-planner", res.stdout)

    def test_since_filter(self):
        res = self.run_audit(
            [row("bm-coder", date="2026-10-10"), row("bm-planner", date="2026-10-15")],
            "--since", "2026-10-12"
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertNotIn("bm-coder", res.stdout)
        self.assertIn("bm-planner", res.stdout)

    def test_bad_since_exit_2(self):
        res = self.run_audit([row("bm-coder")], "--since", "invalid-date")
        self.assertEqual(res.returncode, 2)
        self.assertIn("bad --since", res.stderr)

    def test_save_and_compare_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            ledger_path = os.path.join(tmp, "ledger.txt")
            base_path = os.path.join(tmp, "baseline.txt")
            with open(ledger_path, "w", encoding="utf8", newline="\n") as fh:
                fh.write("\n".join([HEADER, row("bm-coder", tokens=1000, date="2026-10-14")]) + "\n")

            # Save baseline
            res_save = subprocess.run(
                [sys.executable, SCRIPT, "--ledger", ledger_path, "--save-baseline", base_path],
                capture_output=True, text=True
            )
            self.assertEqual(res_save.returncode, 0, res_save.stderr)
            self.assertTrue(os.path.exists(base_path))

            # Add more rows to ledger
            with open(ledger_path, "a", encoding="utf8", newline="\n") as fh:
                fh.write(row("bm-coder", tokens=2000, date="2026-10-15") + "\n")
                fh.write(row("bm-planner", tokens=500, date="2026-10-15") + "\n")

            # Compare with baseline
            res_comp = subprocess.run(
                [sys.executable, SCRIPT, "--ledger", ledger_path, "--baseline", base_path],
                capture_output=True, text=True
            )
            self.assertEqual(res_comp.returncode, 0, res_comp.stderr)
            self.assertIn("vs baseline", res_comp.stdout)
            self.assertIn("bm-coder", res_comp.stdout)
            self.assertIn("bm-planner", res_comp.stdout)
            self.assertIn("new", res_comp.stdout)

    def test_bad_baseline_exit_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad_base = os.path.join(tmp, "bad_baseline.txt")
            with open(bad_base, "w", encoding="utf8") as fh:
                fh.write("invalid|line\n")
            res = self.run_audit([row("bm-coder")], "--baseline", bad_base)
            self.assertEqual(res.returncode, 2)
            self.assertIn("baseline line 1", res.stderr)


if __name__ == "__main__":
    unittest.main()
