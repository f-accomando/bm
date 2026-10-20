#!/usr/bin/env python3
"""Writes the status block at the top of sync/<ID>.md (--- ... ---) with `updated` taken from the clock (ISO 8601, time zone).

  python3 tools/sync_hdr.py AGY --state WORKING --task "AGY-09 step 3" [--waiting-on user] [--progress 40] [--eta 18:00]
  python3 tools/sync_hdr.py CLD --ack auto           # a heartbeat that also acknowledges the last row of SYNC.md read
  python3 tools/sync_hdr.py CLD --wakeup no          # profile: this agent has no autonomous wake-up (yes | no)
  python3 tools/sync_hdr.py CLD --close WO-3 --commit <sha> --tokens 12000 --min 25 [--read R] [--state FREE|WAITING --waiting-on Y]
      # closes a work order: appends a JSON row to sync/<ID>.ledger.jsonl and prints the chat line
      # `done WO <X> [waiting on <Y> | FREE]: <files> files +<a> -<d>, <N> tokens, <M> min`; the status block is NOT touched.
      # files/add/del/lines come from `git show --numstat <commit>` unless given by hand (--files --add --del --lines code=..,docs=..,data=..).

  python3 tools/sync_hdr.py CLD --closed none --branch claude/x [--commit <sha>]
      # writes `CLOSED @ <branch>@<commit> · tree clean · open: <none|WO ids> · <ISO>` in the `## Closing` section
      # (an existing `## Closing` or `## Chiusura` is reused; else `## Closing` is created before `## Progress`/`## Progressi`;
      # an existing CLOSED line is replaced), then applies --state DONE --ack auto.
      # --commit defaults to `git rev-parse --short HEAD`.

state: DONE | WORKING | WAITING | FREE;  waiting_on (only with WAITING): utente | Antigravity | Claude cloud | Claude local.
The text of the file is kept; the old lines '**Ultimo aggiornamento:**' and '**Stato:**' are refreshed too, if the file has them.
Then: git add sync/<ID>.md && git commit && git pull --rebase origin bm-core && git push origin HEAD:bm-core."""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
STATES = ("DONE", "WORKING", "WAITING", "FREE")
KEYS = ["schema", "id", "state", "task", "waiting_on", "progress", "eta", "wakeup", "ack", "updated"]


def parse(text):
    m = re.match(r"---\r?\n(.*?)\r?\n---\r?\n", text, re.S)
    if not m:
        return {}, text
    d = {}
    for line in m.group(1).splitlines():
        k, _, v = line.partition(":")
        val = re.split(r"\s+#", v)[0].strip() if "#" in v else v.strip()
        d[k.strip()] = val
    return d, text[m.end():]


def last_row_time():
    """ISO time (Rome, +02:00 or +01:00 as written) of the last Log row of SYNC.md: `| 2026-10-14 18:35 | ...`"""
    path = os.path.join(ROOT, "SYNC.md")
    last = ""
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            m = re.match(r"\|\s*(\d{4}-\d\d-\d\d)[ T](\d\d:\d\d)(\S*)", line)
            if m:
                tz = m.group(3).strip()
                if not tz or not (tz.startswith("+") or tz.startswith("-")):
                    local_tz = datetime.datetime.now().astimezone().strftime("%z")
                    # If host is in UTC (+0000), default to Rome time (+02:00) as SYNC.md times are Rome time
                    if local_tz in ("+0000", "-0000", ""):
                        tz = "+02:00"
                    else:
                        tz = local_tz[:3] + ":" + local_tz[3:]
                last = "%sT%s%s" % (m.group(1), m.group(2), tz)
    return last


def parse_eta(v):
    """`18:00` = delivery expected at 18:00 (today, or tomorrow if already past), in the user's clock; a full ISO time is kept as given."""
    m = re.fullmatch(r"(\d{1,2}):(\d\d)", v.strip())
    if not m:
        return v
    now = datetime.datetime.now().astimezone()
    off = now.strftime("%z")
    if off in ("+0000", "-0000", ""):
        off = "+0200"
    tz = datetime.timezone(datetime.timedelta(hours=int(off[:3]), minutes=int(off[0] + off[3:])))
    now = now.astimezone(tz)
    t = now.replace(hour=int(m.group(1)), minute=int(m.group(2)), second=0, microsecond=0)
    if t < now:
        t += datetime.timedelta(days=1)
    return t.isoformat(timespec="minutes")


