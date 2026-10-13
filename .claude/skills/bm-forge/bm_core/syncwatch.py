"""Drift watch: the repo files bm_core was ported from, hashed in SYNC.json.

  python3 -m bm_core.syncwatch check     exit 1 and list the files that changed
  python3 -m bm_core.syncwatch update    record the current hashes (after re-checking bm_core)

Run from .claude/skills/bm-forge. A change in one of these files may change
the format bm_core reads and writes: read the diff, port it, run the golden
tests, then `update`. Also records BM3D_VERSION (src/gpu/version3d.h:26) for
information: the MESH/ANIM format has no version field of its own.
"""
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SYNC = os.path.join(HERE, "SYNC.json")
WATCH = ["src/bm/bm.h", "src/bm/format.c", "scripts/bmmesh.py", "src/script/bm3d.lua", "src/gpu/version3d.h"]


def digest(rel):
    """sha256 of a file with line endings normalised (a Windows checkout with
    CRLF and a WSL one give the same hash), or None if missing"""
    p = os.path.join(REPO, rel)
    if not os.path.exists(p):
        return None
    with open(p, "rb") as f:
        return hashlib.sha256(f.read().replace(b"\r\n", b"\n")).hexdigest()


def bm3d_version():
    p = os.path.join(REPO, "src", "gpu", "version3d.h")
    try:
        with open(p, encoding="utf-8", errors="replace") as f:
            m = re.search(r'#define\s+BM3D_VERSION\s+"([^"]+)"', f.read())
        return m.group(1) if m else None
    except OSError:
        return None


def current():
    return {"files": {rel: digest(rel) for rel in WATCH}, "bm3d_version": bm3d_version()}


def check():
    """-> list of (file, recorded, now) that differ"""
    try:
        with open(SYNC, encoding="utf-8") as f:
            rec = json.load(f)
    except (OSError, ValueError):
        return [("SYNC.json", "missing or broken", "run: python3 -m bm_core.syncwatch update")]
    now = current()
    out = [(rel, rec.get("files", {}).get(rel), h) for rel, h in now["files"].items()
           if rec.get("files", {}).get(rel) != h]
    if rec.get("bm3d_version") != now["bm3d_version"]:
        out.append(("BM3D_VERSION", rec.get("bm3d_version"), now["bm3d_version"]))
    return out


def update():
    data = current()
    with open(SYNC, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")
    return data


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    cmd = argv[0] if argv else "check"
    if cmd == "update":
        data = update()
        print(f"SYNC.json updated: {len(data['files'])} files, bm3d {data['bm3d_version']}")
        return 0
    if cmd == "check":
        drift = check()
        if not drift:
            print("bm_core in sync with " + ", ".join(WATCH))
            return 0
        for rel, old, new in drift:
            print(f"DRIFT {rel}: recorded {old}, now {new}")
        print("read the diffs, port them to bm_core, run the golden tests, then: python3 -m bm_core.syncwatch update")
        return 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
