-- bm3d: what bm Studio and bm Animator share on the console (require "bm3d").
--
-- The MESH (8) and ANIM (9) sections of a .bm (src/bm/bm.h), read and
-- written with string.pack and handed to the kernel with cart_data(); the
-- project (the models as tables, undo, open, save with cart_write); the
-- frame of the two programs (tabs, status bar, menu, dialogs, the keys) and
-- the 3D helpers. The MESH and ANIM sections of src/bm/bm.h.
--
-- A program gives its pages to bm3d.run{...}: each page is a table with
-- id, fkey ("f1"...), label, and the functions enter, key(k), pad(),
-- update(dt), draw(), refresh(), status() and its help lines (keys).

local T = {}
-- the mouse (require "bmui"): what the keys do, by clicking
local U = require "bmui"
T.U = U
-- the assistant's panel (F6, Y + X): the base of a model from a 3D recipe
local ok_assist, assist = pcall(require, "assist")
if not ok_assist then assist = { update = function() return false end, draw = function() end } end
local W, H = SCREEN_W, SCREEN_H
T.W, T.H = W, H

-- the colours of the editors of the console (bm Mesh, bm Pixel...): in the
-- 3D views the wires, the chosen things (PT) and the one under the pointer (HOT)
local C = {
  BG = 0x14161E, PANEL = 0x1C2030, BAR = 0x2A3048, TEXT = 0xE0E4F0, DIM = 0x707890, ACC = 0xFFC050,
  ERR = 0xFF6060, SEL = 0x3050A0, SKY = 0x262C3E, GRID = 0x3A4258, GRID0 = 0x56607C, CUR = 0xFFE070,
  WIRE = 0x7A88B0, PT = 0xFFE070, HOT = 0x60E0FF, OK = 0x90E090,
}
T.C = C
T.FOCAL = (W / 2) / math.tan(math.rad(30))      -- camera3d with fov 60
T.FPS = 12                                       -- the timeline's frames, as in bm Animator
T.MODES = { [0] = "linear", "smooth", "step", "ease-in", "ease-out" }
-- the clip mode's curve (as pose_at in runtime.c): u from 0 to 1 between two keyframes -> the share of the way
function T.ease(mode, u)
  if mode == 2 then return 0 end
  if mode == 1 then return u * u * (3 - 2 * u) end
  if mode == 3 then return u * u end
  if mode == 4 then return u * (2 - u) end
  return u
end
T.LIMIT_V, T.LIMIT_T = 65535, 65535              -- u16 format limit
T.TRIS_60FPS = 1200                              -- about what the console draws at 60 fps
-- text on rows of 16 pixels and columns of 8: the hint row, the status bar
T.HINT_Y, T.STATUS_Y = H - 40, H - 24
local HINT_Y, STATUS_Y = T.HINT_Y, T.STATUS_Y
local SEC_MESH, SEC_ANIM = 8, 9
local TEXTURED = 0x80000000

local floor, abs, sqrt, sin, cos = math.floor, math.abs, math.sqrt, math.sin, math.cos
local max, min, pi = math.max, math.min, math.pi
local spack, sunpack = string.pack, string.unpack

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end
local function round(v) return floor(v + 0.5) end
T.clamp, T.round = clamp, round

T.PALETTE = {
  0x000000, 0x1C2030, 0x404450, 0x808490, 0xC0C4D0, 0xFFFFFF, 0x5A2A22, 0xA84632, 0xE84A5A, 0xE890B0,
  0xF09030, 0xF0D040, 0xFFF4C0, 0x8A5A30, 0xC49A5E, 0xE0C888, 0x285A28, 0x3E8A3A, 0x5CB048, 0xA8E070,
  0x1E4E6E, 0x2E6EB8, 0x3478C4, 0x7ABCE8, 0x2E8A70, 0x60D0C0, 0x3A2A6A, 0x6A50C8, 0xB060D8, 0xFFC050,
}

----------------------------------------------------------------- vectors

local V = {}
T.V = V
function V.sub(a, b) return { a[1] - b[1], a[2] - b[2], a[3] - b[3] } end
function V.add(a, b) return { a[1] + b[1], a[2] + b[2], a[3] + b[3] } end
function V.scale(a, s) return { a[1] * s, a[2] * s, a[3] * s } end
function V.dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
function V.cross(a, b) return { a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1] } end
function V.norm(a)
  local l = sqrt(V.dot(a, a))
  if l < 1e-12 then return { 0, 0, 0 } end
  return { a[1] / l, a[2] / l, a[3] / l }
end
function V.copy(a) return { a[1], a[2], a[3] } end
function V.unit(k, s) local a = { 0, 0, 0 }; a[k] = s or 1; return a end
function V.dist2(a, b) local d = V.sub(a, b); return V.dot(d, d) end

function T.face_normal(f)
  local p = f.p
  local n = V.cross(V.sub(p[2], p[1]), V.sub(p[3], p[1]))
  if #p == 4 then n = V.add(n, V.cross(V.sub(p[3], p[1]), V.sub(p[4], p[1]))) end
  return V.norm(n)
end

function T.face_center(f)
  local c, n = { 0, 0, 0 }, #f.p
  for _, p in ipairs(f.p) do c[1] = c[1] + p[1]; c[2] = c[2] + p[2]; c[3] = c[3] + p[3] end
  return { c[1] / n, c[2] / n, c[3] / n }
end

function T.pkey(p)
  return round(p[1] * 1e4) .. "," .. round(p[2] * 1e4) .. "," .. round(p[3] * 1e4)
end
local pkey = T.pkey

----------------------------------------------------------------- quaternions {x, y, z, w}

local Q = {}
T.Q = Q
function Q.mul(a, b)
  return { a[4] * b[1] + a[1] * b[4] + a[2] * b[3] - a[3] * b[2],
           a[4] * b[2] - a[1] * b[3] + a[2] * b[4] + a[3] * b[1],
           a[4] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[4],
           a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3] }
end
function Q.norm(q)
  local l = sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3] + q[4] * q[4])
  if l < 1e-12 then return { 0, 0, 0, 1 } end
  return { q[1] / l, q[2] / l, q[3] / l, q[4] / l }
end
function Q.axis(axis, a)
  local s = sin(a / 2)
  return { axis[1] * s, axis[2] * s, axis[3] * s, cos(a / 2) }
end
-- the shorter way from a to b: the same as the kernel (runtime.c)
function Q.slerp(a, b, u)
  local d = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]
  local s = 1
  if d < 0 then d, s = -d, -1 end
  local k0, k1
  if d > 0.9995 then k0, k1 = 1 - u, u
  else
    local th = math.acos(d)
    local sn = sin(th)
    k0, k1 = sin((1 - u) * th) / sn, sin(u * th) / sn
  end
  return Q.norm({ a[1] * k0 + s * b[1] * k1, a[2] * k0 + s * b[2] * k1,
                  a[3] * k0 + s * b[3] * k1, a[4] * k0 + s * b[4] * k1 })
end
-- the shorter way from the unit vector a to the unit vector b, as a rotation
function Q.arc(a, b)
  local d = V.dot(a, b)
  if d > 1 - 1e-12 then return { 0, 0, 0, 1 } end
  if d < -1 + 1e-9 then
    local least = abs(a[1]) <= abs(a[2]) and (abs(a[1]) <= abs(a[3]) and 1 or 3) or (abs(a[2]) <= abs(a[3]) and 2 or 3)
    local ax = V.norm(V.cross(a, V.unit(least)))
    return { ax[1], ax[2], ax[3], 0 }
  end
  local c = V.cross(a, b)
  return Q.norm({ c[1], c[2], c[3], 1 + d })
end
function Q.conj(q) return { -q[1], -q[2], -q[3], q[4] } end
-- v turned by q
function Q.rotate(q, v)
  local u = { q[1], q[2], q[3] }
  local t = V.scale(V.cross(u, v), 2)
  return V.add(V.add(v, V.scale(t, q[4])), V.cross(u, t))
end

----------------------------------------------------------------- MESH and ANIM (src/bm/bm.h)