CODE_EXT = {"c", "h", "py", "lua", "rs", "ts", "js", "sh", "mk"}
DOC_EXT = {"md", "txt", "rst"}


def file_kind(path):
    base = os.path.basename(path)
    if base == "Makefile":
        return "code"
    ext = base.rsplit(".", 1)[-1].lower() if "." in base else ""
    return "code" if ext in CODE_EXT else "docs" if ext in DOC_EXT else "data"


def commit_stats(repo, commit):
    """(files, add, del, lines) from `git show --numstat <commit>`; None if git or the commit is missing."""
    try:
        r = subprocess.run(["git", "show", "--numstat", "--format=", commit], cwd=repo, capture_output=True, text=True)
    except OSError:
        return None
    if r.returncode != 0:
        return None
    files = add = dele = 0
    lines = {"code": [0, 0], "docs": [0, 0], "data": [0, 0]}
    for ln in r.stdout.splitlines():
        parts = ln.split("	", 2)
        if len(parts) < 3:
            continue
        a = int(parts[0]) if parts[0].isdigit() else 0
        d = int(parts[1]) if parts[1].isdigit() else 0
        files += 1
        add += a
        dele += d
        k = file_kind(parts[2].split(" => ")[-1].strip("{}"))
        lines[k][0] += a
        lines[k][1] += d
    return files, add, dele, {k: v[0] + v[1] for k, v in lines.items()}


def now_stamp():
    s = datetime.datetime.now().astimezone().strftime("%Y-%m-%dT%H:%M%z")
    return s[:-2] + ":" + s[-2:]


def close_wo(a):
    def err(msg):
        sys.stderr.write("error: %s\n" % msg)
        return 2
    if a.tokens is None or a.min is None:
        return err("--close needs --tokens and --min (the operator reports them)")
    repo = a.repo or ROOT
    files, add, dele, lines = a.files, a.add, a.dele, None
    if a.lines:
        try:
            lines = {k: int(v) for k, v in (kv.split("=") for kv in a.lines.split(","))}
            if set(lines) - {"code", "docs", "data"}:
                raise ValueError
        except ValueError:
            return err("--lines is code=N,docs=N,data=N")
    if a.commit:
        st = commit_stats(repo, a.commit)
        if st is None:
            return err("commit %s not found (or git missing) in %s" % (a.commit, repo))
        files = st[0] if files is None else files
        add = st[1] if add is None else add
        dele = st[2] if dele is None else dele
        lines = st[3] if lines is None else lines
    if files is None or add is None or dele is None:
        return err("give --commit, or --files --add --del")
    row = {"schema": 1, "wo": a.close, "branch": a.branch or "", "commit": a.commit or "", "files": files, "add": add, "del": dele}
    if lines is not None:
        row["lines"] = {k: lines.get(k, 0) for k in ("code", "docs", "data")}
    row["tokens"] = a.tokens
    if a.read:
        row["read"] = a.read
    row["min"] = a.min
    row["iso"] = now_stamp()
    path = a.ledger or os.path.join(ROOT, "sync", a.id + ".ledger.jsonl")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "a", encoding="utf-8", newline="") as f:
        f.write(json.dumps(row, separators=(",", ":")) + "\n")
    tail = "waiting on %s" % a.waiting_on if a.state == "WAITING" and a.waiting_on else a.state or "FREE"
    print("done WO %s %s: %d files +%d -%d, %d tokens, %d min" % (a.close, tail, files, add, dele, a.tokens, a.min))
    return 0


