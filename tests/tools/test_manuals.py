"""Tests for scripts/manuals.py: split_chapters, signature, technical and the gap counter."""
import os, sys, tempfile, unittest, warnings

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import manuals  # noqa: E402  (needs Python 3.11+: tomllib)

# split_chapters opens its file without closing it; that is the script's, not what is tested here
warnings.simplefilter("ignore", ResourceWarning)

FENCE = "`" * 3

CHAPTER_EN = f"""\
# Start

Some prose with `foo()` and `a/b.md` in it.

{FENCE}lua
-- a comment
local s = "hello"
print(s) -- say it
{FENCE}

| name | value |
|------|-------|
| a    | 1     |

- first item
1. first step
"""

# the same shape: other prose, other comments, other string text; the technical spans are the same
CHAPTER_IT = f"""\
# Inizio

Un po' di prosa con `foo()` e `a/b.md` dentro.

{FENCE}lua
-- un commento
local s = "ciao"
print(s) -- dillo
{FENCE}

| nome | valore |
|------|--------|
| b    | 2      |

- primo punto
1. primo passo
"""


class ManualsTest(unittest.TestCase):
    def test_split_chapters_ignores_hash_inside_fence(self):
        text = f"head line\n# One\ntext\n{FENCE}\n# not a chapter\n{FENCE}\n# Two\nmore\n"
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "m.md")
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            chapters = manuals.split_chapters(path)
        self.assertEqual([t for t, _ in chapters], ["", "One", "Two"])
        self.assertIn("# not a chapter", chapters[1][1])
        self.assertNotIn("# not a chapter", chapters[2][1])

    def test_signature_same_for_translated_pair(self):
        self.assertEqual(manuals.signature(CHAPTER_EN), manuals.signature(CHAPTER_IT))

    def test_signature_differs_with_extra_table_row(self):
        longer = CHAPTER_IT.replace("| b    | 2      |\n", "| b    | 2      |\n| c    | 3      |\n")
        self.assertNotEqual(manuals.signature(longer), manuals.signature(CHAPTER_EN))

    def test_signature_differs_when_a_technical_span_changes(self):
        other = CHAPTER_IT.replace("`foo()`", "`bar()`")
        self.assertNotEqual(manuals.signature(other), manuals.signature(CHAPTER_EN))

    def test_technical(self):
        self.assertTrue(manuals.technical("foo()"))
        self.assertTrue(manuals.technical("a/b.md"))
        self.assertFalse(manuals.technical("key=value"))
        self.assertFalse(manuals.technical("<x>"))

    def test_gap_counts_both_languages(self):
        text = "one [TO FILL: a] and two [DA COMPLETARE: b] and plain [text]"
        _sig, _spans, gaps = manuals.signature(text)
        self.assertEqual(gaps, 2)
        self.assertEqual(len(manuals.GAP.findall(text)), 2)


if __name__ == "__main__":
    unittest.main()
