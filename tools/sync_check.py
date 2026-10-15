#!/usr/bin/env python3
"""Objective check of the sync (SYNC.md section 3c): who is alive, who has read what, what breaks the protocol.

  python3 tools/sync_check.py [--strict]

For every operator of sync/operators.toml (active ones) it reads the status block of sync/<ID>.md and prints:
  age of `updated`, `eta`, `ack` (the last Log row of SYNC.md it declares read), the Log rows addressed to it after that `ack`
  (unread), the size of its inbox sync/inbox/<ID>.md, and the problems:
  - no or broken status block; state not in DONE/WORKING/WAITING/IDLE/FREE;
  - IDLE by rule: WORKING/WAITING more than 10 min past its `eta` without an update, or 60 min without `eta` (never FREE/DONE);
  - unread: a row addressed to the operator is older than 30 min and newer than its `ack`;
  - a line of `## Consegne` that is not `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: ... · {ISO}`;
  - the inbox has more than 5 non-empty lines;
  - Log rows longer than 600 characters (ALL rows of the Log, not only the last ones).
The state to tell the user is the one printed here (file, with its time), never a guess. Exit 1 with --strict if there is any problem."""
import datetime
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
STATES = ("DONE", "WORKING", "WAITING", "IDLE", "FREE")
IDLE_MIN, LATE_MIN, UNREAD_MIN, ROW_MAX, INBOX_MAX = 60, 10, 30, 600, 5
DELIVERED = re.compile(r"^[-*]?\s*DELIVERED\s+\S+\s+@\s+\S+@\S+\s+[·\-]\s+verifica:\s+.+(?:→|->).+[·\-]\s+\d{4}-\d\d-\d\dT\d\d:\d\d")


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
    print("sync check at %s (Log rows: %d, last %s)" % (now.strftime("%H:%M"), len(log), log[-1][0].strftime("%H:%M") if log else "-"))
    print("%-5s %-8s %5s %-17s %-17s %6s %5s  %s" % ("ID", "state", "age", "eta", "ack", "unread", "inbox", "notes"))
    for oid, active in toml_ops():
        if not active:
            continue
        path = os.path.join(ROOT, "sync", oid + ".md")
        notes = []
        if not os.path.exists(path):
            print("%-5s %-8s %5s %-17s %-17s %6s %5s  %s" % (oid, "-", "-", "-", "-", "-", "-", "no file"))
            problems.append("%s: no sync/%s.md (handshake not done)" % (oid, oid))
            continue
        text = open(path, encoding="utf-8").read()
        h = header(text)
        if not h:                               # the old format: **Ultimo aggiornamento:** 2026-10-14 18:26 / **Stato:** `...`
            mu = re.search(r"\*\*Ultimo aggiornamento:\*\*\s*(\d{4}-\d\d-\d\d)[ T](\d\d:\d\d)\s*([+-]\d\d:?\d\d)?", text)
            ms = re.search(r"\*\*Stato:\*\*\s*`?(\w+)", text)
            if not (mu and ms):
                print("%-5s %-8s %5s %-17s %-17s %6s %5s  %s" % (oid, "-", "-", "-", "-", "-", "-", "no status"))
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
        seen = ack or upd                        # without `ack` an operator is taken to have read up to its last update
        addressed = []
        for t, line in log:
            if seen and t <= seen:
                continue
            author = line.split("|")[2] if line.count("|") > 2 else ""
            if re.search(r"(?<![A-Za-z])%s(?![A-Za-z])" % re.escape(oid), author):
                continue
            target_text = "|".join(line.split("|")[4:]) if line.count("|") > 4 else line
            if re.search(r"(?<![A-Za-z])%s(?![A-Za-z])|TUTTI|Tutti|tutti" % re.escape(oid), target_text):
                addressed.append(t)
        stale = [t for t in addressed if (now - t).total_seconds() > UNREAD_MIN * 60]
        if stale and oid != "SPV" and state != "DONE":
            problems.append("%s: %d row(s) addressed to it not acknowledged (oldest %s, ack %s)" % (oid, len(stale), min(stale).strftime("%H:%M"), h.get("ack") or "none"))
        if not h.get("ack") and oid != "SPV":
            notes.append("no ack")
        if h.get("wakeup") not in ("yes", "no"):
            notes.append("wakeup?")
        m = re.search(r"^## Consegne\s*\n(.*?)(?=^## |\Z)", text, re.S | re.M)
        for line in (m.group(1).splitlines() if m else []):
            if line.strip() and not line.strip().startswith("(") and not DELIVERED.match(line.strip()):
                problems.append("%s: not a standard delivery line: %s" % (oid, line.strip()[:70]))
        inbox = os.path.join(ROOT, "sync", "inbox", oid + ".md")
        n_in = sum(1 for l in open(inbox, encoding="utf-8") if l.strip()) if os.path.exists(inbox) else 0
        if n_in > INBOX_MAX:
            problems.append("%s: inbox has %d lines (max %d)" % (oid, n_in, INBOX_MAX))
        print("%-5s %-8s %4sm %-17s %-17s %6d %5s  %s" % (oid, state, age if age is not None else "?", h.get("eta", "")[:16] or "-", h.get("ack", "")[:16] or "-", len(addressed), n_in or "-", " ".join(notes)))
    for t, line in log:
        if len(line) > ROW_MAX:
            problems.append("Log row of %s is %d chars (max %d): %s..." % (t.strftime("%H:%M"), len(line), ROW_MAX, line[28:70]))
    print()
    for p in problems:
        print("  ! " + p)
    print("%d problem(s)" % len(problems))
    return 1 if (problems and "--strict" in argv) else 0


if __name__ == "__main__":
    sys.exit(main())