def set_closed(body, line):
    """Put `line` in the `## Closing` (or `## Chiusura`) section of body (LF text): replace a CLOSED line, else add it; create `## Closing` before `## Progress`/`## Progressi` (or at the end)."""
    m = re.search(r"^## (?:Closing|Chiusura)[ \t]*\n", body, re.M)
    if not m:
        sec = "## Closing\n\n%s\n\n" % line
        p = re.search(r"^## Progress", body, re.M)
        if p:
            return body[:p.start()] + sec + body[p.start():]
        return body.rstrip("\n") + "\n\n" + sec.rstrip("\n") + "\n"
    end = re.search(r"^## ", body[m.end():], re.M)
    stop = m.end() + end.start() if end else len(body)
    sect = body[m.end():stop]
    new, n = re.subn(r"^[-*]?[ \t]*CLOSED\s+@.*$", lambda _: line, sect, count=1, flags=re.M)
    if not n:
        new = "\n" + line + "\n" + sect.lstrip("\n") if sect.strip() else "\n" + line + "\n\n"
    return body[:m.end()] + new + body[stop:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id")
    ap.add_argument("--state", choices=STATES)
    ap.add_argument("--task")
    ap.add_argument("--waiting-on", dest="waiting_on")
    ap.add_argument("--progress", type=int)
    ap.add_argument("--eta")
    ap.add_argument("--ack", help="ISO time of the last SYNC.md Log row read, or `auto` (the last row now in SYNC.md; also the default when --ack is missing)")
    ap.add_argument("--wakeup", choices=("yes", "no"), help="does the agent wake itself up (timer, scheduler)?")
    ap.add_argument("--file")
    ap.add_argument("--close", metavar="WO", help="close a work order: ledger row + chat line (status block untouched)")
    ap.add_argument("--closed", metavar="OPEN", help="write the CLOSED line in ## Closing (OPEN: `none` or open WO ids); implies --state DONE --ack auto")
    ap.add_argument("--files", type=int)
    ap.add_argument("--add", type=int)
    ap.add_argument("--del", dest="dele", type=int)
    ap.add_argument("--tokens", type=int)
    ap.add_argument("--read", type=int)
    ap.add_argument("--min", type=int)
    ap.add_argument("--lines", help="code=N,docs=N,data=N")
    ap.add_argument("--commit")
    ap.add_argument("--branch")
    ap.add_argument("--repo", help="git repo for --commit (default: this repo)")
    ap.add_argument("--ledger", help="ledger file (default sync/<ID>.ledger.jsonl)")
    a = ap.parse_args()
    if a.close:
        sys.exit(close_wo(a))
    closed_line = None
    if a.closed:
        if not a.branch:
            sys.exit("error: --closed needs --branch")
        commit = a.commit
        if not commit:
            r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=a.repo or ROOT, capture_output=True, text=True)
            if r.returncode != 0 or not r.stdout.strip():
                sys.exit("error: --closed needs --commit (git rev-parse failed)")
            commit = r.stdout.strip()
        closed_line = "CLOSED @ %s@%s · tree clean · open: %s · %s" % (a.branch, commit, a.closed, now_stamp())
        a.state = "DONE"
        a.ack = a.ack or "auto"
    path = a.file or os.path.join(ROOT, "sync", a.id + ".md")
    text = open(path, encoding="utf-8", newline="").read() if os.path.exists(path) else "# %s\n\n" % a.id
    nl = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    d, body = parse(text)
    d.update({"schema": "1", "id": a.id})
    if a.state:
        d["state"] = a.state
        if a.state != "WAITING":
            d["waiting_on"] = ""
    if a.task is not None:
        d["task"] = a.task
    if a.waiting_on is not None:
        d["waiting_on"] = a.waiting_on
    if a.progress is not None:
        d["progress"] = str(a.progress)
    if a.eta is not None:
        d["eta"] = parse_eta(a.eta)
    if a.wakeup:
        d["wakeup"] = a.wakeup
    if a.ack:
        d["ack"] = last_row_time() if a.ack == "auto" else a.ack
    else:                                    # no --ack: the last SYNC.md row now (empty: the old value stays)
        auto = last_row_time()
        if auto:
            d["ack"] = auto
    if d.get("state") not in STATES:
        sys.exit("state? one of %s (use --state)" % "/".join(STATES))
    now = datetime.datetime.now().astimezone()
    d["updated"] = now.isoformat(timespec="minutes")
    head = "---\n" + "\n".join("%s: %s" % (k, d.get(k, "")) for k in KEYS if k in d or k in ("waiting_on",)) + "\n---\n"
    stamp = now.strftime("%Y-%m-%d %H:%M") + " " + now.strftime("%z")[:3] + ":" + now.strftime("%z")[3:]
    body = re.sub(r"(\*\*Ultimo aggiornamento:\*\*).*", lambda m: "%s %s" % (m.group(1), stamp.strip()), body, count=1)
    if a.state or a.task is not None:
        status = (d["state"] + " " + d.get("task", "")).strip()
        body = re.sub(r"(\*\*Stato:\*\*).*", lambda m: "%s `%s`" % (m.group(1), status), body, count=1)
    if closed_line:
        body = set_closed(body, closed_line)
    open(path, "w", encoding="utf-8", newline="").write((head + body).replace("\n", nl))
    print("%s: %s %s (updated %s)" % (path, d["state"], d.get("task", ""), d["updated"]))


if __name__ == "__main__":
    main()
