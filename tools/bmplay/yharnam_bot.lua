-- A hunter that plays Yharnam by itself, for the videos (tools/bmplay):
-- from the title it starts a hunt, finds its way through the streets of the
-- first area to its two lamps (lit, a path taken when the echoes are
-- enough) and to the Butcher's pyre, and fights what it meets on the way:
-- it locks on, strikes in combos when the creature is open, steps aside
-- when a blow comes, now and then parries with the pistol and tears the
-- reeling creature open, backs off to breathe, heals when it must. It
-- knows what the game knows (YHARNAM): it is a demonstration, not a player.
--
-- bmplay calls bot(frame) every frame; it returns the buttons held, and
-- true when the hunt is over (the Butcher slain and a few seconds after,
-- or the hunt lost). BOT_LOG: a function for its notes (default stderr).

local Y = YHARNAM
local P, F, G = Y.player, Y.FOE, Y.G
local TS, CS = Y.TS, Y.CS
local CPX = TS * CS
local FIRST, RANGE = F.ai.FIRST, F.ai.RANGE
local floor, sqrt, abs, max, min = math.floor, math.sqrt, math.abs, math.max, math.min
local LEFT, RIGHT, UP, DOWN, A, BB, X, YB, START, SELECT, L1, R1 = 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048
local log = BOT_LOG or function(s) io.stderr:write(s .. "\n") end
BOT_TRACE = os.getenv("BOT_TRACE")

-- the bot's own dice (the game's math.random stays the game's)
local seed = 12345
local function rnd()
  seed = (seed * 1103515245 + 12345) % 2147483648
  return seed / 2147483648
end

local S = { goal = 1, frame = 0, deaths = 0, won_at = nil, lost = false, lamps = 0, kills = 0, paths = 0 }
BOT = S

------------------------------------------------------------------ the way

