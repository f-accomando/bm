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
Perimetro: `src/kernel/sdlog.c|.h`, `src/rgb30/main.c:96`, `carts/editor/*`. NON toccare: `src/kernel/monitor.c`, `src/bm/runtime.c|.h`.

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


if __name__ == "__main__":
    unittest.main()
