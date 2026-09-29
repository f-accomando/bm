-- Drawing the kitchen: the camera frames the whole kitchen and zooms in a
-- little when the chefs stay close together; then water, floor, shadows,
-- stations, food, chefs, and the 2D marks drawn over the 3D picture.

local TOP = Kit.TOP
local PITCH, FOV = -0.98, 42
local HUD_TOP = 64                  -- the order cards
local cam = { x = 0, z = 0, d = 10, tx = 0, tz = 0, td = 10, shake = 0 }
Ren.cam = cam

local function set_cam(x, z, d, sx, sy)
  local cp, spp = cos(-PITCH), sin(-PITCH)
  camera3d(x + (sx or 0), d * spp + (sy or 0), z - d * cp, 0, PITCH, FOV)
end

-- The distance that shows the whole kitchen below the order cards, and the
-- point to look at so that it sits in the middle of the free area.
function Ren.fit(run)
  local w, h = run.w, run.h
  local cx = w / 2
  local cz = h / 2 - 0.5
  local pts = { { 0, 0, 0 }, { w, 0, 0 }, { 0, 1.5, h }, { w, 1.5, h }, { 0, -0.55, 0 }, { w, -0.55, 0 } }
  local d = 8
  -- margins: room for the small zoom of Ren.update_cam without cutting anything
  local x_lo, x_hi, y_lo, y_hi = 26, W - 26, HUD_TOP + 8, H - 12
  for _ = 1, 3 do
    -- distance: the smallest that keeps every corner on the screen
    local lo, hi = 3, 60
    for _ = 1, 22 do
      d = (lo + hi) / 2
      set_cam(cx, cz, d)
      local ok = true
      for _, p in ipairs(pts) do
        local sx, sy = project3d(p[1], p[2], p[3])
        if not sx or sx < x_lo or sx > x_hi or sy < y_lo or sy > y_hi then ok = false break end
      end
      if ok then hi = d else lo = d end
    end
    d = hi
    -- then move the target so the picture is centred vertically
    set_cam(cx, cz, d)
    local top, bottom = 1e9, -1e9
    for _, p in ipairs(pts) do
      local _, sy = project3d(p[1], p[2], p[3])
      top, bottom = min(top, sy), max(bottom, sy)
    end
    local want = (y_lo + y_hi) / 2
    local have = (top + bottom) / 2
    -- a pixel on the screen is about d / focal units of floor: move by that
    cz = cz + (want - have) * d / 700
  end
  run.cam = { x = cx, z = cz, d = d }
  cam.x, cam.z, cam.d = cx, cz, d
  cam.tx, cam.tz, cam.td = cx, cz, d
end

function Ren.update_cam(run, dt)
  local base = run.cam
  local chefs = run.chefs
  local tx, tz, td = base.x, base.z, base.d
  if #chefs > 0 then
    local x0, x1, z0, z1 = 1e9, -1e9, 1e9, -1e9
    local sx, sz = 0, 0
    for _, c in ipairs(chefs) do
      x0, x1, z0, z1 = min(x0, c.x), max(x1, c.x), min(z0, c.z), max(z1, c.z)
      sx, sz = sx + c.x, sz + c.z
    end
    sx, sz = sx / #chefs, sz / #chefs
    -- spread 0 (together) .. 1 (far apart): a gentle zoom in when together,
    -- never so much that a station leaves the screen (see the margins of fit)
    local spread = clamp(max((x1 - x0) / run.w, (z1 - z0) / run.h) * 1.7, 0, 1)
    if #chefs == 1 then spread = 0.6 end
    local zoom = lerp(0.95, 1.0, spread)
    td = base.d * zoom
    local k = (1 - zoom) / 0.05 * 0.12
    tx = lerp(base.x, sx, k)
    tz = lerp(base.z, sz, k * 0.5)
  end
  local a = min(1, dt * 2.5)
  cam.x, cam.z, cam.d = lerp(cam.x, tx, a), lerp(cam.z, tz, a), lerp(cam.d, td, a)
  if cam.shake > 0 then cam.shake = max(0, cam.shake - dt * 2.5) end
end

function Ren.shake(k) cam.shake = max(cam.shake, k) end

---------------------------------------------------------------- pieces

local function draw_list(list, x, y, z, flags)
  if not list then return end
  for i = 1, #list do draw3d(list[i], x or 0, y or 0, z or 0, 0, 0, 0, 1, flags) end
end

