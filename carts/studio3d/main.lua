-- bm 3D studio: the 3D models and animations of a .bm, on the console.
-- F1 play (the player), F2 build (blocks and tiles), F3 rig (bones and
-- skin), F4 animate (keyframes), Esc menu; Ctrl+S save, F5 try the game.
-- Hold F12 for the keys of the page. Gamepad: Y + left/right page,
-- Y + B menu, Y + up/down model, Y + A undo.
-- The same formats as bm Studio and bm Animator on the PC: the MESH (6)
-- and ANIM (7) sections of the .bm (src/bm/bm.h), read and written here
-- with string.pack and handed to the kernel with cart_data().

local W, H = SCREEN_W, SCREEN_H
local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x707890, 0xFFC050, 0xFF6060, 0x3050A0
local C_SKY, C_GRID, C_GRID0, C_CUR = 0x262C3E, 0x3A4258, 0x56607C, 0xFFE070
local FOCAL = (W / 2) / math.tan(math.rad(30))   -- camera3d with fov 60
local FPS = 12                                   -- the timeline's frames, as in bm Animator
local TEXTURED = 0x80000000
local MODES = { [0] = "linear", "smooth", "step" }
local LIMIT_V, LIMIT_T = 4096, 16384
local PANEL_W = 168                              -- the list on the left of play, rig, animate
-- text on rows of 16 pixels and columns of 8: the hint row, the status bar
local HINT_Y, STATUS_Y = H - 40, H - 24
local INFO_X = PANEL_W + 16

local floor, abs, sqrt, sin, cos = math.floor, math.abs, math.sqrt, math.sin, math.cos
local max, min, pi = math.max, math.min, math.pi
local spack, sunpack = string.pack, string.unpack

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end
local function round(v) return floor(v + 0.5) end

local PALETTE = {
  0x000000, 0x1C2030, 0x404450, 0x808490, 0xC0C4D0, 0xFFFFFF, 0x5A2A22, 0xA84632, 0xE84A5A, 0xE890B0,
  0xF09030, 0xF0D040, 0xFFF4C0, 0x8A5A30, 0xC49A5E, 0xE0C888, 0x285A28, 0x3E8A3A, 0x5CB048, 0xA8E070,
  0x1E4E6E, 0x2E6EB8, 0x3478C4, 0x7ABCE8, 0x2E8A70, 0x60D0C0, 0x3A2A6A, 0x6A50C8, 0xB060D8, 0xFFC050,
}

-- the Lua of a new project: the viewer of bm Studio (the same text, so the
-- PC tools know it is theirs to replace)
local VIEWER = [[
-- bm Studio: viewer of the 3D models of this cartridge.
-- Left / right: model; up / down: closer / farther; A: light; B: spin;
-- X: the next animation (models with a skeleton, from bm Animator).
-- Replace it with your game: model("name") gives a model as a mesh for
-- draw3d(), models() the list of names, bounds3d(mesh) its size,
-- animate(mesh, "walk", t) the pose of an animation at time t.

local names, meshes, boxes, anims = {}, {}, {}, {}
local cur, zoom, spin, lit, angle, clip, t = 1, 1, true, true, 0.6, 1, 0

function _init()
  names = models()
  for i, n in ipairs(names) do
    meshes[i] = model(n)
    anims[i] = clips(meshes[i])
    local x0, y0, z0, x1, y1, z1 = bounds3d(meshes[i])
    boxes[i] = { (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2,
                 math.max(x1 - x0, y1 - y0, z1 - z0, 0.5) }
  end
end

function _update()
  t = t + 1 / 60
  if #names == 0 then return end
  if btnp(0) then cur = (cur - 2) % #names + 1; clip = 1 end
  if btnp(1) then cur = cur % #names + 1; clip = 1 end
  if btn(2) then zoom = math.max(0.3, zoom - 0.02) end
  if btn(3) then zoom = math.min(4, zoom + 0.02) end
  if btnp(4) then lit = not lit end
  if btnp(5) then spin = not spin end
  if btnp(6) and #anims[cur] > 0 then clip = clip % #anims[cur] + 1; t = 0 end
  if spin then angle = angle + 0.01 end
end

function _draw()
  cls(0x1C2030)
  if #names == 0 then
    print("no 3D models yet: make them with bm Studio", 16, 16, 0xFFFFFF)
    return
  end
  local b, a = boxes[cur], anims[cur]
  if #a > 0 then animate(meshes[cur], a[clip].name, t) end
  local d = b[4] * 1.6 * zoom
  zclear()
  camera3d(0, d * 0.55, -d, 0, -0.5, 60)
  light3d(-0.4, 0.8, -0.5, lit and 0.4 or 1)
  -- turn around the middle of the model: move it there, then spin
  local c, s = math.cos(angle), math.sin(angle)
  local x = -(b[1] * c + b[3] * s)
  local z = -(-b[1] * s + b[3] * c)
  draw3d(meshes[cur], x, -b[2], z, 0, angle, 0, 1, lit and 0 or 2)
  print(names[cur] .. "  " .. cur .. "/" .. #names .. (#a > 0 and ("  " .. a[clip].name) or ""), 8, 6, 0xFFFFFF)
  print(stat(4) .. " triangles  " .. stat(2) .. " fps", 8, 24, 0x8890A8)
  print("left/right model  up/down zoom  A light  B spin" .. (#a > 0 and "  X animation" or ""), 8, SCREEN_H - 20, 0x8890A8)
end
]]

----------------------------------------------------------------- vectors

local function vsub(a, b) return { a[1] - b[1], a[2] - b[2], a[3] - b[3] } end
local function vadd(a, b) return { a[1] + b[1], a[2] + b[2], a[3] + b[3] } end
local function vscale(a, s) return { a[1] * s, a[2] * s, a[3] * s } end
local function vdot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
local function vcross(a, b) return { a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1] } end
local function vnorm(a)
  local l = sqrt(vdot(a, a))
  if l < 1e-12 then return { 0, 0, 0 } end
  return { a[1] / l, a[2] / l, a[3] / l }
end
local function vcopy(a) return { a[1], a[2], a[3] } end
local function unit(k, s) local a = { 0, 0, 0 }; a[k] = s or 1; return a end

local function face_normal(f)
  local p = f.p
  local n = vcross(vsub(p[2], p[1]), vsub(p[3], p[1]))
  if #p == 4 then n = vadd(n, vcross(vsub(p[3], p[1]), vsub(p[4], p[1]))) end
  return vnorm(n)
end

local function face_center(f)
  local c, n = { 0, 0, 0 }, #f.p
  for _, p in ipairs(f.p) do c[1] = c[1] + p[1]; c[2] = c[2] + p[2]; c[3] = c[3] + p[3] end
  return { c[1] / n, c[2] / n, c[3] / n }
end

local function pkey(p)
  return round(p[1] * 1e4) .. "," .. round(p[2] * 1e4) .. "," .. round(p[3] * 1e4)
end

----------------------------------------------------------------- quaternions {x, y, z, w}

local function qmul(a, b)
  return { a[4] * b[1] + a[1] * b[4] + a[2] * b[3] - a[3] * b[2],
           a[4] * b[2] - a[1] * b[3] + a[2] * b[4] + a[3] * b[1],
           a[4] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[4],
           a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3] }
end

local function qnorm(q)
  local l = sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3] + q[4] * q[4])
  if l < 1e-12 then return { 0, 0, 0, 1 } end
  return { q[1] / l, q[2] / l, q[3] / l, q[4] / l }
end

local function qaxis(axis, a)
  local s = sin(a / 2)
  return { axis[1] * s, axis[2] * s, axis[3] * s, cos(a / 2) }
end

-- the shorter way from a to b: the same as the kernel (runtime.c)
local function qslerp(a, b, u)
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
  return qnorm({ a[1] * k0 + s * b[1] * k1, a[2] * k0 + s * b[2] * k1,
                 a[3] * k0 + s * b[3] * k1, a[4] * k0 + s * b[4] * k1 })
end

----------------------------------------------------------------- MESH and ANIM (src/bm/bm.h)

