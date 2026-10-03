-- The match: Control on Partenope (M38.3). Two teams of five, as in
-- Overwatch 2's role queue (1 tank, 2 damage, 2 support); the player is one
-- of the blue team, the other nine are bots (75_bots). The point in the square
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
      local id = free[grandom(#free)]
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
  b.seat, b.human, b.net_cmd = a.seat, a.human, a.net_cmd
  b.pitch = a.pitch
  if not a.alive then b.alive, b.dead_t = false, a.dead_t end
  if G.local_actor == a then G.local_actor = b end
  return b
end

-- a player (`a`: the local one, or a seat of a match on the network) takes
-- hero `id`: the team keeps one hero of each kind (a bot of the new role
-- takes the old hero; a hero another person plays is not taken)
function Match.choose(id, a)
  local me = a or G.local_actor
  if me.hero.id == id then return end
  local old = me.hero.id
  local swap
  for _, o in ipairs(G.actors) do
    if o ~= me and o.team == me.team and o.hero.id == id then
      if o.human then return end
      swap = o
    end
  end
  if not swap and H[id].role ~= H[old].role then
    for _, o in ipairs(G.actors) do
      if o ~= me and not o.human and o.team == me.team and o.hero.role == H[id].role then swap = o break end
    end
  end
  if swap then swap_hero(swap, old) end
  me = swap_hero(me, id)
  me.ult = 0
  log(string.format("overbit hero %s %s", me.name, id))
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
  local x0, z0 = P.x + sin(0) * r, P.z + cos(0) * r        -- each corner once
  for i = 0, RING_N - 1 do
    local a1 = (i + 1) / RING_N * 2 * pi
    local x1, z1 = P.x + sin(a1) * r, P.z + cos(a1) * r
    local c = i < cap_n and TEAM_RGB[M.cap_team] or col
    line3d(x0, y, z0, x1, y, z1, c, 2)
    x0, z0 = x1, z1
  end
end

-- ---------------------------------------------------------------- the match

-- the bots' command each frame (75_bots)
local function bot_think(a, c) Bots.think(a, c, M) end
Match.bot_think = bot_think

-- a person's command on the network: what came in the bundle (83_net)
local function human_think(a, c)
  local n = a.net_cmd
  if n then for k, v in pairs(n) do c[k] = v end end
end

-- the seats: team 1 has 1-5, team 2 has 6-10; alone, the player is seat 1;
-- on the network the people sit where M.net.seats says
local function team_setup(team, ids)
  for slot, id in ipairs(ids) do
    local x, z, yaw = spawn_spot(team, slot)
    local seat = (team - 1) * 5 + slot
    local human, me
    if M.net then
      for _, s in ipairs(M.net.seats) do if s == seat then human = true end end
      me = seat == M.net.seat
    else
      me = seat == 1 and not Match.spectate
      human = me
    end
    local name = me and "You" or human and ("P" .. seat) or H[id].name
    local a = Actors.spawn(id, team, x, 0, z, yaw, { name = name, bot = not human, respawn = RULES.respawn })
    a.home, a.slot, a.seat, a.human = { x = x, y = 0, z = z, yaw = yaw }, slot, seat, human
    if not human then a.script = bot_think elseif M.net then a.script = human_think end
    if me or (seat == 1 and Match.spectate) then G.local_actor = a end
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
  Match.spectate = (OVERBIT_SPECTATE or Match.bench) and true or false
  -- a match on the network (the lobby of 83_net): the same seed, bots and
  -- seats on every console; the clock and the ids start from zero
  M.net = Match.net
  Match.net = nil
  if M.net then grandom_seed(M.net.seed) end
  G.t, M.frame = 0, 0
  Actors.reset_ids()
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
  World.use("map")
  P = World.mark("point")
  Bots.init()
  -- training and tests (art/brain.py): the bots' log, chance, their network
  Bots.log = OVERBIT_AI_LOG and true or false
  Bots.explore = OVERBIT_AI_EXPLORE or 0
  local brain = OVERBIT_AI_NET or Bots.BRAIN
  Bots.net = brain and nnet(brain) or nil
  Bots.net_team = OVERBIT_AI_NET_TEAM
  Bots.diff = M.net and M.net.diff or G.bot_diff or 2
  if M.net then Bots.log, Bots.explore, Bots.net_team = false, 0, nil end
  M.round, M.wins = 1, { 0, 0 }
  M.end_t, M.lost_t, M.waiting = 0, 0, false
  team_setup(1, pick_team(not Match.spectate and not M.net and G.hero_id or nil))
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
  local mine = M.net and G.local_actor.hero.id or G.hero_id
  for i, id in ipairs(HERO_ORDER) do if id == mine then M.select.sel = i end end
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
    if M.net then Net.hero_req = S.sel                -- for everyone, when its frame runs
    else Match.choose(HERO_ORDER[S.sel]) end
    G.hero_id = HERO_ORDER[S.sel]
    S.open = false
    Snd.play("ui")
  elseif (c.menu_p and S.can_close) or c.crouch_p then
    S.open = false
    Snd.play("ui_back")
  end
end

-- one frame of the match: the same on every console of a network match
local function step()
  M.frame = M.frame + 1
  M.t = M.t + DT
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
  end
  Modes.update_actors()
  -- the spawn rooms heal their team quickly (as in Overwatch)
  for _, a in ipairs(G.actors) do
    if a.alive and in_spawn(a) and Actors.total(a) < Actors.total_max(a) then Actors.heal(a, 120 * DT) end
  end
end

Match.step = step

-- a frame of a network match, when the bundle of everyone's inputs is
-- there (at most two a console frame: one behind catches up)
local by_seat = {}
local function net_step(me, c)
  local n = 0
  while n < 2 and Net.tick(me, c) do
    for k in pairs(by_seat) do by_seat[k] = nil end
    for _, a in ipairs(G.actors) do by_seat[a.seat] = a end
    local asks, gone = Net.apply(by_seat)
    for _, s in ipairs(gone) do
      local a = by_seat[s]
      a.human, a.script, a.name = false, bot_think, a.hero.name
      log(string.format("overbit net seat %d to a bot", s))
    end
    for _, ask in ipairs(asks) do Match.choose(ask[2], by_seat[ask[1]]) end
    G.t = G.t + DT
    step()
    Net.check(M.frame, G.actors)
    n = n + 1
    if not Net.behind() then break end
    c = Input.blank(c)                  -- the second frame: no new presses
  end
  M.waiting = n == 0
  G.local_actor = by_seat[M.net.seat] or G.local_actor
end

function Match.update()
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
  if M.net then
    net_step(me, c)
    if Net.lost then
      M.lost_t = (M.lost_t or 0) + DT
      if M.lost_t > 3 then Net.close() Modes.start("menu") return end
    end
  else
    step()
  end
  if M.phase == "match_end" then
    M.end_t = (M.end_t or 0) + DT
    if OVERBIT_HEADLESS and M.end_t >= RULES.match_end then quit() return end     -- the trainer's matches
    if M.end_t >= RULES.match_end or (M.end_t > 2 and menu) then
      if M.net then Net.close() end
      Modes.start("menu")
      return
    end
  elseif menu then
    if M.net then Net.close() end
    Modes.start("menu")
  end
end

-- ---------------------------------------------------------------- drawing

local INK = 0x101418

local function hud_top()
  font("6x12")
  local cx = LW // 2
  -- the two percentages, the point between them
  for team = 1, 2 do
    local x = team == 1 and cx - 62 or cx + 18
    urectfill(x, 4, 44, 16, INK)
    urect(x, 4, 44, 16, TEAM_RGB[team])
    local s = floor(M.pct[team]) .. "%"
    uprint(s, x + 22 - #s * 3, 6, team == M.owner and 0xFFFFFF or 0xB8BEC8)
    -- the rounds won
    for i = 1, RULES.rounds do
      local px = team == 1 and x + 44 - i * 7 or x + (i - 1) * 7
      urectfill(px + 1, 22, 5, 3, i <= M.wins[team] and TEAM_RGB[team] or 0x3A4048)
    end
  end
  local col = M.owner ~= 0 and TEAM_RGB[M.owner] or 0xD8DCE2
  ucircfill(cx, 12, 9, INK)
  ucirc(cx, 12, 9, col)
  if M.cap_team ~= 0 and M.cap > 0 then
    -- the capture as an arc round the point
    local n = floor(M.cap * 24)
    for i = 0, n - 1 do
      local a0, a1 = i / 24 * 2 * pi, (i + 1) / 24 * 2 * pi
      uline(floor(cx + sin(a0) * 9), floor(12 - cos(a0) * 9), floor(cx + sin(a1) * 9), floor(12 - cos(a1) * 9),
           TEAM_RGB[M.cap_team])
      uline(floor(cx + sin(a0) * 8), floor(12 - cos(a0) * 8), floor(cx + sin(a1) * 8), floor(12 - cos(a1) * 8),
           TEAM_RGB[M.cap_team])
    end
  end
  uprint("A", cx - 2, 6, col)
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
    urectfill(cx - #s * 3 - 3, 27, #s * 6 + 6, 13, INK)
    uprint(s, cx - #s * 3, 28, sc or 0xFFFFFF)
  end
  font()
end

local function big_text(s, rgb, y)
  font("8x16")
  uprint(s, LW // 2 - #s * 8 + 1, y + 1, INK, 2)
  uprint(s, LW // 2 - #s * 8, y, rgb, 2)
  font()
end

-- the end of the match: the result, then everyone's kills and deaths
local function scoreboard()
  local x0, y0 = LW // 2 - 120, LH < 270 and 56 or 96
  urectfill(x0, y0, 240, 100, INK)
  font("6x12")
  for team = 1, 2 do
    local x = team == 1 and x0 + 6 or x0 + 126
    uprint(team == 1 and "BLUE" or "RED", x, y0 + 4, TEAM_RGB[team])
    local y = y0 + 18
    for _, a in ipairs(G.actors) do
      if a.team == team then
        uprint(a.name, x, y, a == G.local_actor and 0xFFE070 or 0xD8DCE2)
        local k = a.kills .. "/" .. a.deaths
        uprint(k, x + 108 - #k * 6, y, 0xB8BEC8)
        y = y + 13
      end
    end
  end
  font()
end

Match.draw_point = draw_point
Match.hud_top = hud_top

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
    uprint(who.name:upper(), 6, LH - 14, TEAM_RGB[who.team])
    font()
  end
  if M.phase == "round_end" or M.phase == "match_end" then
    local team = M.round_winner == 1 and "BLUE" or "RED"
    big_text(M.phase == "match_end" and team .. " WINS" or "ROUND TO " .. team, TEAM_RGB[M.round_winner], LH // 3)
  end
end

-- the hero select: the roster by role on the left, the chosen hero's
-- abilities on the right, the team underneath
local FAKE = { st = {}, fx = {}, form = {}, alive = true }
local function select_draw()
  local small = LH < 270                -- 320x180, 384x216: closer rows, no team line
  urectfill(0, 0, LW, LH, 0x0A0E14)
  font("6x12")
  uprint("CHOOSE YOUR HERO", 10, small and 4 or 8, 0xFFE070)
  local y = small and 20 or 30
  local row = small and 12 or 15
  local sel = HERO_ORDER[M.select.sel]
  for _, r in ipairs(ROLE_ORDER) do
    uprint(ROLE_NAME[r], 10, y, 0x7A8290)
    y = y + (small and 12 or 14)
    for i, id in ipairs(HERO_ORDER) do
      if H[id].role == r then
        local h = H[id]
        if id == sel then urectfill(8, y - 1 - (small and 0 or 1), small and 100 or 112, row, h.rgb) end
        urectfill(12, y + 2, 6, 6, h.rgb)
        uprint(h.name:upper(), 22, y, id == sel and INK or 0xD8DCE2)
        y = y + row
      end
    end
    y = y + (small and 2 or 6)
  end
  local h = H[sel]
  local x0 = small and 120 or 150
  local yy = small and 18 or 28
  font("8x16")
  uprint(h.name:upper(), x0, yy, h.rgb)
  font("6x12")
  uprint(ROLE_NAME[h.role], x0 + #h.name * 8 + 8, yy + 4, 0x7A8290)
  -- the line about the hero, cut at the words
  local line = ""
  yy = yy + (small and 20 or 24)
  local cols = (LW - x0 - 10) // 6
  for w in h.desc:gmatch("%S+") do
    if #line + #w + 1 > cols then uprint(line, x0, yy, 0xB8BEC8) yy, line = yy + (small and 12 or 13), "" end
    line = line == "" and w or line .. " " .. w
  end
  uprint(line, x0, yy, 0xB8BEC8)
  yy = yy + (small and 16 or 24)
  for _, ab in ipairs(h.hud) do
    if ab.name then
      local key = Hud.key(ab.key)
      local x = key and uprompt(key, x0, yy, true) or x0
      uprint(ab.name, max(x + 4, x0 + 30), yy, 0xD8DCE2)
      yy = yy + (small and 14 or 17)
    end
  end
  local ok, un = pcall(h.ult_name, FAKE)
  if ok and un then
    local x = uprompt(Hud.key("ult"), x0, yy, true)
    uprint(un, max(x + 4, x0 + 30), yy, 0xFFE070)
  end
  -- the team
  if not small then
    uprint("YOUR TEAM", x0, LH - 58, 0x7A8290)
    local x = x0
    for _, a in ipairs(G.actors) do
      if a.team == G.local_actor.team then
        local nm = a == G.local_actor and h.short or a.hero.short
        uprint(nm, x, LH - 42, a == G.local_actor and 0xFFE070 or a.hero.rgb)
        x = x + 36
      end
    end
  end
  local px = uprompt(Input.cmd.pad and "UPDOWN" or "up", 10, LH - 17, true)
  uprint("HERO", px + 3, LH - 17, 0x7A8290)
  px = uprompt(Input.cmd.pad and "A" or "space", px + 36, LH - 17, true)
  uprint("PLAY", px + 3, LH - 17, 0x7A8290)
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
    local x = uprompt(Input.cmd.pad and "DOWN" or "h", 8, 42, true)
    uprint("CHANGE HERO", x + 3, 42, 0xD8DCE2)
    font()
  end
  if M.net then
    font("6x12")
    local s
    if Net.lost then s = "CONNECTION LOST"
    elseif Net.desync then s = "OUT OF SYNC (frame " .. Net.desync .. ")"
    elseif M.waiting then s = "WAITING FOR THE OTHERS..." end
    if s then
      urectfill(LW // 2 - #s * 3 - 3, 58, #s * 6 + 6, 13, INK)
      uprint(s, LW // 2 - #s * 3, 59, 0xFF8060)
    end
    font()
  end
  if M.phase == "round_end" then
    big_text(M.round_winner == me.team and "ROUND WON" or "ROUND LOST", M.round_winner == me.team and 0x46B4FF or 0xFF4646,
      LH // 3)
  elseif M.phase == "match_end" then
    big_text(M.round_winner == me.team and "VICTORY" or "DEFEAT", M.round_winner == me.team and 0xFFE070 or 0xFF4646,
      LH < 270 and 18 or 48)
    if M.t > 2 then scoreboard() end
  end
end
