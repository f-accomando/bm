"""tools/spv_audit.py credits ledger rows to an operator: from the `operator` field (`OP-<ID>`) or, when the row has none, from
the file name `sync/<ID>.ledger.jsonl` (SYNC-EN-0-fix); `IDLE` is no longer a state of sync_hdr."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, TOOLS)
import sync_hdr  # noqa: E402

HEAD = "| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\n|---|---|---|---|---|\n"
DONE = "2026-10-20T10:00+02:00"
TAKEN = "2026-10-20T10:10+02:00"


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.root = self._td.name
        os.makedirs(os.path.join(self.root, "sync"))
        with open(os.path.join(self.root, "SYNC.md"), "w", encoding="utf-8") as f:
            f.write(HEAD + "| %s | CLD | `b/x` | `NOTE` | preso WO XX-2 |\n" % TAKEN)

    def tearDown(self):
        self._td.cleanup()

    def audit(self, name, row):
        with open(os.path.join(self.root, "sync", name), "w", encoding="utf-8") as f:
            f.write(json.dumps(row) + "\n")
        r = subprocess.run([sys.executable, os.path.join(TOOLS, "spv_audit.py"), "--repo", self.root],
                           capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout

    def test_row_without_operator_counts_for_the_file_name_id(self):
        self.assertIn("(1 gaps)", self.audit("CLD.ledger.jsonl", {"iso": DONE, "wo": "XX-1"}))

    def test_op_prefixed_file_and_field_still_count(self):
        self.assertIn("(1 gaps)", self.audit("OP-CLD.ledger.jsonl", {"iso": DONE, "operator": "OP-CLD", "wo": "XX-1"}))

    def test_row_of_another_operator_does_not_count_for_cld(self):
        self.assertNotIn("(1 gaps)", self.audit("ZZZ.ledger.jsonl", {"iso": DONE, "wo": "XX-1"}))

    def test_idle_is_not_a_state(self):
        self.assertNotIn("IDLE", sync_hdr.STATES)


if __name__ == "__main__":
    unittest.main()
