-- The worlds: the training range (M31.1: a square yard at sunset with
-- walls, crates, a ramp and platforms, lit as it is drawn) and the maps
-- (M31.3: Partenope; made by carts/overbit/art/partenope.py with the light
-- baked into the faces, in chunks of the ground drawn only when the camera
-- sees them). Boxes for the collisions and the rays, in C (world3d).

World = { boxes = {}, spawn = { x = 0, y = 0, z = -14, yaw = 0 }, kind = "range" }

local boxes = World.boxes
local meshes = {}

-- a box from (x0, y0, z0) to (x1, y1, z1), drawn with colours (sides, top)
local function add_box(x0, y0, z0, x1, y1, z1, side, top, solid)
  local b = { x0, y0, z0, x1, y1, z1, side = side, top = top or side, solid = solid ~= false }
  boxes[#boxes + 1] = b
  return b
end

-- the faces of a box, clockwise from outside (r3d), into v, f
local function box_faces(v, f, b)
  local x0, y0, z0, x1, y1, z1 = b[1], b[2], b[3], b[4], b[5], b[6]
  local function quad(c, ...)
    local p = { ... }
    local n = #v // 3
    for i = 1, 12 do v[#v + 1] = p[i] end
    f[#f + 1] = n + 1 f[#f + 1] = n + 2 f[#f + 1] = n + 3 f[#f + 1] = c
    f[#f + 1] = n + 1 f[#f + 1] = n + 3 f[#f + 1] = n + 4 f[#f + 1] = c
  end
  local s, t = b.side, b.top
  quad(t, x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0)      -- top (seen from above)
  quad(s, x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0)      -- -z
  quad(s, x1, y0, z1, x1, y1, z1, x0, y1, z1, x0, y0, z1)      -- +z
  quad(s, x0, y0, z1, x0, y1, z1, x0, y1, z0, x0, y0, z0)      -- -x
  quad(s, x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1)      -- +x
  if y0 > 0.01 then
    quad(s, x0, y0, z1, x0, y0, z0, x1, y0, z0, x1, y0, z1)    -- bottom
  end
end

function World.init()
  -- the yard: 64 x 64, walls 6 m high
  local W, H = 32, 6
  local wall, wtop = 0xC8C2B8, 0xA49E94
  add_box(-W - 1, 0, -W - 1, W + 1, H, -W, wall, wtop)
  add_box(-W - 1, 0, W, W + 1, H, W + 1, wall, wtop)
  add_box(-W - 1, 0, -W, -W, H, W, wall, wtop)
  add_box(W, 0, -W, W + 1, H, W, wall, wtop)
  -- hazard stripes along the walls (thin, not solid)
  add_box(-W, 0.9, -W, W, 1.2, -W + 0.05, 0xF26A21, 0xF26A21, false)
  add_box(-W, 0.9, W - 0.05, W, 1.2, W, 0xF26A21, 0xF26A21, false)
  -- crates and cover
  local crate, ctop = 0x7A8794, 0x93A1AE
  add_box(-8, 0, -4, -6, 1.6, -2, crate, ctop)
  add_box(-6, 0, -4, -4, 1.0, -2, crate, ctop)
  add_box(6, 0, 2, 9, 2.2, 4, crate, ctop)
  add_box(-14, 0, 8, -10, 3.2, 12, 0x5E6A78, 0x7B8896)
  add_box(12, 0, -12, 15, 1.2, -9, crate, ctop)
  add_box(-3, 0, 14, 3, 0.6, 16, 0x8A7A68, 0xA08E7A)
  -- a platform with steps up to it
  add_box(16, 0, 10, 26, 3.0, 20, 0x6B7682, 0x8794A2)
  for i = 1, 5 do
    add_box(13 + i * 0, 0, 10 + i * 1.6 - 1.6, 16, i * 0.6, 10 + i * 1.6, 0x7D8894, 0x95A1AD)
  end
  -- pillars
  for _, p in ipairs({ { -20, -20 }, { 20, -20 }, { -20, 20 } }) do
    add_box(p[1] - 0.8, 0, p[2] - 0.8, p[1] + 0.8, 7, p[2] + 0.8, 0xB8B0A4, 0xCCC4B8)
  end
  -- one mesh for every box
  local v, f = {}, {}
  for _, b in ipairs(boxes) do box_faces(v, f, b) end
  meshes.static = mesh(v, f)
  -- the ground: big tiles in two tones (flat, drawn first without the z-buffer)
  v, f = {}, {}
  local T = 8
  for i = -4, 3 do
    for j = -4, 3 do
      local x0, z0 = i * T, j * T
      local c = ((i + j) & 1 == 0) and 0x6E737A or 0x666B72
      local n = #v // 3
      for _, q in ipairs({ x0, 0, z0, x0, 0, z0 + T, x0 + T, 0, z0 + T, x0 + T, 0, z0 }) do v[#v + 1] = q end
      f[#f + 1] = n + 1 f[#f + 1] = n + 2 f[#f + 1] = n + 3 f[#f + 1] = c
      f[#f + 1] = n + 1 f[#f + 1] = n + 3 f[#f + 1] = n + 4 f[#f + 1] = c
    end
  end
  meshes.ground = mesh(v, f)
  -- lines on the ground: lanes of the range (thin quads)
  v, f = {}, {}
  for k = -3, 3 do
    local x = k * 6
    local n = #v // 3
    for _, q in ipairs({ x - 0.06, 0.01, -30, x - 0.06, 0.01, 30, x + 0.06, 0.01, 30, x + 0.06, 0.01, -30 }) do
      v[#v + 1] = q
    end
    f[#f + 1] = n + 1 f[#f + 1] = n + 2 f[#f + 1] = n + 3 f[#f + 1] = 0xD8D2C4
    f[#f + 1] = n + 1 f[#f + 1] = n + 3 f[#f + 1] = n + 4 f[#f + 1] = 0xD8D2C4
  end
  meshes.lines = mesh(v, f)
  -- far hills: a ring of dark shapes against the sky (unlit)
  v, f = {}, {}
  local N, R = 24, 180
  for i = 0, N - 1 do
    local a0, a1 = 2 * pi * i / N, 2 * pi * (i + 1) / N
    local h0 = 12 + 10 * sin(i * 1.7) + 6 * sin(i * 0.6)
    local h1 = 12 + 10 * sin((i + 1) * 1.7) + 6 * sin((i + 1) * 0.6)
    local n = #v // 3
    for _, q in ipairs({ R * cos(a0), -1, R * sin(a0), R * cos(a0), h0, R * sin(a0),
                         R * cos(a1), h1, R * sin(a1), R * cos(a1), -1, R * sin(a1) }) do v[#v + 1] = q end
    -- seen from inside the ring
    f[#f + 1] = n + 1 f[#f + 1] = n + 3 f[#f + 1] = n + 2 f[#f + 1] = 0x3A3550
    f[#f + 1] = n + 1 f[#f + 1] = n + 4 f[#f + 1] = n + 3 f[#f + 1] = 0x3A3550
  end
  meshes.hills = mesh(v, f)
  World.build_collision()
end

-- the sky: bands from the zenith to the horizon (2D, under everything)
local SKY = { 0x2A3A6A, 0x3D5486, 0x5C6FA0, 0x8A84AE, 0xC08C9C, 0xF0A07A, 0xFFC27A }

function World.draw_sky(pitch)
  if World.kind == "map" then return World.map_sky(pitch) end
  local h = SCREEN_H
  -- the horizon on screen moves with the pitch (focal = w/2 / tan(fov/2))
  local hy = h / 2 + math.tan(pitch) * Cam.focal
  local n = #SKY
  local top = hy - 140
  rectfill(0, 0, SCREEN_W, max(0, floor(top)), SKY[1])
  for i = 1, n do
    local y0 = floor(top + (i - 1) * 140 / n)
    local y1 = floor(top + i * 140 / n)
    if y1 > 0 and y0 < h then rectfill(0, y0, SCREEN_W, y1 - y0, SKY[i]) end
  end
  if hy < h then rectfill(0, floor(hy), SCREEN_W, h - floor(hy), 0x56525E) end
end

function World.draw()
  if World.kind == "map" then return World.map_draw() end
  draw3d(meshes.hills, 0, 0, 0, 0, 0, 0, 1, 1 + 2)
  draw3d(meshes.ground, 0, 0, 0, 0, 0, 0, 1, 1)
  draw3d(meshes.lines, 0, 0, 0, 0, 0, 0, 1, 1 + 2)
  draw3d(meshes.static, 0, 0, 0)
end

-- the light of the range: a low warm sun, blue sky light, warm bounce
function World.light()
  if World.kind == "map" then
    -- the sun of the bake (partenope.py), for the heroes and the effects
    light3d(-0.78, 0.36, -0.5, 0.42)
    sky3d(0xFFB070, 0x9AA8E0, 0x9A7060)
    fog3d(0xD8A0A0, 45, 150)
    return
  end
  light3d(-0.55, 0.62, 0.55, 0.42)
  sky3d(0xFFE2BC, 0xA8C0F0, 0x9A8070)
  fog3d(0xC890A0, 40, 140)
end

-- the collision world in C (world3d): the solid boxes
local CW, CW_range

function World.build_collision()
  CW = world3d()
  for _, b in ipairs(boxes) do
    if b.solid then world_box(CW, b[1], b[2], b[3], b[4], b[5], b[6]) end
  end
  CW_range = CW
end

-- ---------------------------------------------------------------- the map

local MAPW                      -- the loaded map: chunks, collision, marks

local function load_map()
  local M = World.MAP
  local w = { chunks = {}, far = {}, marks = M.marks }
  for _, c in ipairs(M.chunks) do
    local m = model(c[1])
    if m then
      local ch = { mesh = m, x0 = c[2], y0 = c[3], z0 = c[4], x1 = c[5], y1 = c[6], z1 = c[7] }
      ch.cx, ch.cy, ch.cz = (c[2] + c[5]) / 2, (c[3] + c[6]) / 2, (c[4] + c[7]) / 2
      ch.r = len3(c[5] - c[2], c[6] - c[3], c[7] - c[4]) / 2
      if c.far then w.far[#w.far + 1] = ch else w.chunks[#w.chunks + 1] = ch end
    end
  end
  w.pvs = M.pvs
  w.cw = world3d()
  local b = M.boxes
  for i = 1, #b, 6 do world_box(w.cw, b[i], b[i + 1], b[i + 2], b[i + 3], b[i + 4], b[i + 5]) end
  log(string.format("overbit map %s: %d chunks, %d boxes", M.name, #w.chunks, #b // 6))
  return w
end

-- the world in use: "range" or "map"
function World.use(kind)
  if kind == "map" and World.MAP then
    MAPW = MAPW or load_map()
    CW = MAPW.cw
    World.kind = "map"
    local s1 = MAPW.marks.spawn1
    World.spawn = { x = s1.x, y = 0, z = s1.z, yaw = s1.yaw }
  else
    CW = CW_range
    World.kind = "range"
    World.spawn = { x = 0, y = 0, z = -14, yaw = 0 }
  end
end

function World.mark(name)
  return MAPW and MAPW.marks[name]
end

-- the chunks the camera sees (a sphere round each against the sides of the
-- view), nearest first; far ones without their small things
local vis = {}
function World.map_draw()
  local cx, cy, cz, yaw, pitch = Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch
  local cp = cos(pitch)
  local fx, fy, fz = sin(yaw) * cp, sin(pitch), cos(yaw) * cp
  local rx, rz = cos(yaw), -sin(yaw)
  local ux, uy, uz = -sin(pitch) * sin(yaw), cp, -sin(pitch) * cos(yaw)
  local th = math.tan(Cam.fov * pi / 360)
  local tv = th * SCREEN_H / SCREEN_W
  local sh, sv = sqrt(1 + th * th), sqrt(1 + tv * tv)
  local q = G.quality
  local near_d = ({ 16, 22, 28, 34, 40 })[q + 1]
  -- the far scenery first, without the fog (its colours are hazy already)
  fog3d()
  for _, c in ipairs(MAPW.far) do draw3d(c.mesh, 0, 0, 0, 0, 0, 0, 1, 16 + 16) end
  World.fog_on()
  -- the chunks that can be seen from the camera's square of the ground
  -- (precomputed: mapbake.py), else all of them
  local list, set = MAPW.chunks, nil
  local pv = MAPW.pvs
  if pv and cy < pv.max_y then
    local i, j = floor((cx - pv.x0) / pv.cell), floor((cz - pv.z0) / pv.cell)
    if i >= 0 and i < pv.nx and j >= 0 and j < pv.nz then set = pv.sets[j * pv.nx + i + 1] end
  end
  local n = 0
  local count = set and #set or #list
  for k = 1, count do
    local c = set and list[set:byte(k)] or list[k]
    local dx, dy, dz = c.cx - cx, c.cy - cy, c.cz - cz
    local z = dx * fx + dy * fy + dz * fz
    if z > -c.r then
      local x = dx * rx + dz * rz
      local y = dx * ux + dy * uy + dz * uz
      if abs(x) <= z * th + c.r * sh and abs(y) <= z * tv + c.r * sv then
        -- the distance to the box
        local ex = max(c.x0 - cx, 0, cx - c.x1)
        local ey = max(c.y0 - cy, 0, cy - c.y1)
        local ez = max(c.z0 - cz, 0, cz - c.z1)
        n = n + 1
        local v = vis[n] or {}
        vis[n] = v
        v.c, v.d = c, ex * ex + ey * ey + ez * ez
      end
    end
  end
  for i = n + 1, #vis do vis[i] = nil end
  table.sort(vis, function(a, b) return a.d < b.d end)
  local nd2 = near_d * near_d
  for i = 1, n do
    local v = vis[i]
    draw3d(v.c.mesh, 0, 0, 0, 0, 0, 0, 1, v.d < nd2 and 0 or 32)       -- 32: detail 1, no small things
  end
  World.chunks_drawn = n
end

function World.fog_on()
  if World.kind == "map" then fog3d(0xD8A0A0, 45, 150) else fog3d(0xC890A0, 40, 140) end
end

-- the sky of the map: the sunset in bands, the sun low over the sea, two
-- streaks of cloud lit from below
local MSKY = { 0x2A3468, 0x3E4C86, 0x5E66A0, 0x8C78A8, 0xBC84A0, 0xE69488, 0xFFB078, 0xFFCE8A }
local SUN = { -0.78, 0.36, -0.5 }
function World.map_sky(pitch)
  local h = SCREEN_H
  local hy = h / 2 + math.tan(pitch) * Cam.focal
  local n = #MSKY
  local span = 150
  local top = hy - span
  rectfill(0, 0, SCREEN_W, max(0, floor(top)), MSKY[1])
  for i = 1, n do
    local y0 = floor(top + (i - 1) * span / n)
    local y1 = floor(top + i * span / n)
    if y1 > 0 and y0 < h then rectfill(0, y0, SCREEN_W, y1 - y0, MSKY[i]) end
  end
  if hy < h then rectfill(0, floor(hy), SCREEN_W, h - floor(hy), 0x6A6E8C) end
  -- the sun (its direction from the camera, far away)
  local sx, sy = project3d(Cam.x + SUN[1] * 800, Cam.y + SUN[2] * 800 * 0.25, Cam.z + SUN[3] * 800)
  if sx then
    local x, y = floor(sx), floor(sy)
    circfill(x, y, 22, 0xFFC890)
    circfill(x, y, 16, 0xFFDDA8)
    circfill(x, y, 11, 0xFFF4DC)
  end
  -- clouds: long thin streaks, warm under, at fixed headings
  for i = 1, 5 do
    local a = -2.2 + i * 0.55
    local cx2, cy2 = project3d(Cam.x + sin(a) * 700, Cam.y + 120 + i * 18, Cam.z + cos(a) * 700)
    if cx2 then
      local w = 40 + i * 9
      rectfill(floor(cx2 - w), floor(cy2), w * 2, 3, 0xF2A8A0)
      rectfill(floor(cx2 - w * 0.7), floor(cy2) - 2, floor(w * 1.4), 2, 0xD890A8)
    end
  end
end

-- a ray from o along d (unit): distance to the first wall or the ground, and
-- the normal there; nil if nothing within maxd
-- (and the props: then the prop is the 5th value)
function World.ray(ox, oy, oz, dx, dy, dz, maxd)
  local t, nx, ny, nz = world_ray(CW, ox, oy, oz, dx, dy, dz, maxd)
  if #Props.list > 0 then
    local pt, px, py, pz, prop = Props.ray(ox, oy, oz, dx, dy, dz, t or maxd)
    if pt then return pt, px, py, pz, prop end
  end
  return t, nx, ny, nz
end

-- the highest floor under (x, z) at or below height y (a radius r around)
function World.floor_at(x, z, y, r)
  return world_floor(CW, x, z, y, r)
end

-- moves an actor (a box of radius r, height h, feet at y) by (dx, dy, dz),
-- sliding along the walls and stepping up 0.45 m; returns on_ground, hit_wall
function World.move(a, dx, dy, dz)
  local x, y, z, f = world_move(CW, a.x, a.y, a.z, a.radius, a.height, dx, dy, dz, 0.45, a.on_ground)
  a.x, a.y, a.z = x, y, z
  if f & 4 ~= 0 then a.vx = 0 end
  if f & 8 ~= 0 then a.vz = 0 end
  if f & 16 ~= 0 and a.vy > 0 then a.vy = 0 end
  return f & 1 ~= 0, f & 2 ~= 0
end

-- is there a clear line from a to b (no wall in between)?
function World.clear(ax, ay, az, bx, by, bz)
  local dx, dy, dz = bx - ax, by - ay, bz - az
  local d = len3(dx, dy, dz)
  if d < 1e-4 then return true end
  if #Props.list > 0 and Props.ray(ax, ay, az, dx / d, dy / d, dz / d, d) then return false end
  return world_ray(CW, ax, ay, az, dx / d, dy / d, dz / d, d) == nil
end