-- an item somewhere in the world (y = the surface it stands on)
local function draw_item(it, x, y, z, yaw, scale)
  scale = scale or 1
  if it.plate then
    draw3d(Mesh.plate, x, y, z, 0, yaw or 0, 0, scale)
    local n = #it.parts
    for i, key in ipairs(it.parts) do
      local m = Mesh.item(key)
      if m then
        local r = n > 1 and 0.1 * scale or 0
        local a = i / n * TAU + (yaw or 0)
        draw3d(m, x + sin(a) * r, y + 0.035 * scale, z + cos(a) * r, 0, (yaw or 0) + i, 0, 0.75 * scale)
      end
    end
  elseif it.dirty then
    for i = 0, min(it.dirty, 5) - 1 do
      draw3d(Mesh.dplate, x, y + i * 0.04 * scale, z, 0, i * 0.7, 0, scale)
    end
  elseif it.key then
    local m = Mesh.item(it.key)
    if m then draw3d(m, x, y, z, 0, yaw or 0, 0, scale) end
  end
end
Ren.draw_item = draw_item

---------------------------------------------------------------- chefs

-- walking, carrying, chopping... one animation per chef personality
local function chef_pose(c)
  local id = c.def.id
  local w = c.walk
  local sp = min(1, c.speed / (Data.BASE_SPEED * 0.9))
  local t = G.t
  local p = { bob = 0, lean = 0, roll = 0, legL = 0, legR = 0, armL = 0, armR = 0,
              armLz = 0, armRz = 0, sx = 0, hop = 0 }
  local s = sin(w * 3.2)
  if id == "basil" then
    p.legL, p.legR = s * 0.7 * sp, -s * 0.7 * sp
    p.armL, p.armR = -s * 0.55 * sp, s * 0.55 * sp
    p.bob = abs(s) * 0.035 * sp
    p.lean = 0.12 * sp
    if sp < 0.1 then p.bob = sin(t * 2.2) * 0.008 end            -- steady breathing
  elseif id == "bun" then
    local s2 = sin(w * 4.2)
    p.legL, p.legR = s2 * 0.55 * sp, -s2 * 0.55 * sp
    p.armL, p.armR = -s2 * 0.4 * sp, s2 * 0.4 * sp
    p.armLz, p.armRz = -0.35, 0.35
    p.roll = s2 * 0.2 * sp                                         -- the waddle
    p.bob = abs(s2) * 0.06 * sp
    p.sx = s2 * 0.04 * sp
    if sp < 0.1 then p.bob = abs(sin(t * 3)) * 0.02 end            -- belly bounce
  elseif id == "noodle" then
    local s3 = sin(w * 2.4)
    p.legL, p.legR = s3 * 0.95 * sp, -s3 * 0.95 * sp                -- long strides
    p.armL, p.armR = -s3 * 0.9 * sp, s3 * 0.9 * sp
    p.bob = abs(s3) * 0.06 * sp
    p.lean = 0.22 * sp
    if sp < 0.1 then p.roll = sin(t * 1.3) * 0.04; p.armR = sin(t * 5) * 0.1 end   -- restless
  else -- pepper
    local s4 = sin(w * 4.6)
    p.legL, p.legR = s4 * 0.8 * sp, -s4 * 0.8 * sp                 -- quick short steps
    p.armL, p.armR = -s4 * 0.3 * sp, s4 * 0.3 * sp
    p.bob = abs(s4) * 0.03 * sp
    p.lean = 0.08 * sp
    if sp < 0.1 and c.idle_t > 1.5 then                            -- arms crossed
      p.armL, p.armR, p.armLz, p.armRz = -1.3, -1.3, 0.8, -0.8
    end
  end
  -- carrying: both arms forward (Bun leans back under the weight)
  if c.hold and c.act ~= "throw" then
    p.armL, p.armR = -1.35, -1.35
    p.armLz, p.armRz = 0, 0
    if id == "bun" then p.lean = p.lean - 0.12; p.roll = p.roll * 1.6 end
  end
  local a, at = c.act, c.act_t
  if a == "chop" then
    local k = sin(t * (id == "bun" and 36 or 28))
    p.armR = -1.4 + k * 0.9
    p.armL = -1.0
    p.lean = 0.2
    if id == "pepper" then p.hop = abs(k) * 0.03 end
    if id == "noodle" then p.armL = -1.1 + k * 0.2 end
  elseif a == "wash" then
    local k = sin(t * 16)
    p.armL, p.armR = -1.2 + k * 0.3, -1.2 - k * 0.3
    p.lean = 0.25
  elseif a == "throw" then
    local k = 1 - at / 0.3
    p.armR = -2.6 + k * 3.2
    p.lean = -0.1 + k * 0.3
  elseif a == "pick" then
    p.lean = p.lean + 0.25 * sin(at / 0.22 * pi)
  elseif a == "dash" then
    p.lean = 0.45
    p.armL, p.armR = 0.9, 0.9
  elseif a == "hop" then
    p.hop = sin((1 - at / 0.35) * pi) * 0.22
    p.armLz, p.armRz = -1.0, 1.0
  elseif a == "shrug" then
    p.armL, p.armR = -0.3, -0.3
    p.armLz, p.armRz = -1.1, 1.1
    p.hop = 0.03
  elseif a == "cheer" then
    p.armL, p.armR = -2.9, -2.9
    p.hop = abs(sin(at * 12)) * 0.18
  elseif a == "sad" then
    p.lean = 0.35
    p.armL, p.armR = 0.2, 0.2
  elseif a == "catch" then
    p.armL, p.armR = -2.2, -2.2
  end
  if c.spray then p.armL, p.armR = -1.4, -1.4 end
  if c.stun > 0 then p.hop = abs(sin(t * 10)) * 0.1; p.armLz, p.armRz = -1.2, 1.2 end
  return p
