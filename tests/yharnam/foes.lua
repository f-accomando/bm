-- How the creatures of Yharnam fight, measured: the real code of the game
-- in a fake bm, each creature alone with the hunter on an open street. The
-- hunter cannot be hurt, nor the creature killed; for 30 seconds he
-- stands, then for 30 seconds he swings every second (at the creature, when
-- it is in reach). For each creature (and each phase of the bosses) a line:
--
--   moves/10s   moves begun in 10 seconds (a blow, a shot; a boss's moves)
--   gap s       the pause between the end of a move and the next (mean, least)
--   wind-up s   from the start of a move to its blow (least - most: the
--               held blows and the quick ones make the spread)
--   keeps       how far it stays from the hunter between its moves (median)
--   moving      how much of that time it moves; circling: how much of it
--               round the hunter rather than to or from him
--   backs       how many times it backs away after a blow
--   dodges      how many times it steps away from the hunter's blow
--   counters    how many times it comes at once for the hunter caught in
--               his own blow
--   moves       (the bosses) the moves of the phase, and the patterns seen
--
-- And it checks: everything can be read (a wind-up of 8 ticks at least),
-- no two creatures fight alike, the bosses grow quicker and fiercer from
-- one phase to the next.
--
--   luahost tests/yharnam/foes.lua carts/yharnam/main.lua

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

-- open ground in the first area: free all round, no fire, no lamp, no shrine
local function spot()
  for cy = 0, 3 do
    for cx = 1, 3 do
      local ch = Y.ensure(cx, cy)
      for ty = 2, 13 do
        for tx = 2, 13 do
          local x, y = cx * 256 + tx * 16 + 8, cy * 256 + ty * 16 + 8
          local ok = not Y.blocked(x, y)
          for k = 0, 15 do
            local a = k * math.pi / 8
            for _, r in ipairs({ 24, 48, 72 }) do
              if ok and Y.blocked(x + math.cos(a) * r, y + math.sin(a) * r * 0.8) then ok = false end
            end
          end
          for dy = -1, 1 do
            for dx = -1, 1 do
              local n = Y.ensure(cx + dx, cy + dy)
              for _, f in ipairs(n.fires) do if math.abs(f.x - x) + math.abs(f.y - y) < 220 then ok = false end end
              for _, l in ipairs(n.lamps) do if math.abs(l.x - x) + math.abs(l.y - y) < 60 then ok = false end end
              if n.shrine and math.abs(n.shrine.x - x) + math.abs(n.shrine.y - y) < 90 then ok = false end
            end
          end
          if ok then return x, y end
        end
      end
    end
  end
  error("no open ground")
end
local X0, Y0 = spot()
Y.teleport(X0, Y0)
for i = 1, 60 do B.frame(0) end
B.nodraw = true

local function clear()
  for k = #F.list, 1, -1 do F.list[k] = nil end
  for k = #F.shots, 1, -1 do F.shots[k] = nil end
  for k = #F.rings, 1, -1 do F.rings[k] = nil end
  for k = #F.burns, 1, -1 do F.burns[k] = nil end
  F.boss = nil
end

local FIRST = F.ai.FIRST
local OTHER = { hurt = true, stagger = true, rage = true, held = true, dead = true }

-- a creature (a boss in phase ph) 80 px from the hunter, for `ticks` ticks;
-- swing: the hunter swings every second
local function watch(name, ticks, swing, ph)
  clear()
  P.x, P.y, P.act, P.dir, P.ext, P.st, P.lock = X0, Y0, nil, 2, false, P.stmax, nil
  P.anim, P.f, P.ft = "idle", 1, 0
  local o = F.new(name, X0 + 80, Y0, "watch", 0)
  o.alert, o.cd, o.hp = true, 40, 1e9
  if o.boss then
    o.awake, F.boss = true, o
    o.ph, o.style = ph, F.style_of(name, ph)
    o.hpmax = 1e9 / ({ 0.9, 0.5, 0.2 })[ph]
    if o.style.fury then o.fury = 1e9 end
  end
  local r = { moves = 0, gaps = {}, winds = {}, keep = {}, still = 0, moving = 0, round = 0, backs = 0,
              counters = 0, seen = {}, pats = {} }
  local was, t_end, t_start, px, py, last = nil, nil, nil, o.x, o.y, nil
  local bk = 0
  for t = 1, ticks do
    P.inv, P.hp = 999, P.hpmax
    if P.act == "hurt" or P.act == "down" or P.act == "lying" or P.act == "getup" then P.act = nil end
    -- before a boss he steps back and forth every 4 s: its moves from afar too
    local ax = (o.boss and (t // 240) % 2 == 1) and X0 - 50 or X0
    if not P.act then P.x, P.y = ax, Y0 end
    local press = 0
    if swing and t % 60 == 0 then
      P.dir = F.dir_to(o.x - P.x, o.y - P.y)
      press = 1 << 4
    end
    B.frame(press)
    P.st = P.stmax
    local act = o.act
    if act and not OTHER[act] and act ~= was then
      r.moves = r.moves + 1
      r.seen[act] = true
      if t_end then r.gaps[#r.gaps + 1] = t - t_end end
      if last and t_end and t - t_end <= 13 then r.pats[last .. "+" .. act] = true end   -- a pattern: 4-12 ticks
      t_start, last = t, act
    end
    if o.back > 0 and bk == 0 and o.backsp == 1 then r.backs = r.backs + 1 end
    bk = o.back
    if act and not OTHER[act] and o.newf and o.f == FIRST[o.def.a[o.anim]] and t_start then
      r.winds[#r.winds + 1] = t - t_start
      t_start = nil
    end
    if was and not OTHER[was] and not act then t_end = t end
    if not act then
      local dx, dy = o.x - P.x, o.y - P.y
      local d = math.sqrt(dx * dx + dy * dy)
      r.keep[#r.keep + 1] = d
      local mx, my = o.x - px, o.y - py
      local m = math.sqrt(mx * mx + my * my)
      if m > 0.05 then
        r.moving = r.moving + 1
        local radial = math.abs((mx * dx + my * dy) / math.max(d, 1)) / m
        if radial < 0.5 then r.round = r.round + 1 end
      else
        r.still = r.still + 1
      end
    end
    was, px, py = act, o.x, o.y
    if act == "dead" then break end
  end
  table.sort(r.keep)
  r.keep_m = r.keep[#r.keep // 2 + 1] or 0
  local s, mn = 0, math.huge
  for _, g in ipairs(r.gaps) do s, mn = s + g, math.min(mn, g) end
  r.gap, r.gap_min = #r.gaps > 0 and s / #r.gaps / 60 or 0, #r.gaps > 0 and mn / 60 or 0
  local w0, w1 = math.huge, 0
  for _, w in ipairs(r.winds) do w0, w1 = math.min(w0, w), math.max(w1, w) end
  r.w0, r.w1 = w0, w1
  r.rate = r.moves / ticks * 600
  r.dodges, r.counters = o.dodges or 0, o.counters or 0
  local wait = r.still + r.moving
  r.mov = wait > 0 and r.moving / wait or 0
  r.circ = r.moving > 0 and r.round / r.moving or 0
  clear()
  return r
end

local function line(label, r, extra)
  io.write(string.format("%-16s %5.1f   %4.2f %4.2f   %4.2f - %-4.2f  %4d   %3d%%   %3d%%   %4d %5d %6d   %s\n",
                         label, r.rate, r.gap, r.gap_min, r.w0 / 60, r.w1 / 60, math.floor(r.keep_m + 0.5),
                         math.floor(r.mov * 100 + 0.5), math.floor(r.circ * 100 + 0.5), r.backs, r.dodges,
                         r.counters, extra or ""))
end
io.write("creature        moves   gap s       wind-up s     keeps  moving circling backs dodges counters\n")
io.write("                /10s    mean least  least - most  px\n")

-- the creatures: 30 s standing, 30 s swinging at the air
local R = {}
for _, f in ipairs(Y.FOES) do
  if not f.boss then
    local a = watch(f.name, 1800, false)
    local b = watch(f.name, 1800, true)
    -- one line: its ways from the first half, what it does under the
    -- hunter's blows from the second
    local r = {}
    for k, v in pairs(a) do r[k] = v end
    r.backs, r.dodges, r.counters = a.backs + b.backs, b.dodges, b.counters
    r.w0, r.w1 = math.min(a.w0, b.w0), math.max(a.w1, b.w1)
    R[f.name] = r
    line(f.name, r)
    check(a.moves >= 2, f.name .. ": it fights")
    check(r.w0 >= 8, f.name .. ": every blow can be read (" .. r.w0 .. " ticks)")
  end
end

-- no two fight alike: each differs from every other in its pace, its
-- distance, its wind-up or its moving about
local names = {}
for n in pairs(R) do names[#names + 1] = n end
table.sort(names)
local function differ(a, b)
  return math.abs(a.rate - b.rate) > 0.25 * math.max(a.rate, b.rate) or math.abs(a.keep_m - b.keep_m) > 10 or
         math.abs(a.w1 - b.w1) > 8 or math.abs(a.mov - b.mov) > 0.2 or math.abs(a.circ - b.circ) > 0.25 or
         math.abs(a.backs - b.backs) > 4 or math.abs(a.dodges - b.dodges) > 3 or
         math.abs(a.counters - b.counters) > 3
end
for i = 1, #names do
  for j = i + 1, #names do
    check(differ(R[names[i]], R[names[j]]), names[i] .. " and " .. names[j] .. " fight alike")
  end
end

-- the bosses, phase by phase (as if their blood were at 90%, 50%, 20%)
io.write("\n")
for _, f in ipairs(Y.FOES) do
  if f.boss then
    local prev
    for ph = 1, 3 do
      local r = watch(f.name, 1800, false, ph)
      local b = watch(f.name, 1800, true, ph)
      r.dodges, r.counters = b.dodges, b.counters
      local mv, pt = {}, {}
      for k in pairs(r.seen) do mv[#mv + 1] = k end
      for k in pairs(r.pats) do pt[#pt + 1] = k end
      table.sort(mv)
      table.sort(pt)
      line(f.name .. " " .. ph, r, table.concat(mv, " ") .. (#pt > 0 and "; " .. table.concat(pt, " ") or ""))
      check(r.moves >= 3, f.name .. " " .. ph .. ": it fights")
      check(r.w0 >= 8, f.name .. " " .. ph .. ": every move can be read")
      -- the moves of the phase (its tables), the patterns among them
      local S, all, pats = F.style_of(f.name, ph), {}, 0
      for _, band in ipairs({ "near", "mid", "far" }) do
        local l = S[band] or {}
        for i = 1, #l, 2 do
          if l[i]:find("+", 1, true) then pats = pats + 1 end
          for m in l[i]:gmatch("[^+]+") do all[m] = true end
        end
      end
      local nall = 0
      for _ in pairs(all) do nall = nall + 1 end
      r.nall, r.npats = nall, pats
      if prev then
        check(r.rate > prev.rate and r.gap < prev.gap, f.name .. " " .. ph .. ": quicker than the phase before")
        check(nall + pats > prev.nall + prev.npats and pats > prev.npats, f.name .. " " .. ph .. ": more ways than before")
      else
        check(nall <= 4 and pats == 0, f.name .. ": plain at first")
      end
      if ph == 3 then check(#pt >= 2, f.name .. ": patterns at the end") end
      prev = r
    end
  end
end

io.write(string.format("foes: %d checks passed\n", checks))
