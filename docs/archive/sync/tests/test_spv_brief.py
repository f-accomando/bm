import datetime
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "spv_brief.py")
HEAD = "| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\n|---|---|---|---|---|\n"


def stamp(minutes_ago):
    t = datetime.datetime.now().astimezone() - datetime.timedelta(minutes=minutes_ago)
    return t.isoformat(timespec="minutes")


def run(root):
    return subprocess.run([sys.executable, TOOL, "--root", root], capture_output=True, text=True, encoding="utf-8")


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.root = self._td.name
        os.makedirs(os.path.join(self.root, "sync"))
        with open(os.path.join(self.root, "sync", "operators.toml"), "w", encoding="utf-8") as f:
            f.write('schema = 1\n\n[[operator]]\nid = "SPV"\nkind = "ai"\n\n'
                    '[[operator]]\nid = "AGX"\nkind = "ai"\n\n[[operator]]\nid = "UTE"\nkind = "human"\n')

    def tearDown(self):
        self._td.cleanup()

    def sync_md(self, rows):
        with open(os.path.join(self.root, "SYNC.md"), "w", encoding="utf-8") as f:
            f.write(HEAD + "".join(rows))

    def operator(self, oid, delivered, updated_ago=5, ack_ago=10):
        body = ("---\nschema: 1\nid: %s\nstate: WORKING\nupdated: %s\nack: %s\n---\n# %s\n\n## Consegne\n"
                % (oid, stamp(updated_ago), stamp(ack_ago), oid))
        body += "".join("- %s\n" % ln for ln in delivered)
        with open(os.path.join(self.root, "sync", oid + ".md"), "w", encoding="utf-8") as f:
            f.write(body)

    def review(self, minutes_ago, text):
        return "| %s | SPV | `sync/t` | `REVIEW` | %s |\n" % (stamp(minutes_ago), text)

    def test_only_items_after_last_review_are_new(self):
        # REVIEW 30 min ago covers AGX-1 (sha 11111111). AGX-2 is older than that review and
        # uncovered: treated as reviewed. AGX-3 is newer than it: new. AGX-1 is covered.
        self.operator("AGX", [
            "DELIVERED AGX-3 @ b@33333333 · verifica: x -> ok · 1 file · %s" % stamp(5),
            "DELIVERED AGX-2 @ b@22222222 · verifica: x -> ok · 1 file · %s" % stamp(40),
            "DELIVERED AGX-1 @ b@11111111 · verifica: x -> ok · 1 file · %s" % stamp(50),
        ])
        self.sync_md([self.review(30, "REVIEW: APPROVED AGX-1 (11111111): ok")])
        r = run(self.root)
        self.assertEqual(r.returncode, 0, r.stderr)
        new = [ln for ln in r.stdout.splitlines() if ln.startswith("    new DELIVERED")]
        self.assertEqual(len(new), 1, r.stdout)
        self.assertIn("AGX-3 33333333", new[0])
        self.assertNotIn("AGX-2", new[0])
        self.assertNotIn("AGX-1", new[0])
        self.assertNotIn("verifica", r.stdout.split("last")[0])

    def test_english_labels_deliveries_and_verify(self):
        # SYNC-EN-1: `## Deliveries` and `verify:` are read as `## Consegne` and `verifica:`
        body = ("---\nschema: 1\nid: AGX\nstate: WORKING\nupdated: %s\nack: %s\n---\n# AGX\n\n## Deliveries\n"
                "- DELIVERED AGX-7 @ b@77777777 · verify: x -> ok · 1 file · %s\n" % (stamp(5), stamp(10), stamp(5)))
        with open(os.path.join(self.root, "sync", "AGX.md"), "w", encoding="utf-8") as f:
            f.write(body)
        self.sync_md([])
        r = run(self.root)
        self.assertEqual(r.returncode, 0, r.stderr)
        new = [ln for ln in r.stdout.splitlines() if ln.startswith("    new DELIVERED")]
        self.assertEqual(len(new), 1, r.stdout)
        self.assertIn("AGX-7 77777777", new[0])

    def test_unread_count_and_state(self):
        self.operator("AGX", [])
        self.sync_md([self.review(1, "REVIEW: APPROVED none"),
                      "| %s | SPV | `sync/t` | `RULE` | per AGX: leggi |\n" % stamp(2)])
        out = run(self.root).stdout
        line = [ln for ln in out.splitlines() if ln.startswith("AGX")][0]
        self.assertIn("WORKING", line)
        self.assertIn("unread 1", line)

    def test_review_rows_cut_and_budget(self):
        names = ["OP%d" % i for i in range(12)]
        for n in names:
            self.operator(n, ["DELIVERED %s-%d @ b@%08x · verifica: x -> ok · 2026-10-17T09:00+02:00" % (n, k, k)
                              for k in range(1, 4)])
        with open(os.path.join(self.root, "sync", "operators.toml"), "a", encoding="utf-8") as f:
            f.write("".join('\n[[operator]]\nid = "%s"\nkind = "ai"\n' % n for n in names))
        self.sync_md([self.review(i + 1, "REVIEW: APPROVED " + "x" * 600) for i in range(5)])
        r = run(self.root)
        lines = r.stdout.splitlines()
        self.assertLessEqual(len(lines), 25, r.stdout)
        review_lines = lines[-3:]
        for ln in review_lines:
            self.assertLessEqual(len(ln), 100)
        self.assertTrue(review_lines[0].startswith("| "))

    def test_no_file_for_operator_is_reported(self):
        r = run(self.root)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("no sync/AGX.md", r.stdout)


if __name__ == "__main__":
    unittest.main()
