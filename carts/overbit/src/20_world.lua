-- The world of the training range (M31.1): a square yard at sunset with
-- walls, crates, a ramp and platforms; boxes for the collisions and the
-- rays. The map of step 3 (Partenope) replaces it with the C world.

World = { boxes = {}, spawn = { x = 0, y = 0, z = -14, yaw = 0 } }

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
  draw3d(meshes.hills, 0, 0, 0, 0, 0, 0, 1, 1 + 2)
  draw3d(meshes.ground, 0, 0, 0, 0, 0, 0, 1, 1)
  draw3d(meshes.lines, 0, 0, 0, 0, 0, 0, 1, 1 + 2)
  draw3d(meshes.static, 0, 0, 0)
end

-- the light of the range: a low warm sun, blue sky light, warm bounce
function World.light()
  light3d(-0.55, 0.62, 0.55, 0.42)
  sky3d(0xFFE2BC, 0xA8C0F0, 0x9A8070)
  fog3d(0xC890A0, 40, 140)
end

-- the collision world in C (world3d): the solid boxes
local CW

function World.build_collision()
  CW = world3d()
  for _, b in ipairs(boxes) do
    if b.solid then world_box(CW, b[1], b[2], b[3], b[4], b[5], b[6]) end
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
