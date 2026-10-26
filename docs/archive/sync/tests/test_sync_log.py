import datetime
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_log.py")
HEAD = "| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\r\n|---|---|---|---|---|\r\n"
ROW = "| 2026-10-15T20:00+02:00 | SPV | `b` | `RULE` | x |\r\n"


def run(args, cwd):
    return subprocess.run([sys.executable, TOOL] + args, cwd=cwd, capture_output=True, text=True)


def git(args, cwd):
    subprocess.run(["git"] + args, cwd=cwd, check=True, capture_output=True)


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        self.f = os.path.join(self.d, "SYNC.md")
        with open(self.f, "wb") as fh:
            fh.write((HEAD + ROW).encode())

    def tearDown(self):
        self._td.cleanup()

    def raw(self):
        with open(self.f, "rb") as fh:
            return fh.read()

    def log(self, text="ciao", action="RULE", extra=()):
        return run(["log", "--who", "SPV", "--branch", "br", "--action", action,
                    "--text", text, "--file", self.f] + list(extra), self.d)

    def test_append_and_timestamp(self):
        r = self.log(extra=["--commit", "abc123"])
        self.assertEqual(r.returncode, 0, r.stderr)
        last = self.raw().decode().split("\r\n")[-2]
        m = re.match(r"\| (\d{4}-\d\d-\d\dT\d\d:\d\d[+-]\d\d:\d\d) \| SPV \| `br`@abc123 \| `RULE` \| ciao \|$", last)
        self.assertTrue(m, last)
        ts = datetime.datetime.fromisoformat(m.group(1))
        delta = abs((datetime.datetime.now().astimezone() - ts).total_seconds())
        self.assertLess(delta, 120)
        self.assertEqual(r.stderr, "")

    def test_too_long(self):
        before = self.raw()
        r = self.log(text="a" * 601)
        self.assertEqual(r.returncode, 2)
        self.assertIn("max 600", r.stderr)
        self.assertEqual(self.raw(), before)
        self.assertEqual(self.log(text="a" * 540).returncode, 0)

    def test_pipe_and_newline(self):
        before = self.raw()
        self.assertEqual(self.log(text="a|b").returncode, 2)
        self.assertEqual(self.log(text="a\nb").returncode, 2)
        self.assertEqual(self.raw(), before)

    def test_unknown_action_warns(self):
        r = self.log(action="ZZZ")
        self.assertEqual(r.returncode, 0)
        self.assertIn("warning", r.stderr)

    def test_inbox(self):
        ib = os.path.join(self.d, "inbox")
        a = ["inbox", "--id", "AGY", "--file-dir", ib]
        r = run(a + sum([["--line", "l%d" % i] for i in range(5)], []), self.d)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(os.path.join(ib, "AGY.md"), "rb") as fh:
            self.assertEqual(fh.read(), b"l0\nl1\nl2\nl3\nl4\n")
        r = run(a + sum([["--line", "l%d" % i] for i in range(6)], []), self.d)
        self.assertEqual(r.returncode, 2)

    def _repo(self):
        bare = os.path.join(self.d, "remote.git")
        work = os.path.join(self.d, "work")
        git(["init", "--bare", "-b", "sync/t", bare], self.d)
        git(["init", "-b", "sync/t", work], self.d)
        for k, v in (("user.email", "t@t"), ("user.name", "t")):
            git(["config", k, v], work)
        git(["remote", "add", "origin", bare], work)
        with open(os.path.join(work, "SYNC.md"), "wb") as fh:
            fh.write((HEAD + ROW).encode())
        git(["add", "."], work)
        git(["commit", "-m", "init"], work)
        git(["push", "origin", "HEAD:sync/t"], work)
        return bare, work

    def count(self, repo, ref):
        return subprocess.run(["git", "rev-list", "--count", ref],
                              cwd=repo, capture_output=True, text=True).stdout.strip()

    def test_push(self):
        bare, work = self._repo()
        r = run(["log", "--who", "SPV", "--branch", "sync/t", "--action", "RULE",
                 "--text", "p", "--file", os.path.join(work, "SYNC.md"), "--push"], work)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.count(work, "HEAD"), "2")
        self.assertEqual(self.count(bare, "sync/t"), "2")
        msg = subprocess.run(["git", "log", "-1", "--format=%B"], cwd=work, capture_output=True, text=True).stdout
        self.assertIn("sync: RULE SPV", msg)

    def test_no_push_no_commit(self):
        bare, work = self._repo()
        r = run(["log", "--who", "SPV", "--branch", "sync/t", "--action", "RULE",
                 "--text", "p", "--file", os.path.join(work, "SYNC.md")], work)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.count(work, "HEAD"), "1")


if __name__ == "__main__":
    unittest.main()
