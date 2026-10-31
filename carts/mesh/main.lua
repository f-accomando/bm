-- bm Mesh: the 3D meshes of a .bm, on the console.
-- Reads the models of the MESH section (bm Studio, bm Animator) and the
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
local U = require "bmui"                 -- the mouse: what the keys do, by clicking
local B = require "bm3d"                 -- the shared code; B.G is the pure geometry of a model
B.LY.auto()                              -- the elastic layout (B.LY = lib.layout): 1280x720 where the GPU draws
local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x707890, 0xFFC050, 0xFF6060, 0x3050A0
local C_SKY, C_GRID, C_WIRE, C_PT, C_HOT = 0x262C3E, 0x3A4258, 0x7A88B0, 0xFFE070, 0x60E0FF
local KIND_C = { model = 0x80C8FF, code = 0x90E090, game = 0xE0A060 }
local KIND_L = { model = "M", code = "C", game = "G" }
local KIND_ORDER = { model = 1, code = 2, game = 3 }
local AXIS = { "x", "y", "z" }
local SEC_MESH, SEC_ANIM = 8, 9                  -- section types (src/bm/bm.h)
local TEXTURED = 0x80000000
local LIMIT_V, LIMIT_F, LIMIT_MODELS = 65535, 65535, 256
local PANEL_W = 208                              -- the list on the left
local HINT_Y, STATUS_Y = H - 40, H - 24          -- text on rows of 16 pixels
local INFO_X = PANEL_W + 16
local FOCAL = (W / 2) / math.tan(math.rad(30))   -- camera3d with fov 60

-- the screen changed size (screen(): 1280x720 where the GPU draws): what was cut from W and H again
B.LY.on_resize(function(w, h)
  W, H = w, h
  HINT_Y, STATUS_Y = H - 40, H - 24
  FOCAL = (W / 2) / math.tan(math.rad(30))
  B.resize(w, h)
  if B.screen_row then B.screen_row[1] = B.LY.label() end   -- the menu's Screen row follows the size
end)
local BEGIN, END = "-- [bm Mesh begin]", "-- [bm Mesh end]"

local floor, abs, sqrt, sin, cos = math.floor, math.abs, math.sqrt, math.sin, math.cos
local max, min, pi = math.max, math.min, math.pi
local spack, sunpack = string.pack, string.unpack
local ok_assist, assist = pcall(require, "assist")   -- F6: the assistant's 3D recipes

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end
local function round(v) return floor(v + 0.5) end

-- the pure geometry (bm3d.lua, B.G)
local model_item, encode_model = B.G.model_item, B.G.encode_model
local normal_of, unit, flip, add_vertex = B.G.normal_of, B.G.unit, B.G.flip, B.G.add_vertex

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
  local hid, n = it.hid, 0                  -- hidden vertices (APP-11): their faces are not drawn
  for i, p in ipairs(it.v) do v[i * 3 - 2], v[i * 3 - 1], v[i * 3] = p[1], p[2], p[3] end
  for _, q in ipairs(it.f) do
    if not (hid and (hid[q[1]] or hid[q[2]] or hid[q[3]])) then
      n = n + 1
      f[n * 4 - 3], f[n * 4 - 2], f[n * 4 - 1], f[n * 4] = q[1], q[2], q[3], q[4]
      local u = q[5]
      if q[4] == -1 and u then
        tex = true
        for k = 1, 6 do uv[n * 6 - 6 + k] = u[k] end
      else
        for k = 1, 6 do uv[n * 6 - 6 + k] = 0 end
        if q[4] == -1 then f[n * 4] = 0xFFFFFF end
      end
    end
  end
  if n == 0 then it.view = false; return false end
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
  local out = U.project_files({ bm = true, bme = true, bmm = true }, { B.BMM_DIR })   -- and the model files of /bmm
  table.sort(out)
  return out
end

local function short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or U.PROJECTS
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):lower():gsub("[^%w_]", "")
  if stem == "" then stem = "game" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".bme"
end

-- a game (.bm, .b16) is read only: saving it makes its editable copy
local function is_game(path)
  local p = path and path:lower() or ""
  return p:match("%.bm$") ~= nil or p:match("%.b16$") ~= nil
end

local reset_pages, go       -- defined with the pages

