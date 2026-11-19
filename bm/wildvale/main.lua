-- Wildvale: 2D Top-Down Survival Action RPG + Modular Building + Co-op (bmnet)
local net = require "bmnet"
local lib = require "bmlib"

local TILE = 16
local MAP_W, MAP_H = 80, 80
local DAY_LEN = 3600       -- 60 seconds per day at 60 fps
local DAYS_PER_SEASON = 3
local SEASONS = { "Primavera", "Estate", "Autunno", "Inverno" }
local HOTBAR = {
  { id = "axe",   name = "Ascia",      icon = 0 },
  { id = "sword", name = "Spada",      icon = 1 },
  { id = "hoe",   name = "Zappa",      icon = 2 },
  { id = "build", name = "Costruisci", icon = 3 },
  { id = "fire",  name = "Falo'",      icon = 4 },
  { id = "chest", name = "Cassa",      icon = 9 },
  { id = "bench", name = "Banco",      icon = 10 },
  { id = "fence", name = "Recinto",    icon = 11 },
}

local CHEST_ITEMS = {
  { field = "wood",    label = "Legna" },
  { field = "stone",   label = "Pietra" },
  { field = "iron",    label = "Ferro" },
  { field = "berries", label = "Bacche" },
  { field = "meat",    label = "Carne" },
  { field = "crops",   label = "Grano" },
  { field = "seeds",   label = "Semi" },
}

local BENCH_RECIPES = {
  { name = "Ascia Rinforzata (+1)", wood = 4, iron = 3, kind = "axe" },
  { name = "Spada d'Acciaio (+10)", wood = 2, iron = 5, kind = "sword" },
  { name = "Razione (+2 Carne)",    crops = 1, berries = 1, kind = "ration" },
  { name = "Benda (+30 HP)",        berries = 2, wood = 2, kind = "heal" },
}

local mode = "title" -- "title", "lobby", "play", "dead"
local title_sel = 1
local lobby_msg = ""
local is_coop = false
local rng

-- World state (deterministic, synced via lockstep in co-op)
local world_tick = 0
local day_num = 1
local season_idx = 1
local ground = {}
local props = {}
local buildings = {}
local bgrid = {}
local fauna = {}
local particles = {}
local players = {}
local prev_inputs = {}
local my_seat = 1
local dead_stats = nil

-- Local camera & UI
local cam_x, cam_y = 0, 0
local toast_text, toast_timer = "", 0

local function pkey(gx, gy) return gy * MAP_W + gx end
local function clamp(v, lo, hi) return v < lo and lo or (v > hi and hi or v) end

local function toast(msg)
  toast_text = msg
  toast_timer = 150
end

local function play_sfx(kind)
  if not note then return end
  if kind == "chop" then note(0, 140, 0.07, 2, 0.35)
  elseif kind == "hit" then note(0, 220, 0.08, 1, 0.4)
  elseif kind == "hurt" then note(1, 110, 0.14, 2, 0.5)
  elseif kind == "build" then note(2, 330, 0.12, 0, 0.4)
  elseif kind == "eat" then note(2, 520, 0.09, 0, 0.3)
  elseif kind == "dash" then note(1, 280, 0.06, 3, 0.25)
  end
end

