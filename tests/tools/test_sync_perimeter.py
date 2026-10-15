"""Tests for tools/sync_perimeter.py."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "tools", "sync_perimeter.py")

FIXTURE_WO = """# Work Order Sample
Base: `bm-core`
Perimetro: `src/kernel/sdlog.c|.h`, `src/rgb30/main.c:96`, `carts/editor/*`. NON toccare: `src/kernel/monitor.c`, `src/bm/runtime.c|.h`, `.github/workflows/*`.

## SEC-1 — sub task
- File: `src/kernel/reports.c|.h`, `src/bm/runtime.c`. NON toccare: `src/net/netxfer.c`.
"""


class SyncPerimeterTest(unittest.TestCase):
    def test_fixture_all_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(FIXTURE_WO)

            # Valid files: sdlog.h (expanded from .c|.h), main.c (from :96), carts/editor/ui.lua (from glob)
            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "src/kernel/sdlog.h", "src/rgb30/main.c", "carts/editor/ui.lua"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 0, res.stdout)

            # Forbidden file
            res_forbid = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "src/kernel/monitor.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res_forbid.returncode, 1)
            self.assertIn("FORBIDDEN src/kernel/monitor.c", res_forbid.stdout)

            # Out of perimeter file
            res_out = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "random/file.txt"],
                capture_output=True, text=True
            )
            self.assertEqual(res_out.returncode, 1)
            self.assertIn("OUT random/file.txt", res_out.stdout)

    def test_section_override_forbidden(self):
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(FIXTURE_WO)

            # In initial part, runtime.c is forbidden. But SEC-1 allows it explicitly.
            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "SEC-1", "--files", "src/bm/runtime.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 0, f"stdout: {res.stdout}\nstderr: {res.stderr}")

            # monitor.c was forbidden in initial and not mentioned in SEC-1
            res_forbid = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "SEC-1", "--files", "src/kernel/monitor.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res_forbid.returncode, 1)
            self.assertIn("FORBIDDEN src/kernel/monitor.c", res_forbid.stdout)

    def test_basename_without_slash(self):
        """Basename without '/' matches files with matching basename in any directory."""
        wo_text = """# Basename WO
Perimetro: `Makefile`, `app.py`. NON toccare: `secret.key`.
"""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(wo_text)

            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "Makefile", "sub/dir/app.py"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 0, res.stdout)

            res_forbid = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "config/secret.key"],
                capture_output=True, text=True
            )
            self.assertEqual(res_forbid.returncode, 1)
            self.assertIn("FORBIDDEN config/secret.key", res_forbid.stdout)

    def test_line_range_colon_n_m(self):
        """:N-M and :N are stripped from file tokens."""
        wo_text = """# Line Range WO
Perimetro: `src/kernel/main.c:260-295`, `src/rgb30/pad.c:10`.
"""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(wo_text)

            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "src/kernel/main.c", "src/rgb30/pad.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 0, res.stdout)

    def test_dot_github_workflows_forbidden(self):
        """.github paths must not be stripped by lstrip and must trigger FORBIDDEN."""
        wo_text = """# Dot Github WO
Perimetro: `src/main.c`. NON toccare: `.github/workflows/*`.
"""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(wo_text)

            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", ".github/workflows/ci.yml"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 1)
            self.assertIn("FORBIDDEN .github/workflows/ci.yml", res.stdout)

            res_dot_slash = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--files", "./.github/workflows/ci.yml"],
                capture_output=True, text=True
            )
            self.assertEqual(res_dot_slash.returncode, 1)
            self.assertIn("FORBIDDEN .github/workflows/ci.yml", res_dot_slash.stdout)

    def test_previous_section_not_inherited(self):
        """When checking a section, previous sections' allowed files are not inherited."""
        wo_text = """# Multi Section WO
Perimetro: `base.c`.

## SEC-1
File: `sec1.c`.

## SEC-2
File: `sec2.c`.
"""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(wo_text)

            # SEC-2 should allow base.c and sec2.c, but NOT sec1.c
            res_ok = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "SEC-2", "--files", "base.c", "sec2.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res_ok.returncode, 0, res_ok.stdout)

            res_out = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "SEC-2", "--files", "sec1.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res_out.returncode, 1)
            self.assertIn("OUT sec1.c", res_out.stdout)

    def test_id_prefix_exact_match(self):
        """FRZ-1 must not match FRZ-10 as section ID."""
        wo_text = """# Prefix ID WO
Perimetro: `base.c`.

## FRZ-10 — task ten
File: `ten.c`.

## FRZ-1 — task one
File: `one.c`.
"""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(wo_text)

            res_one = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "FRZ-1", "--files", "one.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res_one.returncode, 0, res_one.stdout)

            res_ten_in_one = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "FRZ-1", "--files", "ten.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res_ten_in_one.returncode, 1)
            self.assertIn("OUT ten.c", res_ten_in_one.stdout)

    def test_missing_section_exits_2(self):
        """Non-existent --section writes to stderr and exits with code 2."""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(FIXTURE_WO)

            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--section", "NONEXISTENT", "--files", "src/bm/runtime.c"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 2)
            self.assertIn("Section 'NONEXISTENT' not found", res.stderr)

    def test_missing_wo_file_exits_2(self):
        """Non-existent WO file writes to stderr and exits with code 2."""
        res = subprocess.run(
            [sys.executable, SCRIPT, "--wo", "nonexistent_file_path.md", "--files", "foo.c"],
            capture_output=True, text=True
        )
        self.assertEqual(res.returncode, 2)
        self.assertIn("Cannot read", res.stderr)

    def test_diff_option(self):
        """--diff executes git diff and checks files; bad range exits 2."""
        wo_text = """# Diff WO
Perimetro: `*`.
"""
        with tempfile.TemporaryDirectory() as tmp:
            wo_path = os.path.join(tmp, "wo.md")
            with open(wo_path, "w", encoding="utf-8") as f:
                f.write(wo_text)

            # In HEAD...HEAD diff is empty, so returncode is 0
            res = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--diff", "HEAD...HEAD"],
                capture_output=True, text=True
            )
            self.assertEqual(res.returncode, 0, f"stdout: {res.stdout}\nstderr: {res.stderr}")

            # Invalid git revision range
            res_bad = subprocess.run(
                [sys.executable, SCRIPT, "--wo", wo_path, "--diff", "INVALID_REV_XYZ...INVALID_REV_ABC"],
                capture_output=True, text=True
            )
            self.assertEqual(res_bad.returncode, 2)
            self.assertIn("git diff error", res_bad.stderr)


if __name__ == "__main__":
    unittest.main()
