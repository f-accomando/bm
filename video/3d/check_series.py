#!/usr/bin/env python3
"""Consistency of the Tropic Trail documents (docs/video/**) against video/3d/series.json.
Checks: files per video, banned words (inspiration game), chain file names, model/clip/sound
names used in code-like places, resolutions, chapters of youtube.md, cover.json.
Exit 0 = coherent, 1 = problems (each printed)."""
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DOCS = os.path.join(ROOT, "docs", "video")
S = json.load(open(os.path.join(ROOT, "video", "3d", "series.json"), encoding="utf-8"))
FILES = ["script.md", "shot-list.md", "youtube.md", "input.md", "cover.json"]
BANNED = ["Crash", "Bandicoot", "Wumpa", "Naughty Dog", "Skyvale", "Kip "]
RES_OK = {"1920x1080", "1280x720", "640x360", "480x270", "320x180", "256x256", "720x720", "360x360", "1080x1920"}
problems = []


def bad(path, msg):
    problems.append(f"{os.path.relpath(path, ROOT)}: {msg}")


def secs(t):
    p = [int(x) for x in t.split(":")]
    return p[0] * 60 + p[1] if len(p) == 2 else p[0] * 3600 + p[1] * 60 + p[2]


outs = {v["out"] for v in S["videos"] if v["out"]}
for v in S["videos"]:
    d = os.path.join(DOCS, v["dir"])
    if not os.path.isdir(d):
        bad(d, "missing folder")
        continue
    for f in FILES:
        if not os.path.exists(os.path.join(d, f)):
            bad(os.path.join(d, f), "missing")
    cj = os.path.join(d, "cover.json")
    if os.path.exists(cj):
        try:
            c = json.load(open(cj, encoding="utf-8"))
            if c.get("number") != v["n"]:
                bad(cj, f"number {c.get('number')} != {v['n']}")
            if c.get("tool") != v["tool"] and v["tool"] != "GAMEPLAY":
                bad(cj, f"tool {c.get('tool')} != {v['tool']}")
            for k in ("title", "subtitle", "accent"):
                if k not in c:
                    bad(cj, f"key {k} missing")
        except ValueError as e:
            bad(cj, f"invalid JSON: {e}")
    yt = os.path.join(d, "youtube.md")
    if os.path.exists(yt):
        ch = [m for m in re.findall(r"^(\d+:\d\d(?::\d\d)?)\s+\S", open(yt, encoding="utf-8").read(), re.M)]
        if not ch:
            bad(yt, "no chapters")
        else:
            if ch[0] != "0:00":
                bad(yt, "first chapter must be 0:00")
            ts = [secs(x) for x in ch]
            if ts != sorted(ts) or len(set(ts)) != len(ts):
                bad(yt, "chapters not strictly increasing")
            if len(ch) < 3:
                bad(yt, "YouTube needs at least 3 chapters")

for root, _, names in os.walk(DOCS):
    for n in names:
        if not n.endswith((".md", ".json")):
            continue
        p = os.path.join(root, n)
        t = open(p, encoding="utf-8").read()
        for w in BANNED:
            if w in t:
                bad(p, f"banned word {w!r}")
        for m in re.findall(r"chain/(\S+?\.bm)", t):
            if m not in outs and m not in ("tropic.bm", "NN-nome.bm"):
                bad(p, f"unknown chain file {m}")
        for m in set(re.findall(r"\b(\d{3,4}x\d{3,4})\b", t)):
            if m not in RES_OK:
                bad(p, f"unexpected resolution {m}")
        for m in re.findall(r'model\("([A-Za-z_]+)"\)', t):
            if m not in S["models"]:
                bad(p, f"unknown model {m}")
        for m in re.findall(r'animate\([^,]+,\s*"([a-z_]+)"', t):
            if m not in S["clips"]:
                bad(p, f"unknown clip {m}")

for p in problems:
    print(p)
print(f"{len(problems)} problem(s)")
sys.exit(1 if problems else 0)
