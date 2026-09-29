-- The screens: title (with a demo fight when nobody plays), the mode, the
-- hangar where each player fits the robot, the fight in rounds with its
-- pause, and the result.

local S = {}
Scr.screens = S
Scr.setup = {
  mode = "1p",                -- "1p" against the computer, "2p", "cpu" (the computer against itself)
  level = "normal",
  cfg = { { armor = "light", weapon = "sword" }, { armor = "heavy", weapon = "guns" } },
}
local setup = Scr.setup

function Scr.go(name, ...)
  Scr.name, Scr.cur = name, S[name]
  Scr.t = 0
  if Scr.cur.enter then Scr.cur.enter(...) end
end

-- a button just pressed on this pad (nil: any pad)
local function hit(b, p) return btnp(b, p) end
local function ok(p) return btnp(BA, p) or btnp(BSTART, p) end

-- the pads of the two sides; nil means any pad
local function pad_of(i)
  if setup.mode == "2p" then return i end
  if setup.mode == "1p" and i == 1 then return nil end
  return "cpu"
end

local function random_cfg()
  return { armor = choose(Data.ARMORS), weapon = choose(Data.WEAPONS) }
end

-- a list menu: rows of text, the selected one lit, in a panel
local function menu_draw(rows, sel, x, y, w)
  panel(x, y, w, #rows * 24 + 16, 0x0C0E16, 0x8A96B0)
  for k, r in ipairs(rows) do
    local yy = y + 10 + (k - 1) * 24
    if k == sel then
      rectfill(x + 6, yy - 3, w - 12, 22, 0x2A3A5A)
      text(">", x + 12, yy, 0xFFD040)
    end
    text_c(r, x + w // 2, yy, k == sel and 0xFFFFFF or 0x9AA4B8)
  end
end

local function menu_move(sel, n, p)
  if hit(BU, p) then sel = (sel - 2) % n + 1; Snd.ui_move() end
  if hit(BD, p) then sel = sel % n + 1; Snd.ui_move() end
  return sel
end

-- the list of moves, for the menu and the pause
local MOVES = {
  { "LEFT / RIGHT", "WALK  (HOLD BACK: GUARD)" },
  { "DOWN", "CROUCH  (DOWN-BACK: LOW GUARD)" },
  { "UP", "JUMP  (LIGHT: AGAIN IN THE AIR)" },
  { "X  /  Y", "LIGHT  /  HEAVY PUNCH" },
  { "A  /  B", "LIGHT  /  HEAVY KICK" },
  { "FWD, FWD", "DASH  (ALSO IN THE AIR)" },
  { "DOWN, FWD + PUNCH", "WEAPON: SLASH OR BURST" },
  { "Y + B", "WEAPON, THE EASY WAY" },
  { "HIT, THEN STRONGER", "COMBO: IT CANCELS" },
}
function Scr.moves_panel(y)
  panel(40, y, 560, #MOVES * 20 + 44, 0x0C0E16, 0x8A96B0)
  text_c("MOVES", 320, y + 8, 0xFFD040)
  for k, m in ipairs(MOVES) do
    local yy = y + 30 + (k - 1) * 20
    text(m[1], 56, yy, 0x9AD8FF)
    text(m[2], 256, yy, 0xE0E6F0)
  end
end

---------------------------------------------------------------- title

local show = {}          -- the two robots on the title

S.title = {}
function S.title.enter()
  G.camx = 0
  Stage.reset()
  Fx.clear()
  show[1] = Fighter.new(1, "cpu", { armor = "light", weapon = "sword" }, 150, 1)
  show[2] = Fighter.new(2, "cpu", { armor = "heavy", weapon = "guns" }, 490, -1)
  Snd.play_song("title")
end

function S.title.update()
  Scr.t = Scr.t + 1
  Stage.update()
  G.camx = 192 + sin(Scr.t / 600) * 190
  for _, f in ipairs(show) do
    f.t = f.t + 1
    f.ft = f.ft - 1
    if f.ft <= 0 then f.fi, f.ft = f.fi % #ANIM.idle + 1, 9 end
  end
  if ok() then
    Snd.ui_ok()
    Scr.go("mode")
  elseif Scr.t > 60 * 25 then
    -- nobody plays: the computer shows a fight
    Scr.go("fight", true)
  end
end

local function draw_idle(f, x)
  local cx = G.camx
  G.camx = 0
  f.x = x
  Fighter.draw(f)
  G.camx = cx
end

function S.title.draw()
  Stage.draw_back(G.camx)
  draw_idle(show[1], 96)
  draw_idle(show[2], 544)
  Stage.draw_front(G.camx)
  sprite("t_titan", 320, 86 + floor(sin(Scr.t / 40) * 2))
  sprite("t_clash", 320, 166 + floor(sin(Scr.t / 40 + 1) * 2))
  text_c("GIANT ROBOTS, ONE ON ONE", 320, 222, 0xE0E6F0)
  if Scr.t % 60 < 40 then text_c("PRESS START", 320, 262, 0xFFD040, 2) end
  text_c("BM33  M20 PROTOTYPE", 320, 338, 0x8A96B0)
end

---------------------------------------------------------------- mode

S.mode = { sel = 1, help = false }
local MODE_ROWS = { "1 PLAYER VS CPU", "2 PLAYERS", "CPU VS CPU", "CPU LEVEL", "MOVES", "BACK" }

function S.mode.enter() S.mode.help = false end

function S.mode.update()
  local m = S.mode
  Scr.t = Scr.t + 1
  Stage.update()
  G.camx = 192 + sin(Scr.t / 600) * 190
  if m.help then
    if ok() or hit(BB) then m.help = false; Snd.ui_back() end
    return
  end
  m.sel = menu_move(m.sel, #MODE_ROWS)
  local row = MODE_ROWS[m.sel]
  if row == "CPU LEVEL" and (hit(BL) or hit(BR) or ok()) then
    local k = 1
    for j, n in ipairs(Data.CPUS) do if n == setup.level then k = j end end
    k = (k + (hit(BL) and -2 or 0)) % #Data.CPUS + 1
    setup.level = Data.CPUS[k]
    Snd.ui_move()
  elseif ok() then
    if row == "BACK" then Snd.ui_back(); return Scr.go("title") end
    if row == "MOVES" then m.help = true; Snd.ui_ok(); return end
    setup.mode = m.sel == 1 and "1p" or (m.sel == 2 and "2p" or "cpu")
    Snd.ui_ok()
    Scr.go("hangar")
  elseif hit(BB) then
    Snd.ui_back()
    Scr.go("title")
  end
end

function S.mode.draw()
  Stage.draw_back(G.camx)
  Stage.draw_front(G.camx)
  sprite("t_titan", 320, 50)
  if S.mode.help then return Scr.moves_panel(96) end
  local rows = {}
  for k, r in ipairs(MODE_ROWS) do
    rows[k] = r == "CPU LEVEL" and ("CPU LEVEL  < " .. Data.CPU[setup.level].name .. " >") or r
  end
  menu_draw(rows, S.mode.sel, 180, 110, 280)
  text_c("2 PLAYERS: PAD 1 AND PAD 2", 320, 300, 0x8A96B0)
end

---------------------------------------------------------------- hangar

-- each side: its row (1 armour, 2 weapon, 3 ready), ready or not
local H = { side = {}, workers = {}, sparks = {}, crane = 320, crane_to = 320 }
S.hangar = H
local ROBOT_X = { 222, 418 }

function H.enter()
  for i = 1, 2 do
    local p = pad_of(i)
    if p == "cpu" then setup.cfg[i] = random_cfg() end
    H.side[i] = { row = 1, ready = false, pad = p, cpu_t = 50 + random(40) + (i - 1) * 30, fit = 0 }
    H.side[i].f = Fighter.new(i, "cpu", setup.cfg[i], ROBOT_X[i], i == 1 and 1 or -1)
  end
  H.workers = {
    { x = 40, y = 303, v = 0.35 }, { x = 600, y = 303, v = -0.3 }, { x = 20, y = 200, v = 0.25 },
    { x = 610, y = 200, v = -0.2 }, { x = 206, y = 322, weld = true }, { x = 434, y = 322, weld = true },
  }
  H.sparks = {}
  G.camx = 0
  Snd.play_song("hangar")
end

-- change the robot: the crane comes, sparks fly
local function refit(i)
  local s = H.side[i]
  s.f = Fighter.new(i, "cpu", setup.cfg[i], ROBOT_X[i], i == 1 and 1 or -1)
  s.fit = 24
  H.crane_to = ROBOT_X[i]
  Snd.clank()
  for _ = 1, 16 do
    H.sparks[#H.sparks + 1] = { x = ROBOT_X[i] + random(-30, 30), y = 170 + random(-20, 40),
                                vx = (random() - 0.5) * 5, vy = -random() * 4, t = 0 }
  end
end

local function side_update(i)
  local s = H.side[i]
  local cfg = setup.cfg[i]
  if s.pad == "cpu" then
    s.cpu_t = s.cpu_t - 1
    if s.cpu_t == 20 then refit(i) end
    if s.cpu_t <= 0 then s.ready, s.row = true, 3 end
    return
  end
  local p = s.pad
  if s.ready then
    if hit(BB, p) then s.ready = false; Snd.ui_back() end
    return
  end
  local old = s.row
  if hit(BU, p) then s.row = (s.row - 2) % 3 + 1 end
  if hit(BD, p) then s.row = s.row % 3 + 1 end
  if s.row ~= old then Snd.ui_move() end
  local d = (hit(BR, p) and 1 or 0) - (hit(BL, p) and 1 or 0)
  if s.row == 1 and (d ~= 0 or ok(p)) then
    cfg.armor = cfg.armor == "light" and "heavy" or "light"
    refit(i)
  elseif s.row == 2 and (d ~= 0 or ok(p)) then
    cfg.weapon = cfg.weapon == "sword" and "guns" or "sword"
    refit(i)
  elseif s.row == 3 and ok(p) then
    s.ready = true
    Snd.ui_ok()
  elseif hit(BB, p) and i == 1 then
    Snd.ui_back()
    Scr.go("mode")
  end
end

function H.update()
  Scr.t = Scr.t + 1
  for i = 1, 2 do
    side_update(i)
    if Scr.name ~= "hangar" then return end
    local s = H.side[i]
    local f = s.f
    s.fit = max(0, s.fit - 1)
    f.ft = f.ft - 1
    if f.ft <= 0 then f.fi, f.ft = f.fi % #ANIM.idle + 1, 9 end
  end
  -- the crane goes where the work is, then drifts back
  H.crane = approach(H.crane, H.crane_to, 3)
  if H.crane == H.crane_to and Scr.t % 240 == 0 then H.crane_to = random(120, 520) end
  -- people at work
  for _, w in ipairs(H.workers) do
    if w.weld then
      if random(12) == 1 then
        H.sparks[#H.sparks + 1] = { x = w.x + 3, y = w.y - 6, vx = (random() - 0.5) * 3, vy = -random() * 2.5, t = 0 }
        if random(4) == 1 then Snd.weld() end
      end
    else
      w.x = w.x + w.v
      if w.x < 10 or w.x > 630 then w.v = -w.v end
    end
  end
  local j = 0
  for k = 1, #H.sparks do
    local p = H.sparks[k]
    p.t, p.x, p.y, p.vy = p.t + 1, p.x + p.vx, p.y + p.vy, p.vy + 0.25
    if p.t < 26 then j = j + 1; H.sparks[j] = p end
  end
  for k = #H.sparks, j + 1, -1 do H.sparks[k] = nil end
  if H.side[1].ready and H.side[2].ready then
    H.go = (H.go or 0) + 1
    if H.go > 50 then
      H.go = nil
      Scr.go("fight")
    end
  else
    H.go = nil
  end
end

local STAT_NAMES = { "SPEED", "JUMP", "ARMOR", "POWER", "RANGE" }

local function side_panel(i)
  local s = H.side[i]
  local cfg = setup.cfg[i]
  local A, Wp = Data.ARMOR[cfg.armor], Data.WEAPON[cfg.weapon]
  local x = i == 1 and 8 or W - 8 - 180
  panel(x, 58, 180, 250, 0x0C0E16, Hud.P_COL[i])
  local who = s.pad == "cpu" and "CPU" or ("P" .. i)
  text(who, x + 10, 66, Hud.P_COL[i], 2)
  text("VANGUARD", x + 10 + #who * 16 + 8, 74, 0xE0E6F0)
  local rows = { { "ARMOR", A.name }, { "WEAPON", Wp.name }, { "READY", nil } }
  for k, r in ipairs(rows) do
    local y = 104 + (k - 1) * 30
    local on = s.row == k and not s.ready and s.pad ~= "cpu"
    if on then rectfill(x + 4, y - 4, 172, 24, 0x2A3A5A) end
    if r[2] then
      text(r[1], x + 10, y, 0x9AA4B8)
      local v = (on and "< " or "") .. r[2] .. (on and " >" or "")
      text(v, x + 170 - #v * 8, y, on and 0xFFFFFF or 0xE0E6F0)
    else
      local lit = s.ready and G.frame % 30 < 20
      text_c(s.ready and "READY!" or "READY?", x + 90, y, s.ready and (lit and 0xFFD040 or 0xB08A20) or
             (on and 0xFFFFFF or 0x9AA4B8))
    end
  end
  -- the numbers behind the choice
  local st = { A.stats.speed, A.stats.jump, A.stats.armor,
               min(5, Wp.power + (cfg.armor == "heavy" and 1 or 0)), Wp.reach }
  for k, n in ipairs(STAT_NAMES) do
    local y = 196 + (k - 1) * 16
    text(n, x + 10, y, 0x8A96B0)
    for pip = 1, 5 do
      rectfill(x + 82 + (pip - 1) * 18, y + 4, 15, 8, pip <= st[k] and Hud.P_COL[i] or 0x2A2E3A)
    end
  end
  text(A.desc, x + 10, 278, 0xC8D0E0)
  text(Wp.desc, x + 10, 292, 0xC8D0E0)
end

function H.draw()
  sprite("hangar", 0, 0)
  -- a beacon on each tower
  if G.frame % 50 < 25 then
    circfill(194, 108, 3, 0xFF3A28)
    circfill(444, 108, 3, 0xFF3A28)
  end
  -- people on the catwalks and the floor behind the robots
  for _, w in ipairs(H.workers) do
    if not w.weld then
      local k = floor(w.x / 4) % 2
      sprite(k == 0 and "worker0" or "worker1", floor(w.x), w.y, w.v < 0)
    end
  end
  for i = 1, 2 do
    local s = H.side[i]
    local f = s.f
    local cx = G.camx
    G.camx = 0
    f.x = ROBOT_X[i]
    -- a fitted robot shakes a little
    local shake = s.fit > 0 and (s.fit % 4 < 2 and 1 or -1) or 0
    f.x = f.x + shake
    Fighter.draw(f)
    G.camx = cx
  end
  -- the welders in front of the robots' feet, the sparks
  for _, w in ipairs(H.workers) do
    if w.weld then sprite("worker2", w.x, w.y) end
  end
  for _, p in ipairs(H.sparks) do
    rectfill(floor(p.x), floor(p.y), 2, 2, p.t < 8 and 0xFFFFFF or (p.t < 16 and 0xFFE070 or 0xFF7A1E))
  end
  sprite("crane", floor(H.crane), 41)
  side_panel(1)
  side_panel(2)
  text_c("HANGAR", 320, 6, 0xFFD040, 2)
  if H.go then
    text_c("LAUNCH!", 320, 44, 0xFFFFFF, 2)
  else
    text_c("UP/DOWN: CHOOSE  LEFT/RIGHT: CHANGE", 320, 330, 0x9AA4B8)
    text_c("A: READY   B: BACK", 320, 344, 0x9AA4B8)
  end
end

---------------------------------------------------------------- the fight

local F = { f = {} }
S.fight = F

local function new_round()
  local m = F.m
  local a = Fighter.new(1, pad_of(1), setup.cfg[1], ARENA_W / 2 - 150, 1)
  local b = Fighter.new(2, pad_of(2), setup.cfg[2], ARENA_W / 2 + 150, -1)
  F.f[1], F.f[2] = a, b
  m.clock, m.tick = 99, 0
  m.phase, m.t = "intro", 0
  m.final = m.wins[1] == 1 and m.wins[2] == 1
  G.camx = ARENA_W / 2 - W / 2
  G.shake = 0
  Fx.clear()
  Snd.round()
end

-- demo: the computer against itself, until someone presses a button
function F.enter(demo)
  F.demo = demo or false
  F.saved_mode = nil
  if demo then
    F.saved_mode = setup.mode
    setup.mode = "cpu"
    setup.cfg[1], setup.cfg[2] = random_cfg(), random_cfg()
  end
  F.m = { round = 1, wins = { 0, 0 }, clock = 99 }
  F.paused, F.psel, F.help = false, 1, false
  F.rsel = 1
  Stage.reset()
  new_round()
  Snd.play_song("fight")
end

local function leave(to)
  if F.saved_mode then setup.mode = F.saved_mode end
  Scr.go(to)
end

local PAUSE_ROWS = { "RESUME", "MOVES", "HANGAR", "TITLE" }
local RESULT_ROWS = { "REMATCH", "HANGAR", "TITLE" }

local function pause_update()
  if F.help then
    if ok() or hit(BB) or hit(BSTART) then F.help = false; Snd.ui_back() end
    return
  end
  F.psel = menu_move(F.psel, #PAUSE_ROWS)
  if hit(BSTART) or hit(BB) then F.paused = false; Snd.ui_back(); return end
  if hit(BA) then
    local r = PAUSE_ROWS[F.psel]
    Snd.ui_ok()
    if r == "RESUME" then F.paused = false
    elseif r == "MOVES" then F.help = true
    elseif r == "HANGAR" then leave("hangar")
    else leave("title") end
  end
end

-- the winner of a round that ran out of time: more life left (in parts)
local function time_winner(a, b)
  local ka, kb = a.life / 1000, b.life / 1000
  if abs(ka - kb) < 0.005 then return nil end
  return ka > kb and 1 or 2
end

local function end_round(winner)
  local m = F.m
  if winner then m.wins[winner] = m.wins[winner] + 1 end
  m.winner = winner
  if (winner and m.wins[winner] >= 2) or m.round >= 5 then
    m.phase, m.t = "result", 0
    if m.wins[1] == m.wins[2] then m.champion = nil else m.champion = m.wins[1] > m.wins[2] and 1 or 2 end
    if m.champion then Snd.win() end
    F.rsel = 1
    return
  end
  m.round = m.round + 1
  new_round()
end

local function play_frame(control)
  local a, b = F.f[1], F.f[2]
  for i, f in ipairs(F.f) do
    local o = F.f[3 - i]
    if control then
      if f.cpu then Cpu.update(f, o, setup.level) end
      Fighter.read(f)
    else
      Fighter.hands_off(f)
    end
  end
  Fighter.update(a, b)
  Fighter.update(b, a)
  Fighter.separate(a, b)
  Stage.camera(a, b)
  Fx.update(F.f)
end

function F.update()
  local m = F.m
  Stage.update()
  if F.demo and (ok() or hit(BX) or hit(BY)) then return leave("title") end
  if F.paused then return pause_update() end
  if hit(BSTART) and m.phase == "fight" and not F.demo then
    F.paused, F.psel = true, 1
    Snd.ui_ok()
    return
  end
  m.t = m.t + 1
  local a, b = F.f[1], F.f[2]
  if m.phase == "intro" then
    play_frame(false)
    if m.t == 70 then Snd.fight() end
    if m.t >= 110 then m.phase, m.t = "fight", 0 end
  elseif m.phase == "fight" then
    play_frame(true)
    m.tick = m.tick + 1
    if m.tick >= 60 then
      m.tick = 0
      m.clock = m.clock - 1
    end
    if a.life <= 0 or b.life <= 0 then
      m.phase, m.t = "ko", 0
      m.loser = a.life <= 0 and (b.life <= 0 and 0 or 1) or 2
      Snd.ko()
      Snd.boom()
      for _, f in ipairs(F.f) do if f.life <= 0 then Fx.ko(f) end end
    elseif m.clock <= 0 then
      m.phase, m.t = "timeover", 0
      Snd.ko()
    end
  elseif m.phase == "ko" then
    -- slow motion for a moment
    if m.t > 50 or m.t % 2 == 0 then play_frame(false) end
    if m.t > 90 then
      for i, f in ipairs(F.f) do if i ~= m.loser and m.loser ~= 0 then Fighter.celebrate(f) end end
    end
    if m.t >= 210 then end_round(m.loser == 0 and nil or 3 - m.loser) end
  elseif m.phase == "timeover" then
    play_frame(false)
    local w = time_winner(a, b)
    if m.t > 60 and w then Fighter.celebrate(F.f[w]) end
    if m.t >= 170 then end_round(w) end
  elseif m.phase == "result" then
    play_frame(false)
    if m.champion then Fighter.celebrate(F.f[m.champion]) end
    if F.demo then
      if m.t > 240 then leave("title") end
    elseif m.t > 90 then
      F.rsel = menu_move(F.rsel, #RESULT_ROWS)
      if ok() then
        Snd.ui_ok()
        local r = RESULT_ROWS[F.rsel]
        if r == "REMATCH" then F.enter(false)
        elseif r == "HANGAR" then leave("hangar")
        else leave("title") end
      end
    end
  end
  -- the screen shakes after heavy hits
  if G.shake > 0 then
    G.shake = max(0, G.shake - 0.5)
    G.sy = floor(G.shake) * (G.frame % 2 == 0 and 1 or -1)
  else
    G.sy = 0
  end
end

local function big(name, x, y) sprite(name, x, y) end

local function announce()
  local m = F.m
  local t = m.t
  if m.phase == "intro" then
    if t < 70 then
      if m.final or m.round > 3 then
        big("t_final", 320, 150)
      else
        big("t_round", 290, 150)
        big("r" .. m.round, 400, 150)
      end
    elseif t % 8 < 6 or t < 90 then
      big("t_fight", 320, 150)
    end
  elseif m.phase == "ko" then
    if t < 150 then big("t_ko", 320, 140) end
  elseif m.phase == "timeover" then
    big("t_time", 320, 140)
  elseif m.phase == "result" then
    if m.champion then
      local f = F.f[m.champion]
      local who = f.cpu and "t_cpu" or (m.champion == 1 and "t_p1" or "t_p2")
      big(who, 230, 120)
      big("t_wins", 360, 120)
    else
      big("t_draw", 320, 120)
    end
    if F.demo then
      text_c("DEMO", 320, 170, 0xE0E6F0, 2)
    elseif t > 90 then
      menu_draw(RESULT_ROWS, F.rsel, 240, 170, 160)
    end
  end
end

function F.draw()
  local a, b = F.f[1], F.f[2]
  Stage.draw_back(G.camx)
  Fx.draw_back()
  -- the one attacking is drawn in front
  if a.state == "attack" and b.state ~= "attack" then
    Fighter.draw(b); Fighter.draw(a)
  else
    Fighter.draw(a); Fighter.draw(b)
  end
  Fx.draw_front()
  Stage.draw_front(G.camx)
  if F.m.phase == "fight" then
    Fighter.draw_tag(a, a.cpu and "CPU" or "P1", Hud.P_COL[1])
    Fighter.draw_tag(b, b.cpu and "CPU" or "P2", Hud.P_COL[2])
  end
  Hud.draw(F.m, a, b)
  announce()
  if F.demo and G.frame % 60 < 40 then text_c("DEMO - PRESS START", 320, 70, 0xFFD040) end
  if F.paused then
    if F.help then
      Scr.moves_panel(70)
    else
      text_c("PAUSE", 320, 96, 0xFFD040, 2)
      menu_draw(PAUSE_ROWS, F.psel, 230, 136, 180)
    end
  end
end
