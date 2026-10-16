"""Tests for scripts/doc_refs.py."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "doc_refs.py")


class DocRefsTest(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.root = self.tmp_dir.name

        # Create minimal repo structure
        os.makedirs(os.path.join(self.root, "docs"), exist_ok=True)
        with open(os.path.join(self.root, "docs", "a.md"), "w", encoding="utf-8") as f:
            f.write("# Doc A\n")

        with open(os.path.join(self.root, "Makefile"), "w", encoding="utf-8") as f:
            f.write("image:\n\t@echo build\n")

        with open(os.path.join(self.root, "easy_install.sh"), "w", encoding="utf-8") as f:
            f.write("case $1 in\n    send) x ;;\nesac\n")

    def tearDown(self):
        self.tmp_dir.cleanup()

    def run_doc_refs(self, content, *extra_args):
        doc_path = os.path.join(self.root, "docs", "test.md")
        with open(doc_path, "w", encoding="utf-8") as f:
            f.write(content)
        cmd = [sys.executable, SCRIPT, doc_path, "--root", self.root, *extra_args]
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_valid_file(self):
        content = "See `docs/a.md` and `Makefile`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)

    def test_missing_repo_path(self):
        content = "See `docs/nope.md`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 1)
        self.assertIn("missing path docs/nope.md", r.stdout)
        self.assertIn(":1:", r.stdout)

    def test_broken_markdown_link(self):
        content = "See [link](missing.md)."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 1)
        self.assertIn("broken link missing.md", r.stdout)

    def test_make_nosuch(self):
        content = "Run `make nosuch`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 1)
        self.assertIn("unknown make target nosuch", r.stdout)

    def test_make_with_options_and_valid_target(self):
        content = "Run `make TARGET=rgb30 image`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)

    def test_easy_install_nosuch(self):
        content = "Run `./easy_install.sh nosuch`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 1)
        self.assertIn("unknown easy_install.sh command nosuch", r.stdout)

    def test_unknown_console_path(self):
        content = "Check `/foo/x`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 1)
        self.assertIn("unknown console path /foo/x", r.stdout)

    def test_valid_console_path(self):
        content = "Saved to `/carts/MYGAME.BME`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)

    def test_placeholder_skipped(self):
        content = "Create `/carts/<NAME>.BME` and run `<build-cmd>`."
        r = self.run_doc_refs(content)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)

    def test_placeholder_gap_with_and_without_final(self):
        content = "Steps: [TO FILL: not done yet]."
        r_nofinal = self.run_doc_refs(content)
        self.assertEqual(r_nofinal.returncode, 0, r_nofinal.stderr + r_nofinal.stdout)

        r_final = self.run_doc_refs(content, "--final")
        self.assertEqual(r_final.returncode, 1)
        self.assertIn("gap left", r_final.stdout)

    def test_max_lines(self):
        content = "line 1\nline 2\nline 3\n"
        r = self.run_doc_refs(content, "--max-lines", "2")
        self.assertEqual(r.returncode, 1)
        self.assertIn("too long: 3 lines (max 2)", r.stdout)


if __name__ == "__main__":
    unittest.main()
