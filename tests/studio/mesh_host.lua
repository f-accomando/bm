-- bm Mesh (carts/mesh) on the PC: stand-ins for the bm API (a folder is
-- the SD card, nothing is drawn but the text is kept; cart_meshes() runs
-- the game's code with Lua's load as the kernel does with meshcap.c;
-- cart_write() rewrites the sections as bm_rewrite_with), keys typed into
-- it, and checks on what it shows and writes. The files it saves are read
-- again by bm Studio's parser (check_mesh.js) and the kernel's (test_bm).
--
--   luahost tests/studio/mesh_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/carts/astrowing.bm and village.bm must be there.

local ROOT, SD = arg[1] or ".", arg[2] or "build/mesh-sd"
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

local function section(data, t)
  for _, s in ipairs(sections_of(data)) do if s[1] == t then return s[2] end end
end

local function pack_cart(title, author, w, secs)
  local tab, body = {}, {}
  local pos = 128 + 16 * #secs
  for _, s in ipairs(secs) do
    tab[#tab + 1] = string.pack("<I4I4I4I4", s[1], pos, #s[2], 0)
    local pad = (4 - #s[2] % 4) % 4
    body[#body + 1] = s[2] .. string.rep("\0", pad)
    pos = pos + #s[2] + pad
  end
  local after = table.concat(tab) .. table.concat(body)
  local h = w == 320 and 180 or 360
  local head = "BMCART\0\0" .. string.pack("<I2I2I2I2BBI2I4", 1, 128, w, h, 1, #secs, 0, crc32(after))
  head = head .. (title:sub(1, 47) .. string.rep("\0", 48)):sub(1, 48) .. (author:sub(1, 31) .. string.rep("\0", 32)):sub(1, 32)
  head = head .. string.rep("\0", 128 - #head)
  return head .. after
end

-- the sections as the kernel reads them: the models and their rigs
local function mesh_models(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  assert(n >= 1, "MESH: no models")
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nv, nf = string.unpack("<I2I2", bin, pos + 16)
    assert(name ~= "" and nv >= 1 and nf >= 1, "MESH: an empty model")
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

local function anim_rigs(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nb, nc, nv = string.unpack("<I2I2I2", bin, pos + 16)
    local r = { nb = nb, nc = nc, nv = nv, vb = {}, bones = bin:sub(pos + 24, pos + 24 + nb * 44 - 1) }
    for i = 1, nv do
      r.vb[i] = bin:byte(pos + 24 + nb * 44 + i - 1)
      assert(r.vb[i] < nb, "ANIM: a vertex of no bone")
    end
    local p = pos + 24 + nb * 44 + ((nv + 3) & ~3)
    local c0 = p
    for _ = 1, nc do
      local nk = string.unpack("<I2", bin, p + 16)
      p = p + 24 + nk * (4 + nb * 28)
    end
    r.clips = bin:sub(c0, p - 1)
    out[name] = r
    pos = p
  end
  assert(pos == #bin + 1, "ANIM: the size does not add up")
  return out
end

----------------------------------------------------------------- the API

local E = {}                  -- the cartridge's globals
local sec = {}                -- the open project's MESH and ANIM (cart_load, cart_data)
local texts = {}              -- what print() wrote this frame
local keyq, pad, padprev = {}, {}, {}
local frame = 0
local quitted, ran, saved_t = false, nil, nil
local cam = { x = 0, y = 0, z = -5, yaw = 0, pitch = 0, f = 320 / math.tan(math.rad(30)) }

for k, v in pairs(_G) do E[k] = v end
E.SCREEN_W, E.SCREEN_H = 640, 360
local function nop() end
local sel_rows = {}
E.rectfill = function(x, y, w, h, c) if c == 0x3050A0 then sel_rows[#sel_rows + 1] = y end end
for _, n in ipairs({ "cls", "rect", "line", "circ", "circfill", "tri", "clip", "sspr", "spr", "camera", "zclear",
                     "light3d", "fog3d", "lamp3d", "pset", "draw3d" }) do E[n] = nop end
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
E.mesh = function(v, f, uv)
  assert(#v // 3 >= 1 and #v // 3 <= 4096 and #f // 4 >= 1 and #f // 4 <= 16384, "mesh: 1 to 4096 vertices, 1 to 16384 faces")
  for i = 1, #f // 4 do
    for k = 1, 3 do assert(f[i * 4 - 4 + k] >= 1 and f[i * 4 - 4 + k] <= #v // 3, "mesh: a vertex out of range") end
    assert(f[i * 4] ~= -1 or uv, "mesh: a textured face with no uv")
  end
  return { v = v, f = f, uv = uv }
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

local function host(path) return SD .. path:lower() end

local on_sd = { ["/carts"] = { "astrowing.bm", "village.bm" } }
E.ls = function(dir)
  local out = {}
  for _, n in ipairs(on_sd[dir] or {}) do out[#out + 1] = { name = n, size = 0, dir = false } end
  return out
end

-- the sections, with the first bm Studio's numbers as the kernel reads them
local function norm_sections(data)
  local out = {}
  for _, s in ipairs(sections_of(data)) do
    local t = s[1]
    if t == 6 and s[2]:sub(1, 4) ~= "BMAU" then t = 8 elseif t == 7 then t = 9 end
    out[#out + 1] = { t, s[2] }
  end
  return out
end

E.cart_load = function(path)
  local data = read_file(host(path))
  if not data then return nil, "no such file" end
  sec = {}
  for _, s in ipairs(norm_sections(data)) do if s[1] == 8 or s[1] == 9 then sec[s[1]] = s[2] end end
  return { title = data:sub(25, 72):match("^[^\0]*"), author = data:sub(73, 104):match("^[^\0]*"),
           res = string.unpack("<I2", data, 13) == 320 and "320x180" or "640x360", lua = section(data, 1) or "" }
end

local writes = 0
E.cart_write = function(path, t)
  local old = read_file(host(t.from or path))
  assert(old or t.lua, "a new cartridge needs its code")
  local secs = old and norm_sections(old) or {}
  local function put(kind, data)
    local out, done = {}, false
    for _, s in ipairs(secs) do
      if s[1] == kind then
        if data and not done then out[#out + 1] = { kind, data } end
        done = true
      else
        out[#out + 1] = s
      end
    end
    if data and not done then out[#out + 1] = { kind, data } end
    secs = out
  end
  if t.lua then put(1, t.lua) end
  for k, v in pairs(t.sections or {}) do
    assert(k == 8 or k == 9, "sections: only 8 and 9")
    if v then
      local ok = pcall(k == 8 and mesh_models or anim_rigs, v)
      if not ok then return false, k == 8 and "broken MESH section" or "broken ANIM section" end
    end
    put(k, v or nil)
  end
  local f = io.open(host(path), "wb")
  if not f then return false, "cannot write " .. path end
  f:write(pack_cart(old and old:sub(25, 72):match("^[^\0]*") or "", old and old:sub(73, 104):match("^[^\0]*") or "",
                    old and string.unpack("<I2", old, 13) or 640, secs))
  f:close()
  writes = writes + 1
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  local seen = false
  for _, n in ipairs(list) do if n == name:lower() then seen = true end end
  if not seen then list[#list + 1] = name:lower() end
  return true
end

-- cart_meshes(): the game's code run apart, mesh() keeps what it gets;
-- the names from the variables (the same walk as src/bm/meshcap.c)
local API = {}
do
  local src = read_file(ROOT .. "/src/bm/runtime.c")
  local tab = src:match("static const luaL_Reg api%[%] = {(.-)%{ NULL, NULL }")
  for n in tab:gmatch('{ "([%w_]+)"') do API[#API + 1] = n end
end
local captured = {}           -- the last capture, for the checks

E.cart_meshes = function(path)
  local lua = section(read_file(host(path)), 1)
  local env, caps, MT = {}, {}, {}
  for _, n in ipairs(API) do env[n] = function() end end
  for _, n in ipairs({ "math", "string", "table", "utf8", "coroutine", "pairs", "ipairs", "type", "tostring",
                       "tonumber", "select", "next", "error", "pcall", "assert", "setmetatable", "getmetatable",
                       "rawget", "rawset", "rawlen", "rawequal", "xpcall" }) do env[n] = _G[n] end
  env._G, env.SCREEN_W, env.SCREEN_H = env, 640, 360
  for i, w in ipairs({ "SQUARE", "TRIANGLE", "SAW", "NOISE", "SINE", "METAL" }) do env[w] = i - 1 end
  local function keep(m)
    for i, o in ipairs(caps) do
      if table.concat(o.verts, ",") == table.concat(m.verts, ",") and table.concat(o.faces, ",") == table.concat(m.faces, ",") then
        return setmetatable({ id = i }, MT)
      end
    end
    caps[#caps + 1] = m
    return setmetatable({ id = #caps }, MT)
  end
  local function copy(t) local o = {}; for i = 1, #t do o[i] = t[i] end; return o end
  env.mesh = function(v, f, uv) return keep({ kind = "mesh", verts = copy(v), faces = copy(f), uv = uv and copy(uv) }) end
  env.mesh_cube = function(c)
    return keep({ kind = "cube", verts = { -1, -1, -1, 1, -1, -1, 1, 1, -1 }, faces = { 1, 2, 3, c or 0xFFFFFF } })
  end
  env.mesh_sphere = function(r, s, c1)
    return keep({ kind = "sphere", verts = { 0, 1, 0, 1, 0, 0, 0, 0, (r or 8) * 0.01 }, faces = { 1, 2, 3, c1 or 0xFFFFFF } })
  end
  env.rgb = function(r, g, b) return (r or 0) << 16 | (g or 0) << 8 | (b or 0) end
  for _, n in ipairs({ "time", "stat", "print", "mget", "pget", "sget", "animate" }) do env[n] = function() return 0 end end
  env.project3d = function() return 320, 180, 1 end
  env.players = function() return 1, 1 end
  env.stick = function() return 0, 0 end
  env.bone3d = function() return 0, 0, 0, 0, 0, 0 end
  env.bounds3d = env.bone3d
  env.clips = function() return {} end
  env.ls = env.clips
  env.models = env.clips
  env.require = function() return setmetatable({}, { __index = function() return function() end end }) end
  local err
  local function run(f)
    local ok, e = pcall(f)
    if not ok and not err then err = tostring(e) end
  end
  local chunk, e = load(lua, "=main.lua", "t", env)
  if not chunk then return {}, e end
  run(chunk)
  for _, n in ipairs({ "_init", "_update", "_draw" }) do if type(env[n]) == "function" then run(env[n]) end end
  local names, seen, queue = {}, {}, {}
  local function visit(v, name, inarray)
    if type(v) == "table" then
      if getmetatable(v) == MT then
        if not names[v.id] then names[v.id] = name end
        return
      end
      if not seen[v] then seen[v] = true; queue[#queue + 1] = { v, name, inarray } end
    elseif type(v) == "function" and not seen[v] then
      seen[v] = true
      for i = 1, 255 do
        local un, uv = debug.getupvalue(v, i)
        if not un then break end
        if un ~= "_ENV" then visit(uv, un) end
      end
    end
  end
  seen[env] = true
  queue[1] = { env, "" }
  local i = 1
  while i <= #queue do
    local t, base, inarray = queue[i][1], queue[i][2], queue[i][3]
    i = i + 1
    for k, v in next, t do
      if type(k) == "string" then visit(v, inarray and base .. "_" .. k or k)
      elseif math.type(k) == "integer" then visit(v, base .. k, true) end
    end
  end
  for n, m in ipairs(caps) do m.name = names[n] or (m.kind .. n) end
  captured = caps
  return caps, err
end

----------------------------------------------------------------- driving it

local chunk = assert(loadfile(ROOT .. "/carts/mesh/main.lua", "t", E))
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

local function type_text(t)
  for _ = 1, 40 do key("\b") end
  for c in t:gmatch(".") do key(c) end
  key("\n")
end

-- the row of the list chosen (the left panel)
local function chosen()
  for _, y in ipairs(sel_rows) do
    local kind, name
    for _, t in ipairs(texts) do
      if t[3] == y and t[2] == 8 then kind = t[1] end
      if t[3] == y and t[2] == 24 then name = t[1] end
    end
    if kind then return kind .. " " .. name:gsub("%*$", "") end
  end
end

local function goto_item(label)
  key("home")
  for _ = 1, 40 do
    if chosen() == label then return true end
    key("down")
  end
  check(false, "the list has " .. label)
end

local function file(name) return read_file(SD .. "/carts/" .. name) end
local function models_of(name) return mesh_models(section(file(name), 8)) end
local function model_of(name, m)
  for _, x in ipairs(models_of(name)) do if x.name == m then return x end end
end

-- the faces look out of their middle (a convex shape)
local function outward(m)
  local c = { 0, 0, 0 }
  for _, p in ipairs(m.verts) do for k = 1, 3 do c[k] = c[k] + p[k] / m.nv end end
  for _, f in ipairs(m.faces) do
    local a, b, d = m.verts[f[1]], m.verts[f[2]], m.verts[f[3]]
    local u = { b[1] - a[1], b[2] - a[2], b[3] - a[3] }
    local v = { d[1] - a[1], d[2] - a[2], d[3] - a[3] }
    local n = { u[2] * v[3] - u[3] * v[2], u[3] * v[1] - u[1] * v[3], u[1] * v[2] - u[2] * v[1] }
    local mid = { (a[1] + b[1] + d[1]) / 3 - c[1], (a[2] + b[2] + d[2]) / 3 - c[2], (a[3] + b[3] + d[3]) / 3 - c[3] }
    if n[1] * mid[1] + n[2] * mid[2] + n[3] * mid[3] <= 0 then return false end
  end
  return true
end

----------------------------------------------------------------- the scenario

local astro0 = file("astrowing.bm")
local village0 = file("village.bm")
local lua0 = section(astro0, 1)

E._init()
frames(2)
check(sees("open a cartridge") and sees("/carts/astrowing.bm"), "at the start: the list of the files")
key("\n")
check(status():find("0 models, 0 code meshes, 13 from the game's code", 1, true), "Astro Wing: " .. status())
check(chosen() == "G ship" and sees("built by the game's code"), "the first mesh: the ship, from the game's code")
check(sees("30 vertices, 32 triangles"), "the ship: 30 vertices, 32 triangles")

-- mesh -> model
key("m")
check(chosen() == "M ship" and status():find('model("ship")', 1, true), "m: a copy as the model ship: " .. status())
key("^s")
check(status():find("saved /carts/astrowing.bm", 1, true), "Ctrl+S: " .. status())
local ship = model_of("astrowing.bm", "ship")
check(ship and ship.nv == 30 and ship.nf == 32, "the file has the model ship, 30 vertices and 32 triangles")
local cap_ship = captured[1]
local same = ship and true
for i = 1, 30 do
  for k = 1, 3 do
    if same and math.abs(ship.verts[i][k] - cap_ship.verts[i * 3 - 3 + k]) > 1e-5 then same = false end
  end
end
for i = 1, 32 do
  for k = 1, 4 do
    local want = cap_ship.faces[i * 4 - 4 + k] - (k < 4 and 1 or 0)
    local got = ship and (k < 4 and ship.faces[i][k] - 1 or ship.faces[i][4])
    if same and got ~= want then same = false end
  end
end
check(same, "the model's vertices and faces are the ones the game's code gives mesh()")
local astro1 = file("astrowing.bm")
check(section(astro1, 1) == lua0 and section(astro1, 2) == section(astro0, 2) and section(astro1, 9) == nil,
      "the code and the sheet stay as they were")

-- mesh -> code
goto_item("G ship")
key("c")
check(chosen() == "C ship" and status():find("mesh_ship()", 1, true), "c: a copy as code: " .. status())
key("^s")
local lua1 = section(file("astrowing.bm"), 1)
check(lua1:sub(1, #lua0) == lua0 and lua1:find("function mesh_ship()", 1, true) and lua1:find("-- [bm Mesh end]", 1, true),
      "the code: the game's, then mesh_ship() in the bm Mesh block")
do
  local got
  local env = { mesh = function(v, f, uv) got = { v = v, f = f, uv = uv }; return got end }
  local block = lua1:sub(#lua0 + 1)
  local fn = load(block, "=block", "t", env)
  check(fn ~= nil, "the block is Lua")
  if fn then fn(); env.mesh_ship() end
  local ok = got and #got.v == 90 and #got.f == 128
  for i = 1, 90 do if ok and math.abs(got.v[i] - cap_ship.verts[i]) > 1e-5 then ok = false end end
  for i = 1, 128 do if ok and got.f[i] ~= cap_ship.faces[i] then ok = false end end
  check(ok, "mesh_ship() gives mesh() the ship's vertices and faces")
end

-- open again: the model, the code mesh, and the 13 of the game's code
key("esc")
check(sees("bm Mesh") and sees("Open..."), "Esc: the menu")
key("down", "\n", "\n")
-- (the game's ship is the code mesh's: shown once)
check(status():find("1 models, 1 code meshes, 12 from the game's code", 1, true), "open again: " .. status())

-- edit the model: all its vertices 0.2 up, then undo
goto_item("M ship")
key("f2")
check(sees("VERTICES: 0 chosen"), "F2: the edit page, vertices")
key("a")
check(sees("VERTICES: 30 chosen"), "a: all 30 vertices")
key("g", "up", "up")
check(sees("MOVE") and sees("y 0.2"), "g, up, up: moving 0.2 up")
key("\n", "^s")
local up = model_of("astrowing.bm", "ship")
local moved = up and up.nv == 30
for i = 1, 30 do if moved and math.abs(up.verts[i][2] - ship.verts[i][2] - 0.2) > 1e-5 then moved = false end end
check(moved, "saved: every vertex of the ship 0.2 higher")
key("^z", "^s")
check(section(file("astrowing.bm"), 8) == section(astro1, 8), "Ctrl+Z: the ship as before, byte for byte")
key("^y")
check(sees("redo"), "Ctrl+Y: redo")
key("g", "pgup", "\b", "esc")
check(sees("cancelled"), "a tool cancelled with Esc")

-- the tools on a cube: faces in 4, mirror, delete one, a new face, merge
key("f1", "n")
check(chosen() == "M cube", "n: a new model, a cube")
key("f2", "\t", "a")
check(sees("FACES: 12 chosen"), "Tab, a: the 12 faces of the cube")
key("u")
key("K")
check(status():find("0 vertices welded", 1, true), "K: nothing to weld after the subdivision: " .. status())
key("^s")
local cube = model_of("astrowing.bm", "cube")
check(cube and cube.nf == 48 and cube.nv == 26, "u: 48 faces, 26 vertices (" .. (cube and cube.nf .. ", " .. cube.nv or "-") .. ")")
check(cube and outward(cube), "the faces of the cube still look out")
key("\t")
check(sees("VERTICES: 26 chosen"), "Tab: the corners of the chosen faces")
key("m", "^s")
cube = model_of("astrowing.bm", "cube")
check(cube and outward(cube), "m: mirrored, the faces still look out")
key("\t", "a", "n", " ", "del", "^s")
cube = model_of("astrowing.bm", "cube")
check(cube and cube.nf == 47 and cube.nv == 26, "a face deleted: 47 (" .. (cube and cube.nf or "-") .. ")")
key("\t", "n", " ", "n", " ", "n", " ")
check(sees("VERTICES: 3 chosen"), "n and space: three vertices")
key("j", "^s")
cube = model_of("astrowing.bm", "cube")
check(cube and cube.nf == 48, "j: a new face on them (" .. (cube and cube.nf or "-") .. ")")
key("a", "n", " ", "n", " ", "k", "^s")
cube = model_of("astrowing.bm", "cube")
check(cube and cube.nv == 25, "k: two vertices merged into one (" .. (cube and cube.nv or "-") .. ")")
key("\t", "a", "a", "c", "right", "\n", "p", "^s")
cube = model_of("astrowing.bm", "cube")
local pink = cube ~= nil
for _, f in ipairs(cube and cube.faces or {}) do if f[4] ~= 0xE890B0 then pink = false end end
check(pink, "c, right, Enter, p: every face painted #E890B0")

-- a plane pulled up into a box with no bottom
key("f1", "f3")
check(sees("New model: plane"), "F3: the commands of the list")
for _ = 1, 7 do key("down") end
key("\n")
check(chosen() == "M plane", "a new plane")
key("f2", "\t", "a", "x")
for _ = 1, 5 do key("up") end
check(sees("along the normal") and sees("y 0.5"), "x: extruding along the normal, 0.5")
key("\n", "^s")
local plane = model_of("astrowing.bm", "plane")
local top = 0
for _, p in ipairs(plane and plane.verts or {}) do if math.abs(p[2] - 0.5) < 1e-6 then top = top + 1 end end
check(plane and plane.nf == 10 and plane.nv == 8 and top == 4, "the plane extruded: 10 faces, 4 corners up")
check(plane and outward(plane), "its sides look out")

-- duplicate and mirror copy
key("a", "a", "d", "right", "\n", "^s")
plane = model_of("astrowing.bm", "plane")
check(plane and plane.nf == 20 and plane.nv == 16, "d: a copy of the faces (" .. (plane and plane.nf or "-") .. ")")
key("a", "a", "M", "^s")
plane = model_of("astrowing.bm", "plane")
check(plane and plane.nf == 40, "M: copied across x = 0 (" .. (plane and plane.nf or "-") .. ")")

-- rename, model -> code, delete
key("f1")
goto_item("M ship")
key("r")
check(sees("model name"), "r asks the name")
type_text("hero")
check(chosen() == "M hero", "renamed: hero")
key("c")
check(chosen() == "C hero", "model -> code: mesh_hero()")
key("^s")
local lua2 = section(file("astrowing.bm"), 1)
check(lua2:find("function mesh_hero()", 1, true) and lua2:find("function mesh_ship()", 1, true), "two code meshes")
check(model_of("astrowing.bm", "hero") and not model_of("astrowing.bm", "ship"), "the model is now hero")
goto_item("C hero")
key("del")
check(status():find("again", 1, true), "Del asks again")
key("del")
goto_item("C ship")
key("del", "del", "^s")
check(section(file("astrowing.bm"), 1) == lua0, "no code meshes left: the code is the game's again, byte for byte")
key("f5")
check(ran == "/carts/astrowing.bm", "F5: the game runs")

-- the village: a model with a skeleton keeps it
key("esc", "down", "\n")
for _ = 1, 3 do key("down") end
key("\n")
check(status():find("8 models", 1, true), "the village: " .. status())
goto_item("M villager")
check(sees("skeleton: 7 bones, 3 animations"), "the villager has its skeleton")
local rig0 = anim_rigs(section(village0, 9)).villager
local vil0 = model_of("village.bm", "villager")
key("f2", "a", "g", "up", "\n", "^s")
local rig1 = anim_rigs(section(file("village.bm"), 9)).villager
check(rig1 and rig1.nv == 112 and rig1.bones == rig0.bones and rig1.clips == rig0.clips and
      table.concat(rig1.vb, ",") == table.concat(rig0.vb, ","), "moved: the skeleton is the same")
key("a", "n", " ", "del", "^s")
local vil = model_of("village.bm", "villager")
local rig2 = anim_rigs(section(file("village.bm"), 9)).villager
check(vil and rig2 and vil.nv < 112 and rig2.nv == vil.nv, "a vertex deleted: the skeleton has the new vertices (" ..
      (vil and vil.nv or "-") .. ")")
local bones_ok = vil ~= nil
for i, p in ipairs(vil and vil.verts or {}) do
  local found = false
  for j, q in ipairs(vil0.verts) do
    if not found and math.abs(p[1] - q[1]) < 1e-5 and math.abs(p[2] - q[2] - 0.1) < 1e-5 and math.abs(p[3] - q[3]) < 1e-5 and
       rig2.vb[i] == rig0.vb[j] then found = true end
  end
  if not found then bones_ok = false end
end
check(bones_ok, "every vertex left follows the same bone as before")
for _, n in ipairs({ "ground", "house", "tree", "well" }) do
  local a, b = model_of("village.bm", n), nil
  for _, x in ipairs(mesh_models(section(village0, 8))) do if x.name == n then b = x end end
  check(a and b and a.nv == b.nv and a.nf == b.nf, "the other models stay: " .. n)
end
local v1 = file("village.bm")
check(section(v1, 1) == section(village0, 1) and section(v1, 5) == section(village0, 5) and
      section(v1, 4) == section(village0, 4), "the code, the sheet and the cover stay")

-- save as a copy
key("esc")
for _ = 1, 3 do key("down") end
key("\n")
check(sees("file name"), "Save as asks a name")
type_text("MESHCOPY")
check(status():find("saved /carts/MESHCOPY.BM", 1, true), "saved as: " .. status())
local cp = file("meshcopy.bm")
check(cp and section(cp, 8) == section(v1, 8) and section(cp, 9) == section(v1, 9) and section(cp, 5) == section(v1, 5),
      "the copy has the same models, skeletons and sheet")

key("?")
check(sees("copy as a model: mesh -> model"), "?: the keys of the list")
key("x")
check(not sees("copy as a model: mesh -> model"), "any key closes them")
key("f2", "?")
check(sees("extrude faces"), "?: the keys of the edit page")
key("x")

io.write(string.format("bm Mesh: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
