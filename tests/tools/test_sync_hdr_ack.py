"""tools/sync_hdr.py: a call that writes the status block sets `ack` to the last SYNC.md row when --ack is missing (SYNC-EN-3)."""
import contextlib
import importlib.util
import io
import os
import re
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "sync_hdr.py")
ROW1 = "2026-10-20T09:00+02:00"
ROW2 = "2026-10-20T10:00+02:00"
OLD = "2026-10-19T08:00+02:00"
HEAD = "| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\n|---|---|---|---|---|\n"


def load():
    spec = importlib.util.spec_from_file_location("sync_hdr_under_test", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.root = self._td.name
        os.makedirs(os.path.join(self.root, "sync"))
        self.hdr = os.path.join(self.root, "sync", "OP.md")
        with open(self.hdr, "w", encoding="utf-8", newline="") as f:
            f.write("---\nschema: 1\nid: OP\nstate: WORKING\ntask: t\nack: %s\nupdated: x\n---\n# OP\n" % OLD)
        self.mod = load()
        self.mod.ROOT = self.root

    def tearDown(self):
        self._td.cleanup()

    def sync_md(self, *rows):
        with open(os.path.join(self.root, "SYNC.md"), "w", encoding="utf-8") as f:
            f.write(HEAD + "".join("| %s | SPV | `sync/t` | `NOTE` | %s |\n" % (t, t[11:16]) for t in rows))

    def call(self, *args):
        with mock.patch.object(sys, "argv", ["sync_hdr.py", "OP"] + list(args)), \
                contextlib.redirect_stdout(io.StringIO()):
            self.mod.main()
        with open(self.hdr, encoding="utf-8") as f:
            return re.search(r"^ack: *(.*)$", f.read(), re.M).group(1).strip()

    def test_each_writing_call_sets_ack_to_the_last_row(self):
        self.sync_md(ROW1, ROW2)
        for args in (["--state", "WORKING"], ["--task", "next"], ["--eta", "18:00"], ["--progress", "40"]):
            with open(self.hdr, "r+", encoding="utf-8", newline="") as f:      # back to the old ack
                txt = re.sub(r"^ack: .*$", "ack: " + OLD, f.read(), flags=re.M)
                f.seek(0)
                f.truncate()
                f.write(txt)
            self.assertEqual(self.call(*args), ROW2, args)

    def test_empty_sync_md_keeps_the_old_ack(self):
        self.sync_md()
        self.assertEqual(self.call("--task", "x"), OLD)

    def test_explicit_iso_wins(self):
        self.sync_md(ROW1, ROW2)
        self.assertEqual(self.call("--task", "x", "--ack", ROW1), ROW1)

    def test_auto_still_works(self):
        self.sync_md(ROW1, ROW2)
        self.assertEqual(self.call("--ack", "auto"), ROW2)


if __name__ == "__main__":
    unittest.main()
