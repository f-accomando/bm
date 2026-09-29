-- 3D models, built from simple pieces when the cartridge starts (and the
-- kitchen when a stage starts). Faces are turned outwards by the builder,
-- so the vertex order of a piece never matters (Astro Wing's trick).

local function builder()
  local b = { v = {}, f = {}, uv = nil, nv = 0 }
  -- a convex piece: every triangle faces away from the piece's centre
  function b.piece(pts, tris, color)
    local base = #b.v // 3
    local cx, cy, cz = 0, 0, 0
    for _, p in ipairs(pts) do
      local n = #b.v
      b.v[n + 1], b.v[n + 2], b.v[n + 3] = p[1], p[2], p[3]
      cx, cy, cz = cx + p[1], cy + p[2], cz + p[3]
    end
    cx, cy, cz = cx / #pts, cy / #pts, cz / #pts
    for _, t in ipairs(tris) do
      local a, c, d = pts[t[1]], pts[t[2]], pts[t[3]]
      local ux, uy, uz = c[1] - a[1], c[2] - a[2], c[3] - a[3]
      local vx, vy, vz = d[1] - a[1], d[2] - a[2], d[3] - a[3]
      local nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
      local mx, my, mz = (a[1] + c[1] + d[1]) / 3 - cx, (a[2] + c[2] + d[2]) / 3 - cy, (a[3] + c[3] + d[3]) / 3 - cz
      local i, j, k = base + t[1], base + t[2], base + t[3]
      if nx * mx + ny * my + nz * mz < 0 then j, k = k, j end
      local n = #b.f
      b.f[n + 1], b.f[n + 2], b.f[n + 3], b.f[n + 4] = i, j, k, t[4] or color
      if b.uv then
        local u = t[5]
        local m = #b.uv
        for q = 1, 6 do b.uv[m + q] = u and u[q] or 0 end
        if u and nx * mx + ny * my + nz * mz < 0 then
          -- the swapped vertices take their texture coordinates along
          b.uv[m + 3], b.uv[m + 5] = u[5], u[3]
          b.uv[m + 4], b.uv[m + 6] = u[6], u[4]
        end
      end
    end
  end
  -- a flat quad facing (nx, ny, nz): p1..p4 around the edge
  function b.quad(p1, p2, p3, p4, color, nx, ny, nz, uv)
    local base = #b.v // 3
    for _, p in ipairs({ p1, p2, p3, p4 }) do
      local n = #b.v
      b.v[n + 1], b.v[n + 2], b.v[n + 3] = p[1], p[2], p[3]
    end
    local ux, uy, uz = p2[1] - p1[1], p2[2] - p1[2], p2[3] - p1[3]
    local vx, vy, vz = p3[1] - p1[1], p3[2] - p1[2], p3[3] - p1[3]
    local cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    local flip = cx * nx + cy * ny + cz * nz < 0
    local tris = flip and { { 1, 3, 2 }, { 1, 4, 3 } } or { { 1, 2, 3 }, { 1, 3, 4 } }
    for ti, t in ipairs(tris) do
      local n = #b.f
      b.f[n + 1], b.f[n + 2], b.f[n + 3], b.f[n + 4] = base + t[1], base + t[2], base + t[3], uv and -1 or color
      if b.uv then
        local m = #b.uv
        for q = 1, 3 do
          local c = uv and uv[t[q]]
          b.uv[m + q * 2 - 1], b.uv[m + q * 2] = c and c[1] or 0, c and c[2] or 0
        end
      end
    end
  end
  -- box with optional colours for the top and the front (+z) face
  function b.box(x0, y0, z0, x1, y1, z1, color, top, front)
    b.piece({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
              { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } },
            { { 1, 2, 3 }, { 1, 3, 4 }, { 5, 6, 7, front }, { 5, 7, 8, front }, { 1, 2, 6 }, { 1, 6, 5 },
              { 4, 3, 7, top }, { 4, 7, 8, top }, { 1, 4, 8 }, { 1, 8, 5 }, { 2, 3, 7 }, { 2, 7, 6 } }, color)
  end
  -- a box with only some faces: t top, b bottom, n +z, s -z, e +x, w -x;
  -- colours: top and the +z face may differ
  function b.boxf(x0, y0, z0, x1, y1, z1, color, faces, top, north, south)
    if faces:find("t", 1, true) then b.quad({ x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 }, top or color, 0, 1, 0) end
    if faces:find("b", 1, true) then b.quad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 }, color, 0, -1, 0) end
    if faces:find("n", 1, true) then b.quad({ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, north or color, 0, 0, 1) end
    if faces:find("s", 1, true) then b.quad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 }, south or color, 0, 0, -1) end
    if faces:find("e", 1, true) then b.quad({ x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 }, color, 1, 0, 0) end
    if faces:find("w", 1, true) then b.quad({ x0, y0, z0 }, { x0, y1, z0 }, { x0, y1, z1 }, { x0, y0, z1 }, color, -1, 0, 0) end
  end
  -- the same without the bottom face (sits on something)
  function b.block(x0, y0, z0, x1, y1, z1, color, top, front)
    b.piece({ { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
              { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } },
            { { 1, 2, 3 }, { 1, 3, 4 }, { 5, 6, 7, front }, { 5, 7, 8, front },
              { 4, 3, 7, top }, { 4, 7, 8, top }, { 1, 4, 8 }, { 1, 8, 5 }, { 2, 3, 7 }, { 2, 7, 6 } }, color)
  end
  -- n-sided prism / frustum around the y axis at (cx, cz)
  function b.prism(cx, cz, y0, y1, r0, r1, n, color, top, bottom)
    local pts, tris = {}, {}
    for i = 0, n - 1 do
      local a = i / n * TAU + pi / n
      pts[#pts + 1] = { cx + sin(a) * r0, y0, cz + cos(a) * r0 }
      pts[#pts + 1] = { cx + sin(a) * r1, y1, cz + cos(a) * r1 }
    end
    for i = 0, n - 1 do
      local a0, a1 = i * 2 + 1, ((i + 1) % n) * 2 + 1
      tris[#tris + 1] = { a0, a1, a1 + 1 }
      tris[#tris + 1] = { a0, a1 + 1, a0 + 1 }
    end
    if top ~= false then
      for i = 1, n - 2 do tris[#tris + 1] = { 2, (i * 2) + 2, (i * 2) + 4, top } end
    end
    if bottom then
      for i = 1, n - 2 do tris[#tris + 1] = { 1, (i * 2) + 1, (i * 2) + 3, bottom } end
    end
    b.piece(pts, tris, color)
  end
  -- low-poly ellipsoid: `segs` around, `rings` from pole to pole; c2 colours
  -- the top half when given
  function b.ball(cx, cy, cz, rx, ry, rz, color, segs, rings, c2)
    segs, rings = segs or 6, rings or 3
    local pts, tris = { { cx, cy + ry, cz } }, {}
    for r = 1, rings - 1 do
      local th = r / rings * pi
      for s = 0, segs - 1 do
        local ph = s / segs * TAU + (r % 2) * pi / segs
        pts[#pts + 1] = { cx + sin(th) * cos(ph) * rx, cy + cos(th) * ry, cz + sin(th) * sin(ph) * rz }
      end
    end
    pts[#pts + 1] = { cx, cy - ry, cz }
    local last = #pts
    for s = 0, segs - 1 do
      tris[#tris + 1] = { 1, 2 + s, 2 + (s + 1) % segs, c2 }
      local b0 = 2 + (rings - 2) * segs
      tris[#tris + 1] = { last, b0 + s, b0 + (s + 1) % segs }
    end
    for r = 1, rings - 2 do
      local a0, b0 = 2 + (r - 1) * segs, 2 + r * segs
      local col = (c2 and r < rings / 2) and c2 or nil
      for s = 0, segs - 1 do
        local s1 = (s + 1) % segs
        tris[#tris + 1] = { a0 + s, a0 + s1, b0 + s, col }
        tris[#tris + 1] = { a0 + s1, b0 + s1, b0 + s, col }
      end
    end
    b.piece(pts, tris, color)
  end
  -- a triangle facing (nx, ny, nz)
  function b.tri(p1, p2, p3, color, nx, ny, nz)
    local base = #b.v // 3
    for _, p in ipairs({ p1, p2, p3 }) do
      local n = #b.v
      b.v[n + 1], b.v[n + 2], b.v[n + 3] = p[1], p[2], p[3]
    end
    local ux, uy, uz = p2[1] - p1[1], p2[2] - p1[2], p2[3] - p1[3]
    local vx, vy, vz = p3[1] - p1[1], p3[2] - p1[2], p3[3] - p1[3]
    local cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    local n = #b.f
    if cx * nx + cy * ny + cz * nz < 0 then
      b.f[n + 1], b.f[n + 2], b.f[n + 3], b.f[n + 4] = base + 1, base + 3, base + 2, color
    else
      b.f[n + 1], b.f[n + 2], b.f[n + 3], b.f[n + 4] = base + 1, base + 2, base + 3, color
    end
    if b.uv then
      local m = #b.uv
      for q = 1, 6 do b.uv[m + q] = 0 end
    end
  end
  -- a flat disc facing up, n sides
  function b.disc(cx, y, cz, r, n, color)
    local pts = {}
    for i = 0, n - 1 do
      local a = i / n * TAU
      pts[#pts + 1] = { cx + sin(a) * r, y, cz + cos(a) * r }
    end
    for i = 2, n - 1 do b.tri(pts[1], pts[i], pts[i + 1], color, 0, 1, 0) end
  end
  -- a single face towards +z (eyes, mouths, buttons, labels)
  function b.decal(x0, y0, x1, y1, z, color)
    b.quad({ x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z }, color, 0, 0, 1)
  end
  function b.build()
    if #b.f == 0 then return nil end
    return mesh(b.v, b.f, b.uv)
  end
  function b.count() return #b.f // 4 end
  return b
end
Mesh.builder = builder

---------------------------------------------------------------- chefs

-- Each chef: a rigid body (torso, head, hat, face) drawn from the hips, two
-- legs pivoting at the hips, two arms pivoting at the shoulders.
-- rig: hip height, hip half-width, shoulder height, shoulder half-width.
local SKIN, SKIN2 = 0xF4C8A0, 0xD89C78
local DARK = 0x1C1C24

local function make_basil()
  local b = builder()
  local J, JT = 0xF4F4F0, 0xFFFFFF          -- white jacket
  -- jacket
  b.block(-0.19, 0.0, -0.12, 0.19, 0.42, 0.12, J, JT)
  b.decal(-0.07, 0.3, 0.07, 0.4, 0.121, 0xE03A30)      -- red neckerchief
  b.decal(-0.03, 0.34, 0.03, 0.22, 0.122, 0xC02A20)
  for i = 0, 2 do                                       -- double buttons
    b.decal(-0.09, 0.2 - i * 0.07, -0.06, 0.23 - i * 0.07, 0.121, 0x505058)
    b.decal(0.06, 0.2 - i * 0.07, 0.09, 0.23 - i * 0.07, 0.121, 0x505058)
  end
  -- head
  b.ball(0, 0.56, 0, 0.15, 0.15, 0.14, SKIN, 6, 3)
  b.box(-0.15, 0.52, -0.14, 0.15, 0.64, -0.05, 0x6A3A20)   -- hair at the back
  b.decal(-0.08, 0.57, -0.04, 0.62, 0.135, DARK)           -- eyes
  b.decal(0.04, 0.57, 0.08, 0.62, 0.135, DARK)
  b.decal(-0.1, 0.645, -0.03, 0.665, 0.13, 0x4A2A18)       -- determined brows
  b.decal(0.03, 0.645, 0.1, 0.665, 0.13, 0x4A2A18)
  b.decal(-0.04, 0.49, 0.04, 0.51, 0.138, 0x8A3A30)        -- mouth
  -- the tall toque
  b.prism(0, 0, 0.66, 0.9, 0.12, 0.13, 6, 0xF8F8F8, false)
  b.ball(0, 0.94, 0, 0.17, 0.09, 0.16, 0xFFFFFF, 6, 2)
  local body = b.build()
  b = builder()
  b.boxf(-0.05, -0.4, -0.05, 0.05, 0.0, 0.05, 0x3A3E48, "nsew")
  b.boxf(-0.06, -0.45, -0.06, 0.06, -0.37, 0.09, DARK, "tnsew")  -- shoe
  local leg = b.build()
  b = builder()
  b.boxf(-0.045, -0.3, -0.045, 0.045, 0.02, 0.045, J, "nsew")
  b.boxf(-0.04, -0.37, -0.04, 0.04, -0.3, 0.04, SKIN, "bnsew")
  local arm = b.build()
  return { body = body, leg = leg, arm = arm,
           hip = 0.45, hipw = 0.09, sh = 0.4, shw = 0.24, hand = 0.37, top = 1.35, width = 0.2 }
end

local function make_bun()
  local b = builder()
  local SH, AP = 0xF0A020, 0xFFF4E0
  b.ball(0, 0.26, 0, 0.3, 0.3, 0.27, SH, 7, 4)                -- round body
  b.quad({ -0.19, 0.08, 0.26 }, { 0.19, 0.08, 0.26 }, { 0.16, 0.36, 0.26 }, { -0.16, 0.36, 0.26 }, AP, 0, 0, 1)
  b.decal(-0.05, 0.22, 0.05, 0.28, 0.265, 0xF0C060)         -- apron pocket
  b.ball(0, 0.66, 0, 0.18, 0.16, 0.17, SKIN, 6, 3)            -- head
  b.decal(-0.1, 0.66, -0.04, 0.68, 0.165, DARK)              -- happy squints
  b.decal(0.04, 0.66, 0.1, 0.68, 0.165, DARK)
  b.decal(-0.13, 0.6, -0.08, 0.63, 0.16, 0xF08080)           -- rosy cheeks
  b.decal(0.08, 0.6, 0.13, 0.63, 0.16, 0xF08080)
  b.decal(-0.06, 0.57, 0.06, 0.6, 0.168, 0x8A2A20)           -- big grin
  b.ball(0.02, 0.8, -0.02, 0.17, 0.06, 0.16, 0xE04830, 6, 2) -- beret
  b.box(-0.01, 0.84, -0.03, 0.03, 0.9, 0.01, 0xE04830)       -- its stalk
  local body = b.build()
  b = builder()
  b.boxf(-0.06, -0.2, -0.06, 0.06, 0.0, 0.06, 0x8A6A4A, "nsew")
  b.boxf(-0.07, -0.24, -0.07, 0.07, -0.18, 0.1, DARK, "tnsew")
  local leg = b.build()
  b = builder()
  b.boxf(-0.05, -0.2, -0.05, 0.05, 0.02, 0.05, SH, "nsew")
  b.ball(0, -0.24, 0, 0.055, 0.05, 0.055, SKIN, 4, 2)
  local arm = b.build()
  return { body = body, leg = leg, arm = arm,
           hip = 0.24, hipw = 0.13, sh = 0.4, shw = 0.31, hand = 0.24, top = 1.05, width = 0.32 }
end

local function make_noodle()
  local b = builder()
  local V, S = 0x30B060, 0xF8F8F8
  b.block(-0.13, 0.0, -0.09, 0.13, 0.46, 0.09, S, S)          -- shirt
  b.box(-0.135, 0.02, -0.095, -0.05, 0.44, 0.095, V)         -- green vest sides
  b.box(0.05, 0.02, -0.095, 0.135, 0.44, 0.095, V)
  b.ball(0, 0.62, 0, 0.13, 0.14, 0.12, SKIN, 6, 3)            -- head
  b.decal(-0.085, 0.63, -0.02, 0.68, 0.125, 0x80D0FF)        -- glasses
  b.decal(0.02, 0.63, 0.085, 0.68, 0.125, 0x80D0FF)
  b.decal(-0.06, 0.64, -0.04, 0.66, 0.127, DARK)
  b.decal(0.04, 0.64, 0.06, 0.66, 0.127, DARK)
  b.decal(-0.03, 0.555, 0.03, 0.57, 0.123, 0x8A3A30)
  b.box(-0.135, 0.7, -0.125, 0.135, 0.75, 0.125, V)           -- headband
  b.ball(0, 0.74, -0.12, 0.07, 0.06, 0.09, 0x2A1A10, 5, 2)    -- hair bun
  b.box(-0.02, 0.62, -0.26, 0.02, 0.74, -0.14, 0x2A1A10)     -- ponytail
  local body = b.build()
  b = builder()
  b.boxf(-0.035, -0.72, -0.035, 0.035, 0.0, 0.035, 0x202028, "nsew")
  b.boxf(-0.05, -0.78, -0.05, 0.05, -0.7, 0.1, 0x30D070, "tnsew")  -- green sneakers
  local leg = b.build()
  b = builder()
  b.boxf(-0.03, -0.44, -0.03, 0.03, 0.02, 0.03, S, "nsew")
  b.boxf(-0.032, -0.5, -0.032, 0.032, -0.44, 0.032, SKIN, "bnsew")
  local arm = b.build()
  return { body = body, leg = leg, arm = arm,
           hip = 0.77, hipw = 0.06, sh = 0.43, shw = 0.165, hand = 0.5, top = 1.6, width = 0.15 }
end

local function make_pepper()
  local b = builder()
  local A, T = 0x3A70E0, 0xE8E0D0
  b.block(-0.21, 0.0, -0.14, 0.21, 0.34, 0.14, T, T)          -- shirt
  b.quad({ -0.17, 0.02, 0.141 }, { 0.17, 0.02, 0.141 }, { 0.15, 0.3, 0.141 }, { -0.15, 0.3, 0.141 }, A, 0, 0, 1)
  b.ball(0, 0.5, 0, 0.17, 0.16, 0.16, SKIN, 6, 3)             -- big head
  b.decal(-0.09, 0.52, -0.04, 0.55, 0.158, DARK)             -- stern eyes
  b.decal(0.04, 0.52, 0.09, 0.55, 0.158, DARK)
  b.decal(-0.12, 0.565, -0.02, 0.6, 0.152, 0x2A2020)         -- bushy brows
  b.decal(0.02, 0.565, 0.12, 0.6, 0.152, 0x2A2020)
  b.box(-0.13, 0.42, 0.1, 0.13, 0.47, 0.17, 0x241810)         -- the moustache
  b.box(-0.17, 0.62, -0.17, 0.17, 0.68, 0.17, A)              -- flat cap
  b.box(-0.12, 0.62, 0.12, 0.12, 0.645, 0.27, 0x2A50B0)       -- its brim
  local body = b.build()
  b = builder()
  b.boxf(-0.065, -0.2, -0.065, 0.065, 0.0, 0.065, 0x3A3A40, "nsew")
  b.boxf(-0.075, -0.26, -0.075, 0.075, -0.18, 0.11, 0x5A3A20, "tnsew")
  local leg = b.build()
  b = builder()
  b.boxf(-0.055, -0.22, -0.055, 0.055, 0.02, 0.055, T, "nsew")
  b.boxf(-0.05, -0.28, -0.05, 0.05, -0.22, 0.05, SKIN, "bnsew")
  local arm = b.build()
  return { body = body, leg = leg, arm = arm,
           hip = 0.26, hipw = 0.11, sh = 0.3, shw = 0.26, hand = 0.28, top = 0.95, width = 0.24 }
end

-- The chefs' imported models (16_chef_models.lua, from import_chefs.py):
-- the same rig, but each limb has its own mesh and its pivot where the
-- model has the joint (legL/legR/armL/armR: x, y, z above the hips' centre).
local function make_model(i)
  local d = Data.CHEF_MODEL and Data.CHEF_MODEL[i]
  if not d then return nil end
  local function part(p) return p and mesh(p.v, p.f, p.uv) end
  local r = d.rig
  return { body = part(d.body), legL = part(d.legL), legR = part(d.legR),
           armL = part(d.armL), armR = part(d.armR),
           jLegL = r.legL, jLegR = r.legR, jArmL = r.armL, jArmR = r.armR,
           hip = r.hip, hipw = r.hipw or 0.1, sh = r.sh or 0.4, shw = r.shw or 0.2,
           hand = r.hand or 0.3, top = r.top, width = r.width }
end

-- the models, or the chefs built from boxes and balls (the options choose)
function Mesh.use_chefs(classic)
  local m = Mesh.chef_model
  Mesh.chef = {}
  for i = 1, 4 do Mesh.chef[i] = (not classic and m[i]) or Mesh.chef_classic[i] end
end

---------------------------------------------------------------- food

local function darker(c, k) return shade(c, k) end

-- the raw ingredient, about 0.2-0.3 across, resting on y = 0
local function raw_ing(b, g, k)
  local c1, c2, s = g.c1, g.c2, g.size
  k = k or 1
  c1, c2 = darker(c1, k), darker(c2, k)
  local sh = g.shape
  if sh == "ball" then
    b.ball(0, s * 0.85, 0, s, s * 0.85, s, c1, 6, 3)
    b.box(-0.02, s * 1.6, -0.02, 0.02, s * 1.85, 0.02, c2)
  elseif sh == "egg" then
    b.ball(0, s * 1.1, 0, s * 0.8, s * 1.1, s * 0.8, c1, 6, 3)
  elseif sh == "cone" then
    b.piece({ { 0, 0.05, s * 1.3 }, { -0.07, 0.07, -s * 0.5 }, { 0.07, 0.07, -s * 0.5 }, { 0, 0.14, -s * 0.5 }, { 0, 0.0, -s * 0.5 } },
            { { 1, 2, 4 }, { 1, 4, 3 }, { 1, 3, 5 }, { 1, 5, 2 }, { 2, 3, 4 }, { 2, 5, 3 } }, c1)
    b.box(-0.04, 0.04, -s * 0.8, 0.04, 0.12, -s * 0.5, c2)
  elseif sh == "mush" then
    b.prism(0, 0, 0, s * 0.8, s * 0.3, s * 0.35, 5, c2, false)
    b.ball(0, s * 0.85, 0, s, s * 0.5, s, c1, 6, 2)
  elseif sh == "long" then
    b.ball(0, s * 0.35, 0, s * 0.35, s * 0.35, s * 1.3, c1, 6, 3)
  elseif sh == "wedge" then
    b.piece({ { -s, 0, -s * 0.6 }, { s, 0, -s * 0.6 }, { 0, 0, s }, { -s, s * 0.8, -s * 0.6 }, { s, s * 0.8, -s * 0.6 }, { 0, s * 0.8, s } },
            { { 1, 2, 3 }, { 4, 5, 6, c2 }, { 1, 2, 5 }, { 1, 5, 4 }, { 2, 3, 6 }, { 2, 6, 5 }, { 3, 1, 4 }, { 3, 4, 6 } }, c1)
  elseif sh == "loaf" then
    b.block(-s * 0.7, 0, -s, s * 0.7, s * 0.7, s, c1, c1)
    b.ball(0, s * 0.7, 0, s * 0.7, s * 0.3, s, c1, 6, 2)
  elseif sh == "sack" then
    b.prism(0, 0, 0, s * 1.3, s * 0.8, s * 0.6, 6, c2, c1)
    b.box(-s * 0.35, s * 1.3, -s * 0.35, s * 0.35, s * 1.5, s * 0.35, c2)
  elseif sh == "bundle" then
    for i = -1, 1 do b.box(i * 0.06 - 0.02, 0, -s, i * 0.06 + 0.02, 0.05, s, c1) end
    b.box(-0.11, -0.005, -0.04, 0.11, 0.06, 0.04, c2)
  elseif sh == "bottle" then
    b.prism(0, 0, 0, s * 0.9, s * 0.45, s * 0.45, 6, c1, c1)
    b.prism(0, 0, s * 0.9, s * 1.3, s * 0.4, s * 0.18, 6, c1, c2)
    b.decal(-0.06, s * 0.3, 0.06, s * 0.6, s * 0.46, c2)
  elseif sh == "block" then
    b.block(-s, 0, -s * 0.6, s, s * 0.7, s * 0.6, c1, c2)
  elseif sh == "drum" then
    b.ball(0, s * 0.6, s * 0.2, s * 0.7, s * 0.6, s * 0.8, c1, 6, 3)
    b.box(-0.03, s * 0.5, -s * 1.1, 0.03, s * 0.7, -s * 0.3, c2)
  elseif sh == "steak" then
    b.ball(0, s * 0.2, 0, s * 1.1, s * 0.25, s * 0.8, c1, 7, 2)
    b.decal(-0.06, s * 0.3, 0.06, s * 0.35, s * 0.72, c2)
  elseif sh == "fish" then
    b.ball(0, s * 0.35, 0.02, s * 0.45, s * 0.35, s * 1.0, c1, 6, 3, c2)
    b.piece({ { 0, s * 0.35, -s * 0.8 }, { -s * 0.4, s * 0.55, -s * 1.3 }, { s * 0.4, s * 0.15, -s * 1.3 } },
            { { 1, 2, 3 } }, c1)
  elseif sh == "curl" then
    b.ball(-s * 0.4, s * 0.45, 0, s * 0.5, s * 0.45, s * 0.5, c1, 5, 3)
    b.ball(s * 0.45, s * 0.35, s * 0.2, s * 0.35, s * 0.35, s * 0.35, c2, 5, 2)
  elseif sh == "sprig" then
    b.box(-0.015, 0, -0.015, 0.015, s * 1.2, 0.015, 0x207020)
    b.ball(0, s * 1.2, 0, s * 0.6, s * 0.4, s * 0.6, c1, 5, 2)
    b.ball(0.06, s * 0.7, 0, s * 0.4, s * 0.3, s * 0.4, c2, 5, 2)
  end
end

-- chopped: a few pieces of the inside colour
local function chopped_ing(b, g, k)
  local c1, c2 = darker(g.c1, k or 1), darker(g.c2, k or 1)
  local look = Data.CHOP_LOOK[g.id] or "cubes"
  if look == "slices" or look == "rings" then
    for i = -1, 1 do
      b.prism(i * 0.09, 0, 0, 0.04, 0.08, 0.08, 5, c1, c2)
    end
  elseif look == "leaves" then
    b.piece({ { -0.14, 0.02, -0.08 }, { 0.14, 0.02, -0.1 }, { 0.12, 0.05, 0.1 }, { -0.12, 0.08, 0.12 }, { 0, 0.12, 0 } },
            { { 1, 2, 5 }, { 2, 3, 5 }, { 3, 4, 5 }, { 4, 1, 5 }, { 1, 2, 3 }, { 1, 3, 4 } }, c1)
    b.ball(0.05, 0.1, 0.02, 0.07, 0.04, 0.07, c2, 5, 2)
  elseif look == "patty" then
    b.prism(0, 0, 0, 0.07, 0.15, 0.14, 7, c1, c2)
  elseif look == "fillet" then
    b.block(-0.15, 0, -0.06, 0.15, 0.06, 0.06, c2, c2)
    b.decal(-0.15, 0.0, 0.15, 0.06, 0.061, c1)
  elseif look == "dough" then
    b.ball(0, 0.06, 0, 0.17, 0.07, 0.17, c1, 7, 2)
  else
    b.block(-0.13, 0, -0.04, -0.04, 0.08, 0.05, c2, c1)
    b.block(0.02, 0, -0.08, 0.11, 0.08, 0.01, c2, c1)
    b.block(-0.04, 0, 0.04, 0.05, 0.08, 0.13, c2, c1)
  end
end

-- the colour of a mix (a pot of soup, a smoothie, a pizza)
local function mix_color(key)
  local k = Food.parse(key)
  if k.ing then
    local g = Data.ING[k.ing]
    return k.chopped and g.c1 or g.c1
  end
  local r, gg, bb, n = 0, 0, 0, 0
  for _, s in ipairs(k.kids) do
    local c = mix_color(s)
    r, gg, bb, n = r + (c >> 16 & 255), gg + (c >> 8 & 255), bb + (c & 255), n + 1
  end
  local c = floor(r / n) << 16 | floor(gg / n) << 8 | floor(bb / n)
  if k.proc == "fry" then c = mix_rgb(c, 0x9A5A20, 0.35)
  elseif k.proc == "bake" then c = mix_rgb(c, 0xD89040, 0.3)
  elseif k.proc == "blend" then c = mix_rgb(c, 0xFFFFFF, 0.3)
  elseif k.proc == "boil" then c = mix_rgb(c, 0xF0E0C0, 0.12) end
  return c
end
Mesh.mix_color = mix_color

local cache = {}

-- the model of an item key (built the first time it is needed)
local function item_mesh(key)
  local m = cache[key]
  if m ~= nil then return m end
  local b = builder()
  if key == "burnt" then
    b.ball(0, 0.08, 0, 0.18, 0.1, 0.16, 0x201A18, 6, 2)
    b.ball(0.06, 0.14, 0.03, 0.08, 0.06, 0.08, 0x383030, 5, 2)
  elseif key == "ext" then
    b.prism(0, 0, 0, 0.36, 0.08, 0.08, 6, 0xE02020, 0xB01818)
    b.box(-0.02, 0.36, -0.1, 0.02, 0.42, 0.04, 0x303030)
  else
    local k = Food.parse(key)
    if k.ing then
      local g = Data.ING[k.ing]
      if k.chopped then chopped_ing(b, g) else raw_ing(b, g) end
    elseif k.proc == "fry" and #k.kids == 1 and Food.parse(k.kids[1]).ing then
      -- a fried piece: the same shape, golden brown
      local kk = Food.parse(k.kids[1])
      local g = Data.ING[kk.ing]
      local fg = { id = g.id, c1 = mix_rgb(g.c1, 0xB06020, 0.55), c2 = mix_rgb(g.c2, 0xC88040, 0.5),
                   size = g.size, shape = g.shape }
      if kk.chopped then chopped_ing(b, fg) else raw_ing(b, fg) end
    else
      local c = mix_color(key)
      if k.proc == "boil" then            -- a bowl of it
        b.prism(0, 0, 0, 0.13, 0.1, 0.17, 7, 0xF0F0F8, false, 0xD0D0D8)
        b.prism(0, 0, 0.12, 0.125, 0.16, 0.16, 7, c, c)
      elseif k.proc == "blend" then       -- a glass
        b.prism(0, 0, 0, 0.26, 0.08, 0.1, 6, c, 0xFFFFFF)
        b.box(0.02, 0.2, -0.01, 0.04, 0.34, 0.01, 0xF04080)
      elseif k.proc == "bake" then        -- a tray bake / pie
        b.prism(0, 0, 0, 0.08, 0.2, 0.21, 8, 0xC88840, c)
        b.ball(0.05, 0.08, 0.04, 0.05, 0.03, 0.05, mix_rgb(c, 0xFFFFFF, 0.3), 5, 2)
      else                                -- fried mix (pancakes, omelette)
        b.prism(0, 0, 0, 0.05, 0.17, 0.17, 7, mix_rgb(c, 0xE0A040, 0.5), c)
        b.prism(0, 0, 0.05, 0.1, 0.15, 0.15, 7, mix_rgb(c, 0xE0A040, 0.5), c)
      end
    end
  end
  m = b.build() or false
  cache[key] = m
  return m
end
Mesh.item = item_mesh

---------------------------------------------------------------- everything else

function Mesh.init()
  Mesh.chef_classic = { make_basil(), make_bun(), make_noodle(), make_pepper() }
  Mesh.chef_model = { make_model(1), make_model(2), make_model(3), make_model(4) }
  Mesh.use_chefs(Save.data and Save.data.classic_chefs)
  local b = builder()
  b.prism(0, 0, 0, 0.035, 0.24, 0.22, 8, 0xF4F4F8, 0xFFFFFF, nil)
  Mesh.plate = b.build()
  b = builder()
  b.prism(0, 0, 0, 0.035, 0.24, 0.22, 8, 0xB0A890, 0x9A8C70)
  b.ball(0.05, 0.04, 0.03, 0.08, 0.02, 0.06, 0x7A6A40, 5, 2)
  Mesh.dplate = b.build()
  -- a blob shadow and the player rings on the floor
  b = builder()
  b.disc(0, 0.004, 0, 0.3, 8, 0x000000)
  Mesh.shadow = b.build()
  Mesh.ring = {}
  for p = 1, 4 do
    b = builder()
    local n, r0, r1 = 10, 0.34, 0.42
    for i = 0, n - 1 do
      local a0, a1 = i / n * TAU, (i + 1) / n * TAU
      b.quad({ sin(a0) * r0, 0.01, cos(a0) * r0 }, { sin(a1) * r0, 0.01, cos(a1) * r0 },
             { sin(a1) * r1, 0.01, cos(a1) * r1 }, { sin(a0) * r1, 0.01, cos(a0) * r1 }, PCOL[p], 0, 1, 0)
    end
    Mesh.ring[p] = b.build()
  end
  -- the highlight around the station a chef faces (one per player colour)
  Mesh.hilite = {}
  for p = 1, 4 do
    b = builder()
    local r, t = 0.52, 0.06
    local y = 0.005
    b.quad({ -r, y, -r }, { r, y, -r }, { r, y, -r + t }, { -r, y, -r + t }, PCOL[p], 0, 1, 0)
    b.quad({ -r, y, r - t }, { r, y, r - t }, { r, y, r }, { -r, y, r }, PCOL[p], 0, 1, 0)
    b.quad({ -r, y, -r }, { -r + t, y, -r }, { -r + t, y, r }, { -r, y, r }, PCOL[p], 0, 1, 0)
    b.quad({ r - t, y, -r }, { r, y, -r }, { r, y, r }, { r - t, y, r }, PCOL[p], 0, 1, 0)
    Mesh.hilite[p] = b.build()
  end
  -- pot and pan contents, fire, steam puffs
  Mesh.soup = {}
  b = builder()
  b.prism(0, 0, 0, 0.3, 0.07, 0.03, 4, 0xFF8020, 0xFFD040)
  b.prism(0.1, 0.05, 0, 0.22, 0.05, 0.02, 4, 0xFF4010, 0xFFA020)
  b.prism(-0.1, -0.04, 0, 0.25, 0.05, 0.02, 4, 0xFFB020, 0xFFE060)
  Mesh.flame = b.build()
  b = builder()
  b.box(-0.08, -0.08, -0.08, 0.08, 0.08, 0.08, 0xFFFFFF)
  Mesh.cube = b.build()
end

-- a disc of soup of a given colour for the top of a pot
function Mesh.soup_disc(color)
  local m = Mesh.soup[color]
  if m then return m end
  local b = builder()
  b.prism(0, 0, 0, 0.02, 0.2, 0.2, 7, color, mix_rgb(color, 0xFFFFFF, 0.15))
  m = b.build()
  Mesh.soup[color] = m
  return m
end
