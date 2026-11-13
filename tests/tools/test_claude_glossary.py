"""Every Glossary term of CLAUDE.md has a heading in docs/agents/routing.md (the file the Glossary names)."""
import os, re, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "core", "scripts"))
from docs_audit import CLAUDE_MD_LIMIT  # noqa: E402  (the limit lives in one place)
ROUTING = "docs/agents/routing.md"
TERM = re.compile(r"^- `([^`]+)`:")     # a Glossary line: - `Term`: what it is


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf8") as fh:
        return fh.read().splitlines()


def glossary_terms():
    terms, inside = [], False
    for line in read("CLAUDE.md"):
        if line.startswith("## "):
            inside = line.strip() == "## Glossary"
        elif inside:
            m = TERM.match(line)
            if m:
                terms.append(m.group(1))
    return terms


class ClaudeGlossaryTest(unittest.TestCase):
    def test_has_terms(self):
        self.assertGreaterEqual(len(glossary_terms()), 7)

    def test_every_term_has_a_heading(self):
        headings = [ln for ln in read(ROUTING) if ln.startswith("### ")]
        for term in glossary_terms():
            self.assertTrue(any(h.startswith("### " + term) for h in headings),
                            f"{term}: heading '### {term}' not found in {ROUTING}")

    def test_size_budget(self):
        self.assertLessEqual(os.path.getsize(os.path.join(ROOT, "CLAUDE.md")), CLAUDE_MD_LIMIT)


if __name__ == "__main__":
    unittest.main()
