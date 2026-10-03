#!/usr/bin/env python3
"""
meshy_text: a model from a description through Meshy (meshy.ai,
text-to-3D: a preview for the shape, then a refine for the texture), as
a .glb. tools/meshy2mesh.py --glb then makes it a model of the console
(the conversion, the texture on the sheet, the reducer).

  tools/meshy_text.py SPEC.txt -o OUT.glb            a new preview and refine
  tools/meshy_text.py SPEC.txt -o OUT.glb --task ID  a refine already made
  tools/meshy_text.py SPEC.txt --check               the spec only, no call

SPEC.txt: key = value lines (# comments), `prompt` and `texture` (each
at most 600 characters, Meshy's limit) and optionally `polycount`
(default 2000), `pose` (a-pose, t-pose or none), `symmetry` (auto, on,
off), `style` (realistic, sculpture), `model` (meshy-5...), `height`
(metres, for meshy2mesh). The task ids go to OUT.task.txt: --task picks
up a refine already paid for. The key: MESHY_API_KEY in the environment.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

API = "https://api.meshy.ai/openapi/v2"
KEYS = ("prompt", "texture", "polycount", "pose", "symmetry", "style", "model", "height", "name")


def read_spec(path):
    spec = {"polycount": "2000", "pose": "a-pose", "symmetry": "auto", "style": "realistic", "model": "meshy-5",
            "height": "1.8"}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise SystemExit(f"{path}:{n}: key = value")
        k, v = (s.strip() for s in line.split("=", 1))
        if k not in KEYS:
            raise SystemExit(f"{path}:{n}: unknown key {k} ({', '.join(KEYS)})")
        spec[k] = v
    for k in ("prompt", "texture"):
        if not spec.get(k):
            raise SystemExit(f"{path}: no {k}")
        if len(spec[k]) > 600:
            raise SystemExit(f"{path}: {k} has {len(spec[k])} characters, Meshy takes 600")
    return spec


def api(method, path, key, body=None, tries=6, create=False):
    """one call to Meshy; connection errors, timeouts, 429 and 5xx are
    retried with a backoff. A task's creation (create=True) is retried
    only on 429, which made nothing: a lost reply must not make a second
    paid task."""
    data = json.dumps(body).encode() if body is not None else None
    for n in range(tries):
        req = urllib.request.Request(API + path, method=method, data=data,
                                     headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            text = e.read().decode(errors="replace")[:400]
            retry = e.code == 429 or (e.code >= 500 and not create)
            if not retry or n == tries - 1:
                raise MeshyError(e.code, f"meshy {method} {path}: HTTP {e.code}: {text}")
            wait = float(e.headers.get("Retry-After") or 0) or 10 * 2 ** n
        except (urllib.error.URLError, TimeoutError) as e:
            if create or n == tries - 1:
                raise MeshyError(0, f"meshy {method} {path}: {e}")
            wait = 5 * 2 ** n
        print(f"  meshy: retry in {wait:.0f} s", flush=True)
        time.sleep(wait)


class MeshyError(SystemExit):
    def __init__(self, code, text):
        super().__init__(text)
        self.code = code


def wait(task, key, what, minutes):
    deadline = time.time() + minutes * 60
    last = None
    while True:
        t = api("GET", f"/text-to-3d/{task}", key)
        status, progress = t.get("status"), t.get("progress", 0)
        if (status, progress) != last:
            print(f"  {what}: {status} {progress}%", flush=True)
            last = (status, progress)
        if status == "SUCCEEDED":
            return t
        if status in ("FAILED", "CANCELED", "EXPIRED"):
            raise SystemExit(f"meshy {what}: {status}: {(t.get('task_error') or {}).get('message', '')}")
        if time.time() > deadline:
            raise SystemExit(f"meshy {what}: still {status} after {minutes} minutes (task {task})")
        time.sleep(6)


def create(key, body, optional):
    """a new task; a 400 that names an optional key (an older or newer API)
    is tried once more without the optional keys"""
    try:
        return api("POST", "/text-to-3d", key, body, create=True)["result"]
    except MeshyError as e:
        if e.code != 400 or not any(k in str(e) for k in optional):
            raise
        print(f"  {e}; again without {', '.join(optional)}", flush=True)
        return api("POST", "/text-to-3d", key, {k: v for k, v in body.items() if k not in optional},
                   create=True)["result"]


def download(url, path, tries=6):
    for n in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=120) as r:
                data = r.read()
            with open(path, "wb") as f:
                f.write(data)
            return
        except (urllib.error.URLError, TimeoutError) as e:
            if n == tries - 1 or (isinstance(e, urllib.error.HTTPError) and e.code != 429 and e.code < 500):
                raise SystemExit(f"meshy: download {url}: {e}")
            time.sleep(5 * 2 ** n)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    ap.add_argument("-o", "--out", help="the .glb to write")
    ap.add_argument("--task", help="a refine task already made: download it, no new task")
    ap.add_argument("--wait", type=int, default=40, help="minutes to wait for each task (default 40)")
    ap.add_argument("--check", action="store_true", help="read the spec and stop")
    a = ap.parse_args()
    spec = read_spec(a.spec)
    if a.check:
        print(f"{a.spec}: prompt {len(spec['prompt'])}, texture {len(spec['texture'])} characters,"
              f" {spec['polycount']} triangles, pose {spec['pose']}, height {spec['height']} m")
        return
    if not a.out:
        ap.error("-o OUT.glb")
    key = os.environ.get("MESHY_API_KEY")
    if not key:
        raise SystemExit("MESHY_API_KEY: the key from meshy.ai, in the environment")
    log = os.path.splitext(a.out)[0] + ".task.txt"
    if a.task:
        refine = wait(a.task, key, "refine", a.wait)
    else:
        body = {"mode": "preview", "prompt": spec["prompt"], "art_style": spec["style"], "ai_model": spec["model"],
                "topology": "triangle", "target_polycount": int(spec["polycount"]), "should_remesh": True,
                "symmetry_mode": spec["symmetry"]}
        if spec["pose"] in ("a-pose", "t-pose"):
            body["pose_mode"] = spec["pose"]
        pid = create(key, body, ("pose_mode", "symmetry_mode", "ai_model"))
        print(f"meshy: preview {pid}", flush=True)
        with open(log, "w") as f:
            f.write(f"preview {pid}\n")
        wait(pid, key, "preview", a.wait)
        rid = create(key, {"mode": "refine", "preview_task_id": pid, "enable_pbr": False,
                           "texture_prompt": spec["texture"], "ai_model": spec["model"]}, ("ai_model",))
        print(f"meshy: refine {rid} (to pick it up again: --task {rid})", flush=True)
        with open(log, "a") as f:
            f.write(f"refine {rid}\n")
        refine = wait(rid, key, "refine", a.wait)
    url = (refine.get("model_urls") or {}).get("glb")
    if not url:
        raise SystemExit(f"meshy: no glb in {refine.get('model_urls')}")
    download(url, a.out)
    thumb = refine.get("thumbnail_url")
    if thumb:
        try:
            download(thumb, os.path.splitext(a.out)[0] + "_meshy.png", tries=2)
        except SystemExit as e:
            print(f"  {e}", file=sys.stderr)
    print(f"{a.out}: Meshy's model ({os.path.getsize(a.out)} bytes)")


if __name__ == "__main__":
    main()
