-- bm Mesh: the 3D meshes of a .bm, on the console.
-- Reads the models of the MESH section (bm Studio, on the PC and the console) and the
-- meshes the game builds in its own code (mesh(), mesh_sphere(),
-- mesh_cube(): cart_meshes() runs the code apart and keeps them), edits
-- their vertices and faces and writes them back into the .bm:
--  - as models: the MESH section (8), model("name") in the game; a model
--    with a skeleton of bm Animator keeps it (the ANIM part, 9, follows
--    the vertices);
--  - as code: functions mesh_<name>() at the end of main.lua, between the
--    lines "-- [bm Mesh begin]" and "-- [bm Mesh end]" (bm Mesh rewrites
--    only those lines; the rest of the code stays as it is).
-- Mesh -> model and model -> mesh: m and c on the list. Everything else in
-- the file (sheet, map, cover, sounds, other models) stays as it is.
-- F1 list, F2 edit, Esc menu, Ctrl+S save, F5 try the game; hold F12 (or
-- press ?) for the keys. Gamepad: X tap = the commands, Y + left/right page.

local W, H = SCREEN_W, SCREEN_H
local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x707890, 0xFFC050, 0xFF6060, 0x3050A0
local C_SKY, C_GRID, C_WIRE, C_PT, C_HOT = 0x262C3E, 0x3A4258, 0x7A88B0, 0xFFE070, 0x60E0FF
local KIND_C = { model = 0x80C8FF, code = 0x90E090, game = 0xE0A060 }
local KIND_L = { model = "M", code = "C", game = "G" }
local KIND_ORDER = { model = 1, code = 2, game = 3 }
local AXIS = { "x", "y", "z" }
local SEC_MESH, SEC_ANIM = 8, 9                  -- section types (src/bm/bm.h)
local TEXTURED = 0x80000000
local LIMIT_V, LIMIT_F, LIMIT_MODELS = 4096, 16384, 256
local PANEL_W = 208                              -- the list on the left
local HINT_Y, STATUS_Y = H - 40, H - 24          -- text on rows of 16 pixels
local INFO_X = PANEL_W + 16
local FOCAL = (W / 2) / math.tan(math.rad(30))   -- camera3d with fov 60
local BEGIN, END = "-- [bm Mesh begin]", "-- [bm Mesh end]"

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
-- the steps of the tools (, and . change them)
local MOVE_STEPS = { 0.01, 0.05, 0.1, 0.25, 0.5, 1 }
local ROT_STEPS = { 1, 5, 15, 30, 45, 90 }
local SCALE_STEPS = { 1.01, 1.05, 1.1, 1.25, 1.5, 2 }

----------------------------------------------------------------- MESH and ANIM (src/bm/bm.h)

