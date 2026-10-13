#!/usr/bin/env python3
"""
consistency.py - the formats and the platforms of bm kept consistent (docs/spec/CONSISTENCY.md).

  scripts/consistency.py check          every rule below: fails on drift that docs/spec/known.toml does not accept
  scripts/consistency.py report         the same, with the accepted divergences listed (their reason) too
  scripts/consistency.py since PATTERN FILE   the first release tag that has PATTERN in FILE (for docs/spec/specs.toml)

The same fact lives in several files (the kernel in C, the tools in Python, the docs): this compares them, so a change
made in one place and forgotten in the others (a resolution the kernel refuses and the tools write, a tool name in
6 lists) is found when it is made, not months later. Plain Python, no dependencies.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tomllib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
KNOWN = os.path.join(ROOT, "docs", "spec", "known.toml")


def rd(path):
    return open(os.path.join(ROOT, path), encoding="utf-8", errors="replace").read()


class Finding:
    def __init__(self, rule, key, text):
        self.rule, self.key, self.text = rule, key, text

    def id(self):
        return f"{self.rule}:{self.key}"


# ------------------------------------------------------------------ the rules

def rule_resolutions():
    """the cartridge resolutions: the kernel's list is the truth; the tools, the header comment and the docs agree"""
    out, truth = [], None
    fmt = rd("src/bm/format.c")
    truth = {f"{w}x{h}" for w, h in re.findall(r"c->width == (\d+) && c->height == (\d+)", fmt)}
    where = {
        "scripts/mkbm.py": set(re.findall(r'"(\d+x\d+)"', re.search(r"RESOLUTIONS = \[(.*?)\]", rd("scripts/mkbm.py"), re.S).group(1))),
        "src/bm/runtime.c (res_name)": set(re.findall(r'"(\d+x\d+)"', re.search(r"static const char \*res_name.*?\n}", rd("src/bm/runtime.c"), re.S).group(0))),
        "docs/PROJECT-LAYOUT.md": set(re.findall(r"`(\d+x\d+)`", next(l for l in rd("docs/PROJECT-LAYOUT.md").split("\n") if l.startswith("| `resolution`")))),
    }
    hdr = re.search(r"12  u16      width  \(([^)]*)\)", rd("src/bm/bm.h"))
    widths = {int(x) for x in re.findall(r"\d+", hdr.group(1))} if hdr else set()
    if {w for w in (int(r.split("x")[0]) for r in truth)} != widths:
        out.append(Finding("resolutions", "bm.h", f"src/bm/bm.h header comment lists widths {sorted(widths)}, the kernel accepts {sorted(truth)}"))
    for f, s in where.items():
        if s != truth:
            out.append(Finding("resolutions", f, f"{f} has {sorted(s)}, the kernel (src/bm/format.c) accepts {sorted(truth)}"))
    return out


def rule_sections():
    """the section type numbers: bm.h (C) against the Python tools"""
    out = []
    c = {m[0]: int(m[1]) for m in re.findall(r"#define BM_SEC_(\w+)\s+(\d+)", rd("src/bm/bm.h"))}
    for tool in ("scripts/mkbm.py", "scripts/bmres.py", "scripts/bmmesh.py"):
        text = rd(tool)
        py = {}
        for names, nums in re.findall(r"^((?:SEC_\w+,\s*)*SEC_\w+)\s*=\s*([\d,\s]+)$", text, re.M):
            for n, v in zip([x.strip()[4:] for x in names.split(",")], [int(x) for x in nums.split(",")]):
                py[n] = v
        for n, v in py.items():
            if n in c and c[n] != v:
                out.append(Finding("sections", f"{tool}:{n}", f"{tool} says SEC_{n} = {v}, src/bm/bm.h says {c[n]}"))
    return out


def rule_flags():
    """the header flags and offsets: project.h against mkbm.py"""
    out = []
    ph = rd("src/bm/project.h")
    flags = {m[0]: int(m[1], 0) if m[1].isdigit() else m[1] for m in re.findall(r"#define (BM_FLAG_\w+)\s+(\S+)", ph)}
    mk = rd("scripts/mkbm.py")
    if not re.search(r'unpack_from\("<H", \w+, 18\)', mk):
        out.append(Finding("flags", "mkbm", "scripts/mkbm.py does not touch byte 18 (the flags)"))
    b = re.search(r"#define BM_BUILT_FROM\s+(\d+)", ph)
    if b and not re.search(rf"\b{b.group(1)}\b", mk):
        out.append(Finding("flags", "built-from", f"BM_BUILT_FROM is at {b.group(1)} in project.h, mkbm.py does not use that offset"))
    if "bit 0" not in rd("src/bm/bm.h") or "bit 1" not in rd("src/bm/bm.h"):
        out.append(Finding("flags", "bm.h", "src/bm/bm.h does not document the flags at offset 18"))
    return out


