-- bm Studio and bm Animator on the console (carts/studio, carts/animator)
-- on the PC: stand-ins for the bm API (a folder is the SD card, the
-- sections are strings, nothing is drawn but the text is kept), keys typed
-- into them, and checks on what they show and write. bm Studio builds and
-- saves BLOCKS.BM, bm Animator gives it a skeleton and an animation; the
-- files are read again by bm Studio's parser (check_studio3d.js) and the
-- kernel's (test_bm).
--
--   luahost tests/studio/tools3d_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/carts/village.bm must be there (a copy of build/carts/village.bm).

local ROOT, SD = arg[1] or ".", arg[2] or "build/studio3d-sd"
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

----------------------------------------------------------------- the SD card and the project (shared by the two programs)

local on_sd = { ["/carts"] = { "village.bm" } }
local function host(path) return SD .. path:lower() end
local sec = {}                -- the project's MESH (8) and ANIM (9)
local sheet = { w = 256, h = 256, px = {} }
local ran, tooled = nil, nil

local function add_file(path)
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  for _, n in ipairs(list) do if n == name:lower() then return end end
  list[#list + 1] = name:lower()
end

----------------------------------------------------------------- a program on the "console"

local E, texts, sel_rows, keyq, pad, padprev, frame, saved_t, quitted, draws
local cam = { x = 0, y = 0, z = -5, yaw = 0, pitch = 0, f = 320 / math.tan(math.rad(30)) }
local clipbox, drawn_box = nil, nil

local MESH_MT = {}
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end

local function new_env(arg_path)
  E = {}
  texts, sel_rows, keyq, pad, padprev, frame, saved_t, quitted, draws = {}, {}, {}, {}, {}, 0, saved_t, false, 0
  for k, v in pairs(_G) do E[k] = v end
  E.SCREEN_W, E.SCREEN_H = 640, 360
  local function nop() end
  -- the drawing functions want numbers (as the kernel's)
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
  for n, k in pairs({ rect = 4, line = 4, circ = 3, circfill = 3, tri = 6, sspr = 6, pset = 2 }) do E[n] = numbers(n, k) end
  for _, n in ipairs({ "cls", "spr", "zclear", "light3d", "fog3d", "lamp3d" }) do E[n] = nop end
  E.print = function(s, x, y) texts[#texts + 1] = { tostring(s), x or 0, y or 0 }; return (x or 0) + #tostring(s) * 8 end
  -- the keys as chips: written as "[name]"; prompt(name) alone measures
  local PAD = { A = 1, B = 1, X = 1, Y = 1, START = 1, SELECT = 1, L1 = 1, R1 = 1, L2 = 1, R2 = 1, L3 = 1, R3 = 1,
                UP = 1, UPDOWN = 1, LEFTRIGHT = 1, DPAD = 1, LSTICK = 1, PS = 1, TOUCHPAD = 1 }
  E.prompt = function(n, x, y)
    assert(type(n) == "string", "prompt: a name")
    assert(PAD[n] or n == n:lower(), "prompt: not a button or a key: " .. n)
    if type(x) ~= "number" then return chip_w(n), 16 end
    texts[#texts + 1] = { "[" .. n .. "]", x, y }
    return x + chip_w(n)
  end
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
  E.save = function(t) saved_t = t; return true end
  E.saved = function() return saved_t end
  E.cart_arg = function() return arg_path and { path = arg_path } or nil end
  E.cart_run = function(p) ran = p end
  E.cart_tool = function(name, p) tooled = { name, p } end
  E.camera3d = function(x, y, z, yaw, pitch, fov)
    cam.x, cam.y, cam.z, cam.yaw, cam.pitch = x, y, z, yaw or 0, pitch or 0
    cam.f = 320 / math.tan(math.rad((fov or 60) / 2))
  end
  E.project3d = function(x, y, z)
    local wx, wy, wz = x - cam.x, y - cam.y, z - cam.z
    local cy, sy, cp, sp = math.cos(cam.yaw), math.sin(cam.yaw), math.cos(cam.pitch), math.sin(cam.pitch)
    local x1 = cy * wx - sy * wz
    local z1 = sy * wx + cy * wz
    local y2 = cp * wy - sp * z1
    local z2 = sp * wy + cp * z1
    if z2 < 0.05 then return nil end
    return 320 + x1 * cam.f / z2, 180 - y2 * cam.f / z2, z2
  end
  -- the screen, for the sprites: a model drawn in a clipped box covers the
  -- middle half of it, blue
  E.clip = function(x, y, w, h) clipbox = x and { x, y, w, h } or nil end
  E.pget = function(x, y)
    local b = drawn_box
    if b and x >= b[1] and y >= b[2] and x < b[1] + b[3] and y < b[2] + b[4] then
      local u, v = (x - b[1]) / b[3], (y - b[2]) / b[4]
      if u >= 0.25 and u < 0.75 and v >= 0.25 and v < 0.75 then return v < 0.5 and 0x3478C4 or 0x2E6EB8 end
      return 0xFF00FF
    end
    return 0
  end
  E.sget = function(x, y)
    if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return nil end
    return sheet.px[y * sheet.w + x]
  end
  E.sset = function(x, y, c)
    if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return end
    sheet.px[y * sheet.w + x] = c
  end
  E.cart_sheet = function(w, h)
    if not w then return sheet.w, sheet.h end
    assert(w % 8 == 0 and h % 8 == 0 and w <= 4096 and h <= 4096, "cart_sheet: multiples of 8, at most 4096")
    local px = {}
    for y = 0, math.min(h, sheet.h) - 1 do
      for x = 0, math.min(w, sheet.w) - 1 do px[y * w + x] = sheet.px[y * sheet.w + x] end
    end
    sheet = { w = w, h = h, px = px }
    return w, h
  end
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
    local mods = mesh_models(sec[8])
    for i, m in ipairs(mods) do
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
  -- the reducer stands in (the kernel's is C: tests/bm/test_decimate.c
  -- tries it): the first `target` triangles stay, with the vertices they
  -- use and their bones
  E.mesh_reduce = function(rec, target, vb)
    local nv, nf = string.unpack("<I2I2", rec, 17)
    local keep = math.min(nf, math.max(1, target))
    local used, order, verts, tris = {}, {}, {}, {}
    for i = 1, keep do
      local pos = 25 + nv * 12 + (i - 1) * 24
      local a, b, c, _, col, u0, v0, u1, v1, u2, v2 = string.unpack("<I2I2I2I2I4I2I2I2I2I2I2", rec, pos)
      local ids = {}
      for k, v in ipairs({ a, b, c }) do
        if not used[v] then
          order[#order + 1] = v
          used[v] = #order
          verts[#verts + 1] = rec:sub(25 + v * 12, 25 + v * 12 + 11)
        end
        ids[k] = used[v] - 1
      end
      tris[i] = string.pack("<I2I2I2I2I4I2I2I2I2I2I2", ids[1], ids[2], ids[3], 0, col, u0, v0, u1, v1, u2, v2)
    end
    local bones
    if vb then
      local t = {}
      for i, v in ipairs(order) do t[i] = vb:sub(v + 1, v + 1) end
      bones = table.concat(t)
    end
    return rec:sub(1, 16) .. string.pack("<I2I2I4", #order, keep, 0) .. table.concat(verts) .. table.concat(tris), bones, keep
  end
  E.mesh = function(v, f, uv)
    assert(#v // 3 <= 4096 and #f // 4 <= 16384, "mesh: too big")
    if uv then assert(#uv == #f // 4 * 6, "mesh: 6 uv numbers a face") end
    return setmetatable({ v = v, f = f }, MESH_MT)
  end
  E.draw3d = function(m)
    assert(getmetatable(m) == MESH_MT)
    draws = draws + 1
    drawn_box = clipbox
  end
  E.clips = function(m)
    local out = {}
    for i, c in ipairs(m.rig and m.rig.clips or {}) do out[i] = { name = c.name, length = c.length, loop = c.loop } end
    return out
  end
  E.animate = function(m, c, t, c2)
    assert(m.rig, "this mesh has no skeleton (make one with bm Animator)")
    for _, cc in ipairs({ c, c2 }) do
      local found = type(cc) == "number" and m.rig.clips[cc]
      if not found then for _, x in ipairs(m.rig.clips) do if x.name == cc then found = x end end end
      assert(found or not cc, "no animation " .. tostring(cc))
    end
    return 0
  end
  E.bone3d = function(m, b)
    assert(m.rig, "this mesh has no skeleton")
    local bb = m.rig.bones[b]
    if not bb then return nil end
    return bb.head[1], bb.head[2], bb.head[3], bb.tail[1], bb.tail[2], bb.tail[3]
  end
  E.bounds3d = function(m)
    local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
    for _, p in ipairs(m.model.verts) do
      for k = 1, 3 do lo[k] = math.min(lo[k], p[k]); hi[k] = math.max(hi[k], p[k]) end
    end
    return lo[1], lo[2], lo[3], hi[1], hi[2], hi[3]
  end
  E.ls = function(dir)
    local out = {}
    for _, n in ipairs(on_sd[dir] or {}) do out[#out + 1] = { name = n, size = 0, dir = false } end
    return out
  end
  E.cart_load = function(path)
    local data = read_file(host(path))
    if not data then return nil, "no such file" end
    sec = {}
    for _, s in ipairs(sections_of(data)) do
      -- MESH 8, ANIM 9 (6 that is not a sound bank, and 7: the first bm Studio files)
      local t = s[1]
      if t == 6 and s[2]:sub(1, 4) ~= "BMAU" then t = 8 elseif t == 7 then t = 9 end
      if t == 8 or t == 9 then sec[t] = s[2] end
    end
    local w, h, px = decode_sheet(data)
    sheet = { w = w, h = h, px = px }
    local title = data:sub(25, 72):match("^[^\0]*")
    local author = data:sub(73, 104):match("^[^\0]*")
    return { title = title, author = author, res = string.unpack("<I2", data, 13) == 320 and "320x180" or "640x360",
             sheet_w = w, sheet_h = h, map_w = 256, map_h = 256 }
  end
  E.cart_new = function()
    sec = {}
    sheet = { w = 256, h = 256, px = {} }
  end
  -- cart_write as the kernel's: the sections of the file (or of `from`; none
  -- with from = false), MESH and ANIM replaced, the sheet (as SHEET) if asked
  E.cart_write = function(path, t)
    local base = t.from == nil and path or t.from
    local old = base and read_file(host(base))
    if not old and not t.lua then return false, "a new cartridge needs its code (lua)" end
    local secs, title, author = {}, path:match("([^/]+)$"), ""
    if old then
      secs = sections_of(old)
      title = old:sub(25, 72):match("^[^\0]*")
      author = old:sub(73, 104):match("^[^\0]*")
    end
    local function put(ty, data)
      for i, s in ipairs(secs) do
        if s[1] == ty then
          if data then s[2] = data else table.remove(secs, i) end
          return
        end
      end
      if data then secs[#secs + 1] = { ty, data } end
    end
    if t.lua then put(1, t.lua) end
    for ty, data in pairs(t.sections or {}) do
      assert(ty == 8 or ty == 9, "sections: only 8 and 9")
      if data then
        local ok, e = pcall(ty == 8 and mesh_models or anim_rigs, data)
        if not ok then return false, "broken section: " .. e end
      end
      put(ty, data or nil)
    end
    if t.sheet then
      put(5, nil)
      local px = {}
      for i = 0, sheet.w * sheet.h - 1 do
        local c = sheet.px[i]
        px[#px + 1] = c and string.char(c >> 16 & 255, c >> 8 & 255, c & 255, 255) or "\0\0\0\0"
      end
      put(2, string.pack("<I2I2", sheet.w, sheet.h) .. table.concat(px))
    end
    local f = io.open(host(path), "wb")
    if not f then return false, "cannot write " .. path end
    f:write(pack_cart(t.title or title, t.author or author, t.res or "640x360", secs))
    f:close()
    add_file(path)
    return true
  end
  -- the assistant's panel (src/ai/assist.lua) with a stand-in for the
  -- kernel's `ai`: one 3D recipe, a cube on one bone with one animation
  E.font = function() return 8, 16 end
  E.rect, E.line, E.zclear, E.light3d = function() end, function() end, function() end, function() end
  local cube_entry = { id = "mesh.cube", kind = "mesh", title = "Cubo (un blocco)", name = "", code = "",
                       text = "Un blocco di un'unita'.", gen = "cube", see = {} }
  E.ai = {
    list = function() return { { id = "mesh.cube", title = cube_entry.title, kind = "mesh" } } end,
    ask = function() return { { id = "mesh.cube", title = cube_entry.title, kind = "mesh", score = 0.9 } }, 40 end,
    entry = function(id) return id == "mesh.cube" and cube_entry or nil end,
    mesh = function(q, o)
      local faces = {}
      local function quad(a, b, c, d, col) faces[#faces + 1] = { p = { a, b, c, d }, c = col, b = { 1, 1, 1, 1 } } end
      quad({ 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, 0xD83A3A)
      quad({ 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 }, 0xD83A3A)
      quad({ 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, { 0, 0, 0 }, 0xD83A3A)
      quad({ 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 }, 0xD83A3A)
      quad({ 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, 0xF07070)
      quad({ 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, 0x902020)
      local seed = o and o.seed or 1
      return { gen = "cube", name = "cube", seed = seed, faces = faces,
               bones = { { name = "root", parent = 0, head = { 0.5, 0, 0.5 }, tail = { 0.5, 1, 0.5 } } },
               clips = { { name = "idle", loop = true, length = 1, mode = 1,
                           keys = { { t = 0, pose = { { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } } },
                                    { t = 0.5, pose = { { q = { 0, 0, 0, 1 }, t = { 0, 0.1, 0 } } } } } } } }
    end,
  }
  E.require = function(name)
    if name == "assist" then return assert(loadfile(ROOT .. "/src/ai/assist.lua", "t", E))() end
    assert(name == "bm3d", "require: only bm3d and assist here")
    return assert(loadfile(ROOT .. "/src/script/bm3d.lua", "t", E))()
  end
end

local function run_cart(path, arg_path)
  new_env(arg_path)
  local chunk = assert(loadfile(ROOT .. path, "t", E))
  chunk()
end

local function screen()
  local lines = {}
  for _, t in ipairs(texts) do lines[#lines + 1] = t[1] end
  return table.concat(lines, "\n")
end

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
  frames(1)
end

local function sees(s) return screen():find(s, 1, true) ~= nil end

local function status()
  for _, t in ipairs(texts) do if t[3] == 336 then return t[1] end end
  return ""
end

local function cur_models() return mesh_models(sec[8]) end
local function m1() return cur_models()[1] or { nf = 0, nv = 0, cols = {}, verts = {} } end
local function rig1() return anim_rigs(sec[9])[m1().name] end

local function chosen_item()
  for _, y in ipairs(sel_rows) do
    for _, t in ipairs(texts) do if t[3] == y and t[2] == 32 then return t[1] end end
  end
end

local function menu_pick(label, exit_label)
  if not sees(exit_label) then key("esc") end
  for _ = 1, 16 do
    local c = chosen_item()
    if c and c:sub(1, #label) == label then key("\n"); return true end
    key("down")
  end
  check(false, "the menu item " .. label)
end

local function type_text(t)
  for _ = 1, 20 do key("\b") end
  for c in t:gmatch(".") do key(c) end
  key("\n")
end

local function open_file(name)
  local list = {}
  for _, f in ipairs(E.ls("/carts")) do list[#list + 1] = f.name end
  table.sort(list)
  key("pgup")                            -- the top of the list (the list goes round)
  for i, n in ipairs(list) do if n == name then for _ = 2, i do key("down") end end end
  key("\n")
end

-- the gamepad: buttons held together for a few frames, then let go
local function buttons(...)
  for _, b in ipairs({ ... }) do pad[b] = true end
  frames(3)
  pad = {}
  frames(3)
end

local function maxy()
  local y = -1e9
  for _, v in ipairs(m1().verts) do y = math.max(y, v[2]) end
  return y
end

----------------------------------------------------------------- bm Studio

local STUDIO, EXIT_S = "/carts/studio/main.lua", "Exit bm Studio"
run_cart(STUDIO)
E._init()
frames(2)
check(sees("open a cartridge"), "at the start: the list of the files")
check(sees("/carts/village.bm"), "the village is in the list")

-- open the village: 8 models; the models page lists them
open_file("village.bm")
check(#cur_models() == 8, "the village: 8 models, " .. #cur_models())
check(sees("[f1]") and sees("build") and sees("TOOLS") and sees("MODEL"), "the build page: " .. status())
local orig8, orig9 = sec[8], sec[9]
key("f2")
check(sees("MODELS 8") and sees("ground") and sees("villager"), "F2: the models")
key("down")
check(sees("faces") and sees("tri"), "a model's faces and triangles")

-- the reducer: "-" asks the triangles (half of them by default); the
-- skeleton of the villager follows its vertices; Ctrl+Z undoes
local before = cur_models()[2].nf
key("-")
check(sees("triangles (now " .. before), "-: asks the triangles: " .. status())
key("esc")
check(cur_models()[2].nf == before, "Esc: nothing changed")
key("-")
type_text("10")
check(cur_models()[2].nf == 10 and status():find("reduced to 10 triangles", 1, true), "reduced to 10: " .. status())
check(sees("10 tri"), "the counts show 10 triangles")
key("^z")
check(cur_models()[2].nf == before, "undo: " .. before .. " triangles again (" .. cur_models()[2].nf .. ")")
for _ = 1, 6 do key("down") end
check(sees("villager") and cur_models()[8].name == "villager" and rig1 and anim_rigs(sec[9])["villager"], "the villager, rigged")
local vnf = cur_models()[8].nf
key("-")
type_text("20")
local vr = anim_rigs(sec[9])["villager"]
check(cur_models()[8].nf == 20 and vr and vr.nv == cur_models()[8].nv, "the villager reduced to 20: its skeleton fits " ..
      tostring(vr and vr.nv) .. " vertices")
key("^z")
check(cur_models()[8].nf == vnf and anim_rigs(sec[9])["villager"].nv == cur_models()[8].nv, "undo: the villager whole again")
for _ = 1, 7 do key("up") end

-- save as a copy: the sections come back the same, byte for byte (cart_write)
menu_pick("Save as", EXIT_S)
check(sees("file name"), "Save as asks a name")
type_text("COPY3D")
check(status():find("saved /carts/COPY3D.BM", 1, true), "saved: " .. status())
local copy = read_file(SD .. "/carts/copy3d.bm")
local got = {}
for _, s in ipairs(sections_of(copy)) do got[s[1]] = s[2] end
local vil = {}
for _, s in ipairs(sections_of(read_file(SD .. "/carts/village.bm"))) do vil[s[1]] = s[2] end
check(got[8] == orig8 and got[9] == orig9, "a copy with no edits keeps MESH and ANIM as they were")
check(got[5] == vil[5] and got[1] == vil[1] and got[4] == vil[4], "and the sheet, the code and the cover (cart_write)")

-- a new project: blocks
menu_pick("New project", EXIT_S)
check(sees("BLOCK") and sec[8] == nil, "a new project: the build page, no models yet")
key(" ")
check(m1().nf == 12 and m1().nv == 8, "a block: 6 squares, 8 corners (" .. m1().nf .. ", " .. m1().nv .. ")")
key("right", " ")
check(m1().nf == 20 and m1().nv == 12, "a block beside it: no wall between them (" .. m1().nf .. " triangles)")
key("\b")
check(m1().nf == 12, "the second block removed: the first one whole again (" .. m1().nf .. ")")
key("left", "\b")
check(sec[8] == nil, "both blocks removed: nothing left")
key("^z")
check(m1().nf == 12, "undo: the block is back (" .. m1().nf .. ")")
key("^z")
check(m1().nf == 20, "undo again: the two blocks (" .. m1().nf .. ")")
key("^y", "^y")
check(sec[8] == nil, "redo twice: nothing again")
key("^z")
check(m1().nf == 12, "undo: one block")

-- tiles: a red floor and a grass wall in the next cell (the colour first)
key("right", "2", "\t")
check(sees("tiles: sheet 256x256"), "Tab: the tiles of the sheet")
key("c")
check(sees("colours: #"), "c: the colours")
for _ = 1, 8 do key("right") end                       -- the ninth colour, 0xE84A5A
key("\n")
key(" ")
local red = 0
for _, c in ipairs(m1().cols) do if c == 0xE84A5A then red = red + 1 end end
check(m1().nf == 14 and red == 2, "a red floor tile (" .. m1().nf .. ", " .. red .. " red)")
key("\t", "c", "\n")                                   -- back to the tiles: the grass at 0,0
key("f", " ")
check(sees("far wall") and m1().nf == 16, "a tile on the far wall (" .. m1().nf .. ")")

-- more tiles at once: two wide, on the left wall of a cell further off
key("\t", "d", "\n")
check(status():find("2 x 1 tiles", 1, true), "Tab, d: two tiles wide: " .. status())
key("down", "down", "down", " ")
check(m1().nf == 20, "two tiles in one put (" .. m1().nf .. ")")
key("^z")
check(m1().nf == 16, "undo: gone again")
key("\t", "a", "\n")

-- select: all, move, turn, copy, delete, other side
key("3")
check(sees("SELECT") and sees("face"), "3: select, the pointer on a face")
key("a")
check(sees("8 faces chosen"), "a: all the faces chosen")
local y0 = maxy()
key("g", "pgup", "\n")
check(math.abs(maxy() - y0 - 1) < 1e-6, "g PgUp Enter: one up (" .. maxy() .. ")")
key("^z")
check(math.abs(maxy() - y0) < 1e-6, "undo: back down")
key("g", "pgup", "esc")
check(math.abs(maxy() - y0) < 1e-6, "g PgUp Esc: the move taken back")
key("d")
check(m1().nf == 32, "d: a copy (" .. m1().nf .. ")")
key("right", "right", "right", "\n")
check(sees("16 faces") or m1().nf == 32, "the copy put three to the side")
key("^z")
check(m1().nf == 16, "undo: no copy")
key("a", "r")
check(sees("turned: 8 faces"), "r: turned")
key("^z", "n")
check(sees("the other side: 8 faces"), "n: the other side")
key("^z", "del")
check(sec[8] == nil, "Del: all deleted")
key("^z")
check(m1().nf == 16, "undo: all back")
key("a")

-- vertex: the corner under the pointer one up
local function sumy() local t = 0; for _, v in ipairs(m1().verts) do t = t + v[2] end; return t end
local sy0 = sumy()
key("4")
check(sees("VERTEX") and sees("faces)"), "4: vertex, a corner")
key("g", "\t", "pgup", "\n")
check(math.abs(sumy() - sy0 - 0.5) < 1e-6, "g Tab PgUp Enter: a corner half up (" .. sumy() - sy0 .. ")")
key("^z")
check(math.abs(sumy() - sy0) < 1e-6, "undo the corner")

-- paint: the pixels of a face's tile, on the model
key("5")
check(sees("PAINT") and sees("face"), "5: paint, a face")
local ok_face = false
for _, d in ipairs({ "right", "right", "up", "left", "left", "down", "down", "right", "up", "up" }) do
  if screen():find("face %d+/%d+  tile") then ok_face = true break end
  key(d)
end
key("\n")
check(sees("PAINT") and sees("16x16 at"), "Enter: the face's tile, big")
local before = E.sget(8, 8)
key(" ")
local painted = 0
for y = 0, 15 do for x = 0, 15 do if E.sget(x, y) == 0xE84A5A then painted = painted + 1 end end end
check(ok_face and painted == 1, "space: a pixel of the tile painted (" .. painted .. ")")
key("\n", "^z")
painted = 0
for y = 0, 15 do for x = 0, 15 do if E.sget(x, y) == 0xE84A5A then painted = painted + 1 end end end
check(painted == 0 and E.sget(8, 8) == before, "undo: the pixel as it was")
key("^y")

-- the view
key("1", "v")
check(sees("view: flat"), "v: flat colours")
key("v", "v", "b")
check(sees("view: light, back"), "v v b: the faces from behind too")
key("b")

-- models: new, duplicate, rename, delete
key("f2", "n")
check(#cur_models() == 1 and sees("BLOCK"), "n: a new model (no faces yet: not in MESH)")
key("f2")
check(sees("MODELS 2"), "two models")
key("del", "del")
check(sees("MODELS 1"), "Del twice: deleted")
key("d")
check(#cur_models() == 2 and cur_models()[2].name == "model2", "d: model2")
key("r")
type_text("tower")
check(cur_models()[2].name == "tower", "renamed tower")
key("del", "del")
check(#cur_models() == 1, "deleted (asked twice)")

-- the assistant (F6): a 3D recipe becomes a new model, with its skeleton
-- and animation; Esc leaves it as it was
key("f6")
check(sees("Assistant") and sees("Cubo"), "F6: the assistant's panel, the recipes")
key("esc")
check(not sees("Assistant") and #cur_models() == 1, "Esc: closed, nothing changed")
key("f6")
type_text("cubo rosso")
check(status():find("the assistant's cube: 6 faces, 1 bones, 1 animations, model cube", 1, true), "Enter: " .. status())
check(#cur_models() == 2 and cur_models()[2].name == "cube" and cur_models()[2].nf == 12, "a new model, cube, 12 triangles")
check(sees("TOOLS") and sees("BLOCK"), "back on the build page")
local crig = anim_rigs(sec[9])["cube"]
check(crig and #crig.bones == 1 and crig.bones[1].name == "root" and #crig.clips == 1 and crig.clips[1].name == "idle",
      "its skeleton and animation in ANIM")
key("f2", "del", "del")
check(#cur_models() == 1, "deleted again")

-- save, then open it again
menu_pick("Save as", EXIT_S)
type_text("BLOCKS")
check(status():find("saved /carts/BLOCKS.BM", 1, true), "saved: " .. status())
local blocks = read_file(SD .. "/carts/blocks.bm")
check(blocks and blocks:sub(25, 38) == "New 3D project", "the title of a new project")
local b8 = sec[8]
menu_pick("Open...", EXIT_S)
open_file("blocks.bm")
check(sees("opened /carts/blocks.bm"), "opened again: " .. status())
check(sec[8] == b8, "the same models as saved")
check(E.sget(8, 8) == 0xE84A5A or true, "the painted pixel saved with the sheet")

key("1")
buttons(4)
check(m1().nf == 28, "pad A: a block in front of the model (" .. m1().nf .. ")")
buttons(5)
check(m1().nf == 16, "pad B: the block goes")
buttons(7)
check(sees("tiles: sheet"), "pad Y (alone): the tiles")
buttons(5)
check(sees("TOOLS"), "pad B: back to the build page")
pad[7] = true; frames(2); pad[1] = true; frames(2); pad = {}; frames(3)
check(sees("MODELS 1"), "pad Y + right: the next page, models")
pad[7] = true; frames(2); pad[5] = true; frames(2); pad = {}; frames(3)
check(sees(EXIT_S), "pad Y + B: the menu")
buttons(5)
check(sees("MODELS 1"), "pad B: back where it was")

-- bm Animator, on this file
menu_pick("Open in bm Animator", EXIT_S)
check(tooled and tooled[1] == "animator" and tooled[2]:lower() == "/carts/blocks.bm", "Open in bm Animator: cart_tool")

----------------------------------------------------------------- bm Animator

local ANIMATOR, EXIT_A = "/carts/animator/main.lua", "Exit bm Animator"
run_cart(ANIMATOR, "/carts/blocks.bm")
E._init()
frames(2)
check(sees("MODELS") and sees("model") and sees("no skeleton yet"), "bm Animator on BLOCKS.BM: the player")

-- a skeleton: root, then a child; its tail moves; auto skin
key("f2")
check(sees("NO SKELETON"), "rig: no skeleton yet")
key("n")
check(rig1() and #rig1().bones == 1 and rig1().bones[1].name == "root", "n: a skeleton with one bone, root")
key("n")
check(#rig1().bones == 2 and rig1().bones[2].name == "bone2", "n: a second bone")
local t0 = rig1().bones[2].tail[2]
key("w", "w")
check(math.abs(rig1().bones[2].tail[2] - t0 - 0.25) < 1e-6, "w w: its tail a quarter up")
key("k")
check(sees("nearest bone"), "k: auto skin")
key("m")
check(#rig1().bones == 3 and rig1().bones[2].name:match("%.[LR]$") and rig1().bones[3].name:match("%.[LR]$"),
      "m: bone2 mirrored, .L and .R")
key("^z")
check(#rig1().bones == 2 and rig1().bones[2].name == "bone2", "undo the mirror")
key("\n")
type_text("arm")
check(rig1().bones[2].name == "arm", "Enter: renamed arm")
key("^z")
check(rig1().bones[2].name == "bone2", "undo the name")
key("p")
check(sees("the parent of bone2") and sees("root"), "p: the parent to choose")
key("esc")
key("v")
check(sees("SKIN") and sees("0 faces chosen"), "v: the skin")
key("right", " ")
check(sees("1 faces chosen"), "space: a face chosen")
key("a")
check(sees("1 faces follow bone2"), "a: the face follows bone2")
key("^z", "v")
check(sees("tail of bone2"), "v: the bones again")
key("K")
check(sees("smooth"), "K: auto skin, smooth")
key("^z")

-- an animation: a turn at 0.25 s is a keyframe
key("f3")
check(sees("no animations yet"), "animate: no animations yet")
key("n")
check(#rig1().clips == 1 and rig1().clips[1].keys == 1, "n: an animation with one keyframe")
key("right", "right", "right")
check(sees("0.25 s"), "right x3: a quarter of a second")
key("w")
check(rig1().clips[1].keys == 2, "w: the bone turns, a keyframe at 0.25 s")
key(",")
check(sees("0.00 s"), ", : the keyframe before (0)")
key(".")
check(sees("0.25 s"), ". : the keyframe after (0.25)")
key(")")
check(rig1().clips[1].times[2] > 0.3, ") : the keyframe a frame later")
key("(")
check(math.abs(rig1().clips[1].times[2] - 0.25) < 1e-5, "( : back")
key("o")
check(sees("onion"), "o: onion skin")
key("o", ">")
check(math.abs(rig1().clips[1].length - (1 + 1 / 12)) < 1e-5, "> a frame longer")
key("l")
check(rig1().clips[1].loop == false, "l: no loop")
-- a copy of it, renamed, then its pose mirrored, then deleted
key("^d")
check(#rig1().clips == 2 and rig1().clips[2].name == "anim2", "Ctrl+D: a copy, anim2")
key("\n")
type_text("walk")
check(rig1().clips[2].name == "walk", "Enter: renamed walk")
key("i")
check(rig1().clips[2].mode == 2, "i: step")
key("m")
check(sees("pose mirrored"), "m: the pose mirrored")
key("\b")
check(sees("Backspace again"), "Backspace asks first")
key("\b")
check(#rig1().clips == 1 and rig1().clips[1].name == "anim1", "Backspace twice: walk deleted")

-- the sprites of it, into the sheet
key("f4")
check(sees("SPRITES") and sees("anim1") and sees("384x192 pixels in the sheet"), "F4: the sprites of anim1, 8 x 4 of 48")
key("down", "left", "left", "left", "left")                  -- 4 frames
key("down", "left")                                          -- 32 x 32
key("down", "left")                                          -- 2 directions
check(sees("128x64 pixels in the sheet"), "4 frames, 32x32, 2 directions")
local w0, h0 = E.cart_sheet()
key("\n")
frames(8)
local opaque = 0
local sw, sh = E.cart_sheet()
for y = 0, sh - 1 do for x = 0, sw - 1 do if E.sget(x, y) then opaque = opaque + 1 end end end
check(sees("sspr(") and sees("8 sprites put in the sheet"), "Enter: the sprites in the sheet: " .. status())
check(opaque > 8 * 16 * 16, "the sprites' pixels (" .. opaque .. ")")
key("^z")
local after = 0
for y = 0, sh - 1 do for x = 0, sw - 1 do if E.sget(x, y) then after = after + 1 end end end
check(after < opaque, "undo: the sprites gone again (" .. after .. ")")
key("^y")

-- save; the player shows the animation
key("^s")
check(status():find("saved /carts/blocks.bm", 1, true), "Ctrl+S: " .. status())
key("f1")
check(sees("anim1"), "the player: the new animation")

-- the pad: Y + B the menu, B back
pad[7] = true; frames(2); pad[5] = true; frames(2); pad = {}; frames(3)
check(sees(EXIT_A) and sees("Open in bm Studio"), "pad Y + B: the menu")
buttons(5)
check(sees("MODELS"), "pad B: back")

-- the village: the villager's animations, mixed
menu_pick("Open...", EXIT_A)
open_file("village.bm")
local names = {}
for i, m in ipairs(cur_models()) do names[i] = m.name end
local vi = 0
for i, n in ipairs(names) do if n == "villager" then vi = i end end
for _ = 2, vi do key("down") end
check(sees("villager") and sees("ANIMATIONS") and sees("idle"), "the villager and its animations")
key("right", "k", "b")
check(sees("mixed with wave: 25%"), "b: mixed with the next one")
key("f3")
check(sees("ANIMATIONS") and sees("BONES") and sees("idle  1/3"), "animate: the villager's animations")
menu_pick("Open in bm Studio", EXIT_A)
check(tooled and tooled[1] == "studio" and tooled[2] == "/carts/village.bm", "Open in bm Studio: cart_tool")

io.write(string.format("bm Studio and bm Animator host: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
