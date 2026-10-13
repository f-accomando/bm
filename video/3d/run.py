#!/usr/bin/env python3
"""make video-3d-NN: skeleton. Prints what the episode will do and what it needs.
Recording is not implemented yet (docs/video/STATUS.md); exit 2 for a blocked episode."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
s = json.load(open(os.path.join(HERE, "series.json"), encoding="utf-8"))
n = int(sys.argv[1])
v = next(x for x in s["videos"] if x["n"] == n)
d = os.path.join(HERE, v["dir"])
print(f"video {n:02d} {v['dir']} ({v['tool']})")
print(f"  needs : {[f'chain/{u['out']}' for u in s['videos'] if u['n'] in v['needs']] or 'nothing'}")
print(f"  makes : chain/{v['out']}" if v["out"] else "  makes : the gameplay video")
print(f"  docs  : docs/video/{v['dir']}/")
if v["blocked"]:
    print(f"  BLOCKED: {v['blocked']}")
    sys.exit(2)
if not os.path.exists(os.path.join(d, "record.py")):
    print(f"  record.py missing in video/3d/{v['dir']}/ (not implemented yet)")
    sys.exit(0)
os.execvp(sys.executable, [sys.executable, os.path.join(d, "record.py")])