local function spawn_particle(x, y, col, vx, vy, life)
  if #particles > 120 then table.remove(particles, 1) end
  particles[#particles + 1] = { x = x, y = y, col = col, vx = vx, vy = vy, life = life, max = life }
end

local function make_player(seat)
  local off = (seat - 1) * 20
  return {
    seat = seat,
    x = (40 * TILE) + off - 20,
    y = (41 * TILE),
    dir = 0, -- 0:down, 1:up, 2:left, 3:right
    walk_t = 0,
    hp = 100, max_hp = 100,
    stam = 100, max_stam = 100,
    hunger = 100, max_hunger = 100,
    invuln = 0,
    dash_t = 0, dash_vx = 0, dash_vy = 0,
    atk_t = 0, atk_phase = 0, atk_hit = false,
    slot = 1,
    axe_lvl = 1, sword_lvl = 1,
    wood = 12, stone = 6, iron = 0, berries = 3, meat = 0, crops = 0, seeds = 4,
    build_w = 4, build_h = 3,
    ui = nil,
    alive = true,
    days_lived = 0,
    kills = 0,
  }
end

local function spawn_fauna_for_biome(gx, gy, gtype)
  local kind
  if gtype == "meadow" or gtype == "meadow_fl" then
    kind = "deer"
  elseif gtype == "forest" then
    kind = (rng() < 0.6) and "wolf" or "boar"
  elseif gtype == "rock" then
    kind = "boar"
  end
  if not kind then return end
  local hp = (kind == "boar") and 45 or ((kind == "wolf") and 35 or 25)
  fauna[#fauna + 1] = {
    kind = kind,
    x = gx * TILE + 8,
    y = gy * TILE + 8,
    vx = 0, vy = 0,
    hp = hp, max_hp = hp,
    state_t = rng(30, 120),
    hurt_t = 0,
    atk_cd = 0,
  }
end

local function generate_world(seed, seat_count)
  rng = lib.rng(seed)
  world_tick = 0
  day_num = 1
  season_idx = 1
  ground = {}
  props = {}
  buildings = {}
  bgrid = {}
  fauna = {}
  particles = {}
  players = {}
  prev_inputs = {}

  for y = 0, MAP_H - 1 do
    ground[y] = {}
    for x = 0, MAP_W - 1 do
      local dx = x - 40
      local dy = y - 40
      local dist = math.sqrt(dx * dx + dy * dy)
      local n = math.sin(x * 0.14 + seed * 0.01) + math.cos(y * 0.14 - seed * 0.02)
      local g = "meadow"
      if y > 62 + n * 3 then
        g = (y > 65 + n * 3) and "water" or "sand"
      elseif x > 56 + n * 3 then
        g = "rock"
      elseif (x < 26 + n * 3 or y < 22 + n * 3) and dist > 11 then
        g = "forest"
      elseif dist < 8 then
        g = (rng() < 0.22) and "meadow_fl" or "meadow"
      else
        g = (rng() < 0.25) and "meadow_fl" or "meadow"
      end
      -- dirt path from center southwards
      if math.abs(x - 40) <= 1 and y >= 40 and y <= 63 then g = "dirt" end
      ground[y][x] = g

      -- Props
      if dist > 5 and g ~= "water" and g ~= "sand" and g ~= "dirt" then
        local r = rng()
        if g == "forest" then
          if r < 0.18 then
            props[pkey(x, y)] = { kind = (rng() < 0.35) and "tree_birch" or "tree_oak", hp = 3 }
          elseif r < 0.23 then
            props[pkey(x, y)] = { kind = "bush", hp = 1 }
          end
        elseif g == "meadow" or g == "meadow_fl" then
          if r < 0.045 then
            props[pkey(x, y)] = { kind = (rng() < 0.4) and "tree_birch" or "tree_oak", hp = 3 }
          elseif r < 0.08 then
            props[pkey(x, y)] = { kind = "bush", hp = 1 }
          elseif r < 0.095 then
            props[pkey(x, y)] = { kind = "boulder", hp = 4 }
          end
        elseif g == "rock" then
          if r < 0.08 then
            props[pkey(x, y)] = { kind = "iron_ore", hp = 5 }
          elseif r < 0.18 then
            props[pkey(x, y)] = { kind = "boulder", hp = 4 }
          end
        end
      end
    end
  end

  -- Starter campfire in the center clearing
  props[pkey(40, 39)] = { kind = "campfire", hp = 99 }

  -- Initial fauna
  for i = 1, 24 do
    local gx = rng(6, MAP_W - 7)
    local gy = rng(6, MAP_H - 18)
    local d = math.abs(gx - 40) + math.abs(gy - 40)
    if d > 10 and not props[pkey(gx, gy)] then
      spawn_fauna_for_biome(gx, gy, ground[gy][gx])
    end
  end

  for s = 1, seat_count do
    players[s] = make_player(s)
    prev_inputs[s] = 0
  end
end

local function is_solid_tile(gx, gy)
  if gx < 0 or gy < 0 or gx >= MAP_W or gy >= MAP_H then return true end
  if ground[gy][gx] == "water" then return true end
  local b = bgrid[pkey(gx, gy)]
  if b and b.is_solid then return true end
  local p = props[pkey(gx, gy)]
  if p and (p.kind == "tree_oak" or p.kind == "tree_birch" or p.kind == "boulder" or
            p.kind == "iron_ore" or p.kind == "chest" or p.kind == "workbench" or p.kind == "fence") then
    return true
  end
  return false
end

local function can_walk(px, py)
  local x0 = math.floor((px - 5) / TILE)
  local x1 = math.floor((px + 4) / TILE)
  local y0 = math.floor((py - 4) / TILE)
  local y1 = math.floor((py + 2) / TILE)
  for gy = y0, y1 do
    for gx = x0, x1 do
      if is_solid_tile(gx, gy) then return false end
    end
  end
  return true
end

local function front_tile(pl)
  local gx = math.floor(pl.x / TILE)
  local gy = math.floor(pl.y / TILE)
  if pl.dir == 0 then gy = gy + 1
  elseif pl.dir == 1 then gy = gy - 1
  elseif pl.dir == 2 then gx = gx - 1
  else gx = gx + 1 end
  return clamp(gx, 0, MAP_W - 1), clamp(gy, 0, MAP_H - 1)
end

local function build_cost(w, h)
  return w * 2 + h, w + h -- wood, stone
end

local function can_place_building(gx, gy, w, h)
  if gx < 1 or gy < 1 or gx + w >= MAP_W - 1 or gy + h >= MAP_H - 1 then return false end
  for y = gy, gy + h - 1 do
    for x = gx, gx + w - 1 do
      if ground[y][x] == "water" then return false end
      if props[pkey(x, y)] or bgrid[pkey(x, y)] then return false end
    end
  end
  return true
end

local function place_building(gx, gy, w, h)
  local door_x = gx + math.floor(w / 2)
  local b = { gx = gx, gy = gy, w = w, h = h, door_x = door_x }
  buildings[#buildings + 1] = b
  for y = gy, gy + h - 1 do
    for x = gx, gx + w - 1 do
      local is_edge = (x == gx or x == gx + w - 1 or y == gy or y == gy + h - 1)
      local is_door = (y == gy + h - 1 and x == door_x)
      local is_solid = is_edge and not is_door
      bgrid[pkey(x, y)] = { b = b, is_door = is_door, is_solid = is_solid, is_interior = not is_edge }
      ground[y][x] = "dirt"
    end
  end
end

local function player_in_building(pl, b)
  if not pl or not pl.alive then return false end
  return pl.x >= b.gx * TILE and pl.x < (b.gx + b.w) * TILE and
         pl.y >= b.gy * TILE and pl.y < (b.gy + b.h) * TILE
end

local function near_warmth(px, py)
  local gx, gy = math.floor(px / TILE), math.floor(py / TILE)
  for dy = -4, 4 do
    for dx = -4, 4 do
      local k = pkey(gx + dx, gy + dy)
      local p = props[k]
      if p and p.kind == "campfire" then return true end
      if bgrid[k] then return true end
    end
  end
  return false
end

local function ecosystem_new_day()
  day_num = day_num + 1
  if (day_num - 1) % DAYS_PER_SEASON == 0 then
    season_idx = (season_idx % 4) + 1
    toast("Nuova stagione: " .. SEASONS[season_idx] .. "!")
  else
    toast("Giorno " .. day_num .. " - " .. SEASONS[season_idx])
  end

  for _, pl in ipairs(players) do
    if pl.alive then pl.days_lived = pl.days_lived + 1 end
  end

  -- Regrow stumps, bushes, crops
  for k, p in pairs(props) do
    if p.kind == "stump" then
      p.timer = (p.timer or 0) + 1
      if p.timer >= 2 and season_idx ~= 4 then
        p.kind = "tree_oak"
        p.hp = 3
        p.timer = nil
      end
    elseif p.kind == "bush_empty" and season_idx ~= 4 then
      p.kind = "bush"
      p.hp = 1
    elseif p.kind == "crop" and p.stage < 3 and season_idx ~= 4 then
      p.stage = p.stage + 1
    end
  end

  -- Repopulate fauna up to seasonal cap
  local cap = (season_idx == 4) and 14 or 26
  if #fauna < cap then
    for _ = 1, 4 do
      local gx = rng(6, MAP_W - 7)
      local gy = rng(6, MAP_H - 18)
      if not props[pkey(gx, gy)] and not bgrid[pkey(gx, gy)] then
        spawn_fauna_for_biome(gx, gy, ground[gy][gx])
      end
    end
  end
end

local function do_primary_action(pl)
  local gx, gy = front_tile(pl)
  local k = pkey(gx, gy)
  local p = props[k]
  local slot_id = HOTBAR[pl.slot].id

  -- Interact with Chest or Workbench if facing one
  if p and (p.kind == "chest" or p.kind == "workbench") then
    pl.ui = { kind = p.kind, key = k, sel = 1 }
    return
  end

  local can_place_here = (not p) and (ground[gy][gx] ~= "water") and ((not bgrid[k]) or bgrid[k].is_interior)

  if slot_id == "axe" then
    if p and (p.kind == "tree_oak" or p.kind == "tree_birch") then
      p.hp = p.hp - pl.axe_lvl
      play_sfx("chop")
      for _ = 1, 5 do
        spawn_particle(gx * TILE + 8, gy * TILE + 8, 0x94663E, (rng() - 0.5) * 2, -rng() * 2, 18)
      end
      if p.hp <= 0 then
        p.kind = "stump"
        p.timer = 0
        local gain = 3 + pl.axe_lvl
        pl.wood = pl.wood + gain
        pl.seeds = pl.seeds + 1
        toast("+" .. gain .. " Legna, +1 Seme")
      end
    elseif p and p.kind == "boulder" then
      p.hp = p.hp - pl.axe_lvl
      play_sfx("chop")
      for _ = 1, 5 do
        spawn_particle(gx * TILE + 8, gy * TILE + 8, 0x8C8E8C, (rng() - 0.5) * 2, -rng() * 2, 18)
      end
      if p.hp <= 0 then
        props[k] = nil
        local gain = 3 + pl.axe_lvl
        pl.stone = pl.stone + gain
        toast("+" .. gain .. " Pietra")
      end
    elseif p and p.kind == "iron_ore" then
      p.hp = p.hp - pl.axe_lvl
      play_sfx("chop")
      for _ = 1, 5 do
        spawn_particle(gx * TILE + 8, gy * TILE + 8, 0xC4D0DA, (rng() - 0.5) * 2, -rng() * 2, 18)
      end
      if p.hp <= 0 then
        props[k] = nil
        local gain = 1 + pl.axe_lvl
        pl.iron = pl.iron + gain
        toast("+" .. gain .. " Ferro")
      end
    elseif p and p.kind == "bush" then
      p.kind = "bush_empty"
      pl.berries = pl.berries + 2
      play_sfx("eat")
      toast("+2 Bacche")
    elseif p and p.kind == "fence" then
      props[k] = nil
      pl.wood = pl.wood + 2
      play_sfx("chop")
    end
  elseif slot_id == "sword" then
    if pl.atk_t == 0 and pl.stam >= 15 then
      pl.stam = pl.stam - 15
      pl.atk_t = 18 -- 4 startup, 6 active, 8 recovery
      pl.atk_hit = false
      play_sfx("hit")
    end
  elseif slot_id == "hoe" then
    if p and p.kind == "crop" and p.stage >= 3 then
      props[k] = nil
      pl.crops = pl.crops + 2
      pl.seeds = pl.seeds + 2
      play_sfx("eat")
      toast("+2 Grano, +2 Semi")
    elseif not p and not bgrid[k] and (ground[gy][gx] == "meadow" or ground[gy][gx] == "meadow_fl") then
      ground[gy][gx] = "tilled"
      play_sfx("chop")
    elseif not p and ground[gy][gx] == "tilled" and pl.seeds > 0 then
      pl.seeds = pl.seeds - 1
      props[k] = { kind = "crop", stage = 1 }
      play_sfx("build")
      toast("Piantato!")
    end
  elseif slot_id == "build" then
    local wc, sc = build_cost(pl.build_w, pl.build_h)
    local bx = gx - math.floor(pl.build_w / 2)
    local by = gy - pl.build_h + 1
    if pl.dir == 0 then by = gy end
    if pl.wood >= wc and pl.stone >= sc and can_place_building(bx, by, pl.build_w, pl.build_h) then
      pl.wood = pl.wood - wc
      pl.stone = pl.stone - sc
      place_building(bx, by, pl.build_w, pl.build_h)
      play_sfx("build")
      toast("Edificio " .. pl.build_w .. "x" .. pl.build_h .. " costruito!")
    else
      toast("Serve " .. wc .. " Legna, " .. sc .. " Pietra o spazio libero")
    end
  elseif slot_id == "fire" then
    if can_place_here and pl.wood >= 3 and pl.stone >= 2 then
      pl.wood = pl.wood - 3
      pl.stone = pl.stone - 2
      props[k] = { kind = "campfire", hp = 99 }
      play_sfx("build")
      toast("Falo' acceso!")
    else
      toast("Serve 3 Legna, 2 Pietra")
    end
  elseif slot_id == "chest" then
    if can_place_here and pl.wood >= 4 then
      pl.wood = pl.wood - 4
      props[k] = { kind = "chest", inv = { wood = 0, stone = 0, iron = 0, berries = 0, meat = 0, crops = 0, seeds = 0 } }
      play_sfx("build")
      toast("Cassa condivisa piazzata!")
    else
      toast("Serve 4 Legna e spazio libero")
    end
  elseif slot_id == "bench" then
    if can_place_here and pl.wood >= 6 and pl.stone >= 3 then
      pl.wood = pl.wood - 6
      pl.stone = pl.stone - 3
      props[k] = { kind = "workbench" }
      play_sfx("build")
      toast("Banco da lavoro piazzato!")
    else
      toast("Serve 6 Legna, 3 Pietra")
    end
  elseif slot_id == "fence" then
    if can_place_here and pl.wood >= 2 then
      pl.wood = pl.wood - 2
      props[k] = { kind = "fence" }
      play_sfx("build")
    else
      toast("Serve 2 Legna")
    end
  end
end

local function step_player(pl, inp)
  if not pl.alive then return end
  local prev = prev_inputs[pl.seat] or 0
  local function just(b) return net.held(inp, b) and not net.held(prev, b) end

  -- Interactive UI (Shared Chest / Workbench)
  if pl.ui then
    local p = props[pl.ui.key]
    if not p or just("b") or just("x") then
      pl.ui = nil
      prev_inputs[pl.seat] = inp
      return
    end
    if pl.ui.kind == "chest" then
      if just("up") then pl.ui.sel = ((pl.ui.sel - 2) % #CHEST_ITEMS) + 1 end
      if just("down") then pl.ui.sel = (pl.ui.sel % #CHEST_ITEMS) + 1 end
      local f = CHEST_ITEMS[pl.ui.sel].field
      if (just("right") or just("a")) and pl[f] > 0 then
        pl[f] = pl[f] - 1
        p.inv[f] = p.inv[f] + 1
        play_sfx("eat")
      elseif just("left") and p.inv[f] > 0 then
        p.inv[f] = p.inv[f] - 1
        pl[f] = pl[f] + 1
        play_sfx("eat")
      end
    elseif pl.ui.kind == "workbench" then
      if just("up") then pl.ui.sel = ((pl.ui.sel - 2) % #BENCH_RECIPES) + 1 end
      if just("down") then pl.ui.sel = (pl.ui.sel % #BENCH_RECIPES) + 1 end
      if just("a") then
        local r = BENCH_RECIPES[pl.ui.sel]
        local ok = pl.wood >= (r.wood or 0) and pl.stone >= (r.stone or 0) and
                   pl.iron >= (r.iron or 0) and pl.crops >= (r.crops or 0) and pl.berries >= (r.berries or 0)
        if ok then
          pl.wood = pl.wood - (r.wood or 0)
          pl.stone = pl.stone - (r.stone or 0)
          pl.iron = pl.iron - (r.iron or 0)
          pl.crops = pl.crops - (r.crops or 0)
          pl.berries = pl.berries - (r.berries or 0)
          if r.kind == "axe" then pl.axe_lvl = pl.axe_lvl + 1
          elseif r.kind == "sword" then pl.sword_lvl = pl.sword_lvl + 1
          elseif r.kind == "ration" then pl.meat = pl.meat + 2
          elseif r.kind == "heal" then pl.hp = clamp(pl.hp + 30, 0, pl.max_hp)
          end
          play_sfx("build")
          toast("Creato: " .. r.name)
        else
          toast("Materiali insufficienti!")
        end
      end
    end
    prev_inputs[pl.seat] = inp
    return
  end

  -- Hotbar cycle
  if just("l1") then pl.slot = ((pl.slot - 2) % #HOTBAR) + 1 end
  if just("r1") then pl.slot = (pl.slot % #HOTBAR) + 1 end

  -- Build mode resize when holding X in build slot
  if HOTBAR[pl.slot].id == "build" and net.held(inp, "x") then
    if just("left")  then pl.build_w = clamp(pl.build_w - 1, 3, 8) end
    if just("right") then pl.build_w = clamp(pl.build_w + 1, 3, 8) end
    if just("up")    then pl.build_h = clamp(pl.build_h + 1, 3, 7) end
    if just("down")  then pl.build_h = clamp(pl.build_h - 1, 3, 7) end
  else
    if just("x") then
      pl.slot = (HOTBAR[pl.slot].id == "build") and 1 or 4
    end
    -- Movement
    if pl.dash_t > 0 then
      pl.dash_t = pl.dash_t - 1
      if can_walk(pl.x + pl.dash_vx, pl.y) then pl.x = pl.x + pl.dash_vx end
      if can_walk(pl.x, pl.y + pl.dash_vy) then pl.y = pl.y + pl.dash_vy end
    elseif pl.atk_t == 0 then
      local dx, dy = 0, 0
      if net.held(inp, "left")  then dx = dx - 1; pl.dir = 2 end
      if net.held(inp, "right") then dx = dx + 1; pl.dir = 3 end
      if net.held(inp, "up")    then dy = dy - 1; pl.dir = 1 end
      if net.held(inp, "down")  then dy = dy + 1; pl.dir = 0 end
      if dx ~= 0 and dy ~= 0 then dx = dx * 0.707; dy = dy * 0.707 end
      local spd = 1.65
      if dx ~= 0 or dy ~= 0 then
        pl.walk_t = pl.walk_t + 1
        local nx, ny = pl.x + dx * spd, pl.y + dy * spd
        if can_walk(nx, pl.y) then pl.x = nx end
        if can_walk(pl.x, ny) then pl.y = ny end
      else
        pl.walk_t = 0
      end
      if just("y") and pl.stam >= 25 then
        pl.stam = pl.stam - 25
        pl.dash_t = 9
        pl.invuln = 12
        local ddx = (pl.dir == 2 and -1 or (pl.dir == 3 and 1 or 0))
        local ddy = (pl.dir == 1 and -1 or (pl.dir == 0 and 1 or 0))
        pl.dash_vx, pl.dash_vy = ddx * 3.5, ddy * 3.5
        play_sfx("dash")
      end
    end
  end

  if just("a") then do_primary_action(pl) end

  -- Eat food (B)
  if just("b") and (pl.hunger < 95 or pl.hp < pl.max_hp) then
    if pl.berries > 0 then
      pl.berries = pl.berries - 1
      pl.hunger = clamp(pl.hunger + 20, 0, 100)
      pl.hp = clamp(pl.hp + 10, 0, pl.max_hp)
      play_sfx("eat")
    elseif pl.crops > 0 then
      pl.crops = pl.crops - 1
      pl.hunger = clamp(pl.hunger + 30, 0, 100)
      pl.hp = clamp(pl.hp + 15, 0, pl.max_hp)
      play_sfx("eat")
    elseif pl.meat > 0 then
      pl.meat = pl.meat - 1
      pl.hunger = clamp(pl.hunger + 40, 0, 100)
      pl.hp = clamp(pl.hp + 25, 0, pl.max_hp)
      play_sfx("eat")
    end
  end

  -- Sword attack 3-phase (18..15 startup, 14..9 active, 8..1 recovery)
  if pl.atk_t > 0 then
    pl.atk_t = pl.atk_t - 1
    if pl.atk_t >= 9 and pl.atk_t <= 14 and not pl.atk_hit then
      local gx, gy = front_tile(pl)
      local hx, hy = gx * TILE + 8, gy * TILE + 8
      for i = #fauna, 1, -1 do
        local f = fauna[i]
        if math.abs(f.x - hx) < 18 and math.abs(f.y - hy) < 18 then
          f.hp = f.hp - (15 + (pl.sword_lvl - 1) * 10)
          f.hurt_t = 10
          pl.atk_hit = true
          play_sfx("hit")
          for _ = 1, 6 do
            spawn_particle(f.x, f.y, 0xD43640, (rng() - 0.5) * 2.5, -rng() * 2, 16)
          end
          if f.hp <= 0 then
            table.remove(fauna, i)
            pl.meat = pl.meat + 2
            pl.kills = pl.kills + 1
            toast("+2 Carne")
          end
          break
        end
      end
    end
  end

  -- Stamina regen & Hunger/Night cold drain
  if pl.stam < pl.max_stam then pl.stam = clamp(pl.stam + 0.35, 0, pl.max_stam) end
  if pl.invuln > 0 then pl.invuln = pl.invuln - 1 end

  local day_frac = (world_tick % DAY_LEN) / DAY_LEN
  local is_night = (day_frac >= 0.72 and day_frac <= 0.96)
  local warm = near_warmth(pl.x, pl.y)
  local drain = (is_night and not warm) and 0.025 or 0.008
  if season_idx == 4 and not warm then drain = drain * 1.6 end
  pl.hunger = math.max(0, pl.hunger - drain)

  if pl.hunger <= 0 and (world_tick % 45 == 0) then
    pl.hp = pl.hp - 2
  elseif warm and pl.hunger > 50 and pl.hp < pl.max_hp and (world_tick % 90 == 0) then
    pl.hp = pl.hp + 1
  end

  -- Permadeath check
  if pl.hp <= 0 then
    pl.alive = false
    local gx = clamp(math.floor(pl.x / TILE), 1, MAP_W - 2)
    local gy = clamp(math.floor(pl.y / TILE), 1, MAP_H - 2)
    props[pkey(gx, gy)] = { kind = "grave" }
    play_sfx("hurt")
    if pl.seat == my_seat then
      dead_stats = { days = pl.days_lived, kills = pl.kills }
      mode = "dead"
    end
  end

  prev_inputs[pl.seat] = inp
end

local function step_fauna()
  local day_frac = (world_tick % DAY_LEN) / DAY_LEN
  local is_night = (day_frac >= 0.72 and day_frac <= 0.96)
  for _, f in ipairs(fauna) do
    if f.hurt_t > 0 then f.hurt_t = f.hurt_t - 1 end
    if f.atk_cd > 0 then f.atk_cd = f.atk_cd - 1 end
    f.state_t = f.state_t - 1

    -- Find nearest alive player
    local near_p, near_d = nil, 9999
    for _, pl in ipairs(players) do
      if pl.alive then
        local d = math.abs(pl.x - f.x) + math.abs(pl.y - f.y)
        if d < near_d then near_d = d; near_p = pl end
      end
    end

    local agro_r = (f.kind == "wolf" and is_night) and 140 or 90
    if near_p and near_d < agro_r then
      local dx, dy = near_p.x - f.x, near_p.y - f.y
      local len = math.max(1, math.sqrt(dx * dx + dy * dy))
      if f.kind == "deer" then
        f.vx, f.vy = -(dx / len) * 1.3, -(dy / len) * 1.3
      else
        local spd = (f.kind == "wolf") and 1.15 or 0.95
        f.vx, f.vy = (dx / len) * spd, (dy / len) * spd
        if near_d < 14 and f.atk_cd == 0 and near_p.invuln == 0 then
          near_p.hp = near_p.hp - ((f.kind == "wolf") and 14 or 18)
          near_p.invuln = 30
          f.atk_cd = 50
          play_sfx("hurt")
        end
      end
    elseif f.state_t <= 0 then
      f.state_t = rng(45, 130)
      if rng() < 0.5 then
        f.vx, f.vy = (rng() - 0.5) * 0.9, (rng() - 0.5) * 0.9
      else
        f.vx, f.vy = 0, 0
      end
    end

    local nx, ny = f.x + f.vx, f.y + f.vy
    if can_walk(nx, f.y) then f.x = nx else f.vx = -f.vx end
    if can_walk(f.x, ny) then f.y = ny else f.vy = -f.vy end
  end
end

local function step_world(inputs)
  world_tick = world_tick + 1
  if world_tick % DAY_LEN == 0 then
    ecosystem_new_day()
  end

  for s, pl in ipairs(players) do
    local inp = inputs[s]
    if inp ~= false and inp ~= nil then
      step_player(pl, inp)
    end
  end

  step_fauna()

  for i = #particles, 1, -1 do
    local pt = particles[i]
    pt.x = pt.x + pt.vx
    pt.y = pt.y + pt.vy
    pt.life = pt.life - 1
    if pt.life <= 0 then table.remove(particles, i) end
  end
end

local function draw_tile(tx, ty, dx, dy)
  sspr(tx * 16, ty * 16, 16, 16, dx, dy)
end

local function draw_world()
  local lp = players[my_seat] or players[1]
  local target_cx = clamp(math.floor(lp.x - SCREEN_W / 2), 0, MAP_W * TILE - SCREEN_W)
  local target_cy = clamp(math.floor(lp.y - SCREEN_H / 2), 0, MAP_H * TILE - SCREEN_H)
  cam_x = cam_x + (target_cx - cam_x) * 0.2
  cam_y = cam_y + (target_cy - cam_y) * 0.2
  local cx, cy = math.floor(cam_x), math.floor(cam_y)
  camera(cx, cy)

  local x0 = clamp(math.floor(cx / TILE) - 1, 0, MAP_W - 1)
  local y0 = clamp(math.floor(cy / TILE) - 1, 0, MAP_H - 1)
  local x1 = clamp(math.floor((cx + SCREEN_W) / TILE) + 1, 0, MAP_W - 1)
  local y1 = clamp(math.floor((cy + SCREEN_H) / TILE) + 1, 0, MAP_H - 1)

  local wf = math.floor(world_tick / 25) % 2
  for y = y0, y1 do
    for x = x0, x1 do
      local g = ground[y][x]
      local tx = 0
      if g == "meadow" then tx = (season_idx == 3 and 9) or (season_idx == 4 and 10) or 0
      elseif g == "meadow_fl" then tx = (season_idx == 3 and 9) or (season_idx == 4 and 10) or 1
      elseif g == "forest" then tx = (season_idx == 4 and 10) or 2
      elseif g == "rock" then tx = 3
      elseif g == "sand" then tx = 4
      elseif g == "water" then tx = 5 + wf
      elseif g == "dirt" then tx = 7
      elseif g == "tilled" then tx = 8
      end
      draw_tile(tx, 0, x * TILE, y * TILE)
    end
  end

  -- Draw interior wooden floors for buildings the local player is inside
  local inside_map = {}
  for _, b in ipairs(buildings) do
    if player_in_building(lp, b) then
      inside_map[b] = true
      for ry = 0, b.h - 1 do
        for rx = 0, b.w - 1 do
          local wx, wy = (b.gx + rx) * TILE, (b.gy + ry) * TILE
          if (rx > 0 and rx < b.w - 1 and ry > 0 and ry < b.h - 1) or (ry == b.h - 1 and (b.gx + rx) == b.door_x) then
            draw_tile(9, 1, wx, wy)
          end
        end
      end
    end
  end

  -- Collect Y-sorted drawables
  local draw_list = {}
  for y = y0 - 2, y1 + 2 do
    for x = x0 - 1, x1 + 1 do
      if x >= 0 and y >= 0 and x < MAP_W and y < MAP_H then
        local p = props[pkey(x, y)]
        if p then
          draw_list[#draw_list + 1] = { y = y * TILE + 15, kind = "prop", x = x, gy = y, p = p }
        end
      end
    end
  end

  for _, b in ipairs(buildings) do
    if b.gx + b.w >= x0 and b.gx <= x1 and b.gy + b.h >= y0 and b.gy <= y1 then
      if inside_map[b] then
        -- Cutaway mode: add each perimeter wall row/tile to Y-sort so the player can walk behind/in front of walls
        for ry = 0, b.h - 1 do
          for rx = 0, b.w - 1 do
            local is_edge = (rx == 0 or rx == b.w - 1 or ry == 0 or ry == b.h - 1)
            local is_door = (ry == b.h - 1 and (b.gx + rx) == b.door_x)
            if is_edge and not is_door then
              draw_list[#draw_list + 1] = {
                y = (b.gy + ry) * TILE + 14,
                kind = "bwall",
                wx = (b.gx + rx) * TILE,
                wy = (b.gy + ry) * TILE,
                rx = rx, ry = ry, b = b
              }
            end
          end
        end
      else
        draw_list[#draw_list + 1] = { y = (b.gy + b.h - 1) * TILE + 14, kind = "building", b = b }
      end
    end
  end

  for _, f in ipairs(fauna) do
    draw_list[#draw_list + 1] = { y = f.y + 4, kind = "fauna", f = f }
  end

  for _, pl in ipairs(players) do
    if pl.alive then
      draw_list[#draw_list + 1] = { y = pl.y + 4, kind = "player", pl = pl }
    end
  end

  table.sort(draw_list, function(a, b) return a.y < b.y end)

  for _, item in ipairs(draw_list) do
    if item.kind == "prop" then
      local p, px0, py0 = item.p, item.x * TILE, item.gy * TILE
      if p.kind == "tree_oak" then
        local sx = (season_idx == 3) and 64 or 0
        sspr(sx, 48, 32, 32, px0 - 8, py0 - 16)
      elseif p.kind == "tree_birch" then
        sspr(32, 48, 32, 32, px0 - 8, py0 - 16)
      elseif p.kind == "boulder" then draw_tile(0, 2, px0, py0)
      elseif p.kind == "iron_ore" then draw_tile(12, 2, px0, py0)
      elseif p.kind == "bush" then draw_tile(1, 2, px0, py0)
      elseif p.kind == "bush_empty" then draw_tile(2, 2, px0, py0)
      elseif p.kind == "stump" then draw_tile(3, 2, px0, py0)
      elseif p.kind == "campfire" then draw_tile(4 + (math.floor(world_tick / 10) % 2), 2, px0, py0)
      elseif p.kind == "grave" then draw_tile(6, 2, px0, py0)
      elseif p.kind == "crop" then draw_tile(6 + p.stage, 2, px0, py0)
      elseif p.kind == "chest" then draw_tile(10, 2, px0, py0)
      elseif p.kind == "workbench" then draw_tile(11, 2, px0, py0)
      elseif p.kind == "fence" then draw_tile(8, 1, px0, py0)
      end
    elseif item.kind == "bwall" then
      -- Cutaway perimeter wall (roof removed)
      if item.ry == 0 then
        draw_tile((item.rx > 0 and item.rx < item.b.w - 1) and 4 or 3, 1, item.wx, item.wy)
      elseif item.ry == item.b.h - 1 then
        draw_tile((item.rx % 2 == 0) and 2 or 0, 1, item.wx, item.wy)
      else
        -- Side wall cutaway: compact stone+timber pillar so interior stays open
        local sx = (item.rx == 0) and item.wx or (item.wx + 10)
        rectfill(sx, item.wy, 6, 16, 0x686A6C)
        rectfill(sx + 1, item.wy + 1, 4, 14, 0x94663E)
      end
    elseif item.kind == "building" then
      local b = item.b
      for ry = 0, b.h - 1 do
        for rx = 0, b.w - 1 do
          local wx, wy = (b.gx + rx) * TILE, (b.gy + ry) * TILE
          if ry == b.h - 1 then
            local tx = ((b.gx + rx) == b.door_x) and 1 or ((rx % 2 == 0) and 2 or 0)
            draw_tile(tx, 1, wx, wy)
          elseif ry == b.h - 2 and b.h >= 3 then
            local tx = (rx > 0 and rx < b.w - 1) and 4 or 3
            draw_tile(tx, 1, wx, wy)
          else
            local tx = (ry == 0) and 6 or ((ry == b.h - 3 or (b.h == 2 and ry == 0)) and 7 or 5)
            draw_tile(tx, 1, wx, wy)
          end
        end
      end
    elseif item.kind == "fauna" then
      local f = item.f
      local tx = (f.kind == "deer") and 0 or ((f.kind == "wolf") and 1 or 2)
      sspr(tx * 16, 176, 16, 16, math.floor(f.x - 8), math.floor(f.y - 11), f.vx < 0)
    elseif item.kind == "player" then
      local pl = item.pl
      if pl.invuln % 4 < 2 then
        local fr = (pl.walk_t > 0) and (math.floor(pl.walk_t / 8) % 2) or 0
        local sx = (pl.dir * 2 + fr) * 16
        local sy = 80 + ((pl.seat - 1) % 4) * 24
        sspr(sx, sy, 16, 24, math.floor(pl.x - 8), math.floor(pl.y - 19))
        -- Sword swing visual
        if pl.atk_t >= 9 and pl.atk_t <= 14 then
          local gx, gy = front_tile(pl)
          sspr(16, 192, 16, 16, gx * TILE, gy * TILE, pl.dir == 2)
        end
      end
    end
  end

  -- Particles
  for _, pt in ipairs(particles) do
    rectfill(math.floor(pt.x), math.floor(pt.y), 2, 2, pt.col)
  end

  -- Build mode blueprint preview
  if lp.alive and HOTBAR[lp.slot].id == "build" then
    local gx, gy = front_tile(lp)
    local bx = gx - math.floor(lp.build_w / 2)
    local by = gy - lp.build_h + 1
    if lp.dir == 0 then by = gy end
    local wc, sc = build_cost(lp.build_w, lp.build_h)
    local ok = lp.wood >= wc and lp.stone >= sc and can_place_building(bx, by, lp.build_w, lp.build_h)
    local col = ok and 0x58E070 or 0xE05050
    rect(bx * TILE, by * TILE, lp.build_w * TILE, lp.build_h * TILE, col)
    rect(bx * TILE + 1, by * TILE + 1, lp.build_w * TILE - 2, lp.build_h * TILE - 2, col)
  end

  -- Day/Night Lighting Overlay
  local day_frac = (world_tick % DAY_LEN) / DAY_LEN
  local night_alpha = 0
  local tint_col = 0x0C1228
  if day_frac >= 0.62 and day_frac < 0.74 then
    night_alpha = math.floor(((day_frac - 0.62) / 0.12) * 145)
    tint_col = 0x2A1828
  elseif day_frac >= 0.74 and day_frac < 0.92 then
    night_alpha = 145
  elseif day_frac >= 0.92 then
    night_alpha = math.floor((1 - (day_frac - 0.92) / 0.08) * 145)
  end

  if night_alpha > 0 then
    blend("alpha", night_alpha)
    rectfill(cx, cy, SCREEN_W, SCREEN_H, tint_col)
    blend("add", math.min(180, night_alpha + 20))
    for k, p in pairs(props) do
      if p.kind == "campfire" then
        local px0 = (k % MAP_W) * TILE + 8
        local py0 = math.floor(k / MAP_W) * TILE + 8
        radgrad(px0, py0, 72, 0xFF9030, 0x000000)
      end
    end
    for _, pl in ipairs(players) do
      if pl.alive then
        radgrad(math.floor(pl.x), math.floor(pl.y - 6), 44, 0x604828, 0x000000)
      end
    end
    blend("alpha", 255)
  end

  camera(0, 0)
end

local function draw_hud()
  local lp = players[my_seat] or players[1]
  -- Top-left survival bars
  rrectfill(6, 6, 140, 46, 4, 0x1A1614)
  rectfill(12, 12, math.floor(128 * (lp.hp / lp.max_hp)), 8, 0xD44040)
  rectfill(12, 24, math.floor(128 * (lp.stam / lp.max_stam)), 6, 0x50B868)
  rectfill(12, 34, math.floor(128 * (lp.hunger / lp.max_hunger)), 6, 0xE09C38)
  print("HP", 14, 10, 0xFFFFFF)

  -- Top-right clock & season
  local day_frac = (world_tick % DAY_LEN) / DAY_LEN
  local phase = (day_frac < 0.62) and "Giorno" or ((day_frac < 0.74) and "Tramonto" or ((day_frac < 0.92) and "Notte" or "Alba"))
  rrectfill(SCREEN_W - 154, 6, 148, 34, 4, 0x1A1614)
  print("G" .. day_num .. " " .. SEASONS[season_idx], SCREEN_W - 148, 10, 0xF2D478)
  print(phase, SCREEN_W - 148, 24, 0xC8D0DC)

  -- Resources (top-center)
  rrectfill(152, 6, 330, 24, 4, 0x1A1614)
  sspr(80, 192, 16, 16, 156, 10)  print(tostring(lp.wood), 174, 11, 0xFFFFFF)
  sspr(96, 192, 16, 16, 200, 10)  print(tostring(lp.stone), 218, 11, 0xFFFFFF)
  sspr(192, 192, 16, 16, 244, 10) print(tostring(lp.iron), 262, 11, 0xC4D0DA)
  sspr(112, 192, 16, 16, 286, 10) print(tostring(lp.berries), 304, 11, 0xFFFFFF)
  sspr(128, 192, 16, 16, 328, 10) print(tostring(lp.meat), 346, 11, 0xFFFFFF)
  print("G:" .. lp.crops .. " S:" .. lp.seeds, 376, 11, 0xE8D080)

  -- Bottom hotbar
  local hw = #HOTBAR * 34 + 8
  local hx = math.floor((SCREEN_W - hw) / 2)
  rrectfill(hx, SCREEN_H - 38, hw, 32, 4, 0x1A1614)
  for i, it in ipairs(HOTBAR) do
    local bx = hx + 6 + (i - 1) * 34
    if i == lp.slot then
      rect(bx - 2, SCREEN_H - 35, 24, 24, 0xF2D478)
    end
    sspr(it.icon * 16, 192, 16, 16, bx + 2, SCREEN_H - 31)
  end

  if HOTBAR[lp.slot].id == "build" then
    local wc, sc = build_cost(lp.build_w, lp.build_h)
    print("Edificio " .. lp.build_w .. "x" .. lp.build_h .. " (" .. wc .. "L " .. sc .. "P) | X+Frecce: ridimensiona", 120, SCREEN_H - 54, 0xF2D478)
  else
    print(HOTBAR[lp.slot].name .. " | A:Usa  B:Mangia  Y:Schiva  L1/R1:Slot", 150, SCREEN_H - 54, 0xD0D0D0)
  end

  if toast_timer > 0 then
    rrectfill(180, 46, 280, 20, 4, 0x1A1614)
    print(toast_text, 192, 49, 0xF2D478)
  end

  -- Chest / Workbench modal overlay
  if lp.ui then
    local p = props[lp.ui.key]
    if p then
      rrectfill(170, 74, 300, 178, 6, 0x141210)
      rect(170, 74, 300, 178, 0xF2D478)
      if lp.ui.kind == "chest" then
        print("CASSA CONDIVISA (Co-op / Scambio)", 186, 84, 0xF2D478)
        for i, it in ipairs(CHEST_ITEMS) do
          local col = (i == lp.ui.sel) and 0xF2D478 or 0xD8D8D8
          local pre = (i == lp.ui.sel) and "> " or "  "
          print(pre .. it.label .. ": Tu " .. lp[it.field] .. " | Cassa " .. p.inv[it.field], 190, 98 + i * 18, col)
        end
        print("Dx/A: Deposita  Sx: Preleva  B: Chiudi", 184, 232, 0xA8B0B8)
      elseif lp.ui.kind == "workbench" then
        print("BANCO DA LAVORO (Crafting)", 206, 84, 0xF2D478)
        for i, r in ipairs(BENCH_RECIPES) do
          local col = (i == lp.ui.sel) and 0xF2D478 or 0xD8D8D8
          local pre = (i == lp.ui.sel) and "> " or "  "
          print(pre .. r.name, 186, 102 + i * 24, col)
        end
        print("A: Crea   B: Chiudi", 242, 232, 0xA8B0B8)
      end
    end
  end
end

function _init()
  frameskip(4)
  net.open({ game = "WLD1", name = "Survivor" })
end

function _leave()
  net.close()
end

function _update()
  if toast_timer > 0 then toast_timer = toast_timer - 1 end

  for _, e in ipairs(net.update()) do
    if e.type == "start" then
      is_coop = true
      my_seat = e.seat
      generate_world(e.seed, e.seats)
      mode = "play"
    elseif e.type == "lost" then
      lobby_msg = "Connessione persa"
      mode = "title"
    elseif e.type == "joined" then
      lobby_msg = "Connesso! Attendi l'host..."
    elseif e.type == "refused" then
      lobby_msg = "Partita piena o gia' iniziata"
    end
  end

  if mode == "title" then
    if btnp("up") then title_sel = ((title_sel - 2) % 3) + 1 end
    if btnp("down") then title_sel = (title_sel % 3) + 1 end
    if btnp("ok") or btnp("a") then
      if title_sel == 1 then
        is_coop = false
        my_seat = 1
        generate_world(stat(3) + 1337, 1)
        mode = "play"
      elseif title_sel == 2 then
        net.host({ max = 4, info = "Wildvale Co-op" })
        lobby_msg = "Hosting... Premi START per iniziare"
        mode = "lobby"
      elseif title_sel == 3 then
        lobby_msg = "Ricerca partite LAN..."
        mode = "lobby"
      end
    end
  elseif mode == "lobby" then
    if btnp("b") or btnp("back") then
      mode = "title"
    elseif net.state == "hosting" and (btnp("start") or btnp("a")) then
      net.start({ seed = stat(3) + 42, delay = 4 })
    elseif net.state == "lobby" then
      local hosts = net.hosts()
      if #hosts > 0 and (btnp("a") or btnp("ok")) then
        net.join(hosts[1].id)
      end
    end
  elseif mode == "play" then
    if is_coop then
      net.input(net.pad(1))
      for _, inputs in net.frames() do
        step_world(inputs)
      end
    else
      step_world({ [1] = net.pad(1) })
    end
  elseif mode == "dead" then
    if is_coop then
      net.input(0)
      for _, inputs in net.frames() do step_world(inputs) end
    end
    if btnp("a") or btnp("ok") then
      players[my_seat] = make_player(my_seat)
      mode = "play"
      toast("Nuovo sopravvissuto!")
    end
  end
end

function _draw()
  cls(0x14181E)
  if mode == "title" then
    sspr(0, 48, 32, 32, 260, 48)
    sspr(32, 48, 32, 32, 348, 48)
    print("WILDVALE", 252, 92, 0xF2D478, 2)
    print("Survival Action RPG  |  Permadeath  |  Co-op", 148, 128, 0xB8C4B0)
    local opts = { "Gioca Solo", "Ospita Co-op (LAN/bmnet)", "Unisciti a Co-op" }
    for i, o in ipairs(opts) do
      local col = (i == title_sel) and 0xF2D478 or 0xD0D0D0
      print((i == title_sel and "> " or "  ") .. o, 220, 176 + i * 24, col)
    end
  elseif mode == "lobby" then
    print("WILDVALE CO-OP LOBBY", 220, 60, 0xF2D478)
    print(lobby_msg, 180, 100, 0xFFFFFF)
    local peers = net.peers()
    for i, p in ipairs(peers) do
      print(i .. ". " .. (p.name or "Player"), 220, 130 + i * 20, 0x90E090)
    end
    if net.state == "lobby" then
      local hosts = net.hosts()
      if #hosts > 0 then
        print("Trovato: " .. hosts[1].name .. " (premi A per entrare)", 170, 240, 0xF2D478)
      else
        print("Nessun host trovato ancora...", 200, 240, 0xA0A0A0)
      end
    end
    print("B: Indietro", 260, 310, 0xA0A0A0)
  elseif mode == "play" then
    draw_world()
    draw_hud()
  elseif mode == "dead" then
    draw_world()
    blend("alpha", 180)
    rectfill(0, 0, SCREEN_W, SCREEN_H, 0x200808)
    blend("alpha", 255)
    print("SEI MORTO", 248, 110, 0xE05050, 2)
    if dead_stats then
      print("Giorni sopravvissuti: " .. dead_stats.days .. "  |  Caccia: " .. dead_stats.kills, 170, 160, 0xFFFFFF)
    end
    print("Il tuo personaggio e' perso per sempre, ma il mondo resta.", 116, 190, 0xD0B8B8)
    print("Premi A / Spazio per rinascere da zero", 174, 240, 0xF2D478)
  end
end