local function name16(s)
  s = s:sub(1, 16)
  return s .. string.rep("\0", 16 - #s)
end

local function cname(b, pos) return b:sub(pos, pos + 15):match("^[^\0]*") end

----------------------------------------------------------------- G: the pure geometry of a model

-- The model shape is { v = { {x, y, z}, ... }, f = { {a, b, c, colour, uv}, ... }, vb }
-- (bm Mesh's item). Nothing here touches an editor state.
local G = {}
T.G = G

-- the model as the program reads it: the MESH part (and the ANIM part)
function G.model_item(name, mc, ac)
  local nv, nf = sunpack("<I2I2", mc, 17)
  local it = { kind = "model", name = name, v = {}, f = {}, mc = mc, mc_name = name }
  local pos = 25
  for i = 1, nv do
    local x, y, z = sunpack("<fff", mc, pos)
    it.v[i] = { x, y, z }
    pos = pos + 12
  end
  for i = 1, nf do
    local a, b, c, _, col, u0, v0, u1, v1, u2, v2 = sunpack("<I2I2I2I2I4I2I2I2I2I2I2", mc, pos)
    local f = { a + 1, b + 1, c + 1, col & 0xFFFFFF }
    if col & TEXTURED ~= 0 then f[4], f[5] = -1, { u0 / 8, v0 / 8, u1 / 8, v1 / 8, u2 / 8, v2 / 8 } end
    it.f[i] = f
    pos = pos + 24
  end
  if ac then
    local nb, nc, anv = sunpack("<I2I2I2", ac, 17)
    it.ac, it.bones, it.clips = ac, nb, nc
    if anv == nv then
      it.vb = {}
      local p = 25 + nb * 44
      for i = 1, nv do it.vb[i] = ac:byte(p + i - 1) end
    end
  end
  return it
end

-- the model's part of the MESH section
function G.encode_model(it)
  if not it.dirty and it.mc then
    return it.mc_name == it.name and it.mc or name16(it.name) .. it.mc:sub(17)
  end
  local out = { name16(it.name), spack("<I2I2I4", #it.v, #it.f, 0) }
  for _, p in ipairs(it.v) do out[#out + 1] = spack("<fff", p[1], p[2], p[3]) end
  for _, f in ipairs(it.f) do
    local col, u, t = f[4], f[5], { 0, 0, 0, 0, 0, 0 }
    if col == -1 then
      col = TEXTURED
      if u then for k = 1, 6 do t[k] = clamp(round(u[k] * 8), 0, 65535) end end
    else
      col = col & 0xFFFFFF
    end
    out[#out + 1] = spack("<I2I2I2I2I4I2I2I2I2I2I2", f[1] - 1, f[2] - 1, f[3] - 1, 0, col,
                          t[1], t[2], t[3], t[4], t[5], t[6])
  end
  return table.concat(out)
end

-- every face looks away from the point c
function G.orient(it, c)
  for _, f in ipairs(it.f) do
    local a, b, d = it.v[f[1]], it.v[f[2]], it.v[f[3]]
    local ux, uy, uz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
    local vx, vy, vz = d[1] - a[1], d[2] - a[2], d[3] - a[3]
    local nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    local mx, my, mz = (a[1] + b[1] + d[1]) / 3 - c[1], (a[2] + b[2] + d[2]) / 3 - c[2], (a[3] + b[3] + d[3]) / 3 - c[3]
    if nx * mx + ny * my + nz * mz < 0 then f[2], f[3] = f[3], f[2] end
  end
end

function G.normal_of(it, f)
  local a, b, c = it.v[f[1]], it.v[f[2]], it.v[f[3]]
  local ux, uy, uz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
  local vx, vy, vz = c[1] - a[1], c[2] - a[2], c[3] - a[3]
  return { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx }
end

function G.unit(v)
  local l = sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
  if l < 1e-9 then return { 0, 1, 0 } end
  return { v[1] / l, v[2] / l, v[3] / l }
end

function G.flip(f)
  f[2], f[3] = f[3], f[2]
  local u = f[5]
  if u then u[3], u[4], u[5], u[6] = u[5], u[6], u[3], u[4] end
end

-- vertices no face uses go away (the bones follow). sv is a selection
-- { [vertex] = order }; the function returns it with the new numbers. A second
-- set (extra: for example the hidden vertices) is renumbered the same way and
-- returned as the second value.
function G.compact(it, sv, extra)
  local used, map, nv, vb = {}, {}, {}, it.vb and {} or nil
  for _, f in ipairs(it.f) do used[f[1]], used[f[2]], used[f[3]] = true, true, true end
  for i, p in ipairs(it.v) do
    if used[i] then
      nv[#nv + 1] = p
      map[i] = #nv
      if vb then vb[#nv] = it.vb[i] end
    end
  end
  for _, f in ipairs(it.f) do f[1], f[2], f[3] = map[f[1]], map[f[2]], map[f[3]] end
  it.v = nv
  if vb then it.vb = vb end
  local out, out2 = {}, {}
  for i, o in pairs(sv or {}) do if map[i] then out[map[i]] = o end end
  for i, o in pairs(extra or {}) do if map[i] then out2[map[i]] = o end end
  return out, out2
end

-- Comfort tools (APP-11). All pure: they know no editor state.
-- G.box_pick(sx, sy, x0, y0, x1, y1, skip): the vertices whose screen place (sx[i], sy[i]) is inside the
-- rectangle (any two opposite corners), in order; a vertex with no place (behind the camera) or in the
-- set skip is left out.
function G.box_pick(sx, sy, x0, y0, x1, y1, skip)
  if x0 > x1 then x0, x1 = x1, x0 end
  if y0 > y1 then y0, y1 = y1, y0 end
  local out = {}
  for i, x in pairs(sx) do
    local y = sy[i]
    if y and x >= x0 and x <= x1 and y >= y0 and y <= y1 and not (skip and skip[i]) then out[#out + 1] = i end
  end
  table.sort(out)
  return out
end

-- The share of a move a vertex at distance d from the chosen ones takes when the radius is r: 1 at the
-- chosen ones, a smooth fall (smoothstep) to 0 at r, 0 beyond.
function G.falloff(d, r)
  if r <= 0 or d >= r then return 0 end
  local w = 1 - d / r
  return w * w * (3 - 2 * w)
end

-- Proportional (soft) move: { [vertex] = weight } for the vertices within r of the nearest vertex of
-- src (a list of vertex numbers), weight from G.falloff. A vertex of src, or in the set skip, is not in the
-- result. A hash grid keeps the cost near the number of vertices.
function G.soft_weights(it, src, r, skip)
  local out = {}
  if r <= 0 or #src == 0 then return out end
  local cell, grid, v = r, {}, it.v
  local function key(cx, cy, cz) return (cx * 73856093) ~ (cy * 19349663) ~ (cz * 83492791) end
  local isrc = {}
  for _, i in ipairs(src) do
    local p = v[i]
    isrc[i] = true
    local k = key(math.floor(p[1] / cell), math.floor(p[2] / cell), math.floor(p[3] / cell))
    local b = grid[k]
    if not b then b = {}; grid[k] = b end
    b[#b + 1] = i
  end
  local r2 = r * r
  for i, p in ipairs(v) do
    if not isrc[i] and not (skip and skip[i]) then
      local cx, cy, cz = math.floor(p[1] / cell), math.floor(p[2] / cell), math.floor(p[3] / cell)
      local best = r2
      for dx = -1, 1 do for dy = -1, 1 do for dz = -1, 1 do
        local b = grid[key(cx + dx, cy + dy, cz + dz)]
        if b then
          for _, j in ipairs(b) do
            local q = v[j]
            local ex, ey, ez = p[1] - q[1], p[2] - q[2], p[3] - q[3]
            local d2 = ex * ex + ey * ey + ez * ez
            if d2 < best then best = d2 end
          end
        end
      end end end
      if best < r2 then
        local w = G.falloff(math.sqrt(best), r)
        if w > 0 then out[i] = w end
      end
    end
  end
  return out
end

-- Smooth (APP-25, Shift+V): one pass that relaxes the vertices of sv (a list) toward the mean of their
-- edge neighbours: p + k * (mean - p). Every new position comes from the old ones (no order effect).
-- A vertex on an open border (an edge with one face), one with no edge, and one not in range stay.
-- fol = { [leader] = counterpart } (live mirror): the counterpart gets the mirror image (x -> -x) of the
-- new position of its leader. Returns the number of vertices that moved.
function G.smooth(it, sv, k, fol)
  local v = it.v
  local nb, border = {}, {}
  for key, l in pairs(G.edge_faces(it)) do
    local a, b = G.edge_ends(key)
    nb[a] = nb[a] or {}; nb[b] = nb[b] or {}
    nb[a][#nb[a] + 1] = b; nb[b][#nb[b] + 1] = a
    if #l == 1 then border[a], border[b] = true, true end
  end
  local new, n = {}, 0
  for _, i in ipairs(sv) do
    local l, p = nb[i], v[i]
    if p and l and not border[i] and not new[i] then
      local x, y, z = 0, 0, 0
      for _, j in ipairs(l) do local q = v[j]; x, y, z = x + q[1], y + q[2], z + q[3] end
      local m = #l
      new[i] = { p[1] + k * (x / m - p[1]), p[2] + k * (y / m - p[2]), p[3] + k * (z / m - p[3]) }
    end
  end
  for i, q in pairs(new) do
    local p = v[i]
    p[1], p[2], p[3] = q[1], q[2], q[3]
    n = n + 1
    local j = fol and fol[i]
    if j and v[j] then v[j][1], v[j][2], v[j][3] = -q[1], q[2], q[3]; n = n + 1 end
  end
  return n
end

-- Hide and isolate: hid is a set { [vertex] = true }. A face is hidden when any corner is.
function G.face_hidden(f, hid)
  return hid[f[1]] or hid[f[2]] or hid[f[3]] or false
end

-- every vertex but the ones in keep goes into the set (isolate)
function G.isolate(it, keep)
  local hid = {}
  for i in ipairs(it.v) do if not keep[i] then hid[i] = true end end
  return hid
end

-- Numeric entry: the text typed into a number, or nil. "-", "" and "." are not numbers; a trailing point is
-- fine ("2." is 2).
function G.parse_number(s)
  if s == nil or s == "" or s == "-" or s == "." or s == "-." then return nil end
  return tonumber(s)
end

-- Reference pictures (front and side): where the picture of a slot is drawn. view = { yaw, pitch }; the
-- slot "front" shows when the camera looks along the Z axis (yaw 0 or pi), "side" along the X axis
-- (yaw +-pi/2), within the tolerance tol (radians, default 0.3) and with the pitch near 0. Returns the
-- slot's flip: false (as stored), true (seen from the back), or nil (not looked at).
function G.ref_flip(slot, yaw, pitch, tol)
  tol = tol or 0.3
  if math.abs(pitch) > tol then return nil end
  local c, s = math.cos(yaw), math.sin(yaw)
  if slot == "front" then
    if c >= math.cos(tol) then return false elseif c <= -math.cos(tol) then return true end
  else
    if s <= -math.cos(tol) then return false elseif s >= math.cos(tol) then return true end
  end
  return nil
end

-- the faces with two equal corners go away; returns how many
function G.drop_degenerate(it)
  local out = {}
  for _, f in ipairs(it.f) do if f[1] ~= f[2] and f[2] ~= f[3] and f[1] ~= f[3] then out[#out + 1] = f end end
  local n = #it.f - #out
  it.f = out
  return n
end

function G.add_vertex(it, p, from)
  it.v[#it.v + 1] = { p[1], p[2], p[3] }
  if it.vb then it.vb[#it.v] = it.vb[from] or 0 end
  return #it.v
end

-- Mirror across the plane X = 0. G.mirror_pairs(it, list, eps) takes a list
-- of vertex numbers and returns { [vertex] = partner }: the vertex at the
-- mirrored position (-x, y, z), within eps (0.001 by default). A vertex on
-- the plane is its own partner. A vertex with no counterpart is not in the
-- table. When two vertices share the place, the nearest one is the partner.
function G.mirror_pairs(it, list, eps)
  eps = eps or 0.001
  local cell, grid, v = eps * 2, {}, it.v
  local function key(cx, cy, cz) return (cx * 1000003 + cy) * 1000033 + cz end
  for i, p in ipairs(v) do
    local k = key(floor(p[1] / cell), floor(p[2] / cell), floor(p[3] / cell))
    local b = grid[k]
    if b then b[#b + 1] = i else grid[k] = { i } end
  end
  local out = {}
  for _, i in ipairs(list) do
    local p = v[i]
    local x, y, z = -p[1], p[2], p[3]
    local cx, cy, cz = floor(x / cell), floor(y / cell), floor(z / cell)
    local best, bd = nil, eps * eps * 3 + 1e-12
    for dx = -1, 1 do for dy = -1, 1 do for dz = -1, 1 do
      local b = grid[key(cx + dx, cy + dy, cz + dz)]
      if b then
        for _, j in ipairs(b) do
          local q = v[j]
          local ex, ey, ez = q[1] - x, q[2] - y, q[3] - z
          if math.abs(ex) <= eps and math.abs(ey) <= eps and math.abs(ez) <= eps then
            local d = ex * ex + ey * ey + ez * ez
            if d < bd then best, bd = j, d end
          end
        end
      end
    end end end
    out[i] = best
  end
  return out
end

-- The faces that mirror the faces fs (a list of face numbers): the face whose
-- three corners are the partners (table mp of G.mirror_pairs, which must hold
-- every corner of fs) of the three corners. Returns { [face] = partner face };
-- a face with no counterpart is not in the table.
function G.mirror_faces(it, fs, mp)
  local byk, out = {}, {}
  local function key(a, b, c)
    if a > b then a, b = b, a end
    if b > c then b, c = c, b end
    if a > b then a, b = b, a end
    return (a * 70001 + b) * 70001 + c
  end
  for fi, f in ipairs(it.f) do
    local k = key(f[1], f[2], f[3])
    local b = byk[k]
    if b then b[#b + 1] = fi else byk[k] = { fi } end
  end
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    local a, b, c = mp[f[1]], mp[f[2]], mp[f[3]]
    if a and b and c then
      local l = byk[key(a, b, c)]
      if l then out[fi] = l[1] end
    end
  end
  return out
end

-- Edges are not stored: they come from the triangles. An edge is a pair of
-- vertices a < b; its key is a * 65536 + b.
function G.edge_key(a, b)
  if a > b then a, b = b, a end
  return a * 65536 + b
end

function G.edge_ends(k) return k // 65536, k % 65536 end

-- { [edge key] = { the faces that have the edge } }
function G.edge_faces(it)
  local out = {}
  for fi, f in ipairs(it.f) do
    for k = 1, 3 do
      local key = G.edge_key(f[k], f[k % 3 + 1])
      local l = out[key]
      if l then l[#l + 1] = fi else out[key] = { fi } end
    end
  end
  return out
end

-- The quad that the face ti makes across the edge key: ti and the face
-- over one of its other two edges (the diagonal; it must be shared with one
-- face only, and the two faces must look the same way: normals within 60
-- degrees; when both edges fit, the flatter pair, then the longer edge, is the
-- diagonal). Returns { c = {q1, q2, q3, q4} the corners in the winding of
-- ti with the edge (q1, q2) and the opposite edge (q3, q4), t = ti, u = the
-- other face, e2 = the key of the opposite edge } or nil.
local function loop_quad(it, ef, ti, key)
  local f = it.f[ti]
  local p, q, c
  for k = 1, 3 do
    if G.edge_key(f[k], f[k % 3 + 1]) == key then p, q, c = f[k], f[k % 3 + 1], f[(k + 1) % 3 + 1] end
  end
  if not p then return nil end
  local n1 = G.unit(G.normal_of(it, f))
  local best
  for side = 1, 2 do
    local x, y = c, p
    if side == 1 then x, y = q, c end
    local l = ef[G.edge_key(x, y)]
    if l and #l == 2 then
      local u = l[1] == ti and l[2] or l[1]
      local g = it.f[u]
      local d
      for k = 1, 3 do if g[k] ~= x and g[k] ~= y then d = g[k] end end
      if d and d ~= p and d ~= q and d ~= c then
        local n2 = G.unit(G.normal_of(it, g))
        local dot = n1[1] * n2[1] + n1[2] * n2[2] + n1[3] * n2[3]
        local a, b = it.v[x], it.v[y]
        local len = (a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2
        if dot >= 0.5 and (not best or dot > best.dot + 1e-4 or (dot > best.dot - 1e-4 and len > best.len)) then
          best = { dot = dot, len = len, u = u,
                   c = side == 1 and { p, q, d, c } or { p, q, c, d } }
        end
      end
    end
  end
  if not best then return nil end
  return { c = best.c, t = ti, u = best.u, e2 = G.edge_key(best.c[3], best.c[4]) }
end

-- Loop cut, the plan. The ring of quads (two triangles) that cross the edge
-- (a, b): from the edge, over the quad of each face it has, to the opposite
-- edge, and on, until the ring is closed. The walk stops at an open border
-- (an edge with one face) and at a face that makes no quad: that face is cut
-- in two from the new vertex to its far corner, so no gap stays. Nothing
-- changes. Returns the plan { edges = {keys}, quads = {...}, plains = { {face,
-- key} }, closed } or nil and a message.
function G.loop_plan(it, a, b)
  local ef = G.edge_faces(it)
  local k0 = G.edge_key(a, b)
  local l0 = ef[k0]
  if not l0 then return nil, "this is not an edge" end
  if #l0 > 2 then return nil, "the edge has more than 2 faces" end
  local plan = { edges = { k0 }, quads = {}, plains = {}, closed = false }
  local seen, used, failed = { [k0] = true }, {}, nil
  local function walk(ti)
    local key = k0
    while true do
      if used[ti] then failed = "the ring crosses itself"; return end
      local qd = loop_quad(it, ef, ti, key)
      if not qd then
        used[ti] = true
        plan.plains[#plan.plains + 1] = { ti, key }
        return
      end
      if used[qd.u] then failed = "the ring crosses itself"; return end
      used[ti], used[qd.u] = true, true
      plan.quads[#plan.quads + 1] = qd
      if qd.e2 == k0 then plan.closed = true; return end
      if seen[qd.e2] then failed = "the ring crosses itself"; return end
      seen[qd.e2] = true
      plan.edges[#plan.edges + 1] = qd.e2
      local l = ef[qd.e2]
      if #l > 2 then failed = "an edge of the ring has more than 2 faces"; return end
      if #l == 1 then return end
      key, ti = qd.e2, l[1] == qd.u and l[2] or l[1]
    end
  end
  walk(l0[1])
  if not failed and l0[2] and not used[l0[2]] then walk(l0[2]) end
  if failed then return nil, failed end
  if #plan.quads == 0 then return nil, "no quad around this edge (two triangles that make a square)" end
  return plan
end

-- Loop cut, the change: one new vertex in the middle of each edge of the
-- plan (the bone of the lower end; the uv by the mean), the quads in 4
-- triangles, the other faces in 2. A new triangle takes the colour and the uv
-- of the old face that holds most of its corners. Returns { mids = the new
-- vertices, rungs = { {m, n} one for each quad: the new edge across it } }.
function G.loop_apply(it, plan)
  local mid, ends = {}, {}
  for _, k in ipairs(plan.edges) do
    local a, b = G.edge_ends(k)
    local p, q = it.v[a], it.v[b]
    mid[k] = G.add_vertex(it, { (p[1] + q[1]) / 2, (p[2] + q[2]) / 2, (p[3] + q[3]) / 2 }, a)
    ends[mid[k]] = { a, b }
  end
  local function has(f, v)
    if ends[v] then return has(f, ends[v][1]) and has(f, ends[v][2]) end
    return f[1] == v or f[2] == v or f[3] == v
  end
  local function uvof(f, v)
    if not f[5] then return nil end
    for k = 1, 3 do if f[k] == v then return { f[5][k * 2 - 1], f[5][k * 2] } end end
  end
  -- the uv of a corner (an old vertex or a new one) from the face f, else from g
  local function corner_uv(v, f, g)
    local e = ends[v]
    if e then
      local a, b = corner_uv(e[1], f, g), corner_uv(e[2], f, g)
      return { (a[1] + b[1]) / 2, (a[2] + b[2]) / 2 }
    end
    return uvof(f, v) or uvof(g, v) or { 0, 0 }
  end
  local function tri(a, b, c, f, g)
    local sf, sg = 0, 0
    for _, v in ipairs({ a, b, c }) do
      if has(f, v) then sf = sf + 1 end
      if has(g, v) then sg = sg + 1 end
    end
    local s, o = f, g
    if sg > sf then s, o = g, f end
    local uv
    if s[5] then
      local ua, ub, uc = corner_uv(a, s, o), corner_uv(b, s, o), corner_uv(c, s, o)
      uv = { ua[1], ua[2], ub[1], ub[2], uc[1], uc[2] }
    end
    return { a, b, c, s[4], uv }
  end
  local add = {}
  local rungs = {}
  for _, qd in ipairs(plan.quads) do
    local q1, q2, q3, q4 = qd.c[1], qd.c[2], qd.c[3], qd.c[4]
    local m, n = mid[G.edge_key(q1, q2)], mid[qd.e2]
    local f, g = it.f[qd.t], it.f[qd.u]
    local t = { tri(q1, m, n, f, g), tri(q1, n, q4, f, g), tri(m, q2, q3, f, g), tri(m, q3, n, f, g) }
    it.f[qd.t], it.f[qd.u] = t[1], t[2]
    add[#add + 1] = t[3]; add[#add + 1] = t[4]
    rungs[#rungs + 1] = { m, n }
  end
  for _, pl in ipairs(plan.plains) do
    local f = it.f[pl[1]]
    local m = mid[pl[2]]
    for k = 1, 3 do
      if G.edge_key(f[k], f[k % 3 + 1]) == pl[2] then
        local x, y, z = f[k], f[k % 3 + 1], f[(k + 1) % 3 + 1]
        it.f[pl[1]] = tri(x, m, z, f, f)
        add[#add + 1] = tri(m, y, z, f, f)
        break
      end
    end
  end
  for _, f in ipairs(add) do it.f[#it.f + 1] = f end
  local mids = {}
  for _, k in ipairs(plan.edges) do mids[#mids + 1] = mid[k] end
  return { mids = mids, rungs = rungs }
end

function G.loop_cut(it, a, b)
  local plan, err = G.loop_plan(it, a, b)
  if not plan then return nil, err end
  return G.loop_apply(it, plan), plan
end

-- Loop cut with the live mirror (APP-16): the ring of the edge a-b, and when the edge across X = 0
-- (its ends' partners, G.mirror_pairs) is an edge too and is not on the first ring, the ring of that edge
-- as well. A ring that crosses X = 0 holds both edges: it is cut once. Returns { mids, rungs } of both
-- rings together, the first plan and the number of rings cut (1 or 2); or nil and a message.
-- A counterpart edge whose ring cannot be cut leaves the first ring cut alone (rings = 1, mirror_err).
function G.loop_cut_mirror(it, a, b)
  local plan, err = G.loop_plan(it, a, b)
  if not plan then return nil, err end
  local mp = G.mirror_pairs(it, { a, b })
  local ma, mb = mp[a], mp[b]
  local k0 = G.edge_key(a, b)
  local k2
  if ma and mb and ma ~= mb then
    k2 = G.edge_key(ma, mb)
    if k2 == k0 or not G.edge_faces(it)[k2] then k2 = nil end
  end
  if k2 then for _, k in ipairs(plan.edges) do if k == k2 then k2 = nil; break end end end
  local res = G.loop_apply(it, plan)
  res.rings = 1
  if k2 then
    local plan2, err2 = G.loop_plan(it, ma, mb)
    if plan2 then
      local r2 = G.loop_apply(it, plan2)
      for _, m in ipairs(r2.mids) do res.mids[#res.mids + 1] = m end
      for _, r in ipairs(r2.rungs) do res.rungs[#res.rungs + 1] = r end
      res.rings = 2
      res.quads2, res.plains2 = #plan2.quads, #plan2.plains
    else
      res.mirror_err = err2
    end
  end
  return res, plan
end

-- Knife-lite, the plan. The two edges k1 and k2 must be two edges of one
-- triangle (they meet at its apex p). The cut joins the middles of the two
-- edges. The face across each edge (if the edge has one) is cut in 2 too, so
-- no crack stays. Nothing changes. Returns the plan { t = the triangle, p,
-- a, b = its corners (the edges are p-a and p-b), n1, n2 = the faces across
-- the edges or nil } or nil and a message.
function G.knife_plan(it, k1, k2)
  if k1 == k2 then return nil, "choose two different edges" end
  local ef = G.edge_faces(it)
  local l1, l2 = ef[k1], ef[k2]
  if not l1 or not l2 then return nil, "this is not an edge" end
  if #l1 > 2 or #l2 > 2 then return nil, "an edge has more than 2 faces" end
  local t
  for _, x in ipairs(l1) do
    for _, y in ipairs(l2) do if x == y and not t then t = x end end
  end
  if not t then return nil, "the two edges do not share a triangle" end
  local a1, b1 = G.edge_ends(k1)
  local a2, b2 = G.edge_ends(k2)
  local p, a, b
  if a1 == a2 then p, a, b = a1, b1, b2
  elseif a1 == b2 then p, a, b = a1, b1, a2
  elseif b1 == a2 then p, a, b = b1, a1, b2
  elseif b1 == b2 then p, a, b = b1, a1, a2
  else return nil, "the two edges do not share a triangle" end
  local n1, n2
  for _, x in ipairs(l1) do if x ~= t then n1 = x end end
  for _, x in ipairs(l2) do if x ~= t then n2 = x end end
  if n1 and n1 == n2 then return nil, "the faces next to the edges are the same" end
  return { t = t, p = p, a = a, b = b, n1 = n1, n2 = n2, k1 = k1, k2 = k2 }
end

-- Knife-lite, the change: one new vertex in the middle of each of the two
-- edges (the bone of the lower end; the uv by the mean), the triangle in 3
-- triangles, the face across each edge in 2. The faces keep their colour,
-- sheet flag and winding. Returns { mids = {m1, m2}, rung = {m1, m2} the new
-- edge, faces = the number of faces added }.
function G.knife_apply(it, plan)
  local mid = {}
  for _, k in ipairs({ plan.k1, plan.k2 }) do
    local a, b = G.edge_ends(k)
    local p, q = it.v[a], it.v[b]
    mid[k] = G.add_vertex(it, { (p[1] + q[1]) / 2, (p[2] + q[2]) / 2, (p[3] + q[3]) / 2 }, a)
  end
  local ends = { [mid[plan.k1]] = { G.edge_ends(plan.k1) }, [mid[plan.k2]] = { G.edge_ends(plan.k2) } }
  local function uvof(f, v)
    local e = ends[v]
    if e then
      local x, y = uvof(f, e[1]), uvof(f, e[2])
      return { (x[1] + y[1]) / 2, (x[2] + y[2]) / 2 }
    end
    for k = 1, 3 do if f[k] == v then return { f[5][k * 2 - 1], f[5][k * 2] } end end
    return { 0, 0 }
  end
  local function tri(a, b, c, s)
    local uv
    if s[5] then
      local ua, ub, uc = uvof(s, a), uvof(s, b), uvof(s, c)
      uv = { ua[1], ua[2], ub[1], ub[2], uc[1], uc[2] }
    end
    return { a, b, c, s[4], uv }
  end
  local add = {}
  -- the triangle: rotated to x = p; my on x-y, mz on x-z
  local f = it.f[plan.t]
  local k = 1
  while f[k] ~= plan.p do k = k + 1 end
  local x, y, z = f[k], f[k % 3 + 1], f[(k + 1) % 3 + 1]
  local my = mid[G.edge_key(x, y)]
  local mz = mid[G.edge_key(x, z)]
  it.f[plan.t] = tri(x, my, mz, f)
  add[#add + 1] = tri(my, y, z, f)
  add[#add + 1] = tri(my, z, mz, f)
  -- the face across each edge, cut in 2 from the new vertex to its far corner
  for _, pr in ipairs({ { plan.n1, plan.k1 }, { plan.n2, plan.k2 } }) do
    if pr[1] then
      local g, m = it.f[pr[1]], mid[pr[2]]
      for j = 1, 3 do
        if G.edge_key(g[j], g[j % 3 + 1]) == pr[2] then
          local gx, gy, gz = g[j], g[j % 3 + 1], g[(j + 1) % 3 + 1]
          it.f[pr[1]] = tri(gx, m, gz, g)
          add[#add + 1] = tri(m, gy, gz, g)
          break
        end
      end
    end
  end
  for _, nf in ipairs(add) do it.f[#it.f + 1] = nf end
  return { mids = { mid[plan.k1], mid[plan.k2] }, rung = { mid[plan.k1], mid[plan.k2] }, faces = #add }
end

function G.knife(it, k1, k2)
  local plan, err = G.knife_plan(it, k1, k2)
  if not plan then return nil, err end
  return G.knife_apply(it, plan), plan
end
----------------------------------------------------------------- finish tools (APP-12): bevel, box projection, humanoid base
do
  local function sub3(a, b) return { a[1] - b[1], a[2] - b[2], a[3] - b[3] } end
  local function dot3(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
  local function cross3(a, b)
    return { a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1] }
  end
  local function len3(a) return sqrt(dot3(a, a)) end

  -- Bevel. An edge shared by two faces becomes a narrow strip: each end of the edge is split in two
  -- vertices, one for each face, pushed w into its face and across the edge; two triangles fill the strip.
  -- A vertex where several chosen edges meet gets one vertex for each sector between them, and a cap
  -- face (a polygon of the sector vertices) when 3 or more edges meet. A vertex with one chosen edge keeps
  -- its fan whole except one face that is cut in two (the "straddle"), so no gap stays. Faces keep their
  -- colour and uv; the strip takes the colour and uv of the face that holds the edge from a to b.
  G.BEVEL_FLAT = 0.9995         -- two faces with normals this close (dot) are flat: no bevel there

  -- the direction in the plane of face f, across the edge (v, u), into the face
  local function bevel_dir(it, f, v, u)
    local n = G.unit(G.normal_of(it, f))
    local d = G.unit(cross3(n, G.unit(sub3(it.v[u], it.v[v]))))
    local third
    for k = 1, 3 do if f[k] ~= v and f[k] ~= u then third = f[k] end end
    if dot3(d, sub3(it.v[third], it.v[v])) < 0 then d = { -d[1], -d[2], -d[3] } end
    return d
  end

  -- the faces around vertex v in order (each shares the edge (v, y) with the next); nil if not a closed fan
  local function bevel_fan(it, v, fl)
    local out, info = {}, {}
    for _, fi in ipairs(fl) do
      local f = it.f[fi]
      local k = 1
      while f[k] ~= v do k = k + 1 end
      local x, y = f[k % 3 + 1], f[(k + 1) % 3 + 1]
      if out[x] then return nil end
      out[x] = fi
      info[fi] = { x = x, y = y }
    end
    local cyc, seen, cur = { fl[1] }, { [fl[1]] = true }, fl[1]
    for i = 2, #fl do
      local nxt = out[info[cur].y]
      if not nxt or seen[nxt] then return nil end
      seen[nxt] = true
      cyc[i] = nxt
      cur = nxt
    end
    if out[info[cur].y] ~= fl[1] then return nil end
    return cyc, info
  end

  local function corner_uv(f, v)
    if not f[5] then return nil end
    for k = 1, 3 do if f[k] == v then return { f[5][k * 2 - 1], f[5][k * 2] } end end
  end

  -- G.bevel_plan(it, keys, w): keys = a list of edge keys. Nothing changes. Returns the plan or nil and a
  -- message. plan.w is the width used (never more than 0.45 of the height of a face over the edge),
  -- plan.flat the number of flat edges left out, plan.edges the edges that will be bevelled.
  function G.bevel_plan(it, keys, w)
    if not w or w <= 0 then return nil, "the width must be more than 0" end
    local ef = G.edge_faces(it)
    local chosen, order, flat, hmin = {}, {}, 0, 1e30
    for _, k in ipairs(keys) do
      if chosen[k] == nil then
        local l = ef[k]
        if not l then return nil, "this is not an edge" end
        if #l ~= 2 then return nil, "an edge on a border, or with more than 2 faces, cannot be bevelled" end
        local f1, f2, a, b
        for _, fi in ipairs(l) do
          local f = it.f[fi]
          for j = 1, 3 do
            local x, y = f[j], f[j % 3 + 1]
            if G.edge_key(x, y) == k then
              if not f1 then f1, a, b = fi, x, y
              elseif x == b and y == a then f2 = fi end
            end
          end
        end
        if not f2 then return nil, "the two faces of an edge look different ways: turn one over (i)" end
        local n1, n2 = G.normal_of(it, it.f[f1]), G.normal_of(it, it.f[f2])
        local l1, l2 = len3(n1), len3(n2)
        if l1 < 1e-12 or l2 < 1e-12 then return nil, "an edge has a face with no area" end
        if dot3(n1, n2) / (l1 * l2) > G.BEVEL_FLAT then
          flat = flat + 1
          chosen[k] = false
        else
          local el = len3(sub3(it.v[b], it.v[a]))
          hmin = min(hmin, l1 / el, l2 / el)
          chosen[k] = { a = a, b = b, f1 = f1, f2 = f2 }
          order[#order + 1] = k
        end
      end
    end
    if #order == 0 then
      return nil, flat > 0 and "the chosen edges are flat: nothing to bevel" or "choose edges first"
    end
    w = min(w, 0.45 * hmin)
    -- the faces around each vertex that an edge touches
    local vs, vlist = {}, {}
    for _, k in ipairs(order) do
      for _, v in ipairs({ chosen[k].a, chosen[k].b }) do
        if not vs[v] then vs[v] = true; vlist[#vlist + 1] = v end
      end
    end
    table.sort(vlist)
    local vf = {}
    for fi, f in ipairs(it.f) do
      for j = 1, 3 do
        if vs[f[j]] then
          vf[f[j]] = vf[f[j]] or {}
          table.insert(vf[f[j]], fi)
        end
      end
    end
    local cps, cm, caps = {}, {}, {}          -- copy specs, corner map { [face] = { [v] = copy | {R, L} } }, caps
    local function put(fi, v, c)
      cm[fi] = cm[fi] or {}
      cm[fi][v] = c
    end
    for _, v in ipairs(vlist) do
      local cyc, info = bevel_fan(it, v, vf[v])
      if not cyc then
        return nil, "vertex " .. v .. " is on a border or not a closed fan of faces: bevel stops"
      end
      local n, br = #cyc, {}
      for i = 1, n do
        local y = info[cyc[i]].y
        if chosen[G.edge_key(v, y)] then br[#br + 1] = i end
      end
      local k = #br
      local pv = it.v[v]
      if k == 1 then
        if n < 3 then return nil, "vertex " .. v .. " has too few faces" end
        local chain = {}
        for j = 1, n do chain[j] = cyc[(br[1] + j - 1) % n + 1] end
        local u = info[cyc[br[1]]].y
        local dir = G.unit(sub3(it.v[u], pv))
        local m, best = 2, 1e30
        for j = 2, n - 1 do
          local f = it.f[chain[j]]
          local qa, qb, qc = it.v[f[1]], it.v[f[2]], it.v[f[3]]
          local c = G.unit(sub3({ (qa[1] + qb[1] + qc[1]) / 3, (qa[2] + qb[2] + qc[2]) / 3, (qa[3] + qb[3] + qc[3]) / 3 }, pv))
          local d = dot3(c, dir)
          if d < best - 1e-9 then best, m = d, j end
        end
        local dl, dr = bevel_dir(it, it.f[chain[1]], v, u), bevel_dir(it, it.f[chain[n]], v, u)
        cps[#cps + 1] = { v = v, p = { pv[1] + w * dl[1], pv[2] + w * dl[2], pv[3] + w * dl[3] } }
        local L = #cps
        cps[#cps + 1] = { v = v, p = { pv[1] + w * dr[1], pv[2] + w * dr[2], pv[3] + w * dr[3] } }
        local R = #cps
        for j = 1, n do
          if j < m then put(chain[j], v, L) elseif j > m then put(chain[j], v, R) else put(chain[j], v, { R, L }) end
        end
      else
        local secs = {}
        for s = 1, k do
          local from, to = br[s] + 1, br[s % k + 1]
          local faces = {}
          local j = from
          while true do
            faces[#faces + 1] = cyc[(j - 1) % n + 1]
            if (j - 1) % n + 1 == to then break end
            j = j + 1
          end
          local d1 = bevel_dir(it, it.f[faces[1]], v, info[cyc[br[s]]].y)
          local d2 = bevel_dir(it, it.f[faces[#faces]], v, info[cyc[to]].y)
          local den = max(1 + dot3(d1, d2), 0.25)
          cps[#cps + 1] = { v = v, p = { pv[1] + w / den * (d1[1] + d2[1]), pv[2] + w / den * (d1[2] + d2[2]),
                                         pv[3] + w / den * (d1[3] + d2[3]) } }
          for _, fi in ipairs(faces) do put(fi, v, #cps) end
          secs[s] = { c = #cps, f = faces[1] }
        end
        if k >= 3 then
          local cap = { v = v, c = {}, f = secs[1].f }
          for s = 1, k do cap.c[s] = secs[s].c end
          caps[#caps + 1] = cap
        end
      end
    end
    local strips = {}
    for _, k in ipairs(order) do strips[#strips + 1] = chosen[k] end
    return { w = w, flat = flat, edges = order, strips = strips, cps = cps, cm = cm, caps = caps }
  end

  -- G.bevel_apply(it, plan): makes the change. The first vertex of each split keeps its number, so no vertex
  -- is left over. Returns { strips = {the new faces of the strips}, verts = {the new vertex numbers},
  -- faces = the number of faces added }.
  function G.bevel_apply(it, plan)
    local id, newv, used = {}, {}, {}
    for i, c in ipairs(plan.cps) do
      if not used[c.v] then
        used[c.v] = true
        id[i] = c.v
        it.v[c.v] = { c.p[1], c.p[2], c.p[3] }
      else
        id[i] = G.add_vertex(it, c.p, c.v)
      end
      newv[#newv + 1] = id[i]
    end
    local nf0 = #it.f
    local old = {}
    for fi in pairs(plan.cm) do old[fi] = it.f[fi] end
    local function cid(fi, v)
      local c = plan.cm[fi] and plan.cm[fi][v]
      if type(c) == "number" then return id[c] end
      return v
    end
    local add = {}
    for fi, f in pairs(old) do
      local poly, uvs = {}, {}
      for j = 1, 3 do
        local v = f[j]
        local c = plan.cm[fi][v]
        local uv = f[5] and { f[5][j * 2 - 1], f[5][j * 2] }
        if type(c) == "table" then
          poly[#poly + 1] = id[c[1]]; uvs[#poly] = uv
          poly[#poly + 1] = id[c[2]]; uvs[#poly] = uv
        else
          poly[#poly + 1] = c and id[c] or v; uvs[#poly] = uv
        end
      end
      local function tri(i, j, l)
        local g = { poly[i], poly[j], poly[l], f[4] }
        if f[5] then g[5] = { uvs[i][1], uvs[i][2], uvs[j][1], uvs[j][2], uvs[l][1], uvs[l][2] } end
        return g
      end
      it.f[fi] = tri(1, 2, 3)
      for j = 3, #poly - 1 do add[#add + 1] = tri(1, j, j + 1) end
    end
    -- the strips: (b1, a1, a2) and (b1, a2, b2)
    local strips, nextra = {}, #add
    for _, e in ipairs(plan.strips) do
      local f1 = old[e.f1] or it.f[e.f1]
      local a1, b1, a2, b2 = cid(e.f1, e.a), cid(e.f1, e.b), cid(e.f2, e.a), cid(e.f2, e.b)
      local ua, ub = corner_uv(f1, e.a), corner_uv(f1, e.b)
      local function s(p, q, r, up, uq, ur)
        local g = { p, q, r, f1[4] }
        if f1[5] then g[5] = { up[1], up[2], uq[1], uq[2], ur[1], ur[2] } end
        return g
      end
      add[#add + 1] = s(b1, a1, a2, ub, ua, ua)
      add[#add + 1] = s(b1, a2, b2, ub, ua, ub)
    end
    local nstrip = #add
    for _, cap in ipairs(plan.caps) do
      local f = old[cap.f] or it.f[cap.f]
      local uv = corner_uv(f, cap.v)
      for s = 2, #cap.c - 1 do
        local g = { id[cap.c[1]], id[cap.c[s]], id[cap.c[s + 1]], f[4] }
        if uv then g[5] = { uv[1], uv[2], uv[1], uv[2], uv[1], uv[2] } end
        add[#add + 1] = g
      end
    end
    for i, g in ipairs(add) do
      it.f[#it.f + 1] = g
      if i > nextra and i <= nstrip then strips[#strips + 1] = #it.f end
    end
    return { strips = strips, verts = newv, faces = #it.f - nf0 }
  end

  function G.bevel(it, keys, w)
    local plan, err = G.bevel_plan(it, keys, w)
    if not plan then return nil, err end
    return G.bevel_apply(it, plan), plan
  end

  -- Box projection. G.project_box(it, fs, rect): fs = a list of face numbers (nil: every face), rect =
  -- { x, y, cell } the place on the sprite sheet. The sheet area is 3 x 2 cells of cell pixels: the six
  -- sides +x, -x, +y / -y, +z, -z. Each face goes to the cell of the axis its normal is nearest to, drawn
  -- as seen from that side (+y: from above, +z: from the front, up is up), with the same scale on every
  -- side (the largest size of the faces fits one cell, 1 pixel kept free round it). The faces become
  -- textured (colour -1) with the uv as sheet pixels. G.project_plan changes nothing and returns the plan
  -- { placed, scale, rect } or nil and a message; G.project_apply makes it and returns the number of faces
  -- placed and the scale in pixels per unit; G.project_box does both.
  local SIDE_CELL = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }
  function G.project_plan(it, fs, rect, sheet_w, sheet_h)
    local x0, y0, cell = rect[1], rect[2], rect[3]
    if not cell or cell < 4 then return nil, "the cell must be 4 pixels or more" end
    if sheet_w and (x0 < 0 or y0 < 0 or x0 + cell * 3 > sheet_w or y0 + cell * 2 > sheet_h) then
      return nil, "the sheet is too small for 3 x 2 cells of " .. cell
    end
    if not fs then
      fs = {}
      for i = 1, #it.f do fs[i] = i end
    end
    if #fs == 0 then return nil, "no faces to project" end
    local lo, hi = { 1e30, 1e30, 1e30 }, { -1e30, -1e30, -1e30 }
    for _, fi in ipairs(fs) do
      for j = 1, 3 do
        local p = it.v[it.f[fi][j]]
        for a = 1, 3 do lo[a], hi[a] = min(lo[a], p[a]), max(hi[a], p[a]) end
      end
    end
    local ext = max(hi[1] - lo[1], hi[2] - lo[2], hi[3] - lo[3])
    if ext < 1e-9 then return nil, "the faces have no size" end
    local sc = (cell - 2) / ext
    local placed = {}
    for _, fi in ipairs(fs) do
      local f = it.f[fi]
      local n = G.normal_of(it, f)
      local ax, best = 1, abs(n[1])
      if abs(n[2]) > best + 1e-9 then ax, best = 2, abs(n[2]) end
      if abs(n[3]) > best + 1e-9 then ax = 3 end
      local side = (ax - 1) * 2 + (n[ax] >= 0 and 1 or 2)
      local c = SIDE_CELL[side]
      local ox, oy = x0 + c[1] * cell + 1, y0 + c[2] * cell + 1
      local uv = {}
      for j = 1, 3 do
        local p = it.v[f[j]]
        local u, v
        if ax == 1 then
          u = side == 1 and hi[3] - p[3] or p[3] - lo[3]
          v = hi[2] - p[2]
        elseif ax == 2 then
          u = p[1] - lo[1]
          v = side == 3 and p[3] - lo[3] or hi[3] - p[3]
        else
          u = side == 5 and p[1] - lo[1] or hi[1] - p[1]
          v = hi[2] - p[2]
        end
        uv[j * 2 - 1], uv[j * 2] = ox + u * sc, oy + v * sc
      end
      placed[#placed + 1] = { f, uv }
    end
    return { placed = placed, scale = sc, rect = { x0, y0, cell } }
  end

  function G.project_apply(it, plan)
    for _, pl in ipairs(plan.placed) do pl[1][4], pl[1][5] = -1, pl[2] end
    return #plan.placed, plan.scale
  end

  function G.project_box(it, fs, rect, sheet_w, sheet_h)
    local plan, err = G.project_plan(it, fs, rect, sheet_w, sheet_h)
    if not plan then return nil, err end
    return G.project_apply(it, plan)
  end

  -- Humanoid base. 17 boxes on 16 bones (pelvis, spine, neck, head, and on each side uarm, larm, hand, uleg,
  -- lleg, foot; names end in .L (x > 0) or .R), 136 vertices, 204 triangles, 5 colours. The body is about
  -- 1.9 high, stands on y = 0 and faces -z. Bone order puts a parent before its children.
  local HB = {          -- name, parent (0 = none), head, tail
    { "pelvis", 0, { 0, 0.875, 0 }, { 0, 1, 0 } },
    { "spine", 1, { 0, 1, 0 }, { 0, 1.5, 0 } },
    { "neck", 2, { 0, 1.5, 0 }, { 0, 1.625, 0 } },
    { "head", 3, { 0, 1.625, 0 }, { 0, 1.875, 0 } },
    { "uarm.L", 2, { 0.375, 1.5, 0 }, { 0.375, 1.25, 0 } },
    { "larm.L", 5, { 0.375, 1.25, 0 }, { 0.375, 1, 0 } },
    { "hand.L", 6, { 0.375, 1, 0 }, { 0.375, 0.875, 0 } },
    { "uarm.R", 2, { -0.375, 1.5, 0 }, { -0.375, 1.25, 0 } },
    { "larm.R", 8, { -0.375, 1.25, 0 }, { -0.375, 1, 0 } },
    { "hand.R", 9, { -0.375, 1, 0 }, { -0.375, 0.875, 0 } },
    { "uleg.L", 1, { 0.125, 0.875, 0 }, { 0.125, 0.5, 0 } },
    { "lleg.L", 11, { 0.125, 0.5, 0 }, { 0.125, 0.1, 0 } },
    { "foot.L", 12, { 0.125, 0.1, 0 }, { 0.125, 0.05, -0.15 } },
    { "uleg.R", 1, { -0.125, 0.875, 0 }, { -0.125, 0.5, 0 } },
    { "lleg.R", 14, { -0.125, 0.5, 0 }, { -0.125, 0.1, 0 } },
    { "foot.R", 15, { -0.125, 0.1, 0 }, { -0.125, 0.05, -0.15 } },
  }
  local TROUSERS, SHIRT, SKIN, HAIR, BOOTS = 0x404450, 0x3478C4, 0xF0C090, 0x5A3A1E, 0x2A1E18
  -- bone number (1-based), lo, hi, colour
  local HP = {
    { 1, { -0.2, 0.85, -0.12 }, { 0.2, 1.0, 0.12 }, TROUSERS },
    { 2, { -0.25, 1.0, -0.14 }, { 0.25, 1.5, 0.14 }, SHIRT },
    { 3, { -0.06, 1.5, -0.06 }, { 0.06, 1.625, 0.06 }, SKIN },
    { 4, { -0.12, 1.625, -0.12 }, { 0.12, 1.85, 0.12 }, SKIN },
    { 4, { -0.13, 1.8, -0.13 }, { 0.13, 1.9, 0.13 }, HAIR },
  }
  for s = 1, 2 do                                   -- 1 = .L (x > 0), 2 = .R
    local sg, b0 = s == 1 and 1 or -1, s == 1 and 0 or 3
    local function bx(lo, hi) return sg > 0 and lo or -hi, sg > 0 and hi or -lo end
    local function add(bone, x0, y0, z0, x1, y1, z1, col)
      local a, b = bx(x0, x1)
      HP[#HP + 1] = { bone, { a, y0, z0 }, { b, y1, z1 }, col }
    end
    add(5 + b0, 0.315, 1.25, -0.06, 0.435, 1.5, 0.06, SHIRT)
    add(6 + b0, 0.32, 1.0, -0.055, 0.43, 1.25, 0.055, SKIN)
    add(7 + b0, 0.325, 0.86, -0.05, 0.425, 1.0, 0.05, SKIN)
  end
  for s = 1, 2 do
    local sg, b0 = s == 1 and 1 or -1, s == 1 and 11 or 14
    local function add(bone, x0, y0, z0, x1, y1, z1, col)
      local a, b = sg > 0 and x0 or -x1, sg > 0 and x1 or -x0
      HP[#HP + 1] = { bone, { a, y0, z0 }, { b, y1, z1 }, col }
    end
    add(b0, 0.045, 0.5, -0.1, 0.205, 0.875, 0.1, TROUSERS)
    add(b0 + 1, 0.055, 0.1, -0.08, 0.195, 0.5, 0.08, TROUSERS)
    add(b0 + 2, 0.045, 0, -0.2, 0.205, 0.1, 0.1, BOOTS)
  end
  G.HUMANOID_BONES = #HB

  -- the ANIM part of a skeleton with no animation: bones = { {name, parent (0 = none), head, tail}, ... },
  -- vb = the bone of each vertex (from 0)
  function G.rig_part(name, bones, vb)
    local nb, n = #bones, #vb
    local out = { name16(name), spack("<I2I2I2I2", nb, 0, n, 0) }
    for _, b in ipairs(bones) do
      out[#out + 1] = name16(b[1]) .. spack("<i2I2ffffff", b[2] - 1, 0, b[3][1], b[3][2], b[3][3], b[4][1], b[4][2], b[4][3])
    end
    local s = {}
    for i = 1, n do s[i] = string.char(vb[i]) end
    out[#out + 1] = table.concat(s) .. string.rep("\0", (4 - n % 4) % 4)
    return table.concat(out)
  end

  -- G.humanoid(name): a new model item with the base body and its skeleton (it.ac, it.vb, it.bones = 16)
  function G.humanoid(name)
    local it = { kind = "model", name = name, v = {}, f = {}, vb = {}, dirty = true }
    for _, p in ipairs(HP) do
      local lo, hi = p[2], p[3]
      local mid = { (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, (lo[3] + hi[3]) / 2 }
      local n0 = #it.v
      G.primitive(it, "cube", mid, 1, p[4], p[1] - 1)
      for i = n0 + 1, #it.v do
        local q = it.v[i]
        q[1] = mid[1] + (q[1] - mid[1]) * (hi[1] - lo[1])
        q[2] = mid[2] + (q[2] - mid[2]) * (hi[2] - lo[2])
        q[3] = mid[3] + (q[3] - mid[3]) * (hi[3] - lo[3])
      end
    end
    it.ac = G.rig_part(name, HB, it.vb)
    it.bones, it.clips = #HB, 0
    return it
  end
end

-- Snap. G.snap_grid(p, step, axis): a copy of the point p with each axis
-- rounded to a multiple of step (only the axis given, 1-3, when there is one).
-- G.snap_vertex(it, p, proj, px, skip): the number of the vertex of it nearest
-- to p on the screen (proj(q) gives x, y), within px pixels, and the distance;
-- the vertices in the set skip are not looked at. G.snap_pos(it, p, mode, step,
-- proj, px, skip, axis): mode 1 = grid, 2 = vertex, 3 = both (a vertex in range
-- wins over the grid). With an axis (1-3) only that axis changes; with "n" the
-- grid does not apply. Returns the new point and the vertex number it took (or nil).
function G.snap_grid(p, step, axis)
  local q = { p[1], p[2], p[3] }
  for k = 1, 3 do
    if axis == nil or axis == k then
      q[k] = math.floor(math.floor(q[k] / step + 0.5) * step * 1e5 + 0.5) / 1e5
    end
  end
  return q
end

function G.snap_vertex(it, p, proj, px, skip)
  local x0, y0 = proj(p)
  if not x0 then return nil end
  local best, bd = nil, px * px
  for i, v in ipairs(it.v) do
    if not (skip and skip[i]) then
      local x, y = proj(v)
      if x then
        local d = (x - x0) * (x - x0) + (y - y0) * (y - y0)
        if d <= bd then best, bd = i, d end
      end
    end
  end
  return best, best and math.sqrt(bd)
end

function G.snap_pos(it, p, mode, step, proj, px, skip, axis)
  if mode == nil or mode == 0 then return p, nil end
  if mode >= 2 then
    local i = G.snap_vertex(it, p, proj, px, skip)
    if i then
      local v, q = it.v[i], { p[1], p[2], p[3] }
      for k = 1, 3 do if type(axis) ~= "number" or axis == k then q[k] = v[k] end end
      return q, i
    end
  end
  if mode % 2 == 1 and axis ~= "n" then return G.snap_grid(p, step, type(axis) == "number" and axis or nil), nil end
  return p, nil
end

-- A primitive ("cube", "plane", "sphere" or "cylinder") of the width size, in
-- the colour col, with its middle at centre, added to the model it as a new
-- part. The faces look out. The new vertices take the bone given (or 0) when
-- the model has bones. Returns the list of the new vertex numbers.
function G.primitive(it, shape, centre, size, col, bone)
  size = size or 1
  local v, f = {}, {}
  if shape == "cube" then
    for _, p in ipairs({ { -0.5, -0.5, -0.5 }, { 0.5, -0.5, -0.5 }, { 0.5, 0.5, -0.5 }, { -0.5, 0.5, -0.5 },
                         { -0.5, -0.5, 0.5 }, { 0.5, -0.5, 0.5 }, { 0.5, 0.5, 0.5 }, { -0.5, 0.5, 0.5 } }) do v[#v + 1] = p end
    for _, t in ipairs({ { 1, 4, 3 }, { 1, 3, 2 }, { 5, 6, 7 }, { 5, 7, 8 }, { 1, 2, 6 }, { 1, 6, 5 },
                         { 4, 8, 7 }, { 4, 7, 3 }, { 1, 5, 8 }, { 1, 8, 4 }, { 2, 3, 7 }, { 2, 7, 6 } }) do
      f[#f + 1] = { t[1], t[2], t[3], col }
    end
  elseif shape == "plane" then
    v = { { -0.5, 0, -0.5 }, { 0.5, 0, -0.5 }, { 0.5, 0, 0.5 }, { -0.5, 0, 0.5 } }
    f = { { 1, 4, 3, col }, { 1, 3, 2, col } }
  elseif shape == "cylinder" then
    local segs = 8
    for s = 0, segs - 1 do
      local b = s / segs * 2 * pi
      v[#v + 1] = { 0.5 * cos(b), 0.5, 0.5 * sin(b) }
    end
    for s = 0, segs - 1 do
      local b = s / segs * 2 * pi
      v[#v + 1] = { 0.5 * cos(b), -0.5, 0.5 * sin(b) }
    end
    v[#v + 1] = { 0, 0.5, 0 }
    v[#v + 1] = { 0, -0.5, 0 }
    local top, bot = 2 * segs + 1, 2 * segs + 2
    for s = 0, segs - 1 do
      local n = (s + 1) % segs
      f[#f + 1] = { top, s + 1, n + 1, col }
      f[#f + 1] = { bot, segs + n + 1, segs + s + 1, col }
      -- the diagonal of a side quad leans one way on one half and the other way on the other half, so the
      -- triangles are mirror images across X = 0 (the quad s and the quad 3 - s mod 8 swap)
      if s % 4 < 2 then
        f[#f + 1] = { s + 1, segs + s + 1, segs + n + 1, col }
        f[#f + 1] = { s + 1, segs + n + 1, n + 1, col }
      else
        f[#f + 1] = { s + 1, segs + s + 1, n + 1, col }
        f[#f + 1] = { n + 1, segs + s + 1, segs + n + 1, col }
      end
    end
  else
    local rings, segs = 6, 12
    v[1] = { 0, 0.5, 0 }
    for r = 1, rings - 1 do
      local a = r / rings * pi
      for s = 0, segs - 1 do
        local b = s / segs * 2 * pi
        v[#v + 1] = { 0.5 * sin(a) * cos(b), 0.5 * cos(a), 0.5 * sin(a) * sin(b) }
      end
    end
    v[#v + 1] = { 0, -0.5, 0 }
    local last = #v
    local function at(r, s) return 2 + (r - 1) * segs + s % segs end
    for s = 0, segs - 1 do
      f[#f + 1] = { 1, at(1, s), at(1, s + 1), col }
      f[#f + 1] = { last, at(rings - 1, s + 1), at(rings - 1, s), col }
      for r = 1, rings - 2 do
        -- the diagonal leans one way for s % 6 < 3 and the other way after, so the triangles are mirror
        -- images across X = 0 (the quad s and the quad 5 - s mod 12 swap)
        if s % 6 < 3 then
          f[#f + 1] = { at(r, s), at(r + 1, s), at(r + 1, s + 1), col }
          f[#f + 1] = { at(r, s), at(r + 1, s + 1), at(r, s + 1), col }
        else
          f[#f + 1] = { at(r, s), at(r + 1, s), at(r, s + 1), col }
          f[#f + 1] = { at(r + 1, s), at(r + 1, s + 1), at(r, s + 1), col }
        end
      end
    end
  end
  if shape ~= "plane" then G.orient({ v = v, f = f }, { 0, 0, 0 }) end
  centre = centre or { 0, 0, 0 }
  local base, out = #it.v, {}
  for i, p in ipairs(v) do
    it.v[base + i] = { centre[1] + p[1] * size, centre[2] + p[2] * size, centre[3] + p[3] * size }
    if it.vb then it.vb[base + i] = bone or 0 end
    out[i] = base + i
  end
  for _, t in ipairs(f) do it.f[#it.f + 1] = { t[1] + base, t[2] + base, t[3] + base, t[4] } end
  return out
end

-- A part with the live mirror (APP-16). Like G.primitive, and when the middle is off the plane X = 0
-- (more than 0.001 away) the part is added twice: the second is the mirror image of the first (the
-- vertices at -x, the corners of each triangle turned over, so the faces still look out). The second
-- part takes the bone mirror_bone (the same bone when nil). A part in the middle of the plane is added
-- once. Returns the list of all new vertex numbers and the list of the vertices of the mirror part
-- (empty when there is none).
function G.primitive_mirror(it, shape, centre, size, col, bone, mirror_bone)
  local nf0 = #it.f
  local out = G.primitive(it, shape, centre, size, col, bone)
  local twin = {}
  centre = centre or { 0, 0, 0 }
  if math.abs(centre[1]) <= 0.001 then return out, twin end
  local base, nv = out[1] - 1, #out
  for i = 1, nv do
    local p = it.v[out[i]]
    local n = G.add_vertex(it, { -p[1], p[2], p[3] }, out[i])
    if it.vb then it.vb[n] = mirror_bone or bone or 0 end
    twin[i] = n
  end
  for fi = nf0 + 1, #it.f do
    local f = it.f[fi]
    it.f[#it.f + 1] = { twin[f[1] - base], twin[f[3] - base], twin[f[2] - base], f[4] }
  end
  for _, n in ipairs(twin) do out[#out + 1] = n end
  return out, twin
end

local QUAD, TRI = { { 1, 2, 3 }, { 1, 3, 4 } }, { { 1, 2, 3 } }
T.QUAD, T.TRI = QUAD, TRI

-- a model's part of the MESH section: m.mc, with m.nv vertices, m.nt
-- triangles and m.vb (the bone of each vertex, one byte each); nil if the
-- model has no faces. Corners of different bones stay apart.
function T.encode_mesh(m)
  m.mc, m.vb, m.nv, m.nt = nil, nil, 0, 0
  local rig = m.rig
  local nb = rig and #rig.bones or 1
  local verts, index, vb, tris = {}, {}, {}, {}
  local function vid(p, bone)
    if not rig or not bone or bone < 1 or bone > nb then bone = 1 end
    local k = pkey(p)
    if rig then k = k .. "#" .. bone end
    local i = index[k]
    if not i then
      i = #verts + 1
      index[k] = i
      verts[i] = spack("<fff", p[1], p[2], p[3])
      vb[i] = string.char(bone - 1)
    end
    return i
  end
  for _, f in ipairs(m.faces) do
    local ids = {}
    for k, p in ipairs(f.p) do ids[k] = vid(p, f.b and f.b[k]) end
    local col = f.c and (f.c & 0xFFFFFF) or TEXTURED
    for _, t in ipairs(#f.p == 4 and QUAD or TRI) do
      local a, b, c = ids[t[1]], ids[t[2]], ids[t[3]]
      if a ~= b and b ~= c and a ~= c then
        local uv = { 0, 0, 0, 0, 0, 0 }
        if not f.c then
          for j = 1, 3 do
            local q = f.uv[t[j]]
            uv[j * 2 - 1] = clamp(round(q[1] * 8), 0, 65535)
            uv[j * 2] = clamp(round(q[2] * 8), 0, 65535)
          end
        end
        tris[#tris + 1] = spack("<I2I2I2I2I4I2I2I2I2I2I2", a - 1, b - 1, c - 1, 0, col,
                                uv[1], uv[2], uv[3], uv[4], uv[5], uv[6])
      end
    end
  end
  if #tris == 0 then return true end
  if #verts > T.LIMIT_V then return false, #verts .. " vertices: format limit " .. T.LIMIT_V end
  if #tris > T.LIMIT_T then return false, #tris .. " triangles: format limit " .. T.LIMIT_T end
  m.mc = name16(m.name) .. spack("<I2I2I4", #verts, #tris, 0) .. table.concat(verts) .. table.concat(tris)
  m.vb, m.nv, m.nt = table.concat(vb), #verts, #tris
  return true
end

-- the model's rig in the ANIM section: m.ac (after encode_mesh)
function T.encode_anim(m)
  m.ac = nil
  local rig = m.rig
  if not rig or #rig.bones == 0 or not m.mc then return end
  local nb = #rig.bones
  local out = { name16(m.name), spack("<I2I2I2I2", nb, #rig.clips, m.nv, 0) }
  for _, b in ipairs(rig.bones) do
    out[#out + 1] = name16(b.name) .. spack("<i2I2ffffff", b.parent - 1, 0, b.head[1], b.head[2], b.head[3],
                                            b.tail[1], b.tail[2], b.tail[3])
  end
  out[#out + 1] = m.vb .. string.rep("\0", (4 - m.nv % 4) % 4)
  for _, c in ipairs(rig.clips) do
    out[#out + 1] = name16(c.name) .. spack("<I2BBf", #c.keys, c.mode, c.loop and 1 or 0, c.length)
    for _, key in ipairs(c.keys) do
      out[#out + 1] = spack("<f", key.t)
      for i = 1, nb do
        local p = key.pose[i]
        if p then
          out[#out + 1] = spack("<fffffff", p.q[1], p.q[2], p.q[3], p.q[4], p.t[1], p.t[2], p.t[3])
        else
          out[#out + 1] = spack("<fffffff", 0, 0, 0, 1, 0, 0, 0)
        end
      end
    end
  end
  m.ac = table.concat(out)
end

-- the parts of the sections, one per model: { {name, chunk}, ... }
function T.split_mesh(bin)
  local out = {}
  if not bin then return out, 0.25 end
  local n, inset = sunpack("<I2I2", bin)
  local pos = 9
  for _ = 1, n do
    local nv, nf = sunpack("<I2I2", bin, pos + 16)
    local size = 24 + nv * 12 + nf * 24
    out[#out + 1] = { cname(bin, pos), bin:sub(pos, pos + size - 1) }
    pos = pos + size
  end
  return out, inset / 256
end

function T.split_anim(bin)
  local out = {}
  if not bin then return out end
  local n = sunpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local nb, nc, nv = sunpack("<I2I2I2", bin, pos + 16)
    local p = pos + 24 + nb * 44 + ((nv + 3) & ~3)
    for _ = 1, nc do
      local nk = sunpack("<I2", bin, p + 16)
      p = p + 24 + nk * (4 + nb * 28)
    end
    out[cname(bin, pos)] = bin:sub(pos, p - 1)
    pos = p
  end
  return out
end

----------------------------------------------------------------- model files: /bmm and /glb (APP-9)

-- What bm Studio, bm Animator and bm Mesh share to bring a model in. A .glb in /glb is read only
-- (glb3d); a .bmm in /bmm is a model file (res_read, res_write). The item is the one of bm Mesh
-- (T.G.model_item: v, f, and vb, bones, clips when the model has a skeleton), plus
--   item.sheet = { type = 2 | 5, data = the sheet section }   when the model has its own sheet
--   item.inset                                                 the texture margin (sheet pixels), 0.25
--   item.file = { name =, author = }                           the name shown and the author of the file
T.BMM_DIR, T.GLB_DIR = "/bmm", "/glb"

-- the name of a .bmm: 1 to 8 of a-z 0-9 _ - (what res_write takes)
local function bmm_stem(name)
  local s = tostring(name or ""):lower():gsub("%.bmm$", ""):gsub("%.glb$", "")
  if s:match("^[a-z0-9_%-]+$") and #s <= 8 then return s end
end

-- the model's name in an editor: 15 characters of letters, digits and _
local function item_name(s)
  s = tostring(s):lower():gsub("[^%w_]", ""):sub(1, 15)
  return s ~= "" and s or "model"
end

-- the models on the card: { { name = "knight", kind = "bmm" | "glb", glb = true if a .glb of that name
-- is there }, ... } sorted by name. kind "bmm": the converted model (preferred, it may be edited);
-- kind "glb": only the .glb, to be converted. Names in lower case.
function T.model_files()
  local by, out = {}, {}
  for _, d in ipairs({ { T.BMM_DIR, "bmm" }, { T.GLB_DIR, "glb" } }) do
    for _, f in ipairs(ls(d[1])) do
      local stem, ext = f.name:lower():match("^(.+)%.(%w+)$")
      if not f.dir and ext == d[2] then
        local e = by[stem]
        if not e then
          e = { name = stem, kind = d[2] }
          by[stem] = e
          out[#out + 1] = e
        end
        if d[2] == "glb" then e.glb = true end
        if d[2] == "bmm" then e.kind = "bmm" end
      end
    end
  end
  table.sort(out, function(a, b) return a.name < b.name end)
  return out
end

local function model_entry(name)
  name = tostring(name or ""):lower():gsub("%.bmm$", ""):gsub("%.glb$", "")
  for _, e in ipairs(T.model_files()) do
    if e.name == name then return e end
  end
end

-- a model from a file: the item, or nil and a message. name: as model_files() lists it.
--   opts.from   "bmm" | "glb": that file (default: the .bmm if there is one, else the .glb)
--   opts.model  of a .bmm with several models: the name or the number (default: the first)
--   opts.name   the name of the item (default: the file's)
-- for a .glb (glb3d): opts.height (2), opts.faces (0: all), opts.sheet (256), opts.texture (true: the
-- texture of the .glb on the item's sheet; default: flat colours, no sheet)
function T.import_model(name, opts)
  opts = opts or {}
  local e = model_entry(name)
  if not e then return nil, "no model " .. tostring(name) .. " in /bmm or /glb" end
  local from = opts.from or e.kind
  if from == "glb" then
    if not e.glb then return nil, "no " .. e.name .. ".glb in /glb" end
    local nm = item_name(opts.name or e.name)
    local m, err = glb3d(T.GLB_DIR .. "/" .. e.name .. ".glb",
                         { name = nm, height = opts.height or 2, faces = opts.faces or 0, sheet = opts.sheet or 256 })
    if not m then return nil, err end
    local sheet
    if opts.texture and m.textured and m.texture then
      local side = opts.sheet or 256
      if #m.texture == side * side * 4 then
        sheet = { type = 2, data = spack("<I2I2", side, side) .. m.texture }
      end
    end
    local item = G.model_item(nm, sheet and m.record or m.flat or m.record)
    item.sheet = sheet
    item.inset = 0.25
    item.file = { name = nm, author = "" }
    item.from = "glb"
    return item
  end
  if from ~= "bmm" or e.kind ~= "bmm" then return nil, "no " .. e.name .. ".bmm in /bmm" end
  local r, err = res_read(T.BMM_DIR .. "/" .. e.name .. ".bmm")
  if not r then return nil, err end
  if r.kind ~= 1 or not r.sections[SEC_MESH] then return nil, e.name .. ".bmm is not a model file" end
  local parts, inset = T.split_mesh(r.sections[SEC_MESH])
  local part
  if type(opts.model) == "number" then
    part = parts[opts.model]
  elseif opts.model then
    for _, p in ipairs(parts) do if p[1] == opts.model then part = p end end
  else
    part = parts[1]
  end
  if not part then return nil, e.name .. ".bmm has no model " .. tostring(opts.model or 1) end
  local rigs = r.sections[9] and T.split_anim(r.sections[9]) or {}
  local item = G.model_item(part[1], part[2], rigs[part[1]])
  if opts.name then item.name = item_name(opts.name) end
  if r.sections[2] then item.sheet = { type = 2, data = r.sections[2] }
  elseif r.sections[5] then item.sheet = { type = 5, data = r.sections[5] } end
  item.inset = inset
  item.file = { name = r.name, author = r.author }
  item.from = "bmm"
  return item
end

----------------------------------------------------------------- bones in bm Mesh (APP-14)

-- A model with a skeleton (it.ac, and it.vb when the skeleton fits the vertices; a skeleton with no
-- vertices yet gets it.vb at the first give, all on bone 0) gives each vertex to one bone. These functions read the bone names and heads from the ANIM part and change it.vb.
-- Bone numbers start at 0 (as in the file); lists of names start at 1 (name of bone b = names[b + 1]).

-- { { name, hx }, ... } for the bones of the skeleton (hx is the x of the head), or nil
function G.rig_bones(it)
  local ac = it.ac
  if not ac then return nil end
  local nb = sunpack("<I2", ac, 17)
  local out, pos = {}, 25
  for i = 1, nb do
    out[i] = { name = cname(ac, pos), hx = (sunpack("<f", ac, pos + 20)) }
    pos = pos + 44
  end
  return out
end

-- the name of the other side: arm_l -> arm_r, hand.R -> hand.L, Left -> Right, l_arm -> r_arm; nil
-- when the name has no side
local SIDE = { l = "r", r = "l", L = "R", R = "L" }
function G.side_name(n)
  local p, c = n:match("^(.*[%._%- ])([LlRr])$")
  if p then return p .. SIDE[c] end
  p, c = n:match("^([LlRr])([%._%- ].*)$")
  if p then return SIDE[p] .. c end
  p = n:match("^(.-)[Ll]eft$")
  if p then return p .. (n:sub(-4) == "Left" and "Right" or "right") end
  p = n:match("^(.-)[Rr]ight$")
  if p then return p .. (n:sub(-5) == "Right" and "Left" or "left") end
  return nil
end

-- the bone that mirrors bone b across X = 0: the bone with the other side's name, else b itself
function G.mirror_bone(rig, b)
  local r = rig[b + 1]
  local other = r and G.side_name(r.name)
  if other then
    for i, o in ipairs(rig) do if o.name == other then return i - 1 end end
  end
  return b
end

-- "none" (no skeleton), "skinned" (a bone for each vertex: it.vb), "unskinned" (the skeleton has no vertices
-- yet: every vertex is on bone 0 until it is given another) or "mismatch" (it has vertices that are not these)
function G.skin_state(it)
  if not it.ac then return "none" end
  if it.vb then return "skinned" end
  return sunpack("<I2", it.ac, 21) == 0 and "unskinned" or "mismatch"
end

-- the vertices of an unskinned skeleton go to bone 0 (it.vb is made); true when there is it.vb after
function G.ensure_bones(it)
  if it.vb then return true end
  if G.skin_state(it) ~= "unskinned" then return false end
  it.vb = {}
  for i = 1, #it.v do it.vb[i] = 0 end
  return true
end

-- the bone of the vertices in list: bone, count (how many different bones; bone is the first one)
function G.bone_summary(it, list)
  if not it.vb then return nil end
  local seen, n, first = {}, 0, nil
  for _, i in ipairs(list) do
    local b = it.vb[i]
    if b and not seen[b] then
      seen[b] = true
      n = n + 1
      first = first or b
    end
  end
  if n == 0 then return nil end
  return first, n
end

-- Gives the vertices in list to bone b; returns the number of vertices changed, or nil and a message.
-- With mirror on, the counterpart across X = 0 (G.mirror_pairs) takes the mirror bone (G.mirror_bone:
-- the bone with the other side's name, else the same bone). A counterpart that is chosen too takes the
-- mirror bone only when it is on the other side of the bone's head; a vertex on X = 0 takes b.
-- Changes it.vb and nothing else.
function G.set_bone(it, list, b, mirror)
  if not it.ac then return nil, "no skeleton: bm Animator makes the bones" end
  if not G.ensure_bones(it) then return nil, "the skeleton does not fit the vertices: skin it in bm Animator" end
  local rig = G.rig_bones(it)
  if b < 0 or b >= #rig then return nil, "no bone " .. b end
  local chosen = {}
  for _, i in ipairs(list) do chosen[i] = true end
  local mb = mirror and G.mirror_bone(rig, b) or b
  local mp = mirror and G.mirror_pairs(it, list) or {}
  local hx = rig[b + 1].hx
  local out = {}
  local function put(i, nb)
    out[i] = true
    it.vb[i] = nb
  end
  for _, i in ipairs(list) do
    local j = mp[i]
    local nb = b
    if j and j ~= i and chosen[j] and mb ~= b and it.v[i][1] * hx < 0 then nb = mb end
    put(i, nb)
  end
  if mirror then
    for _, i in ipairs(list) do
      local j = mp[i]
      if j and j ~= i and not chosen[j] then put(j, mb) end
    end
  end
  local n = 0
  for _ in pairs(out) do n = n + 1 end
  return n
end

----------------------------------------------------------------- the ghost reference (APP-10 step 7)

-- A ghost is a model shown as a faint wire behind the model that is edited. It is a layer of its own:
-- never chosen, never saved, never counted. It keeps only what the wire needs: the vertices, a cached
-- list of unique edges, a scale and a height offset.
--   g = { name, from = "bmm" | "glb", nv, nf (the model's), shown (faces in the wire), stride,
--         v = { {x, y, z}, ... }, ea = {}, eb = {} (edge ends), used = { vertex numbers }, scale = 1, dy = 0, on = true }
-- G.GHOST_FACES: the default budget of faces in the wire; a model with more faces keeps every stride-th face
-- (a .glb is reduced by glb3d to the budget instead). G.GHOST_DETAIL: the budgets the editor offers (the
-- last, 65535, is the format limit: all). G.GHOST_EDGES_FRAME: the most edges one frame draws; a longer list
-- is drawn 1 edge in `every`, and the window moves by one edge each frame (g.every, g.phase). The cost on
-- the Pi Zero W is not measured: see docs/progress/tools.md.
G.GHOST_FACES = 20000
G.GHOST_DETAIL = { 2000, 5000, 20000, 65535 }
G.GHOST_EDGES_FRAME = 4000
G.GHOST_SCALE_MIN, G.GHOST_SCALE_MAX, G.GHOST_DY_MAX = 0.1, 10, 20

-- the ghost of an item { v, f } (budget: faces in the wire, default G.GHOST_FACES)
function G.ghost_make(item, budget)
  budget = budget or G.GHOST_FACES
  local nf = #item.f
  local stride = nf > budget and (-(-nf // budget)) or 1
  local ea, eb, seen, used, usedset = {}, {}, {}, {}, {}
  local shown = 0
  for fi = 1, nf, stride do
    local f = item.f[fi]
    shown = shown + 1
    for k = 1, 3 do
      local a, b = f[k], f[k % 3 + 1]
      if a > b then a, b = b, a end
      local key = a * 65536 + b
      if not seen[key] then
        seen[key] = true
        ea[#ea + 1], eb[#eb + 1] = a, b
        if not usedset[a] then usedset[a] = true; used[#used + 1] = a end
        if not usedset[b] then usedset[b] = true; used[#used + 1] = b end
      end
    end
  end
  local v = {}
  for _, i in ipairs(used) do v[i] = item.v[i] end       -- the other vertices are let go
  local every = (#ea + G.GHOST_EDGES_FRAME - 1) // G.GHOST_EDGES_FRAME
  if every < 1 then every = 1 end
  return { name = item.name, from = item.from, nv = #item.v, nf = nf, shown = shown, stride = stride,
           every = every, phase = 0,
           v = v, ea = ea, eb = eb, used = used, scale = 1, dy = 0, on = true }
end

-- where vertex i of the ghost is
function G.ghost_pos(g, i)
  local p = g.v[i]
  return p[1] * g.scale, p[2] * g.scale + g.dy, p[3] * g.scale
end

-- a nudge of the scale (factor) or of the height (step): kept in the range
function G.ghost_scale(g, factor)
  g.scale = clamp(g.scale * factor, G.GHOST_SCALE_MIN, G.GHOST_SCALE_MAX)
  return g.scale
end
function G.ghost_lift(g, step)
  g.dy = clamp(g.dy + step, -G.GHOST_DY_MAX, G.GHOST_DY_MAX)
  if math.abs(g.dy) < 1e-9 then g.dy = 0 end
  return g.dy
end

-- the line the editor tells after a load
function G.ghost_text(g)
  local s = string.format("ghost %s.%s: %d vertices, %d triangles", g.name, g.from, g.nv, g.nf)
  if g.stride > 1 then
    s = s .. string.format("; the wire keeps %d triangles (1 in %d: the budget)", g.shown, g.stride)
  end
  if g.every > 1 then
    s = s .. string.format("; each frame draws 1 edge in %d (the line budget)", g.every)
  end
  return s
end

-- a ghost from /bmm or /glb: the ghost, or nil and a message. Reads only: a .glb is read by glb3d (reduced
-- to G.GHOST_FACES triangles) and NOT written to /bmm; no texture; nothing is changed on the card.
function T.ghost_load(name, budget)
  budget = budget or G.GHOST_FACES
  local item, err = T.import_model(name, { faces = budget, texture = false })
  if not item then return nil, err end
  local g = G.ghost_make(item, budget)
  g.name = model_entry(name).name                -- the file's name (the model in it may have another)
  return g
end

-- an item into /bmm/NAME.bmm (NAME: 1 to 8 of a-z 0-9 _ -): true and the path, or nil and a message.
-- The skeleton (ANIM) and the sheet go with the model when it has them (not when its vertices
-- changed since the skeleton was made). A file that is there is not changed unless opts.replace.
--   opts.title  the name shown in the file (default: the file's own, else the item's)
--   opts.author, opts.inset (sheet pixels), opts.replace
function T.write_bmm(item, name, opts)
  opts = opts or {}
  local stem = bmm_stem(name)
  if not stem then return nil, tostring(name) .. ": a model file name is 1 to 8 of a-z 0-9 _ -" end
  local nv, nf = #item.v, #item.f
  if nv == 0 or nf == 0 then return nil, "the model is empty" end
  if nv > T.LIMIT_V or nf > T.LIMIT_T then
    return nil, nv .. " vertices and " .. nf .. " triangles: the format holds " .. T.LIMIT_V .. " of each"
  end
  local inset = opts.inset or item.inset or 0.25
  local mesh = spack("<I2I2I4", 1, clamp(round(inset * 256), 0, 65535), 0) .. G.encode_model(item)
  local sections = { [SEC_MESH] = mesh }
  if item.ac then
    local _, _, anv = sunpack("<I2I2I2", item.ac, 17)
    if anv == nv then
      sections[SEC_ANIM] = spack("<I2I2I4", 1, 0, 0) .. name16(item.name) .. item.ac:sub(17)
    end
  end
  if item.sheet then sections[item.sheet.type] = item.sheet.data end
  local file = item.file or {}
  local path = T.BMM_DIR .. "/" .. stem .. ".bmm"
  local ok, err = res_write(path, 1, { sections = sections, name = opts.title or file.name or item.name,
                                       author = opts.author or file.author or "", replace = opts.replace })
  if not ok then return nil, err end
  return true, path
end

-- /glb/NAME.glb to /bmm/TO.bmm: the .glb is read (glb3d), never changed; opts as import_model and
-- write_bmm, and opts.to (the .bmm name; default: NAME, 8 characters at most). A .bmm that is there
-- stays unless opts.replace. Returns true, the path and the item, or nil and a message.
function T.import_glb_to_bmm(name, opts)
  opts = opts or {}
  local e = model_entry(name)
  if not e or not e.glb then return nil, "no " .. tostring(name) .. ".glb in /glb" end
  local to = bmm_stem(opts.to or e.name)
  if not to then
    return nil, e.name .. ".glb: a .bmm name is 1 to 8 of a-z 0-9 _ - (give opts.to)"
  end
  if not opts.replace then
    for _, f in ipairs(T.model_files()) do
      if f.name == to and f.kind == "bmm" then
        return nil, T.BMM_DIR .. "/" .. to .. ".bmm: is there already (replace = true changes it)"
      end
    end
  end
  local o = {}
  for k, v in pairs(opts) do o[k] = v end
  o.from = "glb"
  local item, err = T.import_model(e.name, o)
  if not item then return nil, err end
  local ok, path = T.write_bmm(item, to, o)
  if not ok then return nil, path end
  return true, path, item
end

function T.decode_rig(ac)
  local nb, nc, nv = sunpack("<I2I2I2", ac, 17)
  local pos = 25
  local bones = {}
  for i = 1, nb do
    local parent, _, hx, hy, hz, tx, ty, tz = sunpack("<i2I2ffffff", ac, pos + 16)
    bones[i] = { name = cname(ac, pos), parent = parent + 1, head = { hx, hy, hz }, tail = { tx, ty, tz } }
    pos = pos + 44
  end
  local vb = {}
  for i = 1, nv do vb[i] = ac:byte(pos + i - 1) + 1 end
  pos = pos + ((nv + 3) & ~3)
  local clips = {}
  for c = 1, nc do
    local nk, mode, flags, length = sunpack("<I2BBf", ac, pos + 16)
    local clip = { name = cname(ac, pos), mode = mode, loop = flags & 1 == 1, length = length, keys = {} }
    pos = pos + 24
    for k = 1, nk do
      local key = { t = sunpack("<f", ac, pos), pose = {} }
      pos = pos + 4
      for i = 1, nb do
        local qx, qy, qz, qw, mx, my, mz = sunpack("<fffffff", ac, pos)
        key.pose[i] = { q = { qx, qy, qz, qw }, t = { mx, my, mz } }
        pos = pos + 28
      end
      clip.keys[k] = key
    end
    clips[c] = clip
  end
  return { bones = bones, clips = clips }, vb, nv
end

-- Animations from another model (APP-27). T.retarget(src_rig, dst_rig): the clips of src_rig (as
-- decode_rig gives them) for the bones of dst_rig. A bone matches by its name, else by its name in lower case
-- with . _ - taken out. The rotation q of a matched bone is copied; its translation t is scaled by the ratio
-- of the heights of the two rigs (the y span of heads and tails); a bone with no match gets the rest pose.
-- Returns the new clips (the times, length, loop and mode kept) and { matched = bones of dst_rig that
-- have a match, missing = their names, clips = #clips }. Nothing in the two rigs changes.
function T.rig_height(rig)
  local lo, hi
  for _, b in ipairs(rig.bones) do
    for _, p in ipairs({ b.head, b.tail }) do
      if not lo or p[2] < lo then lo = p[2] end
      if not hi or p[2] > hi then hi = p[2] end
    end
  end
  return lo and hi - lo or 0
end

function T.retarget(src, dst)
  local exact, loose = {}, {}
  for i, b in ipairs(src.bones) do
    if not exact[b.name] then exact[b.name] = i end
    local n = b.name:lower():gsub("[%._%-]", "")
    if not loose[n] then loose[n] = i end
  end
  local map, missing, matched = {}, {}, 0
  for i, b in ipairs(dst.bones) do
    local j = exact[b.name] or loose[(b.name:lower():gsub("[%._%-]", ""))]
    map[i] = j or false
    if j then matched = matched + 1 else missing[#missing + 1] = b.name end
  end
  local hs, hd = T.rig_height(src), T.rig_height(dst)
  local ratio = (hs > 1e-6 and hd > 1e-6) and hd / hs or 1
  local clips = {}
  for c, clip in ipairs(src.clips) do
    local o = { name = clip.name, mode = clip.mode, loop = clip.loop, length = clip.length, keys = {} }
    for k, key in ipairs(clip.keys) do
      local pose = {}
      for i = 1, #dst.bones do
        local p = map[i] and key.pose[map[i]]
        if p then
          pose[i] = { q = { p.q[1], p.q[2], p.q[3], p.q[4] }, t = { p.t[1] * ratio, p.t[2] * ratio, p.t[3] * ratio } }
        else
          pose[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
        end
      end
      o.keys[k] = { t = key.t, pose = pose }
    end
    clips[c] = o
  end
  return clips, { matched = matched, missing = missing, clips = #clips }
end

-- Two-bone IK (APP-29), for the editors only: the runtime, the clip format and the kernel do not change; the
-- result is ordinary rotation keys. T.ik2(root, len1, len2, target, pole [, rest1, rest2]) puts a chain of two
-- segments (upper from root, lower after it) on a target. Returns { mid, tip, d1, d2, q1, q2, reached }: the
-- joint and the end, the unit directions of the two segments, and the rotations that turn rest1 / rest2
-- (unit vectors, default +y) onto d1 / d2 by the shortest way. The lengths never change: a target out of
-- reach (too far, or nearer than |len1 - len2|) gives a straight (or fully folded) chain that points at it
-- and reached = false. The joint leans toward `pole` (a direction; the part of it across the root-target line).
function T.ik2(root, len1, len2, target, pole, rest1, rest2)
  local to = V.sub(target, root)
  local d = sqrt(V.dot(to, to))
  pole = pole or { 0, 0, 1 }
  local axis
  if d > 1e-9 then axis = V.scale(to, 1 / d) else axis = V.norm(pole) end
  if V.dot(axis, axis) < 0.5 then axis = { 0, 1, 0 } end
  local reach, dmin = len1 + len2, abs(len1 - len2)
  local reached = d <= reach + 1e-9 and d >= dmin - 1e-9
  local dd = d
  if dd > reach then dd = reach end
  if dd < dmin then dd = dmin end
  if dd < 1e-6 * max(reach, 1e-9) then dd = 1e-6 * max(reach, 1e-9) end
  -- the bend: the pole across the axis
  local bend = V.sub(pole, V.scale(axis, V.dot(pole, axis)))
  if V.dot(bend, bend) < 1e-10 then
    local least = abs(axis[1]) <= abs(axis[2]) and (abs(axis[1]) <= abs(axis[3]) and 1 or 3) or (abs(axis[2]) <= abs(axis[3]) and 2 or 3)
    bend = V.cross(axis, V.unit(least))
  end
  bend = V.norm(bend)
  local a = (len1 * len1 - len2 * len2 + dd * dd) / (2 * dd)
  local h = sqrt(max(0, len1 * len1 - a * a))
  local mid = V.add(V.add(root, V.scale(axis, a)), V.scale(bend, h))
  local tip = V.add(root, V.scale(axis, dd))
  local d1, d2 = V.norm(V.sub(mid, root)), V.norm(V.sub(tip, mid))
  if V.dot(d1, d1) < 0.5 then d1 = axis end
  if V.dot(d2, d2) < 0.5 then d2 = axis end
  return { mid = mid, tip = tip, d1 = d1, d2 = d2, reached = reached,
           q1 = Q.arc(rest1 or { 0, 1, 0 }, d1), q2 = Q.arc(rest2 or { 0, 1, 0 }, d2) }
end

-- The pose of a rig in the model's space, as the kernel skins it (M = M_parent * T(head + t) * R(q) * T(-head)):
-- { rot[i] = the bone's rotation in the world, head[i], tail[i] = where they are }. `pose` is { {q =, t =}, ... }.
function T.fk(rig, pose)
  local rot, tr, head, tail = {}, {}, {}, {}
  local ident = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
  for i, b in ipairs(rig.bones) do
    local p = pose[i] or ident
    local q = Q.norm(p.q)
    local h = b.head
    local lt = V.sub(V.add(h, p.t), Q.rotate(q, h))
    local par = b.parent
    if par and par > 0 and par < i then
      rot[i] = Q.mul(rot[par], q)
      tr[i] = V.add(Q.rotate(rot[par], lt), tr[par])
    else
      rot[i], tr[i] = q, lt
    end
    head[i] = V.add(Q.rotate(rot[i], h), tr[i])
    tail[i] = V.add(Q.rotate(rot[i], b.tail), tr[i])
  end
  return { rot = rot, head = head, tail = tail }
end

-- The direction the joint of the chain g (upper) > p (lower) > b (end) leans to in the rest pose, in the space
-- of the parent of g: its offset from the line head(g) - head(b). A straight rest limb leans as the pose
-- `pose` does, else as the end bone points across the limb (toes), else toward +z.
function T.ik_pole(rig, g, p, b, pose)
  local function lean(hg, hp, hb)
    local ax = V.sub(hb, hg)
    local l2 = V.dot(ax, ax)
    if l2 < 1e-12 then return nil end
    local off = V.sub(hp, hg)
    off = V.sub(off, V.scale(ax, V.dot(off, ax) / l2))
    if V.dot(off, off) < 1e-8 then return nil end
    return V.norm(off)
  end
  local B = rig.bones
  local rest = lean(B[g].head, B[p].head, B[b].head)
  if rest then return rest end
  if pose then
    local fk = T.fk(rig, pose)
    local w = lean(fk.head[g], fk.head[p], fk.head[b])
    if w then
      local pg = B[g].parent
      if pg and pg > 0 then w = Q.rotate(Q.conj(fk.rot[pg]), w) end
      return w
    end
  end
  -- a limb straight at rest and in the pose: the knee leans where the end bone points across the limb (the
  -- toes of a foot), else toward +z (an elbow of a model that faces -z)
  local ax = V.sub(B[b].head, B[g].head)
  local l2 = V.dot(ax, ax)
  if l2 > 1e-12 then
    local d = V.sub(B[b].tail, B[b].head)
    d = V.sub(d, V.scale(ax, V.dot(d, ax) / l2))
    if V.dot(d, d) > 0.0025 then return V.norm(d) end
  end
  return { 0, 0, 1 }
end

-- Puts the end bone b of the chain g > p > b (each the parent of the next) with its head on `target` (the
-- model's space) by turning g and p in `pose`; b keeps its rotation in the world. `pole` is as T.ik_pole gives
-- it. Returns a new pose (the old one stays) and { reached, eff = where the head of b is now }.
function T.ik_chain(rig, pose, g, p, b, target, pole)
  local fk0 = T.fk(rig, pose)
  local root, mid, tip = fk0.head[g], fk0.head[p], fk0.head[b]
  local len1, len2 = sqrt(V.dist2(mid, root)), sqrt(V.dist2(tip, mid))
  local out = {}
  for i, bp in ipairs(pose) do out[i] = { q = V.copy(bp.q), t = V.copy(bp.t) }; out[i].q[4] = bp.q[4] end
  if len1 < 1e-6 or len2 < 1e-6 then return out, { reached = false, eff = V.copy(tip) } end
  local pg = rig.bones[g].parent
  local par = (pg and pg > 0) and fk0.rot[pg] or { 0, 0, 0, 1 }
  local res = T.ik2(root, len1, len2, target, Q.rotate(par, pole or { 0, 0, 1 }))
  local rg = Q.mul(Q.arc(V.norm(V.sub(mid, root)), res.d1), fk0.rot[g])
  out[g].q = Q.norm(Q.mul(Q.conj(par), rg))
  local fk1 = T.fk(rig, out)
  local rp = Q.mul(Q.arc(V.norm(V.sub(fk1.head[b], fk1.head[p])), res.d2), fk1.rot[p])
  out[p].q = Q.norm(Q.mul(Q.conj(fk1.rot[g]), rp))
  local fk2 = T.fk(rig, out)
  out[b].q = Q.norm(Q.mul(Q.conj(fk2.rot[p]), fk0.rot[b]))
  return out, { reached = res.reached, eff = T.fk(rig, out).head[b] }
end

-- back from triangles: the two halves of each tile become a tile again
-- (as bm Studio's meshToFaces)
function T.decode_model(mc, ac, bones)
  local nv, nf = sunpack("<I2I2", mc, 17)
  local pos = 25
  local verts = {}
  for i = 1, nv do
    local x, y, z = sunpack("<fff", mc, pos)
    verts[i] = { x, y, z }
    pos = pos + 12
  end
  local tris = {}
  for i = 1, nf do
    local a, b, c, _, col, u0, v0, u1, v1, u2, v2 = sunpack("<I2I2I2I2I4I2I2I2I2I2I2", mc, pos)
    tris[i] = { a + 1, b + 1, c + 1, col, u0 / 8, v0 / 8, u1 / 8, v1 / 8, u2 / 8, v2 / 8 }
    pos = pos + 24
  end
  local rig, vb
  if ac then
    local r, b, n = T.decode_rig(ac)
    if n == nv then rig, vb = r, b end
  end
  if bones and #bones == nv then vb = bones end   -- the bones given apart (reduce_model)
  local function tri(t)
    local f = { p = { V.copy(verts[t[1]]), V.copy(verts[t[2]]), V.copy(verts[t[3]]) } }
    if t[4] & TEXTURED ~= 0 then
      f.uv = { { t[5], t[6] }, { t[7], t[8] }, { t[9], t[10] } }
    else
      f.c = t[4] & 0xFFFFFF
    end
    if vb then f.b = { vb[t[1]], vb[t[2]], vb[t[3]] } end
    return f
  end
  local faces, i = {}, 1
  while i <= nf do
    local t, n = tris[i], tris[i + 1]
    local a = tri(t)
    local merged = false
    if n and n[1] == t[1] and n[2] == t[3] and n[4] == t[4] and
       (t[4] & TEXTURED == 0 or (n[5] == t[5] and n[6] == t[6] and n[7] == t[9] and n[8] == t[10])) then
      local b = tri(n)
      if V.dot(T.face_normal(a), T.face_normal(b)) > 0 then
        a.p[4] = b.p[3]
        if a.uv then a.uv[4] = b.uv[3] end
        if a.b then a.b[4] = b.b[3] end
        merged = true
      end
    end
    faces[#faces + 1] = a
    i = i + (merged and 2 or 1)
  end
  return faces, rig
end

-- fewer triangles for a model (the kernel's mesh_reduce, src/bm/decimate.c):
-- its faces become the reduced triangles, the skeleton stays and the bones
-- follow the vertices. Returns the number of triangles, or false and why.
function T.reduce_model(m, target)
  if not mesh_reduce then return false, "this kernel has no mesh_reduce" end
  if m.dirty or not m.mc then
    local ok, e = T.encode_mesh(m)
    if not ok then return false, e end
  end
  if not m.mc then return false, "no faces" end
  local mc, vb, nt = mesh_reduce(m.mc, target, m.rig and m.vb or nil)
  if not mc then return false, vb end
  local bones
  if vb then
    bones = {}
    for i = 1, #vb do bones[i] = vb:byte(i) + 1 end
  end
  m.faces = T.decode_model(mc, nil, bones)
  m.dirty = true
  return nt
end

----------------------------------------------------------------- the project

-- S: the state the programs share; S.models = { {name, faces, rig, mc, ac,
-- vb, nv, nt, dirty, adirty} }, S.cur the model being worked on, S.view
-- the kernel's mesh of it (model())
local S = {
  proj = { title = "New 3D project", author = "", res = "640x360" },
  models = {}, cur = 1, inset = 0.25, view = nil, dirty = false, sheet_dirty = false,
  page = "menu", last_page = nil, frame = 0, undo = {}, redo = {},
  msg = nil, msg_c = C.TEXT, msg_t = 0,
}
T.S = S
local A = nil                -- the program (bm3d.run)

function T.say(s, c, t) S.msg, S.msg_c, S.msg_t = s, c or C.TEXT, t or 200 end
local say = T.say
function T.M() return S.models[S.cur] end
function T.rig() local m = S.models[S.cur]; return m and m.rig end
local M = T.M

function T.unique_name(base, skip)
  base = base:sub(1, 16)
  local function used(n)
    for i, m in ipairs(S.models) do if m.name == n and i ~= skip then return true end end
    return false
  end
  if not used(base) then return base end
  for k = 2, 999 do
    local s = tostring(k)
    local n = base:sub(1, 16 - #s) .. s
    if not used(n) then return n end
  end
  return base
end

-- the sections, from the models; the kernel checks them and draws the
-- current model from them
function T.sync()
  local mparts, aparts = {}, {}
  for _, m in ipairs(S.models) do
    if m.dirty then
      local ok, e = T.encode_mesh(m)
      if not ok then return false, m.name .. ": " .. e end
      T.encode_anim(m)
      m.dirty = false
    elseif m.adirty then
      T.encode_anim(m)
    end
    m.adirty = false
    if m.mc then
      mparts[#mparts + 1] = m.mc
      if m.ac then aparts[#aparts + 1] = m.ac end
    end
  end
  local mesh = #mparts > 0 and spack("<I2I2I4", #mparts, clamp(round(S.inset * 256), 0, 65535), 0) .. table.concat(mparts) or nil
  local anim = #aparts > 0 and spack("<I2I2I4", #aparts, 0, 0) .. table.concat(aparts) or nil
  local ok, e = cart_data(SEC_MESH, mesh)
  if ok then ok, e = cart_data(SEC_ANIM, anim) end
  if not ok then return false, e end
  S.view = M() and M().mc and model(M().name) or nil
  return true
end

function T.select_model(i)
  if #S.models == 0 then S.cur = 1; S.view = nil; return end
  S.cur = (i - 1) % #S.models + 1
  S.view = M().mc and model(M().name) or nil
end

function T.new_model(name)
  S.models[#S.models + 1] = { name = T.unique_name(name or "model"), faces = {}, dirty = true }
  return #S.models
end

function T.deep(t)
  if type(t) ~= "table" then return t end
  local o = {}
  for k, v in pairs(t) do o[k] = T.deep(v) end
  return o
end

----------------------------------------------------------------- undo

-- a snapshot is the model's own bytes (already made for the kernel), so
-- taking one costs nothing; the faces come back by decoding them. A sheet
-- edit is the list of the pixels it changed, as they were: { px = {x, y, c, ...} }.
local function snapshot()
  local m = M()
  if m.dirty or m.adirty then T.sync() end
  return { idx = S.cur, name = m.name, mc = m.mc, ac = m.ac, rig = not m.mc and T.deep(m.rig) or nil }
end

local function restore(s)
  local m = S.models[s.idx]
  if not m or m.name ~= s.name then return false end
  if s.mc then
    m.faces, m.rig = T.decode_model(s.mc, s.ac)
  else
    m.faces, m.rig = {}, T.deep(s.rig)
  end
  m.dirty = true
  S.cur = s.idx
  return true
end

local function push(stack, s)
  stack[#stack + 1] = s
  if #stack > 40 then table.remove(stack, 1) end
end

function T.begin_edit()
  push(S.undo, snapshot())
  S.redo = {}
end

-- a sheet edit: the pixels as they were, x, y, colour (false: clear)
function T.sheet_undo(px)
  push(S.undo, { px = px })
  S.redo = {}
end

-- after an edit: the kernel gets the new sections; too big: undone
function T.commit()
  M().dirty = true
  local ok, e = T.sync()
  if not ok then
    local s = table.remove(S.undo)
    if s and not s.px then restore(s); T.sync() end
    say("cannot: " .. tostring(e), C.ERR)
    return false
  end
  S.dirty = true
  return true
end

-- the same for the skeleton and animations only (the faces stay)
function T.commit_anim()
  M().adirty = true
  local ok, e = T.sync()
  if not ok then say("cannot: " .. tostring(e), C.ERR); return false end
  S.dirty = true
  return true
end

local function flip_pixels(s)
  local back = {}
  for i = 1, #s.px, 3 do
    local x, y, c = s.px[i], s.px[i + 1], s.px[i + 2]
    back[#back + 1], back[#back + 2], back[#back + 3] = x, y, sget(x, y) or false
    sset(x, y, c or nil)
  end
  S.sheet_dirty = true
  return { px = back }
end

function T.do_undo(stack, other, what)
  local s = table.remove(stack)
  if not s then say("nothing to " .. what, C.DIM); return end
  if s.px then
    push(other, flip_pixels(s))
    S.dirty = true
    say(what .. ": the sheet", C.ACC, 90)
    return
  end
  if not S.models[s.idx] or S.models[s.idx].name ~= s.name then say("cannot " .. what .. ": the model changed", C.ERR); return end
  S.cur = s.idx
  push(other, snapshot())
  restore(s)
  T.sync()
  S.dirty = true
  say(what .. ": " .. M().name, C.ACC, 90)
end

----------------------------------------------------------------- files

-- a project (.bme) is what the tools change; a game (.bm, .b16) is read
-- only: saving it makes its editable copy (the console asks first)
function T.is_game(path)
  local p = path and path:lower() or ""
  return p:match("%.bm$") ~= nil or p:match("%.b16$") ~= nil
end

-- "/bme/my 3d.bme" -> "/bme/my3d.bme" (8.3); no folder: /bme; an old project keeps its folder
function T.short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or U.PROJECTS
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):lower():gsub("[^%w_]", "")
  if stem == "" then stem = "game" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".bme"
end

function T.list_files()
  local out = U.project_files({ bm = true, bme = true })
  table.sort(out)
  return out
end

local reset_pages

function T.load_project(path)
  local p, e = cart_load(path)
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C.ERR); return false end
  S.proj = { title = p.title, author = p.author, res = p.res, path = path,
             sheet_w = p.sheet_w, sheet_h = p.sheet_h, palette = p.palette }
  local parts, ins = T.split_mesh(cart_data(SEC_MESH))
  local rigs = T.split_anim(cart_data(SEC_ANIM))
  S.inset = ins
  S.models = {}
  for _, part in ipairs(parts) do
    local name, mc = part[1], part[2]
    local faces, r = T.decode_model(mc, rigs[name])
    local m = { name = name, faces = faces, rig = r, mc = mc, ac = r and rigs[name] or nil, dirty = false }
    m.nv, m.nt = sunpack("<I2I2", mc, 17)
    if r then
      local vb = {}
      for i = 1, m.nv do vb[i] = string.char(rigs[name]:byte(25 + #r.bones * 44 + i - 1)) end
      m.vb = table.concat(vb)
    end
    S.models[#S.models + 1] = m
  end
  S.undo, S.redo = {}, {}
  S.dirty, S.sheet_dirty = false, false
  if #S.models == 0 and A.empty_model then T.new_model("model") end
  local ok, err = T.sync()
  if not ok then say("broken models: " .. tostring(err), C.ERR) end
  T.select_model(1)
  reset_pages()
  say("opened " .. path .. "  (" .. #parts .. " models" .. (T.is_game(path) and "; a game: saving makes its copy" or "") .. ")", C.ACC)
  return true
end

-- a project not saved yet (bm Studio's New project)
function T.new_project(title)
  S.proj = { title = title or "New 3D project", author = "", res = "640x360" }
  S.models, S.undo, S.redo, S.inset = {}, {}, {}, 0.25
  T.new_model("model")
  T.sync()
  T.select_model(1)
  reset_pages()
  S.dirty, S.sheet_dirty = false, false
end

-- writes the models (and the sheet, if it was painted) into `path`: with
-- cart_write the rest of the file stays as it was. A project never saved
-- becomes a new cartridge with the program's code (A.viewer) and the sheet.
function T.save_to(path, from)
  local ok, e = T.sync()
  if not ok then say("cannot save: " .. tostring(e), C.ERR); return false end
  local new = not S.proj.path
  local t = { sections = { [SEC_MESH] = cart_data(SEC_MESH) or false, [SEC_ANIM] = cart_data(SEC_ANIM) or false },
              title = S.proj.title, author = S.proj.author }
  if new then
    t.lua, t.from, t.res = A.viewer, false, S.proj.res
  elseif from then
    t.from = from
  end
  if new or S.sheet_dirty then t.sheet, t.palette = true, S.proj.palette end
  ok, e = cart_write(path, t)
  if not ok then say("save failed: " .. tostring(e), C.ERR); return false end
  if type(e) == "string" then path = e end      -- the editable copy of a game, or a new project
  S.proj.path = path
  S.dirty, S.sheet_dirty = false, false
  say(U.saved_text(path), C.ACC)
  return true
end

local ask

function T.save_as(after)
  local p = S.proj.path
  ask("file name (8.3, in /bme)", p and T.short_path(p):match("([^/]+)$") or "my3d.bme", function(t)
    if t == "" then return end
    if T.save_to(U.project_path(t), p) and after then after() end
  end)
end

function T.save_project(after)
  if S.proj.path then
    if T.save_to(S.proj.path) and after then after() end
  else
    T.save_as(after)
  end
end

-- tries the game: saved first, then cart_run; the program comes back to
-- the same page (saved())
function T.run_project()
  local function go_run()
    save({ page = S.last_page, model = S.cur })
    cart_run(S.proj.path)
  end
  if not S.proj.path or S.dirty then T.save_project(go_run) else go_run() end
end

-- the other program on the same file (bm Studio <-> bm Animator)
function T.open_in(tool, label)
  local function go_tool()
    save({ page = S.last_page, model = S.cur })
    cart_tool(tool, S.proj.path)
  end
  if not S.proj.path or S.dirty then
    T.save_project(go_tool)
    if not S.proj.path then say("give the project a name to open it in " .. label, C.ACC) end
  else
    go_tool()
  end
end

----------------------------------------------------------------- input

T.held, T.rp, T.tap = {}, {}, {}
local held, rp, tap = T.held, T.rp, T.tap
local xy_down, xy_combo = {}, {}
local function read_pad()
  for i = 0, 7 do
    if btn(i) then held[i] = (held[i] or 0) + 1 else held[i] = 0 end
    local h = held[i]
    rp[i] = h == 1 or (h > 14 and h % 4 == 0)
  end
  -- X and Y: a tap is a press and a release with no other button between
  for _, b in ipairs({ 6, 7 }) do
    tap[b] = false
    if btn(b) then
      if not xy_down[b] then xy_down[b], xy_combo[b] = true, false end
      for i = 0, 7 do if i ~= b and btn(i) then xy_combo[b] = true end end
    elseif xy_down[b] then
      xy_down[b] = false
      tap[b] = not xy_combo[b]
    end
  end
end

-- Alt held on a keyboard (the camera keys)
function T.alt() return keydown(0xE2) or keydown(0xE6) end

----------------------------------------------------------------- 3D helpers

-- an orbit camera: target, yaw, pitch, distance; the target shows `dx`
-- pixels right of the middle of the screen and `dy` below it (the panels,
-- the text at the top)
function T.look(cam, dx, dy)
  local cp, sp = cos(cam.pitch), sin(cam.pitch)
  local sy, cy = sin(cam.yaw), cos(cam.yaw)
  local f = { cp * sy, sp, cp * cy }
  local r = { cy, 0, -sy }
  local u = { -sp * sy, cp, -sp * cy }
  local d = cam.dist
  local a, b = -(dx or 0) * d / T.FOCAL, (dy or 0) * d / T.FOCAL
  local eye = { cam.tx - f[1] * d + r[1] * a + u[1] * b, cam.ty - f[2] * d + u[2] * b,
                cam.tz - f[3] * d + r[3] * a + u[3] * b }
  camera3d(eye[1], eye[2], eye[3], cam.yaw, cam.pitch, 60)
  cam.f, cam.r, cam.u, cam.eye = f, r, u, eye
end

-- the ray of a pixel of the screen from the camera (as last looked):
-- origin and direction, nil before the first look
function T.ray(cam, px, py)
  if not cam.eye then return nil end
  local a, b = (px - W / 2) / T.FOCAL, (H / 2 - py) / T.FOCAL
  local f, r, u = cam.f, cam.r, cam.u
  return cam.eye, { f[1] + r[1] * a + u[1] * b, f[2] + r[2] * a + u[2] * b, f[3] + r[3] * a + u[3] * b }
end

-- where that ray meets the plane y = level (nil if it does not)
function T.ray_level(cam, px, py, level)
  local o, d = T.ray(cam, px, py)
  if not o or abs(d[2]) < 1e-6 then return nil end
  local t = (level - o[2]) / d[2]
  if t <= 0 then return nil end
  return { o[1] + d[1] * t, level, o[3] + d[3] * t }
end

-- the three axes as the camera sees them, at (x, y) of the screen (bm Mesh)
function T.gizmo(cam, x, y)
  if not cam.r then return end
  local cols, names = { 0xFF6060, 0x60E060, 0x6090FF }, { "x", "y", "z" }
  for k = 1, 3 do
    local dx, dy = cam.r[k], cam.u[k]
    line(x, y, x + dx * 18, y - dy * 18, cols[k])
    print(names[k], x + dx * 24 - 3, y - dy * 24 - 6, cols[k])
  end
end

function T.scr(p)
  local x, y, z = project3d(p[1], p[2], p[3])
  if x and abs(x) < 4000 and abs(y) < 4000 then return x, y, z end
end
local scr = T.scr

function T.seg(a, b, c)
  local ax, ay = scr(a)
  local bx, by = scr(b)
  if ax and bx then line(ax, ay, bx, by, c) end
end
local seg = T.seg

-- the edges of a face, and a dot in its middle (as bm Mesh)
function T.outline(f, c, dot)
  local p = f.p
  for i = 1, #p do seg(p[i], p[i % #p + 1], c) end
  if dot then
    local x, y = scr(T.face_center(f))
    if x then rectfill(x - 1, y - 1, 3, 3, c) end
  end
end

-- the world axis nearest to where the camera looks, and the one to its
-- right (never the same: at 45 degrees one is x, the other z)
function T.view_axes(yaw)
  local fx, fz = sin(yaw), cos(yaw)
  local fa, fs, ra, rs
  if abs(fx) > abs(fz) + 1e-6 then
    fa, fs = 1, fx > 0 and 1 or -1
    ra, rs = 3, -fs
  else
    fa, fs = 3, fz > 0 and 1 or -1
    ra, rs = 1, fs
  end
  return fa, fs, ra, rs
end

-- Block shapes of bm Studio (APP-12). T.BLOCK_KINDS names them: 1 cube, 2 ramp, 3 corner, 4 rounded.
-- T.block_shape(kind, c, fa, fs, ra, rs): the faces ({ p = corners }, 3 or 4 each, looking out) of a
-- block in the cell c ({x, y, z} the lowest corner). The ramp rises towards "far": axis fa (1 or 3), sign
-- fs; "right" is axis ra, sign rs (T.view_axes). Ramp: a slope from the near floor edge to the far top
-- edge, a far wall, 2 triangular sides. Corner: a pyramid on the cell floor whose top is the far right top
-- corner (5 faces; the sloped sides meet a ramp's slope). Rounded: an octagon prism (the 4 vertical edges
-- cut 0.3 deep), 14 faces. The cube is not here: bm Studio builds it with block_faces.
T.BLOCK_KINDS = { "cube", "ramp", "corner", "rounded" }
function T.block_shape(kind, c, fa, fs, ra, rs)
  local function at(lr, lf, h)
    local p = {}
    p[ra] = c[ra] + (rs > 0 and lr or 1 - lr)
    p[fa] = c[fa] + (fs > 0 and lf or 1 - lf)
    p[2] = c[2] + h
    return p
  end
  local faces = {}
  if kind == 2 then
    local nbl, nbr, fbl, fbr = at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(1, 1, 0)
    local ftl, ftr = at(0, 1, 1), at(1, 1, 1)
    faces = { { p = { nbl, nbr, fbr, fbl } }, { p = { fbl, fbr, ftr, ftl } }, { p = { nbl, nbr, ftr, ftl } },
              { p = { nbl, fbl, ftl } }, { p = { nbr, fbr, ftr } } }
  elseif kind == 3 then
    local nbl, nbr, fbl, fbr, ftr = at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(1, 1, 0), at(1, 1, 1)
    faces = { { p = { nbl, nbr, fbr, fbl } }, { p = { fbl, fbr, ftr } }, { p = { nbr, fbr, ftr } },
              { p = { nbl, nbr, ftr } }, { p = { nbl, ftr, fbl } } }
  elseif kind == 4 then
    local o = { { 0.3, 0 }, { 0.7, 0 }, { 1, 0.3 }, { 1, 0.7 }, { 0.7, 1 }, { 0.3, 1 }, { 0, 0.7 }, { 0, 0.3 } }
    local lo, hi = {}, {}
    for i, q in ipairs(o) do lo[i], hi[i] = at(q[1], q[2], 0), at(q[1], q[2], 1) end
    for i = 1, 8 do
      local j = i % 8 + 1
      faces[#faces + 1] = { p = { lo[i], lo[j], hi[j], hi[i] } }
    end
    for _, r in ipairs({ lo, hi }) do
      faces[#faces + 1] = { p = { r[1], r[2], r[3], r[4] } }
      faces[#faces + 1] = { p = { r[1], r[4], r[5], r[6] } }
      faces[#faces + 1] = { p = { r[1], r[6], r[7], r[8] } }
    end
  else
    return nil
  end
  -- every face looks away from the middle of the corners (all three shapes are convex)
  local mid, seen, nm = { 0, 0, 0 }, {}, 0
  for _, f in ipairs(faces) do
    for _, q in ipairs(f.p) do
      local key = q[1] .. "," .. q[2] .. "," .. q[3]
      if not seen[key] then
        seen[key] = true
        nm = nm + 1
        mid[1], mid[2], mid[3] = mid[1] + q[1], mid[2] + q[2], mid[3] + q[3]
      end
    end
  end
  mid[1], mid[2], mid[3] = mid[1] / nm, mid[2] / nm, mid[3] / nm
  for _, f in ipairs(faces) do
    local n, m = T.face_normal(f), T.face_center(f)
    if n[1] * (m[1] - mid[1]) + n[2] * (m[2] - mid[2]) + n[3] * (m[3] - mid[3]) < 0 then
      local r = {}
      for i = #f.p, 1, -1 do r[#r + 1] = f.p[i] end
      f.p = r
    end
    for i, q in ipairs(f.p) do f.p[i] = { q[1], q[2], q[3] } end    -- no corner shared between faces
  end
  return faces
end

-- the camera from the keyboard and the pad, the same on every page: Alt +
-- arrows (or X + the pad) turn and tilt, + - zoom (X + A / B)
function T.cam_key(cam, k, lo, hi)
  if k == "+" or k == "=" then cam.dist = max(lo or 0.3, cam.dist * 0.85); return true end
  if k == "-" then cam.dist = min(hi or 200, cam.dist / 0.85); return true end
  if T.alt() then
    if k == "left" then cam.yaw = cam.yaw - pi / 16; return true end
    if k == "right" then cam.yaw = cam.yaw + pi / 16; return true end
    if k == "up" then cam.pitch = clamp(cam.pitch - 0.1, -1.45, 1.2); return true end
    if k == "down" then cam.pitch = clamp(cam.pitch + 0.1, -1.45, 1.2); return true end
  end
  return false
end

-- the camera with the mouse, in the view (x, y, w, h): the right button
-- dragged (or the left one, with o.left) turns and tilts, the middle one
-- moves the target (not with o.no_pan), the wheel zooms. o.yaw(d): the
-- page turns its own way (bm Studio's build page eases its yaw). True
-- when the mouse moved the camera.
function T.mouse_cam(cam, x, y, w, h, o)
  o = o or {}
  if not U.on then return false end
  local d = U.drag
  local used = false
  if d and d.x0 >= x and d.x0 < x + w and d.y0 >= y and d.y0 < y + h then
    if d.b == 1 or (d.b == 0 and o.left) then
      if o.yaw then o.yaw(-d.dx * 0.01) else cam.yaw = cam.yaw - d.dx * 0.01 end
      cam.pitch = clamp(cam.pitch - d.dy * 0.01, o.pmin or -1.45, o.pmax or 1.2)
      used = true
    elseif d.b == 2 and not o.no_pan and cam.r then
      local k = cam.dist / T.FOCAL
      cam.tx = cam.tx - (cam.r[1] * d.dx - cam.u[1] * d.dy) * k
      cam.ty = cam.ty + cam.u[2] * d.dy * k
      cam.tz = cam.tz - (cam.r[3] * d.dx - cam.u[3] * d.dy) * k
      used = true
    end
  end
  if U.wheel ~= 0 and U.inside(x, y, w, h) then
    cam.dist = clamp(cam.dist * 0.85 ^ U.wheel, o.lo or 0.3, o.hi or 200)
    used = true
  end
  return used
end

-- the face under the pointer: the index of the nearest face whose corners,
-- as the camera sees them, hold (px, py); front faces first
function T.face_at(faces, px, py)
  local best, bz, back, bbz
  for i, f in ipairs(faces) do
    local p = f.p
    local xs, ys, zs, n = {}, {}, 0, #p
    local ok = true
    for k = 1, n do
      local x, y, z = scr(p[k])
      if not x then ok = false; break end
      xs[k], ys[k], zs = x, y, zs + z
    end
    if ok then
      local pos, neg, area = false, false, 0
      for k = 1, n do
        local k2 = k % n + 1
        local c = (xs[k2] - xs[k]) * (py - ys[k]) - (ys[k2] - ys[k]) * (px - xs[k])
        if c > 0 then pos = true elseif c < 0 then neg = true end
        area = area + xs[k] * ys[k2] - xs[k2] * ys[k]
      end
      if not (pos and neg) then
        local z = zs / n
        if area > 0 then
          if not bz or z < bz then best, bz = i, z end
        elseif not bbz or z < bbz then back, bbz = i, z end
      end
    end
  end
  return best or back
end

-- the point under the pointer: the index of the nearest of `points` (each
-- a position {x, y, z}, or a table with .p) within r pixels
function T.point_at(points, px, py, r)
  r = r or 8
  local best, bd
  for i, q in ipairs(points) do
    local x, y, z = scr(q.p or q)
    if x then
      local d = (x - px) ^ 2 + (y - py) ^ 2
      if d <= r * r and (not bd or d + z * 0.01 < bd) then best, bd = i, d + z * 0.01 end
    end
  end
  return best
end

function T.cam_pad(cam, lo, hi)
  if btn(0) then cam.yaw = cam.yaw - 0.04 end
  if btn(1) then cam.yaw = cam.yaw + 0.04 end
  if btn(2) then cam.pitch = clamp(cam.pitch - 0.03, -1.45, 1.2) end
  if btn(3) then cam.pitch = clamp(cam.pitch + 0.03, -1.45, 1.2) end
  if btn(4) then cam.dist = max(lo or 0.3, cam.dist * 0.98) end
  if btn(5) then cam.dist = min(hi or 200, cam.dist / 0.98) end
end

-- a hue for each bone (as bm Animator)
function T.bone_colour(i)
  local h, s, v = ((i - 1) * 0.618034) % 1, 0.65, 0.95
  local function k(n)
    local x = (n + h * 6) % 6
    return v - v * s * max(0, min(1, min(x, 4 - x)))
  end
  return (round(k(5) * 255) << 16) | (round(k(3) * 255) << 8) | round(k(1) * 255)
end

function T.draw_bone(h, t, c, chosen)
  local hx, hy = scr(h)
  local tx, ty = scr(t)
  if not hx or not tx then return end
  local dx, dy = tx - hx, ty - hy
  local l = sqrt(dx * dx + dy * dy)
  if l < 1 then circ(hx, hy, 3, c); return end
  local w = clamp(l * 0.12, 2, 7)
  local px, py = -dy / l * w, dx / l * w
  local mx, my = hx + dx * 0.2, hy + dy * 0.2
  line(hx, hy, mx + px, my + py, c); line(mx + px, my + py, tx, ty, c)
  line(hx, hy, mx - px, my - py, c); line(mx - px, my - py, tx, ty, c)
  if chosen then
    line(mx + px, my + py, mx - px, my - py, c)
    circ(hx, hy, 4, c)
  end
  circfill(hx, hy, 2, c)
end

-- grid lines on the plane y = level around (cx, cz), `n` cells each way
function T.draw_grid(cx, level, cz, n, step, c)
  for i = -n, n do
    local x, z = cx + i * step, cz + i * step
    seg({ x, level, cz - n * step }, { x, level, cz + n * step }, c)
    seg({ cx - n * step, level, z }, { cx + n * step, level, z }, c)
  end
end

function T.bounds_of(faces)
  local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
  for _, f in ipairs(faces) do
    for _, p in ipairs(f.p) do
      for k = 1, 3 do
        if p[k] < lo[k] then lo[k] = p[k] end
        if p[k] > hi[k] then hi[k] = p[k] end
      end
    end
  end
  if lo[1] > hi[1] then return { 0, 0, 0 }, { 1, 1, 1 } end
  return lo, hi
end

function T.aim(cam, faces, k)
  local lo, hi = T.bounds_of(faces)
  cam.floor = lo[2]                    -- the grid goes under the model
  cam.tx, cam.ty, cam.tz = (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, (lo[3] + hi[3]) / 2
  cam.size = max(hi[1] - lo[1], hi[2] - lo[2], hi[3] - lo[3], 0.5)
  cam.dist = cam.size * (k or 1.8)
end

-- the keyboard's pointer on what is on the screen: from item `cur`, the
-- nearest one in the direction of the arrow. `pos(i)` gives x, y, depth on
-- the screen (nil: not seen); what is farther counts a little more.
function T.nav(n, cur, dir, pos)
  if n == 0 then return nil end
  local cx, cy
  if cur then cx, cy = pos(cur) end
  if not cx then
    -- nothing yet: the one nearest to the middle of the view
    local best, bd = nil, 1e18
    for i = 1, n do
      local x, y, z = pos(i)
      if x then
        local d = (x - W / 2) ^ 2 + (y - H / 2) ^ 2 + (z or 0) * 4
        if d < bd then best, bd = i, d end
      end
    end
    return best
  end
  local dx, dy = 0, 0
  if dir == "left" then dx = -1 elseif dir == "right" then dx = 1
  elseif dir == "up" then dy = -1 else dy = 1 end
  local best, bs = cur, 1e18
  for i = 1, n do
    if i ~= cur then
      local x, y, z = pos(i)
      if x then
        local ax, ay = x - cx, y - cy
        local along = ax * dx + ay * dy
        local across = abs(ax * dy - ay * dx)
        if along > 1 and across <= along * 2 then
          local s = along + across * 2.5 + (z or 0) * 2
          if s < bs then best, bs = i, s end
        end
      end
    end
  end
  return best
end

----------------------------------------------------------------- drawing the frame

local function snap(x) return (x + 7) // 8 * 8 end   -- text stays on its 8 px columns
T.snap = snap

-- the keys of the page on the hint row, as chips (prompt()): a list of
-- { { keys... }, label }, as many as fit
function T.hint(list, y)
  y = y or HINT_Y
  local items = {}
  for i, h in ipairs(list) do items[i] = { keys = h[1], label = h[2] } end
  U.hint_row(items, { y = y, w = W, snap = snap, dim = C.DIM })
end

-- a key as a chip, or the pad's button when a pad was used last and the
-- action has one; then its label. Returns the x after it.
function T.chip_hint(key, pad, label, x, y, c)
  local li = lastinput()
  local x0 = x
  x = prompt(pad and (li == "ds4" or li == "pad") and pad or key, x, y)
  x = print(label, snap(x + 3), y, c or C.DIM)
  U.key_zone(x0, y, x - x0, 16, key)
  return x + 12
end

-- a list with a title (as the lists of bm Mesh): items[i] is a string;
-- `colour_of(i)` a dot before it; `pick(i, again)` makes it a list of the
-- mouse: a click chooses row i (again: a double click), the wheel moves
function T.draw_list(title, items, sel, x, y, rows, w, colour_of, pick)
  w = w or 168
  print(title, x + 16, y, C.DIM)
  local first = clamp(sel - rows // 2, 1, max(1, #items - rows + 1))
  local L = pick and { pick = pick, sel = sel, n = #items }
  if L then U.zone(x, y, w, (rows + 1) * 16, "list", 0, L) end
  for i = first, min(#items, first + rows - 1) do
    local yy = y + 16 + (i - first) * 16
    if L then U.zone(x, yy, w, 16, "row", i, L) end
    if i == sel then rectfill(x, yy, w, 16, C.SEL) end
    if colour_of then rectfill(x + 4, yy + 4, 6, 8, colour_of(i)) end
    print(items[i]:sub(1, (w - 24) // 8), x + 16, yy, i == sel and 0xFFFFFF or C.TEXT)
  end
  if first > 1 then print("^", x + w - 12, y + 16, C.DIM) end
  if first + rows - 1 < #items then print("v", x + w - 12, y + rows * 16, C.DIM) end
end

-- a box with a border, for the panels over the 3D view
function T.box(x, y, w, h, border)
  rectfill(x, y, w, h, C.PANEL)
  if border then rect(x, y, w, h, border) end
end

-- the faces, triangles and vertices of a model (on two lines when
-- `narrow`), with warnings
function T.counts(m, x, y, narrow)
  local nt, nv = m and m.nt or 0, m and m.nv or 0
  local x0 = x
  x = print((m and #m.faces or 0) .. " faces", x, y, C.TEXT) + 8
  x = print(nt .. " tri", x, y, nt > T.TRIS_60FPS and C.ACC or C.TEXT) + 8
  if narrow then x, y = x0, y + 16 end
  print(nv .. " of " .. T.LIMIT_V .. " vertices", x, y, nv > T.LIMIT_V and C.ERR or C.TEXT)
  if nt > T.TRIS_60FPS then return "heavy for 60 fps (" .. T.TRIS_60FPS .. " triangles a scene)" end
end

----------------------------------------------------------------- a model from a picture

-- a picture becomes a model through an image-to-3D service (picture3d,
-- src/net/img3d.c): the pictures on the SD card to choose from, the job
-- followed on the status line (a look every 5 seconds), then the .glb as
-- a model: its texture goes on the sheet when the sheet is untouched,
-- else the faces take the colours under them. Esc gives the job up.
local pic = nil
local PIC_PROVIDER = "meshy"

local function pictures()
  local out = {}
  for _, dir in ipairs({ "/pics", "/" }) do
    for _, f in ipairs(ls(dir)) do
      local n = f.name:lower()
      if not f.dir and (n:match("%.png$") or n:match("%.jpe?g$")) then
        out[#out + 1] = (dir == "/" and "" or dir) .. "/" .. f.name
      end
    end
  end
  table.sort(out)
  return out
end

local function sheet_untouched()
  local w, h = cart_sheet()
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      if sget(x, y) then return false end
    end
  end
  return true
end

----------------------------------------------------------------- import and export of models (APP-9)

-- What the three editors share to bring a model in from /bmm and /glb and to write one to /bmm.
-- The flows know no screen: ui = { choose = f(title, rows, sel), ask = f(label, text, done),
-- say = f(text, is_error) } is the editor's own dialogs. bm Studio and bm Animator give T.ui,
-- bm Mesh gives its own.
do
  local function bmm_names()
    local t = {}
    for _, e in ipairs(T.model_files()) do if e.kind == "bmm" then t[e.name] = true end end
    return t
  end

  -- a free .bmm name made from a name: lower case, 8 characters at most
  local function free_stem(name, taken)
    local s = tostring(name):lower():gsub("%.%w+$", ""):gsub("[^a-z0-9_%-]", ""):sub(1, 8)
    if s == "" then s = "model" end
    if not taken[s] then return s end
    for k = 2, 99 do
      local t = tostring(k)
      local n = s:sub(1, 8 - #t) .. t
      if not taken[n] then return n end
    end
    return s
  end

  local function counts(item) return #item.v .. " vertices, " .. #item.f .. " triangles" end

  -- how the list shows an entry of T.model_files()
  function T.entry_label(e)
    if e.kind == "glb" then return e.name .. "   .glb: converted to /bmm" end
    return e.name .. "   .bmm" .. (e.glb and " (and .glb)" or "")
  end

  -- one entry: a .bmm is read; a .glb is converted to /bmm/NAME.bmm (a new name is asked when its own
  -- is over 8 characters or is taken); done(item, text) gets the item
  function T.import_entry(ui, e, done)
    if e.kind == "bmm" then
      local item, err = T.import_model(e.name)
      if not item then ui.say(tostring(err), true); return false end
      done(item, e.name .. ".bmm: " .. counts(item))
      return true
    end
    local function convert(to)
      local ok, path, item = T.import_glb_to_bmm(e.name, { to = to })
      if not ok then ui.say(tostring(path), true); return false end
      done(item, e.name .. ".glb to " .. path .. ": " .. counts(item))
      return true
    end
    local to = bmm_stem(e.name)
    if to and not bmm_names()[to] then return convert(to) end
    local function name_it(text, why)
      ui.say(why, true)
      ui.ask("new name in /bmm (1-8 of a-z 0-9 _ -)", text, function(t)
        local s = bmm_stem(t)
        if not s then return name_it(t, "'" .. t .. "': a name is 1 to 8 of a-z 0-9 _ -") end
        if bmm_names()[s] then return name_it(t, "/bmm/" .. s .. ".bmm is there already") end
        convert(s)
      end)
    end
    name_it(free_stem(e.name, bmm_names()),
            to and ("/bmm/" .. to .. ".bmm is there already: give the .glb another name")
                or (e.name .. ".glb: the .bmm name is 1 to 8 of a-z 0-9 _ -: give it a name"))
    return true
  end

  -- the list of /bmm and /glb to choose from; false (and a message) if there is none
  function T.import_flow(ui, done)
    local list = T.model_files()
    if #list == 0 then ui.say("no model in /bmm or /glb on the SD card", true); return false end
    local rows = {}
    for i, e in ipairs(list) do
      rows[i] = { T.entry_label(e), function() T.import_entry(ui, e, done) end }
    end
    ui.choose("import a model (/bmm, /glb)", rows, 1)
    return true
  end

  -- an item to /bmm/NAME.bmm: asks the name; a file that is there is replaced only when chosen
  function T.export_item(ui, item)
    if not item or #item.v == 0 or #item.f == 0 then ui.say("the model is empty: nothing to export", true); return false end
    local function write(stem, replace)
      local ok, path = T.write_bmm(item, stem, { replace = replace })
      if not ok then ui.say(tostring(path), true); return false end
      ui.say("exported to " .. path .. ": " .. counts(item), false)
      return true
    end
    local function name_it(text)
      ui.ask("export to /bmm: name (1-8 of a-z 0-9 _ -)", text, function(t)
        local s = bmm_stem(t)
        if not s then ui.say("'" .. t .. "': a name is 1 to 8 of a-z 0-9 _ -", true); return name_it(t) end
        if bmm_names()[s] then
          ui.choose("/bmm/" .. s .. ".bmm is there already", {
            { "Replace it", function() write(s, true) end },
            { "Another name...", function() name_it(s) end },
          }, 2)
        else
          write(s, false)
        end
      end)
    end
    name_it(free_stem(item.name, {}))
    return true
  end

  -- the sheet of the project as a sheet section (type 2: width, height, RGBA), nil if it has none
  -- maxh: only the first rows (bm Mesh keeps its reference pictures below the project's sheet)
  function T.sheet_section(maxh)
    if not cart_sheet or not sget then return nil end
    local ok, w, h = pcall(cart_sheet)
    if not ok or not w or not h or w < 1 or h < 1 then return nil end
    if maxh and maxh < h then h = maxh end
    local rows = {}
    for y = 0, h - 1 do
      local px = {}
      for x = 0, w - 1 do
        local c = sget(x, y)
        px[#px + 1] = c and spack("BBBB", (c >> 16) & 255, (c >> 8) & 255, c & 255, 255) or "\0\0\0\0"
      end
      rows[#rows + 1] = table.concat(px)
    end
    return spack("<I2I2", w, h) .. table.concat(rows)
  end

  -- the pixels of a sheet section on the project's sheet
  local function sheet_put(data)
    local w, h = sunpack("<I2I2", data)
    local cw, ch = cart_sheet()
    if w > cw or h > ch then cart_sheet(math.max(w, cw), math.max(h, ch)) end
    for y = 0, h - 1 do
      local base = 4 + y * w * 4
      for x = 0, w - 1 do
        local r, g, b, a = data:byte(base + x * 4 + 1, base + x * 4 + 4)
        sset(x, y, a >= 128 and (r << 16 | g << 8 | b) or nil)
      end
    end
    S.sheet_dirty = true
  end

  -- bm Studio and bm Animator: the item as a model of the project (the current one if it is empty).
  -- The sheet of a .bmm goes on the project's sheet when that is untouched; else its tiles turn
  -- flat grey.
  function T.take_item(item, note)
    local faces, rig = T.decode_model(item.mc, item.ac)
    local textured = false
    for _, f in ipairs(faces) do if f.uv then textured = true end end
    local sheet_text = ""
    if textured then
      if item.sheet and item.sheet.type == 2 and sheet_untouched() then
        sheet_put(item.sheet.data)
        sheet_text = ", its texture on the sheet"
      else
        for _, f in ipairs(faces) do if f.uv then f.uv, f.c = nil, 0x808080 end end
        sheet_text = ", textured faces flat grey (the sheet is in use)"
      end
    end
    local cur = M()
    local reuse = cur ~= nil and #cur.faces == 0 and not (cur.rig and #cur.rig.bones > 0)
    local i
    if reuse then
      i = S.cur
      cur.name = T.unique_name(item.name, S.cur)
    else
      i = T.new_model(item.name)
    end
    local m = S.models[i]
    m.faces, m.rig, m.dirty = faces, rig, true
    S.undo, S.redo = {}, {}
    local ok, e = T.sync()
    if not ok then
      if reuse then m.faces, m.rig = {}, nil else table.remove(S.models, i) end
      T.sync()
      say("cannot take the model: " .. tostring(e), C.ERR, 400)
      return false
    end
    S.dirty = true
    T.select_model(i)
    T.refresh()
    T.go(A.order[1])
    local pg = A.pages[A.order[1]]
    if pg.reset then pg.reset() end
    local nb = rig and #rig.bones or 0
    say(note .. sheet_text .. (nb > 0 and (", " .. nb .. " bones") or "") .. ": model " .. m.name, C.ACC, 400)
    return true
  end

  T.ui = {
    choose = function(title, rows, sel) T.choose(title, rows, sel) end,
    ask = function(label, text, done) T.ask(label, text, done) end,
    say = function(s, err) say(s, err and C.ERR or C.ACC, 400) end,
  }

  function T.import_dialog() return T.import_flow(T.ui, T.take_item) end

  -- the current model to /bmm (its skeleton and, if it has textured faces, the sheet go with it)
  function T.export_dialog()
    local m = M()
    if not m or #m.faces == 0 then say("the model is empty: nothing to export", C.ERR, 300); return false end
    local ok, e = T.sync()
    if not ok or not m.mc then say("cannot export: " .. tostring(e or "no faces"), C.ERR, 300); return false end
    local item = G.model_item(m.name, m.mc, m.ac)
    item.inset = S.inset
    for _, f in ipairs(m.faces) do
      if f.uv then
        local data = T.sheet_section()
        if data then item.sheet = { type = 2, data = data } end
        break
      end
    end
    return T.export_item(T.ui, item)
  end
end

-- the model into the project: a new model named after the picture
local function picture_into_project(m, how, name)
  local use_texture = m.textured and m.texture and sheet_untouched()
  local i = T.new_model(name)
  local model = S.models[i]
  model.faces = T.decode_model(use_texture and m.record or m.flat or m.record)
  if use_texture then
    local w, h = cart_sheet()
    if w < 256 or h < 256 then cart_sheet(math.max(w, 256), math.max(h, 256)) end
    local tex = m.texture
    for y = 0, 255 do
      local base = y * 1024
      for x = 0, 255 do
        local r, g, b, a = tex:byte(base + x * 4 + 1, base + x * 4 + 4)
        sset(x, y, a >= 128 and (r << 16 | g << 8 | b) or nil)
      end
    end
    S.sheet_dirty = true
  end
  S.undo, S.redo = {}, {}
  model.dirty = true
  local ok, e = T.sync()
  if not ok then
    table.remove(S.models, i)
    T.sync()
    T.say("cannot take the model: " .. tostring(e), C.ERR, 400)
    return
  end
  T.select_model(i)
  S.dirty = true
  T.refresh()
  T.say(how .. ": the model " .. model.name .. ", " .. m.nf .. " triangles" ..
      (use_texture and ", its texture on the sheet" or (m.textured and ", flat colours (the sheet is in use)" or "")),
      C.ACC, 400)
end

local function picture_take(url)
  local m, err = picture3d("take", url, { name = pic.name, faces = T.TRIS_60FPS, height = 2 })
  if not m then T.say(PIC_PROVIDER .. ": " .. tostring(err), C.ERR, 400); return end
  picture_into_project(m, PIC_PROVIDER, pic.name)
end

local function model_name(path)
  local name = path:match("([^/]+)%.[^.]+$") or "model"
  name = name:gsub("[^%w_]", ""):sub(1, 15):lower()
  return name ~= "" and name or "model"
end

local function picture_start(path)
  local task, err = picture3d("start", path, { provider = PIC_PROVIDER, polycount = 2000 })
  if not task then T.say("cannot start: " .. tostring(err), C.ERR, 400); return end
  pic = { task = task, t = 0, name = model_name(path), progress = 0, url = nil }
  T.say(PIC_PROVIDER .. ": the job started: a few minutes for the model (Esc gives up)", C.ACC, 600)
end

-- the outline methods, made here (cutout3d, src/bm/cutout.c): the frame after
-- the message, as the call takes a moment on the console
local function outline_start(path, lathe)
  pic = { local_path = path, lathe = lathe, name = model_name(path), t = 0 }
  T.say((lathe and "lathe" or "cutout") .. ": making the model from the outline...", C.ACC, 300)
end

local function outline_take()
  local p = pic
  pic = nil
  local m, err = cutout3d(p.local_path, { name = p.name, lathe = p.lathe, faces = T.TRIS_60FPS, height = 2 })
  if not m then T.say((p.lathe and "lathe" or "cutout") .. ": " .. tostring(err), C.ERR, 400); return end
  picture_into_project(m, p.lathe and "lathe" or "cutout", p.name)
end

-- the pictures of the SD card to choose from, for a method; false if
-- there are none (or no key, for the service)
local function picture_list(method)
  if method == "meshy" then
    local ok, why = picture3d("ready", PIC_PROVIDER)
    if not ok then T.say(tostring(why), C.ERR, 600); return false end
  end
  local files = pictures()
  if #files == 0 then T.say("no .png or .jpg pictures in /pics on the SD card", C.ERR, 400); return false end
  local rows = {}
  for i, f in ipairs(files) do
    rows[i] = { f, function()
      if method == "meshy" then picture_start(f) else outline_start(f, method == "lathe") end
    end }
  end
  T.choose("a picture to make a model from (" .. method .. ")", rows, 1)
  return true
end

-- the ways: the outline cut out or turned (here, no network), or the
-- image-to-3D service; then the pictures
function T.picture_chooser()
  if not cutout3d and not picture3d then T.say("this kernel cannot make models from pictures", C.ERR, 300); return false end
  if pic then T.say("a model is on its way already (Esc gives it up)", C.ERR, 300); return false end
  local rows = {}
  if cutout3d then
    rows[#rows + 1] = { "cutout: the picture's outline with some thickness (made here)",
                        function() picture_list("cutout") end }
    rows[#rows + 1] = { "lathe: the outline turned around (vases, towers; made here)",
                        function() picture_list("lathe") end }
  end
  if picture3d then
    rows[#rows + 1] = { PIC_PROVIDER .. ".ai: image-to-3D service (a key in bm/config.txt)",
                        function() picture_list("meshy") end }
  end
  T.choose("a model from a picture: how", rows, 1)
  return true
end

-- each frame: a look at the job every 5 seconds; the download the frame
-- after it is done (the message shows first: the calls block)
function T.picture_update()
  if not pic then return end
  pic.t = pic.t + 1
  if pic.local_path then
    if pic.t >= 2 then outline_take() end
    return
  end
  if pic.url then
    local url = pic.url
    pic = { name = pic.name }
    picture_take(url)
    pic = nil
    return
  end
  if pic.t % 300 ~= 0 then return end
  local st, a = picture3d("status", pic.task, PIC_PROVIDER)
  if st == "running" then
    pic.progress = a
    T.say(PIC_PROVIDER .. ": " .. a .. "% of the model (Esc gives up)", C.TEXT, 320)
  elseif st == "done" then
    pic.url = a
    T.say(PIC_PROVIDER .. ": downloading the model...", C.ACC, 600)
  else
    T.say(PIC_PROVIDER .. ": " .. tostring(a), C.ERR, 600)
    pic = nil
  end
end

-- Esc while a job is on its way: it is given up (the service goes on by itself)
function T.picture_key(k)
  if pic and not pic.url and not pic.local_path and (k == "esc" or k == "back") then
    pic = nil
    T.say(PIC_PROVIDER .. ": the job given up", C.DIM, 200)
    return true
  end
  return false
end

function T.picture_busy() return pic ~= nil end

----------------------------------------------------------------- dialogs

local input, choosing = nil, nil
local confirm_t, confirm_what = 0, nil

-- a line of text: done(text) on Enter
ask = function(label, text, done)
  input = { label = label, text = text, done = done }
end
T.ask = ask

-- a list to choose from: rows = { {label, fn}, ... }
function T.choose(title, rows, sel)
  choosing = { title = title, rows = rows, sel = sel or 1 }
end

-- an action that loses something: true the first time (it says so), false
-- when chosen again soon after
function T.confirm(what, text)
  if confirm_what == what and confirm_t > 0 then confirm_what = nil; return false end
  confirm_what, confirm_t = what, 150
  say(text, C.ERR)
  return true
end

local function needs_confirm(what)
  if not S.dirty then return false end
  return T.confirm(what, "unsaved changes: choose again to confirm")
end
T.needs_confirm = needs_confirm

local function input_key(k)
  if k == "\n" then local d, t = input.done, input.text; input = nil; d(t)
  elseif k == "esc" then input = nil
  else U.line_key(input, k, 40) end
end

local function choose_key(k)
  local c = choosing
  if k == "up" then c.sel = (c.sel - 2) % #c.rows + 1
  elseif k == "down" then c.sel = c.sel % #c.rows + 1
  elseif k == "pgup" then c.sel = max(1, c.sel - 12)
  elseif k == "pgdn" then c.sel = min(#c.rows, c.sel + 12)
  elseif k == "esc" or k == "back" then choosing = nil
  elseif k == "\n" or k == " " or k == "ok" then
    choosing = nil
    c.rows[c.sel][2]()
  end
end

-- the dialogs: the same as bm Mesh's and bm Pixel's
local function draw_input()
  U.zone(0, 0, W, H, "shade")                  -- a click outside: Esc
  U.zone(80, 144, 480, 64, "box")
  rectfill(80, 144, 480, 64, C.PANEL)
  rect(80, 144, 480, 64, C.ACC)
  print(input.label, 96, 160, C.DIM)
  U.zone(88, 176, 464, 16, "field")              -- a click puts the caret
  U.line_draw(input, 96, 176, C.TEXT, (S.frame // 20) % 2 == 0)
  T.chip_hint("esc", nil, "cancel", T.chip_hint("enter", nil, "ok", 336, 144), 144)
end

local function draw_choose()
  local p = choosing
  local rows = min(#p.rows, 14)
  local h = (rows + 2) * 16
  local y0 = max(32, (H - h) // 32 * 16)
  U.zone(0, 0, W, H, "shade")
  U.zone(40, y0, 560, h, "box")
  rectfill(40, y0, 560, h, C.PANEL)
  rect(40, y0, 560, h, C.ACC)
  local tx = print(p.title, 56, y0, C.ACC) + 16
  T.chip_hint("esc", "B", "back", T.chip_hint("enter", "A", "choose", tx, y0), y0)
  local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
  for i = first, min(#p.rows, first + rows - 1) do
    local y = y0 + 16 + (i - first) * 16
    U.zone(48, y, 544, 16, "choose", i)
    if i == p.sel then rectfill(48, y, 544, 16, C.SEL) end
    print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C.TEXT)
  end
  if first + rows - 1 < #p.rows then print("v", 576, y0 + rows * 16, C.DIM) end
end

----------------------------------------------------------------- the menu

local items, msel = {}, 1

-- The keys shown while F12 is held, under the system's (the kernel's
-- keyhelp(), 2026-10-04): the pages and the models (all the apps of bm3d),
-- the app's (A.help), the page's (its help), the pad's (bm3d's Y + ...,
-- A.help_pad, the page's help_pad). Entries { "keys", "what" }: keyboard
-- keys in lower case ("ctrl d", "shift w", "a / d"), the pad's buttons in
-- upper case; a string is a heading.
function T.keyhelp()
  if not keyhelp then return end                -- a kernel before them
  local list = {}
  local function add(t) for _, e in ipairs(t or {}) do list[#list + 1] = e end end
  for _, id in ipairs(A.order) do list[#list + 1] = { A.pages[id].fkey, A.pages[id].label } end
  list[#list + 1] = { "[ / ]", "the model before / after" }
  list[#list + 1] = { "ctrl i", "import a model: the list of /bmm and /glb" }
  list[#list + 1] = { "ctrl e", "export the model to /bmm" }
  add(A.help)
  local pg = A.pages[S.page]
  if pg and pg.help then
    list[#list + 1] = pg.label
    add(pg.help)
  elseif S.page == "menu" then
    list[#list + 1] = "menu"
    add({ { "up / down", "choose" }, { "enter", "select" } })
  end
  list[#list + 1] = "pad"
  add({ { "Y LEFTRIGHT", "page" }, { "Y B", "menu (Import, Export)" }, { "Y UPDOWN", "model" }, { "Y A", "undo" },
        { "Y X", "assistant" } })
  add(A.help_pad)
  if pg then add(pg.help_pad) end
  keyhelp(list, A.name)
end

local function go(p)
  if not A.pages[p] and p ~= "menu" then p = A.order[1] end
  if p ~= "menu" then S.last_page = p
  elseif S.page ~= "menu" then msel = 1 end     -- the menu opens on Continue
  S.page = p
  local pg = A.pages[p]
  if pg and pg.enter then pg.enter() end
  T.keyhelp()
end
T.go = go

reset_pages = function()
  for _, id in ipairs(A.order) do
    local pg = A.pages[id]
    if pg.reset then pg.reset() end
  end
end
T.reset_pages = reset_pages

-- after undo, a model chosen: the pages keep valid indexes
local function refresh()
  for _, id in ipairs(A.order) do
    local pg = A.pages[id]
    if pg.refresh then pg.refresh() end
  end
end
T.refresh = refresh

-- the .bm files of the SD card to open (as bm Mesh's); true if there are some
function T.open_chooser()
  local files = T.list_files()
  if #files == 0 then say("no .bm files on the SD card", C.ERR); return false end
  local rows, sel = {}, 1
  for i, f in ipairs(files) do
    rows[i] = { f, function() if T.load_project(f) then go(A.order[1]) end end }
    if f == S.proj.path then sel = i end
  end
  T.choose("open a cartridge", rows, sel)
  return true
end

local function build_menu()
  items = {
    { "Continue", function() go(S.last_page or A.order[1]) end },
    { "Open...", function() if not needs_confirm("open") then T.open_chooser() end end },
  }
  if A.new_project then
    items[#items + 1] = { "New project", function() if not needs_confirm("new") then A.new_project() end end }
  end
  items[#items + 1] = { "Save   (Ctrl+S)", function() T.save_project() end }
  items[#items + 1] = { "Save as...   (Ctrl+Shift+S)", function() T.save_as() end }
  items[#items + 1] = { "Try the game (F5)", function() T.run_project() end }
  if A.menu then A.menu(items) end
  items[#items + 1] = { "Import a model...   (Ctrl+I)", function() if T.import_dialog() then go(S.last_page or A.order[1]) end end }
  items[#items + 1] = { "Export to /bmm...   (Ctrl+E)", function() if T.export_dialog() then go(S.last_page or A.order[1]) end end }
  if A.picture then items[#items + 1] = { "Model from picture...", function() if T.picture_chooser() then go(S.last_page or A.order[1]) end end } end
  -- opened by the bm SDK on this file: the way back to it (saved first)
  if S.from_sdk then items[#items + 1] = { "Back to bm SDK", function() T.open_in("sdk", "bm SDK") end } end
  items[#items + 1] = { "Exit " .. A.name, function() if not needs_confirm("exit") then quit() end end }
end

local function menu_key(k)
  build_menu()
  if k == "up" then msel = (msel - 2) % #items + 1
  elseif k == "down" then msel = msel % #items + 1
  elseif k == "\n" or k == "ok" then items[msel][2]()
  elseif (k == "esc" or k == "back") and #S.models > 0 then go(S.last_page or A.order[1]) end
end

-- the menu page, laid out as bm Mesh's and bm Pixel's
local function draw_menu()
  cls(C.BG)
  build_menu()
  print(A.name, 32, 32, C.ACC)
  print((S.proj.path or "(not saved yet)") .. (S.dirty and "  *modified*" or ""), snap(32 + (#A.name + 2) * 8), 32,
        C.DIM)
  for i, it in ipairs(items) do
    local y = 64 + (i - 1) * 16
    U.zone(24, y, 272, 16, "menuitem", i)
    if i == msel and not choosing and not input then rectfill(24, y, 272, 16, C.SEL) end
    print(it[1], 32, y, C.TEXT)
  end
  local x = 320
  print(S.proj.title:sub(1, 38), x, 64, C.TEXT)
  local nr = 0
  for _, m in ipairs(S.models) do if m.rig then nr = nr + 1 end end
  print(#S.models .. " models, " .. nr .. " with a skeleton", x, 96, C.TEXT)
  -- the keys of the pages, as many a line as fit
  local y, cx = 128, x
  local function chip(key, label)
    local w = prompt(key) + 3 + #label * 8 + 12
    if cx > x and cx + w > W - 8 then cx, y = x, y + 16 end
    cx = T.chip_hint(key, nil, label, cx, y)
  end
  for _, id in ipairs(A.order) do chip(A.pages[id].fkey, A.pages[id].label) end
  chip("f5", "try the game")
  y = y + 16
  T.chip_hint("f12", nil, "held: the keys", x, y)
  if A.menu_info then A.menu_info(x, y + 32) end
  T.hint({ { { "up", "down" }, "choose" }, { { "enter" }, "select" }, { { "esc" }, "back" } })
end

----------------------------------------------------------------- main

local last_t = 0

local function global_key(k)
  local pg = A.pages[S.page]
  local busy = pg and pg.modal and pg.modal()
  for _, id in ipairs(A.order) do
    if k == A.pages[id].fkey then go(id); return true end
  end
  -- the system's keys (the kernel's syskeys.c)
  if k == "esc" and S.page ~= "menu" and not busy then go("menu"); return true
  elseif k == "^s" then T.save_project(); return true
  elseif k == "^S" then T.save_as(); return true
  elseif k == "^o" and not busy then
    if not needs_confirm("open") then T.open_chooser() end
    return true
  elseif k == "^n" and not busy and A.new_project then
    if not needs_confirm("new") then A.new_project() end
    return true
  elseif k == "^i" and not busy then T.import_dialog(); return true
  elseif k == "^e" and not busy then T.export_dialog(); return true
  elseif k == "f5" or k == "^r" then T.run_project(); return true
  elseif k == "f6" and not busy then T.assistant(); return true
  elseif k == "^z" then T.do_undo(S.undo, S.redo, "undo"); refresh(); return true
  elseif k == "^y" then T.do_undo(S.redo, S.undo, "redo"); refresh(); return true
  elseif (k == "[" or k == "]") and S.page ~= "menu" and not busy and #S.models > 0 then
    T.select_model(S.cur + (k == "]" and 1 or -1)); refresh(); go(S.page)
    say("model " .. M().name, C.ACC, 60)
    return true
  end
  return false
end

-- the assistant (F6, Y + X): a 3D recipe ("una casa rossa", "mech"...)
-- becomes a model, with its skeleton and animations when it has them
function T.assistant()
  if not ok_assist or not ai or not ai.mesh then say("the assistant is not here", C.ERR); return end
  assist.open{ mode = "mesh", on_mesh = T.take_model }
end

-- the assistant's model: into the current model if it is empty, else a new
-- one named after the recipe; then the first page shows it
function T.take_model(m)
  if not m or not m.faces or #m.faces == 0 then return end
  local cur, i = M(), nil
  if cur and #cur.faces == 0 and not (cur.rig and #cur.rig.bones > 0) then
    i = S.cur
    if cur.name:match("^model%d*$") then cur.name = T.unique_name(m.gen, S.cur) end
  else
    i = T.new_model(m.gen)
  end
  local mm = S.models[i]
  mm.faces = m.faces
  mm.rig = nil
  if m.bones and #m.bones > 0 then
    mm.rig = { bones = m.bones, clips = m.clips or {} }
  else
    for _, f in ipairs(mm.faces) do f.b = nil end
  end
  mm.dirty = true
  S.undo, S.redo = {}, {}
  local ok, e = T.sync()
  if not ok then
    mm.faces, mm.rig, mm.dirty = {}, nil, true
    T.sync()
    say("cannot: " .. tostring(e), C.ERR)
    return
  end
  S.dirty = true
  T.select_model(i)
  refresh()
  go(A.order[1])
  local pg = A.pages[A.order[1]]
  if pg.reset then pg.reset() end              -- the camera on it
  local nb = mm.rig and #mm.rig.bones or 0
  say(string.format("the assistant's %s: %d faces%s, model %s", m.name or m.gen, #m.faces,
                    nb > 0 and string.format(", %d bones, %d animations", nb, #mm.rig.clips) or "", mm.name),
      C.ACC, 300)
end

-- The mouse, each frame (bmui: it does what the keys do). The chips and
-- the tabs press their keys; in the dialogs a click chooses (a click on the
-- chosen row, or a double click, acts), outside them it is Esc; in the menu
-- a click acts; a list of the panel chooses its row (double: acts) and its
-- wheel moves; then the page's own (pg.mouse), and the right button the
-- context menu (pg.menu_items(), plus undo, save and the menu)
function T.mouse_frame()
  if not U.on then return end
  if U.keys() then return end
  if input then
    local z = U.click(0)
    if z and z.kind == "shade" then U.press("esc")
    elseif z and z.kind == "field" then U.line_click(input, 96, 8) end
    return
  end
  if choosing then
    local c = choosing
    if U.wheel ~= 0 then c.sel = clamp(c.sel - U.wheel, 1, #c.rows) end
    local z = U.click(0)
    if z and z.kind == "choose" then
      if z.a == c.sel or U.double then U.press("\n") else c.sel = z.a end
    elseif z and z.kind == "shade" then
      U.press("esc")
    end
    return
  end
  if S.page == "menu" then
    if U.wheel ~= 0 and #items > 0 then msel = clamp(msel - U.wheel, 1, #items) end
    local z = U.click(0)
    if z and z.kind == "menuitem" then msel = z.a; U.press("\n") end
    return
  end
  local pg = A.pages[S.page]
  local z = U.at()
  if z and (z.kind == "row" or z.kind == "list") and U.wheel ~= 0 then
    local L = z.b
    L.pick(clamp(L.sel - U.wheel, 1, max(1, L.n)), false)
  end
  local c = U.click(0)
  if c and c.kind == "row" then
    c.b.pick(c.a, U.double)
    return
  end
  if pg and pg.mouse then pg.mouse() end
  local r = U.clicked(1)
  if r and not (pg and pg.modal and pg.modal()) then
    local list = pg and pg.menu_items and pg.menu_items() or {}
    if #list > 0 then list[#list + 1] = "-" end
    list[#list + 1] = { "Undo", "^z" }
    list[#list + 1] = { "Redo", "^y" }
    list[#list + 1] = { "Save", "^s" }
    list[#list + 1] = { "Try the game", "f5" }
    list[#list + 1] = { "Import a model", "^i" }
    list[#list + 1] = { "Export to /bmm", "^e" }
    list[#list + 1] = { "Assistant", "f6" }
    list[#list + 1] = { "Menu", "esc" }
    U.menu(list)
  end
end

function T.run(app)
  A = app
  A.pages = {}
  A.order = {}
  for _, pg in ipairs(app.page_list) do
    A.pages[pg.id] = pg
    A.order[#A.order + 1] = pg.id
  end

  function _init()
    keyp()                                 -- typing on: the keyboard types
    U.wants()                              -- the pointer, when there is a mouse
    if A.init then A.init() end
    last_t = time()
    local a = cart_arg()
    S.from_sdk = a and a.from == "sdk"
    if a and a.path and T.load_project(a.path) then
      local back = a.back and saved()
      if a.error then
        go("menu")
        say("the game stopped: " .. a.error:sub(1, 60), C.ERR, 400)
      elseif back and back.page then
        T.select_model(back.model or 1)
        refresh()
        go(back.page)
        say("back from the game", C.ACC)
      else
        go(A.order[1])
      end
    else
      if A.new_project then A.new_project(true) else S.models = {} end
      go("menu")
      say(A.hello(T.open_chooser()), C.ACC, 400)
    end
  end

  -- Ctrl+Esc or PS (the system's keys): back to bm's menu; with unsaved
  -- changes it asks first, and the same again leaves without saving
  function _exit()
    if S.dirty and T.confirm("exit", "unsaved changes: Ctrl+Esc again leaves without saving") then
      return false
    end
    return true
  end

  function _update()
    S.frame = S.frame + 1
    if S.msg_t > 0 then S.msg_t = S.msg_t - 1 end
    if confirm_t > 0 then confirm_t = confirm_t - 1 end
    local now = time()
    local dt = clamp(now - last_t, 0, 0.1)
    last_t = now
    read_pad()
    U.update()
    if assist.update() then return end          -- the assistant has the keys
    T.picture_update()                          -- a model on its way from a picture
    if U.menu_update() then return end          -- the context menu has the input
    T.mouse_frame()

    while true do
      local k = keyp()
      if not k then break end
      if input then input_key(k)
      elseif choosing then choose_key(k)
      elseif T.picture_key(k) then
      elseif not global_key(k) then
        local pg = A.pages[S.page]
        if pg then pg.key(k) else menu_key(k) end
      end
    end

    local pg = A.pages[S.page]
    if input then
      if btnp(5) then input = nil end
    elseif choosing then
      if rp[2] then choose_key("up") elseif rp[3] then choose_key("down")
      elseif btnp(4) then choose_key("ok") elseif btnp(5) then choose_key("back") end
    elseif btn(7) and not (pg and pg.modal and pg.modal()) then
      -- the pad: Y + left/right page, Y + B menu, Y + A undo, Y + up/down model
      if btnp(0) or btnp(1) then
        local list = { table.unpack(A.order) }
        list[#list + 1] = "menu"
        local i = 1
        for n, p in ipairs(list) do if p == S.page then i = n end end
        go(list[(i - 1 + (btnp(1) and 1 or -1)) % #list + 1])
      elseif btnp(5) then go("menu")
      elseif btnp(6) then T.assistant()
      elseif btnp(4) then T.do_undo(S.undo, S.redo, "undo"); refresh()
      elseif (btnp(2) or btnp(3)) and #S.models > 0 then
        if pg and pg.pad_y then pg.pad_y(btnp(2) and -1 or 1)
        else T.select_model(S.cur + (btnp(3) and 1 or -1)); refresh(); go(S.page); say("model " .. M().name, C.ACC, 60) end
      end
    elseif S.page == "menu" then
      if rp[2] then menu_key("up") elseif rp[3] then menu_key("down")
      elseif btnp(4) then menu_key("ok") elseif btnp(5) then menu_key("back") end
    elseif pg and pg.pad then pg.pad()
    end
    if pg and pg.update then pg.update(dt) end
  end

  function _draw()
    U.begin()
    local pg = A.pages[S.page]
    if pg then pg.draw() else draw_menu() end

    -- tab bar: each page with its key
    rectfill(0, 0, W, 16, C.BAR)
    local x = U.bar_title(A.name, C.ACC)             -- the name first, at the left
    local tabs = {}
    for _, id in ipairs(A.order) do tabs[#tabs + 1] = { id, A.pages[id].fkey, A.pages[id].label } end
    tabs[#tabs + 1] = { "menu", "esc", "menu" }
    for _, t in ipairs(tabs) do
      local lx = snap(x + prompt(t[2]) + 3)            -- the label on its column
      local w = lx + #t[3] * 8 - x
      if t[1] == S.page then rectfill(x - 4, 0, w + 8, 16, C.SEL) end
      U.key_zone(x - 4, 0, w + 8, 16, t[2])        -- a click: the page
      prompt(t[2], x, 0)
      print(t[3], lx, 0, t[1] == S.page and 0xFFFFFF or C.DIM)
      x = x + w + 20
    end
    U.bar_file(S.proj.path or "untitled", S.dirty, x, W, C.ACC, C.DIM)

    -- status bar
    rectfill(0, STATUS_Y, W, H - STATUS_Y, C.BAR)
    local status
    if S.msg_t > 0 and S.msg then status = S.msg
    elseif S.page == "menu" then status = "up/down choose, Enter select"
    elseif pg and pg.status then status = pg.status()
    else status = M() and ("model " .. S.cur .. "/" .. #S.models .. ": " .. M().name) or "" end
    print(status:sub(1, 79), 0, STATUS_Y, (S.msg_t > 0 and S.msg_c) or C.TEXT)
    if #status <= 62 then                  -- room on the right: F12 held for the keys
      T.chip_hint("f12", nil, "held: keys", 528, STATUS_Y)
    end
    if choosing then draw_choose() end
    if input then draw_input() end
    assist.draw()                                -- the assistant's panel on top, if open
    U.menu_draw()                                -- the context menu over everything
  end
end

----------------------------------------------------------------- Tiled maps (R11)

-- A small JSON reader (for Tiled's .tmj/.json files): objects, arrays,
-- strings, numbers, true, false, null (null becomes nil). Returns the value,
-- or nil and the reason.
function T.json_decode(s)
  local pos = 1
  local esc = { b = "\b", f = "\f", n = "\n", r = "\r", t = "\t", ['"'] = '"', ["\\"] = "\\", ["/"] = "/" }
  local function skip() pos = s:find("[^ \t\r\n]", pos) or #s + 1 end
  local function fail(msg) error({ msg = msg .. " at byte " .. pos }, 0) end
  local value
  local function str()
    local out, i = {}, pos + 1
    while true do
      local j = s:find('["\\]', i)
      if not j then pos = i; fail("string not closed") end
      out[#out + 1] = s:sub(i, j - 1)
      if s:sub(j, j) == '"' then pos = j + 1; return table.concat(out) end
      local c = s:sub(j + 1, j + 1)
      if c == "u" then
        local cp = tonumber(s:sub(j + 2, j + 5), 16)
        if not cp then pos = j; fail("bad \\u escape") end
        out[#out + 1] = cp < 128 and string.char(cp) or "?"
        i = j + 6
      elseif esc[c] then out[#out + 1] = esc[c]; i = j + 2
      else pos = j; fail("bad escape") end
    end
  end
  function value()
    skip()
    local c = s:sub(pos, pos)
    if c == "{" then
      local t = {}
      pos = pos + 1; skip()
      if s:sub(pos, pos) == "}" then pos = pos + 1; return t end
      while true do
        skip()
        if s:sub(pos, pos) ~= '"' then fail("a key was expected") end
        local k = str()
        skip()
        if s:sub(pos, pos) ~= ":" then fail("':' was expected") end
        pos = pos + 1
        t[k] = value()
        skip()
        local d = s:sub(pos, pos)
        pos = pos + 1
        if d == "}" then return t end
        if d ~= "," then pos = pos - 1; fail("',' or '}' was expected") end
      end
    elseif c == "[" then
      local t, n = {}, 0
      pos = pos + 1; skip()
      if s:sub(pos, pos) == "]" then pos = pos + 1; return t end
      while true do
        n = n + 1
        t[n] = value()
        skip()
        local d = s:sub(pos, pos)
        pos = pos + 1
        if d == "]" then return t end
        if d ~= "," then pos = pos - 1; fail("',' or ']' was expected") end
      end
    elseif c == '"' then return str()
    elseif s:sub(pos, pos + 3) == "true" then pos = pos + 4; return true
    elseif s:sub(pos, pos + 4) == "false" then pos = pos + 5; return false
    elseif s:sub(pos, pos + 3) == "null" then pos = pos + 4; return nil
    else
      local num = s:match("^-?%d+%.?%d*[eE]?[+-]?%d*", pos)
      if not num or num == "" then fail("a value was expected") end
      pos = pos + #num
      local n = tonumber(num)
      if not n then fail("bad number") end
      return math.type(n) == "float" and n == math.floor(n) and math.tointeger(n) or n
    end
  end
  local ok, v = pcall(value)
  if not ok then return nil, type(v) == "table" and v.msg or tostring(v) end
  skip()
  if pos <= #s then return nil, "extra text after the JSON value" end
  return v
end

-- Terrain brush (autotile). A terrain set is 16 tiles: a 4 x 4 block of the
-- sheet that starts at `base`. `step`: 1 cell (8x8 mode) or 2 cells (16x16
-- mode). `cpr`: cells per row of the sheet. The mask of a cell adds N=1,
-- E=2, S=4 and W=8 for each neighbour that is in the set.
function T.autotile_tile(base, mask, step, cpr)
  return base + (mask % 4) * step + (mask // 4) * step * cpr
end

local function autotile_in(t, base, step, cpr)
  local d = t - base
  if d < 0 then return false end
  local row, col = d // (step * cpr), d % (step * cpr)
  return row <= 3 and col % step == 0 and col // step <= 3 and d == col + row * step * cpr
end

-- get(x, y): the tile id of a cell (0 for none). w, h: the map size.
function T.autotile_mask(get, x, y, base, step, cpr, w, h)
  local function inset(cx, cy)
    if cx < 0 or cy < 0 or (w and cx >= w) or (h and cy >= h) then return false end
    return autotile_in(get(cx, cy), base, step, cpr)
  end
  local m = 0
  if inset(x, y - step) then m = m + 1 end
  if inset(x + step, y) then m = m + 2 end
  if inset(x, y + step) then m = m + 4 end
  if inset(x - step, y) then m = m + 8 end
  return m
end

-- Paints (or erases) the cell x, y with the set and re-tiles the cell and its
-- 4 neighbours. put(x, y, tile) writes a tile (0 clears).
function T.autotile_paint(get, put, x, y, base, step, cpr, erase, w, h)
  put(x, y, erase and 0 or base)
  local at = { { x, y }, { x, y - step }, { x + step, y }, { x, y + step }, { x - step, y } }
  for _, c in ipairs(at) do
    local cx, cy = c[1], c[2]
    if cx >= 0 and cy >= 0 and (not w or cx < w) and (not h or cy < h)
       and autotile_in(get(cx, cy), base, step, cpr) then
      put(cx, cy, T.autotile_tile(base, T.autotile_mask(get, cx, cy, base, step, cpr, w, h), step, cpr))
    end
  end
end

-- The flags of a tile come from the custom properties of a Tiled tile: a
-- boolean property called solid, platform, ladder, water or hurt (flags 0 to
-- 4), or flag0 to flag7.
local TILED_FLAGS = { solid = 0, platform = 1, ladder = 2, water = 3, hurt = 4, damage = 4 }
for i = 0, 7 do TILED_FLAGS["flag" .. i] = i end

-- Reads a Tiled map (the text of a .tmj/.json file: orthogonal, CSV tile
-- layers) into bm cells. `cols`: the columns of the sheet in 8x8 cells
-- (sheet_w // 8). Tiles of 8x8 or 16x16 pixels: a 16x16 Tiled tile becomes
-- a tile of 2 x 2 cells (n, n+1 and the two under them), as the 16x16 brush
-- (z) puts it. A Tiled tile at column c, row r of its tileset is the sheet
-- tile at the same column and row. Tiled gid 0 is empty. Returns
--   { w, h (in cells), tile = 8|16, layers = { {name=, cells={w*h}} },
--     flags = { [cell] = byte }, ignored = { "name (kind)", ... } }
-- or nil and the reason.
function T.tiled_import(text, cols)
  local m, err = T.json_decode(text)
  if not m then return nil, "not a JSON file: " .. tostring(err) end
  if type(m) ~= "table" or type(m.layers) ~= "table" then return nil, "not a Tiled map (no layers)" end
  if m.orientation and m.orientation ~= "orthogonal" then return nil, "only orthogonal maps (" .. tostring(m.orientation) .. ")" end
  if m.infinite then return nil, "infinite maps are not supported (give the map a fixed size in Tiled)" end
  local tw, th = m.tilewidth, m.tileheight
  if tw ~= th or (tw ~= 8 and tw ~= 16) then
    return nil, "tiles of 8x8 or 16x16 pixels only (" .. tostring(tw) .. "x" .. tostring(th) .. ")"
  end
  local k = tw // 8
  local mw, mh = m.width, m.height
  if math.type(mw) ~= "integer" or math.type(mh) ~= "integer" or mw < 1 or mh < 1 then return nil, "no map size" end
  if mw * k > 256 or mh * k > 256 then return nil, "the map is " .. mw * k .. "x" .. mh * k .. " cells: 256x256 at most" end
  cols = cols or 32
  local sets = {}
  for _, ts in ipairs(m.tilesets or {}) do
    if type(ts.firstgid) == "number" then sets[#sets + 1] = ts end
  end
  table.sort(sets, function(a, b) return a.firstgid < b.firstgid end)
  -- the sheet cells of a tileset's tile (its local id)
  local function cells_of(ts, id)
    local tc = ts.columns
    if math.type(tc) ~= "integer" or tc < 1 then tc = cols // k end
    local n = (id // tc) * k * cols + (id % tc) * k
    if k == 1 then return { n } end
    return { n, n + 1, n + cols, n + cols + 1 }
  end
  local flags = {}
  for _, ts in ipairs(sets) do
    for _, tl in ipairs(ts.tiles or {}) do
      local bits, props = 0, tl.properties
      if type(props) == "table" then
        if props[1] == nil then               -- (old style: { name = value })
          for name, v in pairs(props) do
            if v == true and TILED_FLAGS[name] then bits = bits | 1 << TILED_FLAGS[name] end
          end
        end
        for _, p in ipairs(props) do
          if p.value == true and TILED_FLAGS[p.name] then bits = bits | 1 << TILED_FLAGS[p.name] end
        end
      end
      if bits ~= 0 and math.type(tl.id) == "integer" then
        for _, c in ipairs(cells_of(ts, tl.id)) do flags[c] = (flags[c] or 0) | bits end
      end
    end
  end
  local out = { w = mw * k, h = mh * k, tile = tw, layers = {}, flags = flags, ignored = {} }
  local taken = {}
  local function layer_name(n)
    n = tostring(n):gsub("[^%w_%-%. ]", "_"):sub(1, 16)
    if n == "" then n = "layer" end
    local base, i = n, 1
    while taken[n] do i = i + 1; n = base:sub(1, 16 - #tostring(i)) .. i end
    taken[n] = true
    return n
  end
  local walk
  function walk(list, prefix)
    for _, ly in ipairs(list) do
      local nm = tostring(ly.name or "layer")
      if ly.type == "group" and type(ly.layers) == "table" then
        local ok, why = walk(ly.layers, prefix .. nm .. "_")
        if not ok then return nil, why end
      elseif ly.type ~= "tilelayer" then
        out.ignored[#out.ignored + 1] = nm .. " (" .. tostring(ly.type) .. ")"
      else
        if ly.chunks then return nil, "layer " .. nm .. ": infinite layers are not supported" end
        if type(ly.data) ~= "table" then
          return nil, "layer " .. nm .. ": use CSV encoding (Tiled: Map properties, Tile Layer Format: CSV)"
        end
        if #ly.data ~= mw * mh then return nil, "layer " .. nm .. ": " .. #ly.data .. " tiles, the map has " .. mw * mh end
        local cells = {}
        for i = 1, out.w * out.h do cells[i] = 0 end
        for i, g in ipairs(ly.data) do
          if math.type(g) ~= "integer" then return nil, "layer " .. nm .. ": a tile is not a number" end
          g = g & 0x0FFFFFFF                   -- (the flip bits of Tiled are dropped)
          if g > 0 then
            local ts
            for _, s2 in ipairs(sets) do if s2.firstgid <= g then ts = s2 end end
            if not ts then return nil, "layer " .. nm .. ": tile " .. g .. " has no tileset" end
            local cs = cells_of(ts, g - ts.firstgid)
            for _, c in ipairs(cs) do
              if c > 4095 then return nil, "layer " .. nm .. ": tile " .. g .. " is outside the sheet (cell " .. c .. ")" end
            end
            local x, y = (i - 1) % mw, (i - 1) // mw
            for j, c in ipairs(cs) do
              cells[(y * k + (j - 1) // k) * out.w + x * k + (j - 1) % k + 1] = c
            end
          end
        end
        out.layers[#out.layers + 1] = { name = layer_name(prefix .. nm), cells = cells }
      end
    end
    return true
  end
  local ok, why = walk(m.layers, "")
  if not ok then return nil, why end
  if #out.layers == 0 then return nil, "the map has no tile layer" end
  return out
end

-- Puts a map read by tiled_import in the project's map. mode "add": the
-- layers go after the ones there (8 in all at most) and the map grows to fit;
-- "replace": the map has only the imported layers, at their size. The tiles'
-- flags are added to the sheet's. Returns the map's width, height and number
-- of layers, or nil and the reason (nothing is changed then).
function T.tiled_apply(res, mode)
  local names = mlayers()
  local list = {}
  if mode ~= "replace" then
    if #names + #res.layers > 8 then
      return nil, "needs " .. #res.layers .. " free layers, the map has " .. 8 - #names
    end
    list = table.move(names, 1, #names, 1, {})
  elseif #res.layers > 8 then
    return nil, "the file has " .. #res.layers .. " tile layers, 8 at most"
  end
  local have, added = {}, {}
  for _, n in ipairs(list) do have[n] = true end
  for _, ly in ipairs(res.layers) do
    local n, i = ly.name, 1
    while have[n] do i = i + 1; n = ly.name:sub(1, 16 - #tostring(i)) .. i end
    have[n] = true
    list[#list + 1] = n
    added[#added + 1] = { n, ly }
  end
  local w0, h0 = msize()
  mlayers(list)
  local w, h = w0, h0
  if mode == "replace" then
    w, h = res.w, res.h
    for _, a in ipairs(added) do           -- (a layer that kept its name starts empty)
      for y = 0, h0 - 1 do for x = 0, w0 - 1 do mset(x, y, 0, a[1]) end end
    end
  else
    w, h = math.max(w0, res.w), math.max(h0, res.h)
  end
  if w ~= w0 or h ~= h0 then msize(w, h) end
  for _, a in ipairs(added) do
    for y = 0, res.h - 1 do
      for x = 0, res.w - 1 do
        local c = a[2].cells[y * res.w + x + 1]
        if c ~= 0 then mset(x, y, c, a[1]) end
      end
    end
  end
  for c, bits in pairs(res.flags) do fset(c, fget(c) | bits) end
  return w, h, #list
end

return T
