#!/usr/bin/env python3
"""Writes the status block at the top of sync/<ID>.md (--- ... ---) with `updated` taken from the clock (ISO 8601, time zone).

  python3 tools/sync_hdr.py AGY --state WORKING --task "AGY-09 passo 3" [--waiting-on utente] [--progress 40] [--eta 18:00]
  python3 tools/sync_hdr.py CLD --ack auto           # a heartbeat that also acknowledges the last row of SYNC.md read
  python3 tools/sync_hdr.py CLD --wakeup no          # profile: this agent has no autonomous wake-up (yes | no)

state: DONE | WORKING | WAITING | IDLE | FREE;  waiting_on (only with WAITING): utente | Antigravity | Claude cloud | Claude local.
The text of the file is kept; the old lines '**Ultimo aggiornamento:**' and '**Stato:**' are refreshed too, if the file has them.
Then: git add sync/<ID>.md && git commit && git pull --rebase origin bm-core && git push origin HEAD:bm-core."""
import argparse
import datetime
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
STATES = ("DONE", "WORKING", "WAITING", "IDLE", "FREE")
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id")
    ap.add_argument("--state", choices=STATES)
    ap.add_argument("--task")
    ap.add_argument("--waiting-on", dest="waiting_on")
    ap.add_argument("--progress", type=int)
    ap.add_argument("--eta")
    ap.add_argument("--ack", help="ISO time of the last SYNC.md Log row read, or `auto` (the last row now in SYNC.md)")
    ap.add_argument("--wakeup", choices=("yes", "no"), help="does the agent wake itself up (timer, scheduler)?")
    ap.add_argument("--file")
    a = ap.parse_args()
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
    open(path, "w", encoding="utf-8", newline="").write((head + body).replace("\n", nl))
    print("%s: %s %s (updated %s)" % (path, d["state"], d.get("task", ""), d["updated"]))


if __name__ == "__main__":
    main()
