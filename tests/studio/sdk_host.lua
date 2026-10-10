-- The bm SDK on the console (carts/editor) on the PC: stand-ins for the bm
-- API (a folder is the SD card, the sections are strings, the map and the
-- sheet are tables, nothing is drawn but the text is kept), keys typed into
-- it, and checks on what it shows, writes and asks of the kernel: the
-- project page and the dev kit, the templates (each one's game run for a
-- while on stand-ins), the code, sprite, map and 3D pages, the assistant's
-- guides and models, the tools of the suite opened on the project, the
-- try and the dev kit's numbers it brings back.
--
--   luahost tests/studio/sdk_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/carts/village.bm must be there (a copy of build/carts/village.bm).

local ROOT, SD = arg[1] or ".", arg[2] or "build/sdk-sd"
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

----------------------------------------------------------------- .bm files

local function crc32(s)
  local crc = 0xFFFFFFFF
  for i = 1, #s do
    crc = crc ~ s:byte(i)
    for _ = 1, 8 do crc = (crc >> 1) ~ (0xEDB88320 & -(crc & 1)) end
  end
  return (~crc) & 0xFFFFFFFF
end

local function read_file(p)
  local f = io.open(p, "rb")
  if not f then return nil end
  local d = f:read("a")
  f:close()
  return d
end

