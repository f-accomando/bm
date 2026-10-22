"""tools/sync_check.py reads the English labels and the Italian ones (SYNC-EN-1): `## Deliveries` / `## Consegne`,
`verify:` / `verifica:`, `## Closing` / `## Chiusura`, `open:` / `aperti:`, `none` / `nessuno`."""
import os
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import sync_check as sc  # noqa: E402

ISO = "2026-10-20T10:00+02:00"
DELIVERED = "DELIVERED XX-1 @ claude/x@abc1234 · %s: t -> ok · %s"
CLOSED = "CLOSED @ claude/x@abc1234 · tree clean · %s: %s · " + ISO


def block(oid, section_d, section_c, delivered, closed):
    return ("---\nschema: 1\nid: %s\nstate: DONE\nupdated: %s\n---\n# %s\n\n## %s\n%s\n\n## %s\n%s\n"
            % (oid, ISO, oid, section_d, delivered, section_c, closed))


class T(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.root = self._td.name
        os.makedirs(os.path.join(self.root, "sync", "inbox"))
        with open(os.path.join(self.root, "sync", "operators.toml"), "w", encoding="utf-8") as f:
            f.write('schema = 1\n\n[[operator]]\nid = "SPV"\nkind = "ai"\n\n[[operator]]\nid = "AAA"\nkind = "ai"\n')
        with open(os.path.join(self.root, "SYNC.md"), "w", encoding="utf-8") as f:
            f.write("| Timestamp | Operatore | Branch / Commit | Azione | Messaggio |\n|---|---|---|---|---|\n")
        self._root = sc.ROOT
        sc.ROOT = self.root

    def tearDown(self):
        sc.ROOT = self._root
        self._td.cleanup()

    def test_delivered_line_in_both_languages(self):
        for label in ("verify", "verifica"):
            self.assertTrue(sc.DELIVERED.match(DELIVERED % (label, ISO)), label)
        self.assertFalse(sc.DELIVERED.match(DELIVERED % ("check", ISO)))

    def test_closed_line_in_both_languages(self):
        for label, value in (("open", "none"), ("aperti", "nessuno"), ("open", "XX-3"), ("aperti", "XX-3")):
            self.assertTrue(sc.CLOSED.match(CLOSED % (label, value)), label)
        self.assertFalse(sc.CLOSED.match(CLOSED % ("left", "none")))

    def test_sections_in_both_languages(self):
        for sd, sc_ in (("Deliveries", "Closing"), ("Consegne", "Chiusura")):
            text = block("AAA", sd, sc_, DELIVERED % ("verify", ISO), CLOSED % ("open", "none"))
            self.assertEqual(sc.consegne(text), [DELIVERED % ("verify", ISO)], sd)
            self.assertTrue(sc.has_closed_line(text), sc_)

    def test_both_delivery_sections_are_joined(self):
        text = ("## Consegne\n" + DELIVERED % ("verifica", ISO) + "\n\n## Deliveries\n"
                + (DELIVERED % ("verify", ISO)).replace("XX-1", "XX-2") + "\n")
        self.assertEqual(len(sc.consegne(text)), 2)

    def test_template_has_no_delivery_problem(self):
        # SYNC-EN-5: the English template used as an operator file: every line of its Deliveries
        # section is a note in brackets or a standard DELIVERED line, and the sections are English
        path = os.path.join(HERE, "..", "..", "sync", "_template.md")
        with open(path, encoding="utf-8") as f:
            text = f.read().replace("{ID}", "AAA")
        lines = sc.consegne(text)
        self.assertTrue(lines, "the template has no Deliveries section")
        bad = [ln for ln in lines if not ln.startswith("(") and not sc.DELIVERED.match(ln)]
        self.assertEqual(bad, [])
        for heading in ("## Profile", "## Requirements", "## Deliveries", "## Progress", "## Problems", "## Requests"):
            self.assertIn(heading + "\n", text)
        for word in ("Profilo", "Requisiti", "Consegne", "verifica", "utente"):
            self.assertNotIn(word, text)

    def leftovers(self, closed):
        with open(os.path.join(self.root, "sync", "AAA.md"), "w", encoding="utf-8") as f:
            f.write(block("AAA", "Deliveries", "Closing", DELIVERED % ("verify", ISO), closed))
        with mock.patch.object(sc, "git", side_effect=lambda *a: None):
            return sc.leftovers(sc.rows(os.path.join(self.root, "SYNC.md")), sc.toml_ops())

    def test_open_none_and_nessuno_are_not_reported_open_item_is(self):
        for label, value in (("open", "none"), ("aperti", "nessuno")):
            out = self.leftovers(CLOSED % (label, value))
            self.assertFalse([e for e in out if "closed with open items" in e], (label, out))
        out = self.leftovers(CLOSED % ("open", "XX-3"))
        self.assertIn("  > AAA: closed with open items: XX-3", out)
        out = self.leftovers(CLOSED % ("aperti", "XX-3"))
        self.assertIn("  > AAA: closed with open items: XX-3", out)


if __name__ == "__main__":
    unittest.main()
