"""Shared helpers of the Tropic Trail episodes (video/3d/NN-name/record.py). Standard library only.

An episode builds a `bmvideo.Script` (keys, captions, scenes, narration), then:
  - write_input(): the bmhost input file (`FRAME type CODES`, `FRAME pad ...`, `FRAME shot NAME`): the canonical key script;
    Claude cloud's QEMU recorder converts the same file (video/lib/qemurec.py), so keep it plain;
  - write_overlay(): out/overlay.json, the data of the key overlay (claude/skills/bm-video-series/overlay.md);
  - run_headless(): bmhost --tool without video (works where ffmpeg is missing) with shots and wav;
  - chain_put(): the cartridge the video makes goes to video/3d/chain/ for the next video.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CHAIN = os.path.join(HERE, "chain")
BMHOST = os.path.join(ROOT, "build", "host", "bmhost-bin")
BMHOST_AI = os.path.join(ROOT, "build", "host", "bmhost-ai")
sys.path.insert(0, os.path.join(ROOT, "video", "lib"))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

FPS = 60


def fresh_sd(sd, files=()):
    """A clean SD (an editor remembers page and file on it): carts/ with `files` [(src, name)] copied in."""
    shutil.rmtree(sd, ignore_errors=True)
    os.makedirs(os.path.join(sd, "carts"))
    for src, name in files:
        shutil.copy(src, os.path.join(sd, "carts", name))
    return sd


def write_overlay(s, path, title=""):
    """The overlay data of a Script: keys pressed, scenes (tab/option/keys), narration, chapters. Frames at 60 fps."""
    doc = {"fps": FPS, "title": title,
           "keys": [{"frame": f, "cap": c, "what": w} for f, c, w in s.caps],
           "scenes": [{"frame": f, "place": p, "keys": [{"cap": c, "what": w} for c, w in k]} for f, p, k in s.scenes],
           "say": [{"from": a, "to": b, "text": t} for a, b, t in s.lines_say],
           "chapters": [{"frame": f, "title": t} for f, t in s.cards],
           "frames": s.t}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)


def run_headless(cart, input_path, seconds, sd, shots=None, wav=None, tool=True, ai=False, log=None):
    """bmhost without video. Returns the CompletedProcess (stdout/stderr captured unless `log`)."""
    exe = BMHOST_AI if ai else BMHOST
    cmd = [exe, cart, "--sd", sd, "--seconds", str(seconds), "--input", input_path, "--quiet"]
    if tool:
        cmd.append("--tool")
    if shots:
        os.makedirs(shots, exist_ok=True)
        cmd += ["--shots", shots]
    if wav:
        cmd += ["--wav", wav]
    print("+", " ".join(cmd), file=sys.stderr)
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=900)


def have_ffmpeg():
    return shutil.which("ffmpeg") is not None


def chain_put(src, name):
    """Copy the cartridge/project a video makes to video/3d/chain/NAME (not in git)."""
    os.makedirs(CHAIN, exist_ok=True)
    dst = os.path.join(CHAIN, name)
    shutil.copy(src, dst)
    print("chain:", os.path.relpath(dst, ROOT))
    return dst


def chain_get(name, fallback=None):
    """The file the previous video left; else `fallback` (a stand-in, e.g. from video/3d/assets) with a warning."""
    p = os.path.join(CHAIN, name)
    if os.path.exists(p):
        return p
    if fallback and os.path.exists(fallback):
        print("WARNING: chain/%s missing, using the stand-in %s" % (name, fallback), file=sys.stderr)
        return fallback
    raise SystemExit("missing chain/%s (run the previous video first: make video-3d-NN)" % name)
