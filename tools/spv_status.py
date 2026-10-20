#!/usr/bin/env python3
"""Zero-model status dashboard of the sync (one screen, no model, no write to the repo).

  python3 tools/spv_status.py --once          write one snapshot
  python3 tools/spv_status.py --loop [MIN]    write a snapshot every MIN minutes (default 15)
  python3 tools/spv_status.py --summary       print a 6-line digest

Snapshots go to C:\\Users\\Utente\\bm-status\\status.md and status.html (outside the repo, atomic write).
Data: `git fetch origin <branch>`, then every file is read with `git show origin/<branch>:<path>`,
so the worktree does not need to be at the latest commit. Errors of the loop go to status.log.
Beta scope = all WO files in sync/wo (index/README/draft files excluded).
Stdlib only."""
import argparse
import datetime
import html
import json
import os
import re
import subprocess
import sys
import tempfile
import time

BRANCH = "sync/protocollo-ottobre"
OUT_DIR = r"C:\Users\Utente\bm-status"
OPS = ["NET", "B16", "CI", "AGX", "SPV"]
STATES = ("DONE", "WORKING", "WAITING", "FREE")
IDLE_MIN, LATE_MIN = 60, 10
WT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
# WO files that are not work orders (index / README / per-operator indexes / closeout)
# correction WO -> original WOs it closes (a REVIEW of the correction counts for the original)
CORRECTS = [("agy-6a", "agy-1"), ("agy-6b", "agy-3"), ("agy-6c", "agy-5"), ("agy-9", "agy-3"), ("agy-9", "agy-6"), ("agy-10", "agy-8")]
NOT_WO = {"D09-0", "README", "CLOSEOUT", "AGY", "B16", "CI", "NET", "AGX", "SPV"}

PROJECTS = os.path.join(os.path.expanduser("~"), ".claude", "projects")
SPV_SESSION = "9eafe2b2-124f-4040-aaad-9baf1ab9453a"
CTX_LIMIT = 1000000
CTX_GUARD_PCT = 12                      # as in ctx_guard: warn above 12% of a 1M window
CTX_WARN = CTX_LIMIT * CTX_GUARD_PCT // 100
CALLS_WARN = 400                        # calls since last compact
GROW_WINDOW = 5                         # "still growing": last call above the one GROW_WINDOW calls earlier

# ---------------------------------------------------------------- git access


def git(*args, check=True):
    r = subprocess.run(["git", "-C", WT] + list(args), capture_output=True, timeout=120)
    if check and r.returncode != 0:
        raise RuntimeError("git %s: %s" % (" ".join(args[:2]), r.stderr.decode("utf-8", "replace").strip()[:200]))
    return r.stdout.decode("utf-8", "replace")


def fetch():
    git("fetch", "-q", "origin", BRANCH)


def show(path):
    try:
        return git("show", "origin/%s:%s" % (BRANCH, path))
    except RuntimeError:
        return ""


def ls(dirpath):
    out = git("ls-tree", "--name-only", "origin/%s" % BRANCH, dirpath.rstrip("/") + "/", check=False)
    return [l.strip() for l in out.splitlines() if l.strip()]

# ---------------------------------------------------------------- parsing helpers


def iso(s):
    s = (s or "").strip()
    m = re.match(r"(\d{4}-\d\d-\d\d)[T ](\d\d:\d\d)(?::\d\d)?\s*([+-]\d\d):?(\d\d)?", s)
    try:
        if m:
            t = datetime.datetime.fromisoformat("%sT%s%s:%s" % (m.group(1), m.group(2), m.group(3), m.group(4) or "00"))
        else:
            m = re.match(r"(\d{4}-\d\d-\d\d)[T ](\d\d:\d\d)", s)
            if not m:
                return None
            t = datetime.datetime.fromisoformat("%sT%s" % (m.group(1), m.group(2)))
        return t if t.tzinfo else t.astimezone()
    except ValueError:
        return None


def header(text):
    m = re.match(r"---\r?\n(.*?)\r?\n---", text, re.S)
    if not m:
        return {}
    d = {}
    for line in m.group(1).splitlines():
        k, _, v = line.partition(":")
        d[k.strip()] = re.split(r"\s+#", v)[0].strip()
    return d


