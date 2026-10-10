-- Bots: the heroes nobody plays (M38.4). Three layers:
--   1. a TACTIC every half second: go to the point, fight, fall back, flank,
--      guard a friend, hold where it is. A small INT8 network chooses it
--      (nnet(), trained on the PC by art/brain.py on the bots' own matches:
--      76_brain.lua); without it, the rules of `teacher` (also the teacher of
--      the network);
--   2. the WAY: the map's graph of places (mapbake.navgraph), the shortest
--      path to the goal of the tactic;
--   3. the HANDS: seeing (rays to the bodies), aiming with a reaction time
--      and a wandering error (by difficulty), leading the slow shots,
--      strafing, the abilities of each hero when they make sense.
-- Bots.log (training builds): every choice, with what the bot saw, and the
-- events that tell the trainer how it went.

Bots = { diff = 2, net = nil, log = false, explore = 0 }
-- nnet() is the kernel's (src/ai/net.c); the RGB30's kernels before it had a
-- placeholder table there: on those the bots choose by the rules alone
Bots.has_nnet = type(nnet) == "function"

local TACTICS = { "point", "fight", "retreat", "flank", "guard", "hold" }
Bots.TACTICS = TACTICS
local T_POINT, T_FIGHT, T_RETREAT, T_FLANK, T_GUARD, T_HOLD = 1, 2, 3, 4, 5, 6
local NF = 24                 -- what a bot sees, as numbers (features below)
Bots.NF = NF

-- how good the bots are: reaction (s), aim error (radians), turning (rad/s)
local SKILL = {
  { react = 0.50, err = 0.080, turn = 3.5, name = "EASY" },
  { react = 0.30, err = 0.045, turn = 5.5, name = "NORMAL" },
  { react = 0.17, err = 0.022, turn = 8.0, name = "HARD" },
}
Bots.SKILL = SKILL

-- how each hero fights: reach (m), the speed of its shots (for leading;
-- nil = instant), the button that hurts and the one that heals
local STYLE = {
  rally = { range = 13, keep = 6 }, kaiju = { range = 4.5, keep = 2 },
  sarge = { range = 30, keep = 14 }, frost = { range = 9, keep = 5 },
  fuse = { range = 20, keep = 10, speed = 22, lob = true }, rail = { range = 35, keep = 16 },
  orbit = { range = 25, keep = 12, healer = "fire", speed = 60 },
  akari = { range = 22, keep = 10, healer = "fire", attack = "fire2", attack_p = true, speed = 80 },
}
Bots.STYLE = STYLE

local NAV, P                  -- the graph (Bots.init) and the point

-- ---------------------------------------------------------------- the way

