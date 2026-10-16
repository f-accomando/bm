"""Tests for scripts/consistency.py beta: subprocess tests with temporary roots."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "consistency.py")

MINIMAL_PLATFORMS = """
[[capability]]
id = "square-panel"
what = "square screen"
"""


class ConsistencyBetaTest(unittest.TestCase):
    def run_beta(self, divergences_toml, platforms_toml=MINIMAL_PLATFORMS, beta_toml=None):
        with tempfile.TemporaryDirectory() as tmp:
            spec_dir = os.path.join(tmp, "docs", "spec")
            os.makedirs(spec_dir, exist_ok=True)
            with open(os.path.join(spec_dir, "divergences.toml"), "w", encoding="utf-8") as f:
                f.write(divergences_toml)
            if platforms_toml is not None:
                with open(os.path.join(spec_dir, "platforms.toml"), "w", encoding="utf-8") as f:
                    f.write(platforms_toml)
            if beta_toml is not None:
                with open(os.path.join(spec_dir, "beta.toml"), "w", encoding="utf-8") as f:
                    f.write(beta_toml)
            return subprocess.run(
                [sys.executable, SCRIPT, "beta", "--root", tmp],
                capture_output=True, text=True
            )

    def test_all_resolved_or_hardware_valid(self):
        divs = """
[[divergence]]
id = "D01"
status = "resolved"
cause = "history"
area = "input"
action = "fix"

[[divergence]]
id = "D02"
status = "open"
cause = "hardware"
capability = "square-panel"
area = "screen"
action = "none"
"""
        beta = 'scope = ["D01", "D02"]\n'
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("resolved     D01", res.stdout)
        self.assertIn("accepted     D02 (hardware: square-panel)", res.stdout)
        self.assertIn("beta: 0 blockers, 1 accepted, 1 resolved, 0 errors", res.stdout)

    def test_history_open_blocks(self):
        divs = """
[[divergence]]
id = "D01"
status = "open"
cause = "history"
area = "input"
action = "forward quit"
"""
        beta = 'scope = ["D01"]\n'
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("BLOCKS BETA  D01 (history, open): input: forward quit", res.stdout)
        self.assertIn("beta: 1 blockers, 0 accepted, 0 resolved, 0 errors", res.stdout)

    def test_untested_blocks(self):
        divs = """
[[divergence]]
id = "D01"
status = "untested"
cause = "history"
area = "input"
action = "try it"
"""
        beta = 'scope = ["D01"]\n'
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("BLOCKS BETA  D01 (history, untested): input: try it", res.stdout)

    def test_suspended_blocks(self):
        divs = """
[[divergence]]
id = "D01"
status = "suspended"
cause = "unknown"
area = "pointer"
action = "hide rows"
"""
        beta = 'scope = ["D01"]\n'
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("BLOCKS BETA  D01 (unknown, suspended): pointer: hide rows", res.stdout)

    def test_suspended_with_accept_passes(self):
        divs = """
[[divergence]]
id = "D01"
status = "suspended"
cause = "unknown"
area = "pointer"
action = "hide rows"
"""
        beta = """
scope = ["D01"]

[[accept]]
id = "D01"
because = "hardware lacks mouse"
by = "SPV"
date = "2026-10-16"
"""
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("accepted     D01 (in writing, SPV 2026-10-16): hardware lacks mouse", res.stdout)
        self.assertIn("beta: 0 blockers, 1 accepted, 0 resolved, 0 errors", res.stdout)

    def test_hardware_missing_capability_blocks(self):
        divs = """
[[divergence]]
id = "D01"
status = "open"
cause = "hardware"
capability = "missing-cap"
area = "screen"
action = "none"
"""
        beta = 'scope = ["D01"]\n'
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("BLOCKS BETA  D01 (hardware, open): screen: no capability of platforms.toml", res.stdout)

    def test_scope_id_missing_from_divergences_error(self):
        divs = """
[[divergence]]
id = "D01"
status = "resolved"
cause = "history"
area = "input"
action = "none"
"""
        beta = 'scope = ["D01", "D99"]\n'
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("ERROR  D99: not in docs/spec/divergences.toml", res.stdout)
        self.assertIn("beta: 0 blockers, 0 accepted, 1 resolved, 1 errors", res.stdout)

    def test_accept_outside_scope_error(self):
        divs = """
[[divergence]]
id = "D01"
status = "resolved"
cause = "history"
area = "input"
action = "none"

[[divergence]]
id = "D99"
status = "open"
cause = "history"
area = "other"
action = "none"
"""
        beta = """
scope = ["D01"]

[[accept]]
id = "D99"
because = "not in scope"
by = "SPV"
date = "2026-10-16"
"""
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("ERROR  accept D99: not in scope", res.stdout)

    def test_accept_missing_because_error(self):
        divs = """
[[divergence]]
id = "D01"
status = "open"
cause = "history"
area = "input"
action = "none"
"""
        beta = """
scope = ["D01"]

[[accept]]
id = "D01"
by = "SPV"
date = "2026-10-16"
"""
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("ERROR  accept D01: because missing or invalid", res.stdout)

    def test_accept_on_resolved_stale(self):
        divs = """
[[divergence]]
id = "D01"
status = "resolved"
cause = "history"
area = "input"
action = "none"
"""
        beta = """
scope = ["D01"]

[[accept]]
id = "D01"
because = "already solved"
by = "SPV"
date = "2026-10-16"
"""
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("STALE  accept D01: resolved, remove it", res.stdout)

    def test_duplicate_accept_error(self):
        divs = """
[[divergence]]
id = "D01"
status = "open"
cause = "history"
area = "input"
action = "none"
"""
        beta = """
scope = ["D01"]

[[accept]]
id = "D01"
because = "first reason"
by = "SPV"
date = "2026-10-16"

[[accept]]
id = "D01"
because = "second reason"
by = "SPV"
date = "2026-10-16"
"""
        res = self.run_beta(divs, beta_toml=beta)
        self.assertEqual(res.returncode, 1)
        self.assertIn("ERROR  accept D01: duplicate", res.stdout)

    def test_missing_beta_toml_exit_2(self):
        divs = """
[[divergence]]
id = "D01"
status = "resolved"
cause = "history"
area = "input"
action = "none"
"""
        res = self.run_beta(divs, beta_toml=None)
        self.assertEqual(res.returncode, 2)
        self.assertIn("ERROR", res.stderr)


if __name__ == "__main__":
    unittest.main()