LOG_ROW = re.compile(r"^\|\s*(\d{4}-\d\d-\d\d)[ T](\d\d:\d\d)(\S*)\s*\|\s*([^|]*)\|\s*([^|]*)\|\s*`?([^|`]*)`?\s*\|\s*(.*?)\s*\|?\s*$")


def log_rows(text):
    """[(datetime, author, kind, text)] of the Log table, in file order"""
    out = []
    for line in text.splitlines():
        m = LOG_ROW.match(line)
        if m:
            t = iso(m.group(1) + "T" + m.group(2) + (m.group(3) or ""))
            if t:
                out.append((t, m.group(4).strip(), m.group(6).strip(), m.group(7)))
    return out


DELIV = re.compile(r"^[-*]?\s*DELIVERED\s+(\S+)\s+@\s+\S+@(\S+)\s")
TS_END = re.compile(r"(\d{4}-\d\d-\d\dT\d\d:\d\d(?::\d\d)?(?:[+-]\d\d:?\d\d)?)\s*$")
ITEM = r"[0-9A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*"


def deliveries(text):
    """[(id, sha, time or None, lineno)] of every DELIVERED line of an operator file"""
    out = []
    for n, line in enumerate(text.splitlines(), 1):
        m = DELIV.match(line.strip())
        if m:
            t = TS_END.search(line.strip())
            out.append((m.group(1), m.group(2), iso(t.group(1)) if t else None, n))
    return out


def expand_ranges(s):
    """'W09a-d' -> 'W09a, W09b, W09c, W09d'; 'CO-1..CO-5' -> 'CO-1, CO-2, ...'"""
    def lett(m):
        a, b = m.group(2), m.group(3)
        if ord(b) <= ord(a) or ord(b) - ord(a) > 12:
            return m.group(0)
        return ", ".join(m.group(1) + chr(c) for c in range(ord(a), ord(b) + 1))

    def num(m):
        a, b = int(m.group(2)), int(m.group(3))
        if b <= a or b - a > 40:
            return m.group(0)
        return ", ".join("%s%d" % (m.group(1), i) for i in range(a, b + 1))
    s = re.sub(r"(?<![\w-])([A-Za-z][A-Za-z0-9-]*?\d)([a-z])-([a-z])(?![\w-])", lett, s)
    s = re.sub(r"(?<![\w-])([A-Za-z]+-)(\d+)\.\.(?:\1)?(\d+)(?![\w-])", num, s)
    return s


def norm(i):
    return i.strip().lower()


VERDICT = re.compile(r"(?<![A-Za-z])(APPROVED|CHANGES REQUESTED)(?![A-Za-z])")
OPPFX = r"(?:(?:NET|B16|CI|AGX|AGY)\s+)?"
# one list item: an id, or a bare number/suffix continuing the previous id's prefix (AGY-18 (..), 19 (..))
ITEM2 = r"(?:%s|\d+[a-z]?)" % ITEM
LIST_RE = re.compile(r"%s%s(?:\s+fix)?(?:\s+rev\.\s*\d+)?(?:\s*(?:,|[ ]and[ ]|&)\s*%s(?:\s+fix)?(?:\s+rev\.\s*\d+)?)*" % (OPPFX, ITEM, ITEM2))
ID_FIND = re.compile(r"(%s)(\s+fix)?" % ITEM2)


BEFORE_RE = re.compile(r"((?:(?<![\w-])%s(?:\s+fix)?(?:\s*@[0-9a-f]{7,40})?\s*(?:,|&|and)?\s*)+)$" % ITEM)


def ids_before(prefix):
    """ids written right before a verdict: 'D09-2a @5e553b7e and D09-2b @bd47da86 APPROVED', 'W10a fix @3c739e29 APPROVED'"""
    m = BEFORE_RE.search(prefix.rstrip(" :"))
    out = []
    if m:
        for im in re.finditer(r"(%s)(\s+fix)?(?:\s*@[0-9a-f]{7,40})?" % ITEM, m.group(1)):
            tok = im.group(1)
            if re.search(r"\d", tok) and re.search(r"[A-Za-z]", tok) and not re.fullmatch(r"[0-9a-f]{7,40}", tok) and tok.lower() != "and":
                out.append(tok + ("-fix" if im.group(2) else ""))
    return out