local function name16(s)
  s = s:sub(1, 16)
  return s .. string.rep("\0", 16 - #s)
end

local function cname(b, pos) return b:sub(pos, pos + 15):match("^[^\0]*") end

-- the models of a MESH section: { {name, chunk}, ... } and the inset (u16)
local function split_mesh(bin)
  local out = {}
  if not bin or #bin < 8 then return out, 64 end
  local n, inset = sunpack("<I2I2", bin)
  local pos = 9
  for _ = 1, n do
    local nv, nf = sunpack("<I2I2", bin, pos + 16)
    local size = 24 + nv * 12 + nf * 24
    out[#out + 1] = { cname(bin, pos), bin:sub(pos, pos + size - 1) }
    pos = pos + size
  end
  return out, inset
end

-- the skeletons of an ANIM section, by model name
local function split_anim(bin)
  local out = {}
  if not bin or #bin < 8 then return out end
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

-- an item: { kind = "model" | "code" | "game", name,
--   v = { {x, y, z}, ... }, f = { {a, b, c, colour, uv}, ... } (indices
--   from 1; colour 0xRRGGBB or -1 = textured, uv = 6 sheet pixels),
--   vb = { the bone of each vertex, from 0 } and ac (the ANIM part) for a
--   model with a skeleton, mc (the MESH part as read, while unchanged) }
local function model_item(name, mc, ac)
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
local function encode_model(it)
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

-- its skeleton in the ANIM section: the bones and animations as they
-- were, the bone of each vertex from vb (vertices added and removed)
local function encode_rig(it)
  local ac = it.ac
  if not ac then return nil end
  if not it.vb then return not it.dirty and name16(it.name) .. ac:sub(17) or nil end
  local nb, nc, nv = sunpack("<I2I2I2", ac, 17)
  local head = 25 + nb * 44
  local vb = {}
  for i = 1, #it.v do vb[i] = string.char(clamp(it.vb[i] or 0, 0, nb - 1)) end
  local n = #it.v
  return name16(it.name) .. spack("<I2I2I2I2", nb, nc, n, 0) .. ac:sub(25, head - 1) .. table.concat(vb) ..
         string.rep("\0", (4 - n % 4) % 4) .. ac:sub(head + ((nv + 3) & ~3))
end

----------------------------------------------------------------- code: the block of main.lua

-- a number as Lua code: at most 5 decimals, no exponent
local function num(v)
  if v == floor(v) and abs(v) < 1e9 then return string.format("%d", floor(v)) end
  local s = string.format("%.5f", v):gsub("0+$", ""):gsub("%.$", "")
  if s == "-0" then s = "0" end
  return s
end

local function code_of(it)
  local out = { "function mesh_" .. it.name .. "()",
                string.format("  -- %s: %d vertices, %d triangles (bm Mesh)", it.name, #it.v, #it.f),
                "  return mesh({" }
  local row, tex = {}, false
  local function flush(last)
    if #row > 0 and (last or #row == 4) then out[#out + 1] = "    " .. table.concat(row, " "); row = {} end
  end
  for i, p in ipairs(it.v) do
    row[#row + 1] = num(p[1]) .. ", " .. num(p[2]) .. ", " .. num(p[3]) .. ","
    flush(i == #it.v)
  end
  out[#out + 1] = "  }, {"
  for i, f in ipairs(it.f) do
    local c = f[4] == -1 and "-1" or string.format("0x%06X", f[4] & 0xFFFFFF)
    if f[4] == -1 and f[5] then tex = true end
    row[#row + 1] = f[1] .. ", " .. f[2] .. ", " .. f[3] .. ", " .. c .. ","
    flush(i == #it.f)
  end
  if tex then
    out[#out + 1] = "  }, {"
    for i, f in ipairs(it.f) do
      local u = f[4] == -1 and f[5] or { 0, 0, 0, 0, 0, 0 }
      local s = {}
      for k = 1, 6 do s[k] = num(u[k]) end
      row[#row + 1] = table.concat(s, ", ") .. ","
      if #row == 2 or i == #it.f then out[#out + 1] = "    " .. table.concat(row, " "); row = {} end
    end
  end
  out[#out + 1] = "  })"
  out[#out + 1] = "end"
  return table.concat(out, "\n")
end

local function make_block(codes)
  if #codes == 0 then return nil end
  local out = { BEGIN .. " meshes written by bm Mesh, the mesh editor of the console:",
                "-- it rewrites these lines down to [bm Mesh end]. Use them in _init or later,",
                "-- e.g. local m = mesh_" .. codes[1].name .. "()  then  draw3d(m, x, y, z)" }
  for _, it in ipairs(codes) do out[#out + 1] = code_of(it) end
  out[#out + 1] = END
  return table.concat(out, "\n") .. "\n"
end

-- main.lua with the block in place of the old one (at the end if new;
-- block nil: the old one goes away)
local function put_block(lua, block)
  local s = lua:find(BEGIN, 1, true)
  local e = s and lua:find(END, s, true)
  if s and e then
    local after = lua:sub((lua:find("\n", e, true) or #lua) + 1)
    local before = lua:sub(1, s - 1)
    if not block then return (before:gsub("\n\n+$", "\n")) .. after end
    return before .. block .. after
  end
  if not block then return lua end
  if lua ~= "" and lua:sub(-1) ~= "\n" then lua = lua .. "\n" end
  return lua .. "\n" .. block
end

-- the meshes of the block (bm Mesh's own format, read back)
local function parse_block(lua)
  local s = lua:find(BEGIN, 1, true)
  if not s then return {} end
  local e = lua:find(END, s, true)
  if not e then return {}, "the line " .. END .. " is missing: the code meshes are not read" end
  local out, bad = {}, nil
  for name, body in lua:sub(s, e - 1):gmatch("function mesh_([%w_]+)%(%)(.-)\nend") do
    local b = body:find("mesh(", 1, true)
    local parts = {}
    if b then
      for part in body:sub(b + 5):gmatch("%b{}") do
        local t = {}
        for tok in part:gmatch("[^%s,{}]+") do t[#t + 1] = tonumber(tok) end
        parts[#parts + 1] = t
      end
    end
    local v, f, uv = parts[1] or {}, parts[2] or {}, parts[3]
    local it = { kind = "code", name = name, v = {}, f = {} }
    local ok = #v >= 3 and #f >= 4
    for i = 1, #v // 3 do
      it.v[i] = { v[i * 3 - 2], v[i * 3 - 1], v[i * 3] }
      if not (it.v[i][1] and it.v[i][2] and it.v[i][3]) then ok = false end
    end
    for i = 1, #f // 4 do
      local q = { f[i * 4 - 3], f[i * 4 - 2], f[i * 4 - 1], f[i * 4] }
      for k = 1, 3 do if not q[k] or q[k] < 1 or q[k] > #it.v or q[k] ~= floor(q[k]) then ok = false end end
      if not q[4] then ok = false
      elseif q[4] == -1 then
        if uv and #uv >= i * 6 then q[5] = { table.unpack(uv, i * 6 - 5, i * 6) } else q[4] = 0xFFFFFF end
      end
      it.f[i] = q
    end
    if ok then out[#out + 1] = it else bad = "mesh_" .. name .. " cannot be read: left out" end
  end
  return out, bad
end

-- a mesh the game's code builds (cart_meshes)
local function game_item(c)
  local it = { kind = "game", name = c.name, v = {}, f = {}, from = c.kind }
  for i = 1, #c.verts // 3 do it.v[i] = { c.verts[i * 3 - 2], c.verts[i * 3 - 1], c.verts[i * 3] } end
  for i = 1, #c.faces // 4 do
    local f = { c.faces[i * 4 - 3], c.faces[i * 4 - 2], c.faces[i * 4 - 1], c.faces[i * 4] }
    if f[4] == -1 then
      if c.uv then f[5] = { table.unpack(c.uv, i * 6 - 5, i * 6) } else f[4] = 0xFFFFFF end
    end
    it.f[i] = f
  end
  return it
end

local function signature(it)
  local t = { #it.v, #it.f }
  for _, p in ipairs(it.v) do t[#t + 1] = string.format("%.3f,%.3f,%.3f", p[1], p[2], p[3]) end
  for _, f in ipairs(it.f) do t[#t + 1] = f[1] .. "," .. f[2] .. "," .. f[3] .. "," .. f[4] end
  return table.concat(t, ";")
end

----------------------------------------------------------------- the file

local proj = { path = nil, title = "", lua = "", warn = nil }
local items = {}
local cur = 1
local inset = 64             -- the MESH header's, kept
local dirty = false          -- something to save
local code_dirty = false     -- the block of main.lua changes on save
local page = "list"
local msg, msg_c, msg_t = nil, C_TEXT, 0
local frame = 0
local undo, redo = {}, {}

local function say(s, c, t) msg, msg_c, msg_t = s, c or C_TEXT, t or 220 end
local function I() return items[cur] end

local function unique_name(kind, base, skip)
  local lim = kind == "code" and 24 or 16
  if kind == "code" then
    base = base:gsub("[^%w_]", "_")
    if base == "" or base:match("^%d") then base = "m" .. base end
  else
    base = base:gsub("%c", "")
    if base == "" then base = "model" end
  end
  base = base:sub(1, lim)
  local function used(n)
    for i, it in ipairs(items) do if it.kind == kind and it.name == n and i ~= skip then return true end end
    return false
  end
  if not used(base) then return base end
  for k = 2, 999 do
    local s = tostring(k)
    local n = base:sub(1, lim - #s) .. s
    if not used(n) then return n end
  end
  return base
end

-- after the last item of its kind (models, code, game); its index
local function insert_item(it)
  local pos = 1
  for i, x in ipairs(items) do if KIND_ORDER[x.kind] <= KIND_ORDER[it.kind] then pos = i + 1 end end
  table.insert(items, pos, it)
  undo, redo = {}, {}         -- the indexes moved
  return pos
end

local function copy_geo(src, kind, name)
  local it = { kind = kind, name = unique_name(kind, name), v = {}, f = {}, dirty = true }
  for i, p in ipairs(src.v) do it.v[i] = { p[1], p[2], p[3] } end
  for i, f in ipairs(src.f) do
    it.f[i] = { f[1], f[2], f[3], f[4], f[5] and { table.unpack(f[5]) } or nil }
  end
  return it
end

-- the mesh the kernel draws (made again after each change); false if none
local function view_of(it)
  if it.view ~= nil then return it.view end
  if #it.f == 0 or #it.v == 0 or #it.v > LIMIT_V or #it.f > LIMIT_F then it.view = false; return false end
  local v, f, uv, tex = {}, {}, {}, false
  for i, p in ipairs(it.v) do v[i * 3 - 2], v[i * 3 - 1], v[i * 3] = p[1], p[2], p[3] end
  for i, q in ipairs(it.f) do
    f[i * 4 - 3], f[i * 4 - 2], f[i * 4 - 1], f[i * 4] = q[1], q[2], q[3], q[4]
    local u = q[5]
    if q[4] == -1 and u then
      tex = true
      for k = 1, 6 do uv[i * 6 - 6 + k] = u[k] end
    else
      for k = 1, 6 do uv[i * 6 - 6 + k] = 0 end
      if q[4] == -1 then f[i * 4] = 0xFFFFFF end
    end
  end
  local ok, m = pcall(mesh, v, f, tex and uv or nil)
  it.view = ok and m or false
  return it.view
end

local function changed(it)
  it.view, it.dirty, dirty = nil, true, true
  if it.kind == "code" then code_dirty = true end
end

local function bounds(it, list)
  local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
  local function add(p)
    for k = 1, 3 do
      if p[k] < lo[k] then lo[k] = p[k] end
      if p[k] > hi[k] then hi[k] = p[k] end
    end
  end
  if list then for _, i in ipairs(list) do add(it.v[i]) end else for _, p in ipairs(it.v) do add(p) end end
  if lo[1] > hi[1] then return { -0.5, -0.5, -0.5 }, { 0.5, 0.5, 0.5 } end
  return lo, hi
end

local function list_files()
  local out = {}
  for _, dir in ipairs({ "/carts", "/" }) do
    for _, f in ipairs(ls(dir) or {}) do
      if not f.dir and f.name:lower():match("%.bm$") then out[#out + 1] = (dir == "/" and "" or dir) .. "/" .. f.name end
    end
  end
  table.sort(out)
  return out
end

local function short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or "/carts"
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):upper():gsub("[^%w_]", "")
  if stem == "" then stem = "GAME" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".BM"
end

local reset_pages, go       -- defined with the pages

local function open_file(path)
  local p, e = cart_load(path)      -- its sheet: the textures of the models
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C_ERR); return false end
  proj = { path = path, title = p.title, lua = p.lua or "", warn = nil }
  items, undo, redo = {}, {}, {}
  local mbin, abin = cart_data(SEC_MESH), cart_data(SEC_ANIM)
  local parts, ins = split_mesh(mbin)
  inset = ins
  local rigs = split_anim(abin)
  for _, part in ipairs(parts) do items[#items + 1] = model_item(part[1], part[2], rigs[part[1]]) end
  local codes, berr = parse_block(proj.lua)
  local sigs = {}
  for _, it in ipairs(codes) do
    items[#items + 1] = it
    sigs[signature(it)] = true
  end
  local ngame = 0
  local caps, cerr = cart_meshes(path)
  for _, c in ipairs(caps or {}) do
    local it = game_item(c)
    if not sigs[signature(it)] then items[#items + 1] = it; ngame = ngame + 1 end
  end
  proj.warn = berr or (cerr and "the game's code stopped while read: " .. cerr)
  dirty, code_dirty, cur = false, false, 1
  reset_pages()
  say(string.format("%s: %d models, %d code meshes, %d from the game's code", path, #parts, #codes, ngame), C_ACC)
  return true
end

-- the sections from the items; nil and a message if they cannot be made
local function build_sections()
  local mp, ap, n, skipped = {}, {}, 0, nil
  for _, it in ipairs(items) do
    if it.kind == "model" then
      if #it.f == 0 or #it.v == 0 then
        skipped = it.name
      else
        n = n + 1
        if n > LIMIT_MODELS then return nil, "more than " .. LIMIT_MODELS .. " models" end
        it.new_mc = encode_model(it)
        it.new_ac = encode_rig(it)
        mp[#mp + 1] = it.new_mc
        if it.new_ac then ap[#ap + 1] = it.new_ac end
      end
    end
  end
  local mesh = #mp > 0 and spack("<I2I2I4", #mp, inset, 0) .. table.concat(mp) or false
  local anim = #ap > 0 and spack("<I2I2I4", #ap, 0, 0) .. table.concat(ap) or false
  return mesh, anim, skipped
end

local function full_lua()
  local codes = {}
  for _, it in ipairs(items) do if it.kind == "code" and #it.f > 0 then codes[#codes + 1] = it end end
  return put_block(proj.lua, make_block(codes))
end

local function save_to(path, from)
  if not proj.path then say("open a .bm first: Esc > Open", C_ERR); return false end
  local mesh, anim, skipped = build_sections()
  if mesh == nil then say("cannot save: " .. anim, C_ERR); return false end
  local t = { sections = { [SEC_MESH] = mesh, [SEC_ANIM] = anim }, from = from }
  local lua = code_dirty and full_lua() or nil
  t.lua = lua
  local ok, err = cart_write(path, t)
  if not ok then say("save failed: " .. tostring(err), C_ERR); return false end
  if lua then proj.lua = lua end
  for _, it in ipairs(items) do
    if it.kind == "model" and it.new_mc then
      it.mc, it.mc_name, it.ac = it.new_mc, it.name, it.new_ac
      if not it.ac then it.vb = nil end
    end
    it.dirty, it.new_mc, it.new_ac = false, nil, nil
  end
  cart_data(SEC_MESH, mesh or nil)   -- model() in this session sees them too
  cart_data(SEC_ANIM, anim or nil)
  proj.path = path
  dirty, code_dirty = false, false
  say("saved " .. path .. (skipped and ("  (" .. skipped .. " has no faces: left out)") or ""), C_ACC)
  return true
end

----------------------------------------------------------------- undo

-- a snapshot: the item's vertices, faces and bones packed in a string
local function pack_geo(it)
  local out = { spack("<I4I4", #it.v, #it.f) }
  for _, p in ipairs(it.v) do out[#out + 1] = spack("<ddd", p[1], p[2], p[3]) end
  for _, f in ipairs(it.f) do
    local u = f[5]
    out[#out + 1] = spack("<I2I2I2i4B", f[1], f[2], f[3], f[4], u and 1 or 0)
    if u then out[#out + 1] = spack("<dddddd", u[1], u[2], u[3], u[4], u[5], u[6]) end
  end
  if it.vb then
    local b = {}
    for i = 1, #it.v do b[i] = string.char(it.vb[i] or 0) end
    out[#out + 1] = table.concat(b)
  end
  return table.concat(out)
end

local function unpack_geo(it, s)
  local nv, nf, pos = sunpack("<I4I4", s)
  it.v, it.f = {}, {}
  for i = 1, nv do
    local x, y, z
    x, y, z, pos = sunpack("<ddd", s, pos)
    it.v[i] = { x, y, z }
  end
  for i = 1, nf do
    local a, b, c, col, has
    a, b, c, col, has, pos = sunpack("<I2I2I2i4B", s, pos)
    local f = { a, b, c, col }
    if has == 1 then
      local u = { sunpack("<dddddd", s, pos) }
      pos = u[7]
      u[7] = nil
      f[5] = u
    end
    it.f[i] = f
  end
  if it.vb then
    it.vb = {}
    for i = 1, nv do it.vb[i] = s:byte(pos + i - 1) end
  end
end

local function snapshot()
  local it = I()
  return { idx = cur, name = it.name, kind = it.kind, geo = pack_geo(it) }
end

local on_restore             -- the edit page: the selection after an undo

local function restore(s)
  local it = items[s.idx]
  if not it or it.name ~= s.name or it.kind ~= s.kind then return false end
  cur = s.idx
  unpack_geo(it, s.geo)
  changed(it)
  if on_restore then on_restore() end
  return true
end

-- before an edit: a mesh of the game's code is copied as a model first
-- (its code cannot be rewritten); returns the item to change
local function begin_edit()
  local it = I()
  if it.kind == "game" then
    local c = copy_geo(it, "model", it.name)
    cur = insert_item(c)
    say("a copy as the model " .. c.name .. " (the game's code stays as it is): edit it", C_ACC, 300)
    it = c
  end
  undo[#undo + 1] = snapshot()
  if #undo > 30 then table.remove(undo, 1) end
  redo = {}
  return it
end

-- after it: too big, undone
local function finish_edit(what)
  local it = I()
  if #it.v > LIMIT_V or #it.f > LIMIT_F then
    local s = table.remove(undo)
    if s then restore(s) end
    say("cannot: at most " .. LIMIT_V .. " vertices and " .. LIMIT_F .. " triangles", C_ERR)
    return false
  end
  changed(it)
  if what then say(what, C_ACC, 150) end
  return true
end

local function do_undo(stack, other, what)
  local s = table.remove(stack)
  if not s then say("nothing to " .. what, C_DIM); return end
  if not items[s.idx] or items[s.idx].name ~= s.name then say("cannot " .. what .. ": the list changed", C_ERR); return end
  cur = s.idx
  other[#other + 1] = snapshot()
  restore(s)
  say(what .. ": " .. I().name, C_ACC, 90)
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

-- the overlays: a list to choose from, a line to type, a question
local pick = nil             -- { title, rows = { {label, fn}, ... }, sel }
local input = nil            -- { label, text, done }
local help = false
local confirm_t, confirm_what = 0, nil

-- the keys as chips (prompt(), the look of all the Dev apps): the labels
-- stay on the font's 8 px columns
local function snap(x) return (x + 7) // 8 * 8 end

-- the keys of the page on the hint row: { { {keys...}, label }, ... }, as many as fit
local function hint(list)
  local x = 0
  for _, h in ipairs(list) do
    local kx = x
    for _, k in ipairs(h[1]) do kx = kx + prompt(k) + 1 end
    if snap(kx + 2) + #h[2] * 8 > W then break end
    for _, k in ipairs(h[1]) do x = prompt(k, x, HINT_Y) + 1 end
    x = print(h[2], snap(x + 2), HINT_Y, C_DIM) + 12
  end
end

-- a key as a chip, or the pad's button when a pad was used last and the
-- action has one; then its label. Returns the x after it.
local function chip_hint(key, pad, label, x, y, c)
  local li = lastinput()
  x = prompt(pad and (li == "ds4" or li == "pad") and pad or key, x, y)
  return print(label, snap(x + 3), y, c or C_DIM) + 12
end

local function choose(title, rows, sel) pick = { title = title, rows = rows, sel = sel or 1 } end
local function ask(label, text, done) input = { label = label, text = text, done = done } end

-- true when asked twice in a row
local function confirmed(what, text)
  if confirm_what == what and confirm_t > 0 then confirm_what = nil; return true end
  confirm_what, confirm_t = what, 150
  say(text, C_ERR)
  return false
end

local function pick_key(k)
  local p = pick
  if k == "up" then p.sel = (p.sel - 2) % #p.rows + 1
  elseif k == "down" then p.sel = p.sel % #p.rows + 1
  elseif k == "pgup" then p.sel = max(1, p.sel - 12)
  elseif k == "pgdn" then p.sel = min(#p.rows, p.sel + 12)
  elseif k == "esc" or k == "back" then pick = nil
  elseif k == "\n" or k == "ok" then
    pick = nil
    p.rows[p.sel][2]()
  end
end

local function input_key(k)
  if k == "\n" then local d, t = input.done, input.text; input = nil; d(t)
  elseif k == "esc" then input = nil
  elseif k == "\b" then input.text = input.text:sub(1, -2)
  elseif #k == 1 and k:byte() >= 32 and #input.text < 40 then input.text = input.text .. k end
end

local function draw_pick()
  local p = pick
  local rows = min(#p.rows, 14)
  local h = (rows + 2) * 16
  local y0 = max(32, (H - h) // 32 * 16)
  rectfill(40, y0, 560, h, C_PANEL)
  rect(40, y0, 560, h, C_ACC)
  local tx = print(p.title, 56, y0, C_ACC) + 16
  chip_hint("esc", "B", "back", chip_hint("enter", "A", "choose", tx, y0), y0)
  local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
  for i = first, min(#p.rows, first + rows - 1) do
    local y = y0 + 16 + (i - first) * 16
    if i == p.sel then rectfill(48, y, 544, 16, C_SEL) end
    print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C_TEXT)
  end
  if first + rows - 1 < #p.rows then print("v", 576, y0 + rows * 16, C_DIM) end
end

local function draw_input()
  rectfill(80, 144, 480, 64, C_PANEL)
  rect(80, 144, 480, 64, C_ACC)
  print(input.label, 96, 160, C_DIM)
  print(input.text .. ((frame // 20) % 2 == 0 and "_" or ""), 96, 176, C_TEXT)
  chip_hint("esc", nil, "cancel", chip_hint("enter", nil, "ok", 336, 144), 144)
end

----------------------------------------------------------------- 3D helpers

-- an orbit camera: target, yaw, pitch, distance; the target shows `dx`
-- pixels right of the middle of the screen and `dy` below it
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
  cam.f, cam.r, cam.u = f, r, u
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

-- the world axis nearest to where the camera looks, and the one to its right
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

local function draw_grid(cx, level, cz, n, step, c)
  for i = -n, n do
    local x, z = cx + i * step, cz + i * step
    seg({ x, level, cz - n * step }, { x, level, cz + n * step }, c)
    seg({ cx - n * step, level, z }, { cx + n * step, level, z }, c)
  end
end

local function aim(cam, it, list, k)
  local lo, hi = bounds(it, list)
  if not list then cam.floor = lo[2] end
  cam.tx, cam.ty, cam.tz = (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2, (lo[3] + hi[3]) / 2
  cam.size = max(hi[1] - lo[1], hi[2] - lo[2], hi[3] - lo[3], 0.25)
  cam.dist = cam.size * (k or 1.8)
end

-- the floor grid under the item, with its size
local function floor_grid(cam)
  local s = cam.size or 1
  local step = s > 12 and 2 or (s > 6 and 1 or (s > 2 and 0.5 or 0.25))
  draw_grid(round(cam.tx / step) * step, cam.floor or 0, round(cam.tz / step) * step, 8, step, C_GRID)
end

-- the three axes, in a corner
local function draw_gizmo(cam, x, y)
  local cols = { 0xFF6060, 0x60E060, 0x6090FF }
  for k = 1, 3 do
    local a = { 0, 0, 0 }
    a[k] = 1
    local dx = a[1] * cam.r[1] + a[2] * cam.r[2] + a[3] * cam.r[3]
    local dy = a[1] * cam.u[1] + a[2] * cam.u[2] + a[3] * cam.u[3]
    line(x, y, x + dx * 18, y - dy * 18, cols[k])
    print(AXIS[k], x + dx * 24 - 3, y - dy * 24 - 6, cols[k])
  end
end

local function kind_text(it)
  if it.kind == "model" then return "model (MESH section)"
  elseif it.kind == "code" then return "code: mesh_" .. it.name .. "()"
  else return "built by the game's code (" .. (it.from or "mesh") .. ")" end
end

local function draw_list(x, y, rows)
  print("MESHES " .. #items, x + 16, y, C_DIM)
  local first = clamp(cur - rows // 2, 1, max(1, #items - rows + 1))
  for i = first, min(#items, first + rows - 1) do
    local it = items[i]
    local yy = y + 16 + (i - first) * 16
    if i == cur then rectfill(x, yy, PANEL_W, 16, C_SEL) end
    print(KIND_L[it.kind], x + 8, yy, KIND_C[it.kind])
    print(it.name:sub(1, 22) .. (it.dirty and "*" or ""), x + 24, yy, i == cur and 0xFFFFFF or C_TEXT)
  end
  if first > 1 then print("^", x + PANEL_W - 16, y + 16, C_DIM) end
  if first + rows - 1 < #items then print("v", x + PANEL_W - 16, y + rows * 16, C_DIM) end
end


-- the pages: what each one gives the others (the rest stays inside its block)
local lp, list_reset, list_key, list_pad, list_actions, draw_list_page
local ed, edit_reset, edit_key, edit_pad, edit_actions, draw_edit

----------------------------------------------------------------- list page
do

lp = { cam = { yaw = 0.6, pitch = -0.4, dist = 4, tx = 0, ty = 0, tz = 0 }, spin = true }

function list_reset()
  local it = I()
  if it then aim(lp.cam, it, nil, 2.4) end
end

local function select_item(i)
  if #items == 0 then return end
  cur = (i - 1) % #items + 1
  list_reset()
  edit_reset()
end

-- mesh -> model, mesh -> code (and model <-> code): a copy, the old one stays
local function copy_as(kind)
  local it = I()
  if not it then return end
  if kind == "model" and #it.f == 0 then say("no faces: nothing to copy", C_ERR); return end
  local c = copy_geo(it, kind, it.name)
  if kind == "model" then
    local n = 0
    for _, x in ipairs(items) do if x.kind == "model" then n = n + 1 end end
    if n >= LIMIT_MODELS then say("at most " .. LIMIT_MODELS .. " models", C_ERR); return end
  end
  if kind == "code" then code_dirty = true end
  dirty = true
  select_item(insert_item(c))
  if kind == "model" then
    say("copied as the model " .. c.name .. ": in the game model(\"" .. c.name .. "\")  (Ctrl+S saves)", C_ACC, 400)
  else
    say("copied as code: mesh_" .. c.name .. "() at the end of main.lua" .. (it.ac and " (no skeleton in code)" or "") ..
        "  (Ctrl+S saves)", C_ACC, 400)
  end
end

local function rename()
  local it = I()
  if not it then return end
  if it.kind == "game" then say("its name comes from the game's code: copy it (m or c) to name it", C_ERR); return end
  ask(it.kind == "code" and "name (letters, digits, _): mesh_<name>()" or "model name (up to 16 letters)", it.name,
      function(t)
        t = t:gsub("^mesh_", "")
        if t == "" then return end
        it.name = unique_name(it.kind, t, cur)
        it.view = nil
        dirty = true
        if it.kind == "code" then code_dirty = true end
        undo, redo = {}, {}
        say("renamed: " .. it.name, C_ACC)
      end)
end

local function duplicate()
  local it = I()
  if not it then return end
  copy_as(it.kind == "game" and "model" or it.kind)
  local c = I()
  if it.kind == "model" and it.ac and it.vb and c ~= it then     -- the copy keeps the skeleton
    c.ac, c.bones, c.clips, c.vb = it.ac, it.bones, it.clips, { table.unpack(it.vb) }
  end
end

local function delete()
  local it = I()
  if not it then return end
  if it.kind == "game" then say("the game's code builds it: change the code with bm Code to remove it", C_ERR); return end
  if not confirmed("del" .. cur, "press again to delete " .. it.name .. (it.ac and " (and its skeleton)" or "")) then return end
  table.remove(items, cur)
  if it.kind == "code" then code_dirty = true end
  dirty = true
  undo, redo = {}, {}
  select_item(clamp(cur, 1, max(1, #items)))
  say("deleted " .. it.name .. " (Ctrl+S saves)", C_ACC)
end

-- new models: a cube, a plane, a sphere (in the colour of the edit page)
local function orient(it, c)
  for _, f in ipairs(it.f) do
    local a, b, d = it.v[f[1]], it.v[f[2]], it.v[f[3]]
    local ux, uy, uz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
    local vx, vy, vz = d[1] - a[1], d[2] - a[2], d[3] - a[3]
    local nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    local mx, my, mz = (a[1] + b[1] + d[1]) / 3 - c[1], (a[2] + b[2] + d[2]) / 3 - c[2], (a[3] + b[3] + d[3]) / 3 - c[3]
    if nx * mx + ny * my + nz * mz < 0 then f[2], f[3] = f[3], f[2] end
  end
end

local function new_shape(shape)
  if not proj.path then say("open a .bm first: Esc > Open", C_ERR); return end
  local col = ed.colour
  local it = { kind = "model", name = unique_name("model", shape), v = {}, f = {}, dirty = true }
  if shape == "cube" then
    for _, p in ipairs({ { -0.5, 0, -0.5 }, { 0.5, 0, -0.5 }, { 0.5, 1, -0.5 }, { -0.5, 1, -0.5 },
                         { -0.5, 0, 0.5 }, { 0.5, 0, 0.5 }, { 0.5, 1, 0.5 }, { -0.5, 1, 0.5 } }) do it.v[#it.v + 1] = p end
    for _, t in ipairs({ { 1, 4, 3 }, { 1, 3, 2 }, { 5, 6, 7 }, { 5, 7, 8 }, { 1, 2, 6 }, { 1, 6, 5 },
                         { 4, 8, 7 }, { 4, 7, 3 }, { 1, 5, 8 }, { 1, 8, 4 }, { 2, 3, 7 }, { 2, 7, 6 } }) do
      it.f[#it.f + 1] = { t[1], t[2], t[3], col }
    end
  elseif shape == "plane" then
    it.v = { { -0.5, 0, -0.5 }, { 0.5, 0, -0.5 }, { 0.5, 0, 0.5 }, { -0.5, 0, 0.5 } }
    it.f = { { 1, 4, 3, col }, { 1, 3, 2, col } }
  else
    local rings, segs = 6, 12
    it.v[1] = { 0, 1, 0 }
    for r = 1, rings - 1 do
      local a = r / rings * pi
      for s = 0, segs - 1 do
        local b = s / segs * 2 * pi
        it.v[#it.v + 1] = { 0.5 * sin(a) * cos(b), 0.5 + 0.5 * cos(a), 0.5 * sin(a) * sin(b) }
      end
    end
    it.v[#it.v + 1] = { 0, 0, 0 }
    local function at(r, s) return 2 + (r - 1) * segs + s % segs end
    for s = 0, segs - 1 do
      it.f[#it.f + 1] = { 1, at(1, s), at(1, s + 1), col }
      it.f[#it.f + 1] = { #it.v, at(rings - 1, s + 1), at(rings - 1, s), col }
      for r = 1, rings - 2 do
        it.f[#it.f + 1] = { at(r, s), at(r + 1, s), at(r + 1, s + 1), col }
        it.f[#it.f + 1] = { at(r, s), at(r + 1, s + 1), at(r, s + 1), col }
      end
    end
    orient(it, { 0, 0.5, 0 })
  end
  dirty = true
  select_item(insert_item(it))
  say("new model " .. it.name .. ": F2 edits it", C_ACC)
end

function list_actions()
  local it = I()
  local rows = {
    { "Edit (Enter)", function() if I() then go("edit") end end },
    { "Copy as a model: mesh -> model (m)", function() copy_as("model") end },
    { "Copy as code: model -> mesh (c)", function() copy_as("code") end },
    { "Rename (r)", rename },
    { "Duplicate (d)", duplicate },
    { "Delete (Del)", delete },
    { "New model: cube (n)", function() new_shape("cube") end },
    { "New model: plane", function() new_shape("plane") end },
    { "New model: sphere", function() new_shape("sphere") end },
  }
  if not it then rows = { rows[7], rows[8], rows[9] } end
  choose("the mesh" .. (it and (": " .. it.name) or ""), rows)
end

function list_key(k)
  local cam = lp.cam
  if k == "up" then select_item(cur - 1)
  elseif k == "down" then select_item(cur + 1)
  elseif k == "home" then select_item(1)
  elseif k == "end" then select_item(#items)
  elseif k == "\n" then if I() then go("edit") end
  elseif k == "m" then copy_as("model")
  elseif k == "c" then copy_as("code")
  elseif k == "r" then rename()
  elseif k == "d" then duplicate()
  elseif k == "del" or k == "\b" then delete()
  elseif k == "n" then new_shape("cube")
  elseif k == " " or k == "o" then lp.spin = not lp.spin
  elseif k == "q" then cam.yaw = cam.yaw - 0.15; lp.spin = false
  elseif k == "e" then cam.yaw = cam.yaw + 0.15; lp.spin = false
  elseif k == "w" then cam.pitch = clamp(cam.pitch - 0.1, -1.45, 1.2)
  elseif k == "s" then cam.pitch = clamp(cam.pitch + 0.1, -1.45, 1.2)
  elseif k == "+" or k == "=" then cam.dist = max(0.1, cam.dist * 0.85)
  elseif k == "-" then cam.dist = min(500, cam.dist / 0.85)
  elseif k == "f" then list_reset()
  end
end

function list_pad()
  local cam = lp.cam
  if btn(6) then                     -- X + pad: turn and tilt, X + A / B: zoom
    if btn(0) then cam.yaw = cam.yaw - 0.04; lp.spin = false end
    if btn(1) then cam.yaw = cam.yaw + 0.04; lp.spin = false end
    if btn(2) then cam.pitch = clamp(cam.pitch - 0.03, -1.45, 1.2) end
    if btn(3) then cam.pitch = clamp(cam.pitch + 0.03, -1.45, 1.2) end
    if btn(4) then cam.dist = max(0.1, cam.dist * 0.98) end
    if btn(5) then cam.dist = min(500, cam.dist / 0.98) end
    return
  end
  if rp[2] then list_key("up") end
  if rp[3] then list_key("down") end
  if btnp(4) then list_key("\n") end
  if tap[6] then list_actions() end
end

function draw_list_page()
  local it = I()
  cls(C_SKY)
  zclear()
  if it then
    if lp.spin then lp.cam.yaw = lp.cam.yaw + 0.01 end
    look(lp.cam, PANEL_W / 2, 40)
    floor_grid(lp.cam)
    local v = view_of(it)
    if v then
      light3d(-0.4, 0.8, -0.5, 0.4)
      draw3d(v, 0, 0, 0, 0, 0, 0, 1, 0)
    end
  end
  rectfill(0, 16, PANEL_W, HINT_Y - 16, C_PANEL)
  draw_list(0, 32, (HINT_Y - 64) // 16)
  local x = INFO_X
  rectfill(PANEL_W, 16, W - PANEL_W, 80, C_BG)
  if it then
    print(it.name, x, 32, KIND_C[it.kind])
    print(kind_text(it), x + (#it.name + 2) * 8, 32, C_DIM)
    print(#it.v .. " vertices, " .. #it.f .. " triangles" ..
          (it.ac and (it.vb and (", skeleton: " .. it.bones .. " bones, " .. it.clips .. " animations")
                      or ", its skeleton does not fit (kept while unchanged)") or ""), x, 48, C_TEXT)
    if it.kind == "model" then print("in the game: model(\"" .. it.name .. "\")   c: copy as code", x, 64, C_DIM)
    elseif it.kind == "code" then print("in the game: mesh_" .. it.name .. "()   m: copy as a model", x, 64, C_DIM)
    else print("read only: m copies it as a model, c as code", x, 64, C_DIM) end
  elseif proj.path then
    print("no meshes in " .. proj.path, x, 32, C_TEXT)
    print("n: a new model (a cube), then F2 edits it", x, 48, C_DIM)
  else
    print("no file open: Esc > Open", x, 32, C_TEXT)
  end
  if proj.warn then
    rectfill(PANEL_W, HINT_Y - 32, W - PANEL_W, 16, C_BG)
    print(proj.warn:sub(1, 52), x, HINT_Y - 32, C_ERR)
  end
  print("M model  C code  G game's code", x, HINT_Y - 16, C_DIM)
  hint({ { { "up", "down" }, "choose" }, { { "enter" }, "edit" }, { { "m" }, "to model" }, { { "c" }, "to code" },
         { { "r" }, "rename" }, { { "d" }, "copy" }, { { "del" }, "delete" } })
end
end

----------------------------------------------------------------- edit page
do

ed = { mode = "v", sv = {}, sf = {}, order = 0, px = W // 2, py = 184, tool = nil, step = 3, colour = 0xE84A5A,
       cam = { yaw = 0.6, pitch = -0.45, dist = 4, tx = 0, ty = 0, tz = 0 },
       sx = {}, sy = {}, sz = {}, hv = nil, hf = nil, force = nil, pal = nil, last_k = nil, last_f = 0, rep = 0 }

function edit_reset()
  ed.sv, ed.sf, ed.order, ed.tool, ed.force, ed.pal = {}, {}, 0, nil, nil, nil
  ed.sx, ed.sy, ed.sz = {}, {}, {}
  local it = I()
  if it then aim(ed.cam, it, nil, 2.2) end
  ed.px, ed.py = W // 2, 184
end

-- after an undo (or a cancelled tool): only what still exists stays selected
on_restore = function()
  local it = I()
  for i in pairs(ed.sv) do if not it.v[i] then ed.sv[i] = nil end end
  for i in pairs(ed.sf) do if not it.f[i] then ed.sf[i] = nil end end
  ed.force = nil
end

local function count(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

-- the vertices the tools move: the chosen ones, or the corners of the
-- chosen faces
local function sel_verts()
  local it, out, seen = I(), {}, {}
  if ed.mode == "v" then
    for i in pairs(ed.sv) do if it.v[i] then out[#out + 1] = i end end
  else
    for fi in pairs(ed.sf) do
      local f = it.f[fi]
      if f then
        for k = 1, 3 do
          local i = f[k]
          if not seen[i] then seen[i] = true; out[#out + 1] = i end
        end
      end
    end
  end
  table.sort(out)
  return out
end

-- the faces the commands change: the chosen ones, or those with all their
-- corners chosen
local function sel_faces()
  local it, out = I(), {}
  if ed.mode == "f" then
    for fi in pairs(ed.sf) do if it.f[fi] then out[#out + 1] = fi end end
  else
    for fi, f in ipairs(it.f) do if ed.sv[f[1]] and ed.sv[f[2]] and ed.sv[f[3]] then out[#out + 1] = fi end end
  end
  table.sort(out)
  return out
end

local function select_v(i, on)
  if on then ed.order = ed.order + 1; ed.sv[i] = ed.order else ed.sv[i] = nil end
end

local function normal_of(it, f)
  local a, b, c = it.v[f[1]], it.v[f[2]], it.v[f[3]]
  local ux, uy, uz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
  local vx, vy, vz = c[1] - a[1], c[2] - a[2], c[3] - a[3]
  return { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx }
end

local function unit(v)
  local l = sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
  if l < 1e-9 then return { 0, 1, 0 } end
  return { v[1] / l, v[2] / l, v[3] / l }
end

local function flip(f)
  f[2], f[3] = f[3], f[2]
  local u = f[5]
  if u then u[3], u[4], u[5], u[6] = u[5], u[6], u[3], u[4] end
end

-- vertices no face uses go away (the selection and the bones follow)
local function compact(it)
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
  local sv = {}
  for i, o in pairs(ed.sv) do if map[i] then sv[map[i]] = o end end
  ed.sv = sv
end

local function drop_degenerate(it)
  local out = {}
  for _, f in ipairs(it.f) do if f[1] ~= f[2] and f[2] ~= f[3] and f[1] ~= f[3] then out[#out + 1] = f end end
  local n = #it.f - #out
  it.f = out
  if n > 0 then ed.sf = {} end     -- the faces moved
  return n
end

local function add_vertex(it, p, from)
  it.v[#it.v + 1] = { p[1], p[2], p[3] }
  if it.vb then it.vb[#it.v] = it.vb[from] or 0 end
  return #it.v
end

-- the screen: where each vertex is (after look()); what is under the pointer
local function project_all(it)
  local sx, sy, sz = {}, {}, {}
  for i, p in ipairs(it.v) do
    local x, y, z = project3d(p[1], p[2], p[3])
    if x and abs(x) < 8000 and abs(y) < 8000 then sx[i], sy[i], sz[i] = x, y, z end
  end
  ed.sx, ed.sy, ed.sz = sx, sy, sz
end

local function area(f)
  local sx, sy = ed.sx, ed.sy
  local a, b, c = f[1], f[2], f[3]
  if not (sx[a] and sx[b] and sx[c]) then return nil end
  return (sx[b] - sx[a]) * (sy[c] - sy[a]) - (sy[b] - sy[a]) * (sx[c] - sx[a])
end

local function face_at(px, py)
  local it = I()
  local sx, sy, sz = ed.sx, ed.sy, ed.sz
  local best, bz, back, backz = nil, 1e18, nil, 1e18
  for fi, f in ipairs(it.f) do
    local a, b, c = f[1], f[2], f[3]
    local ar = area(f)
    if ar and ar ~= 0 then
      local d1 = (sx[b] - sx[a]) * (py - sy[a]) - (sy[b] - sy[a]) * (px - sx[a])
      local d2 = (sx[c] - sx[b]) * (py - sy[b]) - (sy[c] - sy[b]) * (px - sx[b])
      local d3 = (sx[a] - sx[c]) * (py - sy[c]) - (sy[a] - sy[c]) * (px - sx[c])
      if (d1 >= 0 and d2 >= 0 and d3 >= 0) or (d1 <= 0 and d2 <= 0 and d3 <= 0) then
        local z = sz[a] + sz[b] + sz[c]
        if ar > 0 then
          if z < bz then best, bz = fi, z end
        elseif z < backz then back, backz = fi, z end
      end
    end
  end
  return best or back
end

local function hover()
  local it = I()
  ed.hv, ed.hf = nil, nil
  if ed.force then
    if ed.mode == "v" then ed.hv = it.v[ed.force] and ed.force else ed.hf = it.f[ed.force] and ed.force end
    return
  end
  if ed.mode == "v" then
    local best, bz = nil, 1e18
    for i in ipairs(it.v) do
      local x = ed.sx[i]
      if x then
        local dx, dy = x - ed.px, ed.sy[i] - ed.py
        if dx * dx + dy * dy <= 100 and ed.sz[i] < bz then best, bz = i, ed.sz[i] end
      end
    end
    ed.hv = best
  else
    ed.hf = face_at(ed.px, ed.py)
  end
end

local function centre_of(it, list)
  local c = { 0, 0, 0 }
  for _, i in ipairs(list) do for k = 1, 3 do c[k] = c[k] + it.v[i][k] end end
  for k = 1, 3 do c[k] = c[k] / max(1, #list) end
  return c
end

----------------------------------------------------------------- the tools: move, rotate, scale

local function mat_mul(a, b)
  local m = {}
  for r = 0, 2 do
    for c = 1, 3 do m[r * 3 + c] = a[r * 3 + 1] * b[c] + a[r * 3 + 2] * b[3 + c] + a[r * 3 + 3] * b[6 + c] end
  end
  return m
end

local function rot_matrix(ax, a)
  local x, y, z = ax[1], ax[2], ax[3]
  local c, s = cos(a), sin(a)
  local t = 1 - c
  return { t * x * x + c, t * x * y - s * z, t * x * z + s * y,
           t * x * y + s * z, t * y * y + c, t * y * z - s * x,
           t * x * z - s * y, t * y * z + s * x, t * z * z + c }
end

local function axis_vec(k, s) local a = { 0, 0, 0 }; a[k] = s or 1; return a end

local function tool_start(kind, axis, keep)
  local vs = sel_verts()
  if #vs == 0 then say("choose vertices or faces first: space under the pointer, a all", C_ERR); return end
  local it = keep and I() or begin_edit()
  local base = {}
  for _, i in ipairs(vs) do base[i] = { it.v[i][1], it.v[i][2], it.v[i][3] } end
  local n = { 0, 0, 0 }
  for _, fi in ipairs(sel_faces()) do
    local fn = unit(normal_of(it, it.f[fi]))
    for k = 1, 3 do n[k] = n[k] + fn[k] end
  end
  ed.tool = { kind = kind, vs = vs, base = base, c = centre_of(it, vs), n = unit(n), axis = axis,
              d = { 0, 0, 0 }, m = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, s = { 1, 1, 1 }, deg = 0 }
end

local function tool_apply()
  local t, it = ed.tool, I()
  for _, i in ipairs(t.vs) do
    local b, p = t.base[i], it.v[i]
    if t.kind == "move" then
      p[1], p[2], p[3] = b[1] + t.d[1], b[2] + t.d[2], b[3] + t.d[3]
    else
      local x, y, z = b[1] - t.c[1], b[2] - t.c[2], b[3] - t.c[3]
      if t.kind == "rotate" then
        local m = t.m
        x, y, z = m[1] * x + m[2] * y + m[3] * z, m[4] * x + m[5] * y + m[6] * z, m[7] * x + m[8] * y + m[9] * z
      else
        x, y, z = x * t.s[1], y * t.s[2], z * t.s[3]
      end
      p[1], p[2], p[3] = t.c[1] + x, t.c[2] + y, t.c[3] + z
    end
  end
  it.view = nil
end

local function tool_text()
  local t = ed.tool
  local ax = t.axis == "n" and "along the normal" or (t.axis and ("on " .. AXIS[t.axis]) or "")
  if t.kind == "move" then
    return string.format("MOVE %s  x %s  y %s  z %s  (step %s)", ax, num(t.d[1]), num(t.d[2]), num(t.d[3]),
                         num(MOVE_STEPS[ed.step]))
  elseif t.kind == "rotate" then
    return string.format("ROTATE %s  %s deg  (step %d)", t.axis and ax or "", num(t.deg), ROT_STEPS[ed.step])
  end
  return string.format("SCALE %s  x %s  y %s  z %s  (step x%s)", ax, num(t.s[1]), num(t.s[2]), num(t.s[3]),
                       num(SCALE_STEPS[ed.step]))
end

local function tool_ok()
  local what = tool_text():gsub("%s*%(step.*$", ""):lower() .. ": " .. #ed.tool.vs .. " vertices"
  ed.tool = nil
  finish_edit(what)
end

local function tool_cancel()
  ed.tool = nil
  local s = table.remove(undo)
  if s then restore(s) end
  say("cancelled", C_DIM, 60)
end

local function tool_key(k)
  local t = ed.tool
  if k == "\n" or k == "ok" then tool_ok(); return end
  if k == "esc" or k == "back" then tool_cancel(); return end
  if k == "x" or k == "y" or k == "z" then
    local a = (k == "x" and 1) or (k == "y" and 2) or 3
    t.axis = t.axis ~= a and a or nil
    return
  end
  if k == "n" then t.axis = t.axis ~= "n" and "n" or nil; return end
  if k == "axis" then                       -- the pad: free, x, y, z, normal
    local order = { [false] = 1, [1] = 2, [2] = 3, [3] = "n", n = false }
    t.axis = order[t.axis or false] or nil
    return
  end
  if k == "," then ed.step = max(1, ed.step - 1); return end
  if k == "." then ed.step = min(#MOVE_STEPS, ed.step + 1); return end
  local dirs = { left = -1, right = 1, up = 1, down = -1, pgup = 1, pgdn = -1 }
  local sg = dirs[k]
  if not sg then return end
  local fa, fs, ra, rs = view_axes(ed.cam.yaw)
  if t.kind == "move" then
    local dir
    if t.axis == "n" then dir = t.n
    elseif t.axis then dir = axis_vec(t.axis)
    elseif k == "left" or k == "right" then dir = axis_vec(ra, rs)
    elseif k == "up" or k == "down" then dir = axis_vec(2)
    else dir = axis_vec(fa, fs) end
    local s = MOVE_STEPS[ed.step] * sg
    for i = 1, 3 do t.d[i] = round((t.d[i] + dir[i] * s) * 1e5) / 1e5 end
  elseif t.kind == "rotate" then
    local ax
    if t.axis == "n" then ax = t.n
    elseif t.axis then ax = axis_vec(t.axis)
    elseif k == "left" or k == "right" then ax = axis_vec(2)
    elseif k == "up" or k == "down" then ax = axis_vec(ra, rs)
    else ax = axis_vec(fa, fs) end
    t.m = mat_mul(rot_matrix(ax, math.rad(ROT_STEPS[ed.step]) * sg), t.m)
    t.deg = t.deg + ROT_STEPS[ed.step] * sg
  else
    local f = SCALE_STEPS[ed.step]
    if sg < 0 then f = 1 / f end
    if t.axis and t.axis ~= "n" then t.s[t.axis] = t.s[t.axis] * f
    else for i = 1, 3 do t.s[i] = t.s[i] * f end end
  end
  tool_apply()
end

----------------------------------------------------------------- the commands

local function need_faces(fs)
  if #fs > 0 then return true end
  say(ed.mode == "v" and "no face has all its corners chosen (Tab: faces)" or "choose faces first (space, a)", C_ERR)
  return false
end

-- the chosen faces become a new layer joined to the old one by sides;
-- then they move along their normal
local function op_extrude()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  local edges, copy = {}, {}
  local function ekey(a, b) return a < b and a .. ":" .. b or b .. ":" .. a end
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    for k = 1, 3 do
      local key = ekey(f[k], f[k % 3 + 1])
      edges[key] = (edges[key] or 0) + 1
    end
  end
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    for k = 1, 3 do if not copy[f[k]] then copy[f[k]] = add_vertex(it, it.v[f[k]], f[k]) end end
  end
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    local u = f[5]
    for k = 1, 3 do
      local k2 = k % 3 + 1
      local a, b = f[k], f[k2]
      if edges[ekey(a, b)] == 1 then
        local ua, ub = u and { u[k * 2 - 1], u[k * 2] }, u and { u[k2 * 2 - 1], u[k2 * 2] }
        it.f[#it.f + 1] = { a, b, copy[b], f[4], u and { ua[1], ua[2], ub[1], ub[2], ub[1], ub[2] } or nil }
        it.f[#it.f + 1] = { a, copy[b], copy[a], f[4], u and { ua[1], ua[2], ub[1], ub[2], ua[1], ua[2] } or nil }
      end
    end
  end
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    for k = 1, 3 do f[k] = copy[f[k]] end
  end
  if ed.mode == "v" then
    ed.sv = {}
    for _, nw in pairs(copy) do select_v(nw, true) end
  end
  compact(it)                       -- all faces chosen: the old corners are left alone
  if #it.v > LIMIT_V or #it.f > LIMIT_F then finish_edit(); return end
  tool_start("move", "n", true)
  say("extruded " .. #fs .. " faces: up/down how far, Enter done, Esc cancel", C_ACC, 300)
end

-- a copy of the chosen faces, then it moves
local function op_duplicate()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  local copy, nf = {}, {}
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    local g = { 0, 0, 0, f[4], f[5] and { table.unpack(f[5]) } or nil }
    for k = 1, 3 do
      if not copy[f[k]] then copy[f[k]] = add_vertex(it, it.v[f[k]], f[k]) end
      g[k] = copy[f[k]]
    end
    it.f[#it.f + 1] = g
    nf[#nf + 1] = #it.f
  end
  ed.sv, ed.sf = {}, {}
  if ed.mode == "v" then for _, nw in pairs(copy) do select_v(nw, true) end
  else for _, fi in ipairs(nf) do ed.sf[fi] = true end end
  if #it.v > LIMIT_V or #it.f > LIMIT_F then finish_edit(); return end
  tool_start("move", nil, true)
  say("copied " .. #fs .. " faces: move the copy, Enter done", C_ACC, 300)
end

-- left-right on the screen (x or z), through the middle of the choice
local function op_mirror()
  local vs = sel_verts()
  if #vs == 0 then say("choose vertices or faces first", C_ERR); return end
  local it = begin_edit()
  local _, _, ra = view_axes(ed.cam.yaw)
  local lo, hi = bounds(it, vs)
  local c = (lo[ra] + hi[ra]) / 2
  local moved = {}
  for _, i in ipairs(vs) do
    it.v[i][ra] = 2 * c - it.v[i][ra]
    moved[i] = true
  end
  for _, f in ipairs(it.f) do if moved[f[1]] and moved[f[2]] and moved[f[3]] then flip(f) end end
  finish_edit("mirrored along " .. AXIS[ra])
end

-- the chosen faces copied on the other side of x = 0 (or z = 0): the
-- vertices on the middle are shared
local function op_mirror_copy()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  local _, _, ra = view_axes(ed.cam.yaw)
  local map = {}
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    for k = 1, 3 do
      local i = f[k]
      if not map[i] then
        local p = it.v[i]
        if abs(p[ra]) < 1e-4 then map[i] = i
        else
          local q = { p[1], p[2], p[3] }
          q[ra] = -q[ra]
          map[i] = add_vertex(it, q, i)
        end
      end
    end
  end
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    local g = { map[f[1]], map[f[2]], map[f[3]], f[4], f[5] and { table.unpack(f[5]) } or nil }
    flip(g)
    it.f[#it.f + 1] = g
  end
  finish_edit("copied on the other side of " .. AXIS[ra] .. " = 0: " .. #fs .. " faces")
end

-- a face on 3 chosen vertices (4: two triangles), in the order chosen,
-- facing the camera
local function op_join()
  if ed.mode ~= "v" then say("new faces join vertices: Tab for the vertices", C_ERR); return end
  local list = {}
  for i, o in pairs(ed.sv) do list[#list + 1] = { i, o } end
  table.sort(list, function(a, b) return a[2] < b[2] end)
  if #list ~= 3 and #list ~= 4 then say("choose 3 or 4 vertices (in order) for a new face", C_ERR); return end
  local it = begin_edit()
  local q = {}
  for k, e in ipairs(list) do q[k] = e[1] end
  local tris = #q == 3 and { { q[1], q[2], q[3] } } or { { q[1], q[2], q[3] }, { q[1], q[3], q[4] } }
  local a = area({ q[1], q[2], q[3] })
  for _, t in ipairs(tris) do
    if a and a < 0 then t[2], t[3] = t[3], t[2] end
    it.f[#it.f + 1] = { t[1], t[2], t[3], ed.colour }
  end
  finish_edit("a new face (" .. #tris .. " triangles)")
end

-- the chosen vertices become one, in their middle
local function op_merge()
  if ed.mode ~= "v" then say("merging joins vertices: Tab for the vertices", C_ERR); return end
  local vs = sel_verts()
  if #vs < 2 then say("choose 2 or more vertices to merge", C_ERR); return end
  local it = begin_edit()
  local c = centre_of(it, vs)
  local t = vs[1]
  it.v[t] = c
  local map = {}
  for _, i in ipairs(vs) do map[i] = t end
  for _, f in ipairs(it.f) do for k = 1, 3 do f[k] = map[f[k]] or f[k] end end
  local gone = drop_degenerate(it)
  ed.sv, ed.order = {}, 0
  select_v(t, true)
  compact(it)
  finish_edit(#vs .. " vertices merged" .. (gone > 0 and (", " .. gone .. " faces gone") or ""))
end

-- vertices in the same place become one (with draw3d's flag 4 the faces
-- around them look smooth)
local function op_weld()
  local it = begin_edit()
  local seen, map, n = {}, {}, 0
  for i, p in ipairs(it.v) do
    local key = string.format("%.4f %.4f %.4f %d", p[1] + 0.0, p[2] + 0.0, p[3] + 0.0, it.vb and it.vb[i] or 0)
    if seen[key] then map[i] = seen[key]; n = n + 1 else seen[key] = i end
  end
  for _, f in ipairs(it.f) do for k = 1, 3 do f[k] = map[f[k]] or f[k] end end
  local gone = drop_degenerate(it)
  ed.sv = {}
  compact(it)
  finish_edit(n .. " vertices welded" .. (gone > 0 and (", " .. gone .. " faces gone") or ""))
end

-- each chosen face in 4
local function op_subdivide()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  local mid = {}
  local function midpoint(a, b)
    local key = a < b and a .. ":" .. b or b .. ":" .. a
    if not mid[key] then
      local p, q = it.v[a], it.v[b]
      mid[key] = add_vertex(it, { (p[1] + q[1]) / 2, (p[2] + q[2]) / 2, (p[3] + q[3]) / 2 }, a)
    end
    return mid[key]
  end
  local new = {}
  for _, fi in ipairs(fs) do
    local f = it.f[fi]
    local a, b, c, u = f[1], f[2], f[3], f[5]
    local ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
    local UA, UB, UC, UAB, UBC, UCA
    if u then
      UA, UB, UC = { u[1], u[2] }, { u[3], u[4] }, { u[5], u[6] }
      UAB = { (u[1] + u[3]) / 2, (u[2] + u[4]) / 2 }
      UBC = { (u[3] + u[5]) / 2, (u[4] + u[6]) / 2 }
      UCA = { (u[5] + u[1]) / 2, (u[6] + u[2]) / 2 }
    end
    local function face(i, j, k, ui, uj, uk)
      return { i, j, k, f[4], u and { ui[1], ui[2], uj[1], uj[2], uk[1], uk[2] } or nil }
    end
    it.f[fi] = face(a, ab, ca, UA, UAB, UCA)
    it.f[#it.f + 1] = face(ab, b, bc, UAB, UB, UBC)
    it.f[#it.f + 1] = face(ca, bc, c, UCA, UBC, UC)
    it.f[#it.f + 1] = face(ab, bc, ca, UAB, UBC, UCA)
    new[#new + 1] = fi
    for d = 2, 0, -1 do new[#new + 1] = #it.f - d end
  end
  if ed.mode == "f" then
    ed.sf = {}
    for _, fi in ipairs(new) do ed.sf[fi] = true end
  else
    for _, m in pairs(mid) do select_v(m, true) end
  end
  finish_edit(#fs .. " faces in 4")
end

local function op_flip()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  for _, fi in ipairs(fs) do flip(it.f[fi]) end
  finish_edit(#fs .. " faces turned over")
end

local function op_paint()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  for _, fi in ipairs(fs) do it.f[fi][4], it.f[fi][5] = ed.colour, nil end
  finish_edit(string.format("%d faces painted #%06X", #fs, ed.colour))
end

local function op_pick_colour()
  local it = I()
  local fi = ed.hf or face_at(ed.px, ed.py)
  local f = fi and it.f[fi]
  if not f then say("no face under the pointer", C_ERR); return end
  if f[4] == -1 then say("a face with a texture of the sheet: no colour", C_ERR); return end
  ed.colour = f[4]
  say(string.format("colour #%06X", f[4]), C_ACC, 90)
end

local function op_delete()
  local it = I()
  if ed.mode == "v" then
    if not next(ed.sv) then say("choose vertices first", C_ERR); return end
    it = begin_edit()
    local out = {}
    for _, f in ipairs(it.f) do if not (ed.sv[f[1]] or ed.sv[f[2]] or ed.sv[f[3]]) then out[#out + 1] = f end end
    local n = #it.f - #out
    it.f, ed.sv, ed.sf = out, {}, {}
    compact(it)
    finish_edit("deleted: the vertices and " .. n .. " faces")
  else
    local fs = sel_faces()
    if not need_faces(fs) then return end
    it = begin_edit()
    local del, out = {}, {}
    for _, fi in ipairs(fs) do del[fi] = true end
    for fi, f in ipairs(it.f) do if not del[fi] then out[#out + 1] = f end end
    it.f, ed.sf = out, {}
    compact(it)
    finish_edit("deleted " .. #fs .. " faces")
  end
end

-- everything joined to the choice by faces
local function op_linked()
  local it = I()
  local start = sel_verts()
  if #start == 0 then
    if ed.hv then start = { ed.hv }
    elseif ed.hf then start = { it.f[ed.hf][1] } end
  end
  if #start == 0 then say("choose a vertex or a face first", C_ERR); return end
  local parent = {}
  local function root(i)
    while parent[i] and parent[i] ~= i do
      parent[i] = parent[parent[i]] or parent[i]
      i = parent[i]
    end
    return i
  end
  local function join(a, b)
    a, b = root(a), root(b)
    if a ~= b then parent[a] = b end
  end
  for i in ipairs(it.v) do parent[i] = i end
  for _, f in ipairs(it.f) do join(f[1], f[2]); join(f[2], f[3]) end
  local want = {}
  for _, i in ipairs(start) do want[root(i)] = true end
  local n = 0
  if ed.mode == "v" then
    for i in ipairs(it.v) do if want[root(i)] and not ed.sv[i] then select_v(i, true) end end
    n = count(ed.sv)
  else
    for fi, f in ipairs(it.f) do if want[root(f[1])] then ed.sf[fi] = true end end
    n = count(ed.sf)
  end
  say(n .. " chosen", C_ACC, 60)
end

local function op_all()
  local it = I()
  if next(ed.sv) or next(ed.sf) then
    ed.sv, ed.sf = {}, {}
    say("none chosen", C_DIM, 60)
  elseif ed.mode == "v" then
    for i in ipairs(it.v) do select_v(i, true) end
    say("all " .. #it.v .. " vertices", C_DIM, 60)
  else
    for fi in ipairs(it.f) do ed.sf[fi] = true end
    say("all " .. #it.f .. " faces", C_DIM, 60)
  end
end

local function op_mode()
  if ed.mode == "v" then
    local fs = sel_faces()
    ed.mode, ed.sf = "f", {}
    for _, fi in ipairs(fs) do ed.sf[fi] = true end
  else
    local vs = sel_verts()
    ed.mode, ed.sv, ed.order = "v", {}, 0
    for _, i in ipairs(vs) do select_v(i, true) end
  end
  ed.force = nil
  say(ed.mode == "v" and "vertices" or "faces", C_DIM, 60)
end

-- space: the vertex (face) under the pointer in or out; Enter: only it
local function op_toggle(only)
  if ed.mode == "v" then
    local i = ed.hv
    if only then ed.sv, ed.order = {}, 0 end
    if not i then if not only then say("no vertex under the pointer", C_DIM, 60) end; return end
    select_v(i, only or not ed.sv[i])
  else
    local fi = ed.hf
    if only then ed.sf = {} end
    if not fi then if not only then say("no face under the pointer", C_DIM, 60) end; return end
    ed.sf[fi] = (only or not ed.sf[fi]) or nil
  end
end

-- n / b: the pointer on the next (previous) vertex or face, even behind
local function op_next(d)
  local it = I()
  local n = ed.mode == "v" and #it.v or #it.f
  if n == 0 then return end
  local i = ((ed.force or (d > 0 and 0 or 1)) - 1 + d) % n + 1
  ed.force = i
  local x, y
  if ed.mode == "v" then x, y = ed.sx[i], ed.sy[i]
  else
    local f = it.f[i]
    if ed.sx[f[1]] and ed.sx[f[2]] and ed.sx[f[3]] then
      x, y = (ed.sx[f[1]] + ed.sx[f[2]] + ed.sx[f[3]]) / 3, (ed.sy[f[1]] + ed.sy[f[2]] + ed.sy[f[3]]) / 3
    end
  end
  if x then ed.px, ed.py = clamp(round(x), 0, W - 1), clamp(round(y), 16, HINT_Y - 1) end
  say((ed.mode == "v" and "vertex " or "face ") .. i .. "/" .. n, C_DIM, 60)
end

local function view(yaw, pitch)
  ed.cam.yaw, ed.cam.pitch = yaw, pitch
end

-- the palette: arrows and Enter (A), Esc (B) closes
local function palette_key(k)
  local i = ed.pal
  if k == "left" then i = (i - 2) % #PALETTE + 1
  elseif k == "right" then i = i % #PALETTE + 1
  elseif k == "up" then i = (i - 11) % #PALETTE + 1
  elseif k == "down" then i = (i + 9) % #PALETTE + 1
  elseif k == "\n" or k == "ok" then ed.colour, ed.pal = PALETTE[i], nil; say(string.format("colour #%06X", PALETTE[i]), C_ACC, 90); return
  elseif k == "esc" or k == "back" then ed.pal = nil; return end
  ed.pal = i
end

local function open_palette()
  ed.pal = 1
  for i, c in ipairs(PALETTE) do if c == ed.colour then ed.pal = i end end
end

local COMMANDS = {
  { "Move (g)", "g" }, { "Rotate (r)", "r" }, { "Scale (t)", "t" }, { "Extrude faces (x)", "x" },
  { "Duplicate (d)", "d" }, { "Mirror left-right (m)", "m" }, { "Mirror copy across 0 (M)", "M" },
  { "New face on 3-4 vertices (j)", "j" }, { "Merge vertices (k)", "k" }, { "Weld equal vertices (K)", "K" },
  { "Subdivide faces (u)", "u" }, { "Turn faces over (i)", "i" }, { "Paint faces (p)", "p" },
  { "Colour from the face (o)", "o" }, { "Palette (c)", "c" }, { "Delete (Del)", "del" },
  { "All / none (a)", "a" }, { "Linked (l)", "l" }, { "Vertices / faces (Tab)", "\t" },
  { "Next element (n)", "n" }, { "Frame the choice (f)", "f" }, { "Finer step (,)", "," }, { "Coarser step (.)", "." },
}

function edit_actions()
  local rows = {}
  for _, c in ipairs(COMMANDS) do rows[#rows + 1] = { c[1], function() edit_key(c[2]) end } end
  choose("commands" .. (I() and (": " .. I().name) or ""), rows)
end

function edit_key(k)
  local it = I()
  if not it then return end
  if ed.pal then palette_key(k); return end
  if ed.tool then tool_key(k); return end
  local cam = ed.cam
  if k == "left" or k == "right" or k == "up" or k == "down" then
    if k == ed.last_k and frame - ed.last_f <= 10 then ed.rep = min(ed.rep + 1, 8) else ed.rep = 0 end
    ed.last_k, ed.last_f = k, frame
    local d = 4 + ed.rep * 4
    if k == "left" then ed.px = ed.px - d elseif k == "right" then ed.px = ed.px + d
    elseif k == "up" then ed.py = ed.py - d else ed.py = ed.py + d end
    ed.px, ed.py, ed.force = clamp(ed.px, 0, W - 1), clamp(ed.py, 16, HINT_Y - 1), nil
  elseif k == "home" then ed.px, ed.py, ed.force = W // 2, 184, nil
  elseif k == " " then op_toggle(false)
  elseif k == "\n" then op_toggle(true)
  elseif k == "a" then op_all()
  elseif k == "l" then op_linked()
  elseif k == "\t" then op_mode()
  elseif k == "n" then op_next(1)
  elseif k == "b" then op_next(-1)
  elseif k == "q" then cam.yaw = cam.yaw - pi / 12
  elseif k == "e" then cam.yaw = cam.yaw + pi / 12
  elseif k == "w" then cam.pitch = clamp(cam.pitch - 0.1, -1.5, 1.5)
  elseif k == "s" then cam.pitch = clamp(cam.pitch + 0.1, -1.5, 1.5)
  elseif k == "+" or k == "=" then cam.dist = max(0.05, cam.dist * 0.85)
  elseif k == "-" then cam.dist = min(500, cam.dist / 0.85)
  elseif k == "f" then
    local vs = sel_verts()
    aim(cam, it, #vs > 0 and vs or nil, #vs > 0 and 3 or 2.2)
  elseif k == "1" then view(0, 0)
  elseif k == "3" then view(-pi / 2, 0)
  elseif k == "7" then view(0, -1.5)
  elseif k == "0" then view(0.6, -0.45)
  elseif k == "g" then tool_start("move")
  elseif k == "r" then tool_start("rotate")
  elseif k == "t" then tool_start("scale")
  elseif k == "x" then op_extrude()
  elseif k == "d" then op_duplicate()
  elseif k == "m" then op_mirror()
  elseif k == "M" then op_mirror_copy()
  elseif k == "j" then op_join()
  elseif k == "k" then op_merge()
  elseif k == "K" then op_weld()
  elseif k == "u" then op_subdivide()
  elseif k == "i" then op_flip()
  elseif k == "p" then op_paint()
  elseif k == "o" then op_pick_colour()
  elseif k == "c" then open_palette()
  elseif k == "del" or k == "\b" then op_delete()
  elseif k == "," then ed.step = max(1, ed.step - 1); say("step " .. num(MOVE_STEPS[ed.step]), C_DIM, 60)
  elseif k == "." then ed.step = min(#MOVE_STEPS, ed.step + 1); say("step " .. num(MOVE_STEPS[ed.step]), C_DIM, 60)
  end
end

function edit_pad()
  local cam = ed.cam
  if ed.pal or ed.tool then
    local k = (rp[0] and "left") or (rp[1] and "right") or (rp[2] and "up") or (rp[3] and "down") or
              (btnp(4) and "ok") or (btnp(5) and "back") or (tap[6] and ed.tool and "axis")
    if k then edit_key(k) end
    return
  end
  if btn(6) then                     -- X + pad: turn and tilt, X + A / B: zoom
    if btn(0) then cam.yaw = cam.yaw - 0.04 end
    if btn(1) then cam.yaw = cam.yaw + 0.04 end
    if btn(2) then cam.pitch = clamp(cam.pitch - 0.03, -1.5, 1.5) end
    if btn(3) then cam.pitch = clamp(cam.pitch + 0.03, -1.5, 1.5) end
    if btn(4) then cam.dist = max(0.05, cam.dist * 0.98) end
    if btn(5) then cam.dist = min(500, cam.dist / 0.98) end
    return
  end
  -- the pointer: faster while held
  local moved = false
  for b, d in pairs({ [0] = { -1, 0 }, [1] = { 1, 0 }, [2] = { 0, -1 }, [3] = { 0, 1 } }) do
    if btn(b) then
      local sp = 1 + min(held[b], 40) // 5
      ed.px, ed.py = ed.px + d[1] * sp, ed.py + d[2] * sp
      moved = true
    end
  end
  if moved then ed.px, ed.py, ed.force = clamp(ed.px, 0, W - 1), clamp(ed.py, 16, HINT_Y - 1), nil end
  if btnp(4) then op_toggle(false) end
  if btnp(5) then ed.sv, ed.sf = {}, {} end
  if tap[6] then edit_actions() end
end

local function draw_palette()
  local x0, y0 = 120, 112
  rectfill(x0 - 16, y0 - 32, 432, 160, C_PANEL)
  rect(x0 - 16, y0 - 32, 432, 160, C_ACC)
  print("colour: arrows, Enter (A)", x0, y0 - 16, C_ACC)
  for i, c in ipairs(PALETTE) do
    local x, y = x0 + ((i - 1) % 10) * 40, y0 + ((i - 1) // 10) * 32
    rectfill(x, y, 32, 24, c)
    if i == ed.pal then rect(x - 3, y - 3, 38, 30, 0xFFFFFF) end
  end
end

function draw_edit()
  local it = I()
  cls(C_SKY)
  zclear()
  if not it then
    print("nothing to edit: F1 the list, Esc > Open", 32, 160, C_TEXT)
    return
  end
  local cam = ed.cam
  look(cam, 0, 24)
  floor_grid(cam)
  local v = view_of(it)
  if v then
    light3d(-0.4, 0.8, -0.5, 0.45)
    draw3d(v, 0, 0, 0, 0, 0, 0, 1, 0)
  end
  project_all(it)
  hover()
  local sx, sy = ed.sx, ed.sy
  -- the edges of the faces that look at the camera
  if #it.f <= 3000 then
    for _, f in ipairs(it.f) do
      local ar = area(f)
      if ar and ar > 0 then
        local a, b, c = f[1], f[2], f[3]
        line(sx[a], sy[a], sx[b], sy[b], C_WIRE)
        line(sx[b], sy[b], sx[c], sy[c], C_WIRE)
        line(sx[c], sy[c], sx[a], sy[a], C_WIRE)
      end
    end
  end
  local function outline(f, c)
    local a, b, d = f[1], f[2], f[3]
    if sx[a] and sx[b] and sx[d] then
      line(sx[a], sy[a], sx[b], sy[b], c)
      line(sx[b], sy[b], sx[d], sy[d], c)
      line(sx[d], sy[d], sx[a], sy[a], c)
      local mx, my = (sx[a] + sx[b] + sx[d]) / 3, (sy[a] + sy[b] + sy[d]) / 3
      rectfill(mx - 1, my - 1, 3, 3, c)
    end
  end
  if ed.mode == "v" then
    if #it.v <= 4096 then
      for i in ipairs(it.v) do if sx[i] and not ed.sv[i] then rectfill(sx[i] - 1, sy[i] - 1, 2, 2, C_WIRE) end end
    end
    for i in pairs(ed.sv) do if sx[i] then rectfill(sx[i] - 2, sy[i] - 2, 5, 5, C_PT) end end
    if ed.hv and sx[ed.hv] then circ(sx[ed.hv], sy[ed.hv], 5, C_HOT) end
  else
    for fi in pairs(ed.sf) do if it.f[fi] then outline(it.f[fi], C_PT) end end
    if ed.hf then outline(it.f[ed.hf], C_HOT) end
  end
  if ed.tool then
    local x, y = scr(ed.tool.c)
    if x then circ(x, y, 4, 0xFFFFFF) end
  end
  -- the pointer
  line(ed.px - 7, ed.py, ed.px - 3, ed.py, 0xFFFFFF)
  line(ed.px + 3, ed.py, ed.px + 7, ed.py, 0xFFFFFF)
  line(ed.px, ed.py - 7, ed.px, ed.py - 3, 0xFFFFFF)
  line(ed.px, ed.py + 3, ed.px, ed.py + 7, 0xFFFFFF)
  draw_gizmo(cam, 40, HINT_Y - 40)
  -- what it is
  rectfill(0, 16, W, 32, C_BG)
  local nsel = ed.mode == "v" and count(ed.sv) or count(ed.sf)
  print(it.name, 8, 16, KIND_C[it.kind])
  print(string.format("%d vertices  %d triangles   %s: %d chosen", #it.v, #it.f,
                      ed.mode == "v" and "VERTICES" or "FACES", nsel), 8 + (#it.name + 2) * 8, 16, C_TEXT)
  if ed.tool then
    print(tool_text(), 8, 32, C_ACC)
  else
    rectfill(8, 36, 12, 8, ed.colour)
    print(string.format("#%06X  step %s  %s", ed.colour, num(MOVE_STEPS[ed.step]), kind_text(it)), 24, 32, C_DIM)
  end
  if ed.pal then draw_palette() end
  if ed.tool then
    hint({ { { "up", "down", "left", "right" }, "change" }, { { "x", "y", "z", "n" }, "axis" }, { { ",", "." }, "step" }, { { "enter" }, "done" },
           { { "esc" }, "cancel" } })
  elseif ed.pal then hint({ { { "up", "down", "left", "right" }, "colour" }, { { "enter" }, "take it" }, { { "esc" }, "close" } })
  else
    hint({ { { "up", "down", "left", "right" }, "point" }, { { "space" }, "choose" }, { { "a" }, "all" }, { { "tab" }, "vert/face" },
           { { "g" }, "move" }, { { "r" }, "rotate" }, { { "t" }, "scale" }, { { "x" }, "extrude" } })
  end
end
end

----------------------------------------------------------------- menu

local msel = 1
local last_page = "list"

go = function(p)
  if p == "menu" then if page ~= "menu" then msel = 1 end
  else last_page = p end
  page = p
end

reset_pages = function()
  list_reset()
  edit_reset()
end

local function open_chooser()
  local files = list_files()
  if #files == 0 then say("no .bm files on the SD card", C_ERR); return end
  local rows, sel = {}, 1
  for i, f in ipairs(files) do
    rows[i] = { f, function() if open_file(f) then go("list") end end }
    if f == proj.path then sel = i end
  end
  choose("open a cartridge", rows, sel)
end

local function try_game()
  if not proj.path then say("open a .bm first", C_ERR); return end
  if dirty and not save_to(proj.path) then return end
  save({ page = last_page, cur = cur })        -- the page to come back to
  cart_run(proj.path)
end

local MENU = {
  { "Continue", function() go(last_page) end },
  { "Open...", function() if not dirty or confirmed("open", "unsaved changes: choose again to open another file") then open_chooser() end end },
  { "Save   (Ctrl+S)", function() save_to(proj.path) end },
  { "Save as...", function()
      if not proj.path then say("open a .bm first", C_ERR); return end
      ask("file name (8.3, in /carts)", proj.path:match("([^/]+)$") or "MESHES.BM", function(t)
        if not t:upper():match("%.BM$") then t = t .. ".BM" end
        local to = short_path("/carts/" .. t)
        local from = proj.path
        code_dirty = true                    -- the new file gets all the code
        if save_to(to, from) then go("list") end
      end)
    end },
  { "Try the game (F5)", try_game },
  { "Exit bm Mesh", function() if not dirty or confirmed("exit", "unsaved changes: choose again to exit") then quit() end end },
}

local function menu_key(k)
  if k == "up" then msel = (msel - 2) % #MENU + 1
  elseif k == "down" then msel = msel % #MENU + 1
  elseif k == "\n" or k == "ok" then MENU[msel][2]()
  elseif k == "esc" or k == "back" then go(last_page) end
end

local function draw_menu()
  cls(C_BG)
  print("bm Mesh", 32, 32, C_ACC)
  print((proj.path or "(no file open)") .. (dirty and "  *modified*" or ""), 128, 32, C_DIM)
  for i, it in ipairs(MENU) do
    local y = 64 + (i - 1) * 16
    if i == msel and not pick and not input then rectfill(24, y, 272, 16, C_SEL) end
    print(it[1], 32, y, C_TEXT)
  end
  local n = { model = 0, code = 0, game = 0 }
  for _, it in ipairs(items) do n[it.kind] = n[it.kind] + 1 end
  local x = 320
  print(proj.title ~= "" and proj.title:sub(1, 38) or "", x, 64, C_TEXT)
  print("M " .. n.model .. " models (MESH section)", x, 96, KIND_C.model)
  print("C " .. n.code .. " code meshes (mesh_ functions)", x, 112, KIND_C.code)
  print("G " .. n.game .. " built by the game's code", x, 128, KIND_C.game)
  chip_hint("f5", nil, "try the game", chip_hint("f2", nil, "edit", chip_hint("f1", nil, "list", x, 160), 160), 160)
  local kx = snap(prompt("f12", x, 176) + 3)
  kx = print("(held) or", kx, 176, C_DIM) + 4
  chip_hint("?", nil, "keys", kx, 176)
  print("the models: as bm Studio and bm", x, 208, C_DIM)
  print("Animator (skeletons kept); the", x, 224, C_DIM)
  print("code: as bm Studio's Lua, for bm Code", x, 240, C_DIM)
  hint({ { { "up", "down" }, "choose" }, { { "enter" }, "select" }, { { "esc" }, "back" } })
end

----------------------------------------------------------------- keys help

local KEYS = {
  all = { "F1 list  F2 edit  Esc menu  Ctrl+S save  F5 try the game  F3 commands",
          "Ctrl+Z / Ctrl+Y undo / redo  [ ] the previous / next mesh" },
  list = { "up/down        the mesh", "Enter          edit it (F2)", "m              copy as a model: mesh -> model",
           "c              copy as code: model -> mesh_name()", "r  d  Del      rename, duplicate, delete",
           "n              a new model (a cube; F3: plane, sphere)", "q e / w s      turn / tilt the view  + - zoom",
           "space          spin", "G: the game's code builds it; editing it makes a model" },
  edit = { "arrows / n b   the pointer / on the next, previous one", "space  Enter   choose (add) / only this    a all, none",
           "Tab  l         vertices or faces / all linked", "g r t          move, rotate, scale: arrows PgUp PgDn",
           "  x y z n      ...only on that axis, the normal; Enter Esc", "x  d           extrude faces, duplicate (then move)",
           "m  M           mirror left-right / copy across 0", "j  k  K        new face, merge, weld equal vertices",
           "u  i  Del      subdivide, turn over, delete", "p  o  c        paint, take the colour, palette",
           "q e w s + -    view; f frame; 1 3 7 0 views; , . step" },
  menu = { "up/down        choose", "Enter          select", "Esc            back" },
  pad = { "pad: Y + left/right page  Y + B menu  Y + A undo  Y + up/down mesh",
          "list: A edit  X tap commands  X + pad view",
          "edit: pad pointer  A choose  B none  X tap commands  X + pad view",
          "tools: pad change  A done  B cancel  X tap axis" },
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
  local a = cart_arg()
  if a and a.path and open_file(a.path) then
    local back = a.back and saved()
    if a.error then
      go("menu")
      say("the game stopped: " .. a.error:sub(1, 60), C_ERR, 400)
    elseif back and back.page then
      cur = clamp(back.cur or 1, 1, max(1, #items))
      reset_pages()
      go(back.page)
      say("back from the game", C_ACC)
    else
      go("list")
    end
  else
    go("menu")
    open_chooser()
    if pick then say("open a .bm: its models and the meshes of its code", C_ACC, 400) end
  end
end

local function global_key(k)
  if k == "f1" then go("list"); return true
  elseif k == "f2" then if I() then go("edit") end; return true
  elseif k == "f3" then if page == "edit" then edit_actions() elseif page == "list" then list_actions() end; return true
  elseif k == "esc" and page ~= "menu" and not (page == "edit" and (ed.tool or ed.pal)) then go("menu"); return true
  elseif k == "^s" then save_to(proj.path); return true
  elseif k == "f5" or k == "^r" then try_game(); return true
  elseif k == "^z" or k == "^y" then
    if ed.tool then edit_key("esc")            -- undo in a tool: the tool goes
    elseif k == "^z" then do_undo(undo, redo, "undo") else do_undo(redo, undo, "redo") end
    return true
  elseif (k == "[" or k == "]") and page ~= "menu" and #items > 0 and not ed.tool then
    cur = (cur - 1 + (k == "]" and 1 or -1)) % #items + 1
    reset_pages()
    say(I().name, C_ACC, 60)
    return true
  end
  return false
end

local PAGES = { "list", "edit", "menu" }

function _update()
  frame = frame + 1
  if msg_t > 0 then msg_t = msg_t - 1 end
  if confirm_t > 0 then confirm_t = confirm_t - 1 end
  read_pad()

  while true do
    local k = keyp()
    if not k then break end
    if help then help = false
    elseif input then input_key(k)
    elseif pick then pick_key(k)
    elseif k == "?" then help = true
    elseif not global_key(k) then
      if page == "list" then list_key(k)
      elseif page == "edit" then edit_key(k)
      else menu_key(k) end
    end
  end

  -- gamepad
  if input then
    if btnp(5) then input = nil end
  elseif pick then
    if rp[2] then pick_key("up") elseif rp[3] then pick_key("down")
    elseif btnp(4) then pick_key("ok") elseif btnp(5) then pick_key("back") end
  elseif btn(7) and not ed.tool and not ed.pal then
    if btnp(0) or btnp(1) then
      local i = 1
      for n, p in ipairs(PAGES) do if p == page then i = n end end
      local p = PAGES[(i - 1 + (btnp(1) and 1 or -1)) % #PAGES + 1]
      if p ~= "edit" or I() then go(p) end
    elseif btnp(5) then go("menu")
    elseif btnp(4) then do_undo(undo, redo, "undo")
    elseif (btnp(2) or btnp(3)) and #items > 0 then
      cur = (cur - 1 + (btnp(3) and 1 or -1)) % #items + 1
      reset_pages()
      say(I().name, C_ACC, 60)
    end
  elseif page == "menu" then
    if rp[2] then menu_key("up") elseif rp[3] then menu_key("down")
    elseif btnp(4) then menu_key("ok") elseif btnp(5) then menu_key("back") end
  elseif page == "list" then list_pad()
  elseif page == "edit" then edit_pad() end
end

function _draw()
  if page == "list" then draw_list_page()
  elseif page == "edit" then draw_edit()
  else draw_menu() end

  -- tab bar: each page with its key
  rectfill(0, 0, W, 16, C_BAR)
  local tabs = { { "list", "f1", "list" }, { "edit", "f2", "edit" }, { "menu", "esc", "menu" } }
  local x = 4
  for _, t in ipairs(tabs) do
    local lx = snap(x + prompt(t[2]) + 3)
    local w = lx + #t[3] * 8 - x
    if t[1] == page then rectfill(x - 4, 0, w + 8, 16, C_SEL) end
    prompt(t[2], x, 0)
    print(t[3], lx, 0, t[1] == page and 0xFFFFFF or C_DIM)
    x = x + w + 20
  end
  print("bm Mesh", snap(x + 4), 0, C_ACC)
  local name = (proj.path or "no file") .. (dirty and "*" or "")
  print(name, W - #name * 8 - 8, 0, dirty and C_ACC or C_DIM)

  -- status bar
  rectfill(0, STATUS_Y, W, H - STATUS_Y, C_BAR)
  local status
  if msg_t > 0 and msg then status = msg
  elseif page == "menu" then status = "up/down choose, Enter select"
  else status = I() and ("mesh " .. cur .. "/" .. #items .. ": " .. I().name .. " (" .. KIND_L[I().kind] .. ")") or "" end
  print(status:sub(1, 79), 0, STATUS_Y, (msg_t > 0 and msg_c) or C_TEXT)
  if #status <= 62 then                  -- room on the right: F12 or ? for the keys
    local kx = print("or", snap(prompt("f12", 524, STATUS_Y) + 3), STATUS_Y, C_DIM) + 4
    chip_hint("?", nil, "keys", kx, STATUS_Y)
  end
  if pick then draw_pick() end
  if input then draw_input() end
  if keyheld("f12") or help then draw_keys() end
end