end

-- chefs are drawn a bit bigger than life: they must read at a glance
local CS = 1.25
Ren.CHEF_SCALE = CS

function Ren.chef(c, x, y, z, yaw)
  local rig = Mesh.chef[c.ci]
  local p = chef_pose(c)
  local cy, sy = cos(yaw), sin(yaw)
  local by = y + (rig.hip + p.bob + p.hop) * CS
  local ox = p.sx * CS
  local bx, bz = x + ox * cy, z - ox * sy
  draw3d(rig.body, bx, by, bz, p.lean, yaw, p.roll, CS)
  -- a joint (x, y, z above the hips' centre), turned with the body; the
  -- shoulders also follow its lean
  local cl, sl = cos(p.lean), sin(p.lean)
  local function joint(j, jx, jy, jz, lean)
    if j then jx, jy, jz = j[1], j[2], j[3] end
    jx, jy, jz = jx * CS, jy * CS, jz * CS
    if lean then jy, jz = jy * cl - jz * sl, jy * sl + jz * cl end
    return bx + jx * cy + jz * sy, by + jy, bz - jx * sy + jz * cy
  end
  local hw, sw, sh = rig.hipw, rig.shw, rig.sh
  local lx, ly, lz = joint(rig.jLegL, -hw, 0, 0)
  draw3d(rig.legL or rig.leg, lx, ly, lz, p.legL, yaw, p.roll, CS)
  lx, ly, lz = joint(rig.jLegR, hw, 0, 0)
  draw3d(rig.legR or rig.leg, lx, ly, lz, p.legR, yaw, p.roll, CS)
  lx, ly, lz = joint(rig.jArmL, -sw, sh, 0, true)
  draw3d(rig.armL or rig.arm, lx, ly, lz, p.armL, yaw, p.roll + p.armLz, CS)
  lx, ly, lz = joint(rig.jArmR, sw, sh, 0, true)
  draw3d(rig.armR or rig.arm, lx, ly, lz, p.armR, yaw, p.roll + p.armRz, CS)
  local shy = by + sh * CS * cl
  local ax, az = bx + sl * sh * CS * sy, bz + sl * sh * CS * cy
  -- what the chef holds
  if c.hold and c.act ~= "throw" then
    local fwd = (rig.width + 0.2) * CS
    local hy = shy - rig.hand * 0.55 * CS
    draw_item(c.hold, ax + sy * fwd, hy, az + cy * fwd, yaw, 1.0)
  end
  return shy + (rig.top - rig.hip - rig.sh) * CS
end

---------------------------------------------------------------- stations

local function draw_station_top(run, st)
  local x, z = st.x, st.z
  local y = TOP + (st.bump > 0 and sin(st.bump / 0.3 * pi) * 0.05 or 0)
  local k = st.kind
  if st.item then
    local ox = 0
    if st.slide then ox = -st.slide end
    local dx, dz = 0, 0
    if st.dir and st.slide then dx, dz = st.dir[1] * -st.slide, st.dir[2] * -st.slide end
    local yy = y + (k == "board" and 0.04 or 0)
    draw_item(st.item, x + dx, yy, z + dz, 0)
  end
  if st.box then
    local b = st.box
    if #b.items > 0 then
      if k == "pot" then
        local c = b.burnt and 0x2A2420 or Mesh.mix_color(b.out or Food.mix_key("boil", b.items))
        local lvl = 0.14 + #b.items * 0.035
        local boil = b.done and not b.burnt and sin(G.t * 20) * 0.01 or 0
        draw3d(Mesh.soup_disc(c), x, y + lvl + boil, z)
      elseif k == "pan" then
        local key = b.burnt and "burnt" or (b.done and b.out) or b.items[1]
        local m = Mesh.item(key)
        if m then draw3d(m, x, y + 0.06, z, 0, G.t * 0.5, 0, 0.9) end
      elseif k == "oven" or k == "blender" then
        -- inside: shown by the bubble above
      end
    end
  end
  if k == "plates" and st.n > 0 then
    for i = 0, min(st.n, 6) - 1 do draw3d(Mesh.plate, x, y + i * 0.04, z) end
  elseif k == "ret" and st.n > 0 then
    for i = 0, min(st.n, 6) - 1 do draw3d(Mesh.dplate, x, y + i * 0.04, z, 0, i, 0) end
  elseif k == "sink" then
    for i = 0, min(st.dirty, 4) - 1 do draw3d(Mesh.dplate, x - 0.12, y - 0.02 + i * 0.03, z, 0.3, i, 0, 0.9) end
    for i = 0, min(st.clean, 4) - 1 do draw3d(Mesh.plate, x + 0.3, y + 0.02 + i * 0.04, z + 0.3, 0, 0, 0, 0.7) end
  end
  if st.fire > 0 then
    local f = 0.8 + sin(G.t * 17 + st.id) * 0.2
    draw3d(Mesh.flame, x, y + 0.05, z, 0, G.t * 3, 0, f * min(1, st.fire * 1.5 + 0.4), 2)
  end
end

---------------------------------------------------------------- the frame

function Ren.world(run)
  local world = run.world
  -- sky: bands from the world colour to a lighter one
  for i = 0, 5 do
    rectfill(0, i * 60, W, 60, mix_rgb(world.sky, 0xFFFFFF, i * 0.07))
  end
  zclear()
  local sx, sy = 0, 0
  if cam.shake > 0 then
    sx, sy = (random() - 0.5) * cam.shake * 0.4, (random() - 0.5) * cam.shake * 0.3
  end
  set_cam(cam.x, cam.z, cam.d, sx, sy)
  -- lights out (ghosts): only what is near a chef is lit
  if run.dark and run.dark > 0 then
    -- the sun from below: no face gets it, only the ambient and the lamps
    light3d(0, -1, 0, 0.1)
    for i = 1, 4 do
      local c = run.chefs[i]
      if c then lamp3d(i, c.x, 1.0, c.z, 2.8, 1.1) else lamp3d(i) end
    end
  else
    light3d(-0.45, 0.85, -0.35, run.ambient or 0.45)
    lamp3d()
  end

  local km = run.meshes
  -- no z-buffer for what lies flat under everything else
  if km.water then draw3d(km.water, 0, 0, 0, 0, 0, 0, 1, 1) end
  draw_list(km.floor, 0, 0, 0, 1)
  for i, p in ipairs(run.plats) do
    local list = km.pfloor[i]
    for j = 1, #list do
      draw3d(list[j], p.x0 + p.w / 2 + p.ox, 0, p.z0 + p.h / 2 + p.oz, 0, p.rot, 0, 1, 1)
    end
  end
  Haz.draw_floor(run)
  for _, c in ipairs(run.chefs) do
    if not c.fall then
      draw3d(km.shadow, c.x, 0, c.z, 0, 0, 0, Mesh.chef[c.ci].width * 3.2 + 0.2, 1)
      draw3d(Mesh.ring[c.pad] or Mesh.ring[1], c.x, 0, c.z, 0, 0, 0, 1, 1)
    end
  end
  for _, l in ipairs(run.loose) do
    draw3d(km.shadow, l.x, 0, l.z, 0, 0, 0, 0.7, 1)
  end

  -- everything that stands up
  draw_list(km.static)
  for i, p in ipairs(run.plats) do
    local list = km.pstatic[i]
    for j = 1, #list do
      draw3d(list[j], p.x0 + p.w / 2 + p.ox, 0, p.z0 + p.h / 2 + p.oz, 0, p.rot, 0, 1)
    end
  end
  -- the stations the chefs face
  for _, c in ipairs(run.chefs) do
    local st = c.target
    if st and not c.fall then
      local hy = st.kind == "trash" and TOP - 0.02 or (st.kind == "oven" and TOP + 0.13 or TOP + 0.012)
      draw3d(Mesh.hilite[c.pad] or Mesh.hilite[1], st.x, hy, st.z, 0, 0, 0, 1, 2)
    end
  end
  for _, st in ipairs(run.stations) do
    if (st.item or st.box or st.fire > 0 or st.n or st.dirty) and not st.drop then draw_station_top(run, st) end
  end
  for _, l in ipairs(run.loose) do
    draw_item(l.item, l.x, l.y, l.z, l.spin or 0)
  end
  Dis.draw(run)
  Haz.draw(run)
  if run.endless then End.draw3d(run) end
  for _, c in ipairs(run.chefs) do
    c.head_y = Ren.chef(c, c.x, c.y, c.z, c.yaw)
  end
  Fx.draw3d(run)
end