local function name16(s)
  s = s:sub(1, 16)
  return s .. string.rep("\0", 16 - #s)
end

local function cname(b, pos) return b:sub(pos, pos + 15):match("^[^\0]*") end

local QUAD, TRI = { { 1, 2, 3 }, { 1, 3, 4 } }, { { 1, 2, 3 } }

-- a model's part of the MESH section: m.mc, with m.nv vertices, m.nt
-- triangles and m.vb (the bone of each vertex, one byte each); nil if the
-- model has no faces. Corners of different bones stay apart.
local function encode_mesh(m)
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
  if #verts > LIMIT_V then return false, #verts .. " vertices, at most " .. LIMIT_V end
  if #tris > LIMIT_T then return false, #tris .. " triangles, at most " .. LIMIT_T end
  m.mc = name16(m.name) .. spack("<I2I2I4", #verts, #tris, 0) .. table.concat(verts) .. table.concat(tris)
  m.vb, m.nv, m.nt = table.concat(vb), #verts, #tris
  return true
end

-- the model's rig in the ANIM section: m.ac (after encode_mesh)
local function encode_anim(m)
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
local function split_mesh(bin)
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

local function split_anim(bin)
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

local function decode_rig(ac)
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
local function decode_model(mc, ac)
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
    local r, b, n = decode_rig(ac)
    if n == nv then rig, vb = r, b end
  end
  local function tri(t)
    local f = { p = { vcopy(verts[t[1]]), vcopy(verts[t[2]]), vcopy(verts[t[3]]) } }
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
      if vdot(face_normal(a), face_normal(b)) > 0 then
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

----------------------------------------------------------------- the project

local proj = { title = "New 3D project", author = "", res = "640x360", lua = VIEWER, save = nil, path = nil }
local models = {}          -- { name, faces, rig, mc, ac, vb, nv, nt, dirty }
local cur = 1
local inset = 0.25         -- sheet pixels (MESH header)
local view = nil           -- the current model as the kernel draws it (model())
local dirty = false
local page = "menu"
local msg, msg_c, msg_t = nil, C_TEXT, 0
local frame = 0
local starter = nil        -- the tiles of a new project (this cartridge's own sheet)

local function say(s, c, t) msg, msg_c, msg_t = s, c or C_TEXT, t or 200 end

local function M() return models[cur] end
local function rig() return models[cur] and models[cur].rig end

local function unique_name(base, skip)
  base = base:sub(1, 16)
  local function used(n)
    for i, m in ipairs(models) do if m.name == n and i ~= skip then return true end end
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
local function sync()
  local mparts, aparts = {}, {}
  for _, m in ipairs(models) do
    if m.dirty then
      local ok, e = encode_mesh(m)
      if not ok then return false, m.name .. ": " .. e end
      encode_anim(m)
      m.dirty = false
    elseif m.adirty then
      encode_anim(m)
    end
    m.adirty = false
    if m.mc then
      mparts[#mparts + 1] = m.mc
      if m.ac then aparts[#aparts + 1] = m.ac end
    end
  end
  local mesh = #mparts > 0 and spack("<I2I2I4", #mparts, clamp(round(inset * 256), 0, 65535), 0) .. table.concat(mparts) or nil
  local anim = #aparts > 0 and spack("<I2I2I4", #aparts, 0, 0) .. table.concat(aparts) or nil
  local ok, e = cart_data(6, mesh)
  if ok then ok, e = cart_data(7, anim) end
  if not ok then return false, e end
  view = M() and M().mc and model(M().name) or nil
  return true
end

local function select_model(i)
  if #models == 0 then cur = 1; view = nil; return end
  cur = (i - 1) % #models + 1
  view = M().mc and model(M().name) or nil
end

local function new_model(name)
  models[#models + 1] = { name = unique_name(name or "model"), faces = {}, dirty = true }
  return #models
end

----------------------------------------------------------------- undo

-- a snapshot is the model's own bytes (already made for the kernel), so
-- taking one costs nothing; the faces come back by decoding them
local undo, redo = {}, {}

local function deep(t)
  if type(t) ~= "table" then return t end
  local o = {}
  for k, v in pairs(t) do o[k] = deep(v) end
  return o
end

local function snapshot()
  local m = M()
  if m.dirty or m.adirty then sync() end
  return { idx = cur, name = m.name, mc = m.mc, ac = m.ac, rig = not m.mc and deep(m.rig) or nil }
end

local function restore(s)
  local m = models[s.idx]
  if not m or m.name ~= s.name then return false end
  if s.mc then
    m.faces, m.rig = decode_model(s.mc, s.ac)
  else
    m.faces, m.rig = {}, deep(s.rig)
  end
  m.dirty = true
  cur = s.idx
  return true
end

local function begin_edit()
  undo[#undo + 1] = snapshot()
  if #undo > 40 then table.remove(undo, 1) end
  redo = {}
end

-- after an edit: the kernel gets the new sections; too big: undone
local function commit()
  M().dirty = true
  local ok, e = sync()
  if not ok then
    local s = table.remove(undo)
    if s then restore(s); sync() end
    say("cannot: " .. tostring(e), C_ERR)
    return false
  end
  dirty = true
  return true
end

-- the same for the skeleton and animations only (the faces stay)
local function commit_anim()
  M().adirty = true
  local ok, e = sync()
  if not ok then say("cannot: " .. tostring(e), C_ERR); return false end
  dirty = true
  return true
end

local function do_undo(stack, other, what)
  local s = table.remove(stack)
  if not s then say("nothing to " .. what, C_DIM); return end
  if not models[s.idx] or models[s.idx].name ~= s.name then say("cannot " .. what .. ": the model changed", C_ERR); return end
  cur = s.idx
  other[#other + 1] = snapshot()
  restore(s)
  sync()
  dirty = true
  say(what .. ": " .. M().name, C_ACC, 90)
end

----------------------------------------------------------------- files

local function short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or "/carts"
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):upper():gsub("[^%w_]", "")
  if stem == "" then stem = "GAME" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".BM"
end

local function list_files()
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

local reset_pages            -- defined with the pages
local last_page = "play"     -- the page the menu goes back to

local function load_project(path)
  local p, e = cart_load(path)
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C_ERR); return false end
  proj = { title = p.title, author = p.author, res = p.res, lua = p.lua, path = path, save = short_path(path),
           sheet_w = p.sheet_w, sheet_h = p.sheet_h }
  local parts, ins = split_mesh(cart_data(6))
  local rigs = split_anim(cart_data(7))
  inset = ins
  models = {}
  for _, part in ipairs(parts) do
    local name, mc = part[1], part[2]
    local faces, r = decode_model(mc, rigs[name])
    local m = { name = name, faces = faces, rig = r, mc = mc, ac = r and rigs[name] or nil, dirty = false }
    m.nv, m.nt = sunpack("<I2I2", mc, 17)
    if r then
      local vb = {}
      for i = 1, m.nv do vb[i] = string.char(rigs[name]:byte(25 + #r.bones * 44 + i - 1)) end
      m.vb = table.concat(vb)
    end
    models[#models + 1] = m
  end
  undo, redo = {}, {}
  dirty = false
  if #models == 0 then new_model("model") end
  local ok, err = sync()
  if not ok then say("broken models: " .. tostring(err), C_ERR) end
  select_model(1)
  reset_pages()
  say("opened " .. path .. "  (" .. #parts .. " models; saves as " .. proj.save .. ")", C_ACC)
  return true
end

local function new_project()
  cart_new()
  if starter then
    local i = 0
    for y = 0, 47 do
      for x = 0, 255 do
        i = i + 1
        local c = starter[i]
        if c then sset(x, y, c) end
      end
    end
  end
  proj = { title = "New 3D project", author = "", res = "640x360", lua = VIEWER, save = nil, path = nil }
  models, undo, redo, inset = {}, {}, {}, 0.25
  new_model("model")
  sync()
  select_model(1)
  reset_pages()
  dirty = false
  say("new project: build with F2; Esc > Save as gives it a name", C_ACC)
end

local function save_project()
  if not proj.save then say("no name yet: Esc > Save as", C_ERR); return false end
  local ok, e = sync()
  if not ok then say("cannot save: " .. tostring(e), C_ERR); return false end
  ok, e = cart_save(proj.save, { title = proj.title, author = proj.author, res = proj.res, lua = proj.lua })
  if ok then dirty = false; say("saved " .. proj.save, C_ACC) else say("save failed: " .. tostring(e), C_ERR) end
  return ok
end

local function run_project()
  if not proj.save then say("give the project a name first: Esc > Save as", C_ERR); return end
  if save_project() then
    save({ page = last_page, model = cur })   -- the page to come back to
    cart_run(proj.save)
  end
end

----------------------------------------------------------------- input

local held, rp = {}, {}
local xy_down, xy_combo, tap = {}, {}, {}
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

----------------------------------------------------------------- 3D helpers

-- an orbit camera: target, yaw, pitch, distance; the target shows `dx`
-- pixels right of the middle of the screen and `dy` below it (the list on
-- the left, the text at the top)
local function look(cam, dx, dy)
  local cp, sp = cos(cam.pitch), sin(cam.pitch)
  local sy, cy = sin(cam.yaw), cos(cam.yaw)
  local f = { cp * sy, sp, cp * cy }
  local r = { cy, 0, -sy }
  local u = { -sp * sy, cp, -sp * cy }
  local d = cam.dist
  local a, b = -(dx or 0) * d / FOCAL, (dy or 0) * d / FOCAL
  camera3d(cam.tx - f[1] * d + r[1] * a + u[1] * b, cam.ty - f[2] * d + u[2] * b,
           cam.tz - f[3] * d + r[3] * a + u[3] * b, cam.yaw, cam.pitch, 60)
  cam.f, cam.r = f, r
end

local function scr(p)
  local x, y = project3d(p[1], p[2], p[3])
  if x and abs(x) < 4000 and abs(y) < 4000 then return x, y end
end

local function seg(a, b, c)
  local ax, ay = scr(a)
  local bx, by = scr(b)
  if ax and bx then line(ax, ay, bx, by, c) end
end

-- the world axis nearest to where the camera looks, and the one to its
-- right (never the same: at 45 degrees one is x, the other z)
local function view_axes(yaw)
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

-- a hue for each bone (as bm Animator)
local function bone_colour(i)
  local h, s, v = ((i - 1) * 0.618034) % 1, 0.65, 0.95
  local function k(n)
    local x = (n + h * 6) % 6
    return v - v * s * max(0, min(1, min(x, 4 - x)))
  end
  return (round(k(5) * 255) << 16) | (round(k(3) * 255) << 8) | round(k(1) * 255)
end

local function draw_bone(h, t, c, chosen)
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
local function draw_grid(cx, level, cz, n, step, c)
  for i = -n, n do
    local x, z = cx + i * step, cz + i * step
    seg({ x, level, cz - n * step }, { x, level, cz + n * step }, c)
    seg({ cx - n * step, level, z }, { cx + n * step, level, z }, c)
  end
end

local function bounds_of(faces)
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

local function aim(cam, faces, k)
  local lo, hi = bounds_of(faces)
  cam.floor = lo[2]                    -- the grid goes under the model
  cam.tx, cam.ty, cam.tz = (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, (lo[3] + hi[3]) / 2
  cam.size = max(hi[1] - lo[1], hi[2] - lo[2], hi[3] - lo[3], 0.5)
  cam.dist = cam.size * (k or 1.8)
end

local function draw_list(title, items, sel, x, y, rows, colour_of)
  print(title, x + 16, y, C_DIM)
  local first = clamp(sel - rows // 2, 1, max(1, #items - rows + 1))
  for i = first, min(#items, first + rows - 1) do
    local yy = y + 16 + (i - first) * 16
    if i == sel then rectfill(x, yy, PANEL_W, 16, C_SEL) end
    if colour_of then rectfill(x + 4, yy + 4, 6, 8, colour_of(i)) end
    print(items[i]:sub(1, 18), x + 16, yy, i == sel and 0xFFFFFF or C_TEXT)
  end
  if first > 1 then print("^", x + PANEL_W - 12, y + 16, C_DIM) end
  if first + rows - 1 < #items then print("v", x + PANEL_W - 12, y + rows * 16, C_DIM) end
end

local function panel(x, y, w, h)
  rectfill(x, y, w, h, C_PANEL)
end

-- the strip behind the lines of text over the 3D view
local function info_strip(lines)
  rectfill(PANEL_W, 16, W - PANEL_W, 16 + lines * 16, C_BG)
end

local function hint(s)
  print(s:sub(1, 79), 0, HINT_Y, C_DIM)
end

-- the pages: what each one gives the others (the rest stays inside its block)
local last_t = 0
local pl, pl_clips, play_reset, play_key, play_pad, play_update, draw_play
local bd, brush, picker, build_reset, build_key, build_pad, build_update, draw_build
local rg, make_skin_mesh, rig_reset, rig_key, rig_pad, draw_rig, bone_depth
local an, anim_reset, anim_key, anim_pad, anim_update, draw_anim

----------------------------------------------------------------- play page (the player)
do

pl = { clip = 1, t = 0, playing = true, speed = 1, bones = false, lit = true, spin = true, blend = 0,
             cam = { yaw = 0.6, pitch = -0.35, dist = 4, tx = 0, ty = 0.5, tz = 0 } }
pl_clips = {}

function play_reset()
  pl.clip, pl.t, pl.blend = 1, 0, 0
  pl_clips = view and clips(view) or {}
  aim(pl.cam, M() and M().faces or {}, 2.7)
end

function play_key(k)
  local cam = pl.cam
  if k == "up" then select_model(cur - 1); play_reset()
  elseif k == "down" then select_model(cur + 1); play_reset()
  elseif k == "left" or k == "right" then
    if #pl_clips > 0 then
      pl.clip = (pl.clip - 1 + (k == "right" and 1 or -1)) % #pl_clips + 1
      pl.t = 0
    end
  elseif k == " " or k == "\n" then pl.playing = not pl.playing
  elseif k == "a" then cam.yaw = cam.yaw - 0.15; pl.spin = false
  elseif k == "d" then cam.yaw = cam.yaw + 0.15; pl.spin = false
  elseif k == "w" then cam.pitch = clamp(cam.pitch - 0.1, -1.45, 1.2)
  elseif k == "s" then cam.pitch = clamp(cam.pitch + 0.1, -1.45, 1.2)
  elseif k == "+" or k == "=" then cam.dist = max(0.3, cam.dist * 0.85)
  elseif k == "-" then cam.dist = min(200, cam.dist / 0.85)
  elseif k == "o" then pl.spin = not pl.spin
  elseif k == "k" then pl.bones = not pl.bones
  elseif k == "l" then pl.lit = not pl.lit
  elseif k == "b" then pl.blend = (pl.blend + 0.25) % 1.25; if pl.blend > 1 then pl.blend = 0 end
  elseif k == "," then pl.playing = false; pl.t = pl.t - 1 / FPS
  elseif k == "." then pl.playing = false; pl.t = pl.t + 1 / FPS
  elseif k == "<" then pl.speed = max(0.125, pl.speed / 2)
  elseif k == ">" then pl.speed = min(4, pl.speed * 2)
  elseif k == "f" then aim(pl.cam, M().faces, 2.7)
  end
end

function play_pad()
  local cam = pl.cam
  if btn(6) then                                  -- X + croce: turn and tilt, X + A / B: zoom
    if btn(0) then cam.yaw = cam.yaw - 0.04; pl.spin = false end
    if btn(1) then cam.yaw = cam.yaw + 0.04; pl.spin = false end
    if btn(2) then cam.pitch = clamp(cam.pitch - 0.03, -1.45, 1.2) end
    if btn(3) then cam.pitch = clamp(cam.pitch + 0.03, -1.45, 1.2) end
    if btn(4) then cam.dist = max(0.3, cam.dist * 0.98) end
    if btn(5) then cam.dist = min(200, cam.dist / 0.98) end
    return
  end
  if rp[2] then play_key("up") end
  if rp[3] then play_key("down") end
  if rp[0] then play_key("left") end
  if rp[1] then play_key("right") end
  if btnp(4) then play_key(" ") end
  if btnp(5) then pl.bones = not pl.bones end
  if tap[6] then pl.spin = not pl.spin end
end

function play_update(dt)
  if pl.spin then pl.cam.yaw = pl.cam.yaw + 0.008 end
  if pl.playing then pl.t = pl.t + dt * pl.speed end
end


function draw_play()
  local m, cam = M(), pl.cam
  cls(C_SKY)
  zclear()
  look(cam, PANEL_W / 2, 28)
  if m and #m.faces > 0 then
    local step = cam.size > 6 and 1 or (cam.size > 2 and 0.5 or 0.25)
    draw_grid(round(cam.tx / step) * step, cam.floor, round(cam.tz / step) * step, 8, step, C_GRID)
  end
  local c = pl_clips[pl.clip]
  if view then
    if c then
      if pl.blend > 0 and #pl_clips > 1 then
        local c2 = pl_clips[pl.clip % #pl_clips + 1]
        animate(view, pl.clip, pl.t, pl.clip % #pl_clips + 1, pl.t * c2.length / max(c.length, 1e-3), pl.blend)
      else
        animate(view, pl.clip, pl.t)
      end
    end
    light3d(-0.4, 0.8, -0.5, pl.lit and 0.35 or 1)
    draw3d(view, 0, 0, 0, 0, 0, 0, 1, pl.lit and 0 or 2)
    if pl.bones and m.rig then
      for i, b in ipairs(m.rig.bones) do
        local hx, hy, hz, tx, ty, tz = bone3d(view, i)
        draw_bone({ hx, hy, hz }, { tx, ty, tz }, bone_colour(i), false)
      end
    end
  else
    print("this model has no faces yet: build it with F2", INFO_X, 160, C_DIM)
  end
  -- the lists
  panel(0, 16, PANEL_W, HINT_Y - 16)
  local names = {}
  for i, mm in ipairs(models) do names[i] = mm.name end
  local rows = #pl_clips > 0 and min(8, #names) or 16
  draw_list("MODELS", names, cur, 0, 32, rows)
  if #pl_clips > 0 then
    local cn = {}
    for i, cc in ipairs(pl_clips) do cn[i] = cc.name end
    local y = 32 + (rows + 2) * 16
    draw_list("ANIMATIONS", cn, pl.clip, 0, y, max(1, min(#cn, (HINT_Y - y - 16) // 16)))
  end
  -- what it is
  local x = INFO_X
  info_strip(c and pl.blend > 0 and #pl_clips > 1 and 4 or 3)
  if m then
    print(m.name, x, 32, C_ACC)
    print((m.nv or 0) .. " vertices, " .. (m.nt or 0) .. " triangles" ..
          (m.rig and (", " .. #m.rig.bones .. " bones") or ""), x, 48, C_TEXT)
    if c then
      local len = max(c.length, 1e-3)
      local tt = c.loop and (pl.t % len) or clamp(pl.t, 0, len)
      print(string.format("%s %s  %.2f / %.2f s  x%g%s", pl.playing and ">" or "||", c.name, tt, c.length,
                          pl.speed, c.loop and "  loop" or ""), x, 64, C_TEXT)
      if pl.blend > 0 and #pl_clips > 1 then
        print(string.format("mixed with %s: %d%%", pl_clips[pl.clip % #pl_clips + 1].name, round(pl.blend * 100)),
              x, 80, C_ACC)
      end
    elseif m.rig then
      print("a skeleton with no animations yet (F4)", x, 64, C_DIM)
    end
  end
  print(string.format("%d fps  %.1f ms", stat(2), stat(1)), W - 128, 32, C_DIM)
  hint("up/down model  left/right anim  space play  a d w s view  + - zoom  k bones")
end
end

----------------------------------------------------------------- build page
do

bd = { cell = { 0, 0, 0 }, tool = 1, side = 1, grid = true,
             cam = { yaw = 0, pitch = -0.6, dist = 8, tx = 0.5, ty = 0.5, tz = 0.5 }, yaw_to = 0 }
local TOOLS = { "block", "tile", "paint" }
local SIDES = { "floor", "far wall", "right wall", "near wall", "left wall", "ceiling", "all sides" }
brush = { c = nil, rect = { 0, 0, 16, 16 }, rot = 0, flip = false }
picker = { open = false, x = 0, y = 0, size = 16, colours = false, pal = 1, top = 0, left = 0 }

-- texture corners of a rect in corner order (bottom left, top left, top
-- right, bottom right), flipped, then turned `rot` quarter turns (edit.js)
local function rect_uv(rect, rot, flip)
  local x, y, w, h = rect[1], rect[2], rect[3], rect[4]
  local b = { { x, y + h }, { x, y }, { x + w, y }, { x + w, y + h } }
  if flip then b = { b[4], b[3], b[2], b[1] } end
  local r = rot % 4
  local out = {}
  for k = 0, 3 do local q = b[(k - r) % 4 + 1]; out[k + 1] = { q[1], q[2] } end
  return out
end

-- up on the floor: the horizontal axis nearest to where the camera looks,
-- so a tile shows the right way up (edit.js floorUp)
local function floor_up(f)
  if abs(f[1]) > abs(f[3]) then return { f[1] > 0 and 1 or -1, 0, 0 } end
  return { 0, 0, f[3] < 0 and -1 or 1 }
end

-- the tile on the plane axis = level, over cell c, showing towards n (+1/-1)
local function tile_face(c, axis, level, n, b)
  local nv = unit(axis, n)
  local U = axis == 2 and floor_up(bd.cam.f or { 0, 0, 1 }) or { 0, 1, 0 }
  local R = vcross(nv, U)
  local m = { c[1] + 0.5, c[2] + 0.5, c[3] + 0.5 }
  m[axis] = level
  local function h(a, bb)
    return { m[1] + (a * R[1] + bb * U[1]) / 2, m[2] + (a * R[2] + bb * U[2]) / 2, m[3] + (a * R[3] + bb * U[3]) / 2 }
  end
  local f = { p = { h(-1, -1), h(-1, 1), h(1, 1), h(1, -1) } }
  if b.c then f.c = b.c else f.uv = rect_uv(b.rect, b.rot, b.flip) end
  return f
end

local function spot_key(f)
  local ks = {}
  for i, p in ipairs(f.p) do ks[i] = pkey(p) end
  table.sort(ks)
  return table.concat(ks, ";")
end

-- adds faces: one on the same spot facing the same way is replaced (a new
-- texture); with `cancel`, one facing the other way goes away with the new
-- one (two blocks side by side have no wall between them)
local function place_faces(faces, added, cancel)
  local index = {}
  for i, f in ipairs(faces) do
    local k = spot_key(f)
    index[k] = index[k] or {}
    table.insert(index[k], i)
  end
  local remove = {}
  for _, nf in ipairs(added) do
    local k, n = spot_key(nf), face_normal(nf)
    local facing, opposite
    for _, i in ipairs(index[k] or {}) do
      if not remove[i] then
        local d = vdot(face_normal(faces[i]), n)
        if d > 0.9 then facing = i elseif d < -0.9 then opposite = i end
      end
    end
    if cancel and opposite then
      remove[opposite] = true
    elseif facing then
      local f = faces[facing]
      f.p, f.uv, f.c = nf.p, nf.uv, nf.c
    else
      faces[#faces + 1] = nf
      index[k] = index[k] or {}
      table.insert(index[k], #faces)
    end
  end
  if next(remove) then
    local keep = {}
    for i, f in ipairs(faces) do if not remove[i] then keep[#keep + 1] = f end end
    for i = 1, #faces do faces[i] = keep[i] end
  end
end

-- the plane of a side of cell c: axis, level and the way a tile there
-- faces (into the cell); for the walls the view decides which is "far"
local function side_plane(c, s)
  if s == 1 then return 2, c[2], 1 end
  if s == 6 then return 2, c[2] + 1, -1 end
  local fa, fs, ra, rs = view_axes(bd.yaw_to)
  local a, d
  if s == 2 then a, d = fa, fs elseif s == 4 then a, d = fa, -fs
  elseif s == 3 then a, d = ra, rs else a, d = ra, -rs end
  return a, c[a] + (d > 0 and 1 or 0), -d
end

-- does face f lie on the square of the plane axis = level over cell c?
local function on_square(f, c, axis, level)
  for _, p in ipairs(f.p) do
    if abs(p[axis] - level) > 1e-3 then return false end
  end
  local m = face_center(f)
  for k = 1, 3 do
    if k ~= axis and (m[k] < c[k] - 1e-3 or m[k] > c[k] + 1 + 1e-3) then return false end
  end
  return true
end

-- the faces on the chosen sides of cell c: { index, ... }
local function faces_on(faces, c, s, outward_only)
  local out = {}
  local planes = {}
  if s == 7 then
    for k = 1, 3 do
      planes[#planes + 1] = { k, c[k], -1 }
      planes[#planes + 1] = { k, c[k] + 1, 1 }
    end
  else
    local a, l, n = side_plane(c, s)
    planes[1] = { a, l, -n }
  end
  for i, f in ipairs(faces) do
    for _, pl3 in ipairs(planes) do
      if on_square(f, c, pl3[1], pl3[2]) and (not outward_only or face_normal(f)[pl3[1]] * pl3[3] > 0.9) then
        out[#out + 1] = i
        break
      end
    end
  end
  return out
end

local function block_faces(c, b)
  local out = {}
  for k = 1, 3 do
    for _, s in ipairs({ -1, 1 }) do
      out[#out + 1] = tile_face(c, k, c[k] + (s > 0 and 1 or 0), s, b)
    end
  end
  return out
end

-- the brush of a face (its colour, or the rectangle of its texture)
local function brush_of(f)
  if f.c then return { c = f.c, rect = brush.rect, rot = 0, flip = false } end
  local u0, v0, u1, v1 = 1e9, 1e9, -1e9, -1e9
  for _, q in ipairs(f.uv) do
    u0, v0, u1, v1 = min(u0, q[1]), min(v0, q[2]), max(u1, q[1]), max(v1, q[2])
  end
  return { rect = { u0, v0, max(1, u1 - u0), max(1, v1 - v0) }, rot = 0, flip = false }
end

local function remove_faces(faces, list)
  local gone = {}
  for _, i in ipairs(list) do gone[i] = true end
  local keep = {}
  for i, f in ipairs(faces) do if not gone[i] then keep[#keep + 1] = f end end
  return keep
end

local function is_block(faces, c)
  return #faces_on(faces, c, 7, true) > 0
end

local function build_action(erase)
  local m = M()
  if not m then return end
  local c = bd.cell
  if bd.tool == 1 then
    if not erase then
      begin_edit()
      place_faces(m.faces, block_faces(c, brush), true)
      commit()
      return
    end
    if not is_block(m.faces, c) then say("no block here", C_DIM, 60); return end
    begin_edit()
    -- its own walls go; where a neighbour touched it, the neighbour's wall
    -- shows again (with the texture of one of its faces)
    local walls = faces_on(m.faces, c, 7, true)
    local had = {}
    for _, i in ipairs(walls) do
      local f = m.faces[i]
      local n = face_normal(f)
      for k = 1, 3 do if abs(n[k]) > 0.9 then had[k * 2 + (n[k] > 0 and 1 or 0)] = true end end
    end
    m.faces = remove_faces(m.faces, walls)
    local add = {}
    for k = 1, 3 do
      for _, s in ipairs({ -1, 1 }) do
        if not had[k * 2 + (s > 0 and 1 or 0)] then
          local nb = vcopy(c)
          nb[k] = nb[k] + s
          local own = faces_on(m.faces, nb, 7, true)
          if #own > 0 then
            add[#add + 1] = tile_face(c, k, c[k] + (s > 0 and 1 or 0), -s, brush_of(m.faces[own[1]]))
          end
        end
      end
    end
    place_faces(m.faces, add, false)
    commit()
  elseif bd.tool == 2 then
    local s = bd.side == 7 and 1 or bd.side
    if erase then
      local list = faces_on(m.faces, c, s)
      if #list == 0 then say("nothing on the " .. SIDES[s], C_DIM, 60); return end
      begin_edit()
      m.faces = remove_faces(m.faces, list)
      commit()
    else
      local a, l, n = side_plane(c, s)
      begin_edit()
      place_faces(m.faces, { tile_face(c, a, l, n, brush) }, false)
      commit()
    end
  else
    local list = faces_on(m.faces, c, bd.side)
    if #list == 0 then say("nothing to paint on the " .. SIDES[bd.side], C_DIM, 60); return end
    if erase then                                  -- pick the brush from the face
      brush = brush_of(m.faces[list[1]])
      say("picked " .. (brush.c and string.format("colour %06X", brush.c) or "a tile"), C_ACC, 90)
      return
    end
    begin_edit()
    for _, i in ipairs(list) do
      local f = m.faces[i]
      if brush.c then
        f.c, f.uv = brush.c, nil
      else
        local uv = rect_uv(brush.rect, brush.rot, brush.flip)
        f.c, f.uv = nil, {}
        for k = 1, #f.p do f.uv[k] = uv[k] end
      end
    end
    commit()
  end
end

local function build_move(dx, dz, dy)
  local fa, fs, ra, rs = view_axes(bd.yaw_to)
  local c = bd.cell
  if dz ~= 0 then c[fa] = c[fa] + dz * fs end
  if dx ~= 0 then c[ra] = c[ra] + dx * rs end
  if dy ~= 0 then c[2] = c[2] + dy end
  for k = 1, 3 do c[k] = clamp(c[k], -64, 63) end
end

local function build_turn(d)
  bd.yaw_to = bd.yaw_to + d * pi / 4
end

function build_reset()
  local m = M()
  bd.for_model = m
  if m and #m.faces > 0 then
    local lo, hi = bounds_of(m.faces)
    bd.cell = { floor((lo[1] + hi[1]) / 2), floor(lo[2] + 1e-3), floor(lo[3] + 1e-3) - 1 }
  else
    bd.cell = { 0, 0, 0 }
  end
end

-- the tile picker (the sheet is the project's: cart_load gives its size)
local function sheet_size() return proj.sheet_w or 256, proj.sheet_h or 256 end

local sheet_w, sheet_h = 256, 256

local function picker_key(k)
  local p = picker
  if k == "esc" or k == "\t" or k == "back" then p.open = false; return end
  if k == "c" then p.colours = not p.colours; return end
  if p.colours then
    if k == "left" then p.pal = (p.pal - 2) % #PALETTE + 1
    elseif k == "right" then p.pal = p.pal % #PALETTE + 1
    elseif k == "up" then p.pal = (p.pal - 7) % #PALETTE + 1
    elseif k == "down" then p.pal = (p.pal + 5) % #PALETTE + 1
    elseif k == "\n" or k == " " or k == "ok" then
      brush = { c = PALETTE[p.pal], rect = brush.rect, rot = 0, flip = false }
      p.open = false
      say(string.format("colour %06X", brush.c), C_ACC, 60)
    end
    return
  end
  local s = p.size
  if k == "left" then p.x = max(0, p.x - s)
  elseif k == "right" then p.x = min(sheet_w - s, p.x + s)
  elseif k == "up" then p.y = max(0, p.y - s)
  elseif k == "down" then p.y = min(sheet_h - s, p.y + s)
  elseif k == "z" then
    p.size = p.size == 8 and 16 or (p.size == 16 and 32 or 8)
    p.x, p.y = p.x // p.size * p.size, p.y // p.size * p.size
  elseif k == "\n" or k == " " or k == "ok" then
    brush = { rect = { p.x, p.y, p.size, p.size }, rot = brush.rot, flip = brush.flip }
    p.open = false
    say(string.format("tile at %d,%d (%dx%d)", p.x, p.y, p.size, p.size), C_ACC, 60)
  end
  p.top = clamp(p.top, max(0, p.y + p.size - 288), p.y)
  p.left = clamp(p.left, max(0, p.x + p.size - 256), p.x)
end

local function draw_tile(rect, x, y, size, rot, flip)
  -- the tile scaled into a size x size box (turned and flipped as it goes on)
  local rx, ry, rw, rh = rect[1], rect[2], rect[3], rect[4]
  local n = max(rw, rh)
  local z = size / n
  for j = 0, rh - 1 do
    for i = 0, rw - 1 do
      local u, v = i, j
      if flip then u = rw - 1 - u end
      for _ = 1, rot % 4 do u, v = rh - 1 - v, u end
      local c = sget(floor(rx + i), floor(ry + j))
      if c then rectfill(x + floor(u * z), y + floor(v * z), max(1, floor(z + 0.5)), max(1, floor(z + 0.5)), c) end
    end
  end
end

local function draw_brush(x, y, size)
  rectfill(x - 2, y - 2, size + 4, size + 4, C_PANEL)
  rect(x - 2, y - 2, size + 4, size + 4, C_DIM)
  if brush.c then rectfill(x, y, size, size, brush.c)
  else draw_tile(brush.rect, x, y, size, brush.rot, brush.flip) end
end

local function draw_picker()
  local p = picker
  cls(C_BG)
  if p.colours then
    print("colours: arrows choose, Enter takes it, c tiles, Tab back", 16, 16, C_ACC)
    for i, c in ipairs(PALETTE) do
      local x, y = 16 + ((i - 1) % 6) * 40, 48 + ((i - 1) // 6) * 40
      rectfill(x, y, 32, 32, c)
      if i == p.pal then rect(x - 3, y - 3, 38, 38, C_CUR) end
    end
    rectfill(280, 48, 96, 96, PALETTE[p.pal])
    print(string.format("%06X", PALETTE[p.pal]), 280, 152, C_TEXT)
    return
  end
  print("tiles: arrows choose, Enter takes it, z size " .. p.size .. ", c colours, Tab back", 16, 16, C_ACC)
  local ox, oy = 16, 44
  clip(ox, oy, 256, 288)
  local vw, vh = min(256, sheet_w - p.left), min(288, sheet_h - p.top)
  rectfill(ox, oy, vw, vh, 0x101218)
  -- a checker shows the transparent pixels
  for y = 0, vh - 1, 8 do
    for x = 0, vw - 1, 8 do
      if (x // 8 + y // 8) % 2 == 0 then rectfill(ox + x, oy + y, 8, 8, 0x1A1C26) end
    end
  end
  sspr(p.left, p.top, vw, vh, ox, oy)
  rect(ox + p.x - p.left, oy + p.y - p.top, p.size, p.size, C_CUR)
  rect(ox + p.x - p.left - 1, oy + p.y - p.top - 1, p.size + 2, p.size + 2, 0x000000)
  clip()
  draw_tile({ p.x, p.y, p.size, p.size }, 300, 48, 128, 0, false)
  rect(299, 47, 130, 130, C_DIM)
  print(string.format("%d,%d  %dx%d", p.x, p.y, p.size, p.size), 300, 184, C_TEXT)
end

function build_key(k)
  if picker.open then picker_key(k); return end
  if k == "up" then build_move(0, 1, 0)
  elseif k == "down" then build_move(0, -1, 0)
  elseif k == "left" then build_move(-1, 0, 0)
  elseif k == "right" then build_move(1, 0, 0)
  elseif k == "pgup" then build_move(0, 0, 1)
  elseif k == "pgdn" then build_move(0, 0, -1)
  elseif k == " " or k == "\n" then build_action(false)
  elseif k == "\b" or k == "del" then build_action(true)
  elseif k == "1" or k == "2" or k == "3" then
    bd.tool = tonumber(k)
    if bd.tool ~= 3 and bd.side == 7 then bd.side = 1 end
  elseif k == "f" then bd.side = bd.side % (bd.tool == 3 and 7 or 6) + 1
  elseif k == "\t" then picker.open = true; sheet_w, sheet_h = sheet_size()
  elseif k == "r" then brush.rot = (brush.rot + 1) % 4
  elseif k == "h" then brush.flip = not brush.flip
  elseif k == "x" then local t = bd.tool; bd.tool = 3; build_action(true); bd.tool = t
  elseif k == "q" then build_turn(-1)
  elseif k == "e" then build_turn(1)
  elseif k == "w" then bd.cam.pitch = clamp(bd.cam.pitch - 0.1, -1.45, 0.3)
  elseif k == "s" then bd.cam.pitch = clamp(bd.cam.pitch + 0.1, -1.45, 0.3)
  elseif k == "+" or k == "=" then bd.cam.dist = max(2, bd.cam.dist * 0.85)
  elseif k == "-" then bd.cam.dist = min(80, bd.cam.dist / 0.85)
  elseif k == "g" then bd.grid = not bd.grid
  elseif k == "0" then build_reset()
  end
end

function build_pad()
  if picker.open then
    if rp[0] then picker_key("left") end
    if rp[1] then picker_key("right") end
    if rp[2] then picker_key("up") end
    if rp[3] then picker_key("down") end
    if btnp(4) then picker_key("ok") end
    if btnp(5) or tap[7] then picker_key("back") end
    if tap[6] then picker_key(picker.colours and "c" or "z") end
    return
  end
  if btn(6) then                               -- X + croce: up/down a level, turn the view
    if rp[2] then build_move(0, 0, 1) end
    if rp[3] then build_move(0, 0, -1) end
    if btnp(0) then build_turn(-1) end
    if btnp(1) then build_turn(1) end
    if btnp(4) then bd.tool = bd.tool % 3 + 1 end
    if btnp(5) then brush.rot = (brush.rot + 1) % 4 end
    return
  end
  if rp[0] then build_move(-1, 0, 0) end
  if rp[1] then build_move(1, 0, 0) end
  if rp[2] then build_move(0, 1, 0) end
  if rp[3] then build_move(0, -1, 0) end
  if btnp(4) then build_action(false) end
  if btnp(5) then build_action(true) end
  if tap[6] then bd.side = bd.side % (bd.tool == 3 and 7 or 6) + 1 end
  if tap[7] then picker.open = true; sheet_w, sheet_h = sheet_size() end
end

function build_update()
  local cam, c = bd.cam, bd.cell
  cam.tx = cam.tx + (c[1] + 0.5 - cam.tx) * 0.25
  cam.ty = cam.ty + (c[2] + 0.5 - cam.ty) * 0.25
  cam.tz = cam.tz + (c[3] + 0.5 - cam.tz) * 0.25
  cam.yaw = cam.yaw + (bd.yaw_to - cam.yaw) * 0.25
end

function draw_build()
  if picker.open then draw_picker(); return end
  local cam, c, m = bd.cam, bd.cell, M()
  cls(C_SKY)
  zclear()
  -- the camera's own axes, before drawing (the tiles face it)
  look(cam, 0)
  if bd.grid then
    draw_grid(c[1], c[2], c[3], 6, 1, C_GRID)
    seg({ -0.2, 0, 0 }, { 1, 0, 0 }, 0xC04040)
    seg({ 0, 0, -0.2 }, { 0, 0, 1 }, 0x4060D0)
  end
  light3d(-0.4, 0.8, -0.5, 0.45)
  if view then draw3d(view, 0, 0, 0, 0, 0, 0, 1, 0) end
  -- the cursor: the cell, and the side the tools work on
  local x0, y0, z0 = c[1], c[2], c[3]
  local x1, y1, z1 = x0 + 1, y0 + 1, z0 + 1
  local P = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
              { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } }
  local E = { { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 1 }, { 5, 6 }, { 6, 7 }, { 7, 8 }, { 8, 5 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, { 4, 8 } }
  local cc = (frame // 15) % 2 == 0 and C_CUR or 0xFFFFFF
  for _, e in ipairs(E) do seg(P[e[1]], P[e[2]], bd.tool == 1 and cc or C_GRID0) end
  if bd.tool ~= 1 then
    local list = bd.side == 7 and { 1, 2, 3, 4, 5, 6 } or { bd.side }
    for _, s in ipairs(list) do
      local a, l = side_plane(c, s)
      local q = {}
      local o1, o2 = a % 3 + 1, (a + 1) % 3 + 1
      for i, d in ipairs({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }) do
        local p = { 0, 0, 0 }
        p[a] = l
        p[o1] = c[o1] + d[1]
        p[o2] = c[o2] + d[2]
        q[i] = p
      end
      for i = 1, 4 do seg(q[i], q[i % 4 + 1], cc) end
      seg(q[1], q[3], cc)
      seg(q[2], q[4], cc)
    end
  end
  -- what the tools do
  rectfill(0, 16, W, 16, C_PANEL)
  local s = string.format("%s  %s  cell %d,%d,%d  %s: %d faces", TOOLS[bd.tool]:upper(),
                          bd.tool == 1 and "" or ("on the " .. SIDES[bd.side]), c[1], c[2], c[3],
                          m and m.name or "-", m and #m.faces or 0)
  print(s, 8, 16, C_TEXT)
  draw_brush(W - 58, 42, 48)
  print((brush.rot > 0 and (brush.rot * 90 .. "\248") or "") .. (brush.flip and " mirror" or ""), W - 58, 94, C_DIM)
  hint("arrows PgUp PgDn move  space put  Bksp remove  1 2 3 tool  f side  Tab tiles")
end
end

----------------------------------------------------------------- rig page (bones and skin)
do

rg = { bone = 1, tail = true, skin = false, skinmesh = nil,
             cam = { yaw = 0.5, pitch = -0.3, dist = 4, tx = 0, ty = 1, tz = 0 } }

function bone_depth(r, i)
  local d = 0
  local p = r.bones[i].parent
  while p > 0 and d < 64 do d, p = d + 1, r.bones[p].parent end
  return d
end

local function nearest_bone(r, p)
  local best, bd2 = 1, 1e18
  for i, b in ipairs(r.bones) do
    local ab, ap = vsub(b.tail, b.head), vsub(p, b.head)
    local l2 = vdot(ab, ab)
    local u = l2 > 1e-12 and clamp(vdot(ap, ab) / l2, 0, 1) or 0
    local d = vsub(ap, vscale(ab, u))
    local dd = vdot(d, d)
    if dd < bd2 - 1e-9 then best, bd2 = i, dd end
  end
  return best
end

-- each face whole to its nearest bone (rigid parts, as on the PS1)
local function auto_skin(m)
  for _, f in ipairs(m.faces) do
    local b = nearest_bone(m.rig, face_center(f))
    f.b = {}
    for k = 1, #f.p do f.b[k] = b end
  end
end

-- the faces coloured by their bones (shared corners), as a mesh
function make_skin_mesh(m)
  rg.skinmesh = nil
  if not m.rig or #m.faces == 0 then return end
  local v, f, index = {}, {}, {}
  local function vid(p)
    local k = pkey(p)
    local i = index[k]
    if not i then
      i = #v // 3 + 1
      index[k] = i
      v[#v + 1], v[#v + 2], v[#v + 3] = p[1], p[2], p[3]
    end
    return i
  end
  for _, fc in ipairs(m.faces) do
    local ids = {}
    for k, p in ipairs(fc.p) do ids[k] = vid(p) end
    local col = bone_colour(fc.b and fc.b[1] or 1)
    for _, t in ipairs(#fc.p == 4 and QUAD or TRI) do
      local a, b, c = ids[t[1]], ids[t[2]], ids[t[3]]
      if a ~= b and b ~= c and a ~= c then
        f[#f + 1], f[#f + 2], f[#f + 3], f[#f + 4] = a, b, c, col
      end
    end
  end
  if #v // 3 <= 4096 and #f > 0 and #f // 4 <= 16384 then rg.skinmesh = mesh(v, f) end
end

function rig_reset()
  rg.bone = 1
  rg.skinmesh = nil
  aim(rg.cam, M() and M().faces or {}, 2.4)
  if rg.skin and M() then make_skin_mesh(M()) end
end

local function new_skeleton(m)
  local lo, hi = bounds_of(m.faces)
  local cx, cz = (lo[1] + hi[1]) / 2, (lo[3] + hi[3]) / 2
  local function g(v) return round(v * 8) / 8 end
  m.rig = { bones = { { name = "root", parent = 0, head = { g(cx), g(lo[2]), g(cz) },
                        tail = { g(cx), g(lo[2] + max(0.25, (hi[2] - lo[2]) * 0.5)), g(cz) } } },
            clips = {} }
  for _, f in ipairs(m.faces) do
    f.b = {}
    for k = 1, #f.p do f.b[k] = 1 end
  end
end

local function rig_new_bone()
  local m = M()
  if not m or #m.faces == 0 then say("build the model first (F2)", C_ERR); return end
  begin_edit()
  if not m.rig then
    new_skeleton(m)
    rg.bone = 1
    commit()
    say("a skeleton: one bone, root; n adds bones to it", C_ACC)
    return
  end
  local r = m.rig
  if #r.bones >= 64 then table.remove(undo); say("at most 64 bones", C_ERR); return end
  local p = r.bones[rg.bone]
  local dir = vsub(p.tail, p.head)
  local l = sqrt(vdot(dir, dir))
  dir = l > 1e-6 and vscale(dir, 0.5 / l) or { 0, 0.5, 0 }
  local nb = { name = "bone" .. (#r.bones + 1), parent = rg.bone, head = vcopy(p.tail),
               tail = vadd(p.tail, { round(dir[1] * 8) / 8, round(dir[2] * 8) / 8, round(dir[3] * 8) / 8 }) }
  if vdot(vsub(nb.tail, nb.head), vsub(nb.tail, nb.head)) < 1e-6 then nb.tail[2] = nb.tail[2] + 0.5 end
  local used = {}
  for _, b in ipairs(r.bones) do used[b.name] = true end
  local k = #r.bones + 1
  while used[nb.name] do k = k + 1; nb.name = "bone" .. k end
  r.bones[#r.bones + 1] = nb
  for _, c in ipairs(r.clips) do
    for _, key in ipairs(c.keys) do key.pose[#r.bones] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
  end
  rg.bone = #r.bones
  rg.tail = true
  commit()
  say("new bone " .. nb.name .. ": move its tail with w a s d r f", C_ACC)
end

local function rig_delete_bone()
  local m = M()
  local r = m and m.rig
  if not r then return end
  begin_edit()
  if #r.bones == 1 then
    m.rig = nil
    for _, f in ipairs(m.faces) do f.b = nil end
    commit()
    say("the skeleton is gone", C_ACC)
    return
  end
  local d = rg.bone
  local parent = r.bones[d].parent
  table.remove(r.bones, d)
  for _, b in ipairs(r.bones) do
    if b.parent == d then b.parent = parent
    elseif b.parent > d then b.parent = b.parent - 1 end
  end
  local to = parent > 0 and parent or 1
  if to > d then to = to - 1 end
  for _, f in ipairs(m.faces) do
    if f.b then
      for k = 1, #f.b do
        if f.b[k] == d then f.b[k] = to elseif f.b[k] > d then f.b[k] = f.b[k] - 1 end
      end
    end
  end
  for _, c in ipairs(r.clips) do
    for _, key in ipairs(c.keys) do table.remove(key.pose, d) end
  end
  rg.bone = clamp(rg.bone - 1, 1, #r.bones)
  commit()
  if rg.skin then make_skin_mesh(m) end
end

-- moves the chosen end of the bone (the tail takes the heads of the
-- bones joined to it along)
local function rig_move(dx, dy, dz, step)
  local r = rig()
  if not r then return end
  local fa, fs, ra, rs = view_axes(rg.cam.yaw)
  local d = { 0, 0, 0 }
  d[ra] = d[ra] + dx * rs * step
  d[2] = d[2] + dy * step
  d[fa] = d[fa] + dz * fs * step
  local b = r.bones[rg.bone]
  begin_edit()
  if rg.tail then
    local old = vcopy(b.tail)
    b.tail = vadd(b.tail, d)
    for _, o in ipairs(r.bones) do
      if o ~= b and vdot(vsub(o.head, old), vsub(o.head, old)) < 1e-8 then o.head = vcopy(b.tail) end
    end
  else
    b.head = vadd(b.head, d)
  end
  commit_anim()
end

function rig_key(k)
  local m, r = M(), rig()
  if k == "n" then rig_new_bone(); return end
  if k == "q" then rg.cam.yaw = rg.cam.yaw - pi / 8; return end
  if k == "e" then rg.cam.yaw = rg.cam.yaw + pi / 8; return end
  if k == "+" or k == "=" then rg.cam.dist = max(0.5, rg.cam.dist * 0.85); return end
  if k == "-" then rg.cam.dist = min(100, rg.cam.dist / 0.85); return end
  if not r then return end
  if k == "up" then rg.bone = (rg.bone - 2) % #r.bones + 1
  elseif k == "down" then rg.bone = rg.bone % #r.bones + 1
  elseif k == "\t" then rg.tail = not rg.tail
  elseif k == "x" or k == "del" then rig_delete_bone()
  elseif k == "k" then
    begin_edit(); auto_skin(m); commit()
    if rg.skin then make_skin_mesh(m) end
    say("every face follows its nearest bone", C_ACC)
  elseif k == "v" then
    rg.skin = not rg.skin
    if rg.skin then make_skin_mesh(m) end
  else
    local moves = { a = { -1, 0, 0 }, d = { 1, 0, 0 }, w = { 0, 1, 0 }, s = { 0, -1, 0 }, r = { 0, 0, 1 }, f = { 0, 0, -1 } }
    local mv = moves[k] or moves[k:lower()]
    if mv then rig_move(mv[1], mv[2], mv[3], moves[k] and 1 / 8 or 1 / 32) end
  end
end

function rig_pad()
  local r = rig()
  if btn(4) and r then                           -- A + croce: move the end
    if rp[0] then rig_move(-1, 0, 0, 1 / 16) end
    if rp[1] then rig_move(1, 0, 0, 1 / 16) end
    if rp[2] then rig_move(0, 1, 0, 1 / 16) end
    if rp[3] then rig_move(0, -1, 0, 1 / 16) end
    return
  end
  if btn(6) and r then                           -- X + up/down: nearer / farther
    if rp[2] then rig_move(0, 0, 1, 1 / 16) end
    if rp[3] then rig_move(0, 0, -1, 1 / 16) end
    return
  end
  if rp[2] then rig_key("up") end
  if rp[3] then rig_key("down") end
  if rp[0] then rg.cam.yaw = rg.cam.yaw - 0.06 end
  if rp[1] then rg.cam.yaw = rg.cam.yaw + 0.06 end
  if btnp(5) then rig_key("\t") end
  if tap[6] then rig_key("n") end
end

function draw_rig()
  local m, cam = M(), rg.cam
  local r = m and m.rig
  cls(C_SKY)
  zclear()
  look(cam, PANEL_W / 2, 12)
  if m and #m.faces > 0 then
    draw_grid(round(cam.tx), cam.floor, round(cam.tz), 6, 0.5, C_GRID)
  end
  light3d(-0.4, 0.8, -0.5, 0.45)
  if rg.skin and rg.skinmesh then draw3d(rg.skinmesh, 0, 0, 0, 0, 0, 0, 1, 0)
  elseif view then
    if r then animate(view) end
    draw3d(view, 0, 0, 0, 0, 0, 0, 1, 0)
  end
  panel(0, 16, PANEL_W, HINT_Y - 16)
  if not r then
    print("NO SKELETON", 16, 32, C_DIM)
    print("n: make one", 16, 64, C_TEXT)
    print("(one bone, then", 16, 96, C_DIM)
    print(" n adds more)", 16, 112, C_DIM)
    hint("n new skeleton  q/e turn  +/- zoom")
    return
  end
  for i, b in ipairs(r.bones) do
    local h, t = b.head, b.tail
    if i ~= rg.bone then draw_bone(h, t, bone_colour(i), false) end
  end
  local b = r.bones[rg.bone]
  draw_bone(b.head, b.tail, C_CUR, true)
  local ex, ey = scr(rg.tail and b.tail or b.head)
  if ex then circ(ex, ey, 6, 0xFFFFFF) end
  local names = {}
  for i, bb in ipairs(r.bones) do names[i] = string.rep(" ", bone_depth(r, i)) .. bb.name end
  draw_list("BONES", names, rg.bone, 0, 32, 16, bone_colour)
  local e = rg.tail and b.tail or b.head
  info_strip(rg.skin and 2 or 1)
  print(string.format("%s of %s: %.3f, %.3f, %.3f", rg.tail and "tail" or "head", b.name, e[1], e[2], e[3]),
        INFO_X, 32, C_ACC)
  print(rg.skin and "colours: the bone of each face (v)" or "", INFO_X, 48, C_DIM)
  hint("up/down bone  w a s d r f move  Tab head/tail  n new  x delete  k auto skin")
end
end

----------------------------------------------------------------- animate page (keyframes)
do

an = { clip = 1, t = 0, playing = false, move = false, copied = nil,
             cam = { yaw = 0.5, pitch = -0.3, dist = 4, tx = 0, ty = 1, tz = 0 } }

local function rest_pose(n)
  local p = {}
  for i = 1, n do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
  return p
end

local function copy_pose(p)
  local o = {}
  for i, b in ipairs(p) do o[i] = { q = { b.q[1], b.q[2], b.q[3], b.q[4] }, t = { b.t[1], b.t[2], b.t[3] } } end
  return o
end

local function ease(mode, u)
  if mode == 2 then return 0 end
  if mode == 1 then return u * u * (3 - 2 * u) end
  return u
end

-- the pose of a clip at time t (rig.js samplePose; the kernel does the same)
local function sample(r, c, t)
  local n, keys = #r.bones, c.keys
  local L = c.length > 0 and c.length or 1
  if c.loop then t = t % L else t = clamp(t, 0, L) end
  local function fill(p)
    for i = #p + 1, n do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
    return p
  end
  if #keys == 1 then return fill(copy_pose(keys[1].pose)) end
  local a, b, ta, tb
  if t < keys[1].t then
    if not c.loop then return fill(copy_pose(keys[1].pose)) end
    a, b = keys[#keys], keys[1]
    ta, tb = a.t - L, b.t
  elseif t >= keys[#keys].t then
    if not c.loop then return fill(copy_pose(keys[#keys].pose)) end
    a, b = keys[#keys], keys[1]
    ta, tb = a.t, b.t + L
  else
    local k = 1
    while k + 1 <= #keys and keys[k + 1].t <= t do k = k + 1 end
    a, b = keys[k], keys[k + 1]
    ta, tb = a.t, b.t
  end
  local u = tb > ta and ease(c.mode, (t - ta) / (tb - ta)) or 0
  local out = {}
  for i = 1, n do
    local pa = a.pose[i] or { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
    local pb = b.pose[i] or pa
    out[i] = { q = qslerp(pa.q, pb.q, u),
               t = { pa.t[1] + (pb.t[1] - pa.t[1]) * u, pa.t[2] + (pb.t[2] - pa.t[2]) * u, pa.t[3] + (pb.t[3] - pa.t[3]) * u } }
  end
  return out
end

local function key_at(c, t)
  for i, k in ipairs(c.keys) do if abs(k.t - t) < 1e-4 then return i end end
end

local function set_key(c, t, pose)
  local i = key_at(c, t)
  if i then c.keys[i].pose = copy_pose(pose); return i end
  c.keys[#c.keys + 1] = { t = t, pose = copy_pose(pose) }
  table.sort(c.keys, function(x, y) return x.t < y.t end)
  return key_at(c, t)
end

local function clip_obj()
  local r = rig()
  return r and r.clips[an.clip]
end

function anim_reset()
  local r = rig()
  an.clip = r and clamp(an.clip, 1, max(1, #r.clips)) or 1
  an.t, an.playing = 0, false
  an.bone = 1
  aim(an.cam, M() and M().faces or {}, 3)
end

local function snap_t(t) return round(t * FPS) / FPS end

local function anim_new_clip()
  local r = rig()
  if not r then say("make a skeleton first (F3)", C_ERR); return end
  if #r.clips >= 255 then say("at most 255 animations", C_ERR); return end
  begin_edit()
  local used = {}
  for _, c in ipairs(r.clips) do used[c.name] = true end
  local k, name = #r.clips + 1, "anim" .. (#r.clips + 1)
  while used[name] do k = k + 1; name = "anim" .. k end
  r.clips[#r.clips + 1] = { name = name, mode = 1, loop = true, length = 1,
                            keys = { { t = 0, pose = rest_pose(#r.bones) } } }
  an.clip, an.t = #r.clips, 0
  commit_anim()
  say("new animation " .. name .. ": turn the bones (w/s a/d q/e), each turn is a keyframe", C_ACC, 300)
end

-- playing stops on the frame it is at (the keys go on frames)
local function stop_play(c)
  if not an.playing then return end
  an.playing = false
  local L = max(c.length, 1e-3)
  an.t = clamp(snap_t(c.loop and an.t % L or an.t), 0, c.length)
end

-- turns (or moves) the chosen bone at the current time: a keyframe there
local function anim_turn(axis, deg)
  local r, c = rig(), clip_obj()
  if not c then say("no animation: n makes one", C_ERR); return end
  stop_play(c)
  begin_edit()
  local pose = sample(r, c, an.t)
  local b = pose[an.bone]
  if an.move then
    b.t[axis] = b.t[axis] + deg / 15 / 16
  else
    b.q = qnorm(qmul(qaxis(unit(axis), math.rad(deg)), b.q))
  end
  an.t = clamp(an.t, 0, c.length)
  set_key(c, an.t, pose)
  commit_anim()
end

function anim_key(k)
  local r, c = rig(), clip_obj()
  if k == "n" then anim_new_clip(); return end
  if k == "," then an.cam.yaw = an.cam.yaw - pi / 8; return end
  if k == "." then an.cam.yaw = an.cam.yaw + pi / 8; return end
  if k == "+" or k == "=" then an.cam.dist = max(0.5, an.cam.dist * 0.85); return end
  if k == "-" then an.cam.dist = min(100, an.cam.dist / 0.85); return end
  if not r then return end
  if k == "up" then an.bone = (an.bone - 2) % #r.bones + 1; return
  elseif k == "down" then an.bone = an.bone % #r.bones + 1; return
  elseif k == "pgup" and #r.clips > 0 then an.clip = (an.clip - 2) % #r.clips + 1; an.t = 0; return
  elseif k == "pgdn" and #r.clips > 0 then an.clip = an.clip % #r.clips + 1; an.t = 0; return
  end
  if not c then return end
  if k ~= " " then stop_play(c) end
  local turns = { w = { 1, 15 }, s = { 1, -15 }, a = { 2, 15 }, d = { 2, -15 }, q = { 3, 15 }, e = { 3, -15 } }
  if turns[k] then anim_turn(turns[k][1], turns[k][2]); return end
  if turns[k:lower()] then anim_turn(turns[k:lower()][1], turns[k:lower()][2] / 3); return end
  if k == "left" then an.playing = false; an.t = max(0, snap_t(an.t) - 1 / FPS)
  elseif k == "right" then an.playing = false; an.t = min(c.length, snap_t(an.t) + 1 / FPS)
  elseif k == "home" then an.t = 0
  elseif k == "end" then an.t = c.length
  elseif k == " " then
    if an.playing then stop_play(c) else an.playing = true end
  elseif k == "g" then an.move = not an.move; say(an.move and "w/s a/d q/e move the bone" or "w/s a/d q/e turn the bone", C_ACC, 90)
  elseif k == "k" then begin_edit(); set_key(c, an.t, sample(r, c, an.t)); commit_anim()
  elseif k == "x" or k == "del" then
    local i = key_at(c, an.t)
    if not i then say("no keyframe here", C_DIM, 60)
    elseif #c.keys == 1 then say("the last keyframe stays", C_DIM, 60)
    else begin_edit(); table.remove(c.keys, i); commit_anim() end
  elseif k == "c" then an.copied = sample(r, c, an.t); say("pose copied", C_ACC, 60)
  elseif k == "v" and an.copied then begin_edit(); set_key(c, an.t, an.copied); commit_anim()
  elseif k == "r" then
    begin_edit()
    local pose = sample(r, c, an.t)
    pose[an.bone] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
    set_key(c, an.t, pose)
    commit_anim()
  elseif k == "l" then begin_edit(); c.loop = not c.loop; commit_anim()
  elseif k == "m" then begin_edit(); c.mode = (c.mode + 1) % 3; commit_anim()
  elseif k == ">" then begin_edit(); c.length = snap_t(c.length) + 1 / FPS; commit_anim()
  elseif k == "<" then
    local last = c.keys[#c.keys].t
    local l = max(1 / FPS, last, snap_t(c.length) - 1 / FPS)
    if l < c.length - 1e-6 then begin_edit(); c.length = l; an.t = min(an.t, l); commit_anim()
    else say("the length stays past the last keyframe", C_DIM, 60) end
  elseif k == "\b" and #r.clips > 0 then
    -- delete the animation: twice to confirm
    if an.confirm == c then
      begin_edit(); table.remove(r.clips, an.clip); an.clip = clamp(an.clip, 1, max(1, #r.clips)); commit_anim()
      an.confirm = nil
      say("animation deleted", C_ACC)
    else
      an.confirm = c
      say("Backspace again deletes the animation " .. c.name, C_ERR)
    end
  end
end

function anim_pad()
  local r, c = rig(), clip_obj()
  if btn(4) and c then                           -- A + croce: turn the bone
    if rp[0] then anim_turn(2, 15) end
    if rp[1] then anim_turn(2, -15) end
    if rp[2] then anim_turn(1, 15) end
    if rp[3] then anim_turn(1, -15) end
    return
  end
  if btn(6) and c then                           -- X + croce: around z; X + up/down: the clip
    if rp[0] then anim_turn(3, 15) end
    if rp[1] then anim_turn(3, -15) end
    if rp[2] then anim_key("pgup") end
    if rp[3] then anim_key("pgdn") end
    return
  end
  if rp[2] then anim_key("up") end
  if rp[3] then anim_key("down") end
  if rp[0] then anim_key("left") end
  if rp[1] then anim_key("right") end
  if btnp(5) then anim_key(" ") end
  if tap[6] then if c then anim_key("k") else anim_key("n") end end
end

function anim_update(dt)
  local c = clip_obj()
  if an.playing and c then
    an.t = an.t + dt
    if not c.loop and an.t >= c.length then an.t, an.playing = c.length, false end
  end
end

local function draw_timeline(c)
  local x0, y0, w = PANEL_W + 16, HINT_Y - 48, W - PANEL_W - 32
  rectfill(PANEL_W, y0, W - PANEL_W, 48, C_PANEL)
  local L = max(c.length, 1e-3)
  local nf = round(L * FPS)
  for i = 0, nf do
    local x = x0 + floor(i / max(nf, 1) * w)
    line(x, y0 + 30, x, y0 + (i % FPS == 0 and 18 or 25), C_DIM)
    if i % FPS == 0 then print(tostring(i // FPS), x + 2, y0, C_DIM) end
  end
  line(x0, y0 + 30, x0 + w, y0 + 30, C_DIM)
  for _, k in ipairs(c.keys) do
    local x = x0 + floor(k.t / L * w)
    tri(x, y0 + 30, x - 5, y0 + 36, x + 5, y0 + 36, C_ACC)
    tri(x - 5, y0 + 36, x, y0 + 42, x + 5, y0 + 36, C_ACC)
  end
  local tt = c.loop and (an.t % L) or clamp(an.t, 0, L)
  local x = x0 + floor(tt / L * w)
  line(x, y0 + 16, x, y0 + 46, 0xFFFFFF)
  return tt
end

function draw_anim()
  local m, cam = M(), an.cam
  local r = m and m.rig
  local c = clip_obj()
  cls(C_SKY)
  zclear()
  look(cam, PANEL_W / 2, -12)
  if m and #m.faces > 0 then
    draw_grid(round(cam.tx), cam.floor, round(cam.tz), 6, 0.5, C_GRID)
  end
  light3d(-0.4, 0.8, -0.5, 0.45)
  if view then
    if r then
      if c then animate(view, an.clip, an.t) else animate(view) end
    end
    draw3d(view, 0, 0, 0, 0, 0, 0, 1, 0)
  end
  panel(0, 16, PANEL_W, HINT_Y - 16)
  if not r then
    print("NO SKELETON", 16, 32, C_DIM)
    print("make one in F3", 16, 64, C_TEXT)
    hint("F3 rig: bones and skin first")
    return
  end
  for i = 1, #r.bones do
    local hx, hy, hz, tx, ty, tz = bone3d(view, i)
    if hx and i ~= an.bone then draw_bone({ hx, hy, hz }, { tx, ty, tz }, bone_colour(i), false) end
  end
  local hx, hy, hz, tx, ty, tz = bone3d(view, an.bone)
  if hx then draw_bone({ hx, hy, hz }, { tx, ty, tz }, C_CUR, true) end
  local names = {}
  for i, bb in ipairs(r.bones) do names[i] = string.rep(" ", bone_depth(r, i)) .. bb.name end
  draw_list("BONES", names, an.bone, 0, 32, 16, bone_colour)
  local x = INFO_X
  info_strip(c and 2 or 1)
  if not c then
    print("no animations yet: n makes one", x, 32, C_ACC)
    hint("n new animation  up/down bone  , . turn the view  +/- zoom")
    return
  end
  print(string.format("%s  %d/%d  %s%s  %.2f s", c.name, an.clip, #r.clips, MODES[c.mode], c.loop and "  loop" or "",
                      c.length), x, 32, C_ACC)
  local tt = draw_timeline(c)
  print(string.format("%s %s   %.2f s  frame %d%s", an.move and "MOVE" or "TURN", r.bones[an.bone].name, tt,
                      round(tt * FPS), key_at(c, tt) and "  key" or ""), x, 48, key_at(c, tt) and C_ACC or C_TEXT)
  hint("left/right time  space play  w/s a/d q/e turn  g move  k key  x del key  n new")
end
end

----------------------------------------------------------------- menu

local items, msel, choosing, files, fsel, input = {}, 1, false, {}, 1, nil
local help = false
local confirm_t, confirm_what = 0, nil

local function needs_confirm(what)
  if not dirty then return false end
  if confirm_what == what and confirm_t > 0 then return false end
  confirm_what, confirm_t = what, 150
  say("unsaved changes: choose again to confirm", C_ERR)
  return true
end

local function go(p)
  if p ~= "menu" then last_page = p
  elseif page ~= "menu" then msel = 1 end     -- the menu opens on Continue
  page = p
  picker.open = false
  if p == "play" then play_reset()
  elseif p == "rig" then rig_reset()
  elseif p == "anim" then anim_reset()
  elseif p == "build" and bd.for_model ~= M() then build_reset() end
end

-- after undo: the same page, the indexes still valid
local function refresh()
  local r = rig()
  pl_clips = view and clips(view) or {}
  pl.clip = clamp(pl.clip, 1, max(1, #pl_clips))
  if r then
    rg.bone = clamp(rg.bone, 1, #r.bones)
    an.bone = clamp(an.bone or 1, 1, #r.bones)
    an.clip = clamp(an.clip, 1, max(1, #r.clips))
  else
    rg.bone, an.bone, an.clip = 1, 1, 1
  end
  if page == "rig" and rg.skin and M() then make_skin_mesh(M()) end
end

reset_pages = function()
  play_reset()
  build_reset()
  rig_reset()
  anim_reset()
end

local function ask(label, text, done)
  input = { label = label, text = text, done = done }
end

local function build_menu()
  local m = M()
  items = {
    { "Continue", function() go(last_page) end },
    { "Open...", function()
        if needs_confirm("open") then return end
        files, fsel, choosing = list_files(), 1, true
        if #files == 0 then choosing = false; say("no .bm files on the SD card", C_ERR) end
      end },
    { "New project", function() if not needs_confirm("new") then new_project(); go("build") end end },
    { "Save   (Ctrl+S)", function() save_project() end },
    { "Save as...", function()
        ask("file name (8.3, in /carts)", proj.save and proj.save:match("([^/]+)$") or "MY3D.BM", function(t)
          if not t:upper():match("%.BM$") then t = t .. ".BM" end
          proj.save = short_path("/carts/" .. t)
          save_project()
        end)
      end },
    { "Try the game (F5)", function() run_project() end },
    { "New model", function()
        local i = new_model("model")
        sync(); select_model(i); dirty = true; go("build")
        say("new model " .. M().name .. ": build it here", C_ACC)
      end },
    { "Rename model: " .. (m and m.name or "-"), function()
        ask("model name (up to 16 letters)", m.name, function(t)
          t = t:gsub("[%c]", ""):sub(1, 16)
          if t == "" then return end
          m.name = unique_name(t, cur)
          m.dirty = true
          sync(); dirty = true
          say("renamed: " .. m.name, C_ACC)
        end)
      end },
    { "Duplicate model", function()
        local c = { name = unique_name(m.name), faces = {}, dirty = true }
        if m.mc then c.faces, c.rig = decode_model(m.mc, m.ac) else c.rig = deep(m.rig) end
        table.insert(models, cur + 1, c)
        undo, redo = {}, {}
        sync(); select_model(cur + 1); dirty = true
        say("copied as " .. c.name, C_ACC)
      end },
    { "Delete model", function()
        if confirm_what ~= "delmodel" or confirm_t <= 0 then
          confirm_what, confirm_t = "delmodel", 150
          say("choose again to delete " .. m.name, C_ERR)
          return
        end
        table.remove(models, cur)
        if #models == 0 then new_model("model") end
        undo, redo = {}, {}
        sync(); select_model(min(cur, #models)); dirty = true
        say("deleted", C_ACC)
      end },
    { "Exit 3D studio", function() if not needs_confirm("exit") then quit() end end },
  }
end

local function menu_key(k)
  if input then
    if k == "\n" then local d, t = input.done, input.text; input = nil; d(t)
    elseif k == "esc" then input = nil
    elseif k == "\b" then input.text = input.text:sub(1, -2)
    elseif #k == 1 and k:byte() >= 32 and #input.text < 40 then input.text = input.text .. k end
    return
  end
  if choosing then
    if k == "up" then fsel = max(1, fsel - 1)
    elseif k == "down" then fsel = min(#files, fsel + 1)
    elseif k == "esc" or k == "back" then choosing = false
    elseif k == "\n" or k == "ok" then
      choosing = false
      if load_project(files[fsel]) then go("play") end
    end
    return
  end
  build_menu()
  if k == "up" then msel = (msel - 2) % #items + 1
  elseif k == "down" then msel = msel % #items + 1
  elseif k == "\n" or k == "ok" then items[msel][2]()
  elseif k == "esc" or k == "back" then go(last_page) end
end

local function draw_menu()
  rectfill(0, 16, W, HINT_Y, C_BG)
  build_menu()
  print("bm 3D studio", 32, 32, C_ACC)
  print((proj.save or "(not saved yet)") .. (dirty and "  *modified*" or ""), 176, 32, C_DIM)
  for i, it in ipairs(items) do
    local y = 64 + (i - 1) * 16
    if i == msel and not choosing and not input then rectfill(24, y, 300, 16, C_SEL) end
    print(it[1], 32, y, C_TEXT)
  end
  local x = 344
  print("project", x, 64, C_DIM)
  print(proj.title:sub(1, 34), x, 80, C_TEXT)
  print(#models .. " models" .. (M() and (", now: " .. M().name) or ""), x, 96, C_TEXT)
  local nr = 0
  for _, m in ipairs(models) do if m.rig then nr = nr + 1 end end
  print(nr .. " with a skeleton", x, 112, C_TEXT)
  print("F1 play   F2 build", x, 144, C_DIM)
  print("F3 rig    F4 animate", x, 160, C_DIM)
  print("F12 (held) or ? : keys", x, 176, C_DIM)
  print("the same files as bm Studio", x, 208, C_DIM)
  print("and bm Animator on the PC", x, 224, C_DIM)
  if choosing then
    rectfill(40, 40, 560, 272, C_PANEL)
    rect(40, 40, 560, 272, C_ACC)
    print("open a cartridge (Enter), Esc back", 56, 48, C_ACC)
    local first = max(1, min(fsel - 7, #files - 13))
    for i = first, min(#files, first + 13) do
      local y = 80 + (i - first) * 16
      if i == fsel then rectfill(52, y, 536, 16, C_SEL) end
      print(files[i], 56, y, C_TEXT)
    end
  end
  if input then
    rectfill(80, 144, 480, 64, C_PANEL)
    rect(80, 144, 480, 64, C_ACC)
    print(input.label, 96, 160, C_DIM)
    print(input.text .. ((frame // 20) % 2 == 0 and "_" or ""), 96, 176, C_TEXT)
  end
end

----------------------------------------------------------------- keys help

local KEYS = {
  all = { "F1 play  F2 build  F3 rig  F4 animate  Esc menu", "Ctrl+S save  F5 try the game  Ctrl+Z/Y undo/redo  [ ] model" },
  play = { "up/down        model", "left/right     animation", "space          play / pause",
           ", .            a frame back / on", "< >            slower / faster", "a d / w s      turn / tilt the view",
           "+ -            zoom", "o              spin  k bones  l light", "b              mix with the next animation",
           "f              frame the model" },
  build = { "arrows         move the cell", "PgUp PgDn      a level up / down", "space          put (block, tile, paint)",
            "Backspace      remove (block, tile)", "1 2 3          block / tile / paint", "f              the side (tile, paint)",
            "Tab            tiles and colours of the sheet", "r / h          turn / mirror the tile", "x              pick from a face",
            "q e / w s      turn / tilt the view", "+ -  g  0      zoom, grid, back to the model" },
  rig = { "up/down        bone", "w a s d r f    move its end (W A S D R F fine)", "Tab            head / tail",
          "n              new bone (child of this one)", "x              delete the bone", "k              auto skin (nearest bone)",
          "v              show the skin", "q e  + -       turn the view, zoom" },
  anim = { "up/down        bone", "left/right     a frame back / on (Home End)", "space          play / stop",
           "w/s a/d q/e    turn the bone 15 deg (W.. 5 deg)", "g              turn / move", "k  x           add / delete a keyframe",
           "c  v  r        copy / paste the pose, reset the bone", "n  PgUp PgDn   new animation, change",
           "l  m  < >      loop, ease, length", "Backspace x2   delete the animation", ", .  + -       turn the view, zoom" },
  menu = { "up/down        choose", "Enter          select", "Esc            back" },
  pad = { "pad: Y + left/right page  Y + B menu  Y + up/down model  Y + A undo",
          "play: X + pad view  build: A put  B remove  X side  Y tiles  X + pad",
          "rig: A + pad move  B head/tail  X new  anim: A/X + pad turn  B play" },
}

local function draw_keys()
  local list = {}
  for _, l in ipairs(KEYS.all) do list[#list + 1] = l end
  list[#list + 1] = ""
  for _, l in ipairs(KEYS[page] or {}) do list[#list + 1] = l end
  list[#list + 1] = ""
  for _, l in ipairs(KEYS.pad) do list[#list + 1] = l end
  local h = (#list + 2) * 16
  local y0 = max(16, (H - h) // 32 * 16)
  rectfill(16, y0, W - 32, h, C_PANEL)
  rect(16, y0, W - 32, h, C_ACC)
  for i, l in ipairs(list) do print(l, 32, y0 + i * 16, i <= #KEYS.all and C_ACC or C_TEXT) end
end

----------------------------------------------------------------- main

function _init()
  keyp()                                 -- typing on: the keyboard types
  -- the tiles of a new project: this cartridge's own sheet (the starter
  -- tiles of bm Studio)
  starter = {}
  local i = 0
  for y = 0, 47 do
    for x = 0, 255 do
      i = i + 1
      starter[i] = sget(x, y) or false
    end
  end
  last_t = time()
  local a = cart_arg()
  if a and a.path and load_project(a.path) then
    local back = a.back and saved()
    if a.error then
      go("menu")
      say("the game stopped: " .. a.error:sub(1, 60), C_ERR, 400)
    elseif back and back.page then
      select_model(back.model or 1)
      go(back.page)
      say("back from the game", C_ACC)
    else
      go("play")
    end
  else
    new_project()
    go("menu")
    files, fsel = list_files(), 1
    choosing = #files > 0
    say(choosing and "open a .bm with 3D models, or Esc for a new project" or "a new project: F2 builds", C_ACC, 400)
  end
end

local function global_key(k)
  if k == "f1" then go("play"); return true
  elseif k == "f2" then go("build"); return true
  elseif k == "f3" then go("rig"); return true
  elseif k == "f4" then go("anim"); return true
  elseif k == "esc" and page ~= "menu" and not picker.open then go("menu"); return true
  elseif k == "^s" then if proj.save then save_project() else go("menu"); say("choose Save as", C_ERR) end; return true
  elseif k == "f5" or k == "^r" then run_project(); return true
  elseif k == "^z" then do_undo(undo, redo, "undo"); refresh(); return true
  elseif k == "^y" then do_undo(redo, undo, "redo"); refresh(); return true
  elseif (k == "[" or k == "]") and page ~= "menu" and not picker.open then
    select_model(cur + (k == "]" and 1 or -1)); go(page)
    say("model " .. M().name, C_ACC, 60)
    return true
  end
  return false
end

local PAGES = { "play", "build", "rig", "anim", "menu" }

function _update()
  frame = frame + 1
  if msg_t > 0 then msg_t = msg_t - 1 end
  if confirm_t > 0 then confirm_t = confirm_t - 1 end
  local now = time()
  local dt = clamp(now - last_t, 0, 0.1)
  last_t = now
  read_pad()

  while true do
    local k = keyp()
    if not k then break end
    if help then help = false
    elseif k == "?" and not input then help = true
    elseif page == "menu" and (input or choosing) then menu_key(k)
    elseif not global_key(k) then
      if page == "play" then play_key(k)
      elseif page == "build" then build_key(k)
      elseif page == "rig" then rig_key(k)
      elseif page == "anim" then anim_key(k)
      else menu_key(k) end
    end
  end

  -- gamepad: Y + ... for the pages and the model
  if btn(7) and not picker.open then
    if btnp(0) or btnp(1) then
      local i = 1
      for n, p in ipairs(PAGES) do if p == page then i = n end end
      go(PAGES[(i - 1 + (btnp(1) and 1 or -1)) % #PAGES + 1])
    elseif btnp(5) then go("menu")
    elseif btnp(4) then do_undo(undo, redo, "undo"); refresh()
    elseif btnp(2) or btnp(3) then
      if page == "anim" then anim_key(btnp(2) and "pgup" or "pgdn")
      else select_model(cur + (btnp(3) and 1 or -1)); go(page); say("model " .. M().name, C_ACC, 60) end
    end
  elseif page == "menu" then
    if input then
      if btnp(5) then input = nil end
    else
      if rp[2] then menu_key("up") elseif rp[3] then menu_key("down")
      elseif btnp(4) then menu_key("ok") elseif btnp(5) then menu_key("back") end
    end
  elseif page == "play" then play_pad()
  elseif page == "build" then build_pad()
  elseif page == "rig" then rig_pad()
  elseif page == "anim" then anim_pad() end

  if page == "play" then play_update(dt)
  elseif page == "build" then build_update()
  elseif page == "anim" then anim_update(dt) end
end

function _draw()
  if page == "play" then draw_play()
  elseif page == "build" then draw_build()
  elseif page == "rig" then draw_rig()
  elseif page == "anim" then draw_anim()
  else draw_menu() end

  -- tab bar
  rectfill(0, 0, W, 16, C_BAR)
  local tabs = { { "play", "F1 play" }, { "build", "F2 build" }, { "rig", "F3 rig" }, { "anim", "F4 animate" },
                 { "menu", "Esc menu" } }
  local x = 0
  for _, t in ipairs(tabs) do
    local s = " " .. t[2] .. " "
    if t[1] == page then rectfill(x, 0, #s * 8, 16, C_SEL) end
    print(s, x, 0, t[1] == page and 0xFFFFFF or C_DIM)
    x = x + #s * 8 + 4
  end
  local name = (proj.save or "untitled") .. (dirty and "*" or "")
  print(name, W - #name * 8 - 8, 0, dirty and C_ACC or C_DIM)

  -- status bar
  rectfill(0, STATUS_Y, W, H - STATUS_Y, C_BAR)
  local status
  if msg_t > 0 and msg then status = msg
  elseif page == "menu" then status = "up/down choose, Enter select"
  else status = (M() and ("model " .. cur .. "/" .. #models .. ": " .. M().name) or "") end
  if #status <= 62 then status = status .. string.rep(" ", 65 - #status) .. "F12 or ?: keys" end
  print(status:sub(1, 79), 0, STATUS_Y, (msg_t > 0 and msg_c) or C_TEXT)
  if keyheld("f12") or help then draw_keys() end
end
