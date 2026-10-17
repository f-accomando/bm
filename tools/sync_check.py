#!/usr/bin/env python3
"""Objective check of the sync (SYNC.md section 3c): who is alive, who has read what, what breaks the protocol.

  python3 tools/sync_check.py [--strict] [--closeout]

For every operator of sync/operators.toml (active ones) it reads the status block of sync/<ID>.md and prints:
  age of `updated`, `eta`, `ack` (the last Log row of SYNC.md it declares read), the Log rows addressed to it after that `ack`
  (unread), the size of its inbox sync/inbox/<ID>.md, and the problems:
  - no or broken status block; state not in DONE/WORKING/WAITING/IDLE/FREE;
  - IDLE by rule: WORKING/WAITING more than 10 min past its `eta` without an update, or 60 min without `eta` (never FREE/DONE);
  - unread: a row addressed to the operator is older than 30 min and newer than its `ack`;
  - a line of `## Consegne` that is not `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: ... · {ISO}`;
  - the inbox has more than 5 non-empty lines;
  - Log rows longer than 600 characters (ALL rows of the Log, not only the last ones).
With --closeout (CLOSEOUT, sync/wo/CLOSEOUT.md section 2b) it adds the `closeout` column, the closing state of each operator
  (ACTIVE, DRAINING, SIGNOFF-SENT, SIGNOFF-ACK, SIGNED-OFF, UNRESPONSIVE, FORCED) and a last line CLOSEOUT-READY (every active
  operator but SPV is SIGNED-OFF or FORCED), CLOSEOUT-END (READY, a `CLOSEOUT` Log line starting with END, SPV DONE) or
  CLOSEOUT-NOT-READY: ID=state,...; with --strict the exit is 1 on NOT-READY.
The state to tell the user is the one printed here (file, with its time), never a guess. Exit 1 with --strict if there is any problem."""
import datetime
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
STATES = ("DONE", "WORKING", "WAITING", "IDLE", "FREE")
IDLE_MIN, LATE_MIN, UNREAD_MIN, ROW_MAX, INBOX_MAX = 60, 10, 30, 600, 5
DELIVERED = re.compile(r"^[-*]?\s*DELIVERED\s+\S+\s+@\s+\S+@\S+\s+[·\-]\s+verifica:\s+.+(?:→|->).+[·\-]\s+\d{4}-\d\d-\d\dT\d\d:\d\d")
CLOSED = re.compile(r"^[-*]?\s*CLOSED\s+@\s+\S+@\S+\s+·\s+tree clean\s+·\s+aperti:\s+.+·\s+\d{4}-\d\d-\d\dT\d\d:\d\d")
SIGNOFF_WAIT_MIN, ETA_GRACE_MIN = 30, 10


DELIVERED_PARTS = re.compile(r"^[-*]?\s*DELIVERED\s+(\S+)\s+@\s+\S+@(\S+)\s")


def toml_ops():
    """[(id, active)] from sync/operators.toml (tomllib on 3.11+, else a plain scan)"""
    path = os.path.join(ROOT, "sync", "operators.toml")
    text = open(path, encoding="utf-8").read()
    try:
        import tomllib
        return [(o["id"], o.get("active", True) and o.get("kind") != "human") for o in tomllib.loads(text).get("operator", [])]
    except ImportError:
        out = []
        for blk in text.split("[[operator]]")[1:]:
            m = re.search(r'^id\s*=\s*"([^"]+)"', blk, re.M)
            if m:
                out.append((m.group(1), "active = false" not in blk and 'kind = "human"' not in blk))
        return out


def header(text):
    m = re.match(r"---\r?\n(.*?)\r?\n---", text, re.S)
    if not m:
        return None
    d = {}
    for line in m.group(1).splitlines():
        k, _, v = line.partition(":")
        val = re.split(r"\s+#", v)[0].strip() if "#" in v else v.strip()
        d[k.strip()] = val
    return d


def iso(s):
    try:
        t = datetime.datetime.fromisoformat(s)
        return t if t.tzinfo else t.astimezone()
    except (ValueError, TypeError):
        return None


def rows(path):
    """[(datetime, full line)] of the Log rows `| 2026-10-14 18:35 | ...` (the time as written; Rome time = the local zone)"""
    out = []
    if not os.path.exists(path):
        return out
    local = datetime.datetime.now().astimezone().tzinfo
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\|\s*(\d{4}-\d\d-\d\d)[ T](\d\d:\d\d)(\S*)\s*\|", line)
        if m:
            t = iso(m.group(1) + "T" + m.group(2) + (m.group(3) or ""))
            if t:
                out.append((t if t.tzinfo else t.replace(tzinfo=local), line.rstrip("\r\n")))
    return out