-- the next node towards a goal from every node: Dijkstra backwards from
-- the goal nodes (75 nodes: a moment), cached by the name of the goal
local flows = {}
local function flow(name, goals)
  local f = flows[name]
  if f then return f end
  local n = NAV.n
  local back = {}
  for i = 1, n do back[i] = {} end
  for i = 1, n do for _, j in ipairs(NAV.links[i]) do back[j][#back[j] + 1] = i end end
  local dist, nxt, done = {}, {}, {}
  for _, g in ipairs(goals) do dist[g] = 0 end
  while true do
    local best, bd = nil, math.huge
    for i = 1, n do if dist[i] and not done[i] and dist[i] < bd then best, bd = i, dist[i] end end
    if not best then break end
    done[best] = true
    for _, i in ipairs(back[best]) do
      local d = bd + len3(NAV.x[i] - NAV.x[best], NAV.y[i] - NAV.y[best], NAV.z[i] - NAV.z[best])
      if not dist[i] or d < dist[i] then dist[i], nxt[i] = d, best end
    end
  end
  f = { next = nxt, dist = dist, goal = {} }
  for _, g in ipairs(goals) do f.goal[g] = true end
  flows[name] = f
  return f
end

local function nodes_named(pred)
  local out = {}
  for i = 1, NAV.n do if pred(NAV.names[i]) then out[#out + 1] = i end end
  return out
end

-- the flows the tactics use: to the point, back to each spawn, round the side
local FLOW = {}
function Bots.init()
  local src = World.MAP and World.MAP.nav
  P = World.mark("point")
  if not src or NAV then return end
  NAV = { x = {}, y = {}, z = {}, links = src.links, names = src.names, n = #src.nodes // 3 }
  for i = 1, NAV.n do
    NAV.x[i], NAV.y[i], NAV.z[i] = src.nodes[i * 3 - 2], src.nodes[i * 3 - 1], src.nodes[i * 3]
  end
  FLOW.point = flow("point", nodes_named(function(s) return s:sub(1, 2) == "p_" end))
  -- team 1 lives at the west end (names ending "_w"), team 2 at the east
  FLOW.home = {
    flow("home1", nodes_named(function(s) return s == "spawn_w" or s == "door_w" end)),
    flow("home2", nodes_named(function(s) return s == "spawn_e" or s == "door_e" end)),
  }
  -- the flanks: the alley's end, the quay's end, the terrace
  FLOW.flank = {
    flow("flank_alley", nodes_named(function(s) return s == "al5_w" or s == "al5_e" end)),
    flow("flank_quay", nodes_named(function(s) return s == "q6_w" or s == "q6_e" end)),
    flow("flank_terrace", nodes_named(function(s) return s == "terrace_w" or s == "terrace_e" end)),
  }
  Bots.nav = NAV
end

-- the nearest node a bot can walk to in a straight line
local function nearest_node(a)
  local best, bd = nil, math.huge
  for i = 1, NAV.n do
    local dx, dz = NAV.x[i] - a.x, NAV.z[i] - a.z
    local d = dx * dx + dz * dz + (NAV.y[i] - a.y) ^ 2 * 4
    if d < bd and d < 500 and World.clear(a.x, a.y + 1.0, a.z, NAV.x[i], NAV.y[i] + 1.0, NAV.z[i]) then
      best, bd = i, d
    end
  end
  return best
end

-- ---------------------------------------------------------------- seeing

local function sees(a, o)
  return World.clear(a.x, a.y + a.eye, a.z, o.x, o.y + o.height * 0.6, o.z)
end

local function hp_frac(o) return Actors.total(o) / max(1, Actors.total_max(o)) end

local function on_point(o)
  if not P then return false end
  local dx, dz = o.x - P.x, o.z - P.z
  return dx * dx + dz * dz <= P.r * P.r and o.y < P.y + 3
end

-- what the bot sees, every quarter of a second: the enemies in sight, the
-- friends around, the hurt friend nearest
local function look(a, b)
  local foes, nfoe, best, bd, weak, wk = b.foes, 0, nil, math.huge, nil, 2
  local friends, hurt, hk = 0, nil, 0.75
  local foes_on, friends_on = 0, 0
  for _, o in ipairs(G.actors) do
    if o.alive and o ~= a then
      local dx, dy, dz = o.x - a.x, o.y - a.y, o.z - a.z
      local d2 = dx * dx + dy * dy + dz * dz
      if o.team ~= a.team then
        if on_point(o) then foes_on = foes_on + 1 end
        if d2 < 45 * 45 and sees(a, o) then
          nfoe = nfoe + 1
          foes[nfoe] = o
          if d2 < bd then best, bd = o, d2 end
          local k = hp_frac(o)
          if k < wk then weak, wk = o, k end
        end
      else
        if on_point(o) then friends_on = friends_on + 1 end
        if d2 < 12 * 12 then friends = friends + 1 end
        local k = hp_frac(o)
        if k < hk and d2 < 25 * 25 and sees(a, o) then hurt, hk = o, k end
      end
    end
  end
  for i = nfoe + 1, #foes do foes[i] = nil end
  b.nfoe, b.near_foe, b.near_d, b.weak_foe = nfoe, best, best and sqrt(bd) or 99, weak
  b.friends, b.hurt = friends, hurt
  b.foes_on, b.friends_on = foes_on, friends_on
end

-- ---------------------------------------------------------------- choosing

local function counts(team)
  local mine, theirs = 0, 0
  for _, o in ipairs(G.actors) do
    if o.alive then if o.team == team then mine = mine + 1 else theirs = theirs + 1 end end
  end
  return mine, theirs
end

-- the numbers the network sees (all about -1..1)
local function features(a, b, M, f)
  local role = a.hero.role
  local mine, theirs = counts(a.team)
  local other = 3 - a.team
  local own = M.owner == a.team and 1 or M.owner == other and -1 or 0
  local cap = M.cap_team == a.team and M.cap or M.cap_team == other and -M.cap or 0
  local dp = P and len3(a.x - P.x, 0, a.z - P.z) or 0
  f[1] = hp_frac(a)
  f[2] = a.ult >= 100 and 1 or 0
  f[3] = role == "tank" and 1 or 0
  f[4] = role == "damage" and 1 or 0
  f[5] = role == "support" and 1 or 0
  f[6] = min(1, dp / 40)
  f[7] = on_point(a) and 1 or 0
  f[8] = own
  f[9] = cap
  f[10] = (M.pct and M.pct[a.team] or 0) / 100
  f[11] = (M.pct and M.pct[other] or 0) / 100
  f[12] = M.phase == "play" and 1 or 0
  f[13] = (M.overtime or M.contested) and 1 or 0
  f[14] = mine / 5
  f[15] = theirs / 5
  f[16] = min(4, b.friends) / 4
  f[17] = min(5, b.nfoe) / 5
  f[18] = min(1, b.near_d / 40)
  f[19] = b.near_foe and hp_frac(b.near_foe) or 1
  f[20] = b.hurt and hp_frac(b.hurt) or 1
  f[21] = min(1, (b.hurt_recent or 0) / max(1, Actors.total_max(a)))
  f[22] = b.friends_on / 5
  f[23] = b.foes_on / 5
  f[24] = min(1, (G.t - (b.born or G.t)) / 20)
  return f
end

-- the rules: what a sensible player does (the network learns from them,
-- then from how its own choices went)
local function teacher(a, b, M)
  local role = a.hero.role
  local hp = hp_frac(a)
  local st = STYLE[a.hero.id] or { range = 20 }
  if M.phase ~= "play" then return T_HOLD end
  if hp < 0.35 and b.nfoe > 0 and not (M.overtime and M.owner ~= a.team) then return T_RETREAT end
  if role == "support" and b.hurt and hp_frac(b.hurt) < 0.6 then return T_GUARD end
  if M.owner ~= a.team and (M.overtime or b.foes_on > 0 and b.foes_on <= b.friends_on + 1) then return T_POINT end
  if b.near_foe and b.near_d < st.range * 1.3 then return T_FIGHT end
  if role == "damage" and b.flanker and not on_point(a) then return T_FLANK end
  return T_POINT
end
Bots.teacher = teacher

-- ---------------------------------------------------------------- the hands

-- turn towards a point, at most `rate` radians a second; the angle left
local function aim_at(a, tx, ty, tz, rate)
  local dx, dy, dz = tx - a.x, ty - (a.y + a.eye), tz - a.z
  local want_yaw = atan(dx, dz)
  local want_pitch = atan(dy, sqrt(dx * dx + dz * dz))
  local dyaw = wrap_angle(want_yaw - a.yaw)
  local step = rate * DT
  a.yaw = wrap_angle(a.yaw + clamp(dyaw, -step, step))
  local dp = want_pitch - a.pitch
  a.pitch = clamp(a.pitch + clamp(dp, -step, step), -1.4, 1.4)
  return abs(dyaw) + abs(dp)
end

-- a hero's abilities: hero id -> function(a, b, c, ctx); ctx: target, its
-- distance, own health, foes seen, friends near, the tactic
local ready = function(a, key)
  for _, ab in ipairs(a.hero.hud) do
    if ab.key == key and ab.state then
      local cd = ab.state(a)
      return (cd or 0) <= 0
    end
  end
  return false
end

local ABIL = {
  rally = function(a, b, c, x)
    if a.form and a.form.name ~= "mech" then
      if a.ult >= 100 then c.ult_p = true end           -- Pit Stop
      return
    end
    if x.tgt and x.d > 10 and x.d < 26 and x.tactic == T_FIGHT and ready(a, "ab1") then c.ab1_p = true end
    if x.tgt and x.d < 25 and ready(a, "ab2") then c.ab2_p = true end
    if x.hurt_recent > 120 and x.tgt then c.fire2 = true end      -- Null Field while taking fire
    if a.ult >= 100 and x.foes_close >= 2 then c.ult_p = true end
  end,
  kaiju = function(a, b, c, x)
    if x.tgt and x.d > 5 and x.d < 14 and ready(a, "ab1") then c.ab1_p = true end
    if x.tgt and x.hurt_recent > 100 and x.friends > 0 then c.ab2 = true end
    if x.tgt and x.d > 6 and x.d < 25 then c.fire2 = true end       -- the repeater at range
    if a.ult >= 100 and x.foes_close >= 2 then c.ult_p = true end
  end,
  sarge = function(a, b, c, x)
    if not x.tgt and x.tactic == T_POINT and x.moving then c.ab1 = true end
    if x.hp < 0.6 and ready(a, "ab2") then c.ab2_p = true end
    if x.tgt and x.d < 35 and x.aimed and ready(a, "fire2") then c.fire2_p = true end
    if a.ult >= 100 and b.nfoe >= 2 then c.ult_p = true end
  end,
  frost = function(a, b, c, x)
    if x.hp < 0.3 and x.tgt and ready(a, "ab1") then c.ab1_p = true end
    if x.tgt and x.tactic == T_RETREAT and ready(a, "ab2") then c.ab2_p = true end
    if x.tgt and x.d > 9 and x.d < 40 and x.aimed then c.fire2_p = true end    -- icicles beyond the stream
    if a.ult >= 100 and x.foes_close >= 2 then c.ult_p = true end
  end,
  fuse = function(a, b, c, x)
    if x.tgt and x.d < 12 and ready(a, "ab1") and x.aimed then c.ab1_p = true b.mine_t = G.t end
    if b.mine_t and G.t - b.mine_t > 0.6 then c.fire2_p = true b.mine_t = nil end
    if x.tactic == T_RETREAT and ready(a, "ab2") then c.ab2_p = true end
    if a.ult >= 100 and b.nfoe >= 2 then c.ult_p = true end
  end,
  rail = function(a, b, c, x)
    if x.tactic == T_FLANK and x.moving and ready(a, "ab1") then c.ab1_p = true end
    if x.tgt and b.nfoe >= 2 and ready(a, "ab2") then c.ab2_p = true end
    if x.tgt and x.aimed and (a.st.energy or 0) >= 70 then c.fire2_p = true end
    if a.ult >= 100 and x.tgt then c.ult_p = true end
  end,
  orbit = function(a, b, c, x)
    if x.tactic == T_POINT and x.moving and not x.tgt and ready(a, "ab1") then c.ab1_p = true end
    if x.tgt and x.friends >= 2 and ready(a, "ab2") then c.ab2_p = true end
    if (x.tgt or b.hurt) and ready(a, "fire2") then c.fire2 = (G.t % 2) < 1.3 end   -- lock on, let go
    if a.ult >= 100 and on_point(a) and b.nfoe >= 1 then c.ult_p = true end
  end,
  akari = function(a, b, c, x)
    if x.hp < 0.3 and b.hurt and ready(a, "ab1") then c.ab1_p = true end
    if b.hurt and hp_frac(b.hurt) < 0.4 and ready(a, "ab2") then c.ab2_p = true end
    if a.ult >= 100 and b.nfoe >= 2 and x.friends >= 1 then c.ult_p = true end
  end,
}

-- ---------------------------------------------------------------- one bot, one frame

local fbuf = {}

function Bots.think(a, c, M)
  local b = a.brain
  if not b or b.deaths ~= a.deaths then
    b = { deaths = a.deaths, born = G.t, foes = {}, nfoe = 0, near_d = 99, friends = 0, friends_on = 0,
          foes_on = 0, err_ph = grandom() * 6, look_t = G.t + (a.id % 8) * 0.031,
          think_t = G.t + (a.id % 8) * 0.063, tactic = T_POINT, flanker = grandom() < 0.4,
          flank = grandom(3), last_hp = Actors.total(a), hurt_recent = 0, lx = a.x, lz = a.z, stuck_t = G.t }
    a.brain = b
  end
  local sk = SKILL[Bots.diff] or SKILL[2]
  local st = STYLE[a.hero.id] or { range = 20, keep = 10 }
  -- damage taken lately (decays over two seconds)
  local hp = Actors.total(a)
  b.hurt_recent = b.hurt_recent * (1 - DT / 2) + max(0, b.last_hp - hp)
  b.last_hp = hp
  if G.t >= b.look_t then
    b.look_t = G.t + 0.25
    look(a, b)
  end
  -- the tactic, twice a second
  if G.t >= b.think_t then
    b.think_t = G.t + 0.5
    local f = features(a, b, M, fbuf)
    local t = teacher(a, b, M)
    local net = Bots.net and (not Bots.net_team or Bots.net_team == a.team) and Bots.net
    if net and M.phase == "play" then t = net:pick(f) or t end
    if Bots.explore > 0 and M.phase == "play" and grandom() < Bots.explore then t = grandom(#TACTICS) end
    if t ~= b.tactic then b.node = nil end
    b.tactic = t
    if Bots.log and M.phase == "play" then
      local s = {}
      for i = 1, NF do s[i] = string.format("%.3f", f[i]) end
      -- and what the bot has done so far (the trainer takes the differences)
      log(string.format("ai d %.2f %d %d %d %s %d %d %d %d %.1f %.1f %.2f", G.t, a.id, a.team, t, table.concat(s, " "),
                        a.kills, a.deaths, floor(a.dealt or 0), floor(a.healed or 0), M.pct[a.team], M.pct[3 - a.team],
                        (M.owner == a.team and 1 or 0) + (M.cap_team == a.team and M.cap or M.cap_team ~= 0 and -M.cap or 0)))
    end
  end
  local tactic = b.tactic
  -- the target: the weakest in reach if any, else the nearest
  local tgt = b.near_foe
  if tgt and not tgt.alive then tgt = nil end
  if b.weak_foe and b.weak_foe.alive and len3(b.weak_foe.x - a.x, 0, b.weak_foe.z - a.z) < st.range then tgt = b.weak_foe end
  local d = tgt and len3(tgt.x - a.x, tgt.y - a.y, tgt.z - a.z) or 99
  -- a new target: the reaction time before the first shot
  if tgt ~= b.tgt then b.tgt, b.seen_t = tgt, G.t end
  -- where to go
  local gx, gz
  local function follow(fl)
    if not b.node or G.t >= (b.replan or 0) then
      b.node = nearest_node(a)
      b.replan = G.t + 2.5
    end
    local node = b.node
    if not node then return end
    if fl.goal[node] then
      gx, gz = NAV.x[node], NAV.z[node]
      return true
    end
    gx, gz = NAV.x[node], NAV.z[node]
    local dx, dz = gx - a.x, gz - a.z
    if dx * dx + dz * dz < 1.6 then
      local nxt = fl.next[node]
      -- before the point opens, wait at the edge of the square
      if nxt and (M.phase == "play" or fl ~= FLOW.point or (fl.dist[nxt] or 0) > 9) then b.node = nxt end
    end
  end
  if tactic == T_POINT or tactic == T_HOLD and M.phase ~= "play" then
    if follow(FLOW.point) or on_point(a) then
      -- on the point: move about on it
      if G.t >= (b.wander_t or 0) then
        b.wander_t = G.t + 1.5 + grandom() * 2
        local ang, r = grandom() * 2 * pi, 1.5 + grandom() * (P.r - 2)
        b.wx, b.wz = P.x + sin(ang) * r, P.z + cos(ang) * r
      end
      gx, gz = b.wx or P.x, b.wz or P.z
    end
  elseif tactic == T_FIGHT and tgt then
    -- close in to the hero's distance, not nearer
    if d > st.keep then gx, gz = tgt.x, tgt.z
    else gx, gz = a.x - (tgt.x - a.x), a.z - (tgt.z - a.z) end
  elseif tactic == T_FIGHT then
    follow(FLOW.point)
  elseif tactic == T_RETREAT then
    follow(FLOW.home[a.team])
  elseif tactic == T_FLANK then
    if follow(FLOW.flank[b.flank]) then b.tactic = T_POINT b.node = nil end
  elseif tactic == T_GUARD then
    local h = b.hurt
    if h and h.alive then
      local dx, dz = h.x - a.x, h.z - a.z
      if dx * dx + dz * dz > 16 then gx, gz = h.x, h.z end
    else
      follow(FLOW.point)
    end
  end
  -- stuck against something: jump, look for the way again
  if G.t >= b.stuck_t + 1.2 then
    local moved = (a.x - b.lx) ^ 2 + (a.z - b.lz) ^ 2
    if moved < 0.15 and gx and ((gx - a.x) ^ 2 + (gz - a.z) ^ 2) > 2 then
      c.jump, c.jump_p = true, true
      b.node = nil
    end
    b.lx, b.lz, b.stuck_t = a.x, a.z, G.t
  end
  -- aim: a support heals the hurt friend when no enemy is close, else the
  -- target, leading slow shots, with an error that wanders and grows with
  -- the distance; the first shot after the reaction time
  local aim, key, key_p = nil, nil, false
  if st.healer and b.hurt and b.hurt.alive and (not tgt or d > 12 or tactic == T_GUARD) then
    aim, key = b.hurt, st.healer
  elseif tgt then
    aim, key, key_p = tgt, st.attack or "fire", st.attack_p
  end
  local aimed = false
  if aim then
    local ad = len3(aim.x - a.x, aim.y - a.y, aim.z - a.z)
    local tx, ty, tz = aim.x, aim.y + aim.height * 0.62, aim.z
    if st.speed then
      local lead = ad / st.speed
      tx, ty, tz = tx + aim.vx * lead, ty + (st.lob and ad * ad * 0.012 or 0), tz + aim.vz * lead
    end
    local w = sk.err * ad * (0.6 + 0.4 * sin(G.t * 1.9 + b.err_ph))
    tx, tz = tx + w * cos(G.t * 2.7 + b.err_ph), tz + w * sin(G.t * 2.3 + b.err_ph)
    local left = aim_at(a, tx, ty, tz, sk.turn)
    aimed = left < 0.08 + 0.6 / max(4, ad)
    local can = aim ~= tgt or G.t - b.seen_t >= sk.react
    if aimed and can and ad < st.range * 1.15 then
      if key_p then
        -- a weapon shot by presses (kunai): press again and again
        c[key .. "_p"] = ((M.frame or 0) + a.id) % 12 == 0
        c[key] = c[key .. "_p"]
      else
        c[key], c[key .. "_p"] = true, not b.firing
        b.firing = true
      end
    else
      b.firing = false
    end
  elseif gx then
    aim_at(a, gx, a.y + a.eye, gz, sk.turn * 0.6)
    b.firing = false
  end
  -- moving, in the bot's own frame (right, forward); strafing in a fight
  local moving = false
  if gx then
    local dx, dz = gx - a.x, gz - a.z
    local dd = sqrt(dx * dx + dz * dz)
    if dd > 0.4 then
      dx, dz = dx / dd, dz / dd
      local fx, fz = sin(a.yaw), cos(a.yaw)
      local rx, rz = cos(a.yaw), -sin(a.yaw)
      c.mx, c.mz = dx * rx + dz * rz, dx * fx + dz * fz
      moving = true
    end
  end
  if tgt and d < st.range * 1.3 then
    c.mx = clamp(c.mx + sin(G.t * 1.7 + b.err_ph) * 0.7, -1, 1)
    if Bots.diff >= 3 and ((M.frame or 0) + a.id * 7) % 97 == 0 and a.on_ground then c.jump, c.jump_p = true, true end
  end
  -- the hero's abilities
  local ab = ABIL[a.hero.id]
  if ab then
    local close = 0
    for i = 1, b.nfoe do
      local o = b.foes[i]
      if o.alive and len3(o.x - a.x, 0, o.z - a.z) < 12 then close = close + 1 end
    end
    ab(a, b, c, { tgt = tgt, d = d, hp = hp_frac(a), aimed = aimed, friends = b.friends, foes_close = close,
                  tactic = tactic, moving = moving, hurt_recent = b.hurt_recent })
  end
end
