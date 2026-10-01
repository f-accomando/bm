-- Texture Room (M14): a room in 3D with textures everywhere, at 320x180,
-- to see what the textured rasterizer does at 60 fps. The same file makes
-- Texture Room HD at 640x360 (M30), for the 3D drawn by the GPU.
-- Arrows: walk and turn. A: automatic tour on/off. B: more or fewer crates.
-- X: light on the floor on/off. Y: hide the numbers.
-- Top left: ms of CPU per frame (16.7 is the limit for 60 fps), fps,
-- triangles and textured pixels drawn.

local W, H = SCREEN_W, SCREEN_H
local T = 32                             -- texture size in the sheet (4x4 of 32x32)
local STONE, BRICK, CRATE, METAL, MOSS, CHECKER, PLANKS, LOGO = 0, 1, 2, 3, 4, 5, 6, 7
local ROOM = 12                          -- the room is 2*ROOM wide and deep
local WALL_H = 5

-- a mesh builder: quads with a texture, four corners clockwise as seen
-- from the side that shows (bottom left, top left, top right, bottom right)
local function builder()
  local b = { v = {}, f = {}, uv = {}, n = 0 }
  function b.quad(p1, p2, p3, p4, t)
    local u0, v0 = (t % 4) * T + 0.5, (t // 4) * T + 0.5
    local u1, v1 = u0 + T - 1, v0 + T - 1
    local i = b.n
    for _, p in ipairs({ p1, p2, p3, p4 }) do
      local k = #b.v
      b.v[k + 1], b.v[k + 2], b.v[k + 3] = p[1], p[2], p[3]
    end
    b.n = i + 4
    local f, uv = b.f, b.uv
    -- triangles (1 2 3) and (1 3 4); colour -1 = textured
    for _, t3 in ipairs({ { 1, 2, 3, u0, v1, u0, v0, u1, v0 }, { 1, 3, 4, u0, v1, u1, v0, u1, v1 } }) do
      local k = #f
      f[k + 1], f[k + 2], f[k + 3], f[k + 4] = i + t3[1], i + t3[2], i + t3[3], -1
      local j = #uv
      for q = 4, 9 do uv[j + q - 3] = t3[q] end
    end
  end
  function b.build() return mesh(b.v, b.f, b.uv) end
  return b
end

local floor_mesh, walls_mesh, crate_mesh, pillar_mesh

local function make_room()
  -- floor: tiles of 2x2 units, stone with moss here and there
  local b = builder()
  for i = -ROOM, ROOM - 2, 2 do
    for j = -ROOM, ROOM - 2, 2 do
      local t = ((i * 7 + j * 13) % 11 == 0) and MOSS or STONE
      if i == -2 and j == -2 then t = LOGO end
      b.quad({ i, 0, j }, { i, 0, j + 2 }, { i + 2, 0, j + 2 }, { i + 2, 0, j }, t)
    end
  end
  floor_mesh = b.build()

  -- walls, seen from inside: the far wall (z = ROOM), turned for the others
  b = builder()
  local turns = { function(x, y, z) return x, y, z end, function(x, y, z) return z, y, -x end,
                  function(x, y, z) return -x, y, -z end, function(x, y, z) return -z, y, x end }
  for _, rot in ipairs(turns) do
    for i = -ROOM, ROOM - 2, 2 do
      for y = 0, WALL_H - 1, 2.5 do
        local y1 = y + 2.5
        local p = { { i, y, ROOM }, { i, y1, ROOM }, { i + 2, y1, ROOM }, { i + 2, y, ROOM } }
        for k, q in ipairs(p) do p[k] = { rot(q[1], q[2], q[3]) } end
        b.quad(p[1], p[2], p[3], p[4], y == 0 and PLANKS or BRICK)
      end
    end
  end
  walls_mesh = b.build()

  -- a crate: a cube of side 2 with the crate texture on every face
  local c = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
              { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } }
  local faces = { { 1, 4, 3, 2 }, { 5, 6, 7, 8 }, { 1, 2, 6, 5 }, { 4, 8, 7, 3 }, { 1, 5, 8, 4 }, { 2, 3, 7, 6 } }
  b = builder()
  for _, q in ipairs(faces) do b.quad(c[q[1]], c[q[2]], c[q[3]], c[q[4]], CRATE) end
  crate_mesh = b.build()

  -- a metal pillar: a box 1 x WALL_H x 1 without top and bottom
  b = builder()
  local s = 0.6
  local p = { { -s, 0, -s }, { s, 0, -s }, { s, WALL_H, -s }, { -s, WALL_H, -s },
              { -s, 0, s }, { s, 0, s }, { s, WALL_H, s }, { -s, WALL_H, s } }
  for _, q in ipairs({ { 1, 4, 3, 2 }, { 5, 6, 7, 8 }, { 1, 5, 8, 4 }, { 2, 3, 7, 6 } }) do
    b.quad(p[q[1]], p[q[2]], p[q[3]], p[q[4]], METAL)
  end
  pillar_mesh = b.build()
