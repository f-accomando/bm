-- bm Mesh builds a low-poly humanoid with the tools of phase 1 of the modelling suite, bm Animator
-- gives it a skeleton and a walk, and the model plays (APP-10, docs/design/modelling-suite.md).
-- Stand-ins for the bm API as in mesh_host.lua and tools3d_host.lua (a folder is the SD card,
-- nothing is drawn, the text is kept); every edit is a key typed into the cartridge.
--
--   luahost tests/studio/humanoid_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/bme must be there; the project humanoid.bme is made here.

local ROOT, SD = arg[1] or ".", arg[2] or "build/humanoid-sd"

local MF = dofile(ROOT .. "/tests/studio/model_card.lua")      -- /bmm and /glb (APP-9)
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

----------------------------------------------------------------- the sections as the kernel reads them

-- MESH: the models with every vertex and every triangle (1-based corners, colour)
local function mesh_models(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nv, nf = string.unpack("<I2I2", bin, pos + 16)
    local verts, faces = {}, {}
    for i = 1, nv do verts[i] = { string.unpack("<fff", bin, pos + 24 + (i - 1) * 12) } end
    for i = 1, nf do
      local f = pos + 24 + nv * 12 + (i - 1) * 24
      local a, b, c, _, col = string.unpack("<I2I2I2I2I4", bin, f)
      assert(a < nv and b < nv and c < nv, "MESH: a vertex out of range")
      faces[i] = { a + 1, b + 1, c + 1, col }
    end
    out[#out + 1] = { name = name, nv = nv, nf = nf, verts = verts, faces = faces }
    pos = pos + 24 + nv * 12 + nf * 24
  end
  assert(pos == #bin + 1, "MESH: the size does not add up")
  return out
end

-- ANIM: the skeletons with their bones, the bone of each vertex and every key of every animation
local function anim_rigs(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nb, nc, nv = string.unpack("<I2I2I2", bin, pos + 16)
    local r = { nb = nb, nc = nc, nv = nv, bones = {}, clips = {}, vb = {} }
    for i = 1, nb do
      local b = pos + 24 + (i - 1) * 44
      local parent, _, hx, hy, hz, tx, ty, tz = string.unpack("<i2I2ffffff", bin, b + 16)
      assert(parent >= -1 and parent < i - 1, "ANIM: a parent after its child")
      r.bones[i] = { name = bin:sub(b, b + 15):match("^[^\0]*"), parent = parent + 1, head = { hx, hy, hz }, tail = { tx, ty, tz } }
    end
    for i = 1, nv do
      r.vb[i] = bin:byte(pos + 24 + nb * 44 + i - 1)
      assert(r.vb[i] < nb, "ANIM: a vertex of no bone")
    end
    local p = pos + 24 + nb * 44 + ((nv + 3) & ~3)
    for c = 1, nc do
      local nk, mode, flags, length = string.unpack("<I2BBf", bin, p + 16)
      assert(nk >= 1 and mode <= 2 and length > 0, "ANIM: a bad clip")
      local prev, times, poses = 0, {}, {}
      for k = 0, nk - 1 do
        local kp = p + 24 + k * (4 + nb * 28)
        local t = string.unpack("<f", bin, kp)
        assert(t >= prev and t <= length + 1e-4, "ANIM: keys out of order")
        prev = t
        times[k + 1] = t
        local pose = {}
        for b = 1, nb do
          local qx, qy, qz, qw, tx, ty, tz = string.unpack("<fffffff", bin, kp + 4 + (b - 1) * 28)
          pose[b] = { q = { qx, qy, qz, qw }, t = { tx, ty, tz } }
        end
        poses[k + 1] = pose
      end
      r.clips[c] = { name = bin:sub(p, p + 15):match("^[^\0]*"), length = length, loop = flags & 1 == 1, keys = nk,
                     times = times, poses = poses, mode = mode }
      p = p + 24 + nk * (4 + nb * 28)
    end
    out[name] = r
    pos = p
  end
  assert(pos == #bin + 1, "ANIM: the size does not add up")
  return out
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

----------------------------------------------------------------- the SD card and the project (shared by the two programs)

local on_sd = { ["/carts"] = { "village.bm" }, ["/pics"] = { "hero.png", "notes.txt" } }
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
local keyhelp_list, keyhelp_title
local cam = { x = 0, y = 0, z = -5, yaw = 0, pitch = 0, f = 320 / math.tan(math.rad(30)) }
local clipbox, drawn_box = nil, nil

local MESH_MT = {}
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end

local MS
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
  E.keyhelp = function(list, title) keyhelp_list, keyhelp_title = list, title; return 0 end
  -- the mouse (bmui): MS as a USB mouse would give it
  MS = { x = 0, y = 0, b = 0, w = 0, p = 0 }
  E.mouse = function(on) if on ~= nil then return true end; return MS.x, MS.y, MS.b, MS.w, true end
  E.mousep = function(i) return MS.p >> (i or 0) & 1 == 1 end
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
    assert(t == 8 or t == 9 or t == 11 or t == 14, "cart_data: 8 (MESH), 9 (ANIM), 11 (SPRITES) or 14 (BOXES)")
    if select("#", ...) == 0 then return sec[t] end
    local data = ...
    if data == "" then data = nil end
    if data and (t == 8 or t == 9) then
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
  E.draw3d = function() draws = draws + 1 end
  E.mesh = function(v, f, uv)
    assert(#v // 3 >= 1 and #v // 3 <= 65535 and #f // 4 >= 1 and #f // 4 <= 65535, "mesh: 1 to 65535 vertices, 1 to 65535 faces")
    return setmetatable({ v = v, f = f, uv = uv }, MESH_MT)
  end
  E.cart_meshes = function() return {}, nil end          -- the project has no code that builds meshes
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
  MF.install(E)
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
    local lua = ""
    for _, x in ipairs(sections_of(data)) do if x[1] == 1 then lua = x[2] end end
    return { title = title, author = author, lua = lua, res = string.unpack("<I2", data, 13) == 320 and "320x180" or "640x360",
             sheet_w = w, sheet_h = h, map_w = 256, map_h = 256 }
  end
  E.cart_new = function()
    sec = {}
    sheet = { w = 256, h = 256, px = {} }
  end
  -- cart_write as the kernel's: the sections of the file (or of `from`; none
  -- with from = false), MESH and ANIM replaced, the sheet (as SHEET) if asked
  local project_target = dofile(ROOT .. "/tests/studio/project_rules.lua")
  E.cart_write = function(path, t)
    local moved
    path, moved = project_target(path, function(p) return read_file(host(p)) ~= nil end, function(a, b)
      local f = io.open(host(b), "wb")
      f:write(read_file(host(a)))
      f:close()
      add_file(b)
    end)
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
    if moved then return true, path end
    return true
  end
  -- the assistant's panel (src/ai/assist.lua) with a stand-in for the
  local bmui
  E.require = function(name)
    if name == "assist" then return assert(loadfile(ROOT .. "/src/ai/assist.lua", "t", E))() end
    if name == "bmui" then                 -- the mouse: once (it takes keyp over)
      bmui = bmui or assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", E))()
      return bmui
    end
    assert(name == "bm3d", "require: only bm3d, bmui and assist here")
    return assert(loadfile(ROOT .. "/src/script/bm3d.lua", "t", E))()
  end
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

local KEYS = 0                               -- every key typed into an editor, for the report
local function key(...)
  for _, k in ipairs({ ... }) do
    KEYS = KEYS + 1
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

local function type_text(t)
  for _ = 1, 20 do key("\b") end
  for c in t:gmatch(".") do key(c) end
  key("\n")
end

local function say(...) io.write(string.format(...), "\n") end

-- the time and the keys of each tool, for the report
local TOOLS, TOOL_ORDER = {}, {}
local function timed(name, fn, ...)
  local t = TOOLS[name]
  if not t then t = { n = 0, keys = 0, secs = 0 }; TOOLS[name] = t; TOOL_ORDER[#TOOL_ORDER + 1] = name end
  local c0, k0 = os.clock(), KEYS
  local r = fn(...)
  t.n, t.keys, t.secs = t.n + 1, t.keys + KEYS - k0, t.secs + os.clock() - c0
  return r
end

local function section(data, t)
  for _, s in ipairs(sections_of(data)) do if s[1] == t then return s[2] end end
end

----------------------------------------------------------------- the project and bm Mesh

local MESH, ANIMATOR = "/carts/mesh/main.lua", "/carts/animator/main.lua"
local PROJECT = "/bme/humanoid.bme"
local PFILE = host(PROJECT)
local NAME = "humanoid"

do
  local f = assert(io.open(PFILE, "wb"), "the SD folder needs a bme folder: " .. PFILE)
  f:write(pack_cart("Humanoid", "bm", "640x360", { { 1, "function _draw() cls(0) end\n" } }))
  f:close()
  add_file(PROJECT)
end

-- Ctrl+S, then the model as the file holds it (NV: its vertices, for the pointer)
local NV = 0
local function save_model()
  key("^s")
  assert(status():find("saved", 1, true), "Ctrl+S: " .. status())
  for _, m in ipairs(mesh_models(section(read_file(PFILE), 8))) do
    if m.name == NAME then NV = m.nv; return m end
  end
end

-- the step of the tools (the keys , and .) as the editor holds it: it starts at 3
local STEP = { move = { 0.01, 0.05, 0.1, 0.25, 0.5, 1 }, scale = { 1.01, 1.05, 1.1, 1.25, 1.5, 2 },
               rot = { 1, 5, 15, 30, 45, 90 } }
local step_i = 3
local function set_step(i)
  while step_i < i do key("."); step_i = step_i + 1 end
  while step_i > i do key(","); step_i = step_i - 1 end
end

-- one value on one axis inside a tool: the axis key, then the arrows, biggest step first
local cur_axis
local function to_axis(a)
  if cur_axis ~= a then key(a); cur_axis = a end
end
local function arrows(kind, a)
  local S, rem = STEP[kind], a
  local neg = false
  if kind == "scale" then rem = math.log(a) else rem = a end
  if rem < 0 then neg, rem = true, -rem end
  for i = #S, 1, -1 do
    local unit = kind == "scale" and math.log(S[i]) or S[i]
    local n = math.floor(rem / unit + 1e-6)
    if n > 0 then
      set_step(i)
      for _ = 1, n do key(neg and "left" or "right") end
      rem = rem - n * unit
    end
  end
end

-- move (metres), scale (factors) or rot (degrees) of what is chosen, on the axes of spec
local function tool(kind, spec)
  key(kind == "move" and "g" or kind == "rot" and "r" or "t")
  cur_axis = nil
  local rest = kind == "scale" and 1 or 0
  for _, a in ipairs({ "x", "y", "z" }) do
    local v = spec[a]
    if v and math.abs(v - rest) > 1e-9 then
      to_axis(a)
      arrows(kind, v)
    end
  end
  key("\n")
end

local function mode(m)                       -- "VERTICES", "EDGES" or "FACES" (Tab)
  for _ = 1, 3 do
    if sees(m .. ":") then return end
    key("\t")
  end
end
local function none() if not sees(": 0 chosen") then key("a") end end

-- the numbers of the vertices (or faces, or edges) to choose, n of them in all: n steps the pointer
-- on, b steps it back (from the start, b goes to the last one), space chooses
local function pick(list, count)
  none()
  key("home")
  count = count or NV
  local cur = nil
  table.sort(list)
  for _, i in ipairs(list) do
    local fwd = cur and (i - cur) % count or i
    local bwd = cur and (cur - i) % count or (count - i + 1)
    if fwd <= bwd then for _ = 1, fwd do key("n") end
    else for _ = 1, bwd do key("b") end end
    cur = i
    key(" ")
  end
end

-- the colour of the palette (index 1..30) for the next parts and for p
local pal_i = 9
local function set_colour(i)
  key("c")
  local d = (i - pal_i) % 30
  for _ = 1, d // 10 do key("down") end
  for _ = 1, d % 10 do key("right") end
  key("\n")
  pal_i = i
end

local SHAPES = { cube = 0, cylinder = 1, sphere = 2, plane = 3 }
-- Shift+A: a part of unit size at 0 (or at the middle of what is chosen: none() first)
local function new_part(shape, col)
  none()
  if col then set_colour(col) end
  key("A")
  for _ = 1, SHAPES[shape] do key("down") end
  key("\n")
end

local function find_vertex(m, x, y, z, from)
  for i = from or 1, m.nv do
    local p = m.verts[i]
    if math.abs(p[1] - x) < 1e-3 and math.abs(p[2] - y) < 1e-3 and math.abs(p[3] - z) < 1e-3 then return i end
  end
end

----------------------------------------------------------------- the scenario: Mesh

local function run_cart(path, arg_path)
  new_env(arg_path)
  -- nothing is drawn: the lines and circles need no checks here (the other hosts check them)
  for _, n in ipairs({ "line", "circ", "circfill", "rect", "tri", "pset" }) do E[n] = function() end end
  local chunk = assert(loadfile(ROOT .. path, "t", E))
  chunk()
  E._init()
  frames(2)
end

local T0 = os.clock()
local R = {}                                 -- the numbers for the report
local function dims(c, s) return { x = s[1], y = s[2], z = s[3] }, { x = c[1], y = c[2], z = c[3] } end

-- Ctrl+R across the vertical edge of a part: the ring of new vertices stays chosen
local function loop_cut(a, b)
  mode("VERTICES")
  pick({ a, b })
  timed("loop cut", key, "^r")
  check(status():find("loop cut", 1, true) and status():find("closed ring", 1, true), "loop cut: " .. status())
end

-- a cylinder or a sphere or a cube as a part: shape, colour, size, middle; returns the vertex
-- number its part starts at
local function make_part(shape, col, size, at, opts)
  opts = opts or {}
  local m = save_model()
  local base = m.nv
  timed("primitive", new_part, shape, col)
  if opts.subdivide then timed("subdivide", key, "u") end
  local s, c = dims(at, size)
  timed("scale", tool, "scale", s)
  timed("move", tool, "move", c)
  return base
end

-- the cuts across a cylinder: the middle, then the middle of each half; the first ring is scaled
local function cut_cylinder(base, n, ring)
  local m = save_model()
  local top, bot, ctr = m.verts[base + 1], m.verts[base + 9], m.verts[base + 17]
  local ymid = (top[2] + bot[2]) / 2
  -- the ring vertex across the edge of the first vertex (the ring may have been scaled)
  local function mid_vertex(mm)
    for i = base + 19, mm.nv do
      local p = mm.verts[i]
      if math.abs(p[2] - ymid) < 1e-3 and math.abs(p[3] - top[3]) < 1e-3 and p[1] > ctr[1] then return i end
    end
  end
  loop_cut(base + 1, base + 9)
  if ring then timed("scale", tool, "scale", { x = ring, z = ring }) end
  if n >= 3 then
    m = save_model()
    local mid = mid_vertex(m)
    check(mid ~= nil, "the first ring is in the middle of the edge")
    loop_cut(base + 1, mid)
    m = save_model()
    mid = mid_vertex(m)
    loop_cut(mid, base + 9)
  end
end

-- the part chosen whole (a vertex, then l: all linked) and copied across x = 0 (Shift+M)
local function mirror_part()
  mode("VERTICES")
  none()
  key("home", "b", " ")                        -- b from the start: the last vertex, a vertex of the new part
  timed("linked", key, "l")
  timed("mirror copy", key, "M")
end

local colour = { skin = 15, shirt = 22, trousers = 3, boots = 7, hair = 14 }

run_cart(MESH, PROJECT)
check(sees("0 models"), "bm Mesh opens the project: " .. status())
key("n")
key("r")
type_text(NAME)
key("f2")
check(sees("VERTICES: 0 chosen") and sees("of 65535 vertices"), "the edit page with the counters")
local m = save_model()
check(m and m.nv == 8 and m.nf == 12, "a new model: a cube, 8 vertices, 12 triangles")

-- the centre line: the pelvis is that cube, cut finer once; then torso, neck, head
key("a")
timed("subdivide", key, "u")
timed("scale", tool, "scale", { x = 0.36, y = 0.2, z = 0.22 })
timed("move", tool, "move", { y = 0.45 })
set_colour(colour.trousers)
timed("paint", key, "p")                     -- the new model's cube is pink: trousers
m = save_model()
R.pelvis = m.nf

local base = make_part("cylinder", colour.shirt, { 0.36, 0.5, 0.24 }, { 0, 1.30, 0 })
cut_cylinder(base, 3, 0.85)                  -- the waist
m = save_model()
R.torso = m.nf - R.pelvis - 32

base = make_part("cylinder", colour.skin, { 0.12, 0.14, 0.12 }, { 0, 1.62, 0 })
R.neck_base = base
cut_cylinder(base, 1)

base = make_part("sphere", nil, { 0.22, 0.26, 0.24 }, { 0, 1.80, 0 }, { subdivide = true })

-- the right side (x > 0): an arm and a leg, each part cut, shaped and copied to the left
local function limb(shape, col, size, at, n, ring, opts)
  local b = make_part(shape, col, size, at, opts)
  if n > 0 then cut_cylinder(b, n, ring) end
  mirror_part()
  return b
end
limb("cylinder", colour.shirt, { 0.11, 0.28, 0.11 }, { 0.36, 1.40, 0 }, 3, 1.15)
limb("cylinder", colour.skin, { 0.09, 0.26, 0.09 }, { 0.36, 1.13, 0 }, 3, 1.1)
limb("cube", 9, { 0.08, 0.12, 0.06 }, { 0.36, 0.94, 0 }, 0, nil, { subdivide = true })
limb("cylinder", colour.trousers, { 0.15, 0.34, 0.15 }, { 0.10, 0.70, 0 }, 3, 1.12)
limb("cylinder", colour.trousers, { 0.12, 0.34, 0.12 }, { 0.10, 0.36, 0 }, 3, 1.2)
limb("cube", colour.boots, { 0.11, 0.10, 0.26 }, { 0.10, 0.05, 0.06 }, 0, nil, { subdivide = true })

m = save_model()
R.before_finish = { nv = m.nv, nf = m.nf }

-- knife-lite (Shift+J) on a triangle of the neck: two of its edges, mirror off
do
  local G = E.require("bm3d").G
  local nb = R.neck_base
  local tri
  for _, f in ipairs(m.faces) do
    if f[1] > nb and f[1] <= nb + 18 and f[2] > nb and f[2] <= nb + 18 and f[3] > nb and f[3] <= nb + 18 then tri = f; break end
  end
  local keys = {}
  for k in pairs(G.edge_faces({ f = m.faces })) do keys[#keys + 1] = k end
  table.sort(keys)
  local function edge_no(a, b)
    for i, k in ipairs(keys) do if k == G.edge_key(a, b) then return i end end
  end
  mode("EDGES")
  pick({ edge_no(tri[1], tri[2]), edge_no(tri[2], tri[3]) }, #keys)
  check(sees("EDGES: 2 chosen"), "knife: two edges of one triangle chosen")
  timed("knife", key, "J")
  local k = save_model()
  check(k.nv == m.nv + 2 and k.nf >= m.nf + 3 and k.nf <= m.nf + 5, "knife: two new vertices and a few triangles (" ..
        (k.nv - m.nv) .. "/" .. (k.nf - m.nf) .. "): " .. status())
  m = k
  mode("VERTICES")
end

local function near_vertex(m, f)               -- the first vertex for which f(x, y, z) holds
  for i, p in ipairs(m.verts) do if f(p[1], p[2], p[3]) then return i end end
end
local function choose_part_at(f)
  mode("VERTICES")
  pick({ near_vertex(m, f) })
  key("l")
end
local function faces_in(m, f)                  -- the faces whose three corners hold f
  local out = {}
  for i, fc in ipairs(m.faces) do
    local ok = true
    for k = 1, 3 do local p = m.verts[fc[k]]; if not f(p[1], p[2], p[3]) then ok = false end end
    if ok then out[#out + 1] = i end
  end
  return out
end
local PALETTE = { [15] = 0xC49A5E, [14] = 0x8A5A30, [22] = 0x2E6EB8, [3] = 0x404450, [7] = 0x5A2A22, [9] = 0xE84A5A }
-- hair: the faces of the top of the head (face mode), painted
do
  local top = 1.80 + 0.26 / 2 * 0.55
  local list = {}
  local head = faces_in(m, function(x, y, z) return y > 1.66 and math.abs(x) < 0.13 and math.abs(z) < 0.14 end)
  for _, fi in ipairs(head) do
    local f = m.faces[fi]
    local cy = (m.verts[f[1]][2] + m.verts[f[2]][2] + m.verts[f[3]][2]) / 3
    if cy > top then list[#list + 1] = fi end
  end
  mode("FACES")
  pick(list, m.nf)
  check(sees("FACES: " .. #list .. " chosen"), "hair: " .. #list .. " faces of the top of the head chosen")
  set_colour(colour.hair)
  timed("paint", key, "p")
  local k = save_model()
  local n = 0
  for _, f in ipairs(k.faces) do if f[4] == PALETTE[colour.hair] then n = n + 1 end end
  check(n == #list and n > 20, "hair: exactly the chosen faces are painted (" .. n .. " of " .. #list .. ")")
  -- the hair gets a thickness: the chosen faces are extruded 0.05 along their normals
  timed("extrude", function() key("x"); set_step(2); key("up"); key("\n") end)
  local e = save_model()
  check(e.nf > k.nf and e.nv > k.nv, "extrude: the hair is a new layer (" .. (e.nf - k.nf) .. " triangles, " .. (e.nv - k.nv) .. " vertices more)")
  local top0, top1 = -1e9, -1e9
  for _, p in ipairs(k.verts) do top0 = math.max(top0, p[2]) end
  for _, p in ipairs(e.verts) do top1 = math.max(top1, p[2]) end
  check(top1 > top0 + 0.03, string.format("extrude: the head is higher by the thickness (%.3f)", top1 - top0))
  k = e
  m = k
  mode("VERTICES")
  none()
end

-- the live mirror (Shift+X): paint, snap and shape on the right side, the left follows
timed("live mirror", key, "X")
check(sees("MIRROR X"), "Shift+X: the live mirror is on")
local function in_hand(x, y, z) return math.abs(math.abs(x) - 0.36) < 0.06 and y > 0.86 and y < 1.02 and math.abs(z) < 0.05 end

-- the hands are pink; the right one chosen and painted, both change
local function count_colour(m, c) local n = 0; for _, f in ipairs(m.faces) do if f[4] == c then n = n + 1 end end; return n end
local pink0, skin0 = count_colour(m, PALETTE[9]), count_colour(m, PALETTE[colour.skin])
choose_part_at(function(x, y, z) return x > 0 and in_hand(x, y, z) and y < 0.99 end)
set_colour(colour.skin)
timed("paint", key, "p")
m = save_model()
check(pink0 == 96 and count_colour(m, PALETTE[9]) == 0 and count_colour(m, PALETTE[colour.skin]) == skin0 + 96,
      "paint with the live mirror: the 96 faces of both hands change colour (" .. pink0 .. " pink before)")

-- the right foot slides forward on the grid (Shift+S); the left foot follows
local function in_foot(x, y, z) return math.abs(math.abs(x) - 0.10) < 0.07 and y < 0.11 end
do
  local lead = near_vertex(m, function(x, y, z) return x > 0 and in_foot(x, y, z) end)
  choose_part_at(function(x, y, z) return x > 0 and in_foot(x, y, z) end)
  local part = {}
  for i, p in ipairs(m.verts) do if p[1] > 0 and in_foot(p[1], p[2], p[3]) then part[#part + 1] = i end end
  lead = part[1]
  local z0 = m.verts[lead][3]
  timed("snap", key, "S")
  check(sees("SNAP grid"), "Shift+S: snap to the grid: " .. tostring(sees("SNAP grid")))
  timed("move", tool, "move", { z = 0.1 })
  key("S", "S", "S")
  check(not sees("SNAP"), "Shift+S three times: snap off")
  local k = save_model()
  local z1 = k.verts[lead][3]
  check(math.abs(z1 / 0.1 - math.floor(z1 / 0.1 + 0.5)) < 1e-3 and math.abs(z1 - z0) > 0.01,
        string.format("snap: the leader of the right foot is on the grid (z %.4f -> %.4f)", z0, z1))
  local left = find_vertex(k, -k.verts[lead][1], k.verts[lead][2], z1)
  check(left ~= nil, "the live mirror: the left foot moved the same way")
  timed("rotate", tool, "rot", { y = 10 })     -- the toes out; the left foot turns the other way
  k = save_model()
  check(math.abs(k.verts[lead][1] - m.verts[lead][1]) > 0.005 and find_vertex(k, -k.verts[lead][1], k.verts[lead][2], k.verts[lead][3]),
        "rotate: the right foot turned 10 degrees and the left foot is its mirror image")
  m = k
end

key("X")
check(not sees("MIRROR X"), "Shift+X again: the live mirror is off")

m = save_model()
R.nv, R.nf = m.nv, m.nf

-- what was built: the counters, the size, the shape, the colours
check(sees(string.format("%d of 65535 vertices  %d triangles", m.nv, m.nf)), "the counters show the model: " .. m.nv .. " vertices, " .. m.nf .. " triangles")
check(m.nf >= 1200 and m.nf <= 1800, "about 1500 triangles (1200 to 1800): " .. m.nf)
do
  local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
  for _, p in ipairs(m.verts) do
    for k = 1, 3 do lo[k] = math.min(lo[k], p[k]); hi[k] = math.max(hi[k], p[k]) end
  end
  R.box = { lo, hi }
  check(lo[2] > -0.01 and lo[2] < 0.01 and hi[2] > 1.85 and hi[2] < 2.0, string.format("the height: %.2f to %.2f", lo[2], hi[2]))
  check(hi[1] < 0.45 and lo[1] > -0.45 and math.abs(hi[1] + lo[1]) < 1e-3, "the width is symmetric: " .. string.format("%.3f %.3f", lo[1], hi[1]))
  local nan = false
  for _, p in ipairs(m.verts) do for k = 1, 3 do if p[k] ~= p[k] then nan = true end end end
  check(not nan, "no NaN in the vertices")
  -- a mirror twin for every vertex but the two the knife added (the head too: the sphere's triangles are mirror images)
  local lone = 0
  for _, p in ipairs(m.verts) do
    if not find_vertex(m, -p[1], p[2], p[3]) then lone = lone + 1 end
  end
  R.lone = lone
  check(lone <= 2, "symmetric left and right, head included: " .. lone .. " vertices have no twin (the knife makes 2)")
  -- the parts: faces look away from the middle of their part
  local parent = {}
  local function root(i) while parent[i] ~= i do parent[i] = parent[parent[i]]; i = parent[i] end; return i end
  for i = 1, m.nv do parent[i] = i end
  for _, f in ipairs(m.faces) do
    parent[root(f[1])] = root(f[2]); parent[root(f[2])] = root(f[3])
  end
  local comps, ncomp = {}, 0
  for i = 1, m.nv do
    local r = root(i)
    if not comps[r] then comps[r] = { n = 0, c = { 0, 0, 0 } }; ncomp = ncomp + 1 end
    local c = comps[r]
    c.n = c.n + 1
    for k = 1, 3 do c.c[k] = c.c[k] + m.verts[i][k] end
  end
  local bad = 0
  for _, f in ipairs(m.faces) do
    local c = comps[root(f[1])]
    local a, b, d = m.verts[f[1]], m.verts[f[2]], m.verts[f[3]]
    local u = { b[1] - a[1], b[2] - a[2], b[3] - a[3] }
    local v = { d[1] - a[1], d[2] - a[2], d[3] - a[3] }
    local n = { u[2] * v[3] - u[3] * v[2], u[3] * v[1] - u[1] * v[3], u[1] * v[2] - u[2] * v[1] }
    local mid = { (a[1] + b[1] + d[1]) / 3 - c.c[1] / c.n, (a[2] + b[2] + d[2]) / 3 - c.c[2] / c.n, (a[3] + b[3] + d[3]) / 3 - c.c[3] / c.n }
    if n[1] * mid[1] + n[2] * mid[2] + n[3] * mid[3] <= 0 then bad = bad + 1 end
  end
  R.parts, R.bad = ncomp, bad
  check(ncomp == 16, "16 parts (torso, pelvis, neck, head, 6 for each side): " .. ncomp)
  check(bad == 0, "every face looks away from the middle of its part (" .. bad .. " do not)")
  local cols, ncols = {}, 0
  for _, f in ipairs(m.faces) do if not cols[f[4]] then cols[f[4]] = true; ncols = ncols + 1 end end
  R.colours = ncols
  check(ncols >= 4, "at least 4 colours painted: " .. ncols)
end

say("model: %d vertices %d triangles in %d parts, %d colours (%d keys, %.1f s)", m.nv, m.nf, R.parts, R.colours, KEYS, os.clock() - T0)

-- saved: the file reopens with the same model
local file_mesh = section(read_file(PFILE), 8)
run_cart(MESH, PROJECT)
check(sees("1 models") or status():find("1 models", 1, true), "reopened: " .. status())
key("f2")
check(sees(string.format("%d of 65535 vertices  %d triangles", m.nv, m.nf)), "reopened: the counters show the same totals")
local again = save_model()
check(again and again.nv == m.nv and again.nf == m.nf and section(read_file(PFILE), 8) == file_mesh, "reopened and saved: the MESH section byte for byte")


----------------------------------------------------------------- bm Animator

-- "head of root: 0.000, 0.000, 0.000" on the rig page: the end that moves, the bone, where it is
local function bone_info()
  for _, t in ipairs(texts) do
    local e, n, x, y, z = t[1]:match("^(%a+) of (.-): (%-?[%d%.]+), (%-?[%d%.]+), (%-?[%d%.]+)$")
    if e then return e, n, tonumber(x), tonumber(y), tonumber(z) end
  end
end
local function the_rig()
  key("^s")
  assert(status():find("saved", 1, true), "Ctrl+S: " .. status())
  return anim_rigs(section(read_file(PFILE), 9))[NAME]
end

run_cart(ANIMATOR, PROJECT)
check(sees("778 vertices, 1492 triangles") or sees(R.nv .. " vertices, " .. R.nf .. " triangles"), "bm Animator lists the model with its counts")
key("f2")
check(sees("NO SKELETON"), "rig page: no skeleton yet")

local cur = 1                                -- the bone chosen on the rig page
local tail_end                               -- the end that moves: true tail, false head
local KEYMAP = {}                            -- the key that moves an end one step along an axis in a direction
local function end_is(t)
  if tail_end ~= t then key("\t"); tail_end = t end
end
-- the end chosen goes to (x, y, z): steps of 1/8
-- (order: the axes in the order they move; an end that comes to the other end of its bone, or to a
-- joint, takes it along from then on, so the way there must not cross one)
local function go_to(t, x, y, z, order)
  end_is(t)
  local _, _, cx, cy, cz = bone_info()
  local want, have = { x, y, z }, { cx, cy, cz }
  for _, ax in ipairs(order or { 1, 2, 3 }) do
    local diff = (want[ax] - have[ax]) * 8
    local n = diff >= 0 and math.floor(diff + 0.5) or -math.floor(-diff + 0.5)
    local sgn = n >= 0 and 1 or -1
    for _ = 1, math.abs(n) do key(KEYMAP[ax][sgn]) end
  end
  local e, name, nx, ny, nz = bone_info()
  check(math.abs(nx - x) < 1e-3 and math.abs(ny - y) < 1e-3 and math.abs(nz - z) < 1e-3,
        string.format("%s of %s at %.3f %.3f %.3f, wanted %.3f %.3f %.3f", e, name, nx, ny, nz, x, y, z))
end
local function select_bone(i)
  while cur ~= i do
    if i < cur then key("up"); cur = cur - 1 else key("down"); cur = cur + 1 end
  end
end
local function new_bone(name)
  timed("new bone", key, "n")
  R.nbones = (R.nbones or 0) + 1
  cur = R.nbones
  tail_end = true
  if name then
    key("\n")
    type_text(name)
    local _, n = bone_info()
    check(n == name, "bone named " .. name .. ": " .. tostring(n))
  end
end

-- the first bone: a skeleton of one bone from the middle of the feet up; the keys that move an end
timed("new bone", key, "n")
R.nbones, cur = 1, 1
do
  local _, _, x0, y0, z0 = bone_info()
  local e0 = bone_info()
  tail_end = e0 == "tail"
  for _, probe in ipairs({ { "d", "a" }, { "w", "s" }, { "r", "f" } }) do
    key(probe[1])
    local _, _, x1, y1, z1 = bone_info()
    local d = { x1 - x0, y1 - y0, z1 - z0 }
    for ax = 1, 3 do
      if math.abs(d[ax]) > 1e-3 then
        KEYMAP[ax] = { [1] = probe[1], [-1] = probe[2] }
        if d[ax] < 0 then KEYMAP[ax] = { [1] = probe[2], [-1] = probe[1] } end
      end
    end
    key(probe[2])
    local _, _, x2, y2, z2 = bone_info()
    check(math.abs(x2 - x0) + math.abs(y2 - y0) + math.abs(z2 - z0) < 1e-3, "a key and its opposite: back where it was")
  end
  check(KEYMAP[1] and KEYMAP[2] and KEYMAP[3], "the keys of the three axes are known")
end
go_to(false, 0, 0.875, 0)
key("\n"); type_text("pelvis")
go_to(true, 0, 1.0, 0)
-- (an end moves every joint at its point: the legs come first, while nothing else ends at 0 1.5 0)
-- a leg or an arm hangs off the end of its parent, and its head goes alone (key j) to the hip or the
-- shoulder: no stub bone, no new parent, no delete
local function chain(from_bone, head_at, bones)
  select_bone(from_bone)
  new_bone(bones[1][1])                      -- from the tail of from_bone, which is its parent
  go_to(true, bones[1][2][1], bones[1][2][2], bones[1][2][3])        -- the tail first: nothing is on its way
  end_is(false)
  key("j")                                   -- the head alone
  go_to(false, table.unpack(head_at))
  key("j")                                   -- the joints go together again
  local rgn = the_rig()
  check(rgn.bones[cur].parent == from_bone, bones[1][1] .. " still hangs from bone " .. from_bone)
  for i = 2, #bones do
    new_bone(bones[i][1])
    go_to(true, bones[i][2][1], bones[i][2][2], bones[i][2][3], bones[i][3])
  end
end
chain(1, { 0.125, 0.875, 0 }, { { "uleg.L", { 0.125, 0.5, 0 } }, { "lleg.L", { 0.125, 0.125, 0 } }, { "foot.L", { 0.125, 0.125, 0.25 }, { 3, 1, 2 } } })
select_bone(1)
new_bone("spine")                            -- 0 1 0 -> 0 1.5 0
do local _, _, _, y = bone_info(); check(math.abs(y - 1.5) < 1e-3, "the spine goes up to 1.5") end
local spine = cur
new_bone("neck");   go_to(true, 0, 1.625, 0)
new_bone("head");   go_to(true, 0, 1.875, 0)
chain(spine, { 0.375, 1.5, 0 }, { { "uarm.L", { 0.375, 1.25, 0 }, { 2, 1, 3 } }, { "larm.L", { 0.375, 1.0, 0 } }, { "hand.L", { 0.375, 0.875, 0 } } })

local rg0 = the_rig()
local WANT = {                                 -- name, parent, head, tail
  { "pelvis", 0, { 0, 0.875, 0 }, { 0, 1, 0 } }, { "uleg.L", 1, { 0.125, 0.875, 0 }, { 0.125, 0.5, 0 } },
  { "lleg.L", 2, { 0.125, 0.5, 0 }, { 0.125, 0.125, 0 } }, { "foot.L", 3, { 0.125, 0.125, 0 }, { 0.125, 0.125, 0.25 } },
  { "spine", 1, { 0, 1, 0 }, { 0, 1.5, 0 } }, { "neck", 5, { 0, 1.5, 0 }, { 0, 1.625, 0 } },
  { "head", 6, { 0, 1.625, 0 }, { 0, 1.875, 0 } }, { "uarm.L", 5, { 0.375, 1.5, 0 }, { 0.375, 1.25, 0 } },
  { "larm.L", 8, { 0.375, 1.25, 0 }, { 0.375, 1.0, 0 } }, { "hand.L", 9, { 0.375, 1.0, 0 }, { 0.375, 0.875, 0 } } }
for i, w in ipairs(WANT) do
  local b = rg0.bones[i]
  local ok = b and b.name == w[1] and b.parent == w[2]
  for k = 1, 3 do ok = ok and math.abs(b.head[k] - w[3][k]) < 1e-3 and math.abs(b.tail[k] - w[4][k]) < 1e-3 end
  check(ok, "bone " .. i .. " " .. w[1] .. " as planned")
end

-- the other side: the arm and the leg mirrored with their children (.L becomes .R)
select_bone(8)
timed("mirror bones", key, "m")
R.nbones = R.nbones + 3
select_bone(2)
timed("mirror bones", key, "m")
R.nbones = R.nbones + 3
local rg1 = the_rig()
check(rg1.nb == 16, "16 bones: " .. rg1.nb)
local names = {}
for i, b in ipairs(rg1.bones) do names[#names + 1] = b.name end
check(table.concat(names, " ") == "pelvis uleg.L lleg.L foot.L spine neck head uarm.L larm.L hand.L uarm.R larm.R hand.R uleg.R lleg.R foot.R",
      "the bones: " .. table.concat(names, " "))
do
  local b = rg1.bones[11]
  check(b.parent == 5 and math.abs(b.head[1] + 0.375) < 1e-3 and math.abs(b.head[2] - 1.5) < 1e-3, "uarm.R hangs from the spine at x = -0.375")
  b = rg1.bones[14]
  check(b.parent == 1 and math.abs(b.head[1] + 0.125) < 1e-3, "uleg.R hangs from the pelvis at x = -0.125")
end

-- the bones of the vertices: bm Mesh gives each part to its bone (APP-14). The Animator's auto skin is not
-- used (it split corners and gave a face to the nearest bone). The Animator saved the rig with every vertex
-- on bone 0, and welded the vertices that share a place (the rings where the neck meets the head, the
-- pelvis the torso: the parts are no longer apart).
local rig_pre = the_rig()
local all0 = rig_pre.nb == 16
for _, b in ipairs(rig_pre.vb) do if b ~= 0 then all0 = false end end
check(all0, "the rig is saved with 16 bones and every vertex on bone 0: " .. rig_pre.nv .. " vertices")
local bone_no = {}
for i, b in ipairs(rig_pre.bones) do bone_no[b.name] = i - 1 end

-- which bone each place of the model belongs to: from the 16 parts of the model as bm Mesh made it
local function pk(p)
  local s = string.format("%.3f,%.3f,%.3f", p[1], p[2], p[3])
  return (s:gsub("%-0%.000", "0.000"))
end
local pre = mesh_models(file_mesh)[1]
local parent_pre = {}
local function root_pre(i) while parent_pre[i] ~= i do parent_pre[i] = parent_pre[parent_pre[i]]; i = parent_pre[i] end; return i end
for i = 1, pre.nv do parent_pre[i] = i end
for _, f in ipairs(pre.faces) do parent_pre[root_pre(f[1])] = root_pre(f[2]); parent_pre[root_pre(f[2])] = root_pre(f[3]) end
local parts_pre = {}
for i = 1, pre.nv do
  local p = parts_pre[root_pre(i)]
  if not p then p = { n = 0, cx = 0, cy = 0 }; parts_pre[root_pre(i)] = p end
  p.n, p.cx, p.cy = p.n + 1, p.cx + pre.verts[i][1], p.cy + pre.verts[i][2]
end
-- by where the part is: the middle line, an arm at x = 0.36, a leg at x = 0.10 (.L is the side of x > 0)
local function bone_of_part(p)
  local cx, cy = p.cx / p.n, p.cy / p.n
  local ax = math.abs(cx)
  local name
  if ax < 0.05 then name = cy < 1.1 and "pelvis" or (cy < 1.5 and "spine" or (cy < 1.72 and "neck" or "head"))
  elseif ax > 0.2 then name = cy > 1.25 and "uarm" or (cy > 1.03 and "larm" or "hand")
  else name = cy > 0.55 and "uleg" or (cy > 0.2 and "lleg" or "foot") end
  if ax >= 0.05 then name = name .. (cx > 0 and ".L" or ".R") end
  return name
end
local at_place, names_pre = {}, {}
for i = 1, pre.nv do
  local name = bone_of_part(parts_pre[root_pre(i)])
  names_pre[name] = true
  local k = pk(pre.verts[i])
  local old = at_place[k]
  -- a place that two parts share goes to the bigger one: the torso, the head
  if not old or old == "pelvis" or old == "neck" then at_place[k] = name end
end
local npre = 0
for n in pairs(names_pre) do npre = npre + 1; check(bone_no[n], "a bone for the part " .. n) end
check(npre == 16, "16 parts, 16 different bones: " .. npre)

run_cart(MESH, PROJECT)
key("f2")
local mod2 = save_model()
check(mod2 and mod2.nv == rig_pre.nv and mod2.nf == pre.nf and mod2.nv < pre.nv, "bm Mesh reopens the rigged model, welded: " ..
      tostring(mod2 and mod2.nv) .. " vertices (" .. pre.nv .. " before)")
key("a")
check(sees("BONE 0 pelvis"), "all chosen: the header shows bone 0, pelvis")
key("a")

-- the model as the Animator saved it: its parts (linked faces) and the bone of each vertex
local expect, missing = {}, 0
for i = 1, mod2.nv do
  expect[i] = at_place[pk(mod2.verts[i])]
  if not expect[i] then missing = missing + 1 end
end
check(missing == 0, "every place of the welded model is a place of a part (" .. missing .. " are not)")
local parent2 = {}
local function root2(i) while parent2[i] ~= i do parent2[i] = parent2[parent2[i]]; i = parent2[i] end; return i end
for i = 1, mod2.nv do parent2[i] = i end
for _, f in ipairs(mod2.faces) do parent2[root2(f[1])] = root2(f[2]); parent2[root2(f[2])] = root2(f[3]) end
local parts2, order2 = {}, {}
for i = 1, mod2.nv do
  local r = root2(i)
  local p = parts2[r]
  if not p then p = { first = i, n = 0, cx = 0, groups = {} }; parts2[r] = p; order2[#order2 + 1] = p end
  p.n, p.cx = p.n + 1, p.cx + mod2.verts[i][1]
  local g = p.groups[expect[i] or "?"]
  if not g then g = {}; p.groups[expect[i] or "?"] = g end
  g[#g + 1] = i
end
-- the middle line and the .L side by hand; the live mirror gives the .R side
local work = {}
for _, p in ipairs(order2) do
  if p.cx / p.n > -0.05 then work[#work + 1] = p end
end
check(#work == 8, "8 pieces by hand (the torso with the pelvis, the neck with the head, 6 limbs on the .L side): " .. #work)

mode("VERTICES")
none()
key("X")
check(sees("MIRROR X"), "the live mirror is on: the .R side follows")
local function give(bone_name)
  key("B")
  check(sees("vertices to a bone"), "Shift+B: the bones of the skeleton are listed")
  key("pgup")
  for _ = 1, bone_no[bone_name] do key("down") end
  timed("give to bone", key, "\n")
  check(status():find("to bone " .. bone_no[bone_name] .. " " .. bone_name, 1, true), "to " .. bone_name .. ": " .. status())
end
for _, p in ipairs(work) do
  -- the biggest group of the piece is the piece's bone; a smaller group is a part that was welded to it
  local main, small = nil, {}
  for name, g in pairs(p.groups) do
    if not main or #g > #p.groups[main] then main = name end
  end
  for name in pairs(p.groups) do if name ~= main then small[#small + 1] = name end end
  table.sort(small)
  pick({ p.first }, mod2.nv)
  timed("linked", key, "l")
  check(sees("BONE"), "the header shows the bone of the piece")
  give(main)
  for _, name in ipairs(small) do
    pick(p.groups[name], mod2.nv)
    give(name)
  end
end

local mod3 = save_model()
local rig2 = anim_rigs(section(read_file(PFILE), 9))[NAME]
check(mod3.nv == mod2.nv and mod3.nf == mod2.nf, "the bones change no vertex and no triangle")
check(rig2 and rig2.nv == mod3.nv and rig2.nb == 16, "the rig fits the model: " .. rig2.nv .. " vertices, " .. mod3.nv .. " in the model")
local per_bone, wrong = {}, 0
for i = 1, 16 do per_bone[i] = 0 end
for i, b in ipairs(rig2.vb) do
  per_bone[b + 1] = per_bone[b + 1] + 1
  if b ~= bone_no[expect[i]] then wrong = wrong + 1 end
end
check(wrong == 0, "every vertex follows the bone of its part, the .R side the mirror bone (" .. wrong .. " do not)")
local empty = {}
for i = 1, 16 do if per_bone[i] == 0 then empty[#empty + 1] = rig2.bones[i].name end end
check(#empty == 0, "every bone has vertices (none for: " .. table.concat(empty, " ") .. ")")
R.skinned_nv = mod3.nv
say("bones by hand: %d vertices (%d in bm Mesh, the Animator welded the rest), vertices per bone: %s", mod3.nv, pre.nv, table.concat(per_bone, " "))

-- bm Animator keeps them and goes on to the walk
run_cart(ANIMATOR, PROJECT)
key("f2")
local rig2b = the_rig()
check(rig2b.nv == rig2.nv and table.concat(rig2b.vb, ",") == table.concat(rig2.vb, ","), "bm Animator keeps the bones of the vertices")


-- a walk: bones turned at 0 s, the pose mirrored at 0.5 s; the animation loops
key("f3")
check(sees("ANIMATIONS") or sees("NO ANIMATION") or sees("animation"), "the animate page")
key("n")
check(sees("anim1"), "n: a new animation, anim1")
local an_bone = 1
local function an_select(i)
  while an_bone ~= i do
    if i < an_bone then key("up"); an_bone = an_bone - 1 else key("down"); an_bone = an_bone + 1 end
  end
end
local function turn(i, ...)
  an_select(i)
  timed("turn bone", key, ...)
end
-- 1 pelvis 2 uleg.L 3 lleg.L 4 foot.L 5 spine 6 neck 7 head 8 uarm.L 9 larm.L 10 hand.L 11 uarm.R 12 larm.R 13 hand.R
-- 14 uleg.R 15 lleg.R 16 foot.R; w s turn around x (swing), a d around y (twist), q e around z; 15 degrees a press
turn(1, "Q")                                 -- a small sway of the pelvis (shift: 5 degrees)
turn(2, "w", "w")
turn(3, "s")
turn(5, "a")
turn(8, "s", "s")
turn(9, "w")
turn(11, "w", "w")
turn(12, "s")
turn(14, "s", "s")
turn(15, "w")
for _ = 1, 6 do key("right") end             -- 6 frames on: 0.5 s
timed("mirror pose", key, "m")
key("home")
key("\n")
type_text("walk")
local rig3 = the_rig()
local clip = rig3.clips[1]
check(rig3.nc == 1 and clip and clip.name == "walk" and clip.keys == 2 and clip.loop and math.abs(clip.length - 1) < 1e-3,
      "one animation, walk, 2 keyframes, 1 s, looping")
check(clip and math.abs(clip.times[2] - 0.5) < 1e-3, "the second keyframe at 0.5 s")

-- the player: it runs the animation for 3 s
key("f1")
key(" ")
frames(180)
key(" ")
check(true, "bm Animator played walk for 180 frames")


----------------------------------------------------------------- the model plays

-- the file as the kernel reads it, in a fresh bm Animator and a fresh bm Mesh
local final_file = read_file(PFILE)
run_cart(ANIMATOR, PROJECT)
check(sees("1 models") or sees("humanoid"), "bm Animator reopens the file")
run_cart(MESH, PROJECT)
key("f2")
local model = mesh_models(section(final_file, 8))[1]
local rig = anim_rigs(section(final_file, 9))[NAME]
check(sees(string.format("%d of 65535 vertices  %d triangles", model.nv, model.nf)),
      "bm Mesh reopens the rigged model: the counters show " .. model.nv .. " vertices, " .. model.nf .. " triangles")
check(rig.nb == 16 and rig.nv == model.nv and rig.nc == 1 and model.nf == R.nf, "the file: 16 bones, 1 animation, the vertices and triangles of the model")
check(#mesh_models(section(final_file, 8)) == 1, "one model in the file")

-- the pose of a clip at time t (the kernel's animate(), runtime.c): keys, smooth ease, loop
local function nlerp(a, b, u)
  local d = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]
  local sg = d < 0 and -1 or 1
  local q = {}
  for i = 1, 4 do q[i] = a[i] + (sg * b[i] - a[i]) * u end
  local l = math.sqrt(q[1] ^ 2 + q[2] ^ 2 + q[3] ^ 2 + q[4] ^ 2)
  return { q[1] / l, q[2] / l, q[3] / l, q[4] / l }
end
local function pose_at(clip, t)
  local L, n = clip.length, clip.keys
  if clip.loop then t = t % L else t = math.max(0, math.min(L, t)) end
  local a, b, ta, tb
  if t < clip.times[1] then a, b, ta, tb = n, 1, clip.times[n] - L, clip.times[1]
  elseif t >= clip.times[n] then a, b, ta, tb = n, 1, clip.times[n], clip.times[1] + L
  else
    local k = 1
    while k + 1 <= n and clip.times[k + 1] <= t do k = k + 1 end
    a, b, ta, tb = k, k + 1, clip.times[k], clip.times[k + 1]
  end
  local u = tb > ta and (t - ta) / (tb - ta) or 0
  if clip.mode == 1 then u = u * u * (3 - 2 * u) elseif clip.mode == 2 then u = 0 end
  local out = {}
  for i = 1, #clip.poses[a] do
    local pa, pb = clip.poses[a][i], clip.poses[b][i]
    out[i] = { q = nlerp(pa.q, pb.q, u), t = { pa.t[1] + (pb.t[1] - pa.t[1]) * u, pa.t[2] + (pb.t[2] - pa.t[2]) * u,
                                               pa.t[3] + (pb.t[3] - pa.t[3]) * u } }
  end
  return out
end
-- M[i] = M[parent] * T(head + t) * R(q) * T(-head)
local function bone_mats(bones, pose)
  local mats = {}
  for i, b in ipairs(bones) do
    local p = pose[i]
    local x, y, z, w = p.q[1], p.q[2], p.q[3], p.q[4]
    local rot = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                  2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                  2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) }
    local h = b.head
    local loc = {}
    for row = 0, 2 do
      local a, c, d = rot[row * 3 + 1], rot[row * 3 + 2], rot[row * 3 + 3]
      loc[row * 4 + 1], loc[row * 4 + 2], loc[row * 4 + 3] = a, c, d
      loc[row * 4 + 4] = h[row + 1] + p.t[row + 1] - (a * h[1] + c * h[2] + d * h[3])
    end
    local par = b.parent > 0 and mats[b.parent]
    if not par then mats[i] = loc
    else
      local m = {}
      for row = 0, 2 do
        local a0, a1, a2, a3 = par[row * 4 + 1], par[row * 4 + 2], par[row * 4 + 3], par[row * 4 + 4]
        for col = 0, 2 do m[row * 4 + col + 1] = a0 * loc[col + 1] + a1 * loc[col + 5] + a2 * loc[col + 9] end
        m[row * 4 + 4] = a0 * loc[4] + a1 * loc[8] + a2 * loc[12] + a3
      end
      mats[i] = m
    end
  end
  return mats
end
-- every vertex follows its one bone (rigid skinning)
local function skin(t)
  local mats = bone_mats(rig.bones, pose_at(rig.clips[1], t))
  local out = {}
  for i, p in ipairs(model.verts) do
    local m = mats[rig.vb[i] + 1]
    out[i] = { m[1] * p[1] + m[2] * p[2] + m[3] * p[3] + m[4], m[5] * p[1] + m[6] * p[2] + m[7] * p[3] + m[8],
               m[9] * p[1] + m[10] * p[2] + m[11] * p[3] + m[12] }
  end
  return out
end

local FRAMES, FPS_T = 60, 30                    -- two seconds at 30 frames a second: two steps of the walk
local function extent(pts)
  local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
  for _, p in ipairs(pts) do for k = 1, 3 do lo[k] = math.min(lo[k], p[k]); hi[k] = math.max(hi[k], p[k]) end end
  return lo, hi
end
local rlo, rhi = extent(model.verts)
local moved = {}                                -- the most each bone's vertices went from the rest
for b = 1, 16 do moved[b] = 0 end
local nan, changed_between, worst, sane = false, 0, 0, true
local prev, first_change
local t_play = os.clock()
for f = 0, FRAMES - 1 do
  local pts = skin(f / FPS_T)
  local lo, hi = extent(pts)
  for i, p in ipairs(pts) do
    for k = 1, 3 do if p[k] ~= p[k] or math.abs(p[k]) == math.huge then nan = true end end
    local d = math.sqrt((p[1] - model.verts[i][1]) ^ 2 + (p[2] - model.verts[i][2]) ^ 2 + (p[3] - model.verts[i][3]) ^ 2)
    local b = rig.vb[i] + 1
    if d > moved[b] then moved[b] = d end
    if d > worst then worst = d end
  end
  local h = hi[2] - lo[2]
  if h < 0.75 * (rhi[2] - rlo[2]) or h > 1.25 * (rhi[2] - rlo[2]) or lo[2] < -0.6 or math.abs(lo[1]) > 1.2 or math.abs(hi[1]) > 1.2
     or math.abs(lo[3]) > 1.2 or math.abs(hi[3]) > 1.2 then sane = false end
  if prev then
    local n = 0
    for i, p in ipairs(pts) do
      if math.abs(p[1] - prev[i][1]) + math.abs(p[2] - prev[i][2]) + math.abs(p[3] - prev[i][3]) > 1e-5 then n = n + 1 end
    end
    if f == 1 then first_change = n end
    changed_between = changed_between + (n > 0 and 1 or 0)
  end
  prev = pts
end
R.play_secs = os.clock() - t_play
local still = {}
for b = 1, 16 do if moved[b] < 0.01 then still[#still + 1] = rig.bones[b].name end end
check(#still == 0, "every bone moves its vertices (still: " .. table.concat(still, " ") .. ")")
check(not nan, "no NaN or infinity in " .. FRAMES .. " frames")
check(sane, "the box of the body stays sane in every frame (height within 25% of the rest, inside 1.2 m)")
check(changed_between == FRAMES - 1 and first_change and first_change > model.nv // 2,
      "the model plays: the skinned vertices change from frame to frame (" .. changed_between .. " of " .. (FRAMES - 1) ..
      " steps, " .. tostring(first_change) .. " of " .. model.nv .. " vertices in the first)")
do
  local a, b = skin(0), skin(1.0)
  local same = true
  for i, p in ipairs(a) do for k = 1, 3 do if math.abs(p[k] - b[i][k]) > 1e-4 then same = false end end end
  check(same, "the walk loops: the pose at 1 s is the pose at 0 s")
  local c = skin(0.5)
  local diff = 0
  for i, p in ipairs(a) do diff = math.max(diff, math.abs(p[1] - c[i][1]) + math.abs(p[2] - c[i][2]) + math.abs(p[3] - c[i][3])) end
  check(diff > 0.1, string.format("half a cycle later the legs are elsewhere (largest move %.2f)", diff))
end
R.worst = worst
say("walk: %d frames, largest move of a vertex %.2f m", FRAMES, worst)
do
  local t = {}
  for b = 1, 16 do t[#t + 1] = string.format("%s %.2f", rig.bones[b].name, moved[b]) end
  say("moved per bone (m): %s", table.concat(t, ", "))
end

----------------------------------------------------------------- the humanoid base (APP-12)

-- bm Mesh makes the base body with its skeleton from the list (F3), bm Animator opens it with the rig
do
  local P2 = "/bme/base.bme"
  local fh = assert(io.open(host(P2), "wb"))
  fh:write(pack_cart("Base", "bm", "640x360", { { 1, "function _draw() cls(0) end\n" } }))
  fh:close()
  add_file(P2)
  run_cart(MESH, P2)
  check(sees("0 models"), "base: bm Mesh opens an empty project")
  key("f3")
  check(sees("New model: humanoid"), "base: F3 lists the humanoid")
  for _ = 1, 4 do key("down") end                  -- cube, plane, sphere, cylinder, humanoid
  key("\n")
  check(status():find("new model man", 1, true) and status():find("16 bones", 1, true), "base: " .. status())
  key("^s")
  check(status():find("saved", 1, true), "base: Ctrl+S: " .. status())
  local file1 = read_file(host(P2))
  local m = mesh_models(section(file1, 8))[1]
  local r1 = anim_rigs(section(file1, 9)).man
  check(m and m.nv == 136 and m.nf == 204 and r1 and r1.nb == 16 and r1.nv == 136 and r1.nc == 0, "base: 136 vertices, 204 triangles, 16 bones, no animation yet")
  run_cart(ANIMATOR, P2)
  check(sees("136 vertices, 204 triangles"), "base: bm Animator lists the model")
  key("f2")
  check(not sees("NO SKELETON"), "base: the rig page shows a skeleton")
  local e, n, x, y, z = bone_info()
  check(n == "pelvis" and math.abs(y - 1.0) < 1e-3, "base: the first bone is the pelvis, " .. e .. " at y 1 (" .. tostring(n) .. " " .. tostring(y) .. ")")
  for _ = 1, 3 do key("down") end
  e, n, x, y, z = bone_info()
  check(n == "head" and math.abs(y - 1.875) < 1e-3, "base: the fourth bone is the head, " .. e .. " at y 1.875 (" .. tostring(n) .. " " .. tostring(y) .. ")")
  for _ = 1, 9 do key("down") end
  e, n, x, y, z = bone_info()
  check(n == "foot.L" and x > 0, "base: the thirteenth bone is foot.L, on the +x side (" .. tostring(n) .. ")")
  key("^s")
  check(status():find("saved", 1, true), "base: bm Animator saves: " .. status())
  local r2 = anim_rigs(section(read_file(host(P2)), 9)).man
  local same = r2 and r2.nb == 16 and r2.nv == 136
  for i = 1, 136 do if not r2 or r2.vb[i] ~= r1.vb[i] then same = false; break end end
  check(same, "base: bm Animator keeps the 16 bones and the bone of every vertex")
  -- the base walks: bm Animator can add a clip on top of it (a key on the rig page is not needed here: the file reads back)
  run_cart(MESH, P2)
  key("f2")
  check(sees("136 of 65535 vertices  204 triangles"), "base: bm Mesh reopens it with the skeleton")
end

for _, n in ipairs(TOOL_ORDER) do say("  %-12s %3d times %5d keys %.2f s", n, TOOLS[n].n, TOOLS[n].keys, TOOLS[n].secs) end
say("the whole build: %d keys, %d distinct tools", KEYS, #TOOL_ORDER)

io.write(string.format("humanoid host: %d/%d checks passed (%d keys, %.1f s)\n", checks - fails, checks, KEYS, os.clock() - T0))
os.exit(fails == 0 and 0 or 1)
