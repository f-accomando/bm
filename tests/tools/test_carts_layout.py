"""Tests for scripts/carts_layout.py: tests CLI checks on temporary repo structures."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "carts_layout.py")

VALID_BMINFO = """name: Pong
author: Dev
resolution: 640x360
"""


def setup_repo(root, projects=None, doc_ids=None, create_carts=True):
    if create_carts:
        carts_dir = os.path.join(root, "carts")
        os.makedirs(carts_dir, exist_ok=True)
        if projects:
            for name, data in projects.items():
                pdir = os.path.join(carts_dir, name)
                os.makedirs(pdir, exist_ok=True)
                if "bminfo" in data and data["bminfo"] is not None:
                    with open(os.path.join(pdir, "bm.info"), "w", encoding="utf-8") as f:
                        f.write(data["bminfo"])
                for fname in data.get("files", []):
                    with open(os.path.join(pdir, fname), "w", encoding="utf-8") as f:
                        f.write("")

    if doc_ids is not None:
        docs_dir = os.path.join(root, "docs")
        os.makedirs(docs_dir, exist_ok=True)
        doc_path = os.path.join(docs_dir, "PROJECT-LAYOUT.md")
        lines = [
            "# Project layout",
            "",
            "## Migrated so far",
            "",
            "| Game | Title | Notes |",
            "|---|---|---|",
        ]
        for did in doc_ids:
            lines.append(f"| `{did}` | Game Title | Migrated |")
        lines.append("")
        lines.append("## Next section")
        with open(doc_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")


class CartsLayoutTest(unittest.TestCase):
    def run_script(self, root):
        return subprocess.run(
            [sys.executable, SCRIPT, "--root", root],
            capture_output=True,
            text=True,
        )

    def test_valid_migrated_and_doc_table_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": VALID_BMINFO, "files": ["main.lua"]}},
                doc_ids=["pong"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("ok     pong", res.stdout)
            self.assertIn("layout: 1 projects, 1 with bm.info, 0 old layout, 0 errors", res.stdout)

    def test_bminfo_missing_author(self):
        bad_info = "name: Pong\nresolution: 640x360\n"
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": bad_info, "files": ["main.lua"]}},
                doc_ids=["pong"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn("ERROR  pong: bm.info has no author", res.stdout)

    def test_bminfo_invalid_resolution(self):
        bad_info = "name: Pong\nauthor: Dev\nresolution: 800x600\n"
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": bad_info, "files": ["main.lua"]}},
                doc_ids=["pong"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn(
                "ERROR  pong: resolution 800x600 is not one of 640x360, 480x270, 320x180, 256x256, 360x360, 720x720",
                res.stdout,
            )

    def test_key_inside_part_block_not_counted(self):
        bad_info = "name: Pong\nresolution: 640x360\n[part 1]\nauthor: Dev\n"
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": bad_info, "files": ["main.lua"]}},
                doc_ids=["pong"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn("ERROR  pong: bm.info has no author", res.stdout)

    def test_misplaced_file_migrated(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": VALID_BMINFO, "files": ["main.lua", "sheet.png"]}},
                doc_ids=["pong"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn("MISPLACED pong/sheet.png: goes in assets/", res.stdout)

    def test_misplaced_file_old_layout_note(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"demo": {"files": ["main.lua", "mkassets.py"]}},
                doc_ids=[],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertIn("old    demo: no bm.info (old layout)", res.stdout)
            self.assertIn("note  demo/mkassets.py: goes in build/ when migrated", res.stdout)
            self.assertIn("layout: 1 projects, 0 with bm.info, 1 old layout, 0 errors", res.stdout)

    def test_case_insensitive_misplaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": VALID_BMINFO, "files": ["main.lua", "Sheet.PNG"]}},
                doc_ids=["pong"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn("MISPLACED pong/Sheet.PNG: goes in assets/", res.stdout)

    def test_doc_table_id_without_bminfo(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": VALID_BMINFO, "files": ["main.lua"]}},
                doc_ids=["pong", "missing"],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn('ERROR  doc: missing is in "Migrated so far" but has no bm.info', res.stdout)

    def test_bminfo_not_in_doc_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(
                tmp,
                projects={"pong": {"bminfo": VALID_BMINFO, "files": ["main.lua"]}},
                doc_ids=[],
            )
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 1)
            self.assertIn('ERROR  doc: pong has bm.info but is not in "Migrated so far"', res.stdout)

    def test_missing_carts_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            setup_repo(tmp, create_carts=False)
            res = self.run_script(tmp)
            self.assertEqual(res.returncode, 2)
            self.assertIn("carts directory not found", res.stderr)


if __name__ == "__main__":
    unittest.main()
