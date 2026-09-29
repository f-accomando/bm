-- Endless mode: a small kitchen that grows while you cook. Money comes from
-- the dishes; at the register a chef sees three offers and buys one without
-- stopping anything: orders, pots and the other chefs carry on. What is
-- bought appears in the kitchen by itself, next to its kind, never where it
-- would cut a path. Five hearts: a lost order costs one, five dishes in a
-- row give one back; the run ends with none left.

local TOP = Kit.TOP
local TIER_EARN = { 0, 220, 650, 1350 }
local TIER_TIME = { 0, 150, 420, 800 }

---------------------------------------------------------------- the pool

-- the recipes that can be ordered: unlocked, and the kitchen has what they need
local function have(run)
  local st, ing = {}, {}
  for _, s in ipairs(run.stations) do
    st[s.kind] = true
    if s.ing then ing[s.ing] = true end
  end
  return st, ing
end

local NEED = { chop = "board", boil = "pot", fry = "pan", bake = "oven", blend = "blender" }

local function makeable(r, st, ing)
  for need in pairs(r.st) do if not st[NEED[need]] then return false end end
  for i in pairs(r.ing) do if not ing[i] then return false end end
  return true
end

local function rebuild_pool(run)
  local st, ing = have(run)
  local pool = {}
  for id in pairs(run.unlocked) do
    local r = Data.RECIPE[id]
    if makeable(r, st, ing) then pool[#pool + 1] = r end
  end
  sort(pool, function(a, b) return a.n < b.n end)
  Food.set_pool(run, pool)
end
End.rebuild_pool = rebuild_pool

---------------------------------------------------------------- placing

local DIRS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }

-- the walkable cells as a table (key z * w + x), and how many
local function open_grid(run)
  local open, total, w = {}, 0, run.w
  for cz = 0, run.h - 1 do
    for cx = 0, w - 1 do
      if not Kit.blocked(Kit.cell(run, cx, cz)) then open[cz * w + cx] = true; total = total + 1 end
    end
  end
  return open, total
end

-- the cells reachable from the first spawn point, with `extra` blocked as
-- if a station stood there; and how many
local function reach(run, open, extra)
  local w = run.w
  local seen, q, n = {}, {}, 0
  local sp = run.spawns[1]
  local k0 = floor(sp[2]) * w + floor(sp[1])
  if not open[k0] or k0 == extra then return seen, 0 end
  seen[k0], q[1], n = true, k0, 1
  local i = 1
  while q[i] do
    local k = q[i]
    i = i + 1
    local x = k % w
    local j = k - w
    if open[j] and not seen[j] and j ~= extra then seen[j] = true; n = n + 1; q[n] = j end
    j = k + w
    if open[j] and not seen[j] and j ~= extra then seen[j] = true; n = n + 1; q[n] = j end
    if x > 0 then
      j = k - 1
      if open[j] and not seen[j] and j ~= extra then seen[j] = true; n = n + 1; q[n] = j end
    end
    if x < w - 1 then
      j = k + 1
      if open[j] and not seen[j] and j ~= extra then seen[j] = true; n = n + 1; q[n] = j end
    end
  end
  return seen, n
end

-- how many sides of cell (x, z) are in `seen`, and the last one
local function sides(seen, w, x, z)
  local n, last = 0, nil
  for _, d in ipairs(DIRS) do
    local nx = x + d[1]
    local k = (z + d[2]) * w + nx
    if nx >= 0 and nx < w and seen[k] then n, last = n + 1, k end
  end
  return n, last
end

-- A station at (cx, cz) must keep the kitchen whole: every floor cell still
-- reachable, and every station that could be reached before still reachable
-- from some side. `ctx` holds what does not change between candidates.
local function keeps_paths(run, cx, cz, ctx)
  local w = run.w
  local key = cz * w + cx
  local seen, n = reach(run, ctx.open, key)
  if n ~= ctx.total - 1 then return false end
  if sides(seen, w, cx, cz) == 0 then return false end
  for _, st in ipairs(ctx.served) do
    if sides(seen, w, st.cx, st.cz) == 0 then return false end
  end
  return true
end