def sha_verdict(full, pos):
    """verdict for a sha mentioned at pos: the one that follows it if only ids/shas/'and' lie between, else the one before it, else the next"""
    nxt = VERDICT.search(full, pos)
    if nxt and re.fullmatch(r"[\s,&:]*(?:(?:and\s+)?[\w-]+(?:\s+fix)?\s*@[0-9a-f]{7,40}[\s,&]*)*", full[pos:nxt.start()]):
        return nxt.group(1)
    vs = list(VERDICT.finditer(full[:pos]))
    if vs:
        return vs[-1].group(1)
    return nxt.group(1) if nxt else None


def review_events(text):
    """[(verdict, id)] from one REVIEW row text: ids listed right after APPROVED / CHANGES REQUESTED"""
    s = expand_ranges(text)
    s = re.sub(r"\([^()]*\)", " ", s)             # drop (sha: notes)
    out = []
    for m in VERDICT.finditer(s):
        for tok in ids_before(s[:m.start()]):
            out.append((m.group(1), norm(tok)))
        rest = s[m.end():].lstrip(" :")
        lm = LIST_RE.match(rest)
        if not lm:
            continue
        prefix = None
        body = re.sub(r"^(?:NET|B16|CI|AGX|AGY)\s+", "", lm.group(0))
        for im in ID_FIND.finditer(body):
            tok = im.group(1)
            if re.fullmatch(r"\d+[a-z]?", tok):               # bare number: reuse the previous prefix
                if prefix is None:
                    continue
                tok = prefix + tok
            elif not (re.search(r"\d", tok) and re.search(r"[A-Za-z]", tok)) or re.fullmatch(r"[0-9a-f]{7,40}", tok):
                continue
            else:
                pm = re.match(r"(.*-)\d+[a-z]?$", tok)
                prefix = pm.group(1) if pm else None
            out.append((m.group(1), norm(tok + ("-fix" if im.group(2) else ""))))
    return out


def inbox_ids(text, known):
    """WO ids (with a file) in the order they appear in an inbox"""
    seen, out = set(), []
    s = expand_ranges(text)
    for tok in re.findall(ITEM, s):
        k = norm(tok)
        if k in known and k not in seen:
            seen.add(k)
            out.append(k)
    return out

# ---------------------------------------------------------------- model


def family(ev, d):
    """event key ev concerns delivery id d (same id, or d plus a suffix such as -fix)"""
    return ev == d or ev.startswith(d + "-") or (ev.startswith(d) and ev[len(d)].isalpha() and d[-1].isdigit())


