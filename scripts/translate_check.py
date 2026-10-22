#!/usr/bin/env python3
"""Structural parity check between a source document and its translation.

    python scripts/translate_check.py SRC DST [--glossary PATH] [--strict] [--lenient-code] [--reverse]

Checks (Markdown):
  - same number and levels of headings (`#`)
  - same number of fenced code blocks; block contents identical line by line, except comments (whole lines
    starting with `#`, `//`, `--`, and trailing ` -- text`); mermaid labels are ignored; --lenient-code also
    ignores the text inside quotes
  - same Markdown links: same targets in the same order (link texts may change). Pure `#anchor`
    targets may differ but must match a heading of DST. `path#fragment` compares the path only.
  - same number of table rows and columns per row
  - same numbers in prose (digit sequences, as a multiset)
  - same inline `backtick` identifiers (multiset; a span translated keeping its punctuation, like a UI label
    or a `<placeholder>`, pairs with its source; paths and file names must match exactly)
  - no line left in Italian: 3 or more frequent Italian function words outside code (skipped with --reverse)
  - --glossary TSV (`italian<TAB>english`): source terms whose translation is missing in DST are
    warnings, once per term (errors with --strict). With --reverse the columns are read swapped.

Exit: 0 no errors, 1 structural differences, 2 bad usage or missing file.
Output: one line per problem, `SRC:line: DST:line: reason` (`-` when the line is unknown).
"""
import argparse
import difflib
import os
import re
import sys
from collections import Counter

