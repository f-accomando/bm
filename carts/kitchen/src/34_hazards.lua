-- Hazards that belong to a kitchen's layout, always readable and fair:
--   platforms   decks that slide over the water and back (stage.plats), or
--               turn a quarter every few seconds (rotate = true)
--   doors       '+' cells that open and close, with a warning blink
--   vents       '^' cells that hiss, then blow chefs away for a moment
--   carts       market carts that roll along a row (haz kind "cart")
--   shelves     boxes that fall from a wall shelf and block a cell for a
--               while; a shadow shows where (haz kind "shelf")
-- stage.haz = { { kind = "cart", row = 4, period = 9 }, ... }

local TOP = Kit.TOP

---------------------------------------------------------------- setup

function Haz.init(run)
  local st = run.stage
  run.doors, run.vents, run.carts, run.debris = {}, {}, {}, {}
  for cz = 0, run.h - 1 do
    for cx = 0, run.w - 1 do
      local c = Kit.cell(run, cx, cz)
      if c.kind == "door" then
        c.open = true
        run.doors[#run.doors + 1] = { cell = c, cx = cx, cz = cz, t = random() * 3, h = 0 }
      elseif c.kind == "vent" then
        run.vents[#run.vents + 1] = { cell = c, cx = cx, cz = cz, t = 4 + random() * 5, state = "idle" }
      end
    end
  end
  run.shelves = nil
  for _, h in ipairs(st.haz or {}) do
    if h.kind == "cart" then
      -- the row, from the layout: rolls between its first and last floor cell
      local cz = run.h - 1 - h.row
      local x0, x1 = nil, nil
      for cx = 0, run.w - 1 do
        if not Kit.blocked(Kit.cell(run, cx, cz)) then x0 = x0 or cx; x1 = cx end
      end
      if x0 then
        run.carts[#run.carts + 1] = { x = x0 + 0.5, z = cz + 0.5, x0 = x0 + 0.5, x1 = x1 + 0.5,
                                      dir = 1, speed = h.speed or 1.6, pause = 0, col = h.col or 0xE04848 }
      end
    elseif h.kind == "shelf" then
      run.shelves = { every = h.every or 14, t = h.first or 10, stay = h.stay or 9 }
    end
  end
  for _, p in ipairs(run.plats) do
    p.rot, p.rot_to, p.turn_t = 0, 0, p.def.period or 8
  end
end

---------------------------------------------------------------- platforms

-- back and forth: wait at one end, glide, wait at the other, glide back
local function plat_offset(p, t)
  local d = p.def
  local period = d.period or 10
  local u = (t % period) / period
  local k
  if u < 0.35 then k = 0
  elseif u < 0.5 then k = ease((u - 0.35) / 0.15)
  elseif u < 0.85 then k = 1
  else k = 1 - ease((u - 0.85) / 0.15) end
  return (d.dx or 0) * k, -(d.dy or 0) * k
end

local function update_plat(run, p, dt)
  p.t = p.t + dt
  if p.def.rotate then
    -- a quarter turn every period; chefs on it turn with it
    p.turn_t = p.turn_t - dt
    if p.turn_t <= 1 and p.turn_t + dt > 1 then Snd.clang() end
    if p.turn_t <= 0 then
      p.turn_t = p.def.period or 8
      p.rot_to = p.rot_to + 1
      Kit.rotate_platform(run, p)
    end
    local target = p.rot_to * pi / 2
    local old = p.rot
    p.rot = approach(p.rot, target, dt * 3)
    local da = p.rot - old
    if da ~= 0 then
      local cx, cz = p.x0 + p.w / 2, p.z0 + p.h / 2
      local ca, sa = cos(-da), sin(-da)
      for _, c in ipairs(run.chefs) do
        local cell, pp = Kit.cell_at(run, c.x, c.z)
        if pp == p then
          local dx, dz = c.x - cx, c.z - cz
          c.x, c.z = cx + dx * ca - dz * sa, cz + dx * sa + dz * ca
          c.yaw = c.yaw + da
        end
      end
    end
    p.vx, p.vz = 0, 0
    return
  end
  local ox, oz = plat_offset(p, p.t)
  p.vx, p.vz = (ox - p.ox) / dt, (oz - p.oz) / dt
  p.ox, p.oz = ox, oz
end

---------------------------------------------------------------- update

local function chef_in_cell(run, cx, cz)
  for _, c in ipairs(run.chefs) do
    if floor(c.x) == cx and floor(c.z) == cz then return c end
  end
  return nil
end

function Haz.update(run, dt)
  for _, p in ipairs(run.plats) do update_plat(run, p, dt) end

  -- doors: open 7 s, blink 1.5 s, closed 4 s (they wait for a chef in the way)
  for _, d in ipairs(run.doors) do
    d.t = d.t + dt
    local c = d.cell
    if c.open then
      if d.t > 8.5 then
        if not chef_in_cell(run, d.cx, d.cz) and not Kit.loose_near(run, d.cx + 0.5, d.cz + 0.5, 0.6) then
          c.open = false
          d.t = 0
          Snd.clang()
        end
      end
    elseif d.t > 4 then
      c.open = true
      d.t = 0
      Snd.clang()
    end
    d.h = approach(d.h, c.open and 0 or 1, dt * 5)
  end

  -- vents: hiss for a second, then blow for a second
  for _, v in ipairs(run.vents) do
    v.t = v.t - dt
    if v.state == "idle" and v.t <= 1.2 then
      v.state = "warn"
      Snd.hiss()
    elseif v.state == "warn" then
      if random() < 0.3 then Fx.steam(v.cx + 0.5, 0.05, v.cz + 0.5) end
      if v.t <= 0 then v.state, v.t = "blow", 1.1 end
    elseif v.state == "blow" then
      Fx.steam(v.cx + 0.5, 0.1, v.cz + 0.5)
      Fx.steam(v.cx + 0.5, 0.4, v.cz + 0.5)
      for _, c in ipairs(run.chefs) do
        local dx, dz = c.x - (v.cx + 0.5), c.z - (v.cz + 0.5)
        local d2 = dx * dx + dz * dz
        if d2 < 0.8 and c.stun <= 0 and not c.fall then
          local d = sqrt(d2) + 0.01
          c.vx, c.vz = dx / d * 7, dz / d * 7
          c.stun = 0.45
          Chef.say(c, "HOT!", 0.8)
        end
      end
      v.t = v.t - dt * 0
      if v.t <= 0 then v.state, v.t = "idle", 6 + random() * 4 end
    end
  end

  -- carts roll along their row and wait a little at each end; a chef in
  -- the way stops them (a ring of the bell), and after a while they turn back
  for _, k in ipairs(run.carts) do
    if k.pause > 0 then
      k.pause = k.pause - dt
    else
      local nx = k.x + k.dir * k.speed * dt
      local blocked = false
      for _, c in ipairs(run.chefs) do
        local dx, dz = c.x - nx, c.z - k.z
        if abs(dz) < 0.72 and abs(dx) < 0.78 and dx * k.dir > -0.1 and not c.fall then blocked = true end
      end
      if blocked then
        k.wait = (k.wait or 0) + dt
        if k.wait > 0.2 and not k.rang then k.rang = true; Snd.bell() end
        if k.wait > 1.6 then k.dir, k.wait, k.rang = -k.dir, 0, false end
      else
        k.wait, k.rang = 0, false
        k.x = nx
      end
      if k.x >= k.x1 then k.x, k.dir, k.pause = k.x1, -1, 1.5 end
      if k.x <= k.x0 then k.x, k.dir, k.pause = k.x0, 1, 1.5 end
    end
  end

  -- falling boxes from the shelves
  local sh = run.shelves
  if sh then
    sh.t = sh.t - dt
    if sh.t <= 0 then
      sh.t = sh.every
      -- a free floor cell near the back wall
      local cands = {}
      for cz = max(0, run.h - 5), run.h - 2 do
        for cx = 1, run.w - 2 do
          local c = Kit.cell(run, cx, cz)
          if not Kit.blocked(c) and not chef_in_cell(run, cx, cz) then cands[#cands + 1] = { cx, cz } end
        end
      end
      if #cands > 0 then
        local p = choose(cands)
        run.debris[#run.debris + 1] = { cx = p[1], cz = p[2], t = 1.4, stay = sh.stay, y = 3 }
        Snd.alarm()
      end
    end
  end
  for i = #run.debris, 1, -1 do
    local d = run.debris[i]
    if d.t > 0 then
      d.t = d.t - dt
      d.y = max(0, d.t / 1.4 * 3)
      if d.t <= 0 then
        local c = Kit.cell(run, d.cx, d.cz)
        -- a chef that did not move away is pushed aside and dizzy
        local ch = chef_in_cell(run, d.cx, d.cz)
        if ch then
          ch.stun = 0.8
          ch.x = ch.x + (random() < 0.5 and -0.8 or 0.8)
          if not Chef.free(run, ch.x, ch.z) then ch.x = d.cx + 0.5; ch.z = ch.z - 0.8 end
          Chef.say(ch, "OUCH!", 0.8)
        end
        c.debris = true
        Snd.bump()
        Ren.shake(0.25)
        Fx.puff(d.cx + 0.5, 0.2, d.cz + 0.5, 0xC8B8A0, 6)
      end
    else
      d.stay = d.stay - dt
      if d.stay <= 0 then
        Kit.cell(run, d.cx, d.cz).debris = nil
        Fx.puff(d.cx + 0.5, 0.3, d.cz + 0.5, 0xFFFFFF, 5)
        remove(run.debris, i)
      end
    end
  end
end

-- carts are solid too
function Haz.solid_at(run, x, z)
  for _, k in ipairs(run.carts) do
    if abs(x - k.x) < 0.45 and abs(z - k.z) < 0.45 then return true end
  end
  return false
end

---------------------------------------------------------------- drawing

local door_mesh, cart_mesh, box_mesh, wheel_mesh, vent_glow

local function meshes(run)
  if door_mesh then return end
  local b = Mesh.builder()
  b.boxf(-0.5, 0, -0.12, 0.5, 1.0, 0.12, 0x8A5A3A, "tsewn", 0xA87050)
  b.boxf(-0.42, 0.1, -0.13, 0.42, 0.9, -0.12, 0x6A4028, "s")
  door_mesh = b.build()
  b = Mesh.builder()
  b.boxf(-0.42, 0.18, -0.4, 0.42, 0.62, 0.4, 0xE04848, "tsewn", 0xF0D080)
  for _, x in ipairs({ -0.3, 0.3 }) do
    b.prism(x, -0.43, 0, 0.3, 0.12, 0.12, 6, 0x303030, 0x505050)
  end
  -- a striped awning on poles
  b.boxf(-0.4, 0.62, -0.38, -0.34, 1.05, -0.32, 0xE0E0E0, "sewn")
  b.boxf(0.34, 0.62, -0.38, 0.4, 1.05, -0.32, 0xE0E0E0, "sewn")
  b.boxf(-0.5, 1.05, -0.5, 0.5, 1.12, 0.5, 0xFFFFFF, "tsewn", 0xE04848)
  for i = 0, 2 do b.ball(-0.2 + i * 0.2, 0.7, 0, 0.1, 0.08, 0.1, ({ 0xE8402A, 0xF0A020, 0x60C040 })[i + 1], 5, 2) end
  cart_mesh = b.build()
  b = Mesh.builder()
  b.boxf(-0.42, 0, -0.42, 0.42, 0.62, 0.42, 0xB88A50, "tsewn", 0xD8A868)
  b.boxf(-0.43, 0.2, -0.43, 0.43, 0.26, 0.43, 0x8A6A3A, "sewn")
  box_mesh = b.build()
end

function Haz.draw_floor(run)
  meshes(run)
  -- shadows of falling boxes
  for _, d in ipairs(run.debris) do
    if d.t > 0 then
      local k = 1 - d.t / 1.4
      draw3d(run.meshes.shadow, d.cx + 0.5, 0, d.cz + 0.5, 0, 0, 0, 0.8 + k * 1.2, 1)
    end
  end
end

function Haz.draw(run)
  meshes(run)
  for _, d in ipairs(run.doors) do
    if d.h > 0.02 then
      local blink = not d.cell.open or d.t < 7 or (G.frame // 6) % 2 == 0
      if blink then draw3d(door_mesh, d.cx + 0.5, (d.h - 1) * 1.0, d.cz + 0.5) end
    elseif d.cell.open and d.t > 7 and (G.frame // 6) % 2 == 0 then
      local sx, sy = project3d(d.cx + 0.5, 0.2, d.cz + 0.5)
      if sx then Hud.icon(Hud.IC.lock, floor(sx - 8), floor(sy - 8)) end
    end
  end
  for _, k in ipairs(run.carts) do
    draw3d(cart_mesh, k.x, 0, k.z, 0, 0, 0, 1)
  end
  for _, d in ipairs(run.debris) do
    draw3d(box_mesh, d.cx + 0.5, d.y, d.cz + 0.5, 0, d.cx * 0.7, 0)
  end
  for _, v in ipairs(run.vents) do
    if v.state == "warn" and (G.frame // 5) % 2 == 0 then
      local sx, sy = project3d(v.cx + 0.5, 0.1, v.cz + 0.5)
      if sx then Hud.icon(Hud.IC.steam, floor(sx - 8), floor(sy - 12)) end
    end
  end
end
