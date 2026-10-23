"""Tests for scripts/wo_scope.py: scope parsing, matching and a real git branch."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "wo_scope.py")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import wo_scope  # noqa: E402

ORDER = """# WO-1
Objective: fix `not/a/file` handling.
## Files to touch
- `scripts/a.py`, `docs/progress/*.md`
- `src/dir/`
- `make` and `x` are not paths
## Verify
`tests/x.py`
"""


def run(*args):
    return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True)


class ScopeParseTest(unittest.TestCase):
    def test_only_files_section_counts(self):
        self.assertEqual(wo_scope.order_scope(ORDER),
                         ["scripts/a.py", "docs/progress/*.md", "src/dir/"])

    def test_no_heading_uses_whole_text(self):
        self.assertEqual(wo_scope.order_scope("Change `a/b.c` and run `make`."), ["a/b.c"])

    def test_matches(self):
        self.assertTrue(wo_scope.matches("src/dir/", "src/dir/q.c"))
        self.assertFalse(wo_scope.matches("src/dir/", "src/dirx/q.c"))
        self.assertTrue(wo_scope.matches("docs/progress/*.md", "docs/progress/tools.md"))
        self.assertFalse(wo_scope.matches("scripts/a.py", "scripts/b.py"))


class ScopeCliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.order = os.path.join(self.tmp.name, "wo.md")
        with open(self.order, "w", encoding="utf-8") as fh:
            fh.write(ORDER)

    def tearDown(self):
        self.tmp.cleanup()

    def test_ok(self):
        res = run("--order", self.order, "--changed", "scripts/a.py", "src/dir/q.c")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("scope ok", res.stdout)
        self.assertIn("unused: docs/progress/*.md", res.stdout)

    def test_violation(self):
        res = run("--order", self.order, "--changed", "scripts/a.py", "src/other.c")
        self.assertEqual(res.returncode, 1)
        self.assertIn("out of scope: src/other.c", res.stdout)
        self.assertIn("scope violated: 1", res.stdout)

    def test_allow_glob(self):
        res = run("--order", self.order, "--allow", "docs/*.md", "--changed", "docs/NEXT.md")
        self.assertEqual(res.returncode, 0, res.stdout)

    def test_usage_errors(self):
        self.assertEqual(run("--changed", "a").returncode, 2)
        empty = os.path.join(self.tmp.name, "e.md")
        with open(empty, "w") as fh:
            fh.write("nothing here\n")
        self.assertEqual(run("--order", empty, "--changed", "a").returncode, 2)

    def git(self, repo, *a):
        subprocess.run(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@t", *a],
                       check=True, capture_output=True)

    def test_real_branch(self):
        repo = os.path.join(self.tmp.name, "repo")
        os.makedirs(os.path.join(repo, "scripts"))
        self.git(repo, "init", "-q", "-b", "main")
        with open(os.path.join(repo, "scripts", "a.py"), "w") as fh:
            fh.write("1\n")
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-q", "-m", "base")
        self.git(repo, "checkout", "-q", "-b", "work")
        with open(os.path.join(repo, "scripts", "a.py"), "w") as fh:
            fh.write("2\n")
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-q", "-m", "ok")
        res = run("--order", self.order, "--repo", repo, "--base", "main", "--branch", "work")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        with open(os.path.join(repo, "extra.txt"), "w") as fh:
            fh.write("x\n")
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-q", "-m", "extra")
        res = run("--order", self.order, "--repo", repo, "--base", "main", "--branch", "work")
        self.assertEqual(res.returncode, 1)
        self.assertIn("out of scope: extra.txt", res.stdout)
        bad = run("--order", self.order, "--repo", repo, "--base", "nope", "--branch", "work")
        self.assertEqual(bad.returncode, 2)


if __name__ == "__main__":
    unittest.main()