def collect(now):
    sync_md = show("SYNC.md")
    log = log_rows(sync_md)
    wo_files = [os.path.basename(p) for p in ls("sync/wo") if p.endswith(".md")]
    stems = [f[:-3] for f in wo_files]
    wo_ids = [s for s in stems if s not in NOT_WO and not s.endswith("-0") and not s.startswith("_")]
    known = {norm(s): s for s in wo_ids}

    # review events in Log order: (time, verdict, id, row_text)
    events = []
    for t, who, kind, txt in log:
        if kind.startswith("REVIEW"):
            full = kind + " " + txt
            for v, i in review_events(full):
                events.append((t, v, i, txt))
            for sm in re.finditer(r"[(@]([0-9a-f]{8})(?![0-9a-f])", full):      # (sha: note) -> verdict that precedes it
                v = sha_verdict(full, sm.end())
                if v:
                    events.append((t, v, "sha:" + sm.group(1), txt))

    # correction WOs close the WO they correct (the Log names the correction, not the original)
    for t, v, i, txt in list(events):
        for src, dst in CORRECTS:
            if i == src or i.startswith(src + "-"):
                events.append((t, v, dst, txt))

    ops = {}
    delivs = []          # (op, id, sha, time, lineno)
    for oid in OPS:
        text = show("sync/%s.md" % oid)
        h = header(text)
        ops[oid] = {"text": text, "h": h}
        for i, sha, t, n in deliveries(text):
            delivs.append((oid, i, sha, t, n))
        ops[oid]["inbox"] = show("sync/inbox/%s.md" % oid)

    def verdict_for(d, sha, t):
        """('APPROVED'|'CHANGES REQUESTED'|None, time) of the latest review event covering delivery d"""
        best = None
        for et, v, i, txt in events:
            bysha = i == "sha:" + sha[:8]
            if (family(i, norm(d)) or bysha) and (t is None or et >= t or bysha):
                best = (v, et)
        return best

    # status of every delivery
    dstat = []
    for oid, i, sha, t, n in delivs:
        vb = verdict_for(i, sha, t)
        dstat.append({"op": oid, "id": i, "sha": sha, "t": t, "line": n, "verdict": vb[0] if vb else None})
    # superseded: an older delivery of the same id with a newer delivery is not awaiting separately
    latest = {}
    for d in dstat:
        k = norm(d["id"])
        if k not in latest or (d["t"] or datetime.datetime.min.astimezone()) >= (latest[k]["t"] or datetime.datetime.min.astimezone()):
            latest[k] = d
    cutoff = now.replace(year=2026, month=10, day=17, hour=0, minute=0, second=0, microsecond=0)
    pend = [d for d in dstat if d["verdict"] is None and latest[norm(d["id"])] is d]
    awaiting = [d for d in pend if not d["t"] or d["t"] >= cutoff]
    legacy = [d for d in pend if d["t"] and d["t"] < cutoff]      # delivered before 17 Oct, no REVIEW row parsed

    # WO status
    ev_by_id = {}
    for et, v, i, txt in events:
        ev_by_id.setdefault(i, []).append((et, v))
    inbox_of = {}
    for oid in OPS:
        for i in inbox_ids(ops[oid]["inbox"], known):
            inbox_of.setdefault(i, oid)
    status = {}
    for s in wo_ids:
        k = norm(s)
        fam_ev = sorted((et, v) for i, lst in ev_by_id.items() if family(i, k) for et, v in lst)
        fam_del = [d for d in dstat if family(norm(d["id"]), k)]
        idev = [e for e in fam_ev]
        if (idev and idev[-1][1] == "APPROVED") or (fam_del and fam_del[-1]["verdict"] == "APPROVED"):
            status[s] = "approved"
        elif any(d["verdict"] is None for d in fam_del):
            status[s] = "review"
        elif fam_ev or k in inbox_of:
            status[s] = "assigned"          # changes requested, or queued in an inbox
        elif fam_del:
            status[s] = "review"
        else:
            status[s] = "unassigned"
    return {"now": now, "log": log, "wo_ids": wo_ids, "known": known, "events": events, "ops": ops,
            "delivs": dstat, "awaiting": awaiting, "legacy": legacy, "status": status, "inbox_of": inbox_of}


# ---------------------------------------------------------------- cost audit (session transcripts, no model)


def scan_transcript(path):
    """Usage stats of one transcript jsonl: calls, cache read/write, last context, calls since last compact."""
    calls, cr, cw, since, seen, ctxs = 0, 0, 0, 0, set(), []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "compact_boundary" in line and '"subtype"' in line:
                try:
                    if json.loads(line).get("subtype") == "compact_boundary":
                        since = 0
                        ctxs = []
                except ValueError:
                    pass
                continue
            if '"usage"' not in line or "assistant" not in line:
                continue
            try:
                d = json.loads(line)
            except ValueError:
                continue
            m = d.get("message") or {}
            u = m.get("usage")
            if d.get("type") != "assistant" or d.get("isSidechain") or not u:
                continue
            key = m.get("id") or d.get("uuid")
            if key in seen:
                continue
            seen.add(key)
            c_in = (u.get("input_tokens") or 0) + (u.get("cache_read_input_tokens") or 0) + (u.get("cache_creation_input_tokens") or 0)
            if c_in == 0:
                continue
            calls += 1
            since += 1
            cr += u.get("cache_read_input_tokens") or 0
            cw += u.get("cache_creation_input_tokens") or 0
            ctxs.append(c_in)
            if len(ctxs) > GROW_WINDOW + 1:
                ctxs.pop(0)
    last = ctxs[-1] if ctxs else 0
    growing = len(ctxs) > GROW_WINDOW and last > ctxs[0]
    return {"calls": calls, "cr": cr, "cw": cw, "ctx": last, "since": since, "growing": growing}


