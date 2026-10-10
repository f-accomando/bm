#!/usr/bin/env python3
"""Replays a button script on a cartridge of Skyvale World with a log: the frame of
the flag, the score, the lives, every stomp and every life lost. Used to check that a
script made by bot.py plays the same as the bot did.

  python3 video/07-play/replay.py CART.bm INPUT.txt [--seconds 60] [--workdir DIR]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

LUA = '''
do
  local real_u, n = _update, 0
  function _update(...)
    n = n + 1
    local sc, lv, st = score, lives, state
    real_u(...)
    if score - sc == 100 then log(string.format("STOMP at frame %d", n)) end
    if score - sc == 10 then log(string.format("COIN at frame %d", n)) end
    if lives ~= lv then log(string.format("LIVES %d at frame %d", lives, n)) end
    if state ~= st then log(string.format("STATE %s at frame %d score %d lives %d", state, n, score, lives)) end
  end
end
'''


def replay(cart, inp, workdir, seconds=60):
    f = bmres.read(cart)
    f.put(bmres.SEC_LUA, (f.get(bmres.SEC_LUA).decode() + LUA).encode())
    os.makedirs(os.path.join(workdir, "carts"), exist_ok=True)
    dbg = os.path.join(workdir, "carts", "RPL.BM")
    bmres.write(dbg, f)
    r = subprocess.run([os.path.join(ROOT, "build", "host", "bmhost-bin"), dbg, "--sd", workdir, "--seconds", str(seconds),
                        "--input", inp], capture_output=True, text=True)
    t = r.stdout + r.stderr
    clear = re.search(r"STATE clear at frame (\d+) score (\d+) lives (\d+)", t)
    return dict(ok="ok," in t and "ERROR" not in t,
                clear=tuple(int(x) for x in clear.groups()) if clear else None,
                stomps=len(re.findall(r"STOMP at", t)), coins=len(re.findall(r"COIN at", t)),
                lives=re.findall(r"LIVES (\d+) at frame (\d+)", t), text=t)


if __name__ == "__main__":
    wd = sys.argv[sys.argv.index("--workdir") + 1] if "--workdir" in sys.argv else os.path.join(os.path.dirname(sys.argv[2]) or ".", "replay-sd")
    secs = int(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else 60
    i = replay(sys.argv[1], sys.argv[2], wd, secs)
    print(i["ok"], "clear (frame, score, lives):", i["clear"], "stomps", i["stomps"], "coins", i["coins"], "lives lost", i["lives"])
