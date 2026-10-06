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
T.MODES = { [0] = "linear", "smooth", "step" }
T.LIMIT_V, T.LIMIT_T = 4096, 16384
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
  if #verts > T.LIMIT_V then return false, #verts .. " vertices, at most " .. T.LIMIT_V end
  if #tris > T.LIMIT_T then return false, #tris .. " triangles, at most " .. T.LIMIT_T end
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

function T.short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or "/carts"
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):upper():gsub("[^%w_]", "")
  if stem == "" then stem = "GAME" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".BM"
end

function T.list_files()
  local out = {}
  for _, dir in ipairs({ "/carts", "/" }) do
    for _, f in ipairs(ls(dir)) do
      if not f.dir and f.name:lower():match("%.bm$") then
        out[#out + 1] = (dir == "/" and "" or dir) .. "/" .. f.name
      end
    end
  end
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
  say("opened " .. path .. "  (" .. #parts .. " models)", C.ACC)
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
  S.proj.path = path
  S.dirty, S.sheet_dirty = false, false
  say("saved " .. path, C.ACC)
  return true
end

local ask

function T.save_as(after)
  local p = S.proj.path
  ask("file name (8.3, in /carts)", p and p:match("([^/]+)$") or "MY3D.BM", function(t)
    if t == "" then return end
    if not t:upper():match("%.BM$") then t = t .. ".BM" end
    if T.save_to(T.short_path("/carts/" .. t), p) and after then after() end
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
  camera3d(cam.tx - f[1] * d + r[1] * a + u[1] * b, cam.ty - f[2] * d + u[2] * b,
           cam.tz - f[3] * d + r[3] * a + u[3] * b, cam.yaw, cam.pitch, 60)
  cam.f, cam.r, cam.u = f, r, u
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
  local x = 0
  for _, h in ipairs(list) do
    local kx = x
    for _, k in ipairs(h[1]) do kx = kx + prompt(k) + 1 end
    if snap(kx + 2) + #h[2] * 8 > W then break end
    for _, k in ipairs(h[1]) do x = prompt(k, x, y) + 1 end
    x = print(h[2], snap(x + 2), y, C.DIM) + 12
  end
end

-- a key as a chip, or the pad's button when a pad was used last and the
-- action has one; then its label. Returns the x after it.
function T.chip_hint(key, pad, label, x, y, c)
  local li = lastinput()
  x = prompt(pad and (li == "ds4" or li == "pad") and pad or key, x, y)
  return print(label, snap(x + 3), y, c or C.DIM) + 12
end

-- a list with a title (as the lists of bm Mesh): items[i] is a string;
-- `colour_of(i)` a dot before it
function T.draw_list(title, items, sel, x, y, rows, w, colour_of)
  w = w or 168
  print(title, x + 16, y, C.DIM)
  local first = clamp(sel - rows // 2, 1, max(1, #items - rows + 1))
  for i = first, min(#items, first + rows - 1) do
    local yy = y + 16 + (i - first) * 16
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
  print(nv .. " vertices", x, y, nv > T.LIMIT_V and C.ERR or C.TEXT)
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
  elseif k == "\b" then input.text = input.text:sub(1, -2)
  elseif #k == 1 and k:byte() >= 32 and #input.text < 40 then input.text = input.text .. k end
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
  rectfill(80, 144, 480, 64, C.PANEL)
  rect(80, 144, 480, 64, C.ACC)
  print(input.label, 96, 160, C.DIM)
  print(input.text .. ((S.frame // 20) % 2 == 0 and "_" or ""), 96, 176, C.TEXT)
  T.chip_hint("esc", nil, "cancel", T.chip_hint("enter", nil, "ok", 336, 144), 144)
end

local function draw_choose()
  local p = choosing
  local rows = min(#p.rows, 14)
  local h = (rows + 2) * 16
  local y0 = max(32, (H - h) // 32 * 16)
  rectfill(40, y0, 560, h, C.PANEL)
  rect(40, y0, 560, h, C.ACC)
  local tx = print(p.title, 56, y0, C.ACC) + 16
  T.chip_hint("esc", "B", "back", T.chip_hint("enter", "A", "choose", tx, y0), y0)
  local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
  for i = first, min(#p.rows, first + rows - 1) do
    local y = y0 + 16 + (i - first) * 16
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
  add({ { "Y LEFTRIGHT", "page" }, { "Y B", "menu" }, { "Y UPDOWN", "model" }, { "Y A", "undo" },
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
    if assist.update() then return end          -- the assistant has the keys
    T.picture_update()                          -- a model on its way from a picture

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
    local pg = A.pages[S.page]
    if pg then pg.draw() else draw_menu() end

    -- tab bar: each page with its key
    rectfill(0, 0, W, 16, C.BAR)
    local x = 4
    local tabs = {}
    for _, id in ipairs(A.order) do tabs[#tabs + 1] = { id, A.pages[id].fkey, A.pages[id].label } end
    tabs[#tabs + 1] = { "menu", "esc", "menu" }
    for _, t in ipairs(tabs) do
      local lx = snap(x + prompt(t[2]) + 3)            -- the label on its column
      local w = lx + #t[3] * 8 - x
      if t[1] == S.page then rectfill(x - 4, 0, w + 8, 16, C.SEL) end
      prompt(t[2], x, 0)
      print(t[3], lx, 0, t[1] == S.page and 0xFFFFFF or C.DIM)
      x = x + w + 20
    end
    local name = (S.proj.path or "untitled") .. (S.dirty and "*" or "")
    local nx = W - #name * 8 - 8
    if snap(x + 4) + #A.name * 8 + 16 <= nx then print(A.name, snap(x + 4), 0, C.ACC) end   -- when it fits
    print(name, nx, 0, S.dirty and C.ACC or C.DIM)

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
  end
end

return T
