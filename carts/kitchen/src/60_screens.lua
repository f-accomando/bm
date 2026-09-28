-- Screens: title, menu, lobby (join and pick a chef), campaign map, stage
-- intro, play, pause, results. Few and quick: the kitchen is the game.

local screens = {}
Scr.screens = screens

function Scr.go(name, arg)
  G.screen = name
  G.t = 0
  local s = screens[name]
  if s.enter then s.enter(arg) end
end

---------------------------------------------------------------- runs

-- star goals of a stage for n players
function Scr.goals(stage, n)
  local pool = {}
  for _, id in ipairs(stage.recipes) do pool[#pool + 1] = Data.RECIPE[id] end
  local w, v = 0, 0
  for _, r in ipairs(pool) do w, v = w + r.work, v + r.value end
  w, v = w / #pool, v / #pool
  local per = (7 + 2.6 * w) / ({ 1, 1.65, 2.2, 2.6 })[clamp(n, 1, 4)]
  local target = stage.time / per * v * 1.15 * (stage.goal_k or 1)
  local function r5(x) return max(5, floor(x / 5 + 0.5) * 5) end
  return { r5(target * 0.35), r5(target * 0.62), r5(target * 0.88) }
end

function Scr.new_run(stage, opts)
  opts = opts or {}
  local run = Kit.new(stage)
  run.chefs = {}
  run.mult = { move = 1, chop = 1, cook = 1, burn = 1, wash = 1, throw = 1 }
  for i, p in ipairs(opts.players or G.players) do
    run.chefs[i] = Chef.new(run, i, p.pad, p.chef)
    run.chefs[i].bot = p.bot
  end
  local pool = {}
  for _, id in ipairs(opts.recipes or stage.recipes) do pool[#pool + 1] = Data.RECIPE[id] end
  Food.set_pool(run, pool)
  Ord.init(run, { every = stage.every or 17, patience = stage.patience or 1 })
  run.time_left = opts.endless and nil or stage.time
  run.goals = Scr.goals(stage, #run.chefs)
  Haz.init(run)
  Dis.init(run)
  run.meshes = Mesh.kitchen(run)
  Ren.fit(run)
  Fx.clear()
  run.t = 0
  return run
end

-- one simulation step of a run (play screen, title attract, tests)
function Scr.step(run, dt, human)
  run.t = run.t + dt
  for _, c in ipairs(run.chefs) do
    if c.bot then Bot.update(run, c, dt)
    elseif human then Chef.read_pad(c)
    else c.inp.mx, c.inp.mz, c.inp.a, c.inp.b, c.inp.x, c.inp.y, c.inp.xh = 0, 0, false, false, false, false, false end
    Chef.update(run, c, dt)
  end
  Chef.separate(run.chefs)
  Kit.update_station_pos(run)
  Chef.update_loose(run, dt)
  Kit.update(run, dt)
  Food.update(run, dt)
  Ord.update(run, dt)
  Haz.update(run, dt)
  Dis.update(run, dt)
  End.update(run, dt)
  Ren.update_cam(run, dt)
end

local function draw_run(run)
  Ren.world(run)
  Dis.draw2d(run)
  Hud.world_marks(run)
  Fx.draw2d()
  Hud.orders(run)
  Hud.status(run)
  End.draw2d(run)
end
Scr.draw_run = draw_run

---------------------------------------------------------------- title

local attract

local function attract_run()
  -- the first kitchen with computer chefs cooking, behind the title
  local players = {}
  for i = 1, 3 do players[i] = { pad = i, chef = i, bot = true } end
  local r = Scr.new_run(Data.STAGES[1], { players = players, recipes = { "tomato_soup", "sunny_salad" } })
  r.time_left = nil
  r.attract = true
  r.no_fire = true
  return r
end

screens.title = {
  enter = function()
    attract = attract_run()
    G.run = attract
    Snd.play_song(1)
    Snd.intensity(0)
  end,
  update = function(dt)
    Scr.step(attract, dt, false)
    if G.t > 0.4 and (btnp(BA) or btnp(BSTART)) then
      Snd.ui_ok()
      Scr.go("menu")
    end
  end,
  draw = function()
    draw_run(attract)
    local y = 100 + floor(sin(G.t * 2) * 4)
    text_cs("CHAOS", W / 2, y, 0xFFD040, 4, 0x602010)
    text_cs("KITCHEN", W / 2, y + 66, 0xFF6040, 4, 0x401008)
    text_cs("a cooking game for 1-4 chefs", W / 2, y + 136, 0xFFFFFF, 1, 0x000000)
    if (G.frame // 30) % 2 == 0 then text_cs("press A", W / 2, y + 170, 0xFFFFFF, 2, 0x000000) end
  end,
}

---------------------------------------------------------------- menu

local MENU = {
  { "CAMPAIGN", "30+ kitchens in six worlds" },
  { "ENDLESS", "the kitchen grows while you cook" },
  { "PRACTICE", "one recipe, no clock" },
  { "RECIPE BOOK", "every dish you have met" },
  { "OPTIONS", "sound, music, statistics" },
}
local menu_sel = 1

screens.menu = {
  update = function(dt)
    Scr.step(attract, dt, false)
    local d = Pad.nav(nil, "y")
    if d ~= 0 then menu_sel = (menu_sel - 1 + d) % #MENU + 1; Snd.ui_move() end
    if btnp(BA) or btnp(BSTART) then
      Snd.ui_ok()
      local m = MENU[menu_sel][1]
      if m == "CAMPAIGN" then Scr.go("lobby", "map")
      elseif m == "ENDLESS" then Scr.go("lobby", "endless")
      elseif m == "PRACTICE" then Scr.go("lobby", "practice")
      elseif m == "RECIPE BOOK" then Scr.go("book")
      else Scr.go("options") end
    elseif btnp(BB) then
      Snd.ui_back()
      Scr.go("title")
    end
  end,
  draw = function()
    draw_run(attract)
    text_cs("CHAOS KITCHEN", W / 2, 70, 0xFFD040, 3, 0x401008)
    for i, m in ipairs(MENU) do
      local y = 130 + (i - 1) * 40
      local sel = i == menu_sel
      panel(W / 2 - 150, y, 300, 34, sel and 0xFFD040 or 0x302A3A, 0x000000)
      text_c(m[1], W / 2, y + 2, sel and 0x301808 or 0xFFFFFF, 1)
      text_c(m[2], W / 2, y + 17, sel and 0x604020 or 0xA0A0B0, 1)
    end
    text_cs(fmt("stars %d", Save.total_stars()), W / 2, H - 22, 0xFFE060)
  end,
}

---------------------------------------------------------------- lobby

-- Every controller joins with A and picks a chef with left/right; A again
-- when ready. Two players never share a chef.
local lobby = { slots = {}, next = nil, t = 0 }

local function chef_taken(ci, except)
  for p, s in pairs(lobby.slots) do
    if p ~= except and s.chef == ci then return true end
  end
  return false
end

local function next_free(ci, dir, p)
  for _ = 1, 4 do
    ci = (ci - 1 + dir) % 4 + 1
    if not chef_taken(ci, p) then return ci end
  end
  return ci
end

screens.lobby = {
  enter = function(nextscreen)
    lobby.next = nextscreen or lobby.next or "map"
    lobby.slots = {}
    lobby.t = 0
    -- the players of the last game are still in
    for _, pl in ipairs(G.players) do
      lobby.slots[pl.pad] = { chef = pl.chef, ready = false }
    end
  end,
  update = function(dt)
    local any = false
    for p = 1, 4 do
      local s = lobby.slots[p]
      if not s then
        if btnp(BA, p) then
          lobby.slots[p] = { chef = next_free(p - 1, 1, p), ready = false }
          Snd.ui_ok()
        end
      else
        any = true
        if not s.ready then
          local d = Pad.nav(p, "x")
          if d ~= 0 then s.chef = next_free(s.chef, d, p); Snd.ui_move() end
          if btnp(BA, p) or btnp(BSTART, p) then s.ready = true; Snd.ui_ok() end
          if btnp(BB, p) then lobby.slots[p] = nil; Snd.ui_back() end
        elseif btnp(BB, p) then
          s.ready = false
          Snd.ui_back()
        end
      end
    end
    if not any and btnp(BB) then Scr.go("menu") return end
    -- everyone who joined is ready: go
    local all, n = true, 0
    for _, s in pairs(lobby.slots) do
      n = n + 1
      if not s.ready then all = false end
    end
    if n > 0 and all then
      lobby.t = lobby.t + dt
      if lobby.t > 0.5 then
        G.players = {}
        for p = 1, 4 do
          local s = lobby.slots[p]
          if s then G.players[#G.players + 1] = { pad = p, chef = s.chef } end
        end
        Scr.go(lobby.next)
      end
    else
      lobby.t = 0
    end
  end,
  draw = function()
    cls(0x2A2440)
    for i = 0, 8 do rectfill(0, 200 + i * 20, W, 20, mix_rgb(0x2A2440, 0x6A5A8A, i / 8)) end
    text_cs("WHO'S COOKING?", W / 2, 8, 0xFFD040, 2, 0x000000)
    text_c("each controller: A to join, left/right to pick, A when ready", W / 2, 44, 0xC0C0D0)
    -- the four chefs on a little stage
    zclear()
    camera3d(0, 1.3, -5.2, 0, -0.12, 38)
    light3d(-0.4, 0.8, -0.6, 0.5)
    for ci = 1, 4 do
      local x = (ci - 2.5) * 1.35
      local owner
      for p, s in pairs(lobby.slots) do if s.chef == ci then owner = p end end
      local fake = { ci = ci, def = Data.CHEFS[ci], walk = G.t * (owner and 2.2 or 0), speed = owner and 2.4 or 0,
                     idle_t = 3, act = nil, act_t = 0, stun = 0, spray = false,
                     hold = nil }
      if owner and lobby.slots[owner].ready then fake.act, fake.act_t, fake.speed = "cheer", 0.35 + (G.t % 0.7), 0 end
      Ren.chef(fake, x, 0, 0, pi + sin(G.t + ci) * 0.4)
    end
    for ci = 1, 4 do
      local d = Data.CHEFS[ci]
      local x = floor(W / 2 + (ci - 2.5) * 156)
      local owner
      for p, s in pairs(lobby.slots) do if s.chef == ci then owner = p end end
      local col = owner and PCOL[owner] or 0x504868
      panel(x - 72, 236, 144, 118, owner and PCOL_DARK[owner] or 0x302A48, col)
      text_c(d.name, x, 240, 0xFFFFFF, 1)
      text_c(d.role, x, 256, 0xFFD040)
      local function stat(label, v, y)
        print(label, x - 64, y, 0xC0C0D0)
        bar(x - 8, y + 5, 70, 6, v / 1.3, 0x60E080, 0x202030)
      end
      stat("speed", d.speed, 274)
      stat("chop", d.chop, 290)
      print(d.dash and "dash" or "no dash", x - 64, 306, d.dash and 0x80E0FF or 0xFF8080)
      print(d.throw and "throw" or "no throw", x + 4, 306, d.throw and 0x80E0FF or 0xFF8080)
      text_c(d.trait, x, 326, 0xE0E0E0)
      if owner then
        local s = lobby.slots[owner]
        local y = 70 + floor(sin(G.t * 4 + ci) * 3)
        panel(x - 34, y, 68, 22, PCOL[owner], 0xFFFFFF)
        text_c((s.ready and "OK " or "") .. "P" .. owner, x, y + 3, 0xFFFFFF)
      end
    end
    local waiting = 0
    for p = 1, 4 do if not lobby.slots[p] then waiting = waiting + 1 end end
    if not next(lobby.slots) then
      text_cs("press A to join", W / 2, 180, 0xFFFFFF, 2)
    end
  end,
}

---------------------------------------------------------------- campaign map

local map_sel = 1

screens.map = {
  enter = function()
    -- start on the first stage without stars
    for i, s in ipairs(Data.STAGES) do
      if Save.open(s) then map_sel = i end
      if (Save.data.stars[s.id] or 0) == 0 and Save.open(s) then map_sel = i break end
    end
    Snd.play_song(Data.STAGES[map_sel].world)
    Snd.intensity(0.2)
  end,
  update = function(dt)
    local dx = Pad.nav(nil, "x")
    local dy = Pad.nav(nil, "y")
    local n = #Data.STAGES
    if dx ~= 0 then map_sel = clamp(map_sel + dx, 1, n); Snd.ui_move() end
    if dy ~= 0 then
      local w = clamp(Data.STAGES[map_sel].world + dy, 1, #Data.WORLDS)
      for i, s in ipairs(Data.STAGES) do if s.world == w then map_sel = i break end end
      Snd.ui_move()
    end
    local st = Data.STAGES[map_sel]
    Snd.play_song(st.world)
    if btnp(BA) then
      if Save.open(st) then Snd.ui_ok(); Scr.go("intro", st)
      else Snd.nope() end
    elseif btnp(BB) then
      Snd.ui_back()
      Scr.go("menu")
    end
  end,
  draw = function()
    local st = Data.STAGES[map_sel]
    local world = Data.WORLDS[st.world]
    cls(world.sky)
    for i = 0, 5 do rectfill(0, 120 + i * 40, W, 40, mix_rgb(world.sky, world.base, i / 6)) end
    text_cs("WORLD " .. st.world .. "  " .. world.name, W / 2, 10, 0xFFFFFF, 2, 0x000000)
    text_cs(world.tag .. "  -  new: " .. world.mech, W / 2, 46, 0xFFE8A0, 1, 0x000000)
    -- the stages of this world
    local list = {}
    for _, s in ipairs(Data.STAGES) do if s.world == st.world then list[#list + 1] = s end end
    local gap = min(110, floor((W - 40) / #list))
    local x0 = floor(W / 2 - gap * (#list - 1) / 2)
    for i, s in ipairs(list) do
      local x = x0 + (i - 1) * gap
      local y = 120
      local open = Save.open(s)
      local sel = s == st
      local r = sel and 34 or 28
      circfill(x, y + 40, r + 3, 0x000000)
      circfill(x, y + 40, r, open and (sel and 0xFFD040 or world.wall) or 0x505060)
      text_c(s.id, x, y + 32, open and 0xFFFFFF or 0x909090, 1)
      if not open then Hud.icon(Hud.IC.lock, x - 8, y + 52) end
      local stars = Save.data.stars[s.id] or 0
      for k = 1, 3 do
        Hud.icon(k <= stars and Hud.IC.star or Hud.IC.star0, x - 26 + (k - 1) * 17, y + 80)
      end
      if i < #list then line(x + r + 4, y + 40, x + gap - r - 4, y + 40, 0xFFFFFF) end
    end
    panel(60, 230, W - 120, 110, 0x302A3A, 0xFFFFFF)
    text_c(st.name, W / 2, 238, 0xFFD040, 2)
    text_c(st.tip, W / 2, 276, 0xFFFFFF)
    local best = Save.data.best[st.id]
    text_c(best and fmt("best %d coins", best) or "not played yet", W / 2, 298, 0xC0C0D0)
    if not Save.open(st) then
      text_c(fmt("needs %d stars (you have %d)", Data.WORLD_NEEDS[st.world], Save.total_stars()), W / 2, 318, 0xFF8080)
    else
      text_c("A: play   B: back   left/right: stage   up/down: world", W / 2, 318, 0xA0A0B0)
    end
  end,
}

---------------------------------------------------------------- stage intro

local intro_stage

screens.intro = {
  enter = function(stage)
    intro_stage = stage
    G.run = Scr.new_run(stage, { mode = G.mode })
  end,
  update = function(dt)
    if G.t > 0.3 and (btnp(BA) or btnp(BSTART)) then
      Snd.ui_ok()
      Scr.go("play")
    elseif btnp(BB) then
      Snd.ui_back()
      Scr.go("map")
    end
  end,
  draw = function()
    local run = G.run
    Ren.world(run)
    local st = intro_stage
    panel(40, 30, W - 80, H - 60, 0x201C2C, 0xFFD040)
    text_c(st.id .. "  " .. st.name, W / 2, 40, 0xFFD040, 2)
    text_c(st.tip, W / 2, 78, 0xFFFFFF)
    text_c("ON THE MENU", W / 2, 104, 0xA0A0C0)
    local x, y = 64, 124
    for _, id in ipairs(st.recipes) do
      local r = Data.RECIPE[id]
      local h = Hud.card_height(r)
      if x + 170 > W - 50 then x, y = 64, y + 58 end
      panel(x, y, 166, 52, 0xFFF8EC, 0x806040)
      print(r.name, x + 5, y + 2, 0x40302A)
      Hud.draw_parts(r.parts, x + 5, y + 20, 156)
      x = x + 172
    end
    local g = run.goals
    local yy = H - 90
    for k = 1, 3 do
      Hud.icon(Hud.IC.star, W / 2 - 150 + (k - 1) * 110, yy)
      print(tostring(g[k]), W / 2 - 130 + (k - 1) * 110, yy, 0xFFE060)
    end
    text_c(fmt("%d chef%s, %s", #run.chefs, #run.chefs > 1 and "s" or "", fmt_time(st.time)), W / 2, yy + 24, 0xC0C0D0)
    if (G.frame // 30) % 2 == 0 then text_c("press A to open the kitchen", W / 2, H - 46, 0xFFFFFF) end
  end,
}

---------------------------------------------------------------- play

local play = { phase = "ready", t = 0 }

screens.play = {
  enter = function()
    play.phase, play.t = "ready", 0
    Snd.play_song(G.run.world and G.run.stage.world or 1)
  end,
  update = function(dt)
    local run = G.run
    play.t = play.t + dt
    if play.phase == "ready" then
      Ren.update_cam(run, dt)
      Fx.update(dt)
      if play.t > 1.8 then play.phase, play.t = "go", 0; Snd.go() end
      return
    end
    if play.phase == "over" then
      Fx.update(dt)
      if play.t > 2.6 then Scr.go("results") end
      return
    end
    for _, c in ipairs(run.chefs) do
      if btnp(BSTART, c.pad) and not c.bot then Scr.go("pause") return end
    end
    Scr.step(run, dt, true)
    Fx.update(dt)
    if run.time_left then
      run.time_left = run.time_left - dt
      if run.time_left <= 0 then
        run.time_left = 0
        play.phase, play.t = "over", 0
        Snd.time_up()
      end
    end
    if run.over then play.phase, play.t = "over", 0 end
    -- every 10 s the frame time goes to the log (serial console): update +
    -- draw of the last frames, and the triangles of the last one
    local pf = run.perf or { n = 0, sum = 0, max = 0 }
    run.perf = pf
    local ms = stat(1)
    pf.n, pf.sum, pf.max = pf.n + 1, pf.sum + ms, max(pf.max, ms)
    if pf.n >= 600 then
      log(fmt("kitchen %s x%d: %.1f ms avg, %.1f max, %d fps, %d triangles",
              run.stage.id or "?", #run.chefs, pf.sum / pf.n, pf.max, stat(2), stat(4)))
      pf.n, pf.sum, pf.max = 0, 0, 0
    end
    -- music: faster with more orders and less patience
    local k = 0
    for _, o in ipairs(run.orders) do k = k + (1 - o.t / o.T) * 0.25 + 0.08 end
    Snd.intensity(k)
  end,
  draw = function()
    local run = G.run
    draw_run(run)
    if play.phase == "ready" then
      text_cs(play.t < 1.1 and "READY..." or "GO!", W / 2, H / 2 - 30, 0xFFFFFF, 4, 0x000000)
    elseif play.phase == "go" and play.t < 0.8 then
      text_cs("GO!", W / 2, H / 2 - 30 - floor(play.t * 40), 0xFFE040, 4, 0x000000)
    elseif play.phase == "over" then
      text_cs(run.over_text or "TIME'S UP!", W / 2, H / 2 - 30, 0xFFE040, 4, 0x000000)
    end
    if G.debug then
      print(fmt("%.1f ms %d fps %d tri", stat(1), stat(2), stat(4)), 130, H - 24, 0xFFFFFF)
    end
  end,
}

---------------------------------------------------------------- pause

local pause_sel = 1
local PAUSE = { "RESUME", "RESTART", "QUIT" }

screens.pause = {
  enter = function() pause_sel = 1 end,
  update = function(dt)
    local d = Pad.nav(nil, "y")
    if d ~= 0 then pause_sel = (pause_sel - 1 + d) % #PAUSE + 1; Snd.ui_move() end
    if btnp(BSTART) or btnp(BB) then G.screen = "play" return end
    if btnp(BA) then
      local m = PAUSE[pause_sel]
      Snd.ui_ok()
      if m == "RESUME" then G.screen = "play"
      elseif m == "RESTART" then
        local run = G.run
        if run.endless then Scr.go("endless") else Scr.go("intro", run.stage) end
      else
        Scr.go(G.run.endless and "menu" or "map")
      end
    end
  end,
  draw = function()
    draw_run(G.run)
    panel(W / 2 - 110, 110, 220, 150, 0x201C2C, 0xFFD040)
    text_c("PAUSED", W / 2, 118, 0xFFD040, 2)
    for i, m in ipairs(PAUSE) do
      local sel = i == pause_sel
      text_c((sel and "> " or "  ") .. m .. (sel and " <" or "  "), W / 2, 160 + (i - 1) * 28, sel and 0xFFFFFF or 0x9090A0)
    end
  end,
}

---------------------------------------------------------------- results

local res = {}

screens.results = {
  enter = function()
    local run = G.run
    local st = run.stage
    local stars = 0
    for k = 1, 3 do if run.coins >= run.goals[k] then stars = k end end
    res.stars, res.shown, res.t = stars, 0, 0
    local d = Save.data
    res.new_best = run.coins > (d.best[st.id] or -1)
    if res.new_best then d.best[st.id] = run.coins end
    if stars > (d.stars[st.id] or 0) then d.stars[st.id] = stars end
    G.stats_add("stages", 1)
    Save.write()
    Snd.stop_song()
    if stars > 0 then Snd.fanfare() else Snd.expire() end
  end,
  update = function(dt)
    res.t = res.t + dt
    local want = min(res.stars, floor((res.t - 0.8) / 0.5) + 1)
    if want > res.shown and res.t > 0.8 then
      res.shown = want
      Snd.star(res.shown)
      Ren.shake(0.2)
    end
    if res.t < 1 then return end
    local st = G.run.stage
    if btnp(BA) then
      Snd.ui_ok()
      local nxt = Data.STAGES[st.n + 1]
      if nxt and Save.open(nxt) then Scr.go("intro", nxt) else Scr.go("map") end
    elseif btnp(BX) then
      Snd.ui_ok()
      Scr.go("intro", st)
    elseif btnp(BB) then
      Snd.ui_back()
      Scr.go("map")
    end
  end,
  draw = function()
    local run = G.run
    Ren.world(run)
    panel(90, 40, W - 180, H - 80, 0x201C2C, 0xFFD040)
    text_c(run.stage.id .. "  " .. run.stage.name, W / 2, 50, 0xFFD040, 2)
    for k = 1, 3 do
      local x = W / 2 - 90 + (k - 1) * 60
      local on = k <= res.shown
      local y = 96 - (on and floor(max(0, 0.3 - (res.t - 0.8 - (k - 1) * 0.5)) * 40) or 0)
      panel(x, y, 52, 52, on and 0xFFD040 or 0x403A50, 0x000000)
      Hud.icon(on and Hud.IC.star or Hud.IC.star0, x + 18, y + 18)
      print(tostring(run.goals[k]), x + 26 - #tostring(run.goals[k]) * 4, y + 54, 0xC0C0C0)
    end
    local y = 186
    text_c(fmt("coins  %d%s", run.coins, res.new_best and "  NEW BEST!" or ""), W / 2, y, 0xFFE060, 1)
    text_c(fmt("served %d   lost %d   wrong %d", run.served, run.failed, run.wrong), W / 2, y + 22, 0xFFFFFF)
    text_c(fmt("best combo x%d", min(4, run.best_combo)), W / 2, y + 44, 0x80E0FF)
    if res.t > 1 then
      text_c("A: next   X: retry   B: map", W / 2, H - 64, 0xC0C0D0)
    end
  end,
}

---------------------------------------------------------------- stubs for later phases

screens.options = {
  update = function() if btnp(BB) or btnp(BA) then Scr.go("menu") end end,
  draw = function() cls(0x201C2C); text_c("OPTIONS", W / 2, 40, 0xFFD040, 2) end,
}
screens.book = {
  update = function() if btnp(BB) or btnp(BA) then Scr.go("menu") end end,
  draw = function() cls(0x201C2C); text_c("RECIPE BOOK", W / 2, 40, 0xFFD040, 2) end,
}
screens.endless = {
  enter = function() Scr.go("map") end,
}
screens.practice = {
  enter = function() Scr.go("map") end,
}

---------------------------------------------------------------- dispatch

function Scr.update(dt)
  local s = screens[G.screen]
  if s and s.update then s.update(dt) end
end

function Scr.draw()
  local s = screens[G.screen]
  if s and s.draw then s.draw() end
end
