#!/usr/bin/env python3
"""A bot that plays Tropic Trail, to make the button script of episode 8 (replay.txt).

It is a Lua chunk added at the end of a copy of the cartridge (video 7's 07-code.bm): it sees the `local`s of the game
(pico, apples, crates, goal, world, state, lives, score), replaces stick/btn/btnp with what the bot wants, decides every
_update and logs the buttons as bmhost input lines ("FRAME pad 1 up a"). Replayed with --input the game is the same every
time (the clock of bmhost is virtual, nothing in the game is random).

  python3 video/3d/08-gameplay/bot.py CART.bm OUT_INPUT.txt [--seconds 60] [key=value ...]

Log lines: APPLE n at frame f, CRATE n at frame f (spin|stomp), HIT lives=L at frame f, FALL at frame f, STATE clear at frame f score S lives L.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

DEFAULTS = dict(start=330, lead=0.6, hole_hold=30, crate_near=1.6, apple_ahead=6, apple_side=3, wall_lead=0.9, wall_hold=40, avoid=1, avoid_ahead=2.2)

LUA = '''
do
  local P = %(params)s
  local real_u, f = _update, 0
  local want = { left = false, right = false, up = false, down = false, a = false, b = false }
  local prev = { a = false, b = false }
  local logged = "none"
  local hold_a, napples, ncrates, last_hurt = 0, 0, 0, 0
  local function key(n) return want[n] or false end
  stick = function()
    local x, y = (want.right and 1 or 0) - (want.left and 1 or 0), (want.down and 1 or 0) - (want.up and 1 or 0)
    if x ~= 0 and y ~= 0 then x, y = x * 0.7071, y * 0.7071 end      -- the cross is normalised on a diagonal, like the real one
    return x, y
  end
  btn = function(n, p)
    if n == "jump" or n == "a" or n == 4 then return want.a end
    if n == "spin" or n == "b" or n == 5 then return want.b end
    if n == "left" or n == 0 then return want.left end
    if n == "right" or n == 1 then return want.right end
    if n == "up" or n == 2 then return want.up end
    if n == "down" or n == 3 then return want.down end
    return false
  end
  btnp = function(n, p)
    if n == "jump" or n == "a" or n == 4 then return want.a and not prev.a end
    if n == "spin" or n == "b" or n == 5 then return want.b and not prev.b end
    return false
  end
  local function decide()
    want.left, want.right, want.up, want.down, want.b = false, false, false, false, false
    if state == "title" then want.a = f >= P.start and f < P.start + 4; return end
    if state == "clear" then want.a = false; return end
    want.up = true
    if hold_a > 0 then hold_a = hold_a - 1; want.a = hold_a > 0 end
    -- the nearest apple ahead: steer to it
    local best, bz = nil, 1e9
    for _, o in ipairs(apples) do
      local dz = o.z - pico.z
      if dz > -0.2 and dz < P.apple_ahead and math.abs(o.x - pico.x) <= P.apple_side and dz < bz then best, bz = o, dz end
    end
    if best then
      local dx = best.x - pico.x
      if dx > 0.12 then want.right = true elseif dx < -0.12 then want.left = true end
    end
    -- steer round what is ahead on this lane (a hole or a wall that fills only a part of the width)
    if P.avoid == 1 and hold_a == 0 then
      for dz = 0.6, P.avoid_ahead, 0.4 do
        if map3d_height(world, pico.x, pico.z + dz) == nil or map3d_blocked(world, pico.x, pico.y + 0.5, pico.z + dz, 0.3) then
          if pico.x < 8.2 then want.right, want.left = true, false else want.left, want.right = true, false end
          break
        end
      end
    end
    if hold_a > 0 then return end
    want.a = false
    if not pico.ground then return end
    -- a hole ahead: jump with the lead
    if map3d_height(world, pico.x, pico.z + P.lead) == nil then hold_a = P.hole_hold; want.a = true; return end
    -- a wall ahead: jump over it
    if map3d_blocked(world, pico.x, pico.y + 0.5, pico.z + P.wall_lead, 0.3) then hold_a = P.wall_hold; want.a = true; return end
    -- a crate ahead: spin
    for _, c in ipairs(crates) do
      local dx, dz = c.x - pico.x, c.z - pico.z
      if dz > 0 and dz < P.crate_near and math.abs(dx) < 0.9 and pico.spin == 0 then want.b = true; return end
    end
  end
  function _update(...)
    f = f + 1
    decide()
    local keys = {}
    for _, n in ipairs({ "left", "right", "up", "down", "a", "b" }) do if want[n] then keys[#keys + 1] = n end end
    local k = #keys > 0 and table.concat(keys, " ") or "none"
    if k ~= logged then log(string.format("PAD %%d pad 1 %%s", f - 1, k)); logged = k end
    if P.tf and f >= P.tf and f <= P.tt then log(string.format("TR f=%%d x=%%.2f y=%%.2f z=%%.2f vy=%%.2f g=%%s a=%%s h=%%s", f, pico.x, pico.y, pico.z, pico.vy, tostring(pico.ground), tostring(want.a), tostring(map3d_height(world, pico.x, pico.z + 1)))) end
    local sc, lv, st, na, nc = score, lives, state, #apples, #crates
    local py = pico.y
    real_u(...)
    if score > sc then napples = napples + (score - sc); log(string.format("APPLE %%d at frame %%d", napples, f)) end
    if #crates < nc then ncrates = ncrates + 1; log(string.format("CRATE %%d at frame %%d (%%s)", ncrates, f, pico.spin > 0 and "spin" or "stomp")) end
    if lives < lv then log(string.format("HIT lives=%%d at frame %%d y=%%.1f", lives, f, py)); if py < -5 then log(string.format("FALL at frame %%d", f)) end end
    if state ~= st then log(string.format("STATE %%s at frame %%d score %%d lives %%d", state, f, score, lives)) end
    prev.a, prev.b = want.a, want.b
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
        with open(input_out, "w", newline="\n") as fh:
            fh.write("\n".join(pads) + "\n")
    return dict(
        apples=len(re.findall(r"APPLE \d+ at", text)), crates=re.findall(r"CRATE (\d+) at frame (\d+) \((\w+)\)", text),
        hits=re.findall(r"HIT lives=(\d+) at frame (\d+) y=(-?[\d.]+)", text), falls=re.findall(r"FALL at frame (\d+)", text),
        clear=re.search(r"STATE clear at frame (\d+) score (\d+) lives (\d+)", text),
        ok="ok," in text and "ERROR" not in text, text=text)


if __name__ == "__main__":
    cart, out = sys.argv[1], sys.argv[2]
    secs = 60
    params = dict(DEFAULTS)
    args = sys.argv[3:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--seconds":
            secs = int(args[i + 1]); i += 1
        elif "=" in a:
            k, v = a.split("=")
            params[k] = v
        i += 1
    info = run(cart, os.path.join(os.path.dirname(os.path.abspath(out)), "bot-sd"), params, secs, out)
    print("ok" if info["ok"] else info["text"][-400:])
    print("apples", info["apples"], "crates", info["crates"], "hits", info["hits"], "falls", info["falls"],
          "clear", info["clear"].groups() if info["clear"] else None)
