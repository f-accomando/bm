#!/usr/bin/env python3
"""A bot that plays Skyvale World, to make the button script of episode 7 (and the
playing shown at the end of episode 6). It is a Lua chunk added to a copy of the
cartridge: it replaces btn and btnp, decides every frame from the state of the game
(a pit ahead, a slime ahead, a coin ahead) and logs the buttons in the format of
bmhost's input script ("frame pad 1 right a"). Replayed with --input, the game is the same
every time (the clock of bmhost is virtual, nothing in the game is random).

  python3 video/07-play/bot.py CART.bm OUT_INPUT.txt [--seconds 60] [key=value ...]

key=value: jump parameters (pit_hold, slime_hold, slime_min, slime_max, coin_hold, ...).
Prints what happened: score, lives, stomps, the frame of the flag.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

DEFAULTS = dict(start=60, pit_ahead=20, pit_hold=40, slime_min=18, slime_max=40, slime_hold=18,
                coin_min=10, coin_max=24, coin_hold=5)

LUA = '''
do
  local P = %(params)s
  local real_u, f = _update, 0
  local want = { left = false, right = false, a = false }
  local prev = { left = false, right = false, a = false }
  local logged = "none"
  local hold_a, stomps, last_score, last_lives = 0, 0, 0, 3
  local names = { [0] = "left", "right", "up", "down", "a", "b" }
  local function name(i) return type(i) == "string" and i or names[i] end
  btn = function(i, p) return want[name(i)] or false end
  btnp = function(i, p)
    local n = name(i)
    if n == "a" then return want.a and not prev.a end
    return false
  end
  local function ground_ahead(dx)
    return mflags(kip.x + dx, 226, 4, 2) & 3 ~= 0                  -- the ground level: solid or platform
  end
  local function decide()
    if state == "title" then
      want.right = false
      want.a = f >= P.start and f < P.start + 4
      return
    end
    if state == "clear" then want.right, want.a = false, false return end
    want.right = true
    if hold_a > 0 then hold_a = hold_a - 1; want.a = hold_a > 0; return end
    want.a = false
    if not kip.ground then return end
    local function jump(h) hold_a = h; want.a = true end
    if kip.y > 200 and not ground_ahead(P.pit_ahead) then jump(P.pit_hold) return end
    for _, s in ipairs(slimes) do
      local dx = s.x - kip.x
      if dx > P.slime_min and dx < P.slime_max and ground_ahead(44) and ground_ahead(80) then jump(P.slime_hold) return end
    end
    for _, c in ipairs(coins) do
      local dx = c.x - kip.x
      if dx > P.coin_min and dx < P.coin_max and ground_ahead(36) and ground_ahead(64) then jump(P.coin_hold) return end
    end
  end
  function _update(...)
    f = f + 1
    decide()
    local keys = {}
    for _, n in ipairs({ "left", "right", "a" }) do if want[n] then keys[#keys + 1] = n end end
    local k = #keys > 0 and table.concat(keys, " ") or "none"
    if k ~= logged then log(string.format("PAD %%d pad 1 %%s", f - 1, k)); logged = k end
    if P.tf and f >= P.tf and f <= P.tt then
      log(string.format("TR f=%%d x=%%.0f y=%%.0f vy=%%.1f g=%%s a=%%s hold=%%d ahead=%%s", f, kip.x, kip.y, kip.vy, tostring(kip.ground), tostring(want.a), hold_a, tostring(ground_ahead(P.pit_ahead))))
    end
    local sc, lv, st = score, lives, state
    local ox, oy = kip.x, kip.y
    real_u(...)
    if score - sc == 100 then stomps = stomps + 1; log(string.format("STOMP %%d at frame %%d", stomps, f)) end
    if lives ~= lv then log(string.format("LIVES %%d at frame %%d x=%%.0f y=%%.0f", lives, f, ox, oy)) end
    if state ~= st then log(string.format("STATE %%s at frame %%d score %%d lives %%d", state, f, score, lives)) end
    prev.left, prev.right, prev.a = want.left, want.right, want.a
  end
end
'''


def make(cart, out_cart, params):
    f = bmres.read(cart)
    lua = f.get(bmres.SEC_LUA).decode()
    p = "{ " + ", ".join("%s = %s" % (k, v) for k, v in params.items()) + " }"
    f.put(bmres.SEC_LUA, (lua + LUA % {"params": p}).encode())
    os.makedirs(os.path.dirname(out_cart), exist_ok=True)
    bmres.write(out_cart, f)


def run(cart, workdir, params, seconds=60, input_out=None):
    dbg = os.path.join(workdir, "carts", "BOT.BM")
    make(cart, dbg, params)
    r = subprocess.run([os.path.join(ROOT, "build", "host", "bmhost-bin"), dbg, "--sd", workdir, "--seconds", str(seconds)],
                       capture_output=True, text=True)
    text = r.stdout + r.stderr
    pads = [m.group(1) for m in re.finditer(r"PAD (\d+ pad 1 [a-z ]+)", text)]
    if input_out:
        with open(input_out, "w") as fh:
            fh.write("\n".join(pads) + "\n")
    info = dict(
        stomps=len(re.findall(r"STOMP", text)),
        lives_lost=re.findall(r"LIVES (\d+) at frame (\d+) x=(-?\d+) y=(-?\d+)", text),
        clear=re.search(r"STATE clear at frame (\d+) score (\d+) lives (\d+)", text),
        ok="ok," in text and "ERROR" not in text,
        text=text)
    return info


if __name__ == "__main__":
    cart, out = sys.argv[1], sys.argv[2]
    secs = 60
    params = dict(DEFAULTS)
    for a in sys.argv[3:]:
        if a == "--seconds":
            continue
        if "=" in a:
            k, v = a.split("=")
            params[k] = v
        else:
            secs = int(a)
    wd = os.path.join(os.path.dirname(out) or ".", "bot-sd")
    info = run(cart, wd, params, secs, out)
    print("ok" if info["ok"] else info["text"][-300:])
    print("stomps", info["stomps"], "lives lost at", info["lives_lost"],
          "clear", info["clear"].groups() if info["clear"] else None)
