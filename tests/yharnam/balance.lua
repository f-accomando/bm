-- The fight of Yharnam, measured: the real code of the game (animations,
-- ticks, stamina, damage, poise) in a fake bm, the hunter striking a
-- creature that stands still and never strikes back, for a hunter with no
-- paths and for paths taken at the lamps. A is pressed as fast as it counts,
-- the saw cleaver folded (f) or open (o); against a boss the poise breaks
-- and A is then the visceral attack (one blow). Every line:
--
--   townsman     blows landed to kill a townsman of the first area (5 hp),
--                and the seconds it takes
--   Butcher      the same for the first boss (60 hp)
--   Watcher      the same for the fourth (133 hp)
--   die          blows the hunter takes before he dies: of 2 (a townsman of
--                the first areas), of 3 (a boss, a townsman further on); no rally
--   4 s          blows landed in 4 seconds (stamina and all), the damage of a
--                blow (mean) and the damage per second (DPS) in them
--   10 s         the DPS over 10 seconds, the breath run out
--   stamina      folded blows from full stamina (none coming back)
--   combo        a combo of three blows, seconds (folded/open)
--   pistol       shots to kill the townsman; the parry window against his
--                thrust (s, from the start of it); how long a creature reels (s)
--
-- And it checks the rules of the paths (PATHS in main.lua): a path taken
-- again still counts, less every time; the first path taken (the hunter's
-- own) weighs more than any other; the other paths weigh less, more so the
-- more of them, so a mix is worth less than a path followed; and no path
-- turns the fight upside down.
--
--   luahost tests/yharnam/balance.lua carts/yharnam/main.lua

local SRC = arg[1]
math.randomseed(1)
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local Y = B.load(SRC)
local P, F, G = Y.player, Y.FOE, Y.G
local checks = 0
local function check(cond, msg)
  checks = checks + 1
  if not cond then io.stderr:write("FAIL: " .. msg .. "\n") os.exit(1) end
end

B.call(B.env._init)
B.frame(1 << 4); B.frame(0)                     -- the hunt begins
check(Y.state() == "play", "the hunt begins")
F.quiet = true
G.echoes = 1000000

---------------------------------------------------------------- the rules

local NP = #Y.PATHS
local function total(slots)
  local s = 0
  for _, w in pairs(Y.path_weights(slots)) do s = s + w end
  return s
end
for p = 1, NP do
  local prev, step = 0, math.huge
  local first
  for n = 1, 4 do
    local b = {}
    for i = 1, n do b[i] = p end
    local w = Y.path_weights(b)[p]
    first = first or w
    check(w > prev and w - prev <= step, Y.PATHS[p].name .. ": taken again it counts, less every time")
    check(w - prev >= 0.25 * first, Y.PATHS[p].name .. ": the fourth still counts")
    prev, step = w, w - prev
  end
  for q = 1, NP do
    if q ~= p then
      local w = Y.path_weights({ p, q })
      check(w[q] < 0.5 * w[p], "his own path weighs more than another")
      check(w[q] < Y.PATHS[p].curve[2], "the same path again weighs more than another")
      check(total({ p, p, q, q }) < total({ p, p, p, p }), "two and two weigh less than one path")
      check(total({ p, p, p, q }) >= 0.9 * total({ p, p, p, p }), "a turn of another path: a fair variant")
      for r = 1, NP do
        for s = 1, NP do
          if r ~= p and r ~= q and s ~= p and s ~= q and s ~= r then
            check(total({ p, q, r, s }) < 0.8 * total({ p, p, p, p }), "four paths weigh less")
          end
        end
      end
    end
  end
end

---------------------------------------------------------------- the fight

