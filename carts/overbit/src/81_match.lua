-- The match: Control on Partenope (M31.3). Two teams of five, as in
-- Overwatch 2's role queue (1 tank, 2 damage, 2 support); the player is one
-- of the blue team, the other nine are bots. The point in the square
-- unlocks after a while; a team alone on it captures it (faster with more
-- of them), then its percentage climbs while it holds it; 100% wins the
-- round (not while the enemy is on the point: overtime), two rounds win
-- the match. Dead heroes come back in their spawn room after 10 s. In the
-- spawn room (or before the match) the hero can be changed: H on the
-- keyboard, down on the pad.

local Match = {}
Modes.list.match = Match

local RULES = {
  unlock = 20,          -- seconds before the point opens, every round
  cap = 8,              -- seconds to capture it alone (x1.5 with two, x2 with three or more)
  pct = 0.9,            -- seconds for each 1% of the team that holds it
  decay = 0.12,         -- the capture going back each second when nobody is on it
  respawn = 10,
  round_end = 6,
  match_end = 9,
  rounds = 2,           -- rounds to win the match
}
Match.rules = RULES

local ROLE_SLOTS = { tank = 1, damage = 2, support = 2 }
local ROLE_ORDER = { "tank", "damage", "support" }
local ROLE_NAME = { tank = "TANK", damage = "DAMAGE", support = "SUPPORT" }

local M = {}            -- the state of the match in course
Match.state = M

-- ---------------------------------------------------------------- teams

