#!/usr/bin/env python3
"""Audit of the tracking docs, for the docs-loop skill.

  docs_audit.py score [--json]   one number (lower is better) and what makes it up
  docs_audit.py check            list the broken links and the named files that are missing (exit 1 if any)
  docs_audit.py rereads          most re-read files and tips, from the crab-ledger report
  docs_audit.py repeats [DAYS]   prompts and commands repeated across sessions (skill candidates)
  docs_audit.py log SCORE keep|discard "what changed"   append to .claude/docs-loop/results.tsv
  docs_audit.py best             last kept score

Score = tokens always loaded (CLAUDE.md + docs/progress.md) + mean tokens of the area file
reached from the index + penalties (orphans, broken links, oversize files, duplicated lines).
Tokens are bytes / 4. Nothing here writes to the docs."""
import glob, json, os, re, sys, time
from collections import Counter, defaultdict

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf8')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.path.expanduser("~")
LOOP = os.path.join(ROOT, ".claude", "docs-loop", "results.tsv")
INDEX = "docs/progress.md"
LIMIT_LINES = 120
P_ORPHAN, P_BROKEN, P_BIG, P_DUP = 500, 300, 200, 50


def rd(rel):
    try:
        return open(os.path.join(ROOT, rel), encoding="utf8", errors="replace").read()
    except OSError:
        return ""


def tok(rel):
    try:
        return os.path.getsize(os.path.join(ROOT, rel)) / 4
    except OSError:
        return 0


def tracked():
    out = ["CLAUDE.md", INDEX]
    out += sorted(os.path.relpath(p, ROOT).replace("\\", "/") for p in glob.glob(os.path.join(ROOT, "docs", "progress", "*.md")))
    return out


def md_links(rel):
    base = os.path.dirname(rel)
    for m in re.finditer(r"\]\(([^)#\s]+\.md)(?:#[^)]*)?\)", rd(rel)):
        yield os.path.normpath(os.path.join(base, m.group(1))).replace("\\", "/")


def broken(files):
    """Messages for the broken links and the missing files named in `files` (each one costs P_BROKEN in the score)."""
    msgs = []
    for f in files:
        for l in md_links(f):
            if not os.path.exists(os.path.join(ROOT, l)):
                msgs.append(f"broken link in {f}: {l}")
        for m in re.finditer(r"`((?:docs|scripts|src|carts|tests)/[\w./-]+\.\w+)`", rd(f)):
            p = m.group(1)
            vague = "/." in p or (not p.startswith(("docs/", "scripts/")) and p.count("/") < 2)
            if not vague and "*" not in p and not os.path.exists(os.path.join(ROOT, p)) and not p.endswith(("-IT.md",)):
                msgs.append(f"missing file named in {f}: {p}")
    return msgs


def cmd_check():
    msgs = broken(tracked())
    for m in msgs:
        print("  !", m)
    if msgs:
        sys.exit(1)
    print("docs_audit: 0 broken")


def cmd_score(as_json):
    files = tracked()
    always = tok("CLAUDE.md") + tok(INDEX)
    linked = sorted(set(md_links(INDEX)))
    area = [l for l in linked if l.startswith("docs/progress/")]
    mean_area = sum(tok(a) for a in area) / max(1, len(area))
    pen = []
    for f in files[2:]:
        if f not in linked:
            pen.append((P_ORPHAN, f"orphan: {f} is not linked from the index"))
    for m in broken(files):
        pen.append((P_BROKEN, m))
    for f in files[2:]:
        n = rd(f).count("\n")
        if n > LIMIT_LINES:
            pen.append((P_BIG, f"{f} has {n} lines (limit ~{LIMIT_LINES}): split by topic"))
    seen = defaultdict(set)
    for f in files:
        for line in rd(f).splitlines():
            t = line.strip()
            if len(t) >= 60 and not t.startswith(("|", "#")):
                seen[t].add(f)
    for t, fs in seen.items():
        if len(fs) > 1:
            pen.append((P_DUP, f"duplicated in {', '.join(sorted(fs))}: {t[:50]}…"))
    entry = [f for f in ("docs/progress.md",) if "docs/progress.md" in rd("CLAUDE.md")]
    if not entry:
        pen.append((2000, "CLAUDE.md does not point to the single index docs/progress.md"))
    score = always + mean_area + sum(p for p, _ in pen)
    res = {
        "score": round(score), "always_loaded": round(always), "mean_area": round(mean_area),
        "penalty": sum(p for p, _ in pen), "penalties": [m for _, m in pen],
        "files": {f: round(tok(f)) for f in files},
    }
    if as_json:
        print(json.dumps(res, indent=1))
        return
    print(f"score {res['score']}  (always loaded {res['always_loaded']} + mean area {res['mean_area']} + penalties {res['penalty']})")
    for f, t in sorted(res["files"].items(), key=lambda x: -x[1])[:6]:
        print(f"  {t:6d} tok  {f}")
    for m in res["penalties"][:15]:
        print("  !", m)