-- a street of the first area with room ahead, no fire, no lamp, no shrine
local function spot()
  for cy = 0, 3 do
    for cx = 2, 3 do
      local ch = Y.ensure(cx, cy)
      for ty = 1, 14 do
        for tx = 1, 13 do
          local x, y = cx * 256 + tx * 16 + 8, cy * 256 + ty * 16 + 8
          local ok = ch.kind[ty * 16 + tx + 1] == Y.kinds.road and not Y.blocked(x, y) and
                     not Y.blocked(x + 20, y) and not Y.blocked(x + 40, y)
          for dy = -1, 1 do
            for dx = -1, 1 do
              local n = Y.ensure(cx + dx, cy + dy)
              for _, f in ipairs(n.fires) do if math.abs(f.x - x) + math.abs(f.y - y) < 200 then ok = false end end
              for _, l in ipairs(n.lamps) do if math.abs(l.x - x) + math.abs(l.y - y) < 60 then ok = false end end
              if n.shrine and math.abs(n.shrine.x - x) + math.abs(n.shrine.y - y) < 80 then ok = false end
            end
          end
          if ok then return x, y end
        end
      end
    end
  end
  error("no quiet street")
end
local X0, Y0 = spot()
Y.teleport(X0, Y0)
for i = 1, 60 do B.frame(0) end
B.nodraw = true                                 -- what is drawn does not count here

local function clear()
  for k = #F.list, 1, -1 do F.list[k] = nil end
  F.boss = nil
end

-- the hunter as these paths (indices of PATHS, in the order taken) made him,
-- whole, the saw cleaver folded or open
local function hunter(slots, ext)
  clear()
  G.slots = {}
  for i, v in ipairs(slots) do G.slots[i] = v end
  Y.apply_paths()
  P.x, P.y, P.dir = X0, Y0, 2
  P.act, P.hp, P.st, P.st_wait, P.ext, P.lock, P.combo, P.queued = nil, P.hpmax, P.stmax, 0, ext, nil, 0, false
  P.rally, P.hits, P.inv, P.vic, P.charge, P.charged = 0, {}, 0, nil, 0, false
  P.anim, P.f, P.ft, P.rev = ext and "idle_x" or "idle", 1, 0, false
  B.held = 0
end

-- a creature 20 px ahead that stands and takes it
local function dummy(name, area, hp)
  local o = F.new(name, X0 + 20, Y0, "dummy", area)
  if hp then o.hp = hp end
  o.st = setmetatable({ sight = 0, sp = 0 }, { __index = o.st })
  return o
end

-- a button (A: 4, X: 6) pressed every other tick for at most `ticks`, or
-- until the creature is dead, or until stop(); blows landed, damage, ticks
local function mash(o, ticks, button, stop)
  local hits, dmg, t, hp = 0, 0, 0, o.hp
  while t < ticks and o.act ~= "dead" and not (stop and stop(hits)) do
    o.x, o.y, o.cd = X0 + 20, Y0, 1e9
    P.x, P.y, P.inv = X0, Y0, 999
    B.frame(t % 2 == 0 and (1 << (button or 4)) or 0)
    t = t + 1
    if o.hp < hp then hits, dmg, hp = hits + 1, dmg + (hp - o.hp), o.hp end
  end
  B.frame(0)
  P.inv = 0
  return hits, dmg, t
end

-- its blows (dmg each) the hunter takes before he falls
local function to_die(dmg)
  P.hp = P.hpmax
  local n = 0
  repeat
    P.inv, P.act, P.hits = 0, nil, {}
    Y.hurt(dmg)
    n = n + 1
  until P.act == "dead" or n > 99
  P.act, P.hp = nil, P.hpmax
  return n
end

local thrust = F.byname.pitchfork.a.attack
local thrust_due = 0
for k = 1, thrust.hit[1] - 1 do thrust_due = thrust_due + thrust.t[k] end