local function by_role(role)
  local out = {}
  for _, id in ipairs(HERO_ORDER) do if H[id].role == role then out[#out + 1] = id end end
  return out
end

-- the heroes of a team: `fixed` (a hero id or nil) first, then the slots
-- left by role, at random among the heroes not taken yet
local function pick_team(fixed)
  local ids, used, need = {}, {}, {}
  for r, n in pairs(ROLE_SLOTS) do need[r] = n end
  if fixed then
    ids[1], used[fixed] = fixed, true
    need[H[fixed].role] = need[H[fixed].role] - 1
  end
  for _, r in ipairs(ROLE_ORDER) do
    local pool = by_role(r)
    for _ = 1, need[r] do
      local free = {}
      for _, id in ipairs(pool) do if not used[id] then free[#free + 1] = id end end
      if #free == 0 then free = pool end
      local id = free[math.random(#free)]
      ids[#ids + 1], used[id] = id, true
    end
  end
  return ids
end

-- the five places of a team in its spawn room
local function spawn_spot(team, slot)
  local s = World.mark(team == 1 and "spawn1" or "spawn2")
  local dz = ({ 0, -2.4, 2.4, -4.6, 4.6 })[slot] or 0
  local dx = team == 1 and -1.2 or 1.2
  return s.x + (slot == 1 and 0 or dx), s.z + dz, s.yaw
end

-- a hero in place of another (the same seat: team, name, home, score)
local function swap_hero(a, id)
  local b = Actors.spawn(id, a.team, a.x, a.y, a.z, a.yaw, { name = a.name, bot = a.bot, respawn = RULES.respawn })
  -- Actors.spawn put it at the end: into the old one's place
  local list = G.actors
  table.remove(list, #list)
  for i, o in ipairs(list) do if o == a then list[i] = b break end end
  b.home, b.kills, b.deaths, b.script, b.slot = a.home, a.kills, a.deaths, a.script, a.slot
  b.pitch = a.pitch
  if not a.alive then b.alive, b.dead_t = false, a.dead_t end
  if G.local_actor == a then G.local_actor = b end
  return b
end

-- the player takes hero `id`: the team keeps one hero of each kind (a bot
-- of the new role takes the old hero)
function Match.choose(id)
  local me = G.local_actor
  if me.hero.id == id then return end
  local old = me.hero.id
  local swap
  for _, o in ipairs(G.actors) do
    if o ~= me and o.team == me.team and o.hero.id == id then swap = o end
  end
  if not swap and H[id].role ~= H[old].role then
    for _, o in ipairs(G.actors) do
      if o ~= me and o.team == me.team and o.hero.role == H[id].role then swap = o break end
    end
  end
  if swap then swap_hero(swap, old) end
  me = swap_hero(me, id)
  me.ult = 0
  log(string.format("overbit hero %s", id))
end

-- ---------------------------------------------------------------- the point

local P                 -- the mark: x, y, z, r

-- who stands on the point: counts by team
local function on_point()
  local n1, n2 = 0, 0
  for _, a in ipairs(G.actors) do
    if a.alive then
      local dx, dz = a.x - P.x, a.z - P.z
      if dx * dx + dz * dz <= P.r * P.r and a.y < P.y + 3 then
        if a.team == 1 then n1 = n1 + 1 else n2 = n2 + 1 end
      end
    end
  end
  return n1, n2
end

local function cap_rate(n) return (n >= 3 and 2 or n == 2 and 1.5 or 1) / RULES.cap end

local function point_update()
  local n1, n2 = on_point()
  M.n1, M.n2 = n1, n2
  local contested = n1 > 0 and n2 > 0
  M.contested = contested
  local alone = (n1 > 0 and n2 == 0) and 1 or (n2 > 0 and n1 == 0) and 2 or 0
  -- capturing: a team alone on a point that is not its own
  if alone ~= 0 and alone ~= M.owner then
    if M.cap_team ~= alone and M.cap > 0 then
      M.cap = max(0, M.cap - cap_rate(alone == 1 and n1 or n2) * DT)      -- first the other side's progress
    else
      M.cap_team = alone
      M.cap = M.cap + cap_rate(alone == 1 and n1 or n2) * DT
      if M.cap >= 1 then
        M.owner, M.cap, M.cap_team = alone, 0, 0
        Snd.play(alone == G.local_actor.team and "capture" or "lost")
        log(string.format("overbit point team %d", alone))
        M.msg, M.msg_t = alone == G.local_actor.team and "POINT CAPTURED" or "POINT LOST", 2.5
      end
    end
  elseif alone == 0 and not contested and M.cap > 0 then
    M.cap = max(0, M.cap - RULES.decay * DT)
  end
  -- the owner's percentage; 100 only when no enemy is on it or capturing
  M.overtime = false
  if M.owner ~= 0 then
    local o = M.owner
    local enemy_on = (o == 1 and n2 or n1) > 0
    local p = M.pct[o] + DT / RULES.pct
    if p >= 99 and (enemy_on or (M.cap > 0 and M.cap_team ~= o)) then
      p = 99
      M.overtime = true
    end
    M.pct[o] = min(100, p)
    if M.pct[o] >= 100 then Match.round_won(o) end
  end
end

function Match.round_won(team)
  M.wins[team] = M.wins[team] + 1
  M.phase, M.t = "round_end", 0
  M.round_winner = team
  log(string.format("overbit round %d team %d (%d-%d)", M.round, team, M.wins[1], M.wins[2]))
  if M.wins[team] >= RULES.rounds then
    M.phase = "match_end"
    log(string.format("overbit match team %d", team))
  end
  Snd.play(team == G.local_actor.team and "win" or "lose")
end

-- the ring of the point on the ground: the owner's colour, the capture
-- growing round it in the colour of the team that captures
local RING_N = 24
local function draw_point()
  if not P then return end
  local y = P.y + 0.04
  local col = M.owner ~= 0 and TEAM_RGB[M.owner] or (M.phase == "setup" and 0x8A8F98 or 0xF2F2F2)
  local r = P.r
  local cap_n = M.cap_team ~= 0 and floor(M.cap * RING_N + 0.5) or 0
  for i = 0, RING_N - 1 do
    local a0, a1 = i / RING_N * 2 * pi, (i + 1) / RING_N * 2 * pi
    local c = i < cap_n and TEAM_RGB[M.cap_team] or col
    line3d(P.x + sin(a0) * r, y, P.z + cos(a0) * r, P.x + sin(a1) * r, y, P.z + cos(a1) * r, c, 2)
  end
end

-- ---------------------------------------------------------------- the bots (stand-ins until M31.4)

local NAV                -- { n, x = {}, y = {}, z = {}, links = {}, to_point = {}, goal = {} }

local function nav_build()
  local src = World.MAP and World.MAP.nav
  if not src then return nil end
  local nv = { x = {}, y = {}, z = {}, links = src.links, names = src.names, goal = {}, to_point = {} }
  local n = #src.nodes // 3
  nv.n = n
  for i = 1, n do
    nv.x[i], nv.y[i], nv.z[i] = src.nodes[i * 3 - 2], src.nodes[i * 3 - 1], src.nodes[i * 3]
  end
  -- the way to the point from every node: distances from the point's nodes
  -- along the links backwards (Dijkstra on 60 nodes: a moment, once)
  local back = {}
  for i = 1, n do back[i] = {} end
  for i = 1, n do for _, j in ipairs(src.links[i]) do back[j][#back[j] + 1] = i end end
  local dist, done = {}, {}
  for i = 1, n do
    if src.names[i]:sub(1, 2) == "p_" then dist[i] = 0 nv.goal[i] = true end
  end
  while true do
    local best, bd = nil, math.huge
    for i = 1, n do if dist[i] and not done[i] and dist[i] < bd then best, bd = i, dist[i] end end
    if not best then break end
    done[best] = true
    for _, i in ipairs(back[best]) do
      local d = bd + len3(nv.x[i] - nv.x[best], nv.y[i] - nv.y[best], nv.z[i] - nv.z[best])
      if not dist[i] or d < dist[i] then dist[i] = d nv.to_point[i] = best end
    end
  end
  nv.dist = dist
  return nv
end

-- the nearest node a bot can walk to in a straight line
local function nav_nearest(a)
  local best, bd = nil, math.huge
  local ey = a.y + 1.0
  for i = 1, NAV.n do
    local dx, dz = NAV.x[i] - a.x, NAV.z[i] - a.z
    local d = dx * dx + dz * dz + (NAV.y[i] - a.y) ^ 2 * 4
    if d < bd and d < 400 and World.clear(a.x, ey, a.z, NAV.x[i], NAV.y[i] + 1.0, NAV.z[i]) then best, bd = i, d end
  end
  return best
end

-- how far each hero likes to fight, and the button of its damage
local STYLE = {
  rally = { range = 14 }, kaiju = { range = 4.5 }, sarge = { range = 28 }, frost = { range = 9 },
  fuse = { range = 18, lob = 0.08 }, rail = { range = 34 }, orbit = { range = 24, healer = "fire" },
  akari = { range = 20, healer = "fire", attack = "fire2" },
}

local function sees(a, o)
  return World.clear(a.x, a.y + a.eye, a.z, o.x, o.y + o.height * 0.6, o.z)
end

-- turn towards a point, at most `rate` radians a second
local function aim_at(a, tx, ty, tz, rate)
  local dx, dy, dz = tx - a.x, ty - (a.y + a.eye), tz - a.z
  local want_yaw = atan(dx, dz)
  local want_pitch = atan(dy, sqrt(dx * dx + dz * dz))
  local dyaw = wrap_angle(want_yaw - a.yaw)
  local step = rate * DT
  a.yaw = wrap_angle(a.yaw + clamp(dyaw, -step, step))
  a.pitch = a.pitch + clamp(want_pitch - a.pitch, -step, step)
  return abs(dyaw) + abs(want_pitch - a.pitch)
end

-- the bot's command for this frame
local function bot_think(a, c)
  local b = a.brain
  if not b or b.deaths ~= a.deaths then
    -- a new life (or the first): a new plan
    b = { node = nil, t = 0, lx = a.x, lz = a.z, wander_t = 0, err = math.random() * 6, deaths = a.deaths }
    a.brain = b
  end
  local st = STYLE[a.hero.id] or { range = 20 }
  b.t = b.t + DT
  -- the target: the nearest enemy it can see (looked for 4 times a second)
  if b.t >= (b.next_look or 0) then
    b.next_look = b.t + 0.25
    local best, bd = nil, 40 * 40
    for _, o in ipairs(G.actors) do
      if o.alive and o.team ~= a.team then
        local dx, dy, dz = o.x - a.x, o.y - a.y, o.z - a.z
        local d = dx * dx + dy * dy + dz * dz
        if d < bd and sees(a, o) then best, bd = o, d end
      end
    end
    b.target = best
    -- a support looks for a hurt friend first
    b.patient = nil
    if st.healer then
      local worst, wk = nil, 0.8
      for _, o in ipairs(G.actors) do
        if o ~= a and o.alive and o.team == a.team then
          local k = Actors.total(o) / Actors.total_max(o)
          local dx, dz = o.x - a.x, o.z - a.z
          if k < wk and dx * dx + dz * dz < 25 * 25 and sees(a, o) then worst, wk = o, k end
        end
      end
      b.patient = worst
    end
  end
  local tgt = b.target
  if tgt and not tgt.alive then tgt, b.target = nil, nil end
  -- where to go: the point (in the setup, its edge), along the nodes
  local gx, gz
  local play = M.phase == "play"
  if not b.node or b.t >= (b.replan or 0) then
    b.node = nav_nearest(a)
    b.replan = b.t + 3
  end
  local node = b.node
  if node then
    if NAV.goal[node] then
      -- on the point: move about on it
      if b.t >= b.wander_t then
        b.wander_t = b.t + 1.5 + math.random() * 2
        local ang, r = math.random() * 2 * pi, 1.5 + math.random() * (P.r - 2)
        b.wx, b.wz = P.x + sin(ang) * r, P.z + cos(ang) * r
      end
      gx, gz = b.wx or P.x, b.wz or P.z
    else
      gx, gz = NAV.x[node], NAV.z[node]
      local dx, dz = gx - a.x, gz - a.z
      if dx * dx + dz * dz < 1.6 then
        local nxt = NAV.to_point[node]
        -- before the point opens, wait at the edge of the square
        if nxt and (play or (NAV.dist[nxt] or 0) > 9) then b.node = nxt end
      end
    end
  end
  -- stuck against something: jump, look for a node again
  if b.t >= (b.stuck_check or 0) then
    b.stuck_check = b.t + 1.2
    local moved = (a.x - b.lx) ^ 2 + (a.z - b.lz) ^ 2
    if moved < 0.15 and gx and ((gx - a.x) ^ 2 + (gz - a.z) ^ 2) > 2 then
      c.jump, c.jump_p = true, true
      b.node = nil
    end
    b.lx, b.lz = a.x, a.z
  end
  -- fighting: face the target and fire within the hero's reach; a support
  -- heals the friend who needs it when no enemy is close
  local aim, fire_key
  if b.patient and (not tgt or len3(tgt.x - a.x, 0, tgt.z - a.z) > 12) then
    aim, fire_key = b.patient, st.healer
  elseif tgt then
    aim, fire_key = tgt, st.attack or "fire"
  end
  if aim then
    -- the aim wanders a little (bots are not perfect)
    local wob = 0.35 * sin(b.t * 2.3 + b.err)
    local err = aim_at(a, aim.x + wob * 0.5, aim.y + aim.height * 0.62 + (st.lob or 0) * len3(aim.x - a.x, 0, aim.z - a.z),
                       aim.z - wob * 0.5, 5.5)
    local d = len3(aim.x - a.x, aim.y - a.y, aim.z - a.z)
    if err < 0.12 and d < st.range then c[fire_key], c[fire_key .. "_p"] = true, not b.firing b.firing = true
    else b.firing = false end
    -- abilities now and then while fighting, the ultimate when charged and close
    if aim == tgt and b.t >= (b.next_ab or 0) then
      b.next_ab = b.t + 1 + math.random() * 2
      local r = math.random()
      if r < 0.3 then c.ab1, c.ab1_p = true, true elseif r < 0.55 then c.ab2, c.ab2_p = true, true end
      if a.ult >= 100 and d < 14 then c.ult, c.ult_p = true, true end
    end
  elseif gx then
    aim_at(a, gx, a.y + a.eye, gz, 3.5)
    b.firing = false
  end
  -- moving towards the goal, in the bot's own frame (right, forward); a
  -- little strafing in a fight
  if gx then
    local dx, dz = gx - a.x, gz - a.z
    local d = sqrt(dx * dx + dz * dz)
    if d > 0.4 then
      dx, dz = dx / d, dz / d
      local fx, fz = sin(a.yaw), cos(a.yaw)
      local rx, rz = cos(a.yaw), -sin(a.yaw)
      c.mx, c.mz = dx * rx + dz * rz, dx * fx + dz * fz
    end
    if tgt then c.mx = clamp(c.mx + sin(b.t * 1.7 + b.err) * 0.6, -1, 1) end
  end
end
Match.bot_think = bot_think

-- ---------------------------------------------------------------- the match

local function team_setup(team, ids)
  for slot, id in ipairs(ids) do
    local x, z, yaw = spawn_spot(team, slot)
    local me = team == 1 and slot == 1 and not Match.spectate
    local a = Actors.spawn(id, team, x, 0, z, yaw, { name = me and "You" or H[id].name, bot = not me,
                                                    respawn = RULES.respawn })
    a.home, a.slot = { x = x, y = 0, z = z, yaw = yaw }, slot
    if me then G.local_actor = a else a.script = bot_think end
    if team == 1 and slot == 1 and Match.spectate then G.local_actor = a end
  end
end

local function round_start()
  M.phase, M.t = "setup", 0
  M.owner, M.cap, M.cap_team, M.pct = 0, 0, 0, { 0, 0 }
  M.overtime, M.contested = false, false
  Proj.clear()
  Fx.clear()
  for _, a in ipairs(G.actors) do
    local ult = a.ult
    Actors.respawn(a)
    a.alive, a.dead_t, a.ult = true, 0, ult
    a.anim.base, a.anim.bt = "idle", 0
    a.brain = nil
  end
  log(string.format("overbit round %d", M.round))
end

function Match.start()
  -- quicker rules for the tests (build.py --define OVERBIT_RULES={...}); a
  -- match of ten bots filmed (OVERBIT_SPECTATE: the reel of the match)
  if OVERBIT_RULES then for k, v in pairs(OVERBIT_RULES) do RULES[k] = v end end
  Match.spectate = OVERBIT_SPECTATE and true or false
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
  World.use("map")
  P = World.mark("point")
  NAV = NAV or nav_build()
  M.round, M.wins = 1, { 0, 0 }
  team_setup(1, pick_team(not Match.spectate and G.hero_id or nil))
  team_setup(2, pick_team(nil))
  round_start()
  if OVERBIT_PROFILE then
    -- profiling (tools/armprof.py): the two teams already face to face at the point
    for i, a in ipairs(G.actors) do
      a.x, a.z = P.x + (a.team == 1 and -9 or 9), P.z + (i % 5 - 2) * 2.5
      a.yaw = a.team == 1 and pi / 2 or -pi / 2
    end
  end
  M.select = { open = not Match.spectate, sel = 1, can_close = false }
  M.shot_t, M.shot = 0, 0
  for i, id in ipairs(HERO_ORDER) do if id == G.hero_id then M.select.sel = i end end
  G.dmg_numbers = false
  log(string.format("overbit match start: %s", G.hero_id))
end

-- in its spawn room? (the hero can be changed there)
local function in_spawn(a)
  local s = World.mark(a.team == 1 and "spawn1" or "spawn2")
  return abs(a.x - s.x) < 4.6 and abs(a.z - s.z) < 7.5
end
Match.in_spawn = in_spawn

local function select_update()
  local S = M.select
  local c = Input.cmd
  local n = #HERO_ORDER
  if c.up_p or c.left_p then S.sel = (S.sel - 2) % n + 1 Snd.play("ui") end
  if c.down_p or c.right_p then S.sel = S.sel % n + 1 Snd.play("ui") end
  if c.jump_p or c.fire_p then
    Match.choose(HERO_ORDER[S.sel])
    G.hero_id = HERO_ORDER[S.sel]
    S.open = false
    Snd.play("ui")
  elseif (c.menu_p and S.can_close) or c.crouch_p then
    S.open = false
    Snd.play("ui_back")
  end
end

function Match.update()
  M.t = M.t + DT
  if M.msg_t then M.msg_t = M.msg_t - DT if M.msg_t <= 0 then M.msg = nil M.msg_t = nil end end
  local me = G.local_actor
  local c = Input.cmd
  local menu = c.menu_p
  if M.select.open then
    select_update()
    -- the hero waits while the player chooses (the menu button only closes)
    Input.blank(c)
    menu = false
  elseif (c.hero_p or (c.pad and c.down_p)) and (in_spawn(me) or not me.alive) and M.phase ~= "match_end" then
    M.select.open, M.select.can_close = true, true
    for i, id in ipairs(HERO_ORDER) do if id == me.hero.id then M.select.sel = i end end
  end
  if M.phase == "setup" then
    local left = RULES.unlock - M.t
    if left <= 5 and math.ceil(left) ~= math.ceil(left + DT) then Snd.play("tick") end
    if M.t >= RULES.unlock then
      M.phase, M.t = "play", 0
      M.msg, M.msg_t = "THE POINT IS OPEN", 2.5
      log("overbit point open")
      Snd.play("open")
    end
  elseif M.phase == "play" then
    point_update()
  elseif M.phase == "round_end" then
    if M.t >= RULES.round_end then
      M.round = M.round + 1
      round_start()
    end
  elseif M.phase == "match_end" then
    if M.t >= RULES.match_end or (M.t > 2 and menu) then
      Modes.start("menu")
      return
    end
  end
  Modes.update_actors()
  if menu and M.phase ~= "match_end" then Modes.start("menu") end
end

-- ---------------------------------------------------------------- drawing

local INK = 0x101418

local function hud_top()
  font("6x12")
  local cx = 160
  -- the two percentages, the point between them
  for team = 1, 2 do
    local x = team == 1 and cx - 62 or cx + 18
    rectfill(x, 4, 44, 16, INK)
    rect(x, 4, 44, 16, TEAM_RGB[team])
    local s = floor(M.pct[team]) .. "%"
    print(s, x + 22 - #s * 3, 6, team == M.owner and 0xFFFFFF or 0xB8BEC8)
    -- the rounds won
    for i = 1, RULES.rounds do
      local px = team == 1 and x + 44 - i * 7 or x + (i - 1) * 7
      rectfill(px + 1, 22, 5, 3, i <= M.wins[team] and TEAM_RGB[team] or 0x3A4048)
    end
  end
  local col = M.owner ~= 0 and TEAM_RGB[M.owner] or 0xD8DCE2
  circfill(cx, 12, 9, INK)
  circ(cx, 12, 9, col)
  if M.cap_team ~= 0 and M.cap > 0 then
    -- the capture as an arc round the point
    local n = floor(M.cap * 24)
    for i = 0, n - 1 do
      local a0, a1 = i / 24 * 2 * pi, (i + 1) / 24 * 2 * pi
      line(floor(cx + sin(a0) * 9), floor(12 - cos(a0) * 9), floor(cx + sin(a1) * 9), floor(12 - cos(a1) * 9),
           TEAM_RGB[M.cap_team])
      line(floor(cx + sin(a0) * 8), floor(12 - cos(a0) * 8), floor(cx + sin(a1) * 8), floor(12 - cos(a1) * 8),
           TEAM_RGB[M.cap_team])
    end
  end
  print("A", cx - 2, 6, col)
  -- the line under it
  local s, sc
  if M.phase == "setup" then
    s = "THE POINT OPENS IN " .. math.ceil(RULES.unlock - M.t)
  elseif M.phase == "play" then
    if M.overtime then s, sc = "OVERTIME", 0xFFB43C
    elseif M.contested then s, sc = "CONTESTED", 0xFFB43C
    elseif M.cap_team ~= 0 and M.cap > 0 then
      s, sc = (M.cap_team == G.local_actor.team and "CAPTURING " or "THE ENEMY IS CAPTURING ") .. floor(M.cap * 100) .. "%",
              TEAM_RGB[M.cap_team]
    end
  end
  if M.msg then s, sc = M.msg, 0xFFE070 end
  if s then
    rectfill(cx - #s * 3 - 3, 27, #s * 6 + 6, 13, INK)
    print(s, cx - #s * 3, 28, sc or 0xFFFFFF)
  end
  font()
end

local function big_text(s, rgb, y)
  font("8x16")
  print(s, 160 - #s * 8 + 1, y + 1, INK, 2)
  print(s, 160 - #s * 8, y, rgb, 2)
  font()
end

-- the end of the match: the result, then everyone's kills and deaths
local function scoreboard()
  rectfill(40, 66, 240, 100, INK)
  font("6x12")
  for team = 1, 2 do
    local x = team == 1 and 46 or 166
    print(team == 1 and "BLUE" or "RED", x, 70, TEAM_RGB[team])
    local y = 84
    for _, a in ipairs(G.actors) do
      if a.team == team then
        print(a.name, x, y, a == G.local_actor and 0xFFE070 or 0xD8DCE2)
        local k = a.kills .. "/" .. a.deaths
        print(k, x + 108 - #k * 6, y, 0xB8BEC8)
        y = y + 13
      end
    end
  end
  font()
end

-- the reel of the match: a camera that changes every few seconds, round
-- the point from above, behind a hero, in a hero's eyes
function Match.film()
  M.shot_t = M.shot_t - DT
  local who = M.film_actor
  if M.shot_t <= 0 or not who or not who.alive then
    M.shot = M.shot % 3 + 1
    M.shot_t = 5
    -- the hero nearest the point, alive
    local best, bd = nil, math.huge
    for _, a in ipairs(G.actors) do
      if a.alive then
        local d = (a.x - P.x) ^ 2 + (a.z - P.z) ^ 2 + math.random() * 60
        if d < bd then best, bd = a, d end
      end
    end
    M.film_actor, who = best, best
    log(string.format("overbit film %d %s", M.shot, best and best.name or "-"))
  end
  if M.shot == 1 or not who then
    local ang = G.t * 0.12
    Cam.orbit(P.x, 1.5, P.z, ang, -0.38, 17, 70)
    Modes.draw_scene(nil, draw_point)
  elseif M.shot == 2 then
    Cam.orbit(who.x, who.y + who.height * 0.8, who.z, who.yaw, -0.18, 4.2, 70)
    Modes.draw_scene(nil, draw_point)
  else
    Cam.first(who)
    Modes.draw_scene(who, draw_point)
    if who.hero.draw_fp_extra then who.hero.draw_fp_extra(who, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch) end
    who.hero.draw_fp(who, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch, Cam.roll)
  end
  Fx.draw2d()
  hud_top()
  if who and M.shot ~= 1 then
    font("6x12")
    print(who.name:upper(), 6, 166, TEAM_RGB[who.team])
    font()
  end
  if M.phase == "round_end" or M.phase == "match_end" then
    local team = M.round_winner == 1 and "BLUE" or "RED"
    big_text(M.phase == "match_end" and team .. " WINS" or "ROUND TO " .. team, TEAM_RGB[M.round_winner], 60)
  end
end

-- the hero select: the roster by role on the left, the chosen hero's
-- abilities on the right, the team underneath
local FAKE = { st = {}, fx = {}, form = {}, alive = true }
local function select_draw()
  rectfill(0, 0, 320, 180, 0x0A0E14)
  font("6x12")
  print("CHOOSE YOUR HERO", 8, 4, 0xFFE070)
  local y = 20
  local sel = HERO_ORDER[M.select.sel]
  for _, r in ipairs(ROLE_ORDER) do
    print(ROLE_NAME[r], 8, y, 0x7A8290)
    y = y + 12
    for i, id in ipairs(HERO_ORDER) do
      if H[id].role == r then
        local h = H[id]
        if id == sel then rectfill(6, y - 1, 96, 13, h.rgb) end
        rectfill(10, y + 2, 6, 6, h.rgb)
        print(h.name:upper(), 20, y, id == sel and INK or 0xD8DCE2)
        y = y + 13
      end
    end
    y = y + 3
  end
  local h = H[sel]
  font("8x16")
  print(h.name:upper(), 120, 18, h.rgb)
  font("6x12")
  print(ROLE_NAME[h.role], 120 + #h.name * 8 + 8, 22, 0x7A8290)
  -- the line about the hero, cut at the words
  local line, yy = "", 38
  for w in h.desc:gmatch("%S+") do
    if #line + #w + 1 > 32 then print(line, 120, yy, 0xB8BEC8) yy, line = yy + 12, "" end
    line = line == "" and w or line .. " " .. w
  end
  print(line, 120, yy, 0xB8BEC8)
  yy = yy + 18
  for _, ab in ipairs(h.hud) do
    if ab.name then
      local key = Hud.key(ab.key)
      local x = key and prompt(key, 120, yy, true) or 120
      print(ab.name, max(x + 4, 146), yy, 0xD8DCE2)
      yy = yy + 14
    end
  end
  local ok, un = pcall(h.ult_name, FAKE)
  if ok and un then
    local x = prompt(Hud.key("ult"), 120, yy, true)
    print(un, max(x + 4, 146), yy, 0xFFE070)
  end
  -- the team
  print("YOUR TEAM", 120, 136, 0x7A8290)
  local x = 120
  for _, a in ipairs(G.actors) do
    if a.team == G.local_actor.team then
      local nm = a == G.local_actor and h.short or a.hero.short
      print(nm, x, 150, a == G.local_actor and 0xFFE070 or a.hero.rgb)
      x = x + 30
    end
  end
  local px = prompt(Input.cmd.pad and "UPDOWN" or "up", 8, 165, true)
  print("HERO", px + 3, 165, 0x7A8290)
  px = prompt(Input.cmd.pad and "A" or "space", px + 36, 165, true)
  print("PLAY", px + 3, 165, 0x7A8290)
  font()
end

function Match.draw()
  local me = G.local_actor
  if M.select.open then
    select_draw()
    return
  end
  if Dev.draw_cam(draw_point) then
    hud_top()
    return
  end
  if Match.spectate then
    Match.film()
    return
  end
  if me.alive and me.hero.camera and me.hero.camera(me) then
    Modes.draw_scene(nil, draw_point)
  elseif me.alive then
    Cam.first(me)
    Modes.draw_scene(me, draw_point)
    if me.hero.draw_fp_extra then me.hero.draw_fp_extra(me, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch) end
    me.hero.draw_fp(me, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch, Cam.roll)
  else
    Cam.death(me)
    Modes.draw_scene(nil, draw_point)
  end
  Fx.draw2d()
  Hud.draw(me)
  hud_top()
  if (in_spawn(me) or not me.alive) and M.phase ~= "match_end" then
    font("6x12")
    local x = prompt(Input.cmd.pad and "DOWN" or "h", 8, 42, true)
    print("CHANGE HERO", x + 3, 42, 0xD8DCE2)
    font()
  end
  if M.phase == "round_end" then
    big_text(M.round_winner == me.team and "ROUND WON" or "ROUND LOST", M.round_winner == me.team and 0x46B4FF or 0xFF4646, 60)
  elseif M.phase == "match_end" then
    big_text(M.round_winner == me.team and "VICTORY" or "DEFEAT", M.round_winner == me.team and 0xFFE070 or 0xFF4646, 30)
    if M.t > 2 then scoreboard() end
  end
end