def cmd_rereads():
    p = os.path.join(HOME, ".claude", "crab-ledger-report.json")
    try:
        r = json.load(open(p, encoding="utf8"))
    except OSError:
        sys.exit("no crab-ledger report yet: open the /ledger pane once (the mod writes " + p + ")")
    print(f"report from {time.strftime('%Y-%m-%d %H:%M', time.localtime(r['at'] / 1000))}; re-read cost over 7 days ${r['mix']['rereadAll']:.2f}")
    for f in r["rereads"]:
        print(f"  ${f['cost']:.2f}  {f['reads']} reads / {f['rereads']} re  {f['path']}")
    for t in r["tips"]:
        print(f"  tip ≈-${t['save']:.0f}: {t['title']}")


def norm(s):
    s = s.lower()
    s = re.sub(r"[a-z]:[\\/][^\s]*|/[\w./-]+", "<path>", s)
    s = re.sub(r"\d+", "N", s)
    return re.sub(r"\s+", " ", s).strip()


def cmd_repeats(days):
    d = os.path.join(HOME, ".claude", "projects", re.sub(r"[^A-Za-z0-9]", "-", ROOT))
    cutoff = time.time() - days * 86400
    prompts, cmds = defaultdict(set), defaultdict(set)
    for f in glob.glob(os.path.join(d, "*.jsonl")):
        if os.path.getmtime(f) < cutoff:
            continue
        sid = os.path.basename(f)
        for line in open(f, encoding="utf8", errors="replace"):
            if '"type":"user"' in line or '"name":"Bash"' in line:
                try:
                    r = json.loads(line)
                except ValueError:
                    continue
                m = r.get("message") or {}
                c = m.get("content")
                if r.get("type") == "user" and isinstance(c, str) and not c.startswith("<") and not r.get("isSidechain"):
                    key = " ".join(norm(c).split()[:7])
                    if len(key) > 12 and "agent-message" not in key:
                        prompts[key].add(sid)
                elif isinstance(c, list):
                    for b in c:
                        if b.get("type") == "tool_use" and b.get("name") == "Bash":
                            cm = norm((b.get("input") or {}).get("command", ""))
                            key = " ".join(cm.split()[:4])
                            if len(key) > 8:
                                cmds[key].add(sid)
    skills = {os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(ROOT, "claude", "skills", "*", "SKILL.md"))}
    print(f"existing skills: {', '.join(sorted(skills)) or 'none'}")
    print("repeated prompts (≥3 sessions):")
    for k, s in sorted(prompts.items(), key=lambda x: -len(x[1])):
        if len(s) >= 3:
            print(f"  {len(s):3d}  {k}")
    print("repeated commands (≥3 sessions):")
    for k, s in sorted(cmds.items(), key=lambda x: -len(x[1]))[:15]:
        if len(s) >= 3:
            print(f"  {len(s):3d}  {k}")


def cmd_log(a):
    os.makedirs(os.path.dirname(LOOP), exist_ok=True)
    new = not os.path.exists(LOOP)
    with open(LOOP, "a", encoding="utf8") as f:
        if new:
            f.write("time\tscore\tstatus\tchange\n")
        f.write(f"{time.strftime('%Y-%m-%d %H:%M')}\t{a[0]}\t{a[1]}\t{a[2] if len(a) > 2 else ''}\n")


def cmd_best():
    try:
        rows = [l.split("\t") for l in open(LOOP, encoding="utf8").read().splitlines()[1:]]
    except OSError:
        sys.exit("none")
    kept = [r for r in rows if len(r) > 2 and r[2] == "keep"]
    print(kept[-1][1] if kept else "none")


if __name__ == "__main__":
    c = sys.argv[1] if len(sys.argv) > 1 else ""
    a = sys.argv[2:]
    if c == "score":
        cmd_score("--json" in a)
    elif c == "rereads":
        cmd_rereads()
    elif c == "repeats":
        cmd_repeats(int(a[0]) if a else 30)
    elif c == "log" and len(a) >= 2:
        cmd_log(a)
    elif c == "check":
        cmd_check()
    elif c == "best":
        cmd_best()
    else:
        sys.exit(__doc__)
