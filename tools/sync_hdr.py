#!/usr/bin/env python3
"""Writes the status block at the top of sync/<ID>.md (--- ... ---) with `updated` taken from the clock (ISO 8601, time zone).

  python3 tools/sync_hdr.py AGY --state WORKING --task "AGY-09 passo 3" [--waiting-on utente] [--progress 40] [--eta 2026-10-14T12:20+02:00]
  python3 tools/sync_hdr.py CLD                      # only refresh `updated` (a heartbeat)

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
KEYS = ["schema", "id", "state", "task", "waiting_on", "progress", "eta", "updated"]


def parse(text):
    m = re.match(r"---\r?\n(.*?)\r?\n---\r?\n", text, re.S)
    if not m:
        return {}, text
    d = {}
    for line in m.group(1).splitlines():
        k, _, v = line.partition(":")
        d[k.strip()] = v.split("  #")[0].strip()
    return d, text[m.end():]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id")
    ap.add_argument("--state", choices=STATES)
    ap.add_argument("--task")
    ap.add_argument("--waiting-on", dest="waiting_on")
    ap.add_argument("--progress", type=int)
    ap.add_argument("--eta")
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
        d["eta"] = a.eta
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