def session_files(ids):
    """{op: path}: SPV by fixed session id, operators by the transcript whose customTitle is 'Operatore <ID>...' (latest file)."""
    found = {}
    if not os.path.isdir(PROJECTS):
        return found
    cands = []
    for proj in os.listdir(PROJECTS):
        pd = os.path.join(PROJECTS, proj)
        if os.path.isdir(pd):
            cands += [os.path.join(pd, f) for f in os.listdir(pd) if f.endswith(".jsonl")]
    cands.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    for p in cands:
        if os.path.basename(p) == SPV_SESSION + ".jsonl":
            found.setdefault("SPV", p)
    for oid in ids:
        if oid in found:
            continue
        pat = re.compile(r'"customTitle":"Operatore ' + re.escape(oid) + r'\b')
        for p in cands:
            try:
                with open(p, encoding="utf-8", errors="replace") as f:
                    if any(pat.search(l) for l in f if '"custom-title"' in l):
                        found[oid] = p
                        break
            except OSError:
                pass
    return found


def cost_audit():
    """-> (lines, warnings). Estimated reread cost = cache-read tokens (each call rereads the whole context)."""
    files = session_files(OPS)
    rows, warns, tot = [], [], 0
    for oid in OPS:
        p = files.get(oid)
        if not p:
            rows.append("%-4s no transcript found" % oid)
            continue
        try:
            st = scan_transcript(p)
        except OSError as e:
            rows.append("%-4s unreadable (%s)" % (oid, e))
            continue
        tot += st["cr"]
        rows.append("%-4s calls %5d  cache-read %7.1fM  cache-write %6.2fM  context %4dk  since-compact %4d  reread cost ~%.1fM tok" % (
            oid, st["calls"], st["cr"] / 1e6, st["cw"] / 1e6, st["ctx"] // 1000, st["since"], st["cr"] / 1e6))
        if st["ctx"] > CTX_WARN and st["growing"]:
            warns.append("WARN %s: context %dk above %dk (CTX_GUARD_PCT=%d) and still growing" % (oid, st["ctx"] // 1000, CTX_WARN // 1000, CTX_GUARD_PCT))
        if st["since"] > CALLS_WARN:
            warns.append("WARN %s: %d calls since last compact (limit %d)" % (oid, st["since"], CALLS_WARN))
    rows.append("Total estimated reread cost (cache-read): ~%.1fM tokens" % (tot / 1e6))
    return rows, warns


def selftest():
    import json as _j
    d = tempfile.mkdtemp()
    p = os.path.join(d, "t.jsonl")
    def a(i, ctx):
        return _j.dumps({"type": "assistant", "uuid": "u%d" % i, "message": {"id": "m%d" % i, "usage": {"input_tokens": 1, "cache_read_input_tokens": ctx, "cache_creation_input_tokens": 10}}})
    lines = [a(1, 100), a(1, 100), _j.dumps({"type": "system", "subtype": "compact_boundary"})]
    lines += [a(i, 130000 + i * 1000) for i in range(2, 9)]
    open(p, "w").write("\n".join(lines) + "\n")
    st = scan_transcript(p)
    assert st["calls"] == 8 and st["since"] == 7 and st["growing"] and st["ctx"] > CTX_WARN, st
    assert st["cw"] == 80, st
    ev = lambda r: sorted(review_events(r))
    assert ev("D09-2a @5e553b7e and D09-2b @bd47da86 APPROVED (scope ok)") == [("APPROVED", "d09-2a"), ("APPROVED", "d09-2b")], ev("D09-2a @5e553b7e and D09-2b @bd47da86 APPROVED")
    assert ev("W10a fix @3c739e29 APPROVED (x). W10b APPROVED (3 files)") == [("APPROVED", "w10a-fix"), ("APPROVED", "w10b")]
    r = "D09-1e @112d941f APPROVED (ok). D09-1g @ecd9231e CHANGES REQUESTED: ui.c"
    assert ev(r) == [("APPROVED", "d09-1e"), ("CHANGES REQUESTED", "d09-1g")], ev(r)
    assert sha_verdict(r, r.index("112d941f") + 8) == "APPROVED" and sha_verdict(r, r.index("ecd9231e") + 8) == "CHANGES REQUESTED"
    assert ev("APPROVED W09a (844aa06c), W09b (88f13d54): ok") == [("APPROVED", "w09a"), ("APPROVED", "w09b")]
    print("selftest ok")
    return 0


def age_str(now, t):
    if not t:
        return "?"
    m = int((now - t).total_seconds() // 60)
    return "%dm" % m if m < 120 else "%dh%02dm" % (m // 60, m % 60)


def op_rows(D):
    now = D["now"]
    rows = []
    for oid in OPS:
        o = D["ops"][oid]
        h = o["h"]
        upd, eta = iso(h.get("updated", "")), iso(h.get("eta", ""))
        state = h.get("state", "?")
        stale = False
        if oid != "SPV" and state not in ("DONE", "FREE") and upd:
            age = (now - upd).total_seconds() / 60
            stale = (age > IDLE_MIN) if not eta else ((now - eta).total_seconds() / 60 > LATE_MIN and upd <= eta)
        done_ids = {norm(d["id"]) for d in D["delivs"] if d["verdict"] == "APPROVED"}
        mine = {norm(d["id"]) for d in D["delivs"] if d["op"] == oid}
        queue = []
        for i in inbox_ids(o["inbox"], D["known"]):
            if i in mine or D["status"].get(D["known"][i]) in ("approved", "review"):
                continue
            queue.append(D["known"][i])
        aw = ["%s %s" % (d["id"], d["sha"][:8]) for d in D["awaiting"] if d["op"] == oid]
        rows.append({"id": oid, "state": state, "age": age_str(now, upd), "stale": stale,
                     "task": h.get("task", ""), "queue": queue, "awaiting": aw, "done": done_ids})
    return rows


def unassigned(D):
    return [s for s in D["wo_ids"] if D["status"][s] == "unassigned"]


def reviewed_recent(D, n=5):
    seen, out = set(), []
    for et, v, i, txt in reversed(D["events"]):
        if i in seen or i.startswith("sha:"):
            continue
        seen.add(i)
        out.append("%s %s %s" % (et.strftime("%d %H:%M"), "OK " if v == "APPROVED" else "CHG", i))
        if len(out) == n:
            break
    return out


def requests(D):
    out = []
    for oid in OPS:
        text = D["ops"][oid]["text"]
        h = D["ops"][oid]["h"]
        ds = [d for d in D["delivs"] if d["op"] == oid]
        last_t = max((d["t"] for d in ds if d["t"]), default=None)
        last_line = max((d["line"] for d in ds), default=0)
        upd = iso(h.get("updated", ""))
        if "BLOCKED" in h.get("task", "").upper() or "richiesta utente" in h.get("task", "").lower():
            if not last_t or (upd and upd > last_t):
                out.append("%s header: %s" % (oid, h.get("task", "")[:90]))
        for n, line in enumerate(text.splitlines(), 1):
            low = line.lower()
            if not ("richiesta utente" in low or "user request" in low or re.search(r"\bBLOCKED\b", line)):
                continue
            if line.strip().startswith("DELIVERED") or line.lstrip().startswith("#"):
                continue
            dm = re.search(r"(\d{4}-\d\d-\d\d)(?:T(\d\d:\d\d))?", line)
            newer = True
            if last_t and dm and dm.group(2):
                lt = iso(dm.group(1) + "T" + dm.group(2))
                newer = bool(lt and lt > last_t)
            elif last_t and dm and dm.group(1) != last_t.strftime("%Y-%m-%d"):
                newer = dm.group(1) > last_t.strftime("%Y-%m-%d")
            elif last_t:
                newer = n > last_line
            if "CLOSED @" in line or not newer:
                continue
            out.append("%s:%d %s" % (oid, n, re.sub(r"\s+", " ", line.strip())[:100]))
    return out


def next_pending():
    text = show("sync/NEXT.md")
    m = re.search(r"^#+\s*6 IN SOSPESO CON L'UTENTE\s*\n(.*?)(?=^#+\s|\Z)", text, re.S | re.M)
    return len(re.findall(r"^\d+\.", m.group(1), re.M)) if m else 0


def group_of(s):
    if s.startswith("R-NET"):
        return "R-NET"
    m = re.match(r"([A-Z]\d+)-", s)
    if m:
        return m.group(1)
    m = re.match(r"\d*[A-Za-z]+", s)
    return m.group(0)


def bar(done, total, width=20):
    pct = int(round(100.0 * done / total)) if total else 0
    n = int(round(width * done / total)) if total else 0
    return "[%s%s] %d%%" % ("#" * n, "-" * (width - n), pct)


def beta(D):
    cnt = {"approved": 0, "review": 0, "assigned": 0, "unassigned": 0}
    groups = {}
    for s in D["wo_ids"]:
        st = D["status"][s]
        cnt[st] += 1
        g = groups.setdefault(group_of(s), {"approved": 0, "review": 0, "assigned": 0, "unassigned": 0})
        g[st] += 1
    return cnt, groups


def next_tick(now, mins):
    """next multiple of mins minutes on the wall clock (15 -> :00 :15 :30 :45)"""
    mins = max(int(mins), 1)
    base = now.replace(minute=0, second=0, microsecond=0)
    k = int((now - base).total_seconds() // 60 // mins) + 1
    return base + datetime.timedelta(minutes=k * mins)


def render_sections(D, interval):
    now = D["now"]
    nxt = next_tick(now, interval) if interval else None
    S = []   # [(title, [lines])]
    S.append(("Status", ["%s   next update %s   (branch %s, Log rows %d)" % (
        now.strftime("%Y-%m-%d %H:%M"), nxt.strftime("%H:%M") if nxt else "on demand", BRANCH, len(D["log"]))]))
    ol = []
    for r in op_rows(D):
        ol.append("%-4s %-8s age %-7s%s  %s" % (r["id"], r["state"], r["age"], " STALE" if r["stale"] else "      ", r["task"][:60]))
        if r["queue"]:
            ol.append("     queue: " + ", ".join(r["queue"]))
        if r["awaiting"]:
            ol.append("     awaiting review: " + ", ".join(r["awaiting"]))
    S.append(("Operators", ol))
    un = unassigned(D)
    S.append(("WOs not assigned (%d)" % len(un), [", ".join(un) or "none"]))
    aw = ["%s %s (%s)" % (d["id"], d["sha"][:8], d["op"]) for d in D["awaiting"]]
    S.append(("Reviews", ["awaiting (%d): %s" % (len(aw), ", ".join(aw) or "none"),
                          "recent: " + (", ".join(reviewed_recent(D)) or "none"),
                          "legacy, delivered before 17 Oct with no REVIEW row matched (%d): %s" % (
                              len(D["legacy"]), ", ".join(d["id"] for d in D["legacy"]) or "none")]))
    rq = requests(D)
    S.append(("Requests for the user", rq + ["NEXT.md section 10 block 6: %d items pending with the user" % next_pending()]))
    cnt, groups = beta(D)
    total = len(D["wo_ids"])
    bl = ["Assumption: Beta scope = all WO files in sync/wo (index/README/draft files excluded).",
          "%s  done %d / %d  review %d  assigned %d  unassigned %d" % (
              bar(cnt["approved"], total), cnt["approved"], total, cnt["review"], cnt["assigned"], cnt["unassigned"])]
    for g in sorted(groups):
        v = groups[g]
        t = sum(v.values())
        bl.append("%-6s %s %d/%d  rev %d asg %d una %d" % (g, bar(v["approved"], t, 10), v["approved"], t, v["review"], v["assigned"], v["unassigned"]))
    S.append(("Beta progress", bl))
    try:
        rows, warns = cost_audit()
    except Exception as e:           # the audit never breaks the page
        rows, warns = ["cost audit failed: %s" % e], []
    S.append(("Cost audit (session transcripts)", rows + (warns or ["no warnings"])))
    return S


def to_md(S):
    out = ["# bm sync status", ""]
    for title, lines in S:
        out += ["## " + title, "```"] + lines + ["```", ""]
    return "\n".join(out)


HTML = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="60"><title>bm sync status</title>
<style>
:root{color-scheme:dark;--bg:#121214;--fg:#ececf0;--card:#1c1c20;--line:#3a3a42;--acc:#6fe3a5}
a{color:#8ec5ff}
body{margin:0;padding:12px 16px;background:var(--bg);color:var(--fg);font:14px/1.4 system-ui,sans-serif}
h1{font-size:18px;margin:0 0 8px}h2{font-size:14px;margin:0 0 4px;color:var(--acc)}
section{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:8px 10px;margin:0 0 8px;max-width:960px}
pre{margin:0;font:12.5px/1.35 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-word}
</style></head><body><h1>bm sync status</h1>
%s
</body></html>
"""


def to_html(S):
    body = "\n".join("<section><h2>%s</h2><pre>%s</pre></section>" % (html.escape(t), html.escape("\n".join(l))) for t, l in S)
    return HTML % body


def atomic_write(path, text):
    d = os.path.dirname(path)
    fd, tmp = tempfile.mkstemp(dir=d, suffix=".tmp")
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    for k in range(5):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.5)
    os.replace(tmp, path)


def snapshot(interval, do_fetch=True):
    if do_fetch:
        fetch()
    D = collect(datetime.datetime.now().astimezone())
    S = render_sections(D, interval)
    os.makedirs(OUT_DIR, exist_ok=True)
    atomic_write(os.path.join(OUT_DIR, "status.md"), to_md(S))
    atomic_write(os.path.join(OUT_DIR, "status.html"), to_html(S))
    return D, S


def summary(D):
    rows = op_rows(D)
    cnt, _ = beta(D)
    total = len(D["wo_ids"])
    return [
        "sync %s  Beta %s %d/%d  review %d  assigned %d  unassigned %d" % (
            D["now"].strftime("%H:%M"), bar(cnt["approved"], total, 10), cnt["approved"], total, cnt["review"], cnt["assigned"], cnt["unassigned"]),
        "operators: " + "; ".join("%s %s %s%s" % (r["id"], r["state"], r["age"], " STALE" if r["stale"] else "") for r in rows),
        "queues: " + "; ".join("%s %s" % (r["id"], ",".join(r["queue"][:4]) or "-") for r in rows if r["id"] != "SPV"),
        "awaiting review: " + (", ".join("%s(%s)" % (d["id"], d["op"]) for d in D["awaiting"]) or "none"),
        "not assigned: %d  user requests: %d  NEXT block 6: %d" % (len(unassigned(D)), len(requests(D)), next_pending()),
        "recent reviews: " + (", ".join(reviewed_recent(D, 3)) or "none"),
    ]


def log_err(msg):
    try:
        os.makedirs(OUT_DIR, exist_ok=True)
        with open(os.path.join(OUT_DIR, "status.log"), "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (datetime.datetime.now().isoformat(timespec="seconds"), msg))
    except OSError:
        pass


def main(argv=None):
    global WT
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--once", action="store_true")
    p.add_argument("--loop", nargs="?", const=15, type=int, metavar="MIN")
    p.add_argument("--summary", action="store_true")
    p.add_argument("--every", type=int, default=0, metavar="MIN", help="with --once: show this as the next-update interval (for an external scheduler)")
    p.add_argument("--selftest", action="store_true")
    p.add_argument("--worktree", help="worktree used for git (default: the one holding this tool)")
    a = p.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.worktree:
        WT = os.path.abspath(a.worktree)
    if a.loop is not None:
        while True:
            try:
                snapshot(a.loop)
                print(datetime.datetime.now().strftime("%H:%M"), "snapshot written", flush=True)
            except Exception as e:  # the loop survives any error
                log_err("%s: %s" % (type(e).__name__, e))
                print("error (logged):", e, flush=True)
            nt = next_tick(datetime.datetime.now().astimezone(), a.loop)
            time.sleep(max((nt - datetime.datetime.now().astimezone()).total_seconds(), 1))
    if a.summary:
        try:
            fetch()
        except Exception as e:
            log_err("summary fetch: %s" % e)
        print("\n".join(summary(collect(datetime.datetime.now().astimezone()))))
        return 0
    D, S = snapshot(a.every)
    print(to_md(S))
    return 0


if __name__ == "__main__":
    sys.exit(main())
