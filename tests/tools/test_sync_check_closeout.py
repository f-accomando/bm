"""tools/sync_check.py --closeout: the closing state of each operator (sync/wo/CLOSEOUT.md 2b) and the
CLOSEOUT-READY / CLOSEOUT-END / CLOSEOUT-NOT-READY last line."""
import contextlib
import datetime
import io
import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import sync_check as sc  # noqa: E402

HEAD = "| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\n|---|---|---|---|---|\n"
CLOSED = "CLOSED @ claude/x@abc1234 · tree clean · aperti: nessuno · %s\n"
NOW = datetime.datetime.now().astimezone().replace(second=0, microsecond=0)


def stamp(minutes_ago):
    return (NOW - datetime.timedelta(minutes=minutes_ago)).isoformat(timespec="minutes")


def row(minutes_ago, action, message, author="SPV"):
    return "| %s | %s | `sync/t` | `%s` | %s |\n" % (stamp(minutes_ago), author, action, message)


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.root = self._td.name
        os.makedirs(os.path.join(self.root, "sync", "inbox"))
        with open(os.path.join(self.root, "sync", "operators.toml"), "w", encoding="utf-8") as f:
            f.write('schema = 1\n\n[[operator]]\nid = "SPV"\nkind = "ai"\n\n'
                    '[[operator]]\nid = "AAA"\nkind = "ai"\n\n[[operator]]\nid = "BBB"\nkind = "ai"\n')
        self._root = sc.ROOT
        sc.ROOT = self.root

    def tearDown(self):
        sc.ROOT = self._root
        self._td.cleanup()

    def sync_md(self, *rows):
        with open(os.path.join(self.root, "SYNC.md"), "w", encoding="utf-8") as f:
            f.write(HEAD + "".join(rows))

    def operator(self, oid, state="FREE", ack_ago=None, eta_ago=None, closed=False, updated_ago=1):
        head = "---\nschema: 1\nid: %s\nstate: %s\nupdated: %s\n" % (oid, state, stamp(updated_ago))
        if eta_ago is not None:
            head += "eta: %s\n" % stamp(eta_ago)
        if ack_ago is not None:
            head += "ack: %s\n" % stamp(ack_ago)
        body = head + "---\n# %s\n\n## Consegne\n\n## Chiusura\n" % oid
        if closed:
            body += CLOSED % stamp(1)
        with open(os.path.join(self.root, "sync", oid + ".md"), "w", encoding="utf-8") as f:
            f.write(body)

    def state(self, oid="AAA"):
        path = os.path.join(self.root, "sync", oid + ".md")
        with open(path, encoding="utf-8") as f:
            text = f.read()
        return sc.closeout_state(oid, sc.header(text), text, sc.rows(os.path.join(self.root, "SYNC.md")), NOW)

    def check(self, *args):
        out = io.StringIO()
        argv, sys.argv = sys.argv, ["sync_check.py", "--closeout"] + list(args)
        try:
            with contextlib.redirect_stdout(out):
                code = sc.main()
        finally:
            sys.argv = argv
        return code, out.getvalue().strip().splitlines()

    # -- the seven states

    def test_active_without_drain(self):
        self.sync_md(row(50, "SIGNOFF", "SIGNOFF AAA: old session"))     # a past session does not count
        self.operator("AAA", "WORKING", ack_ago=60)
        self.assertEqual(self.state(), "ACTIVE")

    def test_draining(self):
        self.sync_md(row(20, "CLOSEOUT", "DRAIN"))
        self.operator("AAA", "WORKING", ack_ago=30, eta_ago=-30)
        self.assertEqual(self.state(), "DRAINING")

    def test_signoff_sent(self):
        self.sync_md(row(20, "CLOSEOUT", "DRAIN"), row(10, "SIGNOFF", "SIGNOFF AAA: closeout"))
        self.operator("AAA", "FREE", ack_ago=15)
        self.assertEqual(self.state(), "SIGNOFF-SENT")

    def test_signoff_ack(self):
        self.sync_md(row(20, "CLOSEOUT", "DRAIN"), row(10, "SIGNOFF", "SIGNOFF AAA: closeout"))
        self.operator("AAA", "FREE", ack_ago=5)                 # read it, but not DONE and no CLOSED line
        self.assertEqual(self.state(), "SIGNOFF-ACK")
        self.operator("AAA", "DONE", ack_ago=5)                 # DONE without the CLOSED line
        self.assertEqual(self.state(), "SIGNOFF-ACK")

    def test_signed_off(self):
        self.sync_md(row(20, "CLOSEOUT", "DRAIN"), row(10, "SIGNOFF", "SIGNOFF AAA: closeout"))
        self.operator("AAA", "DONE", ack_ago=5, closed=True)
        self.assertEqual(self.state(), "SIGNED-OFF")

    def test_unresponsive_after_30_minutes(self):
        self.sync_md(row(60, "CLOSEOUT", "DRAIN"), row(45, "SIGNOFF", "SIGNOFF AAA: closeout"))
        self.operator("AAA", "FREE", ack_ago=50)
        self.assertEqual(self.state(), "UNRESPONSIVE")

    def test_unresponsive_working_past_eta(self):
        self.sync_md(row(60, "CLOSEOUT", "DRAIN"), row(20, "SIGNOFF", "SIGNOFF AAA: closeout"))
        self.operator("AAA", "WORKING", ack_ago=50, eta_ago=15)     # eta + 10 min passed, only 20 min since SIGNOFF
        self.assertEqual(self.state(), "UNRESPONSIVE")

    def test_forced(self):
        self.sync_md(row(60, "CLOSEOUT", "DRAIN"), row(5, "SIGNOFF", "SIGNOFF AAA FORCED: no answer"))
        self.operator("AAA", "FREE", ack_ago=50)
        self.assertEqual(self.state(), "FORCED")

    def test_new_assignment_after_signoff_brings_it_back_to_active(self):
        self.sync_md(row(30, "CLOSEOUT", "DRAIN"), row(20, "SIGNOFF", "SIGNOFF AAA: closeout"),
                     row(5, "WO", "AAA: new work order"))
        self.operator("AAA", "FREE", ack_ago=10)
        self.assertEqual(self.state(), "ACTIVE")
        self.sync_md(row(30, "CLOSEOUT", "DRAIN"), row(20, "SIGNOFF", "SIGNOFF AAA: closeout"),
                     row(5, "WO", "BBB: other work order"))
        self.assertEqual(self.state(), "SIGNOFF-ACK")

    # -- the last line

    def closed_session(self, bbb_state="SIGNED-OFF"):
        self.sync_md(row(30, "CLOSEOUT", "DRAIN"), row(20, "SIGNOFF", "SIGNOFF AAA: closeout"),
                     row(20, "SIGNOFF", "SIGNOFF BBB: closeout"))
        self.operator("AAA", "DONE", ack_ago=5, closed=True)
        if bbb_state == "SIGNED-OFF":
            self.operator("BBB", "DONE", ack_ago=5, closed=True)
        else:
            self.operator("BBB", "FREE", ack_ago=5)

    def test_ready(self):
        self.closed_session()
        self.operator("SPV", "WORKING")
        code, lines = self.check("--strict")
        self.assertEqual(lines[-1], "CLOSEOUT-READY")
        self.assertEqual(code, 0)

    def test_end(self):
        self.sync_md(row(30, "CLOSEOUT", "DRAIN"), row(20, "SIGNOFF", "SIGNOFF AAA: closeout"),
                     row(20, "SIGNOFF", "SIGNOFF BBB: closeout"), row(1, "CLOSEOUT", "END: session closed"))
        self.operator("AAA", "DONE", ack_ago=5, closed=True)
        self.operator("BBB", "DONE", ack_ago=5, closed=True)
        self.operator("SPV", "DONE")
        code, lines = self.check("--strict")
        self.assertEqual(lines[-1], "CLOSEOUT-END")
        self.assertEqual(code, 0)
        self.operator("SPV", "WORKING")                         # END line but SPV not DONE: only READY
        self.assertEqual(self.check()[1][-1], "CLOSEOUT-READY")

    def test_not_ready_exits_1_with_strict(self):
        self.closed_session(bbb_state="FREE")
        self.operator("SPV", "WORKING")
        code, lines = self.check("--strict")
        self.assertEqual(lines[-1], "CLOSEOUT-NOT-READY: BBB=SIGNOFF-ACK")
        self.assertEqual(code, 1)
        self.assertEqual(self.check()[0], 0)                    # without --strict the exit stays 0

    def test_table_has_the_closeout_column(self):
        self.closed_session()
        self.operator("SPV", "WORKING")
        header = self.check()[1][1]
        self.assertIn("closeout", header)

    # -- the leftovers (CLOSEOUT.md 2c and 2e); git is mocked

    GIT = {("branch", "-r", "--no-merged", "origin/bm-core"):
           "  origin/HEAD -> origin/bm-core\n  origin/claude/aaa-work\n  origin/agy/old\n",
           ("worktree", "list", "--porcelain"):
           "worktree /w/main\nHEAD abc\nbranch refs/heads/sync/t\n\nworktree /w/rv\nHEAD def\ndetached\n"}

    def leftovers(self, git=None):
        with mock.patch.object(sc, "git", side_effect=lambda *a: (self.GIT if git is None else git).get(a)):
            return sc.leftovers(sc.rows(os.path.join(self.root, "SYNC.md")), sc.toml_ops())

    def test_leftovers_inbox_wo_and_open_items(self):
        self.sync_md(row(30, "WO", "AAA: WO XX-1 first"), row(29, "ASSIGNED", "AAA: WO XX-2 second"))
        with open(os.path.join(self.root, "sync", "AAA.md"), "w", encoding="utf-8") as f:
            f.write("---\nschema: 1\nid: AAA\nstate: DONE\nupdated: %s\n---\n# AAA\n\n## Consegne\n"
                    "- DELIVERED XX-1 @ b@abc1234 · verifica: t -> ok · 2026-10-17T10:00\n\n## Chiusura\n"
                    "CLOSED @ b@abc1234 · tree clean · aperti: XX-3 · 2026-10-17T10:05\n" % stamp(1))
        with open(os.path.join(self.root, "sync", "inbox", "AAA.md"), "w", encoding="utf-8") as f:
            f.write("new task\n\n")
        out = self.leftovers()
        self.assertIn("  > inbox AAA: 1 line(s) not empty", out)
        self.assertIn("  > WO XX-2 assigned and not delivered", out)
        self.assertNotIn("  > WO XX-1 assigned and not delivered", out)      # delivered
        self.assertIn("  > AAA: closed with open items: XX-3", out)

    def test_leftovers_wo_id_compare_is_exact(self):
        # AGY-10 delivered must not hide AGY-1 (and AGY-1 delivered must not hide AGY-10)
        self.sync_md(row(30, "WO", "AAA: WO AGY-1 first"), row(29, "WO", "AAA: WO AGY-10 tenth"),
                     row(28, "WO", "AAA: WO AGY-2 second"))
        with open(os.path.join(self.root, "sync", "AAA.md"), "w", encoding="utf-8") as f:
            f.write("---\nschema: 1\nid: AAA\nstate: FREE\nupdated: %s\n---\n# AAA\n\n## Consegne\n"
                    "- DELIVERED AGY-10 @ b@abc1234 · verifica: t -> ok · 2026-10-17T10:00\n"
                    "- DELIVERED AGY-2 @ b@abc1235 · verifica: t -> ok · 2026-10-17T10:01\n" % stamp(1))
        out = self.leftovers()
        self.assertIn("  > WO AGY-1 assigned and not delivered", out)
        self.assertNotIn("  > WO AGY-10 assigned and not delivered", out)
        self.assertNotIn("  > WO AGY-2 assigned and not delivered", out)

    def test_leftovers_branches_and_worktrees(self):
        self.sync_md(row(30, "CLOSEOUT", "DRAIN"))
        self.operator("AAA", "FREE")
        with open(os.path.join(self.root, "sync", "operators.toml"), "a", encoding="utf-8") as f:
            f.write('branches = ["claude/aaa-work"]\n')           # belongs to the last operator (BBB)
        out = self.leftovers()
        self.assertIn("  > branch origin/claude/aaa-work not merged (BBB)", out)
        self.assertIn("  > branch origin/agy/old not merged", out)
        self.assertFalse([e for e in out if "->" in e])                     # the origin/HEAD alias is skipped
        self.assertIn("  > worktree /w/main (sync/t)", out)
        self.assertIn("  > worktree /w/rv (detached)", out)

    def test_leftovers_git_unavailable(self):
        self.sync_md(row(30, "CLOSEOUT", "DRAIN"))
        self.operator("AAA", "FREE")
        out = self.leftovers(git={})
        self.assertEqual([e for e in out if "git:" in e], ["  > git: n/d", "  > git: n/d"])

    def test_closeout_prints_leftovers_before_the_last_line(self):
        self.closed_session()
        self.operator("SPV", "WORKING")
        with mock.patch.object(sc, "git", side_effect=lambda *a: self.GIT.get(a)):
            lines = self.check()[1]
        self.assertTrue(any(ln.startswith("  > worktree /w/main") for ln in lines))
        self.assertEqual(lines[-1], "CLOSEOUT-READY")


if __name__ == "__main__":
    unittest.main()