local function measure(slots)
  local r = {}
  for _, ext in ipairs({ false, true }) do
    local k = ext and "o" or "f"
    local h, t
    hunter(slots, ext)
    h, _, t = mash(dummy("pitchfork", 0), 600)
    r["town_" .. k], r["towns_" .. k] = h, t / 60
    hunter(slots, ext)
    h, _, t = mash(dummy("butcher", 0), 60 * 120)
    r["b1_" .. k], r["b1s_" .. k] = h, t / 60
    hunter(slots, ext)
    h, _, t = mash(dummy("watcher", 3), 60 * 200)
    r["b4_" .. k], r["b4s_" .. k] = h, t / 60
    hunter(slots, ext)
    local d
    h, d = mash(dummy("pitchfork", 0, 1e6), 240)
    r["n4_" .. k], r["dps_" .. k], r["blow_" .. k] = h, d / 4, d / h
    hunter(slots, ext)
    _, d = mash(dummy("pitchfork", 0, 1e6), 600)
    r["dps10_" .. k] = d / 10
  end
  hunter(slots, false)
  r.die2, r.die3 = to_die(2), to_die(3)
  -- folded blows from full stamina to none (none of it coming back)
  hunter(slots, false)
  local n, was = 0, P.st
  mash(dummy("pitchfork", 0, 1e6), 600, 4, function()
    if P.st < was then n = n + 1 end
    was, P.st_wait = P.st, 99
    return P.st <= 0
  end)
  r.full = n
  -- a combo of three, with stamina to spare
  for _, ext in ipairs({ false, true }) do
    hunter(slots, ext)
    local _, _, t = mash(dummy("pitchfork", 0, 1e6), 600, 4, function(hits)
      P.st = P.stmax
      return hits >= 3
    end)
    r[ext and "combo_o" or "combo"] = t / 60
  end
  -- the pistol
  hunter(slots, false)
  r.shots = mash(dummy("pitchfork", 0), 1200, 6)
  r.parry = (thrust_due + P.mods.parry) / 60
  r.reel = 110 * P.mods.stag / 60
  r.mods = P.mods
  clear()
  return r
end

-- the builds: none, each path 1 to 4 times, a path followed with a turn of
-- another, two and two, another first, four different ones
local NAMES, ABBR = {}, { "Fer", "Moo", "Qui", "Ser", "Hun" }
for i, p in ipairs(Y.PATHS) do NAMES[i] = ABBR[i] .. " " .. p.name end
local function label(slots)
  if #slots == 0 then return "no path" end
  local s = {}
  for i, v in ipairs(slots) do s[i] = ABBR[v] end
  return table.concat(s, " ")
