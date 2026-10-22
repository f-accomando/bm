"""Tests for scripts/quarantine_check.py."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "quarantine_check.py")


class QuarantineCheckTest(unittest.TestCase):
    def test_valid_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_py = os.path.join(tmp, "my_test.py")
            with open(test_py, "w", encoding="utf-8") as f:
                f.write("def test_sample(): pass\n")

            q_file = os.path.join(tmp, "quarantine.txt")
            with open(q_file, "w", encoding="utf-8") as f:
                f.write(f"# header\n{test_py}::test_sample | known failure | 2030-01-01\n")

            res = subprocess.run(
                [sys.executable, SCRIPT, "--file", q_file, "--today", "2026-10-15"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 0, f"stdout: {res.stdout}\nstderr: {res.stderr}")

    def test_nonexistent_test_in_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_py = os.path.join(tmp, "my_test.py")
            with open(test_py, "w", encoding="utf-8") as f:
                f.write("def other_test(): pass\n")

            q_file = os.path.join(tmp, "quarantine.txt")
            with open(q_file, "w", encoding="utf-8") as f:
                f.write(f"{test_py}::test_missing | flakiness | 2030-01-01\n")

            res = subprocess.run(
                [sys.executable, SCRIPT, "--file", q_file, "--today", "2026-10-15"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("line 1", res.stderr)
            self.assertIn("test_missing", res.stderr)

    def test_expired_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_py = os.path.join(tmp, "my_test.py")
            with open(test_py, "w", encoding="utf-8") as f:
                f.write("def test_old(): pass\n")

            q_file = os.path.join(tmp, "quarantine.txt")
            with open(q_file, "w", encoding="utf-8") as f:
                f.write(f"{test_py}::test_old | expired reason | 2026-01-01\n")

            res = subprocess.run(
                [sys.executable, SCRIPT, "--file", q_file, "--today", "2026-10-15"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("quarantine expired", res.stderr)

    def test_duplicate_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            test_py = os.path.join(tmp, "my_test.py")
            with open(test_py, "w", encoding="utf-8") as f:
                f.write("def test_dup(): pass\n")

            q_file = os.path.join(tmp, "quarantine.txt")
            with open(q_file, "w", encoding="utf-8") as f:
                f.write(f"{test_py}::test_dup | reason 1 | 2030-01-01\n")
                f.write(f"{test_py}::test_dup | reason 2 | 2030-01-01\n")

            res = subprocess.run(
                [sys.executable, SCRIPT, "--file", q_file, "--today", "2026-10-15"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("duplicate", res.stderr)

    def test_wrong_field_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            q_file = os.path.join(tmp, "quarantine.txt")
            with open(q_file, "w", encoding="utf-8") as f:
                f.write("tests/my_test.py::test_foo | only two fields\n")

            res = subprocess.run(
                [sys.executable, SCRIPT, "--file", q_file, "--today", "2026-10-15"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("expected 3 fields", res.stderr)


if __name__ == "__main__":
    unittest.main()