FENCE = re.compile(r"^\s*(`{3,}|~{3,})(.*)$")
HEADING = re.compile(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
INLINE = re.compile(r"(?<!`)(`+)(?!`)(.+?)(?<!`)\1(?!`)")
LINK = re.compile(r"(?<!!)\[(?:[^\]\\]|\\.)*\]\(\s*<?([^)\s>]+)>?(?:\s+(?:\"[^\"]*\"|'[^']*'))?\s*\)")
URL = re.compile(r"https?://\S+")
SEP_ROW = re.compile(r"^\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$")
WORD = re.compile(r"[a-zàèéìòù]+(?:['’\-][a-zàèéìòù]+)*")
IT_WORDS = frozenset(
    "il lo la gli le un una uno che di del dello della dei degli delle per non con sono nel nella nei nelle "
    "questo questa questi queste anche più è si dove quando tutti tutte ogni solo mai essere viene vengono "
    "deve devono può possono sul sulla sui sulle alla alle agli allo tra fra quindi perché già stato cui "
    "sia sua suo loro nello dal dalla dai dalle fa ha hanno".split()
)
IT_MIN_HITS = 3


class Doc:
    def __init__(self, path):
        with open(path, encoding="utf-8-sig", errors="replace") as f:
            self.lines = f.read().splitlines()
        self.headings = []      # (line, level, title)
        self.blocks = []        # (open_line, info, [(line, text)])
        self.prose = []         # (line, text) outside fences
        fence = None            # (char, length)
        cur = None
        for n, text in enumerate(self.lines, 1):
            m = FENCE.match(text)
            if fence is None:
                if m:
                    fence = (m.group(1)[0], len(m.group(1)))
                    cur = (n, m.group(2).strip(), [])
                    continue
                self.prose.append((n, text))
                h = HEADING.match(text)
                if h:
                    self.headings.append((n, len(h.group(1)), h.group(2)))
            else:
                if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= fence[1] and not m.group(2).strip():
                    self.blocks.append(cur)
                    fence, cur = None, None
                else:
                    cur[2].append((n, text))
        if cur is not None:     # unclosed fence: keep what was collected
            self.blocks.append(cur)
            self.unclosed = True
        else:
            self.unclosed = False
        self.tables = [(n, len(re.split(r"(?<!\\)\|", t.strip().strip("|")))) for n, t in self.prose
                       if t.lstrip().startswith("|") and not SEP_ROW.match(t.strip())]
        self.slugs = {slug(t) for _, _, t in self.headings}

    def stripped(self, text):
        return INLINE.sub(" ", text)

    def links(self):
        out = []
        for n, t in self.prose:
            for m in LINK.finditer(self.stripped(t)):
                out.append((n, m.group(1)))
        return out

    def codes(self):
        out = []
        for n, t in self.prose:
            for m in INLINE.finditer(t):
                out.append((n, m.group(2).strip()))
        return out

    def numbers(self):
        out = []
        for n, t in self.prose:
            s = LINK.sub(" ", self.stripped(t))
            s = URL.sub(" ", s)
            s = re.sub(r"(?<=\d)[.,   ](?=\d{3}\b)", "", s)
            for m in re.finditer(r"\d+", s):
                out.append((n, m.group(0)))
        return out


def slug(title):
    t = INLINE.sub(lambda m: m.group(2), title).lower()
    t = re.sub(r"[^\w\- ]", "", t, flags=re.U)
    return t.strip().replace(" ", "-")


TRAILING_COMMENT = re.compile(r"\s+(?:--|//|#)\s.*$")


def strip_comment_lines(lines):
    """Drop leading-comment lines; cut a trailing ` -- text`, ` // text`, ` # text` from code lines."""
    out = []
    for n, t in lines:
        s = t.strip()
        if s.startswith("#!") or not (s.startswith("#") or s.startswith("//") or s.startswith("--")):
            out.append((n, TRAILING_COMMENT.sub("", t.rstrip())))
    return out


def mermaid(t):
    return re.sub(r"\[[^\]]*\]|\([^)]*\)|\"[^\"]*\"|\|[^|]*\|", "[]", t)


def lenient(t):
    return re.sub(r"\"[^\"]*\"|'[^']*'", "''", t)


def first_line(items, key):
    for n, v in items:
        if v == key:
            return n
    return "-"


def multiset_diff(a, b, label, rep):
    ca, cb = Counter(v for _, v in a), Counter(v for _, v in b)
    for v in sorted(set(ca) | set(cb)):
        if ca[v] != cb[v]:
            rep("%s" % first_line(a, v), "%s" % first_line(b, v),
                "%s `%s`: %dx in SRC, %dx in DST" % (label, v, ca[v], cb[v]))


def pathlike(v):
    return (re.search(r"\S/\S", v) is not None or re.search(r"\w\.\w", v) is not None
            or re.search(r"[A-Za-z]:\\|\\\w+\\", v) is not None)


def skeleton(v):
    """A translated label keeps its punctuation: letter runs collapse to `w`; placeholders `<...>` to `<>`."""
    v = re.sub(r"<[^>]*>", "<>", v)
    if pathlike(v):
        return v
    return re.sub(r"[^\W\d]+", "w", v)


def identifier_diff(a, b, rep):
    """Spans only in SRC or only in DST are paired by skeleton (a translated label or placeholder is not a difference)."""
    ca, cb = Counter(v for _, v in a), Counter(v for _, v in b)
    only_a = Counter({v: ca[v] - cb[v] for v in ca if ca[v] > cb[v]})
    only_b = Counter({v: cb[v] - ca[v] for v in cb if cb[v] > ca[v]})
    by_skel = {}
    for v, n in only_b.items():
        by_skel.setdefault(skeleton(v), []).append([v, n])
    for v in sorted(only_a):
        for _ in range(only_a[v]):
            pool = [e for e in by_skel.get(skeleton(v), []) if e[1] > 0]
            if pool:
                pool[0][1] -= 1
                only_b[pool[0][0]] -= 1
            else:
                rep(first_line(a, v), "-", "identifier `%s` in SRC missing in DST (counts: SRC %dx, DST %dx)" % (v, ca[v], cb[v]))
    for v in sorted(only_b):
        for _ in range(max(only_b[v], 0)):
            rep("-", first_line(b, v), "identifier `%s` extra in DST (counts: SRC %dx, DST %dx)" % (v, ca[v], cb[v]))


def load_glossary(path, reverse):
    pairs = []
    with open(path, encoding="utf-8-sig") as f:
        for raw in f:
            raw = raw.rstrip("\r\n")
            if not raw.strip() or raw.lstrip().startswith("#") or "\t" not in raw:
                continue
            a, b = raw.split("\t")[:2]
            a, b = a.strip(), b.strip()
            if a and b:
                pairs.append((b, a) if reverse else (a, b))
    return pairs


def term_re(term, suffix):
    return re.compile(r"(?<!\w)" + re.escape(term) + (r"(?:s|es)?" if suffix else "") + r"(?!\w)", re.I)


def run(args):
    errors, warns = [], []

    def err(sl, dl, msg):
        errors.append("%s:%s: %s:%s: %s" % (args.src, sl, args.dst, dl, msg))

    def warn(sl, dl, msg):
        warns.append("%s:%s: %s:%s: WARN %s" % (args.src, sl, args.dst, dl, msg))

    s, d = Doc(args.src), Doc(args.dst)

    # headings
    sh = [(n, lv) for n, lv, _ in s.headings]
    dh = [(n, lv) for n, lv, _ in d.headings]
    sm = difflib.SequenceMatcher(None, [lv for _, lv in sh], [lv for _, lv in dh], autojunk=False)
    titles_s = {n: t for n, _, t in s.headings}
    titles_d = {n: t for n, _, t in d.headings}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        sl = sh[i1][0] if i1 < len(sh) else (sh[-1][0] if sh else "-")
        dl = dh[j1][0] if j1 < len(dh) else (dh[-1][0] if dh else "-")
        if tag == "delete":
            err(sl, dl, "heading missing in DST: %s" % "; ".join('"%s" (level %d)' % (titles_s[n], lv) for n, lv in sh[i1:i2]))
        elif tag == "insert":
            err(sl, dl, "extra heading in DST: %s" % "; ".join('"%s" (level %d)' % (titles_d[n], lv) for n, lv in dh[j1:j2]))
        else:
            err(sl, dl, "heading level differs: SRC %s, DST %s" % (
                ",".join(str(lv) for _, lv in sh[i1:i2]), ",".join(str(lv) for _, lv in dh[j1:j2])))
    if len(sh) != len(dh):
        err("-", "-", "heading count: %d in SRC, %d in DST" % (len(sh), len(dh)))

    # fenced blocks
    if s.unclosed:
        err(s.blocks[-1][0], "-", "unclosed code fence in SRC")
    if d.unclosed:
        err("-", d.blocks[-1][0], "unclosed code fence in DST")
    if len(s.blocks) != len(d.blocks):
        err(s.blocks[min(len(d.blocks), len(s.blocks) - 1)][0] if s.blocks else "-",
            d.blocks[min(len(s.blocks), len(d.blocks) - 1)][0] if d.blocks else "-",
            "code block count: %d in SRC, %d in DST" % (len(s.blocks), len(d.blocks)))
    for bs, bd in zip(s.blocks, d.blocks):
        if bs[1].split()[:1] != bd[1].split()[:1]:
            err(bs[0], bd[0], "code block language differs: `%s` vs `%s`" % (bs[1], bd[1]))
        ls, ld = strip_comment_lines(bs[2]), strip_comment_lines(bd[2])
        if bs[1].split()[:1] == ["mermaid"]:
            ls, ld = [(n, mermaid(t)) for n, t in ls], [(n, mermaid(t)) for n, t in ld]
        elif args.lenient_code:
            ls, ld = [(n, lenient(t)) for n, t in ls], [(n, lenient(t)) for n, t in ld]
        sm = difflib.SequenceMatcher(None, [t for _, t in ls], [t for _, t in ld], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal":
                continue
            sl = ls[i1][0] if i1 < len(ls) else bs[0]
            dl = ld[j1][0] if j1 < len(ld) else bd[0]
            a = " / ".join(t.strip() for _, t in ls[i1:i2])[:80]
            b = " / ".join(t.strip() for _, t in ld[j1:j2])[:80]
            err(sl, dl, "code block content differs: SRC `%s`, DST `%s`" % (a, b))

    # links
    lk_s, lk_d = s.links(), d.links()
    if len(lk_s) != len(lk_d):
        err("-", "-", "link count: %d in SRC, %d in DST" % (len(lk_s), len(lk_d)))
    for (sl, ts), (dl, td) in zip(lk_s, lk_d):
        if ts.startswith("#") or td.startswith("#"):
            if ts.startswith("#") != td.startswith("#"):
                err(sl, dl, "link target differs: %s vs %s" % (ts, td))
            elif td[1:] not in d.slugs:
                err(sl, dl, "anchor %s does not match any heading of DST (SRC had %s)" % (td, ts))
        elif ts.split("#")[0] != td.split("#")[0]:
            err(sl, dl, "link target differs: %s vs %s" % (ts, td))

    # tables
    if len(s.tables) != len(d.tables):
        err(s.tables[0][0] if s.tables else "-", d.tables[0][0] if d.tables else "-",
            "table row count: %d in SRC, %d in DST" % (len(s.tables), len(d.tables)))
    sm = difflib.SequenceMatcher(None, [c for _, c in s.tables], [c for _, c in d.tables], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "replace" and (i2 - i1) == (j2 - j1):
            for k in range(i2 - i1):
                err(s.tables[i1 + k][0], d.tables[j1 + k][0], "table columns differ: %d in SRC, %d in DST" %
                    (s.tables[i1 + k][1], d.tables[j1 + k][1]))
        elif tag != "equal" and len(s.tables) == len(d.tables):
            err(s.tables[i1][0] if i1 < len(s.tables) else "-", d.tables[j1][0] if j1 < len(d.tables) else "-",
                "table column layout differs")

    # numbers and identifiers
    multiset_diff(s.numbers(), d.numbers(), "number", err)
    identifier_diff(s.codes(), d.codes(), err)

    # Italian residue
    if not args.reverse:
        for n, t in d.prose:
            if SEP_ROW.match(t.strip()):
                continue
            x = URL.sub(" ", LINK.sub(" ", INLINE.sub(" ", t)))
            hits = [w for w in WORD.findall(x.lower()) if w in IT_WORDS]
            if len(hits) >= IT_MIN_HITS:
                err("-", n, "line left in Italian (%s)" % " ".join(hits[:6]))

    # glossary
    if args.glossary:
        src_text = "\n".join(INLINE.sub(" ", t) for _, t in s.prose)
        dst_text = "\n".join(INLINE.sub(" ", t) for _, t in d.prose)
        for a, b in load_glossary(args.glossary, args.reverse):
            m = term_re(a, False).search(src_text)
            if m and not term_re(b, True).search(dst_text):
                line = next((n for n, t in s.prose if term_re(a, False).search(INLINE.sub(" ", t))), "-")
                warn(line, "-", "glossary: `%s` in SRC, expected `%s` in DST" % (a, b))
    return errors, warns


def main(argv=None):
    for st in (sys.stdout, sys.stderr):
        if hasattr(st, "reconfigure"):
            st.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="structural parity check of a translation")
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--glossary", help="TSV `italian<TAB>english`")
    ap.add_argument("--strict", action="store_true", help="glossary warnings are errors")
    ap.add_argument("--lenient-code", action="store_true", help="in code blocks also ignore the text inside quotes")
    ap.add_argument("--reverse", action="store_true", help="EN to IT: swap glossary columns, skip the Italian-residue check")
    args = ap.parse_args(argv)
    for p in (args.src, args.dst) + ((args.glossary,) if args.glossary else ()):
        if not os.path.isfile(p):
            print("translate_check: file not found: %s" % p, file=sys.stderr)
            return 2
    errors, warns = run(args)
    for line in errors + warns:
        print(line)
    print("translate_check: %d error(s), %d warning(s)" % (len(errors), len(warns)), file=sys.stderr)
    return 1 if errors or (args.strict and warns) else 0


if __name__ == "__main__":
    sys.exit(main())