local function sections_of(data)
  local out = {}
  local n = data:byte(18)
  for i = 0, n - 1 do
    local t, off, size = string.unpack("<I4I4I4", data, 129 + i * 16)
    out[#out + 1] = { t, data:sub(off + 1, off + size) }
  end
  return out
end

local function pack_cart(title, author, res, secs)
  local tab, body = {}, {}
  local off = 128 + 16 * #secs
  local pos = off
  for _, s in ipairs(secs) do
    tab[#tab + 1] = string.pack("<I4I4I4I4", s[1], pos, #s[2], 0)
    local pad = (4 - #s[2] % 4) % 4
    body[#body + 1] = s[2] .. string.rep("\0", pad)
    pos = pos + #s[2] + pad
  end
  local after = table.concat(tab) .. table.concat(body)
  local w = res == "320x180" and 320 or 640
  local h = w == 320 and 180 or 360
  local head = "BMCART\0\0" .. string.pack("<I2I2I2I2BBI2I4", 1, 128, w, h, 1, #secs, 0, crc32(after))
  head = head .. (title:sub(1, 47) .. string.rep("\0", 48)):sub(1, 48) .. (author:sub(1, 31) .. string.rep("\0", 32)):sub(1, 32)
  head = head .. string.rep("\0", 128 - #head)
  return head .. after
end

-- the sheet of a .bm (SHEET or SHEET8): w, h, { [y * w + x] = 0xRRGGBB }
local function decode_sheet(data)
  for _, s in ipairs(sections_of(data)) do
    if s[1] == 2 then
      local w, h = string.unpack("<I2I2", s[2])
      local px = {}
      for i = 0, w * h - 1 do
        local r, g, b, a = s[2]:byte(5 + i * 4, 8 + i * 4)
        if a >= 128 then px[i] = r << 16 | g << 8 | b end
      end
      return w, h, px
    elseif s[1] == 5 then
      local b = s[2]
      local w, h, nc = string.unpack("<I2I2I2", b)
      local pal = {}
      for i = 0, nc - 1 do
        local r, g, bb, a = b:byte(9 + i * 4, 12 + i * 4)
        pal[i] = a >= 128 and (r << 16 | g << 8 | bb) or false
      end
      local px, i, q = {}, 0, 9 + nc * 4
      while i < w * h do
        local t = b:byte(q)
        q = q + 1
        if t < 128 then
          for k = 0, t do px[i] = pal[b:byte(q + k)] or nil; i = i + 1 end
          q = q + t + 1
        else
          local c = pal[b:byte(q)]
          for _ = 1, t - 126 do px[i] = c or nil; i = i + 1 end
          q = q + 1
        end
      end
      return w, h, px
    end
  end
  return 256, 256, {}
end

----------------------------------------------------------------- the sections as the kernel reads them

local function mesh_models(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nv, nf = string.unpack("<I2I2", bin, pos + 16)
    local verts, cols = {}, {}
    for i = 1, nv do verts[i] = { string.unpack("<fff", bin, pos + 24 + (i - 1) * 12) } end
    for i = 1, nf do
      local f = pos + 24 + nv * 12 + (i - 1) * 24
      local a, b, c, _, col = string.unpack("<I2I2I2I2I4", bin, f)
      assert(a < nv and b < nv and c < nv, "MESH: a vertex out of range")
      cols[i] = col
    end
    out[#out + 1] = { name = name, nv = nv, nf = nf, verts = verts, cols = cols }
    pos = pos + 24 + nv * 12 + nf * 24
  end
  assert(pos == #bin + 1, "MESH: the size does not add up")
  return out
end

local function anim_rigs(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nb, nc, nv = string.unpack("<I2I2I2", bin, pos + 16)
    local r = { nb = nb, nv = nv, bones = {}, clips = {} }
    for i = 1, nb do
      local b = pos + 24 + (i - 1) * 44
      local parent, _, hx, hy, hz, tx, ty, tz = string.unpack("<i2I2ffffff", bin, b + 16)
      assert(parent >= -1 and parent < i - 1, "ANIM: a parent after its child")
      r.bones[i] = { name = bin:sub(b, b + 15):match("^[^\0]*"), parent = parent + 1, head = { hx, hy, hz }, tail = { tx, ty, tz } }
    end
    for i = 1, nv do assert(bin:byte(pos + 24 + nb * 44 + i - 1) < nb, "ANIM: a vertex of no bone") end
    local p = pos + 24 + nb * 44 + ((nv + 3) & ~3)
    for c = 1, nc do
      local nk, mode, flags, length = string.unpack("<I2BBf", bin, p + 16)
      assert(nk >= 1 and mode <= 2 and length > 0, "ANIM: a bad clip")
      local prev, keys = 0, {}
      for k = 0, nk - 1 do
        local t = string.unpack("<f", bin, p + 24 + k * (4 + nb * 28))
        assert(t >= prev and t <= length + 1e-4, "ANIM: keys out of order")
        prev = t
        keys[k + 1] = t
      end
      r.clips[c] = { name = bin:sub(p, p + 15):match("^[^\0]*"), length = length, loop = flags & 1 == 1, keys = nk,
                     times = keys, mode = mode }
      p = p + 24 + nk * (4 + nb * 28)
    end
    out[name] = r
    pos = p
  end
  assert(pos == #bin + 1, "ANIM: the size does not add up")
  return out
end

----------------------------------------------------------------- the SD card and the project

local on_sd = { ["/carts"] = { "village.bm" } }
local function host(path) return SD .. path:lower() end
local sec = {}                -- the project's MESH (8) and ANIM (9)
local sheet = { w = 256, h = 256, px = {} }
local mapc = {}               -- the map: [y * 4096 + x] = cell (layer 1)
local lnames, mlay, flg = { "main" }, {}, {}   -- the layers' names, layers 2.., the tiles' flags
local ran, tooled, saves, last_save, built = nil, nil, 0, nil, nil

local function add_file(path)
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  for _, n in ipairs(list) do if n == name:lower() then return end end
  list[#list + 1] = name:lower()
end

----------------------------------------------------------------- the SDK on the "console"

local E, texts, sel_rows, keyq, pad, padprev, frame, saved_t, quitted, draws, arg_t
local keyhelp_list, keyhelp_title
local MESH_MT = {}
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end
local PADN = { A = 1, B = 1, X = 1, Y = 1, START = 1, SELECT = 1, L1 = 1, R1 = 1, UP = 1, UPDOWN = 1, LEFTRIGHT = 1,
               DPAD = 1, LSTICK = 1, PS = 1 }
local loaded                  -- the libraries, loaded once (as require does)

-- the assistant's knowledge, standing in for the kernel's `ai`: two guides,
-- a how-to and one 3D recipe (a cube with a bone and an animation)
local ENTRIES = {
  ["guide.platformer"] = { id = "guide.platformer", kind = "guide", title = "Un platform 2D con l'SDK", name = "",
                           text = "1. Ctrl+N, Platform 2D. 2. F3 disegna la mappa.", gen = "", see = {},
                           code = "-- platform\nlocal vy = 0\n" },
  ["guide.3d_first"] = { id = "guide.3d_first", kind = "guide", title = "Il primo gioco 3D con l'SDK", name = "",
                         text = "camera3d, zclear, draw3d.", gen = "", see = {}, code = "zclear()\n" },
  ["jump_gravity"] = { id = "jump_gravity", kind = "howto", title = "Saltare con la gravita'", name = "",
                       text = "vy aumenta.", gen = "", see = {}, code = "vy = vy + 0.3\n" },
  ["mesh.cube"] = { id = "mesh.cube", kind = "mesh", title = "Cubo (un blocco)", name = "", code = "",
                    text = "Un blocco di un'unita'.", gen = "cube", see = {} },
}
local function entries_of(kinds)
  local out = {}
  for _, id in ipairs({ "guide.platformer", "guide.3d_first", "jump_gravity", "mesh.cube" }) do
    local e = ENTRIES[id]
    if not kinds or kinds:find(e.kind, 1, true) then out[#out + 1] = { id = id, title = e.title, kind = e.kind } end
  end
  return out
end

local MS
local function new_env(arg)
  E = {}
  texts, sel_rows, keyq, pad, padprev, frame, quitted, draws = {}, {}, {}, {}, {}, 0, false, 0
  ran, tooled, arg_t, loaded = nil, nil, arg, {}
  for k, v in pairs(_G) do E[k] = v end
  E.SCREEN_W, E.SCREEN_H = 640, 360
  local function nop() end
  local function numbers(name, n)
    return function(...)
      for i = 1, n do
        assert(type(select(i, ...)) == "number", name .. ": argument " .. i .. " is not a number")
      end
    end
  end
  E.rectfill = function(x, y, w, h, c)
    numbers("rectfill", 4)(x, y, w, h)
    if c == 0x3050A0 then sel_rows[#sel_rows + 1] = y end
  end
  for n, k in pairs({ rect = 4, line = 4, circ = 3, circfill = 3, tri = 6, sspr = 6, pset = 2, spr = 3, map = 2 }) do
    E[n] = numbers(n, k)
  end
  for _, n in ipairs({ "cls", "zclear", "light3d", "fog3d", "lamp3d", "camera", "clip", "sky3d" }) do E[n] = nop end
  E.print = function(s, x, y) texts[#texts + 1] = { tostring(s), x or 0, y or 0 }; return (x or 0) + #tostring(s) * 8 end
  E.prompt = function(n, x, y)
    assert(type(n) == "string", "prompt: a name")
    assert(PADN[n] or n == n:lower(), "prompt: not a button or a key: " .. n)
    assert(not n:find(" "), "prompt: one key at a time: " .. n)
    if type(x) ~= "number" then return chip_w(n), 16 end
    texts[#texts + 1] = { "[" .. n .. "]", x, y }
    return x + chip_w(n)
  end
  E.font = function() return 8, 16 end
  E.lastinput = function() return nil end
  E.keydown = function() return false end
  E.time = function() return frame / 60 end
  E.stat = function() return 0 end
  E.log = function(...) io.write(table.concat({ ... }, "\t"), "\n") end
  E.quit = function() quitted = true end
  E.btn = function(i) return pad[i] == true end
  E.btnp = function(i) return pad[i] == true and not padprev[i] end
  E.keyp = function() return table.remove(keyq, 1) end
  E.keyheld = function() return false end
  -- the mouse (bmui): MS as a USB mouse would give it
  MS = { x = 0, y = 0, b = 0, w = 0, p = 0 }
  E.mouse = function(on) if on ~= nil then return true end; return MS.x, MS.y, MS.b, MS.w, true end
  E.mousep = function(i) return MS.p >> (i or 0) & 1 == 1 end
  E.keyhelp = function(list, title)
    for _, e in ipairs(list) do
      if type(e) == "table" then
        assert(not e[1]:find("f11") and not e[1]:find("f12"), "keyhelp: a key the kernel keeps: " .. e[1])
      end
    end
    keyhelp_list, keyhelp_title = list, title
    return 0
  end
  E.save = function(t) saved_t = t; return true end
  E.saved = function() return saved_t end
  E.cart_arg = function() return arg_t end
  E.cart_run = function(p) ran = p end
  E.cart_tool = function(name, p) tooled = { name, p } end
  E.timeslice = function() end
  -- (the real count is the kernel's, tried by tests/bm/test_tokens.c)
  E.code_tokens = function(s)
    local n = 0
    for _ in s:gmatch("[%w_]+") do n = n + 1 end
    return n
  end
  E.camera3d = nop
  E.project3d = function(x, y, z) return 400 + x * 10, 180 - y * 10, 5 end
  E.sget = function(x, y)
    if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return nil end
    return sheet.px[y * sheet.w + x]
  end
  E.sset = function(x, y, c)
    assert(math.type(x) == "integer" and math.type(y) == "integer", "sset: whole pixels")
    if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return end
    sheet.px[y * sheet.w + x] = c
  end
  -- the map's layers and the tiles' flags (R11), as the kernel's
  local function lay(l)
    if l == nil or l == 1 or l == lnames[1] then return mapc end
    if type(l) == "string" then
      for i, n in ipairs(lnames) do if n == l then return mlay[i] end end
      error("the map has no layer \"" .. l .. "\"")
    end
    return assert(mlay[l], "the map has no layer " .. tostring(l))
  end
  E.mget = function(x, y, l)
    assert(math.type(x) == "integer" or x == math.floor(x), "mget: a cell")
    return lay(l)[math.floor(y) * 4096 + math.floor(x)] or 0
  end
  E.mset = function(x, y, v, l)
    assert(type(v) == "number", "mset: a number")
    lay(l)[y * 4096 + x] = v ~= 0 and v or nil
  end
  E.mlayers = function(list)
    if list then
      assert(#list >= 1 and #list <= 8, "mlayers: 1 to 8 layers")
      local old = {}
      for i, n in ipairs(lnames) do old[n] = i == 1 and mapc or mlay[i] end
      local nn, nl = {}, {}
      for i, e in ipairs(list) do
        local name, from = e, e
        if type(e) == "table" then name, from = e[1], e[2] end
        nn[i], nl[i] = name, (type(from) == "string" and old[from]) or {}
      end
      lnames, mlay, mapc = nn, nl, nl[1]
    end
    return table.move(lnames, 1, #lnames, 1, {})
  end
  E.msize = function() return 256, 256, #lnames end
  E.fget = function(n, f)
    local v = flg[n] or 0
    if f == nil then return v end
    return v >> f & 1 == 1
  end
  E.fset = function(n, f, on)
    if on == nil then flg[n] = f & 255; return end
    local bit = 1 << f
    flg[n] = on and (flg[n] or 0) | bit or (flg[n] or 0) & ~bit
  end
  E.mflags = function(x, y, w, h, l)
    local c0, r0 = math.floor(x / 8), math.floor(y / 8)
    local c1 = (w or 0) > 0 and math.ceil((x + w) / 8) - 1 or c0
    local r1 = (h or 0) > 0 and math.ceil((y + h) / 8) - 1 or r0
    local out = 0
    for r = math.max(r0, 0), math.min(r1, 255) do
      for c = math.max(c0, 0), math.min(c1, 255) do
        local n = lay(l)[r * 4096 + c] or 0
        if n > 0 then out = out | (flg[n] or 0) end
      end
    end
    return out
  end
  E.zones = function() return {} end
  E.zone = function() return nil end
  E.zspr = function(name) error("the sheet has no sprite zone \"" .. tostring(name) .. "\"") end
  E.zboxes = E.zspr
  -- the players and the network of the templates (versus, online): one
  -- player with the keyboard, no network
  E.controller = function(p)
    p = p or 1
    return { kind = p == 1 and "keyboard" or "none", layout = p == 1 and "keyboard" or "none", bluetooth = false,
             ok = "a", back = "b", color = ({ 0x3070FF, 0xFF3C28, 0x28D848, 0xFF38A8 })[p] }
  end
  E.players = function() return 1, 1 end
  E.pad = function() return 0 end
  E.stick = function() return 0, 0 end
  E.net_ip = function() return nil end
  E.udp_open = function() return nil, "no network" end
  E.online = function() return false end
  E.note, E.noteoff = nop, nop
  E.SQUARE, E.TRIANGLE, E.SAW, E.NOISE, E.SINE = 0, 1, 2, 3, 4
  E.cart_data = function(t, ...)
    assert(t == 8 or t == 9, "cart_data: 8 (MESH) or 9 (ANIM)")
    if select("#", ...) == 0 then return sec[t] end
    local data = ...
    if data == "" then data = nil end
    if data then
      local ok, e = pcall(t == 8 and mesh_models or anim_rigs, data)
      if not ok then return false, "broken: " .. tostring(e) end
    end
    sec[t] = data
    return true
  end
  E.model = function(name)
    for i, m in ipairs(mesh_models(sec[8])) do
      if m.name == name or i == name then
        local r = anim_rigs(sec[9])[m.name]
        if r and r.nv ~= m.nv then r = nil end
        return setmetatable({ model = m, rig = r }, MESH_MT)
      end
    end
    return nil
  end
  E.models = function()
    local out = {}
    for i, m in ipairs(mesh_models(sec[8])) do out[i] = m.name end
    return out
  end
  E.mesh = function(v, f) assert(#v % 3 == 0 and #f % 4 == 0, "mesh: v and f"); return setmetatable({ v = v, f = f }, MESH_MT) end
  E.mesh_cube = function() return setmetatable({ v = {}, f = {} }, MESH_MT) end
  E.mesh_sphere = E.mesh_cube
  E.draw3d = function(m) assert(getmetatable(m) == MESH_MT, "draw3d: a mesh"); draws = draws + 1 end
  E.clips = function(m)
    local out = {}
    for i, c in ipairs(m.rig and m.rig.clips or {}) do out[i] = { name = c.name, length = c.length, loop = c.loop } end
    return out
  end
  E.animate = function(m, c, t)
    assert(m.rig, "this mesh has no skeleton (make one with bm Animator)")
    if c then
      local found = false
      for _, x in ipairs(m.rig.clips) do if x.name == c then found = true end end
      assert(found, "no animation " .. tostring(c))
    end
    return 0
  end
  E.bounds3d = function(m)
    if not m.model then return -0.5, -0.5, -0.5, 0.5, 0.5, 0.5 end
    local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
    for _, p in ipairs(m.model.verts) do
      for k = 1, 3 do lo[k] = math.min(lo[k], p[k]); hi[k] = math.max(hi[k], p[k]) end
    end
    return lo[1], lo[2], lo[3], hi[1], hi[2], hi[3]
  end
  E.ls = function(dir)
    local out = {}
    for _, n in ipairs(on_sd[dir] or {}) do
      local d = read_file(host(dir .. "/" .. n))
      out[#out + 1] = { name = n, size = d and #d or 0, dir = false }
    end
    return out
  end
  E.cart_load = function(path)
    local data = read_file(host(path))
    if not data then return nil, "no such file" end
    sec, mapc, lnames, mlay, flg = {}, {}, { "main" }, {}, {}
    local lua = ""
    for _, s in ipairs(sections_of(data)) do
      local t = s[1]
      if t == 6 and s[2]:sub(1, 4) ~= "BMAU" then t = 8 elseif t == 7 then t = 9 end
      if t == 8 or t == 9 then sec[t] = s[2] end
      if t == 1 then lua = s[2] end
      if t == 3 then
        local w = string.unpack("<I2", s[2])
        for i = 0, (#s[2] - 4) // 2 - 1 do
          local v = string.unpack("<I2", s[2], 5 + i * 2)
          if v ~= 0 then mapc[(i // w) * 4096 + i % w] = v end
        end
      end
    end
    local w, h, px = decode_sheet(data)
    sheet = { w = w, h = h, px = px }
    return { title = data:sub(25, 72):match("^[^\0]*"), author = data:sub(73, 104):match("^[^\0]*"),
             res = string.unpack("<I2", data, 13) == 320 and "320x180" or "640x360", lua = lua,
             sheet_w = w, sheet_h = h, map_w = 256, map_h = 256 }
  end
  E.cart_new = function()
    sec, mapc, sheet, lnames, mlay, flg = {}, {}, { w = 256, h = 256, px = {} }, { "main" }, {}, {}
  end
  -- cart_save as the kernel's: the code, the sheet, the map, the models; a
  -- game (.bm) is read only, the save goes into its editable copy (.BME)
  local project_target = dofile(ROOT .. "/tests/studio/project_rules.lua")
  local function exists(p) local f = io.open(host(p), "rb") if f then f:close() end return f ~= nil end
  E.cart_save = function(path, t)
    assert(type(t.lua) == "string", "cart_save: the code")
    local moved
    path, moved = project_target(path, exists, function(a, b)
      local fi, fo = assert(io.open(host(a), "rb")), assert(io.open(host(b), "wb"))
      fo:write(fi:read("a"))
      fi:close()
      fo:close()
      add_file(b)
    end)
    assert(path:match("^/carts/[%u%d_]+%.BME$"), "cart_save: a project's 8.3 name in /carts: " .. path)
    local px = {}
    for i = 0, sheet.w * sheet.h - 1 do
      local c = sheet.px[i]
      px[#px + 1] = c and string.char(c >> 16 & 255, c >> 8 & 255, c & 255, 255) or "\0\0\0\0"
    end
    local cells = { string.pack("<I2I2", 256, 256) }
    for i = 0, 256 * 256 - 1 do cells[#cells + 1] = string.pack("<I2", mapc[(i // 256) * 4096 + i % 256] or 0) end
    local secs = { { 1, t.lua }, { 2, string.pack("<I2I2", sheet.w, sheet.h) .. table.concat(px) },
                   { 3, table.concat(cells) } }
    if sec[8] then secs[#secs + 1] = { 8, sec[8] } end
    if sec[9] then secs[#secs + 1] = { 9, sec[9] } end
    local f = assert(io.open(host(path), "wb"))
    f:write(pack_cart(t.title or "", t.author or "", t.res or "640x360", secs))
    f:close()
    add_file(path)
    saves, last_save = saves + 1, { path = path, t = t }
    if moved then return true, path end
    return true
  end
  -- cart_build: the project's game next to it
  E.cart_build = function(path)
    if not path:match("%.BME$") then return false, "only a project (.bme) is built" end
    local game = path:gsub("%.BME$", ".BM")
    local fi, fo = assert(io.open(host(path), "rb")), assert(io.open(host(game), "wb"))
    fo:write(fi:read("a"))
    fi:close()
    fo:close()
    add_file(game)
    built = game
    return true, game
  end
  E.cart_audio = function() return false end
  E.ai = {
    list = function(kinds) return entries_of(kinds) end,
    ask = function(q, o)
      local hits = {}
      for _, e in ipairs(entries_of(o and o.kinds)) do hits[#hits + 1] = { id = e.id, title = e.title, kind = e.kind, score = 0.9 } end
      return hits, 40
    end,
    entry = function(id) return ENTRIES[id] end,
    near = function() return nil end,
    sprite = function(req, o)
      local n = o and o.size or 16
      local s = { w = n, h = n, name = "slime", gen = "slime", px = {} }
      for i = 1, n * n do s.px[i] = (i % 3 == 0) and -1 or 0x30C040 end
      return s
    end,
    mesh = function(q, o)
      local faces = {}
      local function quad(a, b, c, d, col) faces[#faces + 1] = { p = { a, b, c, d }, c = col, b = { 1, 1, 1, 1 } } end
      quad({ 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, 0xD83A3A)
      quad({ 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 }, 0xD83A3A)
      quad({ 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, 0xF07070)
      return { gen = "cube", name = "cube", seed = o and o.seed or 1, faces = faces,
               bones = { { name = "root", parent = 0, head = { 0.5, 0, 0.5 }, tail = { 0.5, 1, 0.5 } } },
               clips = { { name = "idle", loop = true, length = 1, mode = 1,
                           keys = { { t = 0, pose = { { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } } },
                                    { t = 0.5, pose = { { q = { 0, 0, 0, 1 }, t = { 0, 0.1, 0 } } } } } } } }
    end,
  }
  E.require = function(name)
    if loaded[name] then return loaded[name] end
    local file = name == "assist" and "/src/ai/assist.lua" or name == "bm3d" and "/src/script/bm3d.lua" or
                 name == "bmlib" and "/src/script/bmlib.lua" or name == "bmnet" and "/src/script/bmnet.lua" or
                 name == "bmui" and "/src/script/bmui.lua"
    if not file then error("require: no " .. name .. " here") end
    loaded[name] = assert(loadfile(ROOT .. file, "t", E))()
    return loaded[name]
  end
end

local function run_sdk(arg)
  new_env(arg)
  assert(loadfile(ROOT .. "/carts/editor/main.lua", "t", E))()
  E._init()
end

local function screen()
  local lines = {}
  for _, t in ipairs(texts) do lines[#lines + 1] = t[1] end
  return table.concat(lines, "\n")
end
local function sees(s) return screen():find(s, 1, true) ~= nil end

local function frames(n)
  for _ = 1, n or 1 do
    frame = frame + 1
    texts, sel_rows = {}, {}
    E._update()
    E._draw()
    for i = 0, 7 do padprev[i] = pad[i] end
  end
end

local function key(...)
  for _, k in ipairs({ ... }) do
    keyq[#keyq + 1] = k
    frames(1)
  end
end

local function typed(s) for c in s:gmatch(".") do key(c) end end

-- the status bar (y = 336)
local function status()
  local out = {}
  for _, t in ipairs(texts) do if t[3] == 336 then out[#out + 1] = t[1] end end
  return table.concat(out, " ")
end

----------------------------------------------------------------- a template's game, run on stand-ins

-- the code of a template, compiled and run for a while: _init, then
-- _update and _draw with the arrows and A pressed now and then
local function play_template(code, name)
  local G = {}
  for k, v in pairs(E) do G[k] = v end
  G._init, G._update, G._draw = nil, nil, nil       -- (the SDK's own)
  local held = {}
  G.btn = function(i) return held[i] == true end
  G.btnp = function(i) return held[i] == true end
  G.keyp = function() return nil end
  local fn, err = load(code, "=" .. name, "t", G)
  check(fn, name .. ": compiles: " .. tostring(err))
  if not fn then return end
  local ok, e = pcall(function()
    fn()
    if G._init then G._init() end
    for f = 1, 400 do
      held = { [0] = f % 90 < 30, [1] = f % 90 >= 45, [2] = f % 50 < 10, [3] = f % 70 < 10, [4] = f % 37 == 0 }
      if G._update then G._update() end
      if G._draw then G._draw() end
    end
  end)
  check(ok, name .. ": runs 400 frames: " .. tostring(e))
end

----------------------------------------------------------------- the checks

-- a fresh start: the project page of a new game
run_sdk(nil)
frames(30)
check(sees("OPEN IN") and sees("bm Code") and sees("bm Animator") and sees("bm Sound"), "the hub: the suite's programs")
check(sees("PROJECT") and sees("screen 640x360") and sees("target .bm"), "the hub: the project's settings")
check(sees("CONTENTS") and sees("tokens"), "the hub: the contents measured")
check(sees("New game") and sees("bm SDK"), "the hub: the title and the program's name")
check(keyhelp_title == "bm SDK" and keyhelp_list, "F12: the keys of the page")

-- the dev kit (F1 again)
key("f1")
frames(5)
check(sees("dev kit") and sees("DATA IN MEMORY") and sees("CARTRIDGE FILE"), "F1 again: the dev kit")
check(sees("not tried yet"), "the dev kit: no try yet")
key("f1")
check(sees("CONTENTS"), "F1 again: the project")

-- each template: a new project with its code (which compiles and runs on
-- the stand-ins), its sprites and its map
local names = { "Empty 2D", "Platform 2D", "Top-down 2D", "Shooter 2D", "Versus 2D", "Online 2D", "3D scene",
                "3D with models" }
for i, nm in ipairs(names) do
  key("^n")
  check(sees("new project from a template"), "Ctrl+N: the templates")
  for _ = 2, i do key("down") end
  check(sees(nm), "the template " .. nm .. " in the list")
  key("\n")
  frames(2)
  check(sees(nm == "Empty 2D" and "New game" or nm), "new project: " .. nm)
  -- its code, as Save would write it
  key("^S")
  check(sees("file name"), nm .. ": Save as asks the name")
  for _ = 1, 10 do key("\b") end            -- "MYGAME.BME"
  typed("T" .. i)
  key("\n")
  check(last_save and last_save.path == "/carts/T" .. i .. ".BME", nm .. ": saved as T" .. i .. ".BME, a project")
  play_template(last_save.t.lua, nm)
  if nm == "Shooter 2D" then
    local inked = 0
    for y = 0, 7 do for x = 8, 15 do if sheet.px[y * 256 + x] then inked = inked + 1 end end end
    check(inked > 20, "the shooter's ship in cell 1 (" .. inked .. " pixels)")
  end
end

-- the platform: hero in cell 1, ground in cell 2, the map of ground
key("^n", "down", "\n")
check(sees("Platform 2D"), "the platform template again")
local ground = 0
for _, v in pairs(mapc) do if v == 2 then ground = ground + 1 end end
check(ground > 300, "the platform's map: ground (" .. ground .. " cells)")
check(sheet.px[8] == 0xFFD050 or sheet.px[10] == 0xFFD050, "the platform's hero drawn in cell 1")

-- the code page: the status, typing, the tokens
key("f2")
frames(210)                              -- (the message of the new project goes)
check(status():find("line 1/", 1, true) and status():find("tokens", 1, true), "F2: the code, with its tokens: " .. status())
typed("-- hi")
check(sees("-- hi"), "typing in the code")
check(sees("[f6]") and sees("assistant"), "the code page's hints")

-- the 2D page: sprites, then the map with F3 again
key("f3")
check(sees("SPRITES") and sees("spr(1, x, y)"), "F3: the sprites, on the template's first (8x8)")
check(sheet.px[9] == nil, "the hero's first row starts transparent")
key("right", " ")
check(sheet.px[9] == 0xFFFFFF, "space draws a pixel")
key("1")
check(E.fget(1, 1) and status():find("flag 1 on (fget(1, 1))", 1, true), "1: flag 1 of the cell: " .. status())
check(sees("FLAGS") and sees("0 wall 1 plat. 2 ladder"), "the flags under the palette")
key("1")
check(not E.fget(1, 1), "1 again: off")
key("0")
key("f3")
check(sees("MAP") and sees("tile 1") and sees("layer 1/1 main"), "F3 again: the map, on its first layer")
key(" ")
check(mapc[0] == 1, "space places the tile")
key("L")
check(#E.mlayers() == 2 and status():find("map layer 2/2: layer2", 1, true), "L: a new layer: " .. status())
key("right", " ")
check(E.mget(1, 0, 2) == 1 and E.mget(1, 0) == 0, "space places the tile on layer 2")
key("l")
check(status():find("map layer 1/2: main", 1, true), "l: the next layer: " .. status())
key("c")
check(status():find("flags of the tiles", 1, true) or sees("flags of the tiles"), "c: the flags over the map")
key("c")
-- the 16x16 brush (z): a tile of 2 x 2 cells (n, n + 1 and the two under them: the sheet has 32 cells a row),
-- the cursor and the tile on even cells; Home / End: a page left / right
check(sees("8/16"), "the map's hints: z, the brush")
key("z")
check(status():find("16x16", 1, true), "z: the 16x16 brush: " .. status())
key(".", "down", " ")                    -- the next tile (2: steps of 2), down a tile (row 2)
check(E.mget(0, 2) == 2 and E.mget(1, 2) == 3 and E.mget(0, 3) == 34 and E.mget(1, 3) == 35,
      "space: a 16x16 tile, 2 x 2 cells: " .. E.mget(0, 2) .. " " .. E.mget(1, 2) .. " " .. E.mget(0, 3) .. " " .. E.mget(1, 3))
key("end")
check(sees("(80,2)"), "End: a page to the right, on an even cell")
local function count(v) local n = 0 for _, c in pairs(mapc) do if c == v then n = n + 1 end end return n end
local empty, before = E.mget(80, 2) == 0 and E.mget(81, 3) == 0, count(35)
key("f")
check(empty and E.mget(80, 2) == 2 and E.mget(81, 2) == 3 and E.mget(80, 3) == 34 and E.mget(81, 3) == 35,
      "f: the empty area filled with 16x16 tiles: " .. tostring(empty) .. " " .. E.mget(80, 2) .. " " ..
      E.mget(81, 2) .. " " .. E.mget(80, 3) .. " " .. E.mget(81, 3))
local filled = count(35) - before          -- (a fill stops at 20000 cells: 5000 tiles of 2 x 2)
check(filled > 1000 and filled <= 5000, "the fill goes far, on whole tiles: " .. filled .. " tiles")
local odd = 0
for k, c in pairs(mapc) do
  if c == 35 and ((k % 4096) % 2 == 0 or (k // 4096) % 2 == 0) then odd = odd + 1 end
end
check(odd == 0, "the fill's tiles on even cells (" .. odd .. " corners elsewhere)")
key("u")
check(E.mget(80, 2) == 0 and E.mget(81, 3) == 0 and count(35) == before, "u: the fill undone")
key("home", "x")
check(status():find("tile 2 16x16", 1, true) or sees("tile 2 16x16"), "Home, x: back, the tile picked: " .. status())
key("\b")
check(E.mget(0, 2) == 0 and E.mget(1, 3) == 0, "Backspace: the 2 x 2 cells cleared")
key("z")
check(status():find("8x8", 1, true), "z again: 8x8: " .. status())
key("f3")
check(sees("SPRITES"), "F3 again: the sprites")

-- F5: saved, then tried; the SDK remembers where it was
key("f2", "f5")
check(sees("file name"), "F5 on a project with no name: Save as first")
key("\n")
check(ran == "/carts/MYGAME.BME", "F5 saves and tries the project: " .. tostring(ran))
check(saved_t and saved_t.page == "code", "the page remembered for the way back")
local tried = ran

-- back from the game: the dev kit's numbers of the run
run_sdk({ path = tried, back = true, run = { frames = 600, secs = 10, fps = 59.9, ms = 6.1, ms_max = 7.5, slow = 0,
                                              lua_kb = 120, lua_peak_kb = 180, data_kb = 330, instr_max = 12000,
                                              tokens = 321, tris = 0, gpu = false } })
frames(2)
check(status():find("back from the game: 59.9 fps, 6.1 ms (max 7.5)", 1, true), "back: the run's numbers: " .. status())
check(sees("explain the error"), "back on the code page")
key("f1", "f1")
frames(5)
check(sees("59.9 fps") and sees("RAM 510k peak"), "the dev kit: the last try")

-- the target .b16: the cap and what the format will not have (the shooter
-- uses math.random)
run_sdk({ path = "/carts/T4.BME" })
frames(10)
check(sees("Shooter 2D"), "the shooter opened")
key("b")
check(status():find("target .b16", 1, true), "b: the target .b16")
key("f1")
frames(5)
check(sees("of 8M (.b16)") and sees("math.random"), "the dev kit for a .b16: the cap, math.random")
check(saved_t.targets and saved_t.targets["/CARTS/T4.BME"] == "b16", "the target remembered for the file")

-- an error in the game: the code page on its line
run_sdk({ path = "/carts/T1.BME", back = true, error = "main.lua:3: boom" })
frames(2)
check(sees("main.lua:3: boom") or status():find("the game stopped", 1, true), "the error shown")
check(status():find("the game stopped", 1, true) or status():find("boom", 1, true), "back from an error: " .. status())

-- Ctrl+B: the game of the project (cart_build), next to it
key("^b")
check(built == "/carts/T1.BM" and status():find("built /carts/T1.BM", 1, true), "Ctrl+B builds the game: " .. status())

-- the models of a project (village.bm): the 3D page, the code for one,
-- the assistant's model joining them
run_sdk({ path = "/carts/village.bm" })
frames(10)
local nmodels = #E.models()
check(nmodels > 3, "village has models (" .. nmodels .. ")")
key("f4")
frames(3)
check(sees("MODELS " .. nmodels), "F4: the models")
check(draws > 0, "the model drawn")
local first = E.models()[1]
check(sees(first) and sees("vertices") and sees("triangles"), "the chosen model's numbers")
key("i")
check(sees('"' .. first .. '"') and sees("model") and sees("draw3d"), "i: the code for the model, on the code page")
key("f4", "f6")
check(sees("Assistant") and sees("Cubo"), "F6 on the 3D page: the assistant's 3D recipes")
key("\n")
frames(2)
check(#E.models() == nmodels + 1, "the assistant's model joins the project (" .. #E.models() .. ")")
check(status():find("the assistant's cube", 1, true), "its message: " .. status())

-- the guides on the project page (F6), their code into the code page
key("f1", "f6")
check(sees("Assistant") and sees("Un platform 2D"), "F6 on the project page: the guides")
key("\n")
frames(2)
check(sees("-- platform"), "Enter: the guide's code in the code page")

-- the other programs of the suite on the project (saved first)
key("f1", "3")
frames(1)
if sees("file name") then key("\n") end
check(tooled and tooled[1] == "studio", "3: bm Studio on the project: " .. tostring(tooled and tooled[1]))
check(tooled[2] == "/carts/VILLAGE.BME", "the game was saved first: its editable copy " .. tostring(tooled[2]))
run_sdk({ path = "/carts/village.bm", from = "pixel" })
frames(2)
check(status():find("back from bm Pixel", 1, true), "back from bm Pixel: " .. status())

-- the menu, and leaving with unsaved changes
key("esc")
check(sees("Exit bm SDK") and sees("New project"), "Esc: the menu")
key("up", "\n")
check(quitted, "Exit bm SDK leaves")

-- the mouse (bmui, 2026-10-06): the same things by clicking
local function at(str, x, y)
  for _, t in ipairs(texts) do
    if t[1] == str and (not x or t[2] == x) and (not y or t[3] == y) then return t[2] + 4, t[3] + 8 end
  end
end
local function mouse(x, y, b, w)
  local was = MS.b
  MS.x, MS.y, MS.b, MS.w = x, y, b or 0, w or 0
  MS.p = MS.b & ~was
  frames(1)
  MS.p, MS.w = 0, 0
end
local function click(x, y, b)
  mouse(x, y, 0); mouse(x, y, b or 1); mouse(x, y, 0); frames(1)
end

run_sdk({ path = "/carts/village.bm" })
frames(400)
-- the project page: a click chooses a row, a second click does it (target)
click(40, 16 + 12 * 16 + 8)
click(40, 16 + 12 * 16 + 8)
check(sees("target .b16"), "mouse: the project page, the target by clicking twice")
click(40, 16 + 12 * 16 + 8)
-- the tabs: 2D (sprites), drawing, a colour, a flag, the sheet
local mx, my = at("2D", nil, 0)
click(mx, my)
check(sees("SPRITES"), "mouse: the 2D tab")
click(336 + 5 * 16 + 4, 244)
mouse(40, 80, 1); mouse(80, 120, 1); mouse(80, 120, 0); frames(1)
mouse(60, 60, 4); mouse(60, 60, 0)
mouse(60, 60, 0, 1)
click(336 + 40, 56 + 20)
click(380 + 2 * 24, 292)
check(sees("flag 2"), "mouse: a click on a flag: " .. status())
click(100, 100, 2)
check(sees("Pick the colour") and sees("Ctrl+B"), "mouse: the sprite page's context menu")
click(620, 340)
-- the map: paint, pick, pan, the wheel, the tiles
key("f3")
check(sees("MAP"), "the map page")
mouse(100, 100, 1); mouse(140, 100, 1); mouse(140, 100, 0); frames(1)
mouse(100, 100, 4); mouse(100, 100, 0)
mouse(300, 200, 2); mouse(260, 180, 2); mouse(200, 150, 2); mouse(200, 150, 0); frames(1)
mouse(300, 200, 0, -2)
key("\t")
click(420, 100)
check(not sees("choosing the tile"), "mouse: a click in the tiles takes one")
-- the 3D page: a model by clicking, the view turned and zoomed, its menu
key("f4")
check(sees("MODELS"), "the 3D page")
mx, my = at("house", 16)
if mx then click(mx, my) end
mouse(400, 200, 2); mouse(430, 210, 2); mouse(460, 220, 2); mouse(460, 220, 0); frames(1)
mouse(400, 200, 0, 1)
click(400, 200, 2)
check(sees("Open in bm Studio") and sees("Spin"), "mouse: the 3D page's context menu")
mx, my = at("Spin")
click(mx, my)

io.write(string.format("sdk_host: %d checks, %d failed\n", checks, fails))
os.exit(fails == 0 and 0 or 1)
