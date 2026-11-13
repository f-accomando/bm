#!/usr/bin/env python3
"""
learn_run.py - the Learn tab of bm SNES SDK: every example of every lesson runs in the core (make test-snes-learn).

  python3 tests/snes/learn_run.py build/host/luahost build/host/snes_learn_run

1. luahost runs tests/snes/learn_test.lua: it checks the lesson files and the helpers, assembles every example
   and writes the ROMs and a manifest (rom, frames, pad, checks) to a work folder.
2. snes_learn_run (tests/snes/learn_run.c) runs each ROM for the frames of the manifest with the pad held, and checks
   the work RAM, the video RAM, the OAM, the colours and the picture that the lesson names in its expect= list.
Run it from the repository root.
"""
import os, subprocess, sys, tempfile


def main():
    luahost, runner = sys.argv[1:3]
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    with tempfile.TemporaryDirectory() as td:
        r = subprocess.run([luahost, os.path.join("tests", "snes", "learn_test.lua"), ".", td],
                           capture_output=True, text=True, cwd=root)
        sys.stdout.write(r.stdout + r.stderr)
        if r.returncode:
            return 1
        fails = 0
        lines = [l for l in open(os.path.join(td, "manifest.txt")).read().splitlines() if l.strip()]
        for l in lines:
            parts = l.split()
            rom, frames, pad, checks = parts[0], parts[1], parts[2], parts[3:]
            p = subprocess.run([runner, rom, frames, pad] + checks, capture_output=True, text=True)
            if p.returncode:
                fails += 1
                print("FAIL", os.path.basename(rom), p.stdout.strip())
        print("snes learn run: %d examples, %d failed" % (len(lines), fails))
        return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
