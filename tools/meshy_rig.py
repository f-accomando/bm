#!/usr/bin/env python3
"""
meshy_rig: Meshy's auto-rigging (meshy.ai) on a model it made: the same
mesh with a skeleton (hips, spine, neck, head, shoulders, arms, forearms,
hands, legs, feet) and the weight of each joint on each vertex, as a .glb.
Overbit's art/meshyrig.py takes the joints and the bone of each vertex
from it to put the model on a hero's skeleton.

  tools/meshy_rig.py --task REFINE_ID -o OUT.glb [--height 1.8]
  tools/meshy_rig.py --rig RIG_ID -o OUT.glb       (a rigging already made)

The key: MESHY_API_KEY in the environment. The rigging's id goes to
OUT.task.txt.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import meshy_text  # noqa: E402

API = "https://api.meshy.ai/openapi/v1"


def call(method, path, key, body=None, create=False):
    old = meshy_text.API
    meshy_text.API = API
    try:
        return meshy_text.api(method, path, key, body, create=create)
    finally:
        meshy_text.API = old


def urls(x, out):
    """every (key path, url) in a reply"""
    if isinstance(x, dict):
        for k, v in x.items():
            if isinstance(v, str) and v.startswith("https://"):
                out.append((k, v))
            else:
                urls(v, out)
    elif isinstance(x, list):
        for v in x:
            urls(v, out)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--task", help="the text-to-3D (refine) or image-to-3D task of the model")
    ap.add_argument("--rig", help="a rigging task already made")
    ap.add_argument("--height", type=float, default=1.8, help="the figure's height in metres")
    ap.add_argument("--wait", type=int, default=30)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    key = os.environ.get("MESHY_API_KEY")
    if not key:
        raise SystemExit("MESHY_API_KEY: the key from meshy.ai, in the environment")
    rid = a.rig
    if not rid:
        if not a.task:
            ap.error("--task or --rig")
        rid = call("POST", "/rigging", key, {"input_task_id": a.task, "height_meters": a.height}, create=True)["result"]
        with open(os.path.splitext(a.out)[0] + ".task.txt", "w") as f:
            f.write(f"rigging {rid} of {a.task}\n")
    print(f"meshy: rigging {rid}", flush=True)
    deadline = time.time() + a.wait * 60
    last = None
    while True:
        t = call("GET", f"/rigging/{rid}", key)
        status, progress = t.get("status"), t.get("progress", 0)
        if (status, progress) != last:
            print(f"  rigging: {status} {progress}%", flush=True)
            last = (status, progress)
        if status == "SUCCEEDED":
            break
        if status in ("FAILED", "CANCELED", "EXPIRED"):
            raise SystemExit(f"meshy rigging: {status}: {(t.get('task_error') or {}).get('message', '')}")
        if time.time() > deadline:
            raise SystemExit(f"meshy rigging: still {status} after {a.wait} minutes ({rid})")
        time.sleep(6)
    found = urls(t, [])
    print("  " + json.dumps([k for k, _ in found]))
    res = t.get("result") or {}
    url = res.get("rigged_character_glb_url") or next(
        (u for k, u in found if "glb" in k and "rig" in k), None) or next(
        (u for k, u in found if ".glb" in u.split("?")[0]), None)
    if not url:
        raise SystemExit(f"meshy rigging: no glb in {json.dumps(t)[:600]}")
    meshy_text.download(url, a.out)
    walk = (res.get("basic_animations") or {}).get("walking_glb_url")
    if walk:
        try:
            meshy_text.download(walk, os.path.splitext(a.out)[0] + "_walk.glb", tries=2)
        except SystemExit as e:
            print(f"  {e}", file=sys.stderr)
    print(f"{a.out}: the rigged model ({os.path.getsize(a.out)} bytes)")


if __name__ == "__main__":
    main()