def api_registered():
    src = rd("src/bm/runtime.c")
    a = src.index("static const luaL_Reg api[] = {")
    return set(re.findall(r'\{\s*"(\w+)"\s*,\s*\w+\s*\}', src[a:src.index("};", a)]))


def api_documented():
    names = set()
    for f in glob.glob(os.path.join(ROOT, "docs/api/*.md")) + [os.path.join(ROOT, "docs/API.md")]:
        for ln in open(f, encoding="utf-8"):
            if ln.startswith("|") and ln.count("|") >= 3:
                first = ln.split("|")[1]
                for m in re.finditer(r"`([^`]*)`", first):
                    for n in re.finditer(r"(?<![.:\w])([A-Za-z_]\w*)\(", m.group(1)):
                        names.add(n.group(1))
    return names


def rule_api():
    """the functions the runtime registers against the functions docs/api documents"""
    reg, doc = api_registered(), api_documented()
    out = []
    for n in sorted(reg - doc):
        out.append(Finding("api-undocumented", n, f"`{n}` is registered in runtime.c and not in docs/api/*.md"))
    for n in sorted(doc - reg):
        if n.startswith("_") or not n.islower():
            continue
        out.append(Finding("api-missing", n, f"`{n}(` is in docs/api/*.md and not registered in runtime.c (a library function or a typo?)"))
    return out


def rule_tools():
    """the names of the apps and tools: carts.c, devtool.c, catalog.c, mkmarket.py, runtime.c (reserved names) say the same"""
    out = []
    carts = rd("src/kernel/carts.c")
    ids = set(re.findall(r'return "(\w+)";', re.search(r"static const char \*tool_id_of.*?\n}", carts, re.S).group(0)))
    catalog = set(re.findall(r'"([\w-]+)"', re.search(r"apps\[\] = \{(.*?)\};", rd("src/net/catalog.c"), re.S).group(1)))
    market = set(re.findall(r'"([\w-]+)"', re.search(r"APP_IDS = \{(.*?)\}", rd("scripts/mkmarket.py"), re.S).group(1)))
    if catalog != market:
        out.append(Finding("tools", "catalog-vs-mkmarket", f"src/net/catalog.c knows {sorted(catalog)}, scripts/mkmarket.py APP_IDS {sorted(market)}"))
    for i in sorted(ids - catalog):
        out.append(Finding("tools", f"catalog:{i}", f"tool `{i}` (carts.c tool_id_of) is not an app of the Market catalogue (src/net/catalog.c)"))
    res = set(re.findall(r'"(\w+)"', re.search(r"reserved_tool_name.*?\n}", rd("src/bm/runtime.c"), re.S).group(0)))
    for i in sorted(ids - res):
        out.append(Finding("tools", f"reserved:{i}", f"tool `{i}` is not in reserved_tool_name (src/bm/runtime.c): a game could create a file with that name"))
    dev = rd("src/kernel/devtool.c")
    for alt in re.findall(r'"(\w+)" : ', dev) + re.findall(r'\? "(\w+)"', dev):
        if alt not in res and alt in ("editor", "assistan"):
            out.append(Finding("tools", f"reserved:{alt}", f"alias `{alt}` (devtool.c) is not in reserved_tool_name"))
    return out


