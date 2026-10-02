-- A hunt of Yharnam 8 played by a bot, from the title to the Butcher slain,
-- for a video (make yharnam8-video): it lights the first lamp, crosses the
-- town fighting what wakes (blows and combos, dodges, the pistol's parry
-- and the visceral, a vial when low), takes a path at the second lamp and
-- fights the Butcher. It plays with the buttons only (btn/btnp are its);
-- the run is always the same (srand). Run inside nano8 by n8host:
--
--   n8host main.lua --root SD --exec "NANO8.Ui.play(1)" \
--     --at "5:exec=dofile('tests/yharnam/y8_bot.lua')" --video - ...

local G = NANO8.Vm.G
local held, prev = {}, {}
G.btn = function(b) return held[b or 0] or false end
G.btnp = function(b) return (held[b or 0] and not prev[b or 0]) or false end
G.srand(1)

local function log(...) io.stderr:write(string.format(...), "\n") end
local function wait(n) for _ = 1, n or 1 do coroutine.yield() end end
local function set(...) held = {}; for _, b in ipairs({ ... }) do held[b] = true end end
local function tap(...) set(...); wait(2); set(); wait(1) end

local LEFT, RIGHT, UP, DOWN, O, X = 0, 1, 2, 3, 4, 5

-- the way there: a search over the tiles, again every so often
local function walkable(tx, ty)
  if tx < 0 or ty < 0 or tx > 127 or ty > 63 then return false end
  return not G.solid(tx * 8 + 4, ty * 8 + 4)