end

local crates = {}
local CRATE_COUNTS = { 4, 8, 16, 32 }
local count_i = 2

local function place_crates()
  crates = {}
  local n = CRATE_COUNTS[count_i]
  for k = 1, n do
    local a = k / n * 6.2832
    local r = 4 + (k % 3) * 2.2
    crates[k] = { x = math.cos(a) * r, z = math.sin(a) * r, spin = (k % 2 == 0) and 0.6 or -0.4,
                  bob = k * 0.7, size = 0.55 + (k % 3) * 0.15 }
  end
end

local cam = { x = 0, z = -9, yaw = 0 }
local tour, lit_floor, show_hud = true, true, true
local t = 0

function _init()
  make_room()
  place_crates()
  light3d(0.4, 0.8, -0.3, 0.45)
end

function _update()
  t = t + 1 / 60
  if btnp(4) then tour = not tour end
  if btnp(5) then count_i = count_i % #CRATE_COUNTS + 1; place_crates() end
  if btnp(6) then lit_floor = not lit_floor end
  if btnp(7) then show_hud = not show_hud end
  if tour then
    cam.yaw = t * 0.25
    local r = 8.5
    cam.x, cam.z = -math.sin(cam.yaw) * r, -math.cos(cam.yaw) * r
  else
    if btn(0) then cam.yaw = cam.yaw - 0.04 end
    if btn(1) then cam.yaw = cam.yaw + 0.04 end
    local sp = (btn(2) and 0.12 or 0) - (btn(3) and 0.12 or 0)
    cam.x = cam.x + math.sin(cam.yaw) * sp
    cam.z = cam.z + math.cos(cam.yaw) * sp
    local lim = ROOM - 1
    cam.x = math.max(-lim, math.min(lim, cam.x))
    cam.z = math.max(-lim, math.min(lim, cam.z))
  end
end

function _draw()
  cls(0x101018)
  zclear()
  camera3d(cam.x, 1.7, cam.z, cam.yaw, -0.12, 70)
  -- the floor first and without the z-buffer (nothing is under it)
  draw3d(floor_mesh, 0, 0, 0, 0, 0, 0, 1, lit_floor and 1 or 3)
  draw3d(walls_mesh, 0, 0, 0)
  for _, a in ipairs({ { -6, -6 }, { 6, -6 }, { -6, 6 }, { 6, 6 } }) do
    draw3d(pillar_mesh, a[1], 0, a[2])
  end
  for _, c in ipairs(crates) do
    local y = c.size + 0.15 * math.sin(t * 2 + c.bob)
    draw3d(crate_mesh, c.x, y, c.z, 0, t * c.spin, 0, c.size)
  end
  if show_hud then
    rectfill(0, 0, W, 18, 0x000000)
    -- stat(6): the GPU draws the 3D (Settings > 3D of the games); no pixel count then
    local px = stat(6) == 1 and "  GPU" or string.format("%5dpx", stat(5))
    print(string.format("%4.1f ms %2d fps %4d tri %s", stat(1), stat(2), stat(4), px), 2, 1, 0xFFE060)
    print(string.format("A tour %s  B crates %d", tour and "on" or "off", #crates), 2, H - 17, 0xC0C0C0)
    if W >= 640 and stat(6) ~= 1 then
      print("drawn by the ARM: Settings > 3D of the games > GPU", W - 8 * 51, H - 17, 0x808080)
    end
  end
end