end
local builds = { {} }
for p = 1, NP do
  for n = 1, 4 do
    local b = {}
    for i = 1, n do b[i] = p end
    builds[#builds + 1] = b
  end
end
local MIXES = { { 4, 4, 4, 5 }, { 5, 5, 5, 4 }, { 1, 1, 1, 2 }, { 2, 2, 2, 5 }, { 3, 3, 3, 4 }, { 4, 4, 5, 5 },
                { 1, 1, 3, 3 }, { 5, 4, 4, 4 }, { 4, 5, 2, 1 }, { 1, 2, 3, 4 } }
for _, b in ipairs(MIXES) do builds[#builds + 1] = b end

local BL = Y.BLADE
for _, k in ipairs({ "fold", "open" }) do
  local b = BL[k]
  io.write(string.format("%s: light %s, heavy %g (charged %g), trick %g, visceral %g; stamina %g a blow (heavy %g), " ..
                         "breath back after %g ticks; pace %g\n", k == "fold" and "folded" or "open",
                         table.concat(b.light, "/"), b.heavy, b.heavy + b.charged, b.trick, b.visceral, b.cost,
                         b.heavy_cost, b.breath, b.rate))
end
io.write("the paths: " .. table.concat(NAMES, ", ") .. "\n")
io.write("                 townsman (5 hp)      the Butcher (60 hp)     the Watcher (133 hp)  die   in 4 s" ..
         "                          10 s       stamina   combo      pistol\n")
io.write("build            blows f/o  s f/o     blows f/o  s f/o        blows f/o  s f/o     of 2/3  " ..
         "blows f/o  a blow f/o  DPS f/o   DPS f/o    full      s f/o      shots parry reel\n")
local R = {}
for _, b in ipairs(builds) do
  local r = measure(b)
  R[label(b)] = r
  io.write(string.format("%-16s %3d/%-3d %4.2f/%-5.2f %3d/%-3d %4.1f/%-6.1f %3d/%-3d %4.1f/%-5.1f %2d/%-3d " ..
                         "%3d/%-3d %4.2f/%-4.2f %4.1f/%-4.1f %4.1f/%-4.1f %4d   %4.2f/%-4.2f %5d %5.2f %5.2f\n",
                         label(b), r.town_f, r.town_o, r.towns_f, r.towns_o, r.b1_f, r.b1_o, r.b1s_f, r.b1s_o,
                         r.b4_f, r.b4_o, r.b4s_f, r.b4s_o, r.die2, r.die3, r.n4_f, r.n4_o, r.blow_f, r.blow_o,
                         r.dps_f, r.dps_o, r.dps10_f, r.dps10_o, r.full, r.combo, r.combo_o, r.shots, r.parry, r.reel))
end

-- each path, taken again, does more of what it does
local function line(p, n)
  if n == 0 then return R["no path"] end
  local b = {}
  for i = 1, n do b[i] = p end
  return R[label(b)]
end
for n = 1, 4 do
  local a, b = line(1, n - 1), line(1, n)
  check(b.die2 >= a.die2 and b.die3 >= a.die3 and b.die2 + b.die3 > a.die2 + a.die3,
        "Feral Affinity: every taking, a blow more to take")
  a, b = line(2, n - 1), line(2, n)
  check(b.full > a.full, "Moonlit Breath: every taking, more blows on a breath")
  a, b = line(3, n - 1), line(3, n)
  check(b.parry > a.parry and b.reel > a.reel and b.shots <= a.shots, "Quicksilver Rite: every taking, the pistol")
  a, b = line(4, n - 1), line(4, n)
  check(b.dps_o > a.dps_o and b.b1_o <= a.b1_o, "Serrated Oath: every taking, the open saw bites deeper")
  a, b = line(5, n - 1), line(5, n)
  check(b.combo < a.combo and b.dps_f >= a.dps_f, "Hunter's Path: every taking, the folded blade quicker")
end
-- less every time: the open saw's damage
local d = {}
for n = 0, 4 do d[n] = line(4, n).dps_o end
check(d[1] - d[0] > d[2] - d[1] and d[2] - d[1] > d[3] - d[2] and d[3] - d[2] > d[4] - d[3],
      "Serrated Oath: less every time")
-- a path followed does its thing better than any mix
local best = { die = 0, full = 0, reel = 0, dps_o = 0, combo = 99 }
local focus = { die = line(1, 4), full = line(2, 4), reel = line(3, 4), dps_o = line(4, 4), combo = line(5, 4) }
for _, b in ipairs(MIXES) do
  local r = R[label(b)]
  check(r.die2 + r.die3 <= focus.die.die2 + focus.die.die3 and r.full <= focus.full.full and
        r.reel <= focus.reel.reel and r.dps_o <= focus.dps_o.dps_o and r.combo >= focus.combo.combo,
        label(b) .. ": a mix does nothing better than a path followed")
end
-- the two forms of the saw cleaver: folded a little quicker and lighter,
-- more damage per second; open slower and heavier, a townsman in fewer blows
local r0 = R["no path"]
check(r0.combo < r0.combo_o and r0.combo > 0.6 * r0.combo_o, "the folded blade: a little quicker")
check(r0.blow_o > 1.3 * r0.blow_f, "the open saw: heavier blows")
check(r0.dps_f > r0.dps_o and r0.dps10_f > r0.dps10_o, "the folded blade: more damage per second")
check(r0.town_o < r0.town_f, "the open saw: a townsman in fewer blows")
-- and nothing turns the fight upside down
for name, r in pairs(R) do
  check(r.town_f >= 2 and r.town_o >= 2, name .. ": a townsman takes more than one blow")
  check(r.b1s_f >= 5 and r.b1s_o >= 5, name .. ": the Butcher takes a while")
  check(r.die3 <= 7 and r.die2 <= 10, name .. ": blows still hurt")
  check(r.dps_o <= 2 * R["no path"].dps_o and r.dps_f <= 2 * R["no path"].dps_f, name .. ": at most twice the damage")
end

io.write(string.format("balance: %d checks passed\n", checks))
