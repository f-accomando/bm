-- Host tests of Titan Clash (carts/titan): the cartridge runs in a fake
-- bm (the same API, checks like the C code, no pixels) with scripted and
-- random input. It goes through the menus and the hangar, plays against
-- the computer, pauses, runs out the clock, lets the computer fight itself
-- with every pair of configurations (a rough check of the balance) and at
-- every level, plays a 2-player match and the demo, and reports the
-- heaviest frames: Lua instructions and sprite pixels, the two costs that
-- matter on the Pi.
--
--   luahost tests/titan/sim.lua build/titan/main.lua build/titan/main.map

local SRC, MAP = arg[1], arg[2]

---------------------------------------------------------------- line map

local spans = {}
for l in io.lines(MAP) do
  local a, b, name = l:match("^(%d+) (%d+) (.+)$")
  spans[#spans + 1] = { tonumber(a), tonumber(b), name }
end
local function where(msg)
  return (msg:gsub("main%.lua:(%d+)", function(n)
    n = tonumber(n)
    for _, s in ipairs(spans) do
      if n >= s[1] and n <= s[2] then return s[3] .. ":" .. (n - s[1] + 1) end
    end
    return "main.lua:" .. n
  end))
end

---------------------------------------------------------------- fake bm

local env = {}
local frame_calls, frame_px = 0, 0
local held, prev = { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
local now = 0
local logs = {}
local SHEET_W, SHEET_H = 2048, 4096

local function chk(cond, msg) if not cond then error(msg, 3) end end
local function num(v, name) chk(type(v) == "number", (name or "argument") .. ": number expected, got " .. type(v)) end

for _, n in ipairs({ "cls", "line", "rect", "circ", "camera", "clip", "spr", "map", "mset", "sset", "note",
                     "noteoff", "freq", "envelope", "duty", "quit" }) do
  env[n] = function(...) frame_calls = frame_calls + 1 end
end
env.pset = function(x, y, c) num(x, "pset x"); num(y, "pset y"); num(c, "pset colour") end
env.rectfill = function(x, y, w, h, c)
  frame_calls = frame_calls + 1
  num(x, "rectfill x"); num(y, "rectfill y"); num(w, "rectfill w"); num(h, "rectfill h"); num(c, "rectfill colour")
  local x0, y0, x1, y1 = math.max(0, x), math.max(0, y), math.min(640, x + w), math.min(360, y + h)
  if x1 > x0 and y1 > y0 then frame_px = frame_px + (x1 - x0) * (y1 - y0) end
end
env.circfill = function(x, y, r, c)
  frame_calls = frame_calls + 1
  num(x, "circfill x"); num(y, "circfill y"); num(r, "circfill r"); num(c, "circfill colour")
end
env.tri = function(x0, y0, x1, y1, x2, y2, c)
  frame_calls = frame_calls + 1
  num(x0); num(y0); num(x1); num(y1); num(x2); num(y2); num(c, "tri colour")
end
-- the sprites: the pixels on screen are what costs
env.sspr = function(sx, sy, sw, sh, dx, dy, fx, fy)
  frame_calls = frame_calls + 1
  num(sx, "sspr sx"); num(sy, "sspr sy"); num(sw, "sspr sw"); num(sh, "sspr sh"); num(dx, "sspr dx"); num(dy, "sspr dy")
  chk(sx >= 0 and sy >= 0 and sx + sw <= SHEET_W and sy + sh <= SHEET_H, "sspr outside the sheet")
  local x0, y0, x1, y1 = math.max(0, dx), math.max(0, dy), math.min(640, dx + sw), math.min(360, dy + sh)
  if x1 > x0 and y1 > y0 then frame_px = frame_px + (x1 - x0) * (y1 - y0) end
end
env.SCREEN_W, env.SCREEN_H = 640, 360
env.SQUARE, env.TRIANGLE, env.SAW, env.NOISE = 0, 1, 2, 3
env.print = function(s, x, y, c, scale)
  frame_calls = frame_calls + 1
  num(x, "print x"); num(y, "print y")
  local t = tostring(s)
  return x + #t * 8 * (scale or 1)
end
env.pget = function() return 0 end
env.mget = function() return 0 end
env.sget = function() return nil end
env.rgb = function(r, g, b) return (r & 255) << 16 | (g & 255) << 8 | (b & 255) end
env.playing = function() return false end
env.apu = function() return 0 end
local vol = 10
env.volume = function(v) if v then vol = math.max(0, math.min(10, v)) end return vol end
env.log = function(...) local t = { ... } for i = 1, #t do t[i] = tostring(t[i]) end logs[#logs + 1] = table.concat(t, "\t") end
env.time = function() return now end
env.stat = function(n) if n == 3 then return math.floor(now * 60) end return 0 end
env.save = function() return true end
env.saved = function() return nil end
env.keyp = function() return nil end
local function bits(p) return p == nil and (held[1] | held[2] | held[3] | held[4]) or (held[p] or 0) end
local function pbits(p) return p == nil and (prev[1] | prev[2] | prev[3] | prev[4]) or (prev[p] or 0) end
env.btn = function(b, p)
  chk(p == nil or math.type(p) == "integer", "btn: the pad is a number")
  return (bits(p) >> b) & 1 == 1
end
env.btnp = function(b, p)
  chk(p == nil or math.type(p) == "integer", "btnp: the pad is a number")
  return (bits(p) >> b) & 1 == 1 and (pbits(p) >> b) & 1 == 0
end
env.players = function() return 4, 15 end
for _, lib in ipairs({ "string", "table", "math", "utf8", "coroutine" }) do env[lib] = _G[lib] end
for _, f in ipairs({ "assert", "error", "ipairs", "next", "pairs", "pcall", "rawequal", "rawget", "rawlen",
                     "rawset", "select", "setmetatable", "getmetatable", "tonumber", "tostring", "type", "xpcall" }) do
  env[f] = _G[f]
end

---------------------------------------------------------------- load

local f = assert(io.open(SRC))
local code = f:read("a")
f:close()
local chunk, err = load(code, "=main.lua", "t", env)
if not chunk then io.stderr:write(where(err) .. "\n") os.exit(1) end

local instr = 0
local function counter() instr = instr + 1000 end

local function call(fn)
  local ok, e = xpcall(fn, debug.traceback)
  if not ok then
    io.stderr:write(where(e) .. "\n")
    for i = math.max(1, #logs - 5), #logs do io.stderr:write("log: " .. logs[i] .. "\n") end
    os.exit(1)
  end
end

call(chunk)

-- a sprite that is not in the sheet is a mistake
setmetatable(env.SPR, { __index = function(_, k) error("no sprite called " .. tostring(k), 2) end })
for name, list in pairs(env.ANIM) do
  for _, n in ipairs(list) do assert(env.FR[n], "animation " .. name .. ": no frame " .. n) end
end

local T = env.TITAN
local worst = { instr = 0, px = 0, calls = 0 }
local label = "boot"

local function frame()
  now = now + 1 / 60
  instr, frame_calls, frame_px = 0, 0, 0
  debug.sethook(counter, "", 1000)
  call(env._update)
  call(env._draw)
  debug.sethook()
  prev[1], prev[2], prev[3], prev[4] = held[1], held[2], held[3], held[4]
  if instr > worst.instr then worst.instr, worst.iwhere = instr, label end
  if frame_px > worst.px then worst.px, worst.pwhere = frame_px, label end
  worst.calls = math.max(worst.calls, frame_calls)
end

local function press(p, b, frames)
  held[p] = held[p] | (1 << b)
  for _ = 1, frames or 2 do frame() end
  held[p] = held[p] & ~(1 << b)
  frame()
end

local function run_frames(n) for _ = 1, n do frame() end end

local function expect(name)
  assert(T.Scr.name == name, "expected the screen " .. name .. ", got " .. tostring(T.Scr.name))
end

-- random play: a direction for a while, buttons now and then
local rng = { dir = {}, t = {} }
local DIRS = { 0, 1, 2, 4, 8, 5, 9, 6, 10 }     -- none, left, right, up, down, diagonals
local function random_input(p)
  if (rng.t[p] or 0) <= 0 then
    rng.dir[p] = DIRS[math.random(#DIRS)]
    rng.t[p] = math.random(4, 40)
  end
  rng.t[p] = rng.t[p] - 1
  local b = rng.dir[p]
  local r = math.random()
  if r < 0.04 then b = b | (1 << 4) elseif r < 0.08 then b = b | (1 << 5)
  elseif r < 0.12 then b = b | (1 << 6) elseif r < 0.16 then b = b | (1 << 7)
  elseif r < 0.17 then b = b | (1 << 5) | (1 << 7) end
  held[p] = b
end

-- the fight until its result (or a limit)
local function fight_to_end(players, limit)
  local F = T.Scr.screens.fight
  local seen = { fight_frames = 0 }
  for _ = 1, limit or 60 * 60 * 10 do
    for _, p in ipairs(players) do random_input(p) end
    frame()
    seen[F.m.phase] = true
    if F.m.phase == "fight" then seen.fight_frames = seen.fight_frames + 1 end
    if F.m.phase == "result" then break end
  end
  for _, p in ipairs(players) do held[p] = 0 end
  return F.m, seen
end

---------------------------------------------------------------- scenario

math.randomseed(7)
call(env._init)
expect("title")
label = "title"
run_frames(120)
press(1, 8)                    -- title -> mode
expect("mode")
label = "mode"
press(1, 3); press(1, 3); press(1, 3); press(1, 3)    -- MOVES
press(1, 4)
assert(T.Scr.screens.mode.help, "the moves list")
run_frames(5)
press(1, 5)
press(1, 2)                                           -- CPU LEVEL
press(1, 1)                                           -- NORMAL -> HARD
assert(T.Scr.setup.level == "hard", "CPU level " .. T.Scr.setup.level)
press(1, 0)                                           -- back to NORMAL
press(1, 2); press(1, 2); press(1, 2)                 -- 1 PLAYER VS CPU
press(1, 4)
expect("hangar")
label = "hangar"
run_frames(30)
local cfg = T.Scr.setup.cfg[1]
local armor0 = cfg.armor
press(1, 1)                    -- the other armour
assert(cfg.armor ~= armor0, "armour changed")
press(1, 3)
local weapon0 = cfg.weapon
press(1, 0)                    -- the other weapon
assert(cfg.weapon ~= weapon0, "weapon changed")
run_frames(40)
press(1, 3)
press(1, 4)                    -- READY
run_frames(120)
expect("fight")
label = "1p fight"
local F = T.Scr.screens.fight
run_frames(120)
assert(F.m.phase == "fight", "the fight starts after ROUND 1 / FIGHT!, not " .. F.m.phase)
-- pause, the moves, resume
press(1, 8)
assert(F.paused, "paused")
press(1, 3); press(1, 4)
assert(F.help, "the moves in the pause")
press(1, 5)
press(1, 8)
assert(not F.paused, "resumed")
local m, seen = fight_to_end({ 1 })
assert(m.phase == "result", "the match ends")
io.write(string.format("1P %s/%s vs CPU %s/%s: %d-%d in %d rounds\n", T.Scr.setup.cfg[1].armor,
                       T.Scr.setup.cfg[1].weapon, T.Scr.setup.cfg[2].armor, T.Scr.setup.cfg[2].weapon,
                       m.wins[1], m.wins[2], m.round))
run_frames(100)
press(1, 3); press(1, 4)       -- HANGAR
expect("hangar")

-- the computer against itself: every configuration, every level
local combos = {}
for _, a1 in ipairs({ "light", "heavy" }) do
  for _, w1 in ipairs({ "sword", "guns" }) do combos[#combos + 1] = { armor = a1, weapon = w1 } end
end
local score = {}
local rounds, kos, timeouts, frames, matches = 0, 0, 0, 0, 0
-- every pair at the normal level, the mirrors at the others
for _, level in ipairs({ "normal", "easy", "hard" }) do
  for i = 1, #combos do
    for j = 1, #combos do
      if level ~= "normal" and i ~= j then goto next end
      matches = matches + 1
      T.Scr.setup.mode = "cpu"
      T.Scr.setup.level = level
      T.Scr.setup.cfg[1] = { armor = combos[i].armor, weapon = combos[i].weapon }
      T.Scr.setup.cfg[2] = { armor = combos[j].armor, weapon = combos[j].weapon }
      T.Scr.go("fight")
      label = "cpu " .. level .. " " .. i .. "v" .. j
      local mm, sn = fight_to_end({})
      assert(mm.phase == "result", label .. ": the match ends")
      rounds = rounds + mm.round
      if sn.ko then kos = kos + 1 end
      if sn.timeover then timeouts = timeouts + 1 end
      local ki = combos[i].armor .. "/" .. combos[i].weapon
      local kj = combos[j].armor .. "/" .. combos[j].weapon
      score[ki] = score[ki] or { w = 0, l = 0 }
      score[kj] = score[kj] or { w = 0, l = 0 }
      score[ki].w, score[ki].l = score[ki].w + mm.wins[1], score[ki].l + mm.wins[2]
      score[kj].w, score[kj].l = score[kj].w + mm.wins[2], score[kj].l + mm.wins[1]
      frames = frames + sn.fight_frames
      ::next::
    end
  end
end
io.write(string.format("CPU vs CPU: %d matches, %d rounds of %.0f s on average; K.O. in %d, time out in %d\n",
                       matches, rounds, frames / rounds / 60, kos, timeouts))
for _, c in ipairs(combos) do
  local k = c.armor .. "/" .. c.weapon
  io.write(string.format("  %-12s rounds won %3d lost %3d\n", k, score[k].w, score[k].l))
end

-- the clock runs out: the one with more life wins the round
T.Scr.setup.mode = "cpu"
T.Scr.go("fight")
label = "time over"
local F2 = T.Scr.screens.fight
run_frames(120)
F2.f[1].life = 900
F2.f[2].life = 500
F2.m.clock = 1
local before = F2.m.wins[1]
local saw = false
for _ = 1, 400 do
  frame()
  if F2.m.phase == "timeover" then saw = true end
  if F2.m.round == 2 then break end
end
assert(saw, "TIME OVER")
assert(F2.m.wins[1] == before + 1, "the round goes to the one with more life")

-- two players
T.Scr.setup.mode = "2p"
T.Scr.go("hangar")
label = "2p hangar"
press(1, 3); press(1, 3); press(1, 4)
press(2, 1); press(2, 3); press(2, 3); press(2, 4)
run_frames(80)
expect("fight")
label = "2p fight"
-- every direction, on each pad, as a numpad seen from its robot (6 =
-- forwards): P1 faces right, P2 left
local FF = T.Scr.screens.fight
for _ = 1, 400 do if FF.m.phase == "fight" then break end frame() end
assert(FF.m.phase == "fight", "2p: the fight starts")
local L_, R_, U_, D_ = 1, 2, 4, 8
local WANT = {
  [1] = { [R_] = 6, [L_] = 4, [D_] = 2, [U_] = 8, [D_ | R_] = 3, [D_ | L_] = 1, [U_ | R_] = 9, [U_ | L_] = 7 },
  [2] = { [R_] = 4, [L_] = 6, [D_] = 2, [U_] = 8, [D_ | R_] = 1, [D_ | L_] = 3, [U_ | R_] = 7, [U_ | L_] = 9 },
}
for p = 1, 2 do
  for bits_, want in pairs(WANT[p]) do
    held[p] = bits_
    frame()
    held[p] = 0
    assert(FF.f[p].inp.dir == want, string.format("pad %d, buttons %d: direction %d, expected %d",
                                                  p, bits_, FF.f[p].inp.dir, want))
    run_frames(90)                          -- down from a jump
  end
  -- forwards walks towards the other robot
  local f, o = FF.f[p], FF.f[3 - p]
  local gap = math.abs(f.x - o.x)
  held[p] = p == 1 and R_ or L_
  run_frames(20)
  held[p] = 0
  assert(math.abs(f.x - o.x) < gap - 20, "pad " .. p .. ": forwards walks")
  run_frames(30)
end
local m2 = fight_to_end({ 1, 2 })
assert(m2.phase == "result", "the 2-player match ends")
io.write(string.format("2P random: %d-%d\n", m2.wins[1], m2.wins[2]))
run_frames(100)
press(1, 3); press(1, 3); press(1, 4)    -- TITLE
expect("title")

-- nobody plays: the demo, then a button brings the title back
label = "demo"
run_frames(60 * 26)
expect("fight")
assert(T.Scr.screens.fight.demo, "the demo")
run_frames(600)
press(1, 8)
expect("title")
assert(T.Scr.setup.mode == "2p", "the demo gives the mode back")

io.write(string.format("worst frame: %d Lua instructions (%s), %d pixels of sprites and fills (%s), %d calls\n",
                       worst.instr, worst.iwhere, worst.px, worst.pwhere, worst.calls))
-- the frame times the cartridge logs are zero here; anything else is news
for _, l in ipairs(logs) do
  if not l:find("^titan fight:") and not l:find("^titan build ") then io.write("log: " .. l .. "\n") end
end
io.write("titan: ok\n")