local function tkey(tx, ty) return (tx + 4096) * 8192 + (ty + 4096) end
-- the fires of the chunks round the hunter (braziers, pyres): they burn
local fires = {}
local function gather_fires()
  for k = #fires, 1, -1 do fires[k] = nil end
  local pcx, pcy = floor(P.x / CPX), floor(P.y / CPX)
  for cy = pcy - 1, pcy + 1 do
    for cx = pcx - 1, pcx + 1 do
      for _, f in ipairs(Y.ensure(cx, cy).fires) do
        fires[#fires + 1] = { x = f.x, y = f.y + (f.big and 10 or 25), r = (f.big and 26 or 16) + 12 }
      end
    end
  end
end
local function in_fire(x, y, k)
  for _, f in ipairs(fires) do
    local ex, ey = (x - f.x) / (f.r * k), (y - f.y) / (f.r * k * 0.6)
    if ex * ex + ey * ey < 1 then return f end
  end
end
local function walkable(tx, ty)
  local x, y = tx * TS + 8, ty * TS + 8
  return not Y.blocked(x, y) and not in_fire(x, y, 1)
end
local N4 = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
local plan = { path = nil, i = 1, age = 0 }
local visits = {}
local last_tile

-- the streets of the 3 x 3 chunks round the hunter: the way to the goal,
-- or to the tile nearest to it (tiles walked often weigh more)
local function replan(gx, gy)
  local ptx, pty = floor(P.x / TS), floor(P.y / TS)
  local pcx, pcy = floor(P.x / CPX), floor(P.y / CPX)
  for cy = pcy - 1, pcy + 1 do for cx = pcx - 1, pcx + 1 do Y.ensure(cx, cy) end end
  gather_fires()
  local x0, y0, x1, y1 = (pcx - 1) * CS, (pcy - 1) * CS, (pcx + 2) * CS - 1, (pcy + 2) * CS - 1
  local gtx, gty = floor(gx / TS), floor(gy / TS)
  local parent = { [tkey(ptx, pty)] = false }
  local qx, qy, head = { ptx }, { pty }, 1
  local bx, by, bh = ptx, pty, math.huge
  while head <= #qx do
    local tx, ty = qx[head], qy[head]
    head = head + 1
    local h = sqrt((tx - gtx) ^ 2 + (ty - gty) ^ 2) * TS + (visits[tkey(tx, ty)] or 0) * 20
    if h < bh then bx, by, bh = tx, ty, h end
    if tx == gtx and ty == gty then bx, by = tx, ty; break end
    for _, d in ipairs(N4) do
      local nx, ny = tx + d[1], ty + d[2]
      if nx >= x0 and nx <= x1 and ny >= y0 and ny <= y1 then
        local k = tkey(nx, ny)
        if parent[k] == nil and walkable(nx, ny) then
          parent[k] = tkey(tx, ty)
          qx[#qx + 1], qy[#qy + 1] = nx, ny
        end
      end
    end
  end
  local path, k = {}, tkey(bx, by)
  local tx, ty = bx, by
  while parent[k] do
    table.insert(path, 1, { tx * TS + 8, ty * TS + 8 })
    k = parent[k]
    tx, ty = k // 8192 - 4096, k % 8192 - 4096
  end
  plan.path, plan.i, plan.age = path, 1, 0
end

-- buttons to go towards (x, y)
local function towards(x, y, slack)
  slack = slack or 2
  local dx, dy = x - P.x, y - P.y
  local b = 0
  if dx < -slack then b = b | LEFT elseif dx > slack then b = b | RIGHT end
  if dy < -slack then b = b | UP elseif dy > slack then b = b | DOWN end
  return b
end
local function away(x, y)
  local dx, dy = P.x - x, P.y - y
  local b = 0
  if dx < -1 then b = b | LEFT elseif dx > 1 then b = b | RIGHT end
  if dy < -1 then b = b | UP elseif dy > 1 then b = b | DOWN end
  if b == 0 then b = RIGHT end
  return b
end

local stuck = { x = 0, y = 0, t = 0, wiggle = 0, dir = 0 }
local function follow(gx, gy)
  local tile = tkey(floor(P.x / TS), floor(P.y / TS))
  if tile ~= last_tile then visits[tile] = (visits[tile] or 0) + 1; last_tile = tile end
  if stuck.wiggle > 0 then
    stuck.wiggle = stuck.wiggle - 1
    return stuck.dir
  end
  plan.age = plan.age + 1
  if not plan.path or plan.i > #plan.path or plan.age > 90 then replan(gx, gy) end
  local p = plan.path[plan.i]
  while p and abs(p[1] - P.x) + abs(p[2] - P.y) < 6 do
    plan.i = plan.i + 1
    p = plan.path[plan.i]
  end
  if not p then return towards(gx, gy) end
  -- a few tiles ahead when the way there is straight and free
  local q = plan.path[plan.i + 1]
  if q and (q[1] == p[1] or q[2] == p[2]) then p = q end
  -- not moving for a while: a step aside, and another way
  stuck.t = stuck.t + 1
  if stuck.t >= 30 then
    if abs(P.x - stuck.x) + abs(P.y - stuck.y) < 4 then
      stuck.wiggle = 18
      stuck.dir = ({ LEFT, RIGHT, UP, DOWN })[1 + floor(rnd() * 4)]
      visits[tile] = (visits[tile] or 0) + 6
      plan.path = nil
    end
    stuck.x, stuck.y, stuck.t = P.x, P.y, 0
  end
  return towards(p[1], p[2])
end

------------------------------------------------------------------ the fight

local OTHER = { hurt = true, stagger = true, rage = true, held = true, dead = true }
local EVENTS = { "hit", "fire", "slam", "throw", "beam", "howl" }

-- ticks until the creature's next blow lands, if it is coming
local function coming(o)
  if not o.act or OTHER[o.act] then return nil end
  local a = o.def.a[o.anim]
  local next_f
  for _, ev in ipairs(EVENTS) do
    for _, f in ipairs(a[ev] or {}) do
      if (f > o.f or (f == o.f and o.ft == 0 and o.newf)) and (not next_f or f < next_f) then next_f = f end
    end
  end
  if not next_f then return nil end
  local left = -o.ft
  for k = o.f, next_f - 1 do left = left + a.t[k] end
  local rate = (o.style.rate or 1) * (o.agg or 1) * (o.fury and 1.25 or 1)
  return max(0, left) / rate + (o.hold or 0), o.anim
end

local function nearest_foe()
  local best, bd
  for _, o in ipairs(F.list) do
    if o.act ~= "dead" then
      local d = sqrt((o.x - P.x) ^ 2 + (o.y - P.y) ^ 2)
      local range = o.boss and (o.awake and 260 or 0) or (o.alert and 140 or 64)
      if d < range and (not bd or d < bd) then best, bd = o, d end
    end
  end
  return best, bd
end

local fight = { combo = 0, back = 0, side = 1, held_b = 0, dodge_dir = 0, parried = nil, shots = 0, window = 0,
                was = nil, circle = 1, circle_t = 0 }

local function dodge(o)
  -- B tapped with a direction: locked, a quickstep to the side; else a roll away
  local dx, dy = o.x - P.x, o.y - P.y
  local b
  if P.lock then
    fight.side = -fight.side
    local px, py = -dy * fight.side, dx * fight.side
    b = towards(P.x + px, P.y + py, 0.5)
  else
    b = away(o.x, o.y)
  end
  fight.dodge_dir, fight.held_b = b, 2
end

local function fight_frame(o, d)
  local b = 0
  -- a dodge under way: B held a tick or two, then let go with the direction
  if fight.held_b > 0 then
    fight.held_b = fight.held_b - 1
    return fight.dodge_dir | (fight.held_b > 0 and BB or 0)
  end
  -- the lock on the creature fought
  if not P.lock and not P.act and d < 120 and S.frame % 2 == 0 then return L1 end
  -- a creature reeling ahead: the visceral attack
  if F.visceral_target() and not P.act then return (S.frame % 2 == 0) and A or 0 end
  local reach = (P.ext and 34 or 26) + o.st.r - 6
  local left, move = coming(o)
  local danger = left and d < (RANGE[move] or o.st.reach) + 18
  -- slams, bombs and blasts: out of the way
  if danger and (move == "slam" or move == "throw" or move == "blast" or move == "pounce") and d < 90 then
    if left < 14 and not P.act and P.st > 15 then dodge(o); return fight.dodge_dir end
    return away(o.x, o.y)
  end
  for _, s in ipairs(F.shots) do
    if s.kind == "bomb" and abs(s.tx - P.x) + abs(s.ty - P.y) < 40 then return away(s.tx, s.ty) end
  end
  for _, bu in ipairs(F.burns) do
    if abs(bu.x - P.x) < 22 and abs(bu.y - P.y) < 14 then return away(bu.x, bu.y) end
  end
  if danger then
    -- a parry now and then: the shot while it winds up, then the visceral
    if not P.act and left >= 16 and left <= 40 and (move == "attack" or move:sub(1, 5) == "combo") and
       fight.parried ~= o.f + 1000 * #o.anim and rnd() < (o.boss and 0.55 or 0.35) then
      fight.parried = o.f + 1000 * #o.anim
      fight.shots = fight.shots + 1
      return X
    end
    if left <= 12 and (not P.act or P.act == "attack") and P.st > 12 then
      dodge(o)
      return fight.dodge_dir
    end
  end
  -- out of breath, or its combo done: back off a little
  if fight.back > 0 then
    fight.back = fight.back - 1
    return away(o.x, o.y)
  end
  if P.st < 22 then
    fight.back = 20
    return away(o.x, o.y)
  end
  -- blood, when there is time
  if P.hp <= 4 and G.echoes >= 120 and not P.act and (not left or left > 40) and d > 40 then
    return S.frame % 2 == 0 and SELECT or 0
  end
  -- a boss: the hunter waits for its move at a distance, out of the way or
  -- under it, and strikes after it (or while it reels, or roars)
  if o.boss then
    local moving = o.act and not OTHER[o.act]
    if fight.was and not moving then fight.window = 70 end
    fight.was = moving and o.act or nil
    if o.act == "stagger" or o.act == "rage" or o.act == "held" then fight.window = max(fight.window, 20) end
    if fight.window > 0 then fight.window = fight.window - 1 end
    if fight.window <= 0 and not moving then
      -- round it, at the length of its arm
      fight.circle_t = fight.circle_t + 1
      if fight.circle_t > 90 then fight.circle, fight.circle_t = -fight.circle, 0 end
      local dx, dy = o.x - P.x, o.y - P.y
      local want = 56
      local k = (d - want) / max(d, 1)
      return towards(P.x + dx * k - dy / max(d, 1) * 12 * fight.circle, P.y + dy * k + dx / max(d, 1) * 12 * fight.circle, 1)
    end
    if moving and not danger then return away(o.x, o.y) end
  end
  if d > reach then
    fight.combo = 0
    return towards(o.x, o.y, 1)
  end
  -- in reach and it is not striking yet: a combo, then away
  if not left or left > 22 then
    if P.act == "attack" and P.combo >= (o.boss and 3 or 2 + floor(rnd() * 2)) then
      fight.back = o.boss and 26 or 12
      return 0
    end
    -- against a boss, now and then the trick: the saw cleaver transformed in the combo
    if o.boss and P.act == "attack" and P.combo == 2 and rnd() < 0.04 then return YB end
    return (S.frame % 2 == 0) and A or 0
  end
  return 0
end

------------------------------------------------------------------ the hunt

local function goals()
  local l1x, l1y = Y.MAP.lamp_chunk(1, 1)
  local l2x, l2y = Y.MAP.lamp_chunk(1, 2)
  local s1, s2 = Y.ensure(l1x, l1y).shrine, Y.ensure(l2x, l2y).shrine
  local bx, by = Y.MAP.boss_chunk(1)
  return { { kind = "lamp", x = s1.x, y = s1.y + 16, sh = s1 }, { kind = "lamp", x = s2.x, y = s2.y + 16, sh = s2 },
           { kind = "boss", x = bx * CPX + 128, y = by * CPX + 128 } }
end
local GOALS
local lamp_t = 0
local was_dead = false

function bot(frame)
  S.frame = frame
  local st = Y.state()
  if st == "title" then
    return (frame % 150 == 120) and A or 0
  end
  if st == "lost" then
    S.lost = true
    log(string.format("bot: the hunt is lost at frame %d", frame))
    return 0, true
  end
  if not GOALS then GOALS = goals() end
  if st == "lamp" then
    -- a path when the echoes are enough (the Hunter's Path: the folded blade), then away
    lamp_t = lamp_t + 1
    local k = #G.slots + 1
    local costs = { 800, 1200, 1600, 2000 }
    if lamp_t > 40 and costs[k] and G.echoes >= costs[k] and not S.bought then
      local want = 6
      if Y.menu.i ~= want then return (lamp_t % 8 == 0) and DOWN or 0 end
      if lamp_t % 8 == 0 then S.bought, S.paths = true, S.paths + 1; return A end
      return 0
    end
    if lamp_t > 70 then return (lamp_t % 8 == 0) and BB or 0 end
    return 0
  end
  lamp_t, S.bought = 0, false
  if st ~= "play" then return 0 end
  if P.act == "dead" then
    if not was_dead then
      S.deaths = S.deaths + 1
      was_dead = true
      local o, d = nearest_foe()
      log(string.format("bot: dead at frame %d (%d,%d), goal %d, by %s %s at %d", frame, floor(P.x), floor(P.y),
                        S.goal, o and o.name or "?", o and tostring(o.act) or "", floor(d or 0)))
    end
    return 0
  end
  if BOT_TRACE and frame % 600 == 0 then
    local o, d = nearest_foe()
    log(string.format("bot: %d (%d,%d) goal %d hp %.1f st %d echoes %d foe %s %d", frame, floor(P.x), floor(P.y),
                      S.goal, P.hp, floor(P.st), G.echoes, o and o.name or "-", floor(d or 0)))
  end
  was_dead = false
  if F.won or S.won_at then
    S.won_at = S.won_at or frame
    if frame - S.won_at > 360 then
      log(string.format("bot: the Butcher slain at frame %d; deaths %d, lamps %d, paths %d, echoes %d",
                        S.won_at, S.deaths, S.lamps, S.paths, G.echoes))
      return 0, true
    end
    return 0
  end
  -- out of the fire first, then a fight
  if frame % 30 == 0 then gather_fires() end
  local burn = in_fire(P.x, P.y, 0.9)
  if burn then return away(burn.x, burn.y) end
  local o, d = nearest_foe()
  if o then
    if BOT_TRACE and o.boss and frame % 30 == 0 then
      log(string.format("boss %d: hp %.1f ph %d act %s | hunter hp %.1f st %d act %s ext %s", frame, o.hp, o.ph,
                        tostring(o.act), P.hp, floor(P.st), tostring(P.act), tostring(P.ext)))
    end
    return fight_frame(o, d)
  end
  if P.lock and S.frame % 2 == 0 then return L1 end     -- nothing to fight: the lock off
  fight.combo, fight.back = 0, 0
  -- the next goal
  local g = GOALS[S.goal]
  while g and g.kind == "lamp" and G.lit[g.sh.key] do
    S.goal, S.lamps = S.goal + 1, S.lamps + 1
    plan.path = nil
    g = GOALS[S.goal]
  end
  if not g then return 0 end
  if g.kind == "boss" then
    -- the boss itself, once its arena has it
    for _, b in ipairs(F.list) do
      if b.boss and b.act ~= "dead" then g = { kind = "boss", x = b.x, y = b.y + 20 } end
    end
  end
  local dd = abs(g.x - P.x) + abs(g.y - P.y)
  if g.kind == "lamp" and dd < 14 and not P.act then
    return (frame % 10 == 0) and A or 0
  end
  if P.hp < P.hpmax * 0.6 and G.echoes >= 400 and not P.act then
    return (frame % 10 == 0) and SELECT or 0
  end
  local b = follow(g.x, g.y)
  -- the street is clear: the hunter runs (B held), his breath allowing
  if P.st > 70 and not P.act and dd > 60 and g.kind ~= "boss" then b = b | BB end
  return b
end
