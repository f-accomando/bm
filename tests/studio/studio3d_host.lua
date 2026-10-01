-- The 3D studio (carts/studio3d) on the PC: stand-ins for the bm API (a
-- folder is the SD card, the sections are strings, nothing is drawn but
-- the text is kept), keys typed into it, and checks on what it shows and
-- writes. The files it saves are read again by bm Studio's parser
-- (check_studio3d.js) and the kernel's (test_bm).
--
--   luahost tests/studio/studio3d_host.lua ROOT SDDIR     (make test-studio)
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

----------------------------------------------------------------- the API

local E = {}                  -- the cartridge's globals
local sec = {}                -- the project's sections by type (6, 7, others)
local sheet = { w = 256, h = 256, px = {} }
local texts = {}              -- what print() wrote this frame
local keyq, pad, padprev = {}, {}, {}
local frame = 0
local quitted, ran, saved_t = false, nil, nil
local cam = { x = 0, y = 0, z = -5, yaw = 0, pitch = 0, f = 320 / math.tan(math.rad(30)) }
local draws = 0

for k, v in pairs(_G) do E[k] = v end
E.SCREEN_W, E.SCREEN_H = 640, 360
local function nop() end
local sel_rows = {}
E.rectfill = function(x, y, w, h, c) if c == 0x3050A0 then sel_rows[#sel_rows + 1] = y end end
for _, n in ipairs({ "cls", "rect", "line", "circ", "circfill", "tri", "clip", "sspr", "spr",
                     "camera", "zclear", "light3d", "fog3d", "lamp3d", "pset" }) do E[n] = nop end
E.print = function(s, x, y) texts[#texts + 1] = { tostring(s), x or 0, y or 0 }; return (x or 0) + #tostring(s) * 8 end
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
E.cart_arg = function() return nil end
E.cart_run = function(p) ran = p end
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

E.sget = function(x, y)
  if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return nil end
  return sheet.px[y * sheet.w + x]
end
E.sset = function(x, y, c)
  if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return end
  sheet.px[y * sheet.w + x] = c
end

-- the sections as the kernel reads them: the models and their rigs
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
      r.bones[i] = { name = bin:sub(b, b + 15):match("^[^\0]*"), head = { hx, hy, hz }, tail = { tx, ty, tz } }
    end
    for i = 1, nv do assert(bin:byte(pos + 24 + nb * 44 + i - 1) < nb, "ANIM: a vertex of no bone") end
    local p = pos + 24 + nb * 44 + ((nv + 3) & ~3)
    for c = 1, nc do
      local nk, mode, flags, length = string.unpack("<I2BBf", bin, p + 16)
      assert(nk >= 1 and mode <= 2 and length > 0, "ANIM: a bad clip")
      local prev = 0
      for k = 0, nk - 1 do
        local t = string.unpack("<f", bin, p + 24 + k * (4 + nb * 28))
        assert(t >= prev and t <= length + 1e-4, "ANIM: keys out of order")
        prev = t
      end
      r.clips[c] = { name = bin:sub(p, p + 15):match("^[^\0]*"), length = length, loop = flags & 1 == 1, keys = nk }
      p = p + 24 + nk * (4 + nb * 28)
    end
    out[name] = r
    pos = p
  end
  assert(pos == #bin + 1, "ANIM: the size does not add up")
  return out
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

local MESH_MT = {}
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
E.mesh = function(v, f)
  assert(#v // 3 <= 4096 and #f // 4 <= 16384, "mesh: too big")
  return setmetatable({ v = v, f = f }, MESH_MT)
end
E.draw3d = function(m) assert(getmetatable(m) == MESH_MT); draws = draws + 1 end
E.clips = function(m)
  local out = {}
  for i, c in ipairs(m.rig and m.rig.clips or {}) do out[i] = { name = c.name, length = c.length, loop = c.loop } end
  return out
end
E.animate = function(m, c, t)
  assert(m.rig, "this mesh has no skeleton (make one with bm Animator)")
  if c then
    local found = type(c) == "number" and m.rig.clips[c]
    if not found then for _, cc in ipairs(m.rig.clips) do if cc.name == c then found = cc end end end
    assert(found, "no animation " .. tostring(c))
    return found.length
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

local function host(path) return SD .. path:gsub("^/", "/"):lower() end

-- the files on the "card": the village, and what the studio saves
local on_sd = { ["/carts"] = { "village.bm" } }
E.ls = function(dir)
  local out = {}
  for _, n in ipairs(on_sd[dir] or {}) do out[#out + 1] = { name = n, size = 0, dir = false } end
  return out
end

local proj_extra = {}
E.cart_load = function(path)
  local data = read_file(host(path))
  if not data then return nil, "no such file" end
  sec, proj_extra = {}, {}
  sheet = { w = 256, h = 256, px = {} }
  for _, s in ipairs(sections_of(data)) do
    -- MESH 8, ANIM 9 (6 that is not a sound bank, and 7: the first bm Studio files)
    local t = s[1]
    if t == 6 and s[2]:sub(1, 4) ~= "BMAU" then t = 8 elseif t == 7 then t = 9 end
    if t == 8 or t == 9 then sec[t] = s[2]
    elseif s[1] == 2 then
      local w, h = string.unpack("<I2I2", s[2])
      sheet = { w = w, h = h, px = {} }
      for i = 0, w * h - 1 do
        local r, g, b, a = s[2]:byte(5 + i * 4, 8 + i * 4)
        if a >= 128 then sheet.px[i] = r << 16 | g << 8 | b end
      end
    elseif s[1] ~= 1 and s[1] ~= 3 and s[1] ~= 4 and s[1] ~= 5 then proj_extra[#proj_extra + 1] = s end
  end
  local title = data:sub(25, 72):match("^[^\0]*")
  local author = data:sub(73, 104):match("^[^\0]*")
  local lua
  for _, s in ipairs(sections_of(data)) do if s[1] == 1 then lua = s[2] end end
  return { title = title, author = author, res = string.unpack("<I2", data, 13) == 320 and "320x180" or "640x360",
           lua = lua or "", sheet_w = sheet.w, sheet_h = sheet.h, map_w = 256, map_h = 256 }
end
E.cart_new = function()
  sec, proj_extra = {}, {}
  sheet = { w = 256, h = 256, px = {} }
end
E.cart_save = function(path, t)
  local secs = { { 1, t.lua } }
  local px = {}
  for i = 0, sheet.w * sheet.h - 1 do
    local c = sheet.px[i]
    px[#px + 1] = c and string.char(c >> 16 & 255, c >> 8 & 255, c & 255, 255) or "\0\0\0\0"
  end
  secs[#secs + 1] = { 2, string.pack("<I2I2", sheet.w, sheet.h) .. table.concat(px) }
  if sec[8] then secs[#secs + 1] = { 8, sec[8] } end
  if sec[9] then secs[#secs + 1] = { 9, sec[9] } end
  for _, s in ipairs(proj_extra) do secs[#secs + 1] = s end
  local f = io.open(host(path), "wb")
  if not f then return false, "cannot write " .. path end
  f:write(pack_cart(t.title, t.author, t.res, secs))
  f:close()
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  local seen = false
  for _, n in ipairs(list) do if n == name:lower() then seen = true end end
  if not seen then list[#list + 1] = name:lower() end
  return true
end

----------------------------------------------------------------- driving it

local chunk = assert(loadfile(ROOT .. "/carts/studio3d/main.lua", "t", E))
chunk()

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

----------------------------------------------------------------- the scenario

E._init()
frames(2)
check(sees("open a cartridge"), "at the start: the list of the files")
check(sees("/carts/village.bm"), "the village is in the list")

-- open the village: 8 models; the player shows the villager's animations
local list = E.ls("/carts")
table.sort(list, function(a, b) return a.name < b.name end)
local idx = 0
for i, f in ipairs(list) do if f.name == "village.bm" then idx = i end end
for _ = 2, idx do key("down") end
key("\n")
check(#cur_models() == 8, "the village: 8 models, " .. #cur_models())
check(sees("F1 play") and sees("MODELS"), "the player")
local orig6, orig7 = sec[8], sec[9]
-- choose the villager
local names = {}
for i, m in ipairs(cur_models()) do names[i] = m.name end
local vi = 0
for i, n in ipairs(names) do if n == "villager" then vi = i end end
for _ = 2, vi do key("down") end
check(sees("villager") and sees("ANIMATIONS") and sees("idle"), "the villager and its animations")
key("right")
check(sees("walk"), "right: the next animation (walk)")
key("k", "b")
check(sees("mixed with wave: 25%"), "b: mixed with the next one")

-- save as a copy: the sections come back the same, byte for byte
key("esc")
check(sees("bm 3D studio") and sees("Save as..."), "Esc: the menu")
for _ = 1, 4 do key("down") end
key("\n")
check(sees("file name"), "Save as asks a name")
for _ = 1, 12 do key("\b") end
for c in ("COPY3D"):gmatch(".") do key(c) end
key("\n")
check(status():find("saved /carts/COPY3D.BM", 1, true), "saved: " .. status())
local copy = read_file(SD .. "/carts/copy3d.bm")
local got = {}
for _, s in ipairs(sections_of(copy)) do got[s[1]] = s[2] end
check(got[8] == orig6 and got[9] == orig7, "a copy with no edits keeps MESH and ANIM as they were")


local function chosen_item()
  for _, y in ipairs(sel_rows) do
    for _, t in ipairs(texts) do if t[3] == y and t[2] == 32 then return t[1] end end
  end
end

local function menu_pick(label)
  if not sees("Exit 3D studio") then key("esc") end
  for _ = 1, 15 do
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

local function m1() return cur_models()[1] or { nf = 0, nv = 0, cols = {} } end
local function rig1() return anim_rigs(sec[9])[m1().name] end

-- a new project: blocks
menu_pick("New project")
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

-- tiles: a floor and a wall in the next cell, then paint the floor
key("right", "2", " ")
check(m1().nf == 14, "a floor tile (" .. m1().nf .. ")")
key("f", " ")
check(sees("far wall") and m1().nf == 16, "a tile on the far wall (" .. m1().nf .. ")")
key("\t")
check(sees("tiles: arrows choose"), "Tab: the tiles of the sheet")
key("c")
check(sees("colours:"), "c: the colours")
for _ = 1, 8 do key("right") end                       -- the ninth colour, 0xE84A5A
key("\n")
key("3", "f", "f", "f", "f", "f", "f")                 -- paint, on the floor
key(" ")
local red = 0
for _, c in ipairs(m1().cols) do if c == 0xE84A5A then red = red + 1 end end
check(red == 2, "paint: the floor tile is red (" .. red .. " triangles)")
key("^z")
red = 0
for _, c in ipairs(m1().cols) do if c == 0xE84A5A then red = red + 1 end end
check(red == 0, "undo the paint")
key("^y")

-- a skeleton: root, then a child; its tail moves; auto skin
key("f3")
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

-- an animation: a turn at 0.25 s is a keyframe
key("f4")
check(sees("no animations yet"), "animate: no animations yet")
key("n")
check(#rig1().clips == 1 and rig1().clips[1].keys == 1, "n: an animation with one keyframe")
key("right", "right", "right")
check(sees("0.25 s"), "right x3: a quarter of a second")
key("w")
check(rig1().clips[1].keys == 2, "w: the bone turns, a keyframe at 0.25 s")
key(">")
check(math.abs(rig1().clips[1].length - (1 + 1 / 12)) < 1e-5, "> a frame longer")
key("l")
check(rig1().clips[1].loop == false, "l: no loop")

-- the player shows it
key("f1")
check(sees("anim1"), "the player: the new animation")

-- models: duplicate, rename, delete
menu_pick("Duplicate model")
check(#cur_models() == 2 and cur_models()[2].name == "model2", "duplicate: model2")
check(anim_rigs(sec[9])["model2"] ~= nil, "the copy has the skeleton too")
menu_pick("Rename model")
type_text("tower")
check(cur_models()[2].name == "tower" and anim_rigs(sec[9])["tower"] ~= nil, "renamed tower, with its skeleton")
menu_pick("Delete model")
menu_pick("Delete model")
check(#cur_models() == 1, "deleted (asked twice)")

-- save, then open it again
menu_pick("Save as")
type_text("BLOCKS")
check(status():find("saved /carts/BLOCKS.BM", 1, true), "saved: " .. status())
local before6, before7 = sec[8], sec[9]
menu_pick("Open")
for _ = 1, 10 do key("up") end
local names2 = {}
for _, f in ipairs(E.ls("/carts")) do names2[#names2 + 1] = f.name end
table.sort(names2)
for i, n in ipairs(names2) do if n == "blocks.bm" then for _ = 2, i do key("down") end end end
key("\n")
check(sees("opened /carts/blocks.bm"), "opened again: " .. status())
check(sec[8] == before6 and sec[9] == before7, "the same models and skeleton as saved")
key("f2")
check(sees("model: 8 faces"), "the build page: 8 faces (the tiles whole again)")

-- the gamepad: buttons held together for a few frames, then let go
local function buttons(...)
  for _, b in ipairs({ ... }) do pad[b] = true end
  frames(3)
  pad = {}
  frames(3)
end
key("1")
buttons(4)
check(sees("model: 14 faces"), "pad A: a block in front of the model")
buttons(5)
check(sees("model: 8 faces"), "pad B: the block goes")
buttons(7)
check(sees("colours: arrows choose"), "pad Y (alone): the picker (on the colours, as it was left)")
buttons(5)
check(sees("model: 8 faces"), "pad B: back to the build page")
pad[7] = true; frames(2); pad[0] = true; frames(2); pad = {}; frames(3)
check(sees("MODELS") and sees("F1 play"), "pad Y + left: the page before, play")
pad[7] = true; frames(2); pad[5] = true; frames(2); pad = {}; frames(3)
check(sees("Exit 3D studio"), "pad Y + B: the menu")
buttons(5)
check(sees("MODELS"), "pad B: back where it was")

-- more of the animation: keys, poses, clips; a bone deleted and back
key("f4")
check(sees("anim1  1/1"), "animate: anim1")
key("n")
check(#rig1().clips == 2 and sees("anim2  2/2"), "n: a second animation, anim2")
key("pgup")
check(sees("anim1  1/2"), "PgUp: back to anim1")
key("right", "right", "right")
key("c")
check(sees("pose copied"), "c: the pose at 0.25 s copied")
key("end")
key("v")
check(rig1().clips[1].keys == 3, "v at the end: a third keyframe (" .. rig1().clips[1].keys .. ")")
key("x")
check(rig1().clips[1].keys == 2, "x: that keyframe deleted")
key("home", "x")
check(rig1().clips[1].keys == 1, "x at 0: one keyframe left")
key("right", "right", "right", "x")
check(rig1().clips[1].keys == 1 and sees("the last keyframe stays"), "the last keyframe stays")
key("m")
check(sees("anim1  1/2  step"), "m m: step")
key("\b")
check(sees("Backspace again"), "Backspace asks first")
key("\b")
check(#rig1().clips == 1 and rig1().clips[1].name == "anim2", "Backspace twice: anim1 deleted")
key("f3", "down")
check(sees("tail of bone2"), "rig: bone2")
key("x")
check(#rig1().bones == 1, "x: bone2 deleted")
key("^z")
check(#rig1().bones == 2, "undo: bone2 back")
key("v")
check(sees("the bone of each face"), "v: the skin colours")
key("up", "x")
key("x")
check(rig1() == nil and sees("NO SKELETON"), "deleting the last bones: no skeleton")
key("f1")
check(not sees("ANIMATIONS"), "the player: no animations now")

io.write(string.format("studio3d host: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