local ZONE = {
  pot = { "pot", "pan", "oven", "blender" }, pan = { "pan", "pot", "oven" },
  oven = { "oven", "pot", "pan" }, blender = { "blender", "board", "oven" },
  board = { "board", "crate" }, counter = { "board", "serve", "plates" }, crate = { "crate" },
  trash = { "serve", "plates", "board" }, sink = { "plates" },
}

-- the best free cell for a new station of `kind`: along a wall or the other
-- stations, close to the stations it works with
local function find_spot(run, kind)
  local zone = ZONE[kind] or { kind }
  local zx, zz, zn = 0, 0, 0
  for _, k in ipairs(zone) do
    for _, st in ipairs(run.stations) do
      if st.kind == k and not st.plat then zx, zz, zn = zx + st.x, zz + st.z, zn + 1 end
    end
    if zn > 0 then break end
  end
  if zn == 0 then zx, zz, zn = run.w / 2, run.h / 2, 1 end
  zx, zz = zx / zn, zz / zn
  local cands = {}
  for cz = 0, run.h - 1 do
    for cx = 0, run.w - 1 do
      local c = Kit.cell(run, cx, cz)
      if c.kind == "floor" and not c.st and not c.debris then
        local touch, wall = false, false
        for _, d in ipairs(DIRS) do
          local n = Kit.cell(run, cx + d[1], cz + d[2])
          if n.kind == "wall" or n.st then touch = true end
          if n.kind == "wall" and cz + d[2] >= 0 then wall = true end
        end
        -- right in front of a station (the side the camera sees) is poor
        local front = Kit.cell(run, cx, cz + 1).st ~= nil
        local spawn = false
        for i = 1, 4 do
          local sp = run.spawns[i]
          if floor(sp[1]) == cx and floor(sp[2]) == cz then spawn = true end
        end
        local busy = false
        for _, ch in ipairs(run.chefs) do
          if abs(ch.x - (cx + 0.5)) < 0.9 and abs(ch.z - (cz + 0.5)) < 0.9 then busy = true end
        end
        if touch and not spawn and not busy then
          -- along the walls first, in line with the stations of its kind
          local d = sqrt(dist2(cx + 0.5, cz + 0.5, zx, zz)) + (wall and 0 or 5) + (front and 3 or 0)
          cands[#cands + 1] = { cx, cz, d }
        end
      end
    end
  end
  sort(cands, function(a, b) return a[3] < b[3] end)
  local open, total = open_grid(run)
  local before = reach(run, open)
  -- the stations that can be reached now; a cell that is the only way to
  -- one of them is out at once (the common case: right in front of it)
  local ctx, only = { open = open, total = total, served = {} }, {}
  for _, st in ipairs(run.stations) do
    if not st.plat and st.kind ~= "valve" then
      local n, last = sides(before, run.w, st.cx, st.cz)
      if n > 0 then ctx.served[#ctx.served + 1] = st end
      if n == 1 then only[last] = true end
    end
  end
  for _, p in ipairs(cands) do
    if not only[p[2] * run.w + p[1]] and keeps_paths(run, p[1], p[2], ctx) then return p[1], p[2] end
  end
  return nil
end
End.find_spot = find_spot

-- the same, remembered while the offers are drawn up (nothing moves then)
local memo
local function spot_free(run, kind)
  if not memo then return find_spot(run, kind) ~= nil end
  if memo[kind] == nil then memo[kind] = find_spot(run, kind) ~= nil end
  return memo[kind]
end

-- a second serving window: a side wall cell next to the floor
local function window_spot(run)
  local best, bd = nil, -1
  local old = Kit.find(run, "serve")[1]
  for cz = 1, run.h - 2 do
    for _, cx in ipairs({ 0, run.w - 1 }) do
      local c = Kit.cell(run, cx, cz)
      local inner = Kit.cell(run, cx == 0 and 1 or cx - 1, cz)
      if c.kind == "wall" and not c.st and not Kit.blocked(inner) then
        local d = old and dist2(cx, cz, old.x, old.z) or 0
        if d > bd then best, bd = { cx, cz }, d end
      end
    end
  end
  return best
end

-- puts a new station in the kitchen, with a drop from above
function End.place(run, kind, ing)
  local cx, cz
  if kind == "serve" then
    local p = window_spot(run)
    if not p then return nil end
    cx, cz = p[1], p[2]
    Kit.cell(run, cx, cz).kind = "floor"
  else
    cx, cz = find_spot(run, kind)
    if not cx then return nil end
  end
  local st = Kit.add_station(run, kind, cx, cz)
  st.ing = ing
  if kind == "plates" or kind == "ret" then st.n = 0 end
  if kind == "sink" then st.dirty, st.clean = 0, 0 end
  Kit.cell(run, cx, cz).st = st
  -- loose food where it lands hops aside
  for _, l in ipairs(run.loose) do
    if floor(l.x) == cx and floor(l.z) == cz then l.fly, l.vy, l.vx = true, 3, 1 end
  end
  -- the kitchen meshes still show the floor here; they are rebuilt when it lands
  st.drop = 0.7
  st.drop_mesh = Mesh.station_single(run, st)
  return st
end

---------------------------------------------------------------- offers

local function cost_of(run, u)
  local k = run.bought[u.id] or 0
  local c = (u.cost or Data.TIER_COST[u.tier]) * 1.35 ^ k
  return max(5, floor(c / 5 + 0.5) * 5)
end

local function available(run, u)
  if (run.bought[u.id] or 0) >= (u.max or 1) then return false end
  if u.heal and run.hearts >= 5 then return false end
  if u.st then
    if u.st == "serve" then return window_spot(run) ~= nil end
    if u.st == "trash" and #Kit.find(run, "trash") >= 2 then return false end
  end
  if u.flag == "no_pests" and run.tier < 3 then return false end
  return true
end

-- every offer that could be made now, with generated ones for crates and
-- recipes
local function candidates(run)
  local out = {}
  for _, u in ipairs(Data.UPGRADES) do
    if u.tier <= run.tier and available(run, u) then out[#out + 1] = u end
  end
  local st, ing = have(run)
  -- recipes whose stations and crates are all in the kitchen
  for _, r in ipairs(Data.RECIPES) do
    if not run.unlocked[r.id] and r.tier <= run.tier and makeable(r, st, ing) then
      out[#out + 1] = { id = "rec:" .. r.id, cat = "RECIPE", name = r.name, desc = "new dish: " .. r.value .. " coins",
                        tier = r.tier, cost = floor(Data.TIER_COST[r.tier] * 1.1 / 5) * 5, recipe = r.id, parts = r.parts }
    end
  end
  -- crates for ingredients that recipes of this tier use
  local wanted = {}
  for _, r in ipairs(Data.RECIPES) do
    if r.tier <= run.tier then for i in pairs(r.ing) do wanted[i] = true end end
  end
  for id in pairs(wanted) do
    if not ing[id] then
      local t = Data.ING_TIER[id] or 1
      if t <= run.tier then
        out[#out + 1] = { id = "crate:" .. id, cat = "PANTRY", name = Data.ING[id].name:upper() .. " CRATE",
                          desc = "new ingredient", tier = t, cost = floor(Data.TIER_COST[t] * 0.8 / 5) * 5,
                          crate = id, ing_icon = id }
      end
    end
  end
  return out
end

-- how often each category comes up, whatever the number of offers in it
local CAT_W = { EQUIPMENT = 3, PANTRY = 2.4, RECIPE = 2.6, SPEED = 1.6, CAPACITY = 1.1, SAFETY = 1.3, LOGISTICS = 1.1 }

local function pick_offer(run, exclude, shown)
  local cands = candidates(run)
  local per = {}
  for _, u in ipairs(cands) do
    if not exclude[u.id] then per[u.cat] = (per[u.cat] or 0) + 1 end
  end
  -- the three offers come from different categories when they can
  local other = false
  for cat in pairs(per) do if not shown[cat] then other = true end end
  local total, pool = 0, {}
  for _, u in ipairs(cands) do
    if not exclude[u.id] and not (other and shown[u.cat]) then
      local w = (CAT_W[u.cat] or 1) / per[u.cat] * (u.tier == run.tier and 2 or 1)
      if u.heal and run.hearts <= 2 then w = w * 4 end
      pool[#pool + 1] = { u, w }
      total = total + w
    end
  end
  -- a weighted draw; what needs room in the kitchen is checked only when
  -- drawn (finding a spot is the costly part)
  while total > 0 do
    local r = random() * total
    local pick = #pool
    for i, p in ipairs(pool) do
      r = r - p[2]
      if r <= 0 then pick = i break end
    end
    local u = pool[pick][1]
    local kind = u.crate and "crate" or (u.st ~= "serve" and u.st)
    if not kind or spot_free(run, kind) then return u end
    total = total - pool[pick][2]
    remove(pool, pick)
  end
  return nil
end

function End.fill_offers(run)
  memo = {}
  local ex, shown = {}, {}
  for i = 1, 3 do
    local o = run.offers[i]
    if o then ex[o.id], shown[o.cat] = true, true end
  end
  for i = 1, 3 do
    if not run.offers[i] then
      local u = pick_offer(run, ex, shown)
      if u then
        run.offers[i] = u
        ex[u.id], shown[u.cat] = true, true
      end
    end
  end
  memo = nil
end

---------------------------------------------------------------- buying

local function apply(run, u, chef)
  if u.st then
    for _ = 1, u.n or 1 do
      local st = End.place(run, u.st)
      if not st then return false end
    end
  elseif u.crate then
    if not End.place(run, "crate", u.crate) then return false end
  elseif u.recipe then
    run.unlocked[u.recipe] = true
    G.recipe_seen(u.recipe)
  end
  if u.mult then
    for k, v in pairs(u.mult) do
      if k == "money" then run.money_k = (run.money_k or 1) + v
      elseif k == "burn" then run.mult.burn = run.mult.burn + v
      else run.mult[k] = run.mult[k] + v end
    end
  end
  if u.flag then run[u.flag] = true end
  if u.plates then
    local st = Kit.find(run, "plates")[1]
    if st then st.n = st.n + u.plates end
  end
  if u.patience then run.patience_bonus = (run.patience_bonus or 0) + u.patience end
  if u.orders then run.extra_orders = (run.extra_orders or 0) + u.orders end
  if u.heal then run.hearts = min(5, run.hearts + u.heal) end
  return true
end

function End.buy(run, chef, i)
  local u = run.offers[i]
  if not u then return end
  local cost = cost_of(run, u)
  if run.coins < cost then
    Snd.nope()
    Chef.say(chef, "not enough coins", 0.9)
    return
  end
  if not apply(run, u, chef) then
    Snd.nope()
    run.offers[i] = nil
    End.fill_offers(run)
    return
  end
  run.coins = run.coins - cost
  run.spent = run.spent + cost
  run.bought[u.id] = (run.bought[u.id] or 0) + 1
  G.stats_add("upgrades", 1)
  Snd.cash()
  Fx.text(chef.x, chef.z, "-" .. cost, 0xFFE060)
  -- the chosen offer goes, another takes its place
  run.offers[i] = nil
  End.fill_offers(run)
  rebuild_pool(run)
end

---------------------------------------------------------------- the register

function End.open(chef, st)
  local run = G.run
  if not run.endless then return false end
  if chef.bot then return false end
  End.fill_offers(run)
  chef.panel = { sel = 1, st = st, t = 0 }
  Snd.ui_ok()
  return true
end

local function panel_input(run, c, dt)
  local p = c.panel
  p.t = p.t + dt
  local d = (btnp(0, c.pad) and -1 or 0) + (btnp(1, c.pad) and 1 or 0)
  if d ~= 0 then p.sel = (p.sel - 1 + d) % 3 + 1; Snd.ui_move() end
  if p.t > 0.15 and btnp(BA, c.pad) then End.buy(run, c, p.sel) end
  if btnp(BB, c.pad) or btnp(2, c.pad) or btnp(3, c.pad) then
    c.panel = nil
    Snd.ui_back()
  end
end

---------------------------------------------------------------- running

function End.setup(run)
  run.endless = true
  run.hearts, run.tier, run.earned, run.spent = 5, 1, 0, 0
  run.bought, run.offers, run.unlocked = {}, {}, {}
  run.streak = 0
  run.dis_t = 60
  run.no_patience = false
  for _, id in ipairs(run.stage.recipes) do run.unlocked[id] = true end
  rebuild_pool(run)
  run.on_serve = function(r, o, gain)
    r.earned = r.earned + gain
    r.streak = r.streak + 1
    if r.streak >= 5 and r.hearts < 5 then
      r.streak = 0
      r.hearts = r.hearts + 1
      Fx.text(r.w / 2, r.h - 1, "+1 HEART", 0xFF6080, true)
    end
  end
  run.on_expire = function(r, o)
    r.streak = 0
    r.hearts = r.hearts - 1
    Ren.shake(0.5)
    if r.hearts <= 0 then
      r.over = true
      r.over_text = "KITCHEN CLOSED!"
    end
  end
  End.fill_offers(run)
end

local PESTS = { rats = true, ducks = true }

function End.update(run, dt)
  if not run.endless then return end
  local t = run.t
  -- tiers: by money earned, or by time
  local tier = 1
  for k = 2, 4 do
    if run.earned >= TIER_EARN[k] or t >= TIER_TIME[k] then tier = k end
  end
  if tier > run.tier then
    run.tier = tier
    Fx.text(run.w / 2, run.h / 2, "TIER " .. tier .. "!", 0x80FFB0, true)
    Snd.fanfare()
    -- new offers can appear now
    for i = 1, 3 do
      local u = run.offers[i]
      if u and u.tier < tier - 1 then run.offers[i] = nil end
    end
    End.fill_offers(run)
  end
  -- escalation: more orders, less patience
  run.order_every = max(7, 19 - t / 35)
  run.patience_k = max(0.62, 1.3 - t / 900) * (1 + (run.patience_bonus or 0))
  run.max_orders = min(6, 2 + run.tier + (run.extra_orders or 0))
  if run.fast_plates then
    for _, r in ipairs(run.returns) do r.t = min(r.t, 2) end
  end
  -- disasters from tier 2, more and more often
  if run.tier >= 2 then
    run.dis_t = run.dis_t - dt
    if run.dis_t <= 0 then
      run.dis_t = max(35, 80 - t / 15)
      local kinds = { "runaway", "poltergeist", "tornado", "leak" }
      if not run.no_pests then kinds[#kinds + 1] = "rats"; kinds[#kinds + 1] = "ducks" end
      if run.tier >= 3 then kinds[#kinds + 1] = "ghosts"; kinds[#kinds + 1] = "possessed" end
      Dis.trigger(run, choose(kinds))
    end
  end
  if run.no_pests then
    for _, r in ipairs(run.rats) do r.t = 0 end
    for _, d in ipairs(run.ducks) do d.flee = 1 end
  end
  -- the sprinklers
  if run.auto_ext then
    for _, st in ipairs(run.stations) do
      if st.fire > 0 then
        st.fire = st.fire - dt * 0.8
        if random() < 0.3 then Fx.spray(st.x, st.z, 0, 0) end
        if st.fire <= 0 then st.fire = 0 Fx.text(st.x, st.z, "OUT!", 0x80E0FF) end
      end
    end
  end
  -- stations falling into place (the kitchen meshes are rebuilt once, even
  -- when two land together: it takes about 10 ms on the Pi)
  local landed = false
  for _, st in ipairs(run.stations) do
    if st.drop then
      st.drop = st.drop - dt
      if st.drop <= 0 then
        st.drop, st.drop_mesh = nil, nil
        landed = true
        Snd.place()
        Ren.shake(0.15)
        Fx.puff(st.x, 0.2, st.z, 0xFFFFFF, 6)
        Fx.text(st.x, st.z, "NEW!", 0x80FFB0)
      end
    end
  end
  if landed then run.meshes = Mesh.kitchen(run) end
  for _, c in ipairs(run.chefs) do
    if c.panel then panel_input(run, c, dt) end
  end
end

---------------------------------------------------------------- drawing

function End.draw3d(run)
  for _, st in ipairs(run.stations) do
    if st.drop and st.drop_mesh then
      local k = st.drop / 0.7
      local y = k * k * 3
      for _, m in ipairs(st.drop_mesh) do draw3d(m, st.x, y, st.z) end
    end
  end
end

local function offer_icon(u, x, y)
  if u.crate then
    Hud.icon(Data.ING[u.crate].icon, x, y)
  elseif u.recipe then
    local r = Data.RECIPE[u.recipe]
    Hud.draw_part(r.parts[1], x, y)
  else
    Hud.icon(Hud.IC[u.icon] or Hud.IC.up, x, y)
  end
end

function End.draw2d(run)
  if not run.endless then return end
  -- hearts and time, top right
  local x = W - 118
  panel(x - 4, 4, 118, 42, 0x302A3A, 0x000000)
  for i = 1, 5 do Hud.icon(i <= run.hearts and Hud.IC.heart or Hud.IC.heart0, x + (i - 1) * 22, 8) end
  print(fmt("%s  T%d", fmt_time(run.t), run.tier), x + 2, 26, 0xC0C0D0)
  -- a coin over the register when something can be bought
  local cheapest = 1e9
  for i = 1, 3 do if run.offers[i] then cheapest = min(cheapest, cost_of(run, run.offers[i])) end end
  for _, st in ipairs(Kit.find(run, "register")) do
    local sx, sy = project3d(st.x, TOP + 0.9, st.z)
    if sx and run.coins >= cheapest then
      Hud.icon(Hud.IC.coin, floor(sx - 8), floor(sy - 10 + sin(G.t * 5) * 3))
    end
  end
  -- the panels of the chefs at the register
  for _, c in ipairs(run.chefs) do
    local p = c.panel
    if p then
      local sx, sy = project3d(p.st.x, TOP + 1.0, p.st.z)
      sx, sy = sx or W / 2, sy or H / 2
      local pw, ph = 3 * 124 + 12, 96
      local px = clamp(floor(sx - pw / 2), 4, W - pw - 4)
      local py = clamp(floor(sy - ph - 10), 60, H - ph - 34)
      panel(px, py, pw, ph, 0x201C2C, PCOL[c.pad] or 0xFFFFFF)
      print("P" .. c.pad .. " at the register", px + 6, py + 3, PCOL[c.pad] or 0xFFFFFF)
      print("A buy  B close", px + pw - 118, py + 3, 0x9090A0)
      for i = 1, 3 do
        local u = run.offers[i]
        local ox = px + 6 + (i - 1) * 124
        local oy = py + 22
        local sel = i == p.sel
        if u then
          local cost = cost_of(run, u)
          local can = run.coins >= cost
          panel(ox, oy, 118, 68, sel and (can and 0xFFF4D0 or 0xE8D0D0) or 0x3A344A, sel and (PCOL[c.pad] or 0xFFFFFF) or 0x000000)
          local tc = sel and 0x302018 or 0xE0E0E0
          offer_icon(u, ox + 4, oy + 4)
          local name = #u.name > 12 and sub(u.name, 1, 12) or u.name
          print(name, ox + 24, oy + 4, tc)
          local desc = u.desc or ""
          print(sub(desc, 1, 14), ox + 4, oy + 24, sel and 0x605040 or 0xA0A0B0)
          if #desc > 14 then print(sub(desc, 15, 28), ox + 4, oy + 38, sel and 0x605040 or 0xA0A0B0) end
          Hud.icon(Hud.IC.coin, ox + 4, oy + 50)
          print(tostring(cost), ox + 24, oy + 51, can and (sel and 0x207020 or 0xFFE060) or 0xE05050)
          print(u.cat and sub(u.cat, 1, 5) or "", ox + 72, oy + 51, sel and 0x807060 or 0x707080)
        else
          panel(ox, oy, 118, 68, 0x2A2436, 0x000000)
          text_c("sold out", ox + 59, oy + 26, 0x707080)
        end
      end
    end
  end
end

function End.best_offer_cost(run)
  local m = nil
  for i = 1, 3 do
    local u = run.offers[i]
    if u then local c = cost_of(run, u) m = m and min(m, c) or c end
  end
  return m
end
