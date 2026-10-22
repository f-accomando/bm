#!/usr/bin/env python3
"""Unit tests for scripts/translate_check.py (python3 tests/tools/test_translate_check.py)."""
import contextlib
import io
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import translate_check  # noqa: E402

SRC = """# Titolo

Questo è il testo di prova con 3 elementi e il comando `make test`.

## Sezione

Vedi [la guida](docs/guida.md) e [sopra](#sezione).

| Nome | Valore |
|---|---|
| a | 1 |

```sh
# commento in italiano
make test
echo "ciao"
```
"""

DST = """# Title

This is the sample text with 3 items and the command `make test`.

## Section

See [the guide](docs/guida.md) and [above](#section).

| Name | Value |
|---|---|
| a | 1 |

```sh
# comment in English
make test
echo "ciao"
```
"""


def run(src, dst, *extra):
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for name, text in (("src.md", src), ("dst.md", dst)):
            p = os.path.join(d, name)
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            paths.append(p)
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = translate_check.main(paths + list(extra))
        return code, out.getvalue()


class TranslateCheck(unittest.TestCase):
    def test_perfect_pair(self):
        code, out = run(SRC, DST)
        self.assertEqual(code, 0, out)
        self.assertEqual(out, "")

    def test_missing_heading(self):
        code, out = run(SRC, DST.replace("## Section\n\n", ""))
        self.assertEqual(code, 1)
        self.assertIn("heading", out)

    def test_changed_code_block(self):
        code, out = run(SRC, DST.replace("make test\necho", "make all\necho"))
        self.assertEqual(code, 1)
        self.assertIn("code block content differs", out)

    def test_translated_code_comment_ok(self):
        code, out = run(SRC, DST.replace("# comment in English", "# another comment, longer"))
        self.assertEqual(code, 0, out)

    def test_changed_link_target(self):
        code, out = run(SRC, DST.replace("docs/guida.md", "docs/guide.md"))
        self.assertEqual(code, 1)
        self.assertIn("link target differs", out)

    def test_changed_number(self):
        code, out = run(SRC, DST.replace("with 3 items", "with 4 items"))
        self.assertEqual(code, 1)
        self.assertIn("number", out)

    def test_line_left_in_italian(self):
        dst = DST.replace("This is the sample text with 3 items and the command `make test`.",
                          "Questo è il testo di prova con 3 elementi e `make test` per la verifica.")
        code, out = run(SRC, dst)
        self.assertEqual(code, 1)
        self.assertIn("left in Italian", out)

    def test_glossary_warning_and_strict(self):
        with tempfile.TemporaryDirectory() as d:
            g = os.path.join(d, "g.tsv")
            with open(g, "w", encoding="utf-8") as f:
                f.write("# comment\nprova\ttrial\n")
            code, out = run(SRC, DST, "--glossary", g)
            self.assertEqual(code, 0, out)
            self.assertIn("glossary", out)
            code, out = run(SRC, DST, "--glossary", g, "--strict")
            self.assertEqual(code, 1)

    def test_glossary_satisfied(self):
        with tempfile.TemporaryDirectory() as d:
            g = os.path.join(d, "g.tsv")
            with open(g, "w", encoding="utf-8") as f:
                f.write("testo di prova\tsample text\n")
            code, out = run(SRC, DST, "--glossary", g, "--strict")
            self.assertEqual(code, 0, out)

    def test_trailing_comment_and_label_translation_ok(self):
        src = SRC.replace("make test\necho", "make test -- prova\necho").replace("`make test`", "`make test` e `<nome>`")
        dst = DST.replace("make test\necho", "make test -- trial run\necho").replace("`make test`", "`make test` and `<name>`")
        code, out = run(src, dst)
        self.assertEqual(code, 0, out)

    def test_changed_path_in_backticks(self):
        code, out = run(SRC.replace("`make test`", "`docs/a.md`"), DST.replace("`make test`", "`docs/b.md`"))
        self.assertEqual(code, 1)
        self.assertIn("identifier", out)

    def test_missing_file(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(translate_check.main(["/nonexistent/a.md", "/nonexistent/b.md"]), 2)


if __name__ == "__main__":
    unittest.main()