end
local route, route_to, route_t = nil, nil, 0
local function find(tx, ty)
  local P = G.P
  local sx, sy = P.x // 8, (P.y - 2) // 8
  local key = function(x, y) return y * 128 + x end
  local from, todo, i = { [key(sx, sy)] = -1 }, { { sx, sy } }, 1
  while todo[i] do
    local x, y = todo[i][1], todo[i][2]
    i = i + 1
    if x == tx and y == ty then break end
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = x + d[1], y + d[2]
      if walkable(nx, ny) and not from[key(nx, ny)] then
        from[key(nx, ny)] = key(x, y)
        todo[#todo + 1] = { nx, ny }
      end
    end
  end
  if not from[key(tx, ty)] then return nil end
  local path, k = {}, key(tx, ty)
  while k ~= -1 do
    table.insert(path, 1, { k % 128, k // 128 })
    k = from[k]
  end
  return path
end

-- steer to (x, y) in pixels, along the tiles; -> true when there
local function go(x, y, near)
  local P = G.P
  local tx, ty = x // 8, y // 8
  local t = G.t0
  if route_to ~= ty * 128 + tx or t - route_t > 30 or not route then
    route, route_to, route_t = find(tx, ty), ty * 128 + tx, t
  end
  local dx, dy = x - P.x, y - P.y
  if math.abs(dx) < (near or 4) and math.abs(dy) < (near or 4) then set(); return true end
  -- the next tile of the way not reached yet
  local wx, wy = x, y
  if route then
    while #route > 1 do
      local c = route[1]
      local cx, cy = c[1] * 8 + 4, c[2] * 8 + 6
      if math.abs(cx - P.x) < 4 and math.abs(cy - P.y) < 4 then table.remove(route, 1) else break end
    end
    if #route > 1 then wx, wy = route[1][1] * 8 + 4, route[1][2] * 8 + 6 end
  end
  dx, dy = wx - P.x, wy - P.y
  local b = {}
  if dx > 1 then b[#b + 1] = RIGHT elseif dx < -1 then b[#b + 1] = LEFT end
  if dy > 1 then b[#b + 1] = DOWN elseif dy < -1 then b[#b + 1] = UP end
  set(table.unpack(b))
  return false
end

local function dist(a, b) return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2) end

-- the nearest creature awake (or about to wake), in reach of the way
local function foe_near(r)
  local best, bd
  for _, f in ipairs(G.F) do
    if f.st ~= "dead" and (f.st ~= "idle" or dist(f, G.P) < 50) then
      local d = dist(f, G.P)
      if d < r and (not bd or d < bd) then best, bd = f, d end
    end
  end
  return best, bd
end

-- away from (x, y): the buttons
local function away(x, y)
  local P = G.P
  local dx, dy = P.x - x, P.y - y
  local b = {}
  if dx > 2 then b[#b + 1] = RIGHT elseif dx < -2 then b[#b + 1] = LEFT end
  if dy > 2 then b[#b + 1] = DOWN elseif dy < -2 then b[#b + 1] = UP end
  if #b == 0 then b[1] = LEFT end
  return b
end

-- a dodge: away from the danger, the stick held, X tapped
local function dodge_from(x, y)
  local b = away(x, y)
  set(table.unpack(b)); wait(1)
  local c = { table.unpack(b) }
  c[#c + 1] = X
  set(table.unpack(c)); wait(2)
  set(table.unpack(b)); wait(10)
  set()
end

local parries = 0
local function fight(f)
  local P = G.P
  if f.boss and os.getenv("BOT_TRACE") and f.st ~= (f.last or "") then
    log("boss %d: %s %s hp %.0f ph %d | hunter hp %.1f", NANO8.Vm.ticks, f.st, f.st == "wind" and f.mv or "", f.hp,
        f.ph, P.hp)
    f.last = f.st
  end
  local d = dist(f, P)
  local T = f.T
  -- the danger of shots and rings
  for _, s in ipairs(G.S) do
    if math.abs(s.x - P.x) < 14 and math.abs(s.y - (P.y - 6)) < 14 and P.act ~= "dodge" then
      dodge_from(s.x - s.vx * 4, s.y - s.vy * 4); return
    end
  end
  for _, r in ipairs(G.R) do
    local rd = dist(r, P)
    if not r.h and rd - r.r < 10 and rd - r.r > 0 and P.act ~= "dodge" then dodge_from(r.x, r.y); return end
  end
  if f.st == "stag" and d < 18 then
    log("visceral at %d", G.t0)
    tap(O); wait(20); return
  end
  if f.st == "wind" then
    local melee = f.mv:sub(1, 1) == "m"
    if melee and f.tm <= 8 and f.tm >= 5 and d < T.r + 14 and d > 8 and parries % 2 == 0 then
      parries = parries + 1
      log("parry try at %d (%s)", G.t0, T.nm)
      set(O, X); wait(2); set(); wait(6)
      return
    end
    local shot = f.mv == "g" or f.mv == "gun" or f.mv == "fire"
    if f.tm <= 5 and shot then
      -- a shot coming: a step aside, across its line
      local dx, dy = f.x - P.x, f.y - P.y
      dodge_from(P.x + dy, P.y - dx); return
    end
    if f.tm <= 5 and (melee and d < T.r + 16 or f.mv == "leap") then
      parries = parries + 1
      dodge_from(f.x, f.y); return
    end
    if shot and d > 16 then
      -- it aims: run at it
      go(f.x, f.y, 10)
      local b = {}
      for k in pairs(held) do b[#b + 1] = k end
      b[#b + 1] = X
      set(table.unpack(b)); wait(1); return
    end
    if melee and d < T.r + 6 then set(table.unpack(away(f.x, f.y))); wait(2); return end
    set(); wait(1); return
  end
  if P.st < 20 or f.st == "move" and f.boss and f.cd > 10 and d < 20 then
    -- out of breath, or the boss about to turn: a step away
    set(table.unpack(away(f.x, f.y))); wait(3); return
  end
  if d > 15 then
    go(f.x, f.y, 10)
    if d > 40 and T.mv == "g" then
      local b = {}
      for k in pairs(held) do b[#b + 1] = k end
      b[#b + 1] = X
      set(table.unpack(b))
    end
    wait(1); return
  end
  -- blows: a combo of two or three, sometimes the trick, a charged blow
  local n = (G.t0 // 7) % 3
  if n == 0 and f.st == "rec" and f.tm > 20 then
    set(O); wait(24); set(); wait(14)
  else
    tap(O); wait(7); tap(O); wait(7)
    if n == 1 then set(O, X); wait(2); set(); wait(14) else tap(O); wait(12) end
  end
end

-- a vial when the blood runs low and nothing is near
local function heal_if()
  local P = G.P
  if P.hp < 9 and G.G.ec >= 100 and not foe_near(56) and P.act == nil then
    log("vial at %d (hp %.1f)", G.t0, P.hp)
    set(X); wait(28); set(); wait(25)
    return true
  end
end

-- the way: the lamps, the square, the street east, the arena
local WAY = {
  { 6, 14, "lamp" }, { 16, 16 }, { 30, 13 }, { 30, 20 }, { 37, 16 }, { 44, 17, "lamp" }, { 48, 15 },
  { 52, 15 }, { 55, 18, "boss" },
}

local function lamp_here()
  tap(O); wait(6)
  if G.st == "lamp" then
    -- a path, if the echoes suffice: hunter's path (the folded saw)
    if G.G.ec >= 300 then
      for _ = 1, 4 do tap(DOWN); wait(3) end
      tap(O); wait(10)
      log("path taken at %d, echoes left %.0f", G.t0, G.G.ec)
    end
    wait(30)
    tap(X); wait(4)
  end
end

local function hunt()
  wait(40)
  tap(O); wait(20)
  local k = 1
  while k <= #WAY do
    local w = WAY[k]
    local P = G.P
    if os.getenv("BOT_TRACE") and NANO8.Vm.ticks % 120 == 0 then
      local f, fd = foe_near(140)
      log("t %d step %d at %.0f,%.0f hp %.1f st %.0f ec %.0f act %s foe %s %s %s", NANO8.Vm.ticks, k, P.x // 8,
          P.y // 8, P.hp, P.st, G.G.ec, tostring(P.act), f and f.T.nm or "-", f and f.st or "", fd and math.floor(fd) or "")
    end
    if G.st ~= "play" then
      if G.st == "lost" then log("the hunt lost at %d", G.t0); while true do wait() end end
      wait()
    elseif P.act == "dead" then
      wait(100)
      -- back at the lamp: the way again from there
      k = 1
      for i, ww in ipairs(WAY) do if ww[3] == "lamp" and math.abs(ww[1] * 8 + 4 - P.x) < 16 then k = i + 1 end end
      log("died; on from step %d", k)
    elseif not heal_if() then
      local f = foe_near(w[3] == "boss" and 140 or 70)
      if f then
        fight(f)
      elseif w[3] == "boss" then
        local b
        for _, o in ipairs(G.F) do if o.boss == 1 then b = o end end
        if not b or b.st == "dead" or G.G.won[1] then break end
        go(b.x, b.y, 30); wait()
      elseif go(w[1] * 8 + 4, w[2] * 8 + 6, 5) then
        if w[3] == "lamp" then lamp_here() end
        k = k + 1
      else
        wait()
      end
    end
  end
  log("the Butcher slain at tick %d; deaths %d, slain %d, echoes %.0f", NANO8.Vm.ticks, G.G.deaths, G.G.kills,
      G.G.ec)
  set()
  -- (to make the video, the run is played again for as many frames)
  if not os.getenv("Y8_NOEXIT") then wait(150); os.exit(0) end
  while true do wait() end
end

local co = coroutine.create(hunt)
local upd = G._update
G._update = function()
  local ok, e = coroutine.resume(co)
  if not ok then
    log("bot: %s", tostring(e))
    log("%s", debug.traceback(co))
    os.exit(1)
  end
  upd()
  for b = 0, 5 do prev[b] = held[b] end
end