local function open_file(path)
  -- a model file (.bmm): its models and skeletons, no code and no game; Save writes the .bmm again (res_write)
  if B.is_bmm(path) then
    local r, e = res_read(path)
    if not r then say("cannot open " .. path .. ": " .. tostring(e), C_ERR); return false end
    if r.kind ~= 1 or not r.sections[SEC_MESH] then say(path .. " is not a model file", C_ERR); return false end
    proj = { path = path, bmm = true, title = r.name or "", author = r.author or "", lua = "", warn = nil }
    items, undo, redo = {}, {}, {}
    local parts, ins = split_mesh(r.sections[SEC_MESH])
    inset = ins
    local rigs = r.sections[SEC_ANIM] and split_anim(r.sections[SEC_ANIM]) or {}
    for _, part in ipairs(parts) do items[#items + 1] = model_item(part[1], part[2], rigs[part[1]]) end
    dirty, code_dirty, cur = false, false, 1
    reset_pages()
    say(string.format("%s: %d models; a model file: Save writes it, Save as makes another in /bmm", path, #parts), C_ACC)
    return true
  end
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
  say(string.format("%s: %d models, %d code meshes, %d from the game's code%s", path, #parts, #codes, ngame,
                    is_game(path) and "; a game: saving makes its copy" or ""), C_ACC)
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
  if not proj.path then say("open a .bm or .bme first: Esc > Open", C_ERR); return false end
  local mesh, anim, skipped = build_sections()
  if mesh == nil then say("cannot save: " .. anim, C_ERR); return false end
  if proj.bmm then                      -- a model file: res_write, the models and their skeletons only
    if not mesh then say("cannot save: no model with faces", C_ERR); return false end
    local ok, err = res_write(path, 1, { sections = { [SEC_MESH] = mesh, [SEC_ANIM] = anim or nil },
                                         name = proj.title ~= "" and proj.title or nil, author = proj.author,
                                         replace = true })
    if not ok then say("save failed: " .. tostring(err), C_ERR); return false end
    for _, it in ipairs(items) do
      if it.kind == "model" and it.new_mc then
        it.mc, it.mc_name, it.ac = it.new_mc, it.name, it.new_ac
        if not it.ac then it.vb = nil end
      end
      it.dirty, it.new_mc, it.new_ac = false, nil, nil
    end
    proj.path = path
    dirty = false
    say("saved " .. path .. (skipped and ("  (" .. skipped .. " has no faces: left out)") or ""), C_ACC)
    return true
  end
  local t = { sections = { [SEC_MESH] = mesh, [SEC_ANIM] = anim }, from = from }
  local lua = code_dirty and full_lua() or nil
  t.lua = lua
  local ok, err = cart_write(path, t)
  if not ok then say("save failed: " .. tostring(err), C_ERR); return false end
  if type(err) == "string" then path = err end  -- the editable copy of a game
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
  say(U.saved_text(path) .. (skipped and ("  (" .. skipped .. " has no faces: left out)") or ""), C_ACC)
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

-- the hidden vertices (APP-11) as a bit string: 1 bit a vertex, "" when none
local UA = {}                   -- the undo helpers (one local: the limit of 200 in this chunk)
function UA.pack_hid(it)
  local hid = it.hid
  if not hid then return nil end
  local nb, bytes = (#it.v + 7) // 8, {}
  for i = 1, nb do bytes[i] = 0 end
  for i in pairs(hid) do
    local b = (i - 1) // 8 + 1
    if bytes[b] then bytes[b] = bytes[b] | (1 << ((i - 1) % 8)) end
  end
  local out = {}
  for i = 1, nb do out[i] = string.char(bytes[i]) end
  return table.concat(out)
end

function UA.unpack_hid(it, s)
  if not s then it.hid = nil; return end
  local hid = {}
  for i = 1, #it.v do
    if (s:byte((i - 1) // 8 + 1) or 0) >> ((i - 1) % 8) & 1 == 1 then hid[i] = true end
  end
  it.hid = next(hid) and hid or nil
end

local function snapshot()
  local it = I()
  local geo, hid = pack_geo(it), UA.pack_hid(it)
  return { idx = cur, name = it.name, kind = it.kind, geo = geo, hid = hid, size = #geo + (hid and #hid or 0) }
end

-- the undo steps (APP-11): up to UNDO_MAX, and no more than UNDO_BYTES of snapshots (a big mesh keeps
-- fewer steps, never fewer than 2): the Pi Zero W has little RAM
UA.MAX, UA.BYTES = 100, 4 * 1024 * 1024
function UA.trim()
  local total = 0
  for _, s in ipairs(undo) do total = total + s.size end
  while #undo > UA.MAX or (#undo > 2 and total > UA.BYTES) do
    total = total - table.remove(undo, 1).size
  end
end

local on_restore             -- the edit page: the selection after an undo

local function restore(s)
  local it = items[s.idx]
  if not it or it.name ~= s.name or it.kind ~= s.kind then return false end
  cur = s.idx
  unpack_geo(it, s.geo)
  UA.unpack_hid(it, s.hid)
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
  UA.trim()
  redo = {}
  return it
end

-- a step with no change of the geometry (hide, isolate, show all): the snapshot holds the hidden set
function UA.push()
  undo[#undo + 1] = snapshot()
  UA.trim()
  redo = {}
end

-- after it: too big, undone
local function finish_edit(what)
  local it = I()
  if #it.v > LIMIT_V or #it.f > LIMIT_F then
    local s = table.remove(undo)
    if s then restore(s) end
    say("cannot: format limit " .. LIMIT_V .. " vertices and " .. LIMIT_F .. " triangles", C_ERR)
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
local confirm_t, confirm_what = 0, nil

-- the keys as chips (prompt(), the look of all the Dev apps): the labels
-- stay on the font's 8 px columns
local function snap(x) return (x + 7) // 8 * 8 end

-- the keys of the page on the hint row: { { {keys...}, label }, ... }, as many as fit
local function hint(list)
  local items = {}
  for i, h in ipairs(list) do items[i] = { keys = h[1], label = h[2] } end
  U.hint_row(items, { y = HINT_Y, w = W, snap = snap, dim = C_DIM })
end

-- a key as a chip, or the pad's button when a pad was used last and the
-- action has one; then its label. Returns the x after it.
local function chip_hint(key, pad, label, x, y, c)
  local li = lastinput()
  local x0 = x
  x = prompt(pad and (li == "ds4" or li == "pad") and pad or key, x, y)
  x = print(label, snap(x + 3), y, c or C_DIM)
  U.key_zone(x0, y, x - x0, 16, key)
  return x + 12
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
  else U.line_key(input, k, 40) end
end

local function draw_pick()
  local p = pick
  local rows = min(#p.rows, 14)
  local h = (rows + 2) * 16
  local y0 = max(32, (H - h) // 32 * 16)
  U.zone(0, 0, W, H, "shade")                    -- a click outside: Esc
  U.zone(40, y0, 560, h, "box")
  rectfill(40, y0, 560, h, C_PANEL)
  rect(40, y0, 560, h, C_ACC)
  local tx = print(p.title, 56, y0, C_ACC) + 16
  chip_hint("esc", "B", "back", chip_hint("enter", "A", "choose", tx, y0), y0)
  local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
  for i = first, min(#p.rows, first + rows - 1) do
    local y = y0 + 16 + (i - first) * 16
    U.zone(48, y, 544, 16, "pick", i)
    if i == p.sel then rectfill(48, y, 544, 16, C_SEL) end
    print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C_TEXT)
  end
  if first + rows - 1 < #p.rows then print("v", 576, y0 + rows * 16, C_DIM) end
end

local function draw_input()
  U.zone(0, 0, W, H, "shade")
  U.zone(80, 144, 480, 64, "box")
  rectfill(80, 144, 480, 64, C_PANEL)
  rect(80, 144, 480, 64, C_ACC)
  print(input.label, 96, 160, C_DIM)
  U.zone(88, 176, 464, 16, "field")              -- a click puts the caret
  U.line_draw(input, 96, 176, C_TEXT, (frame // 20) % 2 == 0)
  chip_hint("esc", nil, "cancel", chip_hint("enter", nil, "ok", 336, 144), 144)
end

-- the mouse in the dialogs: a click chooses a row (on the chosen one, or a
-- double click: takes it), the wheel moves, a click outside is Esc. True
-- when a dialog is open (it has the mouse).
local function dialog_mouse()
  if not pick and not input then return false end
  if pick and U.wheel ~= 0 then pick.sel = clamp(pick.sel - U.wheel, 1, #pick.rows) end
  local z = U.click(0)
  if z and z.kind == "pick" and pick then
    if z.a == pick.sel or U.double then U.press("\n") else pick.sel = z.a end
  elseif z and z.kind == "shade" then
    U.press("esc")
  elseif z and z.kind == "field" and input then
    U.line_click(input, 96, 8)
  end
  return true
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

-- the camera with the mouse in the view (x, y, w, h): the right button
-- dragged turns and tilts, the middle one moves the target, the wheel zooms
local function cam_mouse(cam, x, y, w, h, lo, hi)
  local d, used = U.drag, false
  if d and d.x0 >= x and d.x0 < x + w and d.y0 >= y and d.y0 < y + h then
    if d.b == 1 then
      cam.yaw = cam.yaw - d.dx * 0.01
      cam.pitch = clamp(cam.pitch - d.dy * 0.01, -1.45, 1.2)
      used = true
    elseif d.b == 2 and cam.r then
      local k = cam.dist / FOCAL
      cam.tx = cam.tx - (cam.r[1] * d.dx - cam.u[1] * d.dy) * k
      cam.ty = cam.ty + cam.u[2] * d.dy * k
      cam.tz = cam.tz - (cam.r[3] * d.dx - cam.u[3] * d.dy) * k
      used = true
    end
  end
  if U.wheel ~= 0 and U.inside(x, y, w, h) then
    cam.dist = clamp(cam.dist * 0.85 ^ U.wheel, lo or 0.05, hi or 500)
    used = true
  end
  return used
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
    U.zone(x, yy, PANEL_W, 16, "mrow", i)
    if i == cur then rectfill(x, yy, PANEL_W, 16, C_SEL) end
    print(KIND_L[it.kind], x + 8, yy, KIND_C[it.kind])
    print(it.name:sub(1, 22) .. (it.dirty and "*" or ""), x + 24, yy, i == cur and 0xFFFFFF or C_TEXT)
  end
  if first > 1 then print("^", x + PANEL_W - 16, y + 16, C_DIM) end
  if first + rows - 1 < #items then print("v", x + PANEL_W - 16, y + rows * 16, C_DIM) end
end


-- the pages: what each one gives the others (the rest stays inside its block)
local lp, list_reset, list_key, list_pad, list_actions, draw_list_page, list_mouse
local ed, edit_reset, edit_key, edit_pad, edit_actions, draw_edit, edit_mouse

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
lp.select = select_item                -- (for the code outside this block)

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

-- new models: a cube, a plane, a sphere, a cylinder (in the colour of the edit page)
local function new_shape(shape)
  if not proj.path then say("open a .bm first: Esc > Open", C_ERR); return end
  local it = { kind = "model", name = unique_name("model", shape), v = {}, f = {}, dirty = true }
  B.G.primitive(it, shape, { 0, shape == "plane" and 0 or 0.5, 0 }, 1, ed.colour)
  dirty = true
  select_item(insert_item(it))
  say("new model " .. it.name .. ": F2 edits it", C_ACC)
end

-- a humanoid base: 17 boxes on 16 bones (T.G.humanoid); bm Animator makes the walk and the poses
function lp.new_humanoid()
  if not proj.path then say("open a .bm first: Esc > Open", C_ERR); return end
  local it = B.G.humanoid(unique_name("model", "man"))
  dirty = true
  select_item(insert_item(it))
  say("new model " .. it.name .. ": " .. #it.v .. " vertices, " .. it.bones .. " bones (bm Animator animates it)", C_ACC)
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
    { "New model: cylinder", function() new_shape("cylinder") end },
    { "New model: humanoid with a skeleton", function() lp.new_humanoid() end },
  }
  rows[#rows + 1] = { "Import a model: /bmm, /glb (i)", function() lp.import_dialog() end }
  rows[#rows + 1] = { "Export to /bmm (x)", function() lp.export_dialog() end }
  if not it then rows = { rows[7], rows[8], rows[9], rows[10], rows[11], rows[12] } end
  choose("the mesh" .. (it and (": " .. it.name) or ""), rows)
end

-- the mouse: a click chooses a mesh, a double click edits it, the wheel
-- moves in the list; right drag turns the view, middle moves, wheel zooms
function list_mouse()
  local z = U.at()
  if z and z.kind == "mrow" then
    if U.wheel ~= 0 then select_item(clamp(cur - U.wheel, 1, #items)) end
    if U.click(0) then
      if z.a ~= cur then select_item(z.a) end
      if U.double then go("edit") end
    end
    return
  end
  if cam_mouse(lp.cam, PANEL_W, 96, W - PANEL_W, HINT_Y - 96, 0.1, 500) and U.drag then lp.spin = false end
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
  elseif k == "i" then lp.import_dialog()
  elseif k == "x" then lp.export_dialog()
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

ed = { mirror = false, snap = 0, snap_i = 2, snap_steps = { 0.05, 0.1, 0.25, 0.5 }, mode = "v", sv = {}, sf = {}, se = {}, he = nil, sig = "", order = 0, px = W // 2, py = 184, tool = nil, step = 3, colour = 0xE84A5A,
       cam = { yaw = 0.6, pitch = -0.45, dist = 4, tx = 0, ty = 0, tz = 0 },
       sx = {}, sy = {}, sz = {}, hv = nil, hf = nil, force = nil, pal = nil, last_k = nil, last_f = 0, rep = 0 }

function edit_reset()
  ed.sv, ed.sf, ed.se, ed.order, ed.tool, ed.force, ed.pal = {}, {}, {}, 0, nil, nil, nil
  ed.he, ed.sig, ed.box = nil, "", nil
  local cur_it = I()
  if cur_it and cur_it.hid then cur_it.hid, cur_it.view = nil, nil end
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
  ed.sig = ""
end

local function count(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

-- the vertices the tools move: the chosen ones, or the corners of the
-- chosen faces
local function sel_verts()
  local it, out, seen = I(), {}, {}
  local hid = it.hid or {}
  if ed.mode ~= "f" then
    for i in pairs(ed.sv) do if it.v[i] and not hid[i] then out[#out + 1] = i end end
  else
    for fi in pairs(ed.sf) do
      local f = it.f[fi]
      if f then
        for k = 1, 3 do
          local i = f[k]
          if not seen[i] and not hid[i] then seen[i] = true; out[#out + 1] = i end
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

-- vertices no face uses go away (the selection and the bones follow)
local function compact(it)
  local h
  ed.sv, h = B.G.compact(it, ed.sv, it.hid)
  it.hid = next(h) and h or nil
end

local function drop_degenerate(it)
  local n = B.G.drop_degenerate(it)
  if n > 0 then ed.sf = {} end     -- the faces moved
  return n
end

-- the screen: where each vertex is (after look()); what is under the pointer
local function project_all(it)
  local sx, sy, sz = {}, {}, {}
  local hid = it.hid or {}
  for i, p in ipairs(it.v) do
    if not hid[i] then                          -- a hidden vertex has no place: nothing sees it
      local x, y, z = project3d(p[1], p[2], p[3])
      if x and abs(x) < 8000 and abs(y) < 8000 then sx[i], sy[i], sz[i] = x, y, z end
    end
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
  ed.hv, ed.hf, ed.he = nil, nil, nil
  if ed.mode == "e" then ed.edge_hover(it); return end
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

-- The pivot of rotate and scale (APP-26, Shift+O): "centre" (the middle of the choice), "origin" (0, 0, 0)
-- or "base" (the middle in x and z, the lowest y of the choice). src[i] is the position of vertex i.
-- With the live mirror a counterpart is the mirror image of its vertex, so it turns about the mirrored pivot.
ed.pivot = "centre"
function ed.pivot_of(src, list)
  if ed.pivot == "origin" then return { 0, 0, 0 } end
  local c, lo = { 0, 0, 0 }, nil
  for _, i in ipairs(list) do
    for k = 1, 3 do c[k] = c[k] + src[i][k] end
    if lo == nil or src[i][2] < lo then lo = src[i][2] end
  end
  for k = 1, 3 do c[k] = c[k] / max(1, #list) end
  if ed.pivot == "base" then c[2] = lo or 0 end
  return c
end

function ed.pivot_text()
  return ed.pivot ~= "centre" and ("PIVOT " .. ed.pivot) or nil
end

----------------------------------------------------------------- edges (select mode "e")
-- ed.se is { [edge key] = order } (B.G.edge_key); ed.sv always holds the ends
-- of the chosen edges, so the tools and the commands that work on vertices
-- (move, rotate, scale, extrude, the live mirror) work on edges as they are.

function ed.edge_list(it)
  local out = {}
  for k in pairs(B.G.edge_faces(it)) do out[#out + 1] = k end
  table.sort(out)
  return out
end

-- the edge nearest to the pointer (6 pixels at most); in front of the others
function ed.edge_hover(it)
  if ed.force then ed.he = ed.edge_list(it)[ed.force]; return end
  local sx, sy, sz = ed.sx, ed.sy, ed.sz
  local best, bd, bz, seen = nil, 37, 1e18, {}
  for _, f in ipairs(it.f) do
    for k = 1, 3 do
      local a, b = f[k], f[k % 3 + 1]
      if sx[a] and sx[b] then
        local key = B.G.edge_key(a, b)
        if not seen[key] then
          seen[key] = true
          local ax, ay = sx[a], sy[a]
          local dx, dy = sx[b] - ax, sy[b] - ay
          local l2 = dx * dx + dy * dy
          local t = l2 > 0 and clamp(((ed.px - ax) * dx + (ed.py - ay) * dy) / l2, 0, 1) or 0
          local ex, ey = ax + dx * t - ed.px, ay + dy * t - ed.py
          local d, z = ex * ex + ey * ey, (sz[a] + sz[b]) / 2
          if d < bd - 1 or (d < bd + 1 and z < bz) then best, bd, bz = key, d, z end
        end
      end
    end
  end
  ed.he = best
end

function ed.se_to_sv()
  ed.sv = {}
  for k, o in pairs(ed.se) do
    local a, b = B.G.edge_ends(k)
    ed.sv[a], ed.sv[b] = ed.sv[a] or o, ed.sv[b] or o
  end
end

-- after a change of the mesh or of the chosen vertices: the chosen edges are
-- edges of the mesh with both ends chosen; a chosen vertex with no chosen edge
-- brings the edges between the chosen vertices; the other vertices go
function ed.sync_e(it)
  if ed.mode ~= "e" then return end
  local sig = #it.v .. ":" .. #it.f .. ":" .. count(ed.sv) .. ":" .. count(ed.se)
  if sig == ed.sig then return end
  local ef = B.G.edge_faces(it)
  local cov = {}
  for k in pairs(ed.se) do
    local a, b = B.G.edge_ends(k)
    if ef[k] and ed.sv[a] and ed.sv[b] then cov[a], cov[b] = true, true else ed.se[k] = nil end
  end
  local lone = false
  for i in pairs(ed.sv) do if not cov[i] then lone = true; break end end
  if lone then
    for k in pairs(ef) do
      local a, b = B.G.edge_ends(k)
      if ed.sv[a] and ed.sv[b] and not (cov[a] and cov[b]) then ed.se[k] = ed.sv[a] end
    end
  end
  ed.se_to_sv()
  ed.sig = #it.v .. ":" .. #it.f .. ":" .. count(ed.sv) .. ":" .. count(ed.se)
end

-- an edge in or out; with the live mirror its counterpart too
function ed.edge_set(it, key, on)
  local keys = { key }
  if ed.mirror then
    local ef = B.G.edge_faces(it)
    local a, b = B.G.edge_ends(key)
    local mp = B.G.mirror_pairs(it, { a, b })
    if mp[a] and mp[b] then
      local k2 = B.G.edge_key(mp[a], mp[b])
      if k2 ~= key and ef[k2] then keys[2] = k2 end
    end
  end
  ed.order = ed.order + 1
  for _, k in ipairs(keys) do ed.se[k] = on and ed.order or nil end
  ed.se_to_sv()
  ed.sig = ""
end

-- Ctrl+R: the loop cut on the chosen edge (edge mode: one edge; vertex
-- mode: two vertices that make an edge)
function ed.loop_cut()
  local it = I()
  local a, b
  if ed.mode == "e" then
    local k1, k2
    for k in pairs(ed.se) do if not k1 then k1 = k else k2 = k end end
    -- live mirror: an edge and its counterpart are chosen together (ed.edge_set): either one is the edge
    if ed.mirror and k1 and k2 and count(ed.se) == 2 then
      local a1, b1 = B.G.edge_ends(k1)
      local mp = B.G.mirror_pairs(it, { a1, b1 })
      if not (mp[a1] and mp[b1] and B.G.edge_key(mp[a1], mp[b1]) == k2) then k1 = nil end
    elseif count(ed.se) ~= 1 then k1 = nil end
    if not k1 then say("loop cut: choose one edge first", C_ERR); return end
    a, b = B.G.edge_ends(k1)
  elseif ed.mode == "v" and count(ed.sv) == 2 then
    local l = {}
    for i in pairs(ed.sv) do l[#l + 1] = i end
    a, b = l[1], l[2]
  else
    say("loop cut: choose one edge first (Tab: edges)", C_ERR)
    return
  end
  local plan, err = B.G.loop_plan(it, a, b)
  if not plan then say("loop cut: " .. err, C_ERR); return end
  it = begin_edit()
  local res
  if ed.mirror then
    -- live mirror: the ring of the counterpart edge too, when there is one (G.loop_cut_mirror)
    res = B.G.loop_cut_mirror(it, a, b)
  else
    res = B.G.loop_apply(it, plan)
  end
  ed.sv, ed.sf, ed.se, ed.order = {}, {}, {}, 0
  if ed.mode == "e" then
    for _, r in ipairs(res.rungs) do ed.order = ed.order + 1; ed.se[B.G.edge_key(r[1], r[2])] = ed.order end
    ed.se_to_sv()
  else
    for _, m in ipairs(res.mids) do select_v(m, true) end
  end
  ed.sig = ""
  finish_edit(string.format("loop cut: %d new vertices, %d quads%s%s%s", #res.mids, #plan.quads + (res.quads2 or 0),
              plan.closed and " (closed ring)" or " (stopped at the ends)",
              #plan.plains > 0 and (", " .. #plan.plains .. " other faces cut in 2") or "",
              ed.mirror and (res.rings == 2 and " (mirror X: both rings)" or
                (res.mirror_err and (" (mirror X: " .. res.mirror_err .. ")") or " (mirror X: one ring)")) or ""))
end

-- Shift+J: knife-lite. Edge mode, two chosen edges of one triangle: a vertex
-- in the middle of each, the triangle cut so that a new edge joins them; the
-- face across each edge is cut too. Not mirrored: the knife works on one triangle, so it stays on the
-- side where it is cut (the loop cut and Shift+A are mirrored).
function ed.knife()
  local it = I()
  if ed.mode ~= "e" then say("knife: choose two edges first (Tab: edges)", C_ERR); return end
  if count(ed.se) ~= 2 then
    say("knife: choose two edges of one triangle" .. (ed.mirror and " (live mirror off: Shift+X)" or ""), C_ERR)
    return
  end
  local k1, k2
  for k in pairs(ed.se) do if not k1 then k1 = k else k2 = k end end
  local plan, err = B.G.knife_plan(it, k1, k2)
  if not plan then say("knife: " .. err, C_ERR); return end
  it = begin_edit()
  local res = B.G.knife_apply(it, plan)
  ed.sv, ed.sf, ed.se, ed.order = {}, {}, {}, 1
  ed.se[B.G.edge_key(res.rung[1], res.rung[2])] = 1
  ed.se_to_sv()
  ed.sig = ""
  finish_edit(string.format("knife: 2 new vertices, %d faces added", res.faces))
end
-- Shift+E: bevel (APP-12). The chosen edges (edge mode; in the other modes the edges with both ends
-- chosen) each become a narrow strip of the width typed. The flat ones are left out. With the live mirror on
-- the counterparts across x = 0 are bevelled too. The strip faces are chosen after it.
function ed.bevel_keys(it)
  local keys = {}
  if ed.mode == "e" then
    for k in pairs(ed.se) do keys[#keys + 1] = k end
  else
    for k in pairs(B.G.edge_faces(it)) do
      local a, b = B.G.edge_ends(k)
      if ed.sv[a] and ed.sv[b] then keys[#keys + 1] = k end
    end
  end
  table.sort(keys)
  if ed.mirror and #keys > 0 then
    local ef, vs, seen = B.G.edge_faces(it), {}, {}
    for _, k in ipairs(keys) do
      local a, b = B.G.edge_ends(k)
      vs[#vs + 1], vs[#vs + 2], seen[k] = a, b, true
    end
    local mp = B.G.mirror_pairs(it, vs)
    for i = 1, #keys do
      local a, b = B.G.edge_ends(keys[i])
      local ma, mb = mp[a], mp[b]
      if ma and mb and ma ~= mb then
        local k2 = B.G.edge_key(ma, mb)
        if ef[k2] and not seen[k2] then seen[k2] = true; keys[#keys + 1] = k2 end
      end
    end
  end
  return keys
end

function ed.bevel_run(w)
  local it = I()
  local keys = ed.bevel_keys(it)
  if #keys == 0 then say("bevel: choose edges first (Tab: edges)", C_ERR); return end
  local plan, err = B.G.bevel_plan(it, keys, w)
  if not plan then say("bevel: " .. err, C_ERR); return end
  it = begin_edit()
  local res = B.G.bevel_apply(it, plan)
  ed.sv, ed.sf, ed.se, ed.order, ed.mode = {}, {}, {}, 0, "f"
  for _, fi in ipairs(res.strips) do ed.sf[fi] = true end
  ed.sig = ""
  finish_edit(string.format("bevel: %d edges, width %s, %d faces added%s%s", #plan.edges, num(plan.w), res.faces,
              plan.flat > 0 and (", " .. plan.flat .. " flat left out") or "",
              plan.w < w - 1e-9 and " (width cut to fit)" or ""))
end

function ed.bevel()
  local it = I()
  if not it then return end
  if #ed.bevel_keys(it) == 0 then say("bevel: choose edges first (Tab: edges)", C_ERR); return end
  ask("bevel width", ed.bevel_text or "0.1", function(t)
    local w = tonumber(t)
    if not w or w <= 0 then say("bevel: the width is a number above 0", C_ERR); return end
    ed.bevel_text = t
    ed.bevel_run(w)
  end)
end

-- Shift+U: box projection onto the sprite sheet (APP-12). The chosen faces (all the shown faces when none is
-- chosen) go to 6 cells of the sheet, 3 across and 2 down, by the side they look to; cell size and place are
-- typed ("32 0 0": 32-pixel cells with the top left at x 0, y 0).
function ed.project_run(cell, x, y)
  local it = I()
  local fs = sel_faces()
  if #fs == 0 then
    for fi, f in ipairs(it.f) do if not (it.hid and B.G.face_hidden(f, it.hid)) then fs[#fs + 1] = fi end end
  end
  local sw, sh = cart_sheet()
  local plan, err = B.G.project_plan(it, fs, { x, y, cell }, sw, sh)
  if not plan then say("project: " .. err, C_ERR); return end
  it = begin_edit()
  local n, sc = B.G.project_apply(it, plan)
  finish_edit(string.format("project: %d faces on the sheet at %d,%d, 3 x 2 cells of %d px, %s px per unit",
              n, x, y, cell, num(round(sc * 10) / 10)))
end

function ed.project()
  if not I() then return end
  ask("sheet cell px, x, y", ed.project_text or "32 0 0", function(t)
    local c, x, y = t:match("^%s*(%d+)%s+(%d+)%s+(%d+)%s*$")
    if not c then c = t:match("^%s*(%d+)%s*$"); x, y = 0, 0 end
    if not c then say("project: type the cell size, x and y: 32 0 0", C_ERR); return end
    ed.project_text = t
    ed.project_run(tonumber(c), tonumber(x), tonumber(y))
  end)
end

-- delete in edge mode: the faces next to the chosen edges
function ed.edge_delete()
  if not next(ed.se) then say("choose edges first", C_ERR); return end
  local it = begin_edit()
  local ef = B.G.edge_faces(it)
  local keys = {}
  for k in pairs(ed.se) do keys[#keys + 1] = k end
  if ed.mirror then
    local list = {}
    for i in pairs(ed.sv) do list[#list + 1] = i end
    local mp = B.G.mirror_pairs(it, list)
    for _, k in ipairs({ table.unpack(keys) }) do
      local a, b = B.G.edge_ends(k)
      if mp[a] and mp[b] then keys[#keys + 1] = B.G.edge_key(mp[a], mp[b]) end
    end
  end
  local del, n = {}, 0
  for _, k in ipairs(keys) do
    for _, fi in ipairs(ef[k] or {}) do if not del[fi] then del[fi] = true; n = n + 1 end end
  end
  local out = {}
  for fi, f in ipairs(it.f) do if not del[fi] then out[#out + 1] = f end end
  it.f, ed.sv, ed.sf, ed.se = out, {}, {}, {}
  compact(it)
  ed.sig = ""
  finish_edit("deleted: " .. n .. " faces next to the edges")
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
  ed.tool = { kind = kind, vs = vs, base = base, c = ed.pivot_of(it.v, vs), n = unit(n), axis = axis, buf = "",
              d = { 0, 0, 0 }, m = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, s = { 1, 1, 1 }, deg = 0 }
  ed.tool.e = ed.tool.d                  -- the move after the snap (the same without it)
  ed.tool.lead = min(table.unpack(vs))   -- the leader: the snap works on this vertex
  ed.tool.moving = {}                    -- the vertices that move: no snap target
  for _, i in ipairs(vs) do ed.tool.moving[i] = true end
  if ed.mirror then
    -- live mirror: a vertex with a counterpart across X = 0 moves it too. When both
    -- are chosen, the lower number leads and the other follows.
    local mp = B.G.mirror_pairs(it, vs)
    if it.hid then for i, j in pairs(mp) do if it.hid[j] then mp[i] = nil end end end    -- a hidden counterpart stays
    local sorted, fol, skip, lone = { table.unpack(vs) }, {}, {}, 0
    table.sort(sorted)
    for _, i in ipairs(sorted) do
      local j = mp[i]
      if j == nil then lone = lone + 1
      elseif j ~= i and not skip[i] then fol[i], skip[j] = j, true end
    end
    ed.tool.fol, ed.tool.skip = fol, skip
    for _, j in pairs(fol) do ed.tool.moving[j] = true end
    if lone > 0 then say("mirror X: " .. lone .. " chosen without a counterpart", C_DIM, 120) end
  end
  if kind == "move" then ed.soft_build(ed.tool, it) end
end

-- Proportional (soft) move (APP-11, Shift+P): the vertices near the chosen ones follow a move, less and
-- less with the distance (B.G.falloff, a smooth fall to 0 at the radius ed.prop_steps[ed.prop]). The
-- distance is the 3D distance to the nearest chosen vertex. With the live mirror the counterparts pull
-- their own neighbours, mirrored. t.soft = { list, w, side, base }; hidden vertices stay.
ed.prop, ed.prop_steps = 0, { 0.25, 0.5, 1, 2 }
function ed.soft_build(t, it)
  if t.soft then                               -- a new radius: the vertices go back first
    for _, i in ipairs(t.soft.list) do
      local b, p = t.soft.base[i], it.v[i]
      p[1], p[2], p[3] = b[1], b[2], b[3]
    end
  end
  t.soft = nil
  local r = ed.prop > 0 and ed.prop_steps[ed.prop] or 0
  if r <= 0 then return end
  local lead, fol = {}, {}
  for _, i in ipairs(t.vs) do if not (t.skip and t.skip[i]) then lead[#lead + 1] = i end end
  if t.fol then for _, j in pairs(t.fol) do fol[#fol + 1] = j end end
  local skip = {}
  for i in pairs(t.moving) do skip[i] = true end
  if it.hid then for i in pairs(it.hid) do skip[i] = true end end
  local wa = B.G.soft_weights(it, lead, r, skip)
  local wb = #fol > 0 and B.G.soft_weights(it, fol, r, skip) or {}
  local soft = { list = {}, w = {}, side = {}, base = {} }
  for i, w in pairs(wa) do soft.w[i] = w end
  for i, w in pairs(wb) do
    if w > (soft.w[i] or 0) then soft.w[i], soft.side[i] = w, true end
  end
  for i in pairs(soft.w) do
    soft.list[#soft.list + 1] = i
    local p = it.v[i]
    soft.base[i] = { p[1], p[2], p[3] }
  end
  table.sort(soft.list)
  t.soft = soft
end

-- the move of the tool after the snap: the leader goes to the grid, or to the
-- vertex nearest on the screen; every chosen vertex gets the same move
function ed.snap_delta(t, it)
  t.e, t.target = t.d, nil
  if ed.snap == 0 then return t.d end
  local b = t.base[t.lead]
  local p = { b[1] + t.d[1], b[2] + t.d[2], b[3] + t.d[3] }
  local q, target = B.G.snap_pos(it, p, ed.snap, ed.snap_steps[ed.snap_i],
                                 function(v) return project3d(v[1], v[2], v[3]) end, 6, t.moving, t.axis)
  t.e = { round((q[1] - b[1]) * 1e5) / 1e5, round((q[2] - b[2]) * 1e5) / 1e5, round((q[3] - b[3]) * 1e5) / 1e5 }
  t.target = target
  return t.e
end

local function tool_apply()
  local t, it = ed.tool, I()
  local d = t.kind == "move" and ed.snap_delta(t, it) or t.d
  for _, i in ipairs(t.vs) do
    if t.skip and t.skip[i] then goto next_vertex end
    local b, p = t.base[i], it.v[i]
    if t.kind == "move" then
      p[1], p[2], p[3] = b[1] + d[1], b[2] + d[2], b[3] + d[3]
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
    local j = t.fol and t.fol[i]
    if j then local q = it.v[j]; q[1], q[2], q[3] = -p[1], p[2], p[3] end
    ::next_vertex::
  end
  local sf = t.soft
  if sf then                                   -- the neighbours, with their share of the move
    for _, i in ipairs(sf.list) do
      local b, w, p = sf.base[i], sf.w[i], it.v[i]
      p[1] = b[1] + (sf.side[i] and -d[1] or d[1]) * w
      p[2], p[3] = b[2] + d[2] * w, b[3] + d[3] * w
    end
  end
  it.view = nil
end

function ed.cycle_pivot()
  ed.pivot = ed.pivot == "centre" and "origin" or (ed.pivot == "origin" and "base" or "centre")
  local t = ed.tool
  if t and t.kind ~= "move" then t.c = ed.pivot_of(t.base, t.vs); tool_apply() end
  say(ed.pivot == "centre" and "pivot: the middle of the choice (Shift+O: origin)" or
      ("PIVOT " .. ed.pivot .. (ed.pivot == "origin" and " (0, 0, 0)" or " (the lowest point of the choice)") ..
       " for rotate and scale (Shift+O: next)"), ed.pivot == "centre" and C_DIM or C_ACC, 140)
end

-- Smooth (APP-25, Shift+V): one pass; each chosen vertex moves ed.smooth_k toward the mean of its edge
-- neighbours (B.G.smooth). An open border and the hidden vertices stay. With the live mirror a counterpart
-- gets the mirror image of the result. One undo step for each pass.
ed.smooth_k = 0.5
function ed.smooth()
  local vs = sel_verts()
  if #vs == 0 then say("smooth: choose vertices or faces first (Shift+V relaxes them)", C_ERR); return end
  local fol
  if ed.mirror then
    local it = I()
    local mp = B.G.mirror_pairs(it, vs)
    if it.hid then for i, j in pairs(mp) do if it.hid[j] then mp[i] = nil end end end
    local sorted, skip = { table.unpack(vs) }, {}
    table.sort(sorted)
    fol = {}
    for _, i in ipairs(sorted) do
      local j = mp[i]
      if j and j ~= i and not skip[i] then fol[i], skip[j] = j, true end
    end
    local lead = {}
    for _, i in ipairs(vs) do if not skip[i] then lead[#lead + 1] = i end end
    vs = lead
  end
  local it = begin_edit()
  local n = B.G.smooth(it, vs, ed.smooth_k, fol)
  if n == 0 then
    table.remove(undo)
    say("smooth: nothing moved (an open border and single vertices stay)", C_DIM, 120)
    return
  end
  it.view = nil
  finish_edit("smooth: " .. n .. " vertices relaxed" .. (ed.mirror and " (mirror X: both sides)" or "") .. " (Shift+V: again)")
end

function ed.cycle_prop()
  ed.prop = (ed.prop + 1) % (#ed.prop_steps + 1)
  local t = ed.tool
  if t and t.kind == "move" then ed.soft_build(t, I()); tool_apply() end
  say(ed.prop == 0 and "soft move off" or ("soft move: radius " .. num(ed.prop_steps[ed.prop])), C_ACC, 90)
end

local function tool_text()
  local t = ed.tool
  local ax = t.axis == "n" and "along the normal" or (t.axis and ("on " .. AXIS[t.axis]) or "")
  local typed = t.buf ~= "" and ("  typed " .. t.buf .. "_") or ""
  if t.kind == "move" then
    return string.format("MOVE %s  x %s  y %s  z %s", ax, num(t.e[1]), num(t.e[2]), num(t.e[3])) ..
           (t.soft and ("  soft " .. num(ed.prop_steps[ed.prop]) .. " (" .. #t.soft.list .. " near)") or "") .. typed ..
           string.format("  (step %s)", num(MOVE_STEPS[ed.step])) .. ed.snap_text(" ")
  elseif t.kind == "rotate" then
    return string.format("ROTATE %s  %s deg", t.axis and ax or "", num(t.deg)) .. typed .. string.format("  (step %d)", ROT_STEPS[ed.step])
  end
  return string.format("SCALE %s  x %s  y %s  z %s", ax, num(t.s[1]), num(t.s[2]), num(t.s[3])) .. typed ..
         string.format("  (step x%s)", num(SCALE_STEPS[ed.step]))
end

-- Numeric entry (APP-11): digits, "." and "-" typed in a move, rotate or scale give the exact value of
-- the axis chosen (x, y, z, n; none: x for a move, y for a rotation, all three for a scale). Another
-- axis key, an arrow or Enter takes the number as it is; Backspace takes the last character off.
function ed.tool_typed(t)
  local v = B.G.parse_number(t.buf)
  local a = t.axis
  if t.kind == "move" then
    v = v or 0
    if a == "n" then
      for i = 1, 3 do t.d[i] = round(t.n[i] * v * 1e5) / 1e5 end
    else
      t.d[a or 1] = round(v * 1e5) / 1e5
    end
  elseif t.kind == "rotate" then
    v = v or 0
    t.m = rot_matrix(a == "n" and t.n or axis_vec(a or 2), math.rad(v))
    t.deg = v
  else
    v = v or 1
    if a and a ~= "n" then t.s[a] = v else t.s[1], t.s[2], t.s[3] = v, v, v end
  end
  tool_apply()
end

function ed.tool_type_key(t, k)
  if k:match("^%d$") or (k == "." and t.buf ~= "") or (k == "-" and t.buf == "") then
    if #t.buf < 12 then t.buf = t.buf .. k end
  elseif (k == "\b" or k == "del") and t.buf ~= "" then
    t.buf = t.buf:sub(1, -2)
  else
    return false
  end
  ed.tool_typed(t)
  return true
end

local function tool_ok()
  local what = tool_text():gsub("%s*%(step.*$", ""):lower() .. ": " .. #ed.tool.vs .. " vertices" ..
               (ed.tool.fol and " (mirror X)" or "")
  local near = ed.tool.soft and #ed.tool.soft.list or 0
  ed.tool = nil
  finish_edit(what .. (near > 0 and (" + " .. near .. " near") or ""))
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
  if ed.tool_type_key(t, k) then return end
  if k == "P" then ed.cycle_prop(); return end
  if k == "O" then ed.cycle_pivot(); return end
  t.buf = ""
  if k == "x" or k == "y" or k == "z" then
    local a = (k == "x" and 1) or (k == "y" and 2) or 3
    t.axis = t.axis ~= a and a or nil
    return
  end
  if k == "n" then t.axis = t.axis ~= "n" and "n" or nil; return end
  if k == "S" then ed.cycle_snap(); return end
  if k == "G" then ed.cycle_step(); return end
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
  if ed.mode ~= "f" then
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
  if ed.mode ~= "f" then for _, nw in pairs(copy) do select_v(nw, true) end
  else for _, fi in ipairs(nf) do ed.sf[fi] = true end end
  if #it.v > LIMIT_V or #it.f > LIMIT_F then finish_edit(); return end
  tool_start("move", nil, true)
  say("copied " .. #fs .. " faces: move the copy, Enter done", C_ACC, 300)
end

-- a cube, cylinder, sphere or plane as a new part of the model: at the
-- middle of the choice (or at 0), in the colour of the page, on the bone of
-- the first chosen vertex; the new vertices are the choice
local function add_part(shape)
  local vs = sel_verts()
  local first = vs[1]
  local it = begin_edit()
  local centre = #vs > 0 and centre_of(it, vs) or { 0, 0, 0 }
  local bone = it.vb and first and it.vb[first] or 0
  local nv, twin
  if ed.mirror then
    -- live mirror: the mirror image across X = 0 too (a part in the middle of the plane is added once)
    local mb = bone
    if it.ac and it.vb then mb = B.G.mirror_bone(B.G.rig_bones(it), bone) end
    nv, twin = B.G.primitive_mirror(it, shape, centre, 1, ed.colour, bone, mb)
  else
    nv, twin = B.G.primitive(it, shape, centre, 1, ed.colour, bone), {}
  end
  ed.mode, ed.sv, ed.sf, ed.order = "v", {}, {}, 0
  for _, i in ipairs(nv) do select_v(i, true) end
  finish_edit("added a " .. shape .. ": " .. #nv .. " vertices chosen" ..
              (#twin > 0 and " (mirror X: both sides)" or (ed.mirror and " (in the middle: once)" or "")))
end

local function op_add_part()
  choose("add a part", {
    { "Cube", function() add_part("cube") end },
    { "Cylinder", function() add_part("cylinder") end },
    { "Sphere", function() add_part("sphere") end },
    { "Plane", function() add_part("plane") end },
  })
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

-- live mirror: the faces and their counterparts across X = 0 (a new list)
function ed.mirror_faces(it, fs)
  if not ed.mirror then return fs end
  local corners, seen = {}, {}
  for _, fi in ipairs(fs) do
    for k = 1, 3 do local c = it.f[fi][k]; if not seen[c] then seen[c] = true; corners[#corners + 1] = c end end
  end
  local mf = B.G.mirror_faces(it, fs, B.G.mirror_pairs(it, corners))
  local out, in_out = {}, {}
  for _, fi in ipairs(fs) do if not in_out[fi] then in_out[fi] = true; out[#out + 1] = fi end end
  for _, fi in ipairs(fs) do
    local g = mf[fi]
    if g and not in_out[g] then in_out[g] = true; out[#out + 1] = g end
  end
  return out
end

local function op_paint()
  local fs = sel_faces()
  if not need_faces(fs) then return end
  local it = begin_edit()
  fs = ed.mirror_faces(it, fs)
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
  if ed.mode == "e" then ed.edge_delete(); return end
  if ed.mode == "v" then
    if not next(ed.sv) then say("choose vertices first", C_ERR); return end
    it = begin_edit()
    if ed.mirror then
      local list = {}
      for i in pairs(ed.sv) do list[#list + 1] = i end
      for _, j in pairs(B.G.mirror_pairs(it, list)) do ed.sv[j] = ed.sv[j] or ed.order end
    end
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
    fs = ed.mirror_faces(it, fs)
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
  if ed.mode ~= "f" then
    for i in ipairs(it.v) do if want[root(i)] and not ed.sv[i] and not (it.hid and it.hid[i]) then select_v(i, true) end end
    n = count(ed.sv)
  else
    for fi, f in ipairs(it.f) do
      if want[root(f[1])] and not (it.hid and B.G.face_hidden(f, it.hid)) then ed.sf[fi] = true end
    end
    n = count(ed.sf)
  end
  say(n .. " chosen", C_ACC, 60)
end

local function op_all()
  local it = I()
  if next(ed.sv) or next(ed.sf) or next(ed.se) then
    ed.sv, ed.sf, ed.se = {}, {}, {}
    say("none chosen", C_DIM, 60)
  elseif ed.mode ~= "f" then
    for i in ipairs(it.v) do if not (it.hid and it.hid[i]) then select_v(i, true) end end
    say("all " .. count(ed.sv) .. " vertices", C_DIM, 60)
  else
    for fi, f in ipairs(it.f) do if not (it.hid and B.G.face_hidden(f, it.hid)) then ed.sf[fi] = true end end
    say("all " .. count(ed.sf) .. " faces", C_DIM, 60)
  end
end

-- Tab: vertices -> edges -> faces -> vertices; the choice goes with it
local MODE_NAME = { v = "vertices", e = "edges", f = "faces" }
local function op_mode()
  local it = I()
  if ed.mode == "v" then                 -- the edges with both ends chosen
    ed.mode, ed.se = "e", {}
    for k in pairs(B.G.edge_faces(it)) do
      local a, b = B.G.edge_ends(k)
      if ed.sv[a] and ed.sv[b] then ed.order = ed.order + 1; ed.se[k] = ed.order end
    end
    ed.se_to_sv()
  elseif ed.mode == "e" then             -- the faces with all corners chosen
    local fs = sel_faces()
    ed.mode, ed.sf, ed.se = "f", {}, {}
    for _, fi in ipairs(fs) do ed.sf[fi] = true end
  else
    local vs = sel_verts()
    ed.mode, ed.sv, ed.order = "v", {}, 0
    for _, i in ipairs(vs) do select_v(i, true) end
  end
  ed.force, ed.sig = nil, ""
  say(MODE_NAME[ed.mode], C_DIM, 60)
end

-- space: the vertex (face) under the pointer in or out; Enter: only it
local function op_toggle(only)
  if ed.mode == "e" then
    local k = ed.he
    if only then ed.se, ed.sv, ed.order = {}, {}, 0 end
    if not k then if not only then say("no edge under the pointer", C_DIM, 60) end; return end
    ed.edge_set(I(), k, only or not ed.se[k])
  elseif ed.mode == "v" then
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
  local el = ed.mode == "e" and ed.edge_list(it)
  local n = el and #el or (ed.mode == "v" and #it.v or #it.f)
  if n == 0 then return end
  local i = ((ed.force or (d > 0 and 0 or 1)) - 1 + d) % n + 1
  ed.force = i
  local x, y
  if el then
    local a, b = B.G.edge_ends(el[i])
    if ed.sx[a] and ed.sx[b] then x, y = (ed.sx[a] + ed.sx[b]) / 2, (ed.sy[a] + ed.sy[b]) / 2 end
  elseif ed.mode == "v" then x, y = ed.sx[i], ed.sy[i]
  else
    local f = it.f[i]
    if ed.sx[f[1]] and ed.sx[f[2]] and ed.sx[f[3]] then
      x, y = (ed.sx[f[1]] + ed.sx[f[2]] + ed.sx[f[3]]) / 3, (ed.sy[f[1]] + ed.sy[f[2]] + ed.sy[f[3]]) / 3
    end
  end
  if x then ed.px, ed.py = clamp(round(x), 0, W - 1), clamp(round(y), 16, HINT_Y - 1) end
  say((el and "edge " or (ed.mode == "v" and "vertex " or "face ")) .. i .. "/" .. n, C_DIM, 60)
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

-- Shift+S: snap off -> grid -> vertex -> grid + vertex -> off. Shift+G: the grid step.
function ed.snap_text(pre)
  if ed.snap == 0 then return "" end
  local g = num(ed.snap_steps[ed.snap_i])
  return pre .. "SNAP " .. (ed.snap == 1 and ("grid " .. g) or (ed.snap == 2 and "vertex" or ("grid " .. g .. " + vertex")))
end

function ed.cycle_snap()
  ed.snap = (ed.snap + 1) % 4
  say(ed.snap == 0 and "snap off" or (ed.snap_text("") .. " (Shift+S: next, Shift+G: grid step)"), ed.snap == 0 and C_DIM or C_ACC, 140)
  if ed.tool then if ed.tool.kind == "move" then tool_apply() end end
end

function ed.cycle_step()
  ed.snap_i = ed.snap_i % #ed.snap_steps + 1
  say("grid step " .. num(ed.snap_steps[ed.snap_i]) .. (ed.snap % 2 == 1 and "" or " (snap to the grid: Shift+S)"), C_ACC, 140)
  if ed.tool and ed.tool.kind == "move" then tool_apply() end
end

-- Shift+X: the live mirror across X = 0 for move, rotate, scale, paint and delete
function ed.toggle_mirror()
  ed.mirror = not ed.mirror
  say(ed.mirror and "mirror X on: edits happen on both sides" or "mirror X off", ed.mirror and C_ACC or C_DIM, 120)
end

-- bones (APP-14): Shift+B gives the chosen vertices (the corners of the chosen faces; "l" chooses a whole
-- part) to a bone of the model's skeleton, v tints the vertex dots by bone; the header shows the bone of the
-- choice. The live mirror gives the counterparts the mirror bone (B.G.mirror_bone). B.G.set_bone: bm3d.lua
ed.BONE_C = { 0xFF5050, 0x50FF50, 0x5080FF, 0xFFE040, 0xFF50FF, 0x40E0E0,
              0xFFA030, 0xA0FF40, 0x9060FF, 0xFF80A0, 0x40C090, 0xC0C0C0 }

-- the skeleton of the model on the page, or nil and the reason
function ed.rig_of(it)
  local st = B.G.skin_state(it)
  if st == "none" then return nil, "no skeleton: bm Animator makes the bones" end
  if st == "mismatch" then return nil, "the skeleton does not fit the vertices: skin it in bm Animator" end
  return B.G.rig_bones(it)
end

function ed.give_bone(b)
  local it = I()
  local rig, err = ed.rig_of(it)
  if not rig then say(err, C_ERR, 240); return false end
  local vs = sel_verts()
  if #vs == 0 then say("choose vertices, faces or a part (l) first", C_ERR); return false end
  if b < 0 or b >= #rig then say("no bone " .. b .. " (0 to " .. (#rig - 1) .. ")", C_ERR); return false end
  B.G.ensure_bones(it)                    -- (an unskinned skeleton: all on bone 0; before the undo step is taken)
  it = begin_edit()
  local n = B.G.set_bone(it, vs, b, ed.mirror)
  finish_edit(string.format("%d vertices to bone %d %s%s", n, b, rig[b + 1].name, ed.mirror and " (mirror: the other side too)" or ""))
  return true
end

function ed.bone_dialog()
  local it = I()
  local rig, err = ed.rig_of(it)
  if not rig then say(err, C_ERR, 240); return false end
  local vs = sel_verts()
  if #vs == 0 then say("choose vertices, faces or a part (l) first", C_ERR); return false end
  local rows = {}
  for i, r in ipairs(rig) do
    rows[i] = { string.format("%2d  %s", i - 1, r.name), function() ed.give_bone(i - 1) end }
  end
  local cur_b = B.G.bone_summary(it, vs)
  choose(#vs .. " vertices to a bone", rows, (cur_b or 0) + 1)
  return true
end

function ed.toggle_bone_view()
  local rig, err = ed.rig_of(I())
  if not rig then say(err, C_ERR, 240); return end
  ed.bonecol = not ed.bonecol
  say(ed.bonecol and "bone view: the dots take the colour of their bone" or "bone view off", ed.bonecol and C_ACC or C_DIM, 120)
end

-- the header line: the bone of the choice (or of the vertex under the pointer)
function ed.bone_text(it)
  if not it.vb or not it.ac then return nil end
  local list = {}
  if ed.mode == "f" then
    for fi in pairs(ed.sf) do
      local f = it.f[fi]
      if f then list[#list + 1] = f[1]; list[#list + 1] = f[2]; list[#list + 1] = f[3] end
    end
  else
    for i in pairs(ed.sv) do list[#list + 1] = i end
  end
  local own = #list > 0
  if not own and ed.hv then list[1] = ed.hv end
  local b, n = B.G.bone_summary(it, list)
  if not b then return nil end
  local rig = B.G.rig_bones(it)
  local name = rig[b + 1] and rig[b + 1].name or "?"
  if n > 1 then return string.format("BONES %d: %s+", n, name) end
  return string.format("BONE %d %s%s", b, name, own and "" or " (pointer)")
end

-- the ghost reference (h shows / hides, Shift+H loads or clears, [ ] scale, { } height): a model of /bmm or
-- /glb as a faint wire behind the model; it is not in `items`, not chosen, not counted, not saved, not
-- mirrored or snapped to (B.ghost_load, B.G.ghost_*: bm3d.lua); ed.ghost is nil without one
ed.GHOST_C = 0x3E7C68

function ed.ghost_clear()
  if not ed.ghost then return end
  ed.ghost = nil                                 -- the vertices and the edges go
  if collectgarbage then collectgarbage() end
  say("ghost cleared", C_ACC, 300)
end

-- the ghost detail: the budget of faces of the next load (in the editor state, never in the model)
ed.ghost_detail_i = 3
function ed.ghost_budget() return B.G.GHOST_DETAIL[ed.ghost_detail_i] end
function ed.ghost_detail_text()
  local n = ed.ghost_budget()
  return "Ghost detail: " .. (n >= 65535 and "all" or tostring(n)) .. " faces (Enter: next)"
end

function ed.ghost_set(name, keep)
  local g, err = B.ghost_load(name, ed.ghost_budget())
  if not g then say(tostring(err), C_ERR, 300); return false end
  if keep then g.scale, g.dy, g.on = keep.scale, keep.dy, keep.on end
  ed.ghost = nil
  ed.ghost = g
  say(B.G.ghost_text(g) .. "  (h hides, [ ] scale, { } height)", (g.stride > 1 or g.every > 1) and C_ERR or C_ACC, 420)
  return true
end

-- the row of the chooser: 2000, 5000, 20000, all; a ghost that is loaded is read again with the new budget
function ed.ghost_detail_next()
  ed.ghost_detail_i = ed.ghost_detail_i % #B.G.GHOST_DETAIL + 1
  local g = ed.ghost
  if g then ed.ghost_set(g.name, g) else say((ed.ghost_detail_text():gsub(" %(Enter: next%)", "")), C_ACC, 200) end
  ed.ghost_dialog(true)
end

function ed.ghost_toggle()
  local g = ed.ghost
  if not g then say("no ghost: Shift+H loads a model from /bmm or /glb as a reference", C_DIM, 200); return end
  g.on = not g.on
  say(g.on and ("ghost " .. g.name .. " shown") or ("ghost " .. g.name .. " hidden"), C_ACC, 120)
end

function ed.ghost_dialog(at_detail)
  local list = B.model_files()
  local rows = {}
  if ed.ghost then rows[#rows + 1] = { "Clear the ghost: " .. ed.ghost.name, function() ed.ghost_clear() end } end
  for _, e in ipairs(list) do
    rows[#rows + 1] = { e.name .. (e.kind == "bmm" and "   .bmm" or "   .glb: read only, not converted"),
                        function() ed.ghost_set(e.name) end }
  end
  if #list == 0 then say("no model in /bmm or /glb on the SD card", C_ERR, 300) end
  rows[#rows + 1] = { ed.ghost_detail_text(), function() ed.ghost_detail_next() end }
  choose("ghost: a model as a reference (never saved)", rows, at_detail and #rows or (ed.ghost and 2 or 1))
  return true
end

-- [ ] scale by 5 %, { } height by 0.05 (the ghost keeps them while the mesh changes)
function ed.ghost_nudge(k)
  local g = ed.ghost
  if not g then say("no ghost: Shift+H loads one", C_DIM, 160); return end
  if k == "[" or k == "]" then
    B.G.ghost_scale(g, k == "]" and 1.05 or 1 / 1.05)
  else
    B.G.ghost_lift(g, k == "}" and 0.05 or -0.05)
  end
  say(string.format("ghost %s: scale %.2f, height %+.2f", g.name, g.scale, g.dy), C_ACC, 90)
end

-- the wire, before the model's own: each used vertex is projected once
function ed.ghost_draw()
  local g = ed.ghost
  if not g or not g.on then return end
  -- at most B.G.GHOST_EDGES_FRAME edges: 1 edge in `every`, the window moves by one edge each frame;
  -- a vertex is projected the first time an edge needs it
  local gx, gy, done = {}, {}, {}
  local v, sc, dy = g.v, g.scale, g.dy
  local function at(i)
    if not done[i] then
      done[i] = true
      local p = v[i]
      local x, y = project3d(p[1] * sc, p[2] * sc + dy, p[3] * sc)
      if x and abs(x) < 8000 and abs(y) < 8000 then gx[i], gy[i] = x, y end
    end
    return gx[i]
  end
  local ea, eb, every = g.ea, g.eb, g.every
  g.phase = (g.phase + 1) % every
  for k = g.phase + 1, #ea, every do
    local a, b = ea[k], eb[k]
    if at(a) and at(b) then line(gx[a], gy[a], gx[b], gy[b], ed.GHOST_C) end
  end
end


----------------------------------------------------------------- comfort (APP-11)

-- Hide and isolate (y, Shift+Y): it.hid is the set of hidden vertices ({ [vertex] = true }, not saved:
-- every file keeps all of them). A hidden vertex has no place on the screen (project_all), so it is not
-- drawn, not under the pointer, not chosen by a box, "a" or "l", and no tool moves it; a face with a
-- hidden corner is not drawn (view_of). One undo step for each (the snapshot holds the set).
function ed.hidden_count(it)
  local n = 0
  if it.hid then for _ in pairs(it.hid) do n = n + 1 end end
  return n
end

-- the chosen vertices and, with the live mirror, their counterparts
function ed.choice_set(it)
  local vs = sel_verts()
  local set = {}
  for _, i in ipairs(vs) do set[i] = true end
  if ed.mirror and #vs > 0 then
    for _, j in pairs(B.G.mirror_pairs(it, vs)) do set[j] = true end
  end
  return set, #vs
end

function ed.set_hidden(it, hid)
  it.hid = next(hid) and hid or nil
  it.view = nil
  ed.sv, ed.sf, ed.se, ed.force = {}, {}, {}, nil
  ed.sig = ""
end

function ed.show_all()
  local it = I()
  if not it or not it.hid then say("nothing is hidden", C_DIM, 90); return end
  local n = ed.hidden_count(it)
  UA.push()
  ed.set_hidden(it, {})
  say("shown: " .. n .. " vertices", C_ACC, 120)
end

function ed.hide_choice()
  local it = I()
  local set, n = ed.choice_set(it)
  if n == 0 then
    if it.hid then ed.show_all() else say("choose vertices or faces first", C_ERR) end
    return
  end
  UA.push()
  local hid = {}
  for i in pairs(it.hid or {}) do hid[i] = true end
  local k = 0
  for i in pairs(set) do if not hid[i] then hid[i] = true; k = k + 1 end end
  ed.set_hidden(it, hid)
  say("hidden: " .. k .. " vertices (y with no choice shows all)", C_ACC, 180)
end

function ed.isolate_choice()
  local it = I()
  local set, n = ed.choice_set(it)
  if n == 0 then
    if it.hid then ed.show_all() else say("choose vertices or faces first", C_ERR) end
    return
  end
  UA.push()
  ed.set_hidden(it, B.G.isolate(it, set))
  say("isolated: " .. ed.hidden_count(it) .. " vertices hidden (Shift+Y with no choice shows all)", C_ACC, 180)
end

-- Box select (z, or the left button dragged over the view): the corner where the pointer is, then the other
-- corner with the arrows; Enter chooses only what is inside, Space adds it, Esc cancels. Everything
-- inside is chosen, also what is behind. Vertices; edges with both ends inside; faces with all
-- corners inside (the mode, Tab).
function ed.box_start()
  ed.box = { x0 = ed.px, y0 = ed.py, x1 = ed.px, y1 = ed.py }
  say("box: move the pointer, Enter chooses, Space adds, Esc cancels", C_DIM, 200)
end

function ed.box_commit(add, x1, y1, box)
  local b, it = box or ed.box, I()
  ed.box = nil
  if not b or not it then return end
  x1, y1 = x1 or b.x1, y1 or b.y1
  local inside = B.G.box_pick(ed.sx, ed.sy, b.x0, b.y0, x1, y1, it.hid)
  local set = {}
  for _, i in ipairs(inside) do set[i] = true end
  if not add then ed.sv, ed.sf, ed.se, ed.order = {}, {}, {}, 0 end
  local n = 0
  if ed.mode == "v" then
    for _, i in ipairs(inside) do if not ed.sv[i] then select_v(i, true); n = n + 1 end end
  elseif ed.mode == "f" then
    for fi, f in ipairs(it.f) do
      if set[f[1]] and set[f[2]] and set[f[3]] and not ed.sf[fi] then ed.sf[fi] = true; n = n + 1 end
    end
  else
    for k in pairs(B.G.edge_faces(it)) do
      local a, c = B.G.edge_ends(k)
      if set[a] and set[c] and not ed.se[k] then ed.edge_set(it, k, true); n = n + 1 end
    end
  end
  ed.force, ed.sig = nil, ""
  say("box: " .. n .. (ed.mode == "v" and " vertices" or (ed.mode == "e" and " edges" or " faces")) .. " chosen", C_ACC, 120)
end

-- Reference pictures (Shift+R): a front and a side picture from /pics (PNG, JPEG, WebP), drawn behind the model
-- when the view looks at them (1: front, 3: side, within about 17 degrees). image_file makes each fit
-- 256 x 256 and pale, and puts it on the project's sprite sheet in two slots BELOW the sheet's own
-- rows (the sheet grows by 512 rows; a clear puts it back; the export writes only the project's
-- rows). proj.refs = { w0, h0 (the sheet before), front/side = { x, y, w, h, name },
-- hgt (height of the picture in the scene), dy, on }.
ed.REF = 256
ed.REF_MIX, ed.REF_BG = 55, 0x203040

function ed.ref_pictures()
  local out = {}
  for _, f in ipairs(ls("/pics") or {}) do
    local n = f.name:lower()
    if not f.dir and (n:match("%.png$") or n:match("%.jpe?g$") or n:match("%.webp$")) then out[#out + 1] = f.name end
  end
  table.sort(out)
  return out
end

function ed.ref_load(slot, name)
  if not image_file or not cart_sheet then say("this kernel has no image_file", C_ERR, 300); return false end
  local r = proj.refs
  local w0, h0
  if r then w0, h0 = r.w0, r.h0 else w0, h0 = cart_sheet() end
  local R = ed.REF
  cart_sheet(max(w0, R), h0 + 2 * R)
  local y = h0 + (slot == "front" and 0 or R)
  if r and r[slot] then                     -- the old picture of the slot goes
    for yy = 0, r[slot].h - 1 do for xx = 0, r[slot].w - 1 do sset(xx, y + yy, nil) end end
  end
  local im, err = image_file("/pics/" .. name, 0, y, R, R, ed.REF_MIX, ed.REF_BG)
  if not im then
    if not r then cart_sheet(w0, h0) end
    say("cannot: " .. tostring(err), C_ERR, 300)
    return false
  end
  if not r then
    local lo, hi = bounds(I())
    r = { w0 = w0, h0 = h0, hgt = max(0.5, hi[2] - lo[2]), dy = lo[2], on = true }
    proj.refs = r
  end
  r[slot] = { x = im.x, y = im.y, w = im.w, h = im.h, name = name }
  r.on = true
  say(slot .. " picture: " .. name .. " (" .. im.w .. " x " .. im.h .. "), seen from view " .. (slot == "front" and "1" or "3"), C_ACC, 300)
  return true
end

function ed.ref_clear()
  local r = proj.refs
  if not r then return end
  cart_sheet(r.w0, r.h0)
  proj.refs = nil
  if collectgarbage then collectgarbage() end
  say("reference pictures cleared", C_ACC, 200)
end

function ed.ref_pick(slot)
  local list = ed.ref_pictures()
  if #list == 0 then say("no .png, .jpg or .webp picture in /pics on the SD card", C_ERR, 300); return end
  local rows = {}
  for i, n in ipairs(list) do rows[i] = { n, function() ed.ref_load(slot, n) end } end
  choose("the " .. slot .. " picture (from /pics)", rows, 1)
end

function ed.ref_dialog(sel)
  local r = proj.refs
  local rows = {
    { "Front picture" .. (r and r.front and (": " .. r.front.name) or ""), function() ed.ref_pick("front") end },
    { "Side picture" .. (r and r.side and (": " .. r.side.name) or ""), function() ed.ref_pick("side") end },
  }
  if r then
    rows[#rows + 1] = { r.on and "Hide the pictures" or "Show the pictures", function() r.on = not r.on; ed.ref_dialog(3) end }
    rows[#rows + 1] = { "Bigger (x1.1)", function() r.hgt = min(200, r.hgt * 1.1); ed.ref_dialog(4) end }
    rows[#rows + 1] = { "Smaller (x0.9)", function() r.hgt = max(0.1, r.hgt / 1.1); ed.ref_dialog(5) end }
    rows[#rows + 1] = { "Up (0.1)", function() r.dy = r.dy + 0.1; ed.ref_dialog(6) end }
    rows[#rows + 1] = { "Down (0.1)", function() r.dy = r.dy - 0.1; ed.ref_dialog(7) end }
    rows[#rows + 1] = { "Clear the pictures", function() ed.ref_clear() end }
  end
  choose("reference pictures (never saved)", rows, sel or 1)
end

-- before the model: each picture whose view the camera has; flat on the plane Z = 0 (front) or X = 0 (side),
-- the model's feet at its bottom edge
function ed.ref_draw()
  local r = proj.refs
  if not r or not r.on then return end
  local cam = ed.cam
  for _, slot in ipairs({ "front", "side" }) do
    local pic = r[slot]
    local flip = pic and B.G.ref_flip(slot, cam.yaw, cam.pitch)
    if flip ~= nil then
      local hw = r.hgt * pic.w / pic.h / 2
      local a, b
      if slot == "front" then a, b = { -hw, r.dy + r.hgt, 0 }, { hw, r.dy, 0 }
      else a, b = { 0, r.dy + r.hgt, -hw }, { 0, r.dy, hw } end
      local ax, ay = scr(a)
      local bx, by = scr(b)
      if ax and bx and abs(by - ay) > 2 then
        sspr(pic.x, pic.y, pic.w, pic.h, min(ax, bx), min(ay, by), flip, false, abs(by - ay) / pic.h)
        ed.ref_drawn = (ed.ref_drawn or 0) + 1
      end
    end
  end
end

local COMMANDS = {
  { "Move (g)", "g" }, { "Rotate (r)", "r" }, { "Scale (t)", "t" }, { "Extrude faces (x)", "x" },
  { "Duplicate (d)", "d" }, { "Add a part (A)", "A" }, { "Mirror left-right (m)", "m" }, { "Mirror copy across 0 (M)", "M" },
  { "Live mirror X on / off (X)", "X" },
  { "Give the choice to a bone (B)", "B" }, { "Bone view: tint by bone (v)", "v" },
  { "Snap: off, grid, vertex, both (S)", "S" }, { "Snap grid step (G)", "G" },
  { "Loop cut on the chosen edge (Ctrl+R)", "^r" },
  { "Knife on two edges of a triangle (J)", "J" },
  { "Bevel the chosen edges (E)", "E" }, { "Project onto the sheet (U)", "U" },
  { "New face on 3-4 vertices (j)", "j" }, { "Merge vertices (k)", "k" }, { "Weld equal vertices (K)", "K" },
  { "Subdivide faces (u)", "u" }, { "Turn faces over (i)", "i" }, { "Paint faces (p)", "p" },
  { "Colour from the face (o)", "o" }, { "Palette (c)", "c" }, { "Delete (Del)", "del" },
  { "Smooth the chosen vertices: relax toward the neighbours (V)", "V" },
  { "All / none (a)", "a" }, { "Linked (l)", "l" }, { "Vertices / edges / faces (Tab)", "\t" },
  { "Next element (n)", "n" }, { "Frame the choice (f)", "f" }, { "Finer step (,)", "," }, { "Coarser step (.)", "." },
  { "Ghost: load a model, clear it (H)", "H" }, { "Ghost: show / hide (h)", "h" },
  { "Ghost: scale up (])", "]" }, { "Ghost: scale down ([)", "[" },
  { "Ghost: up (})", "}" }, { "Ghost: down ({)", "{" },
  { "Box select (z)", "z" }, { "Hide the choice (y)", "y" }, { "Isolate the choice (Y)", "Y" },
  { "Show all hidden", function() ed.show_all() end },
  { "Soft move: radius off, 0.25, 0.5, 1, 2 (P)", "P" },
  { "Pivot of rotate and scale: centre, origin, base (O)", "O" },
  { "Reference pictures: front, side (R)", "R" },
  { "Import a model: /bmm, /glb (Ctrl+I)", "^i" }, { "Export to /bmm (Ctrl+E)", "^e" },
}
ed.commands = COMMANDS                   -- (the context menu, outside this block)

function edit_actions()
  local rows = {}
  for _, c in ipairs(COMMANDS) do
    rows[#rows + 1] = { c[1], function()
      if type(c[2]) == "function" then c[2]()
      elseif c[2] == "^i" then lp.import_dialog() elseif c[2] == "^e" then lp.export_dialog() else edit_key(c[2]) end
    end }
  end
  choose("commands" .. (I() and (": " .. I().name) or ""), rows)
end

-- the mouse: the pointer is the mouse's (what is under it lights up); a
-- click keeps only that (Shift + click adds or takes away, as space); with
-- a tool (g r t) the left button dragged moves as the arrows, a click puts
-- it (Enter); the right button dragged turns the view, the middle one
-- moves it, the wheel zooms; in the palette a click takes the colour
function edit_mouse()
  if ed.mouse_pick then                       -- the click of before: what the drawing found under it
    local k = ed.mouse_pick
    ed.mouse_pick = nil
    edit_key(k)
  end
  if ed.pal then
    local z = U.click(0)
    if z and z.kind == "swatch" then ed.pal = z.a; edit_key("\n")
    elseif z and z.kind == "shade" then edit_key("esc") end
    return
  end
  cam_mouse(ed.cam, 0, 48, W, HINT_Y - 48)
  local view = U.inside(0, 48, W, HINT_Y - 48)
  if ed.tool then
    if U.drag_arrows(12) then return end
    if U.released(0) and view and not U.drag then edit_key("\n") end
    return
  end
  local d = U.drag
  if d and d.b == 0 and not (ed.box and not ed.box.mouse) and d.y0 >= 48 and d.y0 < HINT_Y then
    if not ed.box then ed.box = { x0 = d.x0, y0 = d.y0, mouse = true, add = U.shift() } end
    ed.box.x1, ed.box.y1 = U.x, U.y
    return
  end
  if ed.box and ed.box.mouse then              -- the button is up: the box chooses
    local b = ed.box
    ed.box = nil
    ed.box_commit(b.add, b.x1, b.y1, b)
    return
  end
  if not view then return end
  if U.moved then ed.px, ed.py, ed.force = U.x, clamp(U.y, 16, HINT_Y - 1), nil end
  if U.click(0) then
    ed.px, ed.py, ed.force = U.x, U.y, nil
    ed.mouse_pick = U.shift() and " " or "\n"     -- after the drawing finds what is under it
  end
end

function edit_key(k)
  local it = I()
  if not it then return end
  if ed.pal then palette_key(k); return end
  if ed.tool then tool_key(k); return end
  local cam = ed.cam
  if ed.box then                              -- box select: the arrows move the corner
    if k == "\n" or k == "ok" then ed.box_commit(false, ed.px, ed.py); return
    elseif k == " " then ed.box_commit(true, ed.px, ed.py); return
    elseif k == "esc" or k == "back" then ed.box = nil; say("box cancelled", C_DIM, 60); return
    elseif k ~= "left" and k ~= "right" and k ~= "up" and k ~= "down" and k ~= "home" then return end
  end
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
  elseif k == "A" then op_add_part()
  elseif k == "m" then op_mirror()
  elseif k == "M" then op_mirror_copy()
  elseif k == "X" then ed.toggle_mirror()
  elseif k == "z" then ed.box_start()
  elseif k == "y" then ed.hide_choice()
  elseif k == "Y" then ed.isolate_choice()
  elseif k == "P" then ed.cycle_prop()
  elseif k == "O" then ed.cycle_pivot()
  elseif k == "V" then ed.smooth()
  elseif k == "R" then ed.ref_dialog()
  elseif k == "B" then ed.bone_dialog()
  elseif k == "v" then ed.toggle_bone_view()
  elseif k == "h" then ed.ghost_toggle()
  elseif k == "H" then ed.ghost_dialog()
  elseif k == "[" or k == "]" or k == "{" or k == "}" then ed.ghost_nudge(k)
  elseif k == "S" then ed.cycle_snap()
  elseif k == "G" then ed.cycle_step()
  elseif k == "^r" then ed.loop_cut()
  elseif k == "J" then ed.knife()
  elseif k == "E" then ed.bevel()
  elseif k == "U" then ed.project()
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
  if btnp(4) then
    if ed.box then ed.box_commit(false, ed.px, ed.py) else op_toggle(false) end
  end
  if btnp(5) then
    if ed.box then ed.box = nil else ed.sv, ed.sf = {}, {} end
  end
  if tap[6] then edit_actions() end
end

function ed.draw_palette()
  local x0, y0 = 120, 112
  U.zone(0, 16, W, HINT_Y - 16, "shade")
  rectfill(x0 - 16, y0 - 32, 432, 160, C_PANEL)
  rect(x0 - 16, y0 - 32, 432, 160, C_ACC)
  print("colour: arrows, Enter (A)", x0, y0 - 16, C_ACC)
  for i, c in ipairs(PALETTE) do
    local x, y = x0 + ((i - 1) % 10) * 40, y0 + ((i - 1) // 10) * 32
    U.zone(x - 4, y - 4, 40, 32, "swatch", i)
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
  ed.ref_draw()
  floor_grid(cam)
  local v = view_of(it)
  if v then
    light3d(-0.4, 0.8, -0.5, 0.45)
    draw3d(v, 0, 0, 0, 0, 0, 0, 1, 0)
  end
  ed.ghost_draw()
  project_all(it)
  ed.sync_e(it)
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
  if ed.mode == "e" then
    if #it.v <= 4096 then
      for i in ipairs(it.v) do if sx[i] and not ed.sv[i] then rectfill(sx[i] - 1, sy[i] - 1, 2, 2, C_WIRE) end end
    end
    for k in pairs(ed.se) do
      local a, b = B.G.edge_ends(k)
      if sx[a] and sx[b] then
        line(sx[a], sy[a], sx[b], sy[b], C_PT)
        line(sx[a] + 1, sy[a], sx[b] + 1, sy[b], C_PT)
        line(sx[a], sy[a] + 1, sx[b], sy[b] + 1, C_PT)
      end
    end
    for i in pairs(ed.sv) do if sx[i] then rectfill(sx[i] - 2, sy[i] - 2, 5, 5, C_PT) end end
    if ed.he then
      local a, b = B.G.edge_ends(ed.he)
      if sx[a] and sx[b] then
        line(sx[a], sy[a], sx[b], sy[b], C_HOT)
        line(sx[a] + 1, sy[a], sx[b] + 1, sy[b], C_HOT)
      end
    end
  elseif ed.mode == "v" then
    if #it.v <= 4096 then
      local tint = ed.bonecol and it.vb
      for i in ipairs(it.v) do
        if sx[i] and not ed.sv[i] then rectfill(sx[i] - 1, sy[i] - 1, 2, 2, tint and ed.BONE_C[(it.vb[i] or 0) % 12 + 1] or C_WIRE) end
      end
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
    local tv = ed.tool.target and it.v[ed.tool.target]
    if tv then                                  -- the vertex the snap took
      local tx, ty = scr(tv)
      if tx then circ(tx, ty, 6, 0x40FF80); circ(tx, ty, 2, 0x40FF80) end
    end
  end
  local bx = ed.box
  if bx then                                    -- the box being drawn
    local x1, y1 = bx.x1, bx.y1
    if not bx.mouse then x1, y1 = ed.px, ed.py end
    local l, t, r, b = min(bx.x0, x1), min(bx.y0, y1), max(bx.x0, x1), max(bx.y0, y1)
    rect(l, t, r - l + 1, b - t + 1, 0xFFFFFF)
    rect(l + 1, t + 1, max(0, r - l - 1), max(0, b - t - 1), C_PT)
  end
  -- the pointer
  line(ed.px - 7, ed.py, ed.px - 3, ed.py, 0xFFFFFF)
  line(ed.px + 3, ed.py, ed.px + 7, ed.py, 0xFFFFFF)
  line(ed.px, ed.py - 7, ed.px, ed.py - 3, 0xFFFFFF)
  line(ed.px, ed.py + 3, ed.px, ed.py + 7, 0xFFFFFF)
  draw_gizmo(cam, 40, HINT_Y - 40)
  -- what it is
  rectfill(0, 16, W, 32, C_BG)
  local nsel = ed.mode == "v" and count(ed.sv) or (ed.mode == "e" and count(ed.se) or count(ed.sf))
  if ed.mirror then rectfill(W - 72, 16, 64, 8, C_BG); print("MIRROR X", W - 72, 16, C_ACC) end
  local st = {}                                 -- the tools' states, right of the second row
  if ed.snap ~= 0 then st[#st + 1] = ed.snap_text("") end
  if ed.pivot_text() then st[#st + 1] = ed.pivot_text() end
  if ed.prop > 0 then st[#st + 1] = "SOFT " .. num(ed.prop_steps[ed.prop]) end
  local nh = ed.hidden_count(it)
  if nh > 0 then st[#st + 1] = nh .. " HIDDEN" end
  if #st > 0 then local s = table.concat(st, "  "); print(s, W - 8 - #s * 8, 24, C_ACC) end
  local gw = 0                                  -- the width the ghost line takes at the right of the second row
  if ed.ghost then
    local g = ed.ghost
    local s = string.format("GHOST %s%s x%.2f", g.name:sub(1, 8), g.on and "" or " (hidden)", g.scale)
    print(s, W - 8 - #s * 8, 32, g.on and C_ACC or C_DIM)
    gw = #s * 8 + 8
  end
  print(it.name, 8, 16, KIND_C[it.kind])
  print(string.format("%d of 65535 vertices  %d triangles   %s: %d chosen", #it.v, #it.f,
                      ed.mode == "v" and "VERTICES" or (ed.mode == "e" and "EDGES" or "FACES"), nsel), 8 + (#it.name + 2) * 8, 16, C_TEXT)
  local bt = not ed.tool and ed.bone_text(it)        -- the bone of the choice, right of the second row
  if bt then print(bt, W - 8 - gw - #bt * 8, 32, C_ACC) end
  if ed.tool then
    print(tool_text(), 8, 32, C_ACC)
  else
    rectfill(8, 36, 12, 8, ed.colour)
    print(string.format("#%06X  step %s  %s", ed.colour, num(MOVE_STEPS[ed.step]), kind_text(it)), 24, 32, C_DIM)
  end
  if ed.pal then ed.draw_palette() end
  if ed.tool then
    local hl = { { { "up", "down", "left", "right" }, "change" }, { { "x", "y", "z", "n" }, "axis" }, { { "0" }, "number" }, { { "shift", "p" }, "soft" },
                 { { "enter" }, "done" }, { { "esc" }, "cancel" } }
    if ed.tool.kind ~= "move" then hl[#hl + 1] = { { "shift", "o" }, "pivot" } end
    hint(hl)
  elseif ed.box then
    hint({ { { "up", "down", "left", "right" }, "corner" }, { { "enter" }, "choose" }, { { "space" }, "add" }, { { "esc" }, "cancel" } })
  elseif ed.pal then hint({ { { "up", "down", "left", "right" }, "colour" }, { { "enter" }, "take it" }, { { "esc" }, "close" } })
  else
    hint({ { { "space" }, "choose" }, { { "a" }, "all" }, { { "tab" }, "mode" },
           { { "g" }, "move" }, { { "r" }, "rotate" }, { { "x" }, "extrude" }, { { "t" }, "scale" }, { { "shift", "v" }, "smooth" } })
  end
end
end

----------------------------------------------------------------- menu

local msel = 1
local last_page = "list"

-- the keys while F12 is held, under the system's (keyhelp(), the kernel
-- shows them): keyboard keys in lower case, the pad's buttons in upper case
local HELP = {
  all = {
    { "f1 / f2", "list / edit" },
    { "f3", "the commands" },
    { "[ / ]", "the previous / next mesh" },
    { "ctrl i", "import a model: the list of /bmm and /glb" },
    { "ctrl e", "export the mesh to /bmm" },
  },
  list = {
    { "up / down", "the mesh" },
    { "home / end", "the first / last" },
    { "enter", "edit it" },
    { "m", "copy as a model: mesh -> model" },
    { "c", "copy as code: model -> mesh_name()" },
    { "r / d", "rename / duplicate" },
    { "del", "delete" },
    { "n", "a new model (a cube; f3: plane, sphere, cylinder)" },
    { "i / x", "import a model (/bmm, /glb) / export to /bmm" },
    { "q / e", "turn the view" },
    { "w / s", "tilt" },
    { "+ / -", "zoom" },
    { "space", "spin" },
    { "f", "frame it" },
  },
  edit = {
    { "up down left right", "the pointer" },
    { "n / b", "on the next / previous one" },
    { "home", "the pointer to the middle" },
    { "space / enter", "choose (add) / only this" },
    { "a", "all, none" },
    { "tab / l", "vertices, edges or faces / all linked" },
    { "ctrl r", "loop cut: a ring of new vertices across the chosen edge (live mirror: the counterpart ring too)" },
    { "shift j", "knife: cut a triangle between the middles of 2 chosen edges" },
    { "shift e", "bevel: each chosen edge becomes a narrow strip of the width typed (live mirror: both sides)" },
    { "shift u", "project the chosen faces (or all) onto the sheet: 3 x 2 cells, by the side they look to; type cell px, x, y" },
    { "g / r / t", "move / rotate / scale" },
    { "x / d", "extrude faces / duplicate (then move)" },
    { "shift a", "add a part: cube, cylinder, sphere, plane (live mirror: and its mirror image)" },
    { "m / shift m", "mirror left-right / copy across 0" },
    { "z", "box select: the arrows move the corner, Enter chooses, Space adds (or drag the left mouse button)" },
    { "y / shift y", "hide the choice / isolate it (hide all else); with no choice they show all again" },
    { "shift o", "pivot of rotate and scale: the middle of the choice, the origin (0, 0, 0), the base (lowest point); shown as PIVOT in the header; also inside r and t; not saved" },
    { "shift v", "smooth: relax the chosen vertices halfway toward the mean of their neighbours; an open border and hidden vertices stay; live mirror: both sides; press again for more; one undo step" },
    { "shift p", "soft move: the neighbours follow a move less and less (radius off, 0.25, 0.5, 1, 2; also inside g)" },
    { "0-9 . -", "in g r t: type the exact value (axis x y z n first; Backspace)" },
    { "shift r", "reference pictures: a front and a side from /pics behind the model (views 1 and 3)" },
    { "shift x", "live mirror X on / off: move, rotate, scale, paint, delete, add a part, loop cut on both sides (not the knife)" },
    { "shift b / v", "give the choice (or a part, l) to a bone of the skeleton / tint the dots by bone" },
    { "h / shift h", "ghost reference: show / hide, load or clear (a /bmm or /glb model, never saved); the last row sets its detail (2000, 5000, 20000, all); a long wire is drawn thinned" },
    { "[ ] / { }", "ghost: scale / height (with a ghost, [ ] do not change the mesh)" },
    { "shift s / shift g", "snap: off, grid, vertex, both / the grid step (0.05 to 0.5)" },
    { "j", "new face on 3-4 vertices" },
    { "k / shift k", "merge / weld equal vertices" },
    { "u / i", "subdivide / turn over" },
    { "del", "delete" },
    { "p / o / c", "paint / take the colour / palette" },
    { "q / e", "turn the view" },
    { "w / s", "tilt" },
    { "+ / -", "zoom" },
    { "f", "frame the chosen ones" },
    { "1 / 3 / 7 / 0", "front / side / top / the view" },
    { ", / .", "the step" },
    "move, rotate, scale (g r t)",
    { "up down left right", "change" },
    { "pgup / pgdn", "nearer / farther" },
    { "x / y / z / n", "only on that axis, the normal" },
    { "enter / esc", "done / cancel" },
  },
  pad = {
    { "Y LEFTRIGHT", "page" },
    { "Y B", "menu" },
    { "Y A", "undo" },
    { "Y UPDOWN", "mesh" },
    { "X", "the commands (tap): Import, Export, Ghost" },
    { "X DPAD", "turn the view" },
    { "X A / X B", "zoom" },
    { "A", "edit (list); choose (edit)" },
    { "B", "choose none" },
    { "DPAD", "the pointer; change (tools)" },
  },
}

local function mesh_keyhelp(p)
  if not keyhelp then return end                -- a kernel before them
  local list = {}
  local function add(t) for _, e in ipairs(t) do list[#list + 1] = e end end
  add(HELP.all)
  if HELP[p] then list[#list + 1] = p; add(HELP[p])
  else list[#list + 1] = "menu"; add({ { "up / down", "choose" }, { "enter", "select" } }) end
  list[#list + 1] = "pad"
  add(HELP.pad)
  keyhelp(list, "bm Mesh")
end

go = function(p)
  if p == "menu" then if page ~= "menu" then msel = 1 end
  else last_page = p end
  page = p
  mesh_keyhelp(p)
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
  if proj.bmm then say("a model file has no game to try", C_ERR); return end
  if dirty and not save_to(proj.path) then return end
  save({ page = last_page, cur = cur })        -- the page to come back to
  cart_run(proj.path)
end

-- opened by the bm SDK on this file: the way back to it (saved first)
local function back_to_sdk()
  if not proj.path then say("open a .bm first", C_ERR); return end
  if dirty and not save_to(proj.path) then return end
  save({ page = last_page, cur = cur })
  cart_tool("sdk", proj.path)
end

local function save_as()
  if not proj.path then say("open a .bm first", C_ERR); return end
  if proj.bmm then                      -- another model file in /bmm (1 to 8 of a-z 0-9 _ -)
    ask("file name (in /bmm)", proj.path:match("([^/]+)%.%w+$") or "model", function(t)
      local stem = t:lower():gsub("%.bmm$", "")
      if not stem:match("^[a-z0-9_%-]+$") or #stem > 8 then say("a .bmm name is 1 to 8 of a-z 0-9 _ -", C_ERR); return end
      if save_to(B.BMM_DIR .. "/" .. stem .. ".bmm") then go("list") end
    end)
    return
  end
  ask("file name (8.3, in /bme)", short_path(proj.path):match("([^/]+)$") or "meshes.bme", function(t)
    local to = U.project_path(t)
    local from = proj.path
    code_dirty = true                    -- the new file gets all the code
    if save_to(to, from) then go("list") end
  end)
end

local function open_other()
  if not dirty or confirmed("open", "unsaved changes: choose again to open another file") then open_chooser() end
end

-- the assistant's model (F6, pad Y + X): a new model; its skeleton stays
-- out (bm Studio and bm Animator take it with the skeleton)
local function take_model(m)
  if not m or not m.faces or #m.faces == 0 then return end
  if not proj.path then say("open a .bm first: Esc > Open", C_ERR); return end
  local it = { kind = "model", name = unique_name("model", m.gen or m.name or "model"), v = {}, f = {}, dirty = true }
  local at = {}
  local function vi(p)
    local key = string.format("%.4f %.4f %.4f", p[1], p[2], p[3])
    if not at[key] then it.v[#it.v + 1] = { p[1], p[2], p[3] }; at[key] = #it.v end
    return at[key]
  end
  for _, f in ipairs(m.faces) do
    for k = 2, #f.p - 1 do it.f[#it.f + 1] = { vi(f.p[1]), vi(f.p[k]), vi(f.p[k + 1]), f.c } end
  end
  dirty = true
  lp.select(insert_item(it))
  go("list")
  say(string.format("the assistant's %s: model %s, %d faces (F2 edits it)", m.name or m.gen or "model", it.name, #it.f),
      C_ACC, 300)
end

local function assistant()
  if not ok_assist or not ai or not ai.mesh then say("the assistant is not here", C_ERR); return end
  assist.open{ mode = "mesh", on_mesh = take_model }
end

-- the models of /bmm and /glb (B.import_flow, B.export_item: bm3d.lua): a .glb is converted to
-- /bmm first; the item comes in as a model of the project (its texture stays in the file: the
-- project has its own sheet, so its textured faces turn flat grey); export writes /bmm/NAME.bmm
do
local mesh_ui = {
  choose = function(title, rows, sel) choose(title, rows, sel) end,
  ask = function(label, text, done) ask(label, text, done) end,
  say = function(s, err) say(s, err and C_ERR or C_ACC, 400) end,
}

local function take_imported(it, note)
  local n = 0
  for _, x in ipairs(items) do if x.kind == "model" then n = n + 1 end end
  if n >= LIMIT_MODELS then say("at most " .. LIMIT_MODELS .. " models", C_ERR); return false end
  local flat = 0
  for _, f in ipairs(it.f) do
    if f[4] == -1 then f[4], f[5] = 0x808080, nil; flat = flat + 1 end
  end
  it.kind, it.name, it.dirty, it.mc, it.mc_name = "model", unique_name("model", it.name), true, nil, nil
  it.view = nil
  dirty = true
  lp.select(insert_item(it))
  go("edit")
  say(note .. (flat > 0 and (", " .. flat .. " textured faces flat grey") or "") .. ": model " .. it.name ..
      "  (Ctrl+S saves it in the project)", C_ACC, 400)
  return true
end

lp.import_dialog = function()
  if not proj.path then say("open a .bm first: Esc > Open", C_ERR); return false end
  return B.import_flow(mesh_ui, take_imported)
end

lp.export_dialog = function()
  local it = I()
  if not it or #it.f == 0 then say("no mesh with faces to export", C_ERR); return false end
  local item = { name = it.name, v = it.v, f = it.f, ac = encode_rig(it), inset = inset / 256 }
  for _, f in ipairs(it.f) do
    if f[4] == -1 then
      local data = B.sheet_section(proj.refs and proj.refs.h0 or nil)
      if data then item.sheet = { type = 2, data = data } end
      break
    end
  end
  return B.export_item(mesh_ui, item)
end
end

local MENU = {
  { "Continue", function() go(last_page) end },
  { "Open...   (Ctrl+O)", open_other },
  { "Save   (Ctrl+S)", function() save_to(proj.path) end },
  { "Save as...   (Ctrl+Shift+S)", save_as },
  { "Try the game (F5)", try_game },
  { "Import a model...   (Ctrl+I)", function() if lp.import_dialog() then go("list") end end },
  { "Export to /bmm...   (Ctrl+E)", function() lp.export_dialog() end },
  { B.LY.label(), B.LY.switch },
  { "Exit bm Mesh", function() if not dirty or confirmed("exit", "unsaved changes: choose again to exit") then quit() end end },
}

B.screen_row = MENU[#MENU - 1]

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
    U.zone(24, y, 272, 16, "menuitem", i)
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
  chip_hint("f12", nil, "held: the keys", x, 176)
  print("the models: as bm Studio and bm", x, 208, C_DIM)
  print("Animator (skeletons kept); the", x, 224, C_DIM)
  print("code: as bm Studio's Lua, for bm Code", x, 240, C_DIM)
  hint({ { { "up", "down" }, "choose" }, { { "enter" }, "select" }, { { "esc" }, "back" } })
end

----------------------------------------------------------------- main

function _init()
  keyp()                                 -- typing on: the keyboard types
  U.wants()                              -- the pointer, when there is a mouse
  local a = cart_arg()
  if a and a.from == "sdk" then table.insert(MENU, #MENU, { "Back to bm SDK", back_to_sdk }) end
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
  elseif k == "esc" and page ~= "menu" and not (page == "edit" and (ed.tool or ed.pal or ed.box)) then go("menu"); return true
  -- the system's keys (the kernel's syskeys.c)
  elseif k == "^s" then save_to(proj.path); return true
  elseif k == "^S" then save_as(); return true
  elseif k == "^o" and not ed.tool then open_other(); return true
  elseif k == "^n" and not ed.tool then new_shape("cube"); return true
  elseif k == "^i" and not ed.tool then lp.import_dialog(); return true
  elseif k == "^e" and not ed.tool then lp.export_dialog(); return true
  elseif k == "f6" and not ed.tool then assistant(); return true
  elseif k == "f5" or (k == "^r" and not (page == "edit" and not ed.tool and not ed.pal)) then try_game(); return true
  elseif k == "^z" or k == "^y" then
    if ed.tool then edit_key("esc")            -- undo in a tool: the tool goes
    elseif k == "^z" then do_undo(undo, redo, "undo") else do_undo(redo, undo, "redo") end
    return true
  elseif (k == "[" or k == "]") and page ~= "menu" and #items > 0 and not ed.tool
         and not (page == "edit" and ed.ghost and not ed.pal) then
    cur = (cur - 1 + (k == "]" and 1 or -1)) % #items + 1
    reset_pages()
    say(I().name, C_ACC, 60)
    return true
  end
  return false
end

local PAGES = { "list", "edit", "menu" }

-- Ctrl+Esc or PS (the system's keys): back to bm's menu; with unsaved
-- changes it asks first, and the same again leaves without saving
function _exit()
  return not dirty or confirmed("exit", "unsaved changes: Ctrl+Esc again leaves without saving")
end

-- the mouse, each frame: the chips and the tabs press their keys, the
-- dialogs choose, the page does its own, the right button opens the
-- context menu (the page's keys)
local function mouse_frame()
  if not U.on then return end
  if U.keys() then return end
  if dialog_mouse() then return end
  if page == "list" then list_mouse()
  elseif page == "edit" then edit_mouse()
  else
    if U.wheel ~= 0 then msel = clamp(msel - U.wheel, 1, #MENU) end
    local z = U.click(0)
    if z and z.kind == "menuitem" then msel = z.a; U.press("\n") end
  end
  if not U.clicked(1) or page == "menu" then return end
  local list = {}
  if page == "list" then
    list = { { "Edit", "\n" }, { "Copy as a model", "m" }, { "Copy as code", "c" }, { "Rename...", "r" },
             { "Duplicate", "d" }, { "Delete (twice)", "del" }, { "New cube", "n" } }
  elseif ed.tool then
    list = { { "Put it", "\n" }, { "Cancel", "esc" }, { "Along x", "x" }, { "Along y", "y" }, { "Along z", "z" },
             { "Finer step", "," }, { "Coarser step", "." } }
  elseif not ed.pal then
    for _, c in ipairs(ed.commands) do list[#list + 1] = { (c[1]:gsub(" %(.-%)$", "")), c[2] } end
  end
  if #list > 0 then list[#list + 1] = "-" end
  for _, it in ipairs({ { "Undo", "^z" }, { "Save", "^s" }, { "Try the game", "f5" }, { "Assistant", "f6" },
                        { "Menu", "esc" } }) do list[#list + 1] = it end
  U.menu(list)
end

function _update()
  B.LY.poll()
  frame = frame + 1
  if msg_t > 0 then msg_t = msg_t - 1 end
  if confirm_t > 0 then confirm_t = confirm_t - 1 end
  read_pad()
  U.update()
  if ok_assist and assist.update() then return end   -- the assistant has the keys
  if U.menu_update() then return end                -- the context menu has the input
  mouse_frame()

  while true do
    local k = keyp()
    if not k then break end
    if input then input_key(k)
    elseif pick then pick_key(k)
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
    elseif btnp(6) then assistant()
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
  B.LY.poll()
  U.begin()
  if page == "list" then draw_list_page()
  elseif page == "edit" then draw_edit()
  else draw_menu() end

  -- tab bar: each page with its key
  rectfill(0, 0, W, 16, C_BAR)
  local tabs = { { "list", "f1", "list" }, { "edit", "f2", "edit" }, { "menu", "esc", "menu" } }
  local x = U.bar_title("bm Mesh", C_ACC)       -- the name first, at the left
  for _, t in ipairs(tabs) do
    local lx = snap(x + prompt(t[2]) + 3)
    local w = lx + #t[3] * 8 - x
    if t[1] == page then rectfill(x - 4, 0, w + 8, 16, C_SEL) end
    U.key_zone(x - 4, 0, w + 8, 16, t[2])          -- a click: the page
    prompt(t[2], x, 0)
    print(t[3], lx, 0, t[1] == page and 0xFFFFFF or C_DIM)
    x = x + w + 20
  end
  U.bar_file(proj.path or "no file", dirty, x, W, C_ACC, C_DIM)

  -- status bar
  rectfill(0, STATUS_Y, W, H - STATUS_Y, C_BAR)
  local status
  if msg_t > 0 and msg then status = msg
  elseif page == "menu" then status = "up/down choose, Enter select"
  else status = I() and ("mesh " .. cur .. "/" .. #items .. ": " .. I().name .. " (" .. KIND_L[I().kind] .. ")") or "" end
  print(status:sub(1, W // 8 - 1), 0, STATUS_Y, (msg_t > 0 and msg_c) or C_TEXT)
  if pick then draw_pick() end
  if input then draw_input() end
  if ok_assist then assist.draw() end    -- the assistant's panel on top, if open
  U.menu_draw()                          -- the context menu over everything
end