def consegne(text):
    """the lines of the ## Consegne section of an operator file"""
    m = re.search(r"^## Consegne\s*\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    return [ln.strip() for ln in (m.group(1).splitlines() if m else []) if ln.strip()]


def delivered_parts(line):
    """(WO-ID, commit) of a standard DELIVERED line, or None"""
    m = DELIVERED_PARTS.match(line)
    return (m.group(1), m.group(2)) if m else None


def addressed_rows(oid, h, log):
    """times of the Log rows addressed to oid after its `ack` (or its last update), written by others"""
    seen = iso(h.get("ack", "")) or iso(h.get("updated", ""))
    out = []
    for t, line in log:
        if seen and t <= seen:
            continue
        author = line.split("|")[2] if line.count("|") > 2 else ""
        if re.search(r"(?<![A-Za-z])%s(?![A-Za-z])" % re.escape(oid), author):
            continue
        target_text = "|".join(line.split("|")[4:]) if line.count("|") > 4 else line
        if re.search(r"(?<![A-Za-z])%s(?![A-Za-z])|TUTTI|Tutti|tutti" % re.escape(oid), target_text):
            out.append(t)
    return out


def closeout_rows(log):
    """[(datetime, action, message)] of the Log rows that matter for the CLOSEOUT: the action is the 4th cell (backticks
    removed), the message the rest"""
    out = []
    for t, line in log:
        cells = line.split("|")
        if len(cells) < 6:
            continue
        action = cells[4].replace("`", "").strip()
        if action in ("CLOSEOUT", "SIGNOFF", "ASSIGNED", "WO", "HANDSHAKE"):
            out.append((t, action, "|".join(cells[5:]).strip().rstrip("|").strip()))
    return out


def has_closed_line(text):
    """True if the ## Chiusura section of an operator file has a valid CLOSED line"""
    m = re.search(r"^## Chiusura\s*\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    return bool(m) and any(CLOSED.match(ln.strip()) for ln in m.group(1).splitlines())


def closeout_state(oid, h, text, log, now):
    """closing state of one operator (CLOSEOUT.md 2b): ACTIVE, DRAINING, SIGNOFF-SENT, SIGNOFF-ACK, SIGNED-OFF,
    UNRESPONSIVE or FORCED. Only the lines after the last `CLOSEOUT DRAIN` count (older SIGNOFF rows belong to
    earlier sessions); an assignment or a handshake after the SIGNOFF brings the operator back to ACTIVE."""
    crows = closeout_rows(log)
    drains = [t for t, a, m in crows if a == "CLOSEOUT" and m.startswith("DRAIN")]
    if not drains:
        return "ACTIVE"
    drain = max(drains)
    sign = re.compile(r"^(?:SIGNOFF\s+)?%s\b(\s+FORCED)?" % re.escape(oid))
    addr = re.compile(r"(?<![A-Za-z])%s(?![A-Za-z])" % re.escape(oid))
    signoff, forced = None, False
    for t, a, m in crows:
        mm = sign.match(m) if a == "SIGNOFF" and t >= drain else None
        if mm and (signoff is None or t >= signoff):
            signoff, forced = t, bool(mm.group(1))
    state = h.get("state", "")
    if signoff is None:
        return "DRAINING" if state in ("WORKING", "WAITING") else "ACTIVE"
    if any(a in ("ASSIGNED", "WO", "HANDSHAKE") and t > signoff and addr.search(m) for t, a, m in crows):
        return "ACTIVE"
    if forced:
        return "FORCED"
    ack = iso(h.get("ack", ""))
    if ack and ack >= signoff and state == "DONE" and has_closed_line(text):
        return "SIGNED-OFF"
    eta = iso(h.get("eta", ""))
    if state == "WORKING" and eta:
        limit = eta + datetime.timedelta(minutes=ETA_GRACE_MIN)
    else:
        limit = signoff + datetime.timedelta(minutes=SIGNOFF_WAIT_MIN)
    if now > limit:
        return "UNRESPONSIVE"
    return "SIGNOFF-ACK" if ack and ack >= signoff else "SIGNOFF-SENT"


def toml_branches():
    """{id: [branches]} of sync/operators.toml (the `branches` key of each operator)"""
    with open(os.path.join(ROOT, "sync", "operators.toml"), encoding="utf-8") as f:
        text = f.read()
    try:
        import tomllib
        return {o["id"]: list(o.get("branches", [])) for o in tomllib.loads(text).get("operator", [])}
    except ImportError:
        out = {}
        for blk in text.split("[[operator]]")[1:]:
            m = re.search(r'^id\s*=\s*"([^"]+)"', blk, re.M)
            b = re.search(r"^branches\s*=\s*\[(.*?)\]", blk, re.M | re.S)
            if m:
                out[m.group(1)] = re.findall(r'"([^"]+)"', b.group(1)) if b else []
        return out


def git(*args):
    """stdout of a git command run in ROOT, or None if it fails"""
    try:
        r = subprocess.run(["git"] + list(args), cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    return r.stdout if r.returncode == 0 else None


WO_ID = re.compile(r"(?<![\w-])[A-Z][A-Z0-9]*(?:-[A-Za-z0-9]+)+(?![\w-])")


def leftovers(log, ops):
    """what must not be lost at the closing hand-off (CLOSEOUT.md 2c) and the branch and worktree list (2e), one entry per
    line prefixed `  > `. `ops` is toml_ops(). The DELIVERED-without-REVIEW items need tools/spv_brief.py (missing on bm-core)."""
    out = []
    active = [oid for oid, on in ops if on]
    texts = {}
    for oid in active:
        path = os.path.join(ROOT, "sync", oid + ".md")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                texts[oid] = f.read()
    for oid in active:                                          # inboxes not empty
        path = os.path.join(ROOT, "sync", "inbox", oid + ".md")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                n = sum(1 for ln in f if ln.strip())
            if n:
                out.append("inbox %s: %d line(s) not empty" % (oid, n))
    delivered = set()                                           # WOs assigned and not delivered
    for text in texts.values():
        delivered |= {p[0] for p in map(delivered_parts, consegne(text)) if p}
    seen = set()
    for _, a, m in closeout_rows(log):
        if a not in ("ASSIGNED", "WO"):
            continue
        mm = WO_ID.search(m)
        wo = mm.group(0) if mm else None
        if wo and wo not in seen and wo not in delivered:
            seen.add(wo)
            out.append("WO %s assigned and not delivered" % wo)
    try:                                                        # DELIVERED without REVIEW
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import spv_brief
        reviews = [(t, line) for t, line in log if spv_brief.is_review(line)]
        for oid, text in texts.items():
            for wo, sha in spv_brief.pending(text, reviews):
                out.append("%s: DELIVERED %s@%s without REVIEW" % (oid, wo, sha[:8]))
    except ImportError:
        out.append("DELIVERED without REVIEW: n/d (tools/spv_brief.py missing)")
    for oid, text in texts.items():                             # CLOSED lines with open items
        for m in re.finditer(r"^[-*]?\s*CLOSED\b.*?aperti:\s*(.+?)\s*·", text, re.M):
            if m.group(1).strip().lower() != "nessuno":
                out.append("%s: closed with open items: %s" % (oid, m.group(1).strip()))
    mine = toml_branches()
    unmerged = git("branch", "-r", "--no-merged", "origin/bm-core")
    if unmerged is None:
        out.append("git: n/d")
    else:
        owner = {"origin/" + b: oid for oid, bs in mine.items() for b in bs}
        for br in (ln.strip() for ln in unmerged.splitlines()):
            if br and "->" not in br:
                out.append("branch %s not merged%s" % (br, " (%s)" % owner[br] if br in owner else ""))
    trees = git("worktree", "list", "--porcelain")
    if trees is None:
        out.append("git: n/d")
    else:
        for blk in re.split(r"\n\s*\n", trees.strip()):
            fields = dict(ln.partition(" ")[::2] for ln in blk.splitlines() if ln.strip())
            if "worktree" in fields:
                out.append("worktree %s (%s)" % (fields["worktree"], fields.get("branch", "detached").replace("refs/heads/", "")))
    return ["  > " + e for e in out]


def closeout_summary(states, spv_state, log):
    """the last line of --closeout: (text, ready). `states` {id: state} of the active operators but SPV"""
    waiting = ["%s=%s" % (i, st) for i, st in states.items() if st not in ("SIGNED-OFF", "FORCED")]
    if waiting:
        return "CLOSEOUT-NOT-READY: " + ",".join(waiting), False
    ended = any(a == "CLOSEOUT" and m.startswith("END") for _, a, m in closeout_rows(log))
    return ("CLOSEOUT-END" if ended and spv_state == "DONE" else "CLOSEOUT-READY"), True


def main():
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    argv = sys.argv[1:]
    now = datetime.datetime.now().astimezone()
    log = rows(os.path.join(ROOT, "SYNC.md"))
    problems = []
    co = "--closeout" in argv
    co_states, hdr_states = {}, {}              # closing state and `state` of each operator
    print("sync check at %s (Log rows: %d, last %s)" % (now.strftime("%H:%M"), len(log), log[-1][0].strftime("%H:%M") if log else "-"))
    print("%-5s %-8s %5s %-17s %-17s %6s %5s  %s%s" % ("ID", "state", "age", "eta", "ack", "unread", "inbox", "%-13s " % "closeout" if co else "", "notes"))
    for oid, active in toml_ops():
        if not active:
            continue
        path = os.path.join(ROOT, "sync", oid + ".md")
        notes = []
        if not os.path.exists(path):
            print("%-5s %-8s %5s %-17s %-17s %6s %5s  %s%s" % (oid, "-", "-", "-", "-", "-", "-", "%-13s " % "NO-FILE" if co else "", "no file"))
            co_states[oid] = "NO-FILE"
            problems.append("%s: no sync/%s.md (handshake not done)" % (oid, oid))
            continue
        text = open(path, encoding="utf-8").read()
        h = header(text)
        if not h:                               # the old format: **Ultimo aggiornamento:** 2026-10-14 18:26 / **Stato:** `...`
            mu = re.search(r"\*\*Ultimo aggiornamento:\*\*\s*(\d{4}-\d\d-\d\d)[ T](\d\d:\d\d)\s*([+-]\d\d:?\d\d)?", text)
            ms = re.search(r"\*\*Stato:\*\*\s*`?(\w+)", text)
            if not (mu and ms):
                print("%-5s %-8s %5s %-17s %-17s %6s %5s  %s%s" % (oid, "-", "-", "-", "-", "-", "-", "%-13s " % "NO-STATUS" if co else "", "no status"))
                co_states[oid] = "NO-STATUS"
                problems.append("%s: no status block and no old-style lines (write it with tools/sync_hdr.py)" % oid)
                continue
            z = mu.group(3) or now.strftime("%z")[:3] + ":" + now.strftime("%z")[3:]
            h = {"state": ms.group(1), "updated": "%sT%s%s" % (mu.group(1), mu.group(2), z if ":" in z else z[:3] + ":" + z[3:])}
            problems.append("%s: old status format, no block (use tools/sync_hdr.py)" % oid)
            notes.append("old format")
        state = h.get("state", "")
        if state not in STATES:
            problems.append("%s: state '%s' is not one of %s" % (oid, state, "/".join(STATES)))
        upd, eta, ack = iso(h.get("updated", "")), iso(h.get("eta", "")), iso(h.get("ack", ""))
        age = int((now - upd).total_seconds() // 60) if upd else None
        if age is None:
            problems.append("%s: `updated` missing or not ISO 8601 with zone" % oid)
        elif oid != "SPV" and state not in ("DONE", "FREE") and (
                (age > IDLE_MIN) if not eta else (int((now - eta).total_seconds() // 60) > LATE_MIN and upd <= eta)):
            problems.append("%s: IDLE by rule (no update for %d min, eta %s); tell the user" % (oid, age, h.get("eta") or "none"))
            notes.append("IDLE")
        addressed = addressed_rows(oid, h, log)  # without `ack` an operator is taken to have read up to its last update
        stale = [t for t in addressed if (now - t).total_seconds() > UNREAD_MIN * 60]
        if stale and oid != "SPV" and state != "DONE":
            problems.append("%s: %d row(s) addressed to it not acknowledged (oldest %s, ack %s)" % (oid, len(stale), min(stale).strftime("%H:%M"), h.get("ack") or "none"))
        if not h.get("ack") and oid != "SPV":
            notes.append("no ack")
        if h.get("wakeup") not in ("yes", "no"):
            notes.append("wakeup?")
        for line in consegne(text):
            if not line.startswith("(") and not DELIVERED.match(line):
                problems.append("%s: not a standard delivery line: %s" % (oid, line.strip()[:70]))
        inbox = os.path.join(ROOT, "sync", "inbox", oid + ".md")
        n_in = sum(1 for l in open(inbox, encoding="utf-8") if l.strip()) if os.path.exists(inbox) else 0
        if n_in > INBOX_MAX:
            problems.append("%s: inbox has %d lines (max %d)" % (oid, n_in, INBOX_MAX))
        cs = closeout_state(oid, h, text, log, now) if co else ""
        co_states[oid], hdr_states[oid] = cs, state
        print("%-5s %-8s %4sm %-17s %-17s %6d %5s  %s%s" % (oid, state, age if age is not None else "?", h.get("eta", "")[:16] or "-", h.get("ack", "")[:16] or "-", len(addressed), n_in or "-", "%-13s " % cs if co else "", " ".join(notes)))
    for t, line in log:
        if len(line) > ROW_MAX:
            problems.append("Log row of %s is %d chars (max %d): %s..." % (t.strftime("%H:%M"), len(line), ROW_MAX, line[28:70]))
    print()
    for p in problems:
        print("  ! " + p)
    print("%d problem(s)" % len(problems))
    not_ready = False
    if co:
        print("\n".join(leftovers(log, toml_ops())) or "  > nothing left")
        text, ready = closeout_summary({i: st for i, st in co_states.items() if i != "SPV"}, hdr_states.get("SPV", ""), log)
        print(text)
        not_ready = not ready
    return 1 if ((problems or not_ready) and "--strict" in argv) else 0


if __name__ == "__main__":
    sys.exit(main())
