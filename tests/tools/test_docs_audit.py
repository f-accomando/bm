"""Tests for `docs_audit.py check`: it must list the broken links of the tracking docs and exit 1."""
import contextlib, io, os, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "scripts"))
import docs_audit


def put(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8", newline="\n") as fh:
        fh.write(text)


def run_check():
    out = io.StringIO()
    code = 0
    with contextlib.redirect_stdout(out):
        try:
            docs_audit.cmd_check()
        except SystemExit as e:
            code = e.code
    return code, out.getvalue()


def main():
    old_root = docs_audit.ROOT
    try:
        with tempfile.TemporaryDirectory() as tmp:
            docs_audit.ROOT = tmp
            put(tmp, "CLAUDE.md", "# Notes\nThe index is docs/progress.md\n")
            put(tmp, "docs/progress.md", "# Index\n- [area a](progress/a.md)\n")
            put(tmp, "docs/progress/a.md", "# A\nSee [gone](missing.md).\n")
            code, out = run_check()
            assert code == 1, f"exit {code}, expected 1 (output: {out!r})"
            lines = [l for l in out.splitlines() if l.startswith("  !")]
            assert len(lines) == 1, f"expected 1 message, got {lines}"
            assert "docs/progress/missing.md" in lines[0], lines[0]
            assert len(docs_audit.broken(docs_audit.tracked())) == 1
            put(tmp, "docs/progress/a.md", "# A\nNothing broken here.\n")
            code, out = run_check()
            assert code == 0, f"exit {code}, expected 0 (output: {out!r})"
            assert "docs_audit: 0 broken" in out, out
            assert docs_audit.broken(docs_audit.tracked()) == []
    finally:
        docs_audit.ROOT = old_root
    print("All docs_audit tests PASSED")


if __name__ == "__main__":
    main()
