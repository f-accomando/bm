#!/usr/bin/env python3
"""
manuals.py - the bm manuals in English and Italian, kept together (docs/MANUALS.md).

  scripts/manuals.py status                  one line per manual: gaps, stamps, sources changed
  scripts/manuals.py check [ID ...]          same structure in en and it, and neither behind the other
  scripts/manuals.py stamp ID ...            records that en and it say the same now (after check)
  scripts/manuals.py build [ID ...] [--theme paper] [-o DIR]    PDFs: DIR/ID.en.pdf, DIR/ID.it.pdf

The content is manuals/content/<lang>/<id>.md. The two languages are the same text: the same chapters, sections,
steps, tables, boxes, pictures and code (comments apart), and the same gaps. `check` finds, chapter by chapter,
which side was changed after the last `stamp`: "it is behind" or "en is behind". Needs reportlab for `build`.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
MAN = os.path.join(ROOT, "manuals")
SYNC = os.path.join(MAN, "sync.json")
LANGS = ("en", "it")
GAP = re.compile(r"\[(?:TO FILL|DA COMPLETARE):")


def manuals():
    with open(os.path.join(MAN, "manuals.toml"), "rb") as f:
        return sorted(tomllib.load(f)["manual"], key=lambda m: m["order"])


def path_of(mid, lang):
    return os.path.join(MAN, "content", lang, mid + ".md")


def h(text):
    return hashlib.sha1(text.strip().replace("\r", "").encode("utf-8")).hexdigest()[:12]


def split_chapters(path):
    """[(title, text)]: the head (title "") then each '# ' chapter"""
    out, title, buf, fence = [], "", [], False
    for ln in open(path, encoding="utf-8").read().replace("\r", "").split("\n"):
        if ln.startswith("```"):
            fence = not fence
        if ln.startswith("# ") and not fence:
            out.append((title, "\n".join(buf)))
            title, buf = ln[2:].strip(), []
        buf.append(ln)
    out.append((title, "\n".join(buf)))
    return out


def no_comment(line):
    """a code line without what a translator may change: comments, the text of strings, the values of key=value"""
    if line.lstrip().startswith(("#", "--")):
        return ""
    line = re.sub(r"\s*(--|#)\s.*$", "", line).rstrip()
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    line = re.sub(r"'(?:[^'\\]|\\.)*'", "''", line)
    return re.sub(r"^(\s*[\w.]+)=.*$", r"\1=", line)


def technical(span):
    """an inline `code` span that is an identifier, a file name or a call: it must be the same in both languages
    (what has spaces, <placeholders>, key=value or a key's name may be translated)"""
    return not re.search(r"[\s<>=]", span) and bool(re.search(r"[_(./]", span))


def signature(text):
    """what must be the same in both languages: the shape of a chapter"""
    sig, ln, code, para = [], text.split("\n"), None, False
    i = 0
    while i < len(ln):
        s = ln[i]
        if code is not None:
            if s.startswith("```") or s.strip() == ":::":
                sig.append(("code", tuple(code)))
                code = None
            else:
                if no_comment(s).strip():
                    code.append(no_comment(s))
            i += 1
            continue
        if s.startswith("```text"):                  # free text (a tree of files...): only its length must agree
            j = i + 1
            while j < len(ln) and not ln[j].startswith("```"):
                j += 1
            sig.append(("text", j - i - 1))
            i, para = j + 1, False
            continue
        if s.startswith("```"):
            code, para = [], False
        elif s.startswith("::: "):
            kind = s[4:].strip()
            para = False
            if kind == "copy":
                sig.append(("box", kind))
                code = []
            else:
                sig.append(("box", kind))
        elif s.strip() == ":::":
            sig.append(("end",))
        elif re.match(r"^#{1,3} ", s):
            sig.append(("h" + str(len(s.split(" ")[0])),))
            para = False
        elif s.startswith("|"):
            cells = [c.strip() for c in s.strip().replace("\\|", "\0").strip("|").split("|")]
            if not all(re.fullmatch(r":?-+:?", c) for c in cells):
                sig.append(("row", len(cells)))
            para = False
        elif re.match(r"^- ", s):
            sig.append(("li",))
            para = False
        elif re.match(r"^\d+\. ", s):
            sig.append(("step", s.split(".")[0]))
            para = False
        elif s.startswith("!["):
            sig.append(("img", re.match(r"!\[.*\]\((.*)\)", s).group(1)))
            para = False
        elif s.strip():
            if not para:
                sig.append(("p",))
            para = True
        else:
            para = False
        i += 1
    spans = sorted(x for x in re.findall(r"`([^`]+)`", re.sub(r"```.*?```", "", text, flags=re.S)) if technical(x))
    return sig, spans, len(GAP.findall(text))


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return min(len(a), len(b)), (a[len(b)] if len(a) > len(b) else None), (b[len(a)] if len(b) > len(a) else None)


def sources_hash(m):
    out = {}
    for src in m.get("sources", []):
        p = os.path.join(ROOT, src)
        if os.path.isdir(p):
            data = "".join(f + h(open(os.path.join(d, f), encoding="utf-8", errors="replace").read())
                           for d, _, fs in sorted(os.walk(p)) for f in sorted(fs))
            out[src] = h(data)
        elif os.path.exists(p):
            out[src] = h(open(p, encoding="utf-8", errors="replace").read())
        else:
            out[src] = "missing"
    return out


def load_sync():
    return json.load(open(SYNC)) if os.path.exists(SYNC) else {}


def check_one(m, sync, say):
    """problems (a list of strings) of one manual"""
    mid, bad = m["id"], []
    ps = {lang: path_of(mid, lang) for lang in LANGS}
    for lang, p in ps.items():
        if not os.path.exists(p):
            bad.append(f"{lang}: {os.path.relpath(p, ROOT)} does not exist")
    if bad:
        return bad
    ch = {lang: split_chapters(ps[lang]) for lang in LANGS}
    if len(ch["en"]) != len(ch["it"]):
        bad.append(f"en has {len(ch['en']) - 1} chapters, it has {len(ch['it']) - 1}")
        return bad
    st = sync.get(mid, {}).get("chapters", {})
    for n, ((te, xe), (ti, xi)) in enumerate(zip(ch["en"], ch["it"])):
        name = te or "(head)"
        se, si = signature(xe), signature(xi)
        if n == 0:                                   # the head: the keys differ by language (title...), not the shape
            continue
        if se[0] != si[0]:
            i, a, b = first_diff(se[0], si[0])
            bad.append(f"chapter {n} '{te}': structure differs at element {i}: en {a} / it {b}")
        elif se[1] != si[1]:
            only = [x for x in se[1] if x not in si[1]][:3], [x for x in si[1] if x not in se[1]][:3]
            bad.append(f"chapter {n} '{te}': inline `code` differs: only en {only[0]} / only it {only[1]}")
        elif se[2] != si[2]:
            bad.append(f"chapter {n} '{te}': gaps differ: en {se[2]} / it {si[2]}")
        rec = st.get(str(n))
        if rec is None:
            bad.append(f"chapter {n} '{te}': never stamped")
        else:
            ce, ci = rec["en"] != h(xe), rec["it"] != h(xi)
            if ce and not ci:
                bad.append(f"chapter {n} '{te}': it is behind (en changed since the stamp)")
            elif ci and not ce:
                bad.append(f"chapter {n} '{te}': en is behind (it changed since the stamp)")
            elif ce and ci:
                bad.append(f"chapter {n} '{te}': both changed since the stamp: stamp again once they match")
    return bad


def cmd_status(a):
    sync = load_sync()
    print(f"{'manual':12} {'en':>5} {'it':>5} {'gaps en/it':>11}  state")
    for m in manuals():
        mid = m["id"]
        sizes, gaps = [], []
        for lang in LANGS:
            p = path_of(mid, lang)
            if os.path.exists(p):
                t = open(p, encoding="utf-8").read()
                sizes.append(str(len(t.split("\n"))))
                gaps.append(str(len(GAP.findall(t))))
            else:
                sizes.append("-")
                gaps.append("-")
        bad = check_one(m, sync, print) if "-" not in sizes else ["missing"]
        src = sync.get(mid, {}).get("sources")
        moved = [k for k, v in (src or {}).items() if sources_hash(m).get(k) != v]
        state = "ok" if not bad else f"{len(bad)} to do"
        if moved:
            state += f"; sources changed: {', '.join(moved)}"
        print(f"{mid:12} {sizes[0]:>5} {sizes[1]:>5} {'/'.join(gaps):>11}  {state}")
    return 0


def cmd_check(a):
    sync, rc = load_sync(), 0
    for m in manuals():
        if a.ids and m["id"] not in a.ids:
            continue
        bad = check_one(m, sync, print)
        moved = [k for k, v in sync.get(m["id"], {}).get("sources", {}).items() if sources_hash(m).get(k) != v]
        for b in bad:
            print(f"{m['id']}: {b}")
        for k in moved:
            print(f"{m['id']}: note: source {k} changed since the stamp: check the content still says the truth")
        if bad:
            rc = 1
        else:
            print(f"{m['id']}: en and it agree")
    return rc


def cmd_stamp(a):
    sync = load_sync()
    for m in manuals():
        if m["id"] not in a.ids:
            continue
        # the structure must agree; only the "stamp" problems are the ones this command clears
        bad = [b for b in check_one(m, {}, print) if "never stamped" not in b]
        if bad:
            print(f"{m['id']}: not stamped, the languages differ:")
            for b in bad:
                print("  " + b)
            return 1
        ch = {lang: split_chapters(path_of(m["id"], lang)) for lang in LANGS}
        sync[m["id"]] = {"chapters": {str(n): {"title": te, "en": h(xe), "it": h(xi)}
                                      for n, ((te, xe), (_, xi)) in enumerate(zip(ch["en"], ch["it"])) if n},
                         "sources": sources_hash(m)}
        print(f"{m['id']}: stamped ({len(ch['en']) - 1} chapters)")
    json.dump(sync, open(SYNC, "w"), indent=1, sort_keys=True)
    open(SYNC, "a").write("\n")
    return 0


def cmd_build(a):
    out = a.output
    for m in manuals():
        if a.ids and m["id"] not in a.ids:
            continue
        for lang in LANGS:
            p = path_of(m["id"], lang)
            if not os.path.exists(p):
                print(f"{m['id']}.{lang}: no content yet")
                continue
            subprocess.run([sys.executable, os.path.join(HERE, "mkmanual.py"), p, "--theme", a.theme,
                            "-o", os.path.join(out, f"{m['id']}.{lang}.pdf")], check=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(f=cmd_status)
    c = sub.add_parser("check")
    c.add_argument("ids", nargs="*")
    c.set_defaults(f=cmd_check)
    s = sub.add_parser("stamp")
    s.add_argument("ids", nargs="+")
    s.set_defaults(f=cmd_stamp)
    b = sub.add_parser("build")
    b.add_argument("ids", nargs="*")
    b.add_argument("--theme", default="paper")
    b.add_argument("-o", "--output", default=os.path.join(ROOT, "build", "manuals"))
    b.set_defaults(f=cmd_build)
    a = ap.parse_args()
    return a.f(a)


if __name__ == "__main__":
    sys.exit(main())
