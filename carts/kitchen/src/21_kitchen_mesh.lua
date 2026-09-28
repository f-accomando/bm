-- The kitchen as a few big meshes, built when a stage starts: water under
-- the gaps, the floor (drawn without z-buffer: nothing is below it), and
-- every station and wall in one or more static meshes. Platforms get their
-- own floor and station meshes, drawn where the platform is.

local TOP = Kit.TOP
local MAXV = 3600                   -- vertices per mesh (the limit is 4096)

-- builders that start a new mesh when one gets full
local function multi(textured)
  local list, b = {}, nil
  local m = {}
  function m.get()
    if not b or #b.v // 3 > MAXV then
      b = Mesh.builder()
      if textured then b.uv = {} end
      list[#list + 1] = b
    end
    return b
  end
  function m.build()
    local out = {}
    for _, bb in ipairs(list) do
      local mm = bb.build()
      if mm then out[#out + 1] = mm end
    end
    return out
  end
  return m
end

-- sheet cell of a 16x16 icon -> texture coordinates of a quad's corners
local function icon_uv(icon)
  local u0, v0 = (icon % 32) * 16 + 0.5, (icon // 32) * 16 + 0.5
  local u1, v1 = u0 + 15, v0 + 15
  return { { u0, v1 }, { u1, v1 }, { u1, v0 }, { u0, v0 } }
end
Mesh.icon_uv = icon_uv

-- The camera always looks along +z from above: the +z face and the bottom
-- of a block never show, nor a side against another block. `cover(dx, dz)`
-- says if the neighbouring cell hides that side.
local function block(b, x0, y0, z0, x1, y1, z1, col, top, cover)
  local faces = "t"
  if not cover or not cover(0, -1) then faces = faces .. "s" end
  if not cover or not cover(-1, 0) then faces = faces .. "w" end
  if not cover or not cover(1, 0) then faces = faces .. "e" end
  b.boxf(x0, y0, z0, x1, y1, z1, col, faces, top)
end

-- one station at cell origin (x, z) (the platform's own origin for riders)
local function station_model(mb, st, x, z, world, cover)
  local b = mb.get()
  local def = st.def
  local k = st.kind
  local x0, z0, x1, z1 = x + 0.03, z + 0.03, x + 0.97, z + 0.97
  local cx, cz = x + 0.5, z + 0.5
  if k == "oven" then
    block(b, x0, 0, z0, x1, TOP + 0.12, z1, def.col, def.top)
    b.quad({ x + 0.18, 0.14, z0 - 0.005 }, { x + 0.82, 0.14, z0 - 0.005 }, { x + 0.82, 0.62, z0 - 0.005 },
           { x + 0.18, 0.62, z0 - 0.005 }, 0x2A2420, 0, 0, -1)
    b.quad({ x + 0.3, 0.3, z0 - 0.01 }, { x + 0.7, 0.3, z0 - 0.01 }, { x + 0.7, 0.52, z0 - 0.01 },
           { x + 0.3, 0.52, z0 - 0.01 }, 0xF08030, 0, 0, -1)
    b.block(x + 0.62, TOP + 0.12, z + 0.62, x + 0.82, TOP + 0.5, z + 0.82, 0x6A3A2A, 0x3A2A20)
    return
  end
  if k == "trash" then
    b.prism(cx, cz, 0, TOP - 0.1, 0.36, 0.4, 8, def.col, def.top)
    b.prism(cx, cz, TOP - 0.1, TOP - 0.02, 0.42, 0.42, 8, 0x6A786A, 0x6A786A)
    return
  end
  -- everything else stands on a counter-like block
  block(b, x0, 0, z0, x1, TOP, z1, def.col, def.top, cover)
  if k == "crate" then
    for _, yy in ipairs({ 0.1, 0.4 }) do
      b.quad({ x0 + 0.05, yy, z0 - 0.004 }, { x1 - 0.05, yy, z0 - 0.004 }, { x1 - 0.05, yy + 0.06, z0 - 0.004 },
             { x0 + 0.05, yy + 0.06, z0 - 0.004 }, shade(def.col, 0.7), 0, 0, -1)
    end
    local g = Data.ING[st.ing]
    local y = TOP + 0.004
    local tb = mb.get()
    if tb.uv then
      tb.quad({ x + 0.18, y, z + 0.18 }, { x + 0.82, y, z + 0.18 }, { x + 0.82, y, z + 0.82 },
              { x + 0.18, y, z + 0.82 }, -1, 0, 1, 0, icon_uv(g.icon))
    else
      tb.quad({ x + 0.2, y, z + 0.2 }, { x + 0.8, y, z + 0.2 }, { x + 0.8, y, z + 0.8 },
              { x + 0.2, y, z + 0.8 }, g.c1, 0, 1, 0)
    end
  elseif k == "board" then
    b.block(x + 0.14, TOP, z + 0.2, x + 0.86, TOP + 0.04, z + 0.8, 0xC89060, 0xDCA878)
    b.block(x + 0.66, TOP + 0.04, z + 0.72, x + 0.9, TOP + 0.06, z + 0.78, 0xB8C0C8, 0xD8E0E8)
  elseif k == "pot" then
    b.disc(cx, TOP + 0.005, cz, 0.36, 8, 0x1A1A1E)
    b.prism(cx, cz, TOP, TOP + 0.3, 0.27, 0.29, 8, 0xA0A8B0, false)
    b.disc(cx, TOP + 0.16, cz, 0.26, 8, 0x3A3E44)
    b.block(cx - 0.4, TOP + 0.2, cz - 0.04, cx - 0.28, TOP + 0.25, cz + 0.04, 0x505860)
    b.block(cx + 0.28, TOP + 0.2, cz - 0.04, cx + 0.4, TOP + 0.25, cz + 0.04, 0x505860)
  elseif k == "pan" then
    b.disc(cx, TOP + 0.005, cz, 0.36, 8, 0x1A1A1E)
    b.prism(cx, cz, TOP, TOP + 0.07, 0.28, 0.31, 8, 0x3A3A40, 0x26262A)
    b.block(cx + 0.28, TOP + 0.04, cz + 0.12, cx + 0.62, TOP + 0.08, cz + 0.2, 0x5A3A2A)
  elseif k == "blender" then
    b.block(cx - 0.18, TOP, cz - 0.18, cx + 0.18, TOP + 0.14, cz + 0.18, 0xD04050, 0xE05060)
    b.prism(cx, cz, TOP + 0.14, TOP + 0.5, 0.14, 0.19, 6, 0xB8E0F0, 0x90C0D8)
  elseif k == "sink" then
    b.block(x + 0.12, TOP - 0.02, z + 0.12, x + 0.88, TOP + 0.005, z + 0.88, 0x5A7A98, 0x4A7AB8)
    b.block(cx - 0.05, TOP, z + 0.1, cx + 0.05, TOP + 0.3, z + 0.18, 0xC8D0D8, 0xE0E8F0)
  elseif k == "ret" then
    b.quad({ x + 0.15, TOP * 0.3, z0 - 0.004 }, { x + 0.85, TOP * 0.3, z0 - 0.004 },
           { x + 0.85, TOP * 0.7, z0 - 0.004 }, { x + 0.15, TOP * 0.7, z0 - 0.004 }, 0x5A4E40, 0, 0, -1)
  elseif k == "serve" then
    b.block(x + 0.1, TOP, z + 0.1, x + 0.9, TOP + 0.03, z + 0.9, 0xF0F0F0, 0xFFFFFF)
    b.prism(x + 0.78, z + 0.22, TOP + 0.03, TOP + 0.13, 0.08, 0.02, 6, 0xF0C020, 0xFFE060)
    b.block(x0, TOP + 0.7, z0, x1, TOP + 0.8, z1, 0xE04040, 0xF06060)    -- awning
    b.block(x0, TOP, z0, x0 + 0.06, TOP + 0.7, z0 + 0.06, 0x3A7AC0)
    b.block(x1 - 0.06, TOP, z0, x1, TOP + 0.7, z0 + 0.06, 0x3A7AC0)
  elseif k == "register" then
    b.block(x + 0.2, TOP, z + 0.25, x + 0.8, TOP + 0.3, z + 0.8, 0xD8A030, 0xF0C040)
    b.quad({ x + 0.28, TOP + 0.14, z + 0.245 }, { x + 0.72, TOP + 0.14, z + 0.245 },
           { x + 0.72, TOP + 0.26, z + 0.245 }, { x + 0.28, TOP + 0.26, z + 0.245 }, 0x30C060, 0, 0, -1)
    b.block(x + 0.3, TOP + 0.3, z + 0.45, x + 0.7, TOP + 0.36, z + 0.75, 0xB08020, 0xE0B040)
  elseif k == "belt" then
    local d = st.dir
    for i = 0, 2 do
      local t = 0.2 + i * 0.28
      if d[1] ~= 0 then
        b.quad({ x + t, TOP + 0.003, z + 0.12 }, { x + t + 0.1, TOP + 0.003, z + 0.12 },
               { x + t + 0.1, TOP + 0.003, z + 0.88 }, { x + t, TOP + 0.003, z + 0.88 }, 0x60686E, 0, 1, 0)
      else
        b.quad({ x + 0.12, TOP + 0.003, z + t }, { x + 0.88, TOP + 0.003, z + t },
               { x + 0.88, TOP + 0.003, z + t + 0.1 }, { x + 0.12, TOP + 0.003, z + t + 0.1 }, 0x60686E, 0, 1, 0)
      end
    end
  elseif k == "valve" then
    b.prism(cx, cz, TOP, TOP + 0.4, 0.07, 0.07, 6, 0x8898B8, 0x8898B8)
    b.prism(cx, cz, TOP + 0.4, TOP + 0.45, 0.2, 0.2, 6, 0xD03030, 0xE04040)
  end
end

local function wall_model(mb, cx, cz, world, run)
  local b = mb.get()
  -- the back row is taller; side walls are lower, so they never hide the floor
  local hgt = cz >= run.h - 1 and 1.5 or 1.05
  local function wall_at(dx, dz)
    local c = Kit.cell(run, cx + dx, cz + dz)
    return c.kind == "wall" and (cz + dz >= run.h - 1) == (cz >= run.h - 1)
  end
  block(b, cx, 0, cz, cx + 1, hgt, cz + 1, world.wall, world.wall2, wall_at)
end

local function floor_quad(b, x, z, color)
  b.quad({ x, 0, z }, { x + 1, 0, z }, { x + 1, 0, z + 1 }, { x, 0, z + 1 }, color, 0, 1, 0)
end

local function floor_color(world, kind, cx, cz)
  local odd = (cx + cz) % 2 == 1
  if kind == "ice" then return odd and 0xD8F0FF or 0xB8E0F8 end
  if kind == "vent" then return 0x5A5E66 end
  if kind == "door" then return 0x8A6A4A end
  return odd and world.floor2 or world.floor1
end

-- The meshes of a run: .water, .floor, .static (lists), and per platform
-- .pfloor[i], .pstatic[i] (drawn at the platform's offset).
function Mesh.kitchen(run)
  local world = run.world
  local out = { pfloor = {}, pstatic = {} }
  local w, h = run.w, run.h

  -- the base of the diorama and the water
  local b = Mesh.builder()
  local has_void = #run.plats > 0
  for _, c in ipairs(run.cells) do if c.kind == "void" then has_void = true end end
  if has_void then
    local wc = world.water or 0x2E7AC0
    b.quad({ -0.5, -0.25, -0.5 }, { w + 0.5, -0.25, -0.5 }, { w + 0.5, -0.25, h }, { -0.5, -0.25, h }, wc, 0, 1, 0)
  end
  b.quad({ 0, 0, 0 }, { w, 0, 0 }, { w, -0.55, 0 }, { 0, -0.55, 0 }, world.base, 0, 0, -1)
  b.quad({ 0, 0, 0 }, { 0, 0, h }, { 0, -0.55, h }, { 0, -0.55, 0 }, shade(world.base, 0.8), -1, 0, 0)
  b.quad({ w, 0, 0 }, { w, 0, h }, { w, -0.55, h }, { w, -0.55, 0 }, shade(world.base, 0.8), 1, 0, 0)
  out.water = b.build()

  -- floor
  local fb = multi(false)
  for cz = 0, h - 1 do
    for cx = 0, w - 1 do
      local c = Kit.cell(run, cx, cz)
      if c.kind ~= "void" and c.kind ~= "wall" and not c.st then
        floor_quad(fb.get(), cx, cz, floor_color(world, c.kind, cx, cz))
      end
    end
  end
  out.floor = fb.build()

  -- stations and walls
  local sb = multi(true)
  for cz = 0, h - 1 do
    for cx = 0, w - 1 do
      local c = Kit.cell(run, cx, cz)
      if c.kind == "wall" then wall_model(sb, cx, cz, world, run)
      elseif c.st and not c.st.plat then
        station_model(sb, c.st, cx, cz, world, function(dx, dz)
          local n = Kit.cell(run, cx + dx, cz + dz)
          return n.kind == "wall" or (n.st ~= nil and not n.st.plat and n.st.kind ~= "trash")
        end)
      end
    end
  end
  out.static = sb.build()

  -- platforms: floor and stations at their own origin (0, 0)
  for i, p in ipairs(run.plats) do
    local pf, ps = multi(false), multi(true)
    for lz = 0, p.h - 1 do
      for lx = 0, p.w - 1 do
        local c = p.cells[lz * p.w + lx + 1]
        if c.kind ~= "void" then
          local bb = pf.get()
          floor_quad(bb, lx, lz, floor_color(world, c.kind, lx + p.x0, lz + p.z0))
          if c.st then station_model(ps, c.st, lx, lz, world) end
        end
      end
    end
    -- the deck's edge, so it reads as a raft
    local bb = pf.get()
    bb.quad({ 0, 0, 0 }, { p.w, 0, 0 }, { p.w, -0.3, 0 }, { 0, -0.3, 0 }, 0x6A4A2A, 0, 0, -1)
    out.pfloor[i] = pf.build()
    out.pstatic[i] = ps.build()
  end

  -- the chefs' shadows, a darker shade of the floor
  b = Mesh.builder()
  b.disc(0, 0.004, 0, 0.3, 8, shade(world.floor1, 0.62))
  out.shadow = b.build()
  return out
end
