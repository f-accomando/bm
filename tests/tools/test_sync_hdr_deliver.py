"""tools/sync_hdr.py --deliver WO: ledger row + DELIVERED line + state FREE in one call (SYNC-EN-4)."""
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_hdr.py")
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import sync_check as sc  # noqa: E402

START = "---\nschema: 1\nid: OP\nstate: WORKING\ntask: t\nwaiting_on: \nack: 2026-10-19T08:00+02:00\nupdated: x\n---\n# OP\n\n"


def git(args, cwd):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t"] + args, cwd=cwd, check=True,
                          capture_output=True, text=True).stdout.strip()


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.d = self._td.name
        git(["init", "-q"], self.d)
        with open(os.path.join(self.d, "a.c"), "w") as f:
            f.write("x\n" * 3)
        git(["add", "."], self.d)
        git(["commit", "-qm", "c"], self.d)
        self.sha = git(["rev-parse", "--short", "HEAD"], self.d)
        self.hdr = os.path.join(self.d, "OP.md")
        self.ledger = os.path.join(self.d, "L.jsonl")
        with open(os.path.join(self.d, "SYNC.md"), "w", encoding="utf-8") as f:
            f.write("| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\n|---|---|---|---|---|\n"
                    "| 2026-10-20T09:00+02:00 | SPV | `sync/t` | `NOTE` | a |\n")

    def tearDown(self):
        self._td.cleanup()

    def write(self, body):
        with open(self.hdr, "w", encoding="utf-8", newline="") as f:
            f.write(START + body)

    def read(self):
        with open(self.hdr, encoding="utf-8") as f:
            return f.read()

    def tool(self, *args):
        # the repo for the commit is self.d; the tool's ROOT (SYNC.md for the ack) is the real checkout
        return subprocess.run([sys.executable, TOOL, "OP", "--file", self.hdr, "--repo", self.d,
                               "--ledger", self.ledger] + list(args), capture_output=True, text=True)

    def deliver(self, verify="t -> ok", **kw):
        return self.tool("--deliver", "XX-1", "--branch", "claude/x", "--commit", self.sha, "--tokens", "1000",
                         "--min", "10", "--verify", verify)

    def test_line_in_english_section(self):
        self.write("## Deliveries\n\nDELIVERED XX-0 @ b@abc1234 \u00b7 verify: t -> ok \u00b7 2026-10-19T10:00+02:00\n\n## Closing\n")
        r = self.deliver()
        self.assertEqual(r.returncode, 0, r.stderr)
        txt = self.read()
        lines = txt.splitlines()
        i = [n for n, ln in enumerate(lines) if ln.startswith("DELIVERED XX-1")]
        self.assertEqual(len(i), 1, txt)
        self.assertTrue(lines[i[0] - 1].startswith("DELIVERED XX-0"), txt)           # after the last non-empty line
        self.assertLess(txt.index("DELIVERED XX-1"), txt.index("## Closing"))
        self.assertRegex(lines[i[0]], r"^DELIVERED XX-1 @ claude/x@%s \u00b7 verify: t -> ok \u00b7 \d{4}-\d\d-\d\dT\d\d:\d\d" % self.sha)
        self.assertIn("state: FREE", txt)
        self.assertRegex(r.stdout, r"^done WO XX-1 FREE: 1 files \+3 -0, 1000 tokens, 10 min")

    def test_default_root_is_the_current_checkout(self):
        # SYNC-EN-11: without --file/--ledger/--repo the file and the ledger are those of the checkout of the cwd
        os.makedirs(os.path.join(self.d, "sync"))
        path = os.path.join(self.d, "sync", "OP.md")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(START + "## Deliveries\n\n## Closing\n")
        r = subprocess.run([sys.executable, TOOL, "OP", "--deliver", "XX-1", "--branch", "claude/x", "--commit", self.sha,
                            "--tokens", "1", "--min", "1", "--verify", "t -> ok"], cwd=self.d, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(path, encoding="utf-8") as f:
            self.assertIn("DELIVERED XX-1 @ claude/x@" + self.sha, f.read())
        self.assertTrue(os.path.exists(os.path.join(self.d, "sync", "OP.ledger.jsonl")))
        self.assertFalse(os.path.exists(os.path.join(HERE, "..", "..", "sync", "OP.ledger.jsonl")))

    def test_deliver_drops_only_its_assigned_line(self):
        os.makedirs(os.path.join(self.d, "sync", "inbox"))
        inbox = os.path.join(self.d, "sync", "inbox", "OP.md")
        keep = "ASSIGNED XX-10 @ b · t\nASSIGNED XX-2 @ b · t\n"
        with open(inbox, "w", encoding="utf-8", newline="") as f:
            f.write("ASSIGNED XX-1 @ b · t\n" + keep)
        self.write("## Deliveries\n\n## Closing\n")
        r = subprocess.run([sys.executable, TOOL, "OP", "--file", self.hdr, "--repo", self.d, "--deliver", "XX-1",
                            "--branch", "claude/x", "--commit", self.sha, "--tokens", "1", "--min", "1",
                            "--verify", "t -> ok"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(inbox, encoding="utf-8") as f:
            self.assertEqual(f.read(), keep)

    def test_line_in_italian_section_when_only_that_exists(self):
        self.write("## Consegne\n\n## Chiusura\n")
        r = self.deliver()
        self.assertEqual(r.returncode, 0, r.stderr)
        txt = self.read()
        self.assertNotIn("## Deliveries", txt)
        self.assertLess(txt.index("## Consegne"), txt.index("DELIVERED XX-1"))
        self.assertLess(txt.index("DELIVERED XX-1"), txt.index("## Chiusura"))

    def test_no_section_creates_deliveries_at_the_end(self):
        self.write("## Profilo\n\n- a\n")
        self.assertEqual(self.deliver().returncode, 0)
        txt = self.read()
        self.assertTrue(txt.rstrip().endswith(txt[txt.index("DELIVERED XX-1"):].rstrip()), txt)
        self.assertGreater(txt.index("## Deliveries"), txt.index("## Profilo"))

    def test_free_then_working_and_waiting_on_cleared(self):
        self.write("## Deliveries\n\n")
        self.assertEqual(self.tool("--state", "WAITING", "--waiting-on", "user").returncode, 0)
        self.assertEqual(self.deliver().returncode, 0)
        txt = self.read()
        self.assertIn("state: FREE", txt)
        self.assertRegex(txt, r"waiting_on: *\n")
        self.assertEqual(self.tool("--state", "WORKING", "--task", "Y").returncode, 0)
        self.assertIn("state: WORKING", self.read())

    def test_ledger_row(self):
        self.write("## Deliveries\n\n")
        self.assertEqual(self.deliver().returncode, 0)
        with open(self.ledger, encoding="utf-8") as f:
            rows = [json.loads(ln) for ln in f if ln.strip()]
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0]["wo"], rows[0]["tokens"], rows[0]["min"], rows[0]["branch"]),
                         ("XX-1", 1000, 10, "claude/x"))

    def test_bad_calls_exit_2_and_change_nothing(self):
        self.write("## Deliveries\n\n")
        before = self.read()
        base = ["--branch", "claude/x", "--commit", self.sha, "--tokens", "1", "--min", "1"]
        cases = [
            ["--deliver", "XX-1", "--close", "XX-1"] + base + ["--verify", "t -> ok"],
            ["--deliver", "XX-1"] + base + ["--verify", "no result here"],
            ["--deliver", "XX-1"] + base + ["--verify", "a -> b\nc"],
            ["--deliver", "XX-1", "--commit", self.sha, "--tokens", "1", "--min", "1", "--verify", "t -> ok"],   # no --branch
            ["--deliver", "XX-1", "--branch", "b", "--tokens", "1", "--min", "1", "--verify", "t -> ok"],         # no --commit
            ["--deliver", "XX-1", "--branch", "b", "--commit", self.sha, "--min", "1", "--verify", "t -> ok"],    # no --tokens
            ["--deliver", "XX-1", "--branch", "b", "--commit", self.sha, "--tokens", "1", "--verify", "t -> ok"],  # no --min
            ["--deliver", "XX-1", "--branch", "b", "--commit", self.sha, "--tokens", "1", "--min", "1"],           # no --verify
        ]
        for args in cases:
            r = self.tool(*args)
            self.assertNotEqual(r.returncode, 0, args)
            self.assertEqual(self.read(), before, args)
            self.assertFalse(os.path.exists(self.ledger), args)

    def test_sync_check_accepts_the_line(self):
        self.write("## Deliveries\n\n")
        self.assertEqual(self.deliver("python -m unittest -> 6 OK").returncode, 0)
        lines = sc.consegne(self.read())
        self.assertEqual(len(lines), 1, lines)
        self.assertTrue(sc.DELIVERED.match(lines[0]), lines[0])
        self.assertEqual(sc.delivered_parts(lines[0])[0], "XX-1")


if __name__ == "__main__":
    unittest.main()