def rule_platforms():
    """what the two kernels (Pi Zero W, RGB30) offer: the same unless a capability in docs/spec/platforms.toml differs"""
    out = []
    pi = re.search(r'tabs\[\] = \{([^}]*)\}', rd("src/kernel/carts.c")).group(1)
    rgb = re.search(r'tab_names\[\] = \{([^}]*)\}', rd("src/rgb30/ui.c")).group(1)
    pi_tabs, rgb_tabs = re.findall(r'"(\w+)"', pi), re.findall(r'"(\w+)"', rgb)
    if pi_tabs != rgb_tabs:
        out.append(Finding("platform-menu", "tabs", f"the menu tabs differ: Pi {pi_tabs}, RGB30 {rgb_tabs}"))
    pi_modes = re.search(r"screen_modes\[\]\[2\] = \{(.*?)\};", rd("src/bm/runtime.c"), re.S).group(1)
    sq_modes = re.search(r"square_modes\[\]\[2\] = \{(.*?)\};", rd("src/bm/runtime.c"), re.S).group(1)
    out.append(Finding("platform-screen", "modes", "screen() modes: Pi/TV " + " ".join(f"{a}x{b}" for a, b in re.findall(r"(\d+),\s*(\d+)", pi_modes)) +
                       "; square panel " + " ".join(f"{a}x{b}" for a, b in re.findall(r"(\d+),\s*(\d+)", sq_modes))))
    cart_sym = r"bm_(sdk_b16|editor|sdk|sound|studio|animator|mesh|pixel|code|assistant)_cart"
    emb_rgb = re.findall(cart_sym, rd("src/rgb30/ui.c"))
    emb_pi = re.findall(cart_sym, rd("src/kernel/carts.c"))
    out.append(Finding("platform-tools", "embedded", f"tools embedded in the kernel: Pi {sorted(set(emb_pi))}, RGB30 {sorted(set(emb_rgb))}"))
    return out


def rule_spec():
    """the registry docs/spec/specs.toml: the kernel's revision is the registry's last, and what it points to exists"""
    out = []
    with open(os.path.join(ROOT, "docs/spec/specs.toml"), "rb") as f:
        reg = tomllib.load(f)
    last = max(r["rev"] for r in reg["revision"])
    code = re.search(r"#define BM_SPEC_REV\s+(\d+)", rd("src/bm/bm.h"))
    if not code or int(code.group(1)) != last:
        out.append(Finding("spec", "rev", f"BM_SPEC_REV in src/bm/bm.h is {code.group(1) if code else '?'}, docs/spec/specs.toml ends at revision {last}"))
    ids = set()
    for it in reg["item"]:
        if it["id"] in ids:
            out.append(Finding("spec", f"dup:{it['id']}", f"item {it['id']} is listed twice"))
        ids.add(it["id"])
        if it["since"] > last:
            out.append(Finding("spec", f"since:{it['id']}", f"item {it['id']} is since revision {it['since']}, the last is {last}"))
        for w in it["where"]:
            if not os.path.exists(os.path.join(ROOT, w)):
                out.append(Finding("spec", f"where:{it['id']}", f"item {it['id']}: {w} does not exist"))
    return out


RULES = [rule_resolutions, rule_sections, rule_flags, rule_api, rule_tools, rule_platforms, rule_spec]


# ------------------------------------------------------------------ the accepted divergences

def load_known():
    if not os.path.exists(KNOWN):
        return {}
    with open(KNOWN, "rb") as f:
        return {k["id"]: k for k in tomllib.load(f).get("accept", [])}


def run(verbose):
    known = load_known()
    bad, accepted, seen = [], [], set()
    for rule in RULES:
        for f in rule():
            seen.add(f.id())
            (accepted if f.id() in known else bad).append(f)
    stale = [k for k in known if k not in seen]
    for f in bad:
        print(f"DRIFT   {f.rule:18} {f.text}")
    if verbose:
        for f in accepted:
            k = known[f.id()]
            print(f"accepted {f.rule:17} {f.text}\n         because: {k['because']} ({k['kind']})")
    for k in stale:
        print(f"STALE   {k}: accepted in docs/spec/known.toml but it is not found any more: remove it")
    print(f"consistency: {len(bad)} drift, {len(accepted)} accepted, {len(stale)} stale")
    return 1 if bad or stale else 0


def since(pattern, path):
    """the first release tag whose file has the pattern (git pickaxe)"""
    commit = subprocess.run(["git", "-C", ROOT, "log", "--format=%h", "--diff-filter=AM", "-S" + pattern, "--", path],
                            capture_output=True, text=True).stdout.split()
    if not commit:
        print("not found in the history")
        return 1
    first = commit[-1]
    tags = subprocess.run(["git", "-C", ROOT, "tag", "--contains", first, "--sort=v:refname"], capture_output=True, text=True).stdout.split()
    print(f"{first} {tags[0] if tags else 'unreleased'}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    sub.add_parser("report")
    s = sub.add_parser("since")
    s.add_argument("pattern")
    s.add_argument("path")
    a = ap.parse_args()
    if a.cmd == "since":
        return since(a.pattern, a.path)
    return run(a.cmd == "report")


if __name__ == "__main__":
    sys.exit(main())
