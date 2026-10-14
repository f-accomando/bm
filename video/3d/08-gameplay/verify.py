#!/usr/bin/env python3
"""Checks the game played by the bot (episode 8): python3 video/3d/08-gameplay/verify.py

Replays replay.txt on the game of video 7 (no bot) with a log, and checks, on the level of video 4 (18 units):
  - no Lua error; state title -> play -> clear;  - the goal is reached;  - at least 6 apples;  - at least 1 crate broken;
  - no fall and no hit (lives == 3);  - the shots exist, 480x270, and differ;  - out/play.wav is not silent (the music).
Exit 0 only when all pass."""
import os
import re
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402
import record  # noqa: E402

LOG = '''
do
  local u, f, napples, ncrates, st = _update, 0, 0, 0, state
  function _update(...)
    f = f + 1
    local sc, nc, lv = score, #crates, lives
    u(...)
    if score > sc then napples = napples + score - sc end
    if #crates < nc then ncrates = ncrates + 1 end
    if lives < lv then log("RHIT " .. f) end
    if pico.y < -5 then log("RFALL " .. f) end
    if state ~= st then st = state; log(string.format("RSTATE %s %d apples %d crates %d lives %d", state, f, napples, ncrates, lives)) end
  end
end
'''
ok_all = True


def check(cond, msg):
    global ok_all
    print("  %s  %s" % ("ok " if cond else "BAD", msg))
    if not cond:
        ok_all = False


def main():
    f = bmres.read(record.find_cart())
    f.put(bmres.SEC_LUA, f.get(bmres.SEC_LUA) + LOG.encode())
    wd = os.path.join(HERE, "out", "verify-sd")
    os.makedirs(os.path.join(wd, "carts"), exist_ok=True)
    path = os.path.join(wd, "carts", "V.BM")
    bmres.write(path, f)
    r = subprocess.run([os.path.join(ROOT, "build", "host", "bmhost-bin"), path, "--sd", wd, "--seconds", str(record.SECONDS), "--input",
                        os.path.join(HERE, "replay.txt")], capture_output=True, text=True)
    text = r.stdout + r.stderr
    check("ok," in text and "ERROR" not in text, "the game runs without a Lua error")
    m = re.search(r"RSTATE clear (\d+) apples (\d+) crates (\d+) lives (\d+)", text)
    check(m is not None, "state reaches clear (the goal)")
    if m:
        frame, apples, crates, lives = map(int, m.groups())
        check(apples >= 6, "%d apples (at least 6 of the 13 of the map)" % apples)
        check(crates >= 1, "%d crate(s) broken" % crates)
        check(lives == 3, "lives == %d" % lives)
        print("  clear at frame %d (%.1f s)" % (frame, frame / 60))
    check("RFALL" not in text and "RHIT" not in text, "no fall, no hit")
    shots = os.path.join(HERE, "out", "shots")
    names = sorted(n for n in os.listdir(shots) if n.endswith(".png")) if os.path.isdir(shots) else []
    check(len(names) >= 5, "%d shots from record.py" % len(names))
    try:
        from PIL import Image
        hs = [hash(Image.open(os.path.join(shots, n)).convert("RGB").tobytes()) for n in names]
        check(len(set(hs)) >= len(names) - 1, "the shots differ (the game moves)")
        check(bool(names) and Image.open(os.path.join(shots, names[0])).size == (480, 270), "shots are 480x270")
    except ImportError:
        check(False, "PIL for the shots")
    wav = os.path.join(HERE, "out", "play.wav")
    if os.path.exists(wav):
        w = wave.open(wav)
        data = w.readframes(w.getnframes())
        n = len(data) // 2
        s = struct.unpack("<%dh" % n, data[:n * 2])
        rms = (sum(x * x for x in s) / max(n, 1)) ** 0.5
        check(rms > 50, "play.wav is not silent (rms %.0f)" % rms)
    else:
        check(False, "play.wav (record.py)")
    print("RESULT:", "ALL OK" if ok_all else "FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
