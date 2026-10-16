"""Tests for scripts/ci_audit.py: the script runs as a subprocess on fake workflow files.

Only the `PROBLEM [ci]` lines are looked at, and only their identifiers (job name, timeout-minutes,
branches-ignore), so the tests do not depend on the language of the messages. The other checks of the
script (waits, carts) read the repository and are not what is tested here."""
import os, subprocess, sys, tempfile, unittest

try:
    import yaml  # noqa: F401
except ImportError:
    print("SKIP: PyYAML not installed (pip install pyyaml)")
    sys.exit(0)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "ci_audit.py")

VALID = """\
name: ci
on:
  push:
    branches-ignore: [sync/**]
concurrency:
  group: ci
  cancel-in-progress: true
jobs:
  host:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - run: make test-host
  qemu:
    runs-on: ubuntu-latest
    timeout-minutes: 60
    steps:
      - run: make test-qemu
"""


def ci_lines(stdout):
    return [ln for ln in stdout.splitlines() if ln.startswith("PROBLEM [ci]")]


class CiAuditTest(unittest.TestCase):
    def run_audit(self, text):
        """text: the content of ci.yml, or None for a path that does not exist"""
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "ci.yml")
            if text is not None:
                with open(path, "w", encoding="utf8", newline="\n") as fh:
                    fh.write(text)
            return subprocess.run([sys.executable, SCRIPT, "--ci", path], capture_output=True, text=True)

    def test_valid_workflow_has_no_ci_problem(self):
        res = self.run_audit(VALID)
        self.assertEqual(ci_lines(res.stdout), [], res.stdout)

    def test_job_without_timeout_minutes(self):
        res = self.run_audit(VALID.replace("    timeout-minutes: 30\n", ""))
        lines = ci_lines(res.stdout)
        self.assertEqual(len(lines), 1, res.stdout)
        self.assertIn("'host'", lines[0])
        self.assertIn("timeout-minutes", lines[0])
        self.assertEqual(res.returncode, 1)

    def test_branches_ignore_without_sync(self):
        res = self.run_audit(VALID.replace("[sync/**]", "[other/**]"))
        lines = ci_lines(res.stdout)
        self.assertEqual(len(lines), 1, res.stdout)
        self.assertIn("branches-ignore", lines[0])
        self.assertEqual(res.returncode, 1)

    def test_missing_file(self):
        res = self.run_audit(None)
        self.assertEqual(len(ci_lines(res.stdout)), 1, res.stdout)
        self.assertEqual(res.returncode, 1)


if __name__ == "__main__":
    unittest.main()
