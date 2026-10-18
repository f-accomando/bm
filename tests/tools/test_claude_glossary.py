"""Every Glossary row of CLAUDE.md names a grep command: its file must exist and hold the heading."""
import os, re, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CMD = re.compile(r"`grep -n -A \d+ '\^(#+ [^']+)' (\S+)`")


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf8") as fh:
        return fh.read().splitlines()


def glossary_rows():
    rows, inside = [], False
    for line in read("CLAUDE.md"):
        if line.startswith("## "):
            inside = line.strip() == "## Glossary"
        elif inside and line.startswith("|") and not line.startswith("|---"):
            cells = [c.strip() for c in line.strip("|").split("|")]
            if cells[0] != "Term":
                rows.append(cells)
    return rows


class ClaudeGlossaryTest(unittest.TestCase):
    def test_has_rows(self):
        self.assertGreaterEqual(len(glossary_rows()), 10)

    def test_every_row_points_to_an_existing_heading(self):
        for term, _meaning, command in glossary_rows():
            m = CMD.fullmatch(command)
            self.assertIsNotNone(m, f"{term}: command not in the expected form: {command}")
            heading, rel = m.group(1), m.group(2)
            self.assertTrue(os.path.isfile(os.path.join(ROOT, rel)), f"{term}: missing file {rel}")
            starts = [ln for ln in read(rel) if ln.startswith(heading)]
            self.assertTrue(starts, f"{term}: heading '{heading}' not found in {rel}")

    def test_size_budget(self):
        self.assertLessEqual(os.path.getsize(os.path.join(ROOT, "CLAUDE.md")), 7500)


if __name__ == "__main__":
    unittest.main()
