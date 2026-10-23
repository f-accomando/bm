-- bm Mesh (carts/mesh) on the PC: stand-ins for the bm API (a folder is
-- the SD card, nothing is drawn but the text is kept; cart_meshes() runs
-- the game's code with Lua's load as the kernel does with meshcap.c;
-- cart_write() rewrites the sections as bm_rewrite_with), keys typed into
-- it, and checks on what it shows and writes. The files it saves are read
-- again by the build's Python (check_files.py) and the kernel (test_meshcap).
--
--   luahost tests/studio/mesh_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/carts/astrowing.bm and village.bm must be there (games: the saves go into their .bme copies).

local ROOT, SD = arg[1] or ".", arg[2] or "build/mesh-sd"
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
-- the keys as chips: written as "[name]"; prompt(name) alone measures
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end
E.prompt = function(n, x, y)
  assert(type(n) == "string", "prompt: a name")
  if type(x) ~= "number" then return chip_w(n), 16 end
  texts[#texts + 1] = { "[" .. n .. "]", x, y }
  return x + chip_w(n)
end
E.lastinput = function() return nil end
E.time = function() return frame / 60 end
E.stat = function() return 0 end
E.log = function(...) io.write(table.concat({ ... }, "\t"), "\n") end
E.quit = function() quitted = true end
E.btn = function(i) return pad[i] == true end
E.btnp = function(i) return pad[i] == true and not padprev[i] end
E.keyp = function() return table.remove(keyq, 1) end
E.keyheld = function() return false end
-- the mouse (bmui): MS as a USB mouse would give it
local MS = { x = 0, y = 0, b = 0, w = 0, p = 0 }
E.mouse = function(on) if on ~= nil then return true end; return MS.x, MS.y, MS.b, MS.w, true end
E.mousep = function(i) return MS.p >> (i or 0) & 1 == 1 end
local bmui
E.require = function(name)
  if name == "bmui" then                 -- the mouse: once (it takes keyp over)
    bmui = bmui or assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", E))()
    return bmui
  end
  assert(name == "bm3d", "require: only bm3d and bmui here")      -- (no assistant: F6 says so)
  return assert(loadfile(ROOT .. "/src/script/bm3d.lua", "t", E))()
end
local keyhelp_list, keyhelp_title
E.keyhelp = function(list, title) keyhelp_list, keyhelp_title = list, title; return 0 end
local showkeys_n = 0
E.showkeys = function() showkeys_n = showkeys_n + 1; return true end
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
  assert(#v // 3 >= 1 and #v // 3 <= 65535 and #f // 4 >= 1 and #f // 4 <= 65535, "mesh: 1 to 65535 vertices, 1 to 65535 faces")
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
MF.install(E)

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
local function listed(path)
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  for _, n in ipairs(list) do if n == name:lower() then return end end
  list[#list + 1] = name:lower()
end
local project_target = dofile(ROOT .. "/tests/studio/project_rules.lua")
E.cart_write = function(path, t)
  local moved
  path, moved = project_target(path, function(p) return read_file(host(p)) ~= nil end, function(a, b)
    local f = io.open(host(b), "wb")
    f:write(read_file(host(a)))
    f:close()
    listed(b)
  end)
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
  listed(path)
  if moved then return true, path end
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
    local keys = {}                      -- in order, as the kernel
    for k in next, t do
      if type(k) == "string" or math.type(k) == "integer" then keys[#keys + 1] = k end
    end
    table.sort(keys, function(a, b)
      if type(a) ~= type(b) then return type(a) == "number" end
      return a < b
    end)
    for _, k in ipairs(keys) do
      local v = t[k]
      if type(k) == "string" then visit(v, inarray and base .. "_" .. k or k)
      else visit(v, base .. k, true) end
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

local function file(name) return read_file(SD .. (name:lower():match("%.bme$") and "/bme/" or "/carts/") .. name) end
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
check(status():find("saved /bme/astrowin.bme", 1, true), "Ctrl+S: " .. status())
local ship = model_of("astrowin.bme", "ship")
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
local astro1 = file("astrowin.bme")
check(section(astro1, 1) == lua0 and section(astro1, 2) == section(astro0, 2) and section(astro1, 9) == nil,
      "the code and the sheet stay as they were")

-- mesh -> code
goto_item("G ship")
key("c")
check(chosen() == "C ship" and status():find("mesh_ship()", 1, true), "c: a copy as code: " .. status())
key("^s")
local lua1 = section(file("astrowin.bme"), 1)
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
local up = model_of("astrowin.bme", "ship")
local moved = up and up.nv == 30
for i = 1, 30 do if moved and math.abs(up.verts[i][2] - ship.verts[i][2] - 0.2) > 1e-5 then moved = false end end
check(moved, "saved: every vertex of the ship 0.2 higher")
key("^z", "^s")
check(section(file("astrowin.bme"), 8) == section(astro1, 8), "Ctrl+Z: the ship as before, byte for byte")
key("^y")
check(sees("redo"), "Ctrl+Y: redo")
key("g", "pgup", "\b", "esc")
check(sees("cancelled"), "a tool cancelled with Esc")

-- the tools on a cube: faces in 4, mirror, delete one, a new face, merge
key("f1", "n")
check(chosen() == "M cube", "n: a new model, a cube")
key("f2", "\t", "\t", "a")
check(sees("FACES: 12 chosen"), "Tab, a: the 12 faces of the cube")
key("u")
key("K")
check(status():find("0 vertices welded", 1, true), "K: nothing to weld after the subdivision: " .. status())
key("^s")
local cube = model_of("astrowin.bme", "cube")
check(cube and cube.nf == 48 and cube.nv == 26, "u: 48 faces, 26 vertices (" .. (cube and cube.nf .. ", " .. cube.nv or "-") .. ")")
check(cube and outward(cube), "the faces of the cube still look out")
key("\t")
check(sees("VERTICES: 26 chosen"), "Tab: the corners of the chosen faces")
key("m", "^s")
cube = model_of("astrowin.bme", "cube")
check(cube and outward(cube), "m: mirrored, the faces still look out")
key("\t", "\t", "a", "n", " ", "del", "^s")
cube = model_of("astrowin.bme", "cube")
check(cube and cube.nf == 47 and cube.nv == 26, "a face deleted: 47 (" .. (cube and cube.nf or "-") .. ")")
key("\t", "n", " ", "n", " ", "n", " ")
check(sees("VERTICES: 3 chosen"), "n and space: three vertices")
key("j", "^s")
cube = model_of("astrowin.bme", "cube")
check(cube and cube.nf == 48, "j: a new face on them (" .. (cube and cube.nf or "-") .. ")")
key("a", "n", " ", "n", " ", "k", "^s")
cube = model_of("astrowin.bme", "cube")
check(cube and cube.nv == 25, "k: two vertices merged into one (" .. (cube and cube.nv or "-") .. ")")
key("\t", "\t", "a", "c", "right", "\n", "p", "^s")
cube = model_of("astrowin.bme", "cube")
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
local plane = model_of("astrowin.bme", "plane")
local top = 0
for _, p in ipairs(plane and plane.verts or {}) do if math.abs(p[2] - 0.5) < 1e-6 then top = top + 1 end end
check(plane and plane.nf == 10 and plane.nv == 8 and top == 4, "the plane extruded: 10 faces, 4 corners up")
check(plane and outward(plane), "its sides look out")

-- duplicate and mirror copy
key("a", "a", "d", "right", "\n", "^s")
plane = model_of("astrowin.bme", "plane")
check(plane and plane.nf == 20 and plane.nv == 16, "d: a copy of the faces (" .. (plane and plane.nf or "-") .. ")")
key("a", "a", "M", "^s")
plane = model_of("astrowin.bme", "plane")
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
local lua2 = section(file("astrowin.bme"), 1)
check(lua2:find("function mesh_hero()", 1, true) and lua2:find("function mesh_ship()", 1, true), "two code meshes")
check(model_of("astrowin.bme", "hero") and not model_of("astrowin.bme", "ship"), "the model is now hero")
goto_item("C hero")
key("del")
check(status():find("again", 1, true), "Del asks again")
key("del")
goto_item("C ship")
key("del", "del", "^s")
check(section(file("astrowin.bme"), 1) == lua0, "no code meshes left: the code is the game's again, byte for byte")
key("f5")
check(ran and ran:upper() == "/BME/ASTROWIN.BME", "F5: the project runs: " .. tostring(ran))

-- the village: a model with a skeleton keeps it (the list: astrowin.bme,
-- astrowing.bm, village.bm)
key("esc", "down", "\n")
for _ = 1, 5 do key("down") end
key("\n")
check(status():find("8 models", 1, true), "the village: " .. status())
goto_item("M villager")
check(sees("skeleton: 7 bones, 3 animations"), "the villager has its skeleton")
local rig0 = anim_rigs(section(village0, 9)).villager
local vil0 = model_of("village.bm", "villager")
key("f2", "a", "g", "up", "\n", "^s")
local rig1 = anim_rigs(section(file("village.bme"), 9)).villager
check(rig1 and rig1.nv == 112 and rig1.bones == rig0.bones and rig1.clips == rig0.clips and
      table.concat(rig1.vb, ",") == table.concat(rig0.vb, ","), "moved: the skeleton is the same")
key("a", "n", " ", "del", "^s")
local vil = model_of("village.bme", "villager")
local rig2 = anim_rigs(section(file("village.bme"), 9)).villager
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
  local a, b = model_of("village.bme", n), nil
  for _, x in ipairs(mesh_models(section(village0, 8))) do if x.name == n then b = x end end
  check(a and b and a.nv == b.nv and a.nf == b.nf, "the other models stay: " .. n)
end
local v1 = file("village.bme")
check(section(v1, 1) == section(village0, 1) and section(v1, 5) == section(village0, 5) and
      section(v1, 4) == section(village0, 4), "the code, the sheet and the cover stay")

-- save as a copy
key("esc")
for _ = 1, 3 do key("down") end
key("\n")
check(sees("file name"), "Save as asks a name")
-- a click on the line of the dialog puts the caret there (M48): "ac", a click between, "b"
for _ = 1, 40 do key("\b") end
key("a", "c")
for _, b in ipairs({ 0, 1, 0 }) do
  local was = MS.b
  MS.x, MS.y, MS.b, MS.w = 96 + 8, 184, b, 0
  MS.p = MS.b & ~was
  frames(1)
  MS.p = 0
end
frames(1)
key("b")
check(sees("abc"), "mouse: a click on the line of the dialog puts the caret between the letters")
key("end")
type_text("MESHCOPY")
check(status():find("saved /bme/meshcopy.bme", 1, true), "saved as: " .. status())
local cp = file("meshcopy.bme")
check(cp and section(cp, 8) == section(v1, 8) and section(cp, 9) == section(v1, 9) and section(cp, 5) == section(v1, 5),
      "the copy has the same models, skeletons and sheet")
check(file("astrowing.bm") == astro0 and file("village.bm") == village0,
      "the games stay as they were: the saves went into their editable copies (.bme)")

-- Shift+A: a primitive as a new part of the model (the villager has bones)
do
  key("f1")
  goto_item("M villager")
  key("f2", "a", "a")
  local before = model_of("meshcopy.bme", "villager")
  local rb = anim_rigs(section(file("meshcopy.bme"), 9)).villager
  key("A")
  check(sees("add a part") and sees("Cylinder"), "Shift+A: the four shapes")
  key("\n")
  check(sees("VERTICES: 8 chosen") and status():find("added a cube", 1, true), "a cube: its 8 vertices are chosen: " .. status())
  key("^z")
  check(sees("VERTICES: 0 chosen"), "undo: the new vertices are not chosen any more")
  key("^s")
  local same = model_of("meshcopy.bme", "villager")
  check(same and same.nv == before.nv and same.nf == before.nf, "undo: the count as before (one step)")
  local shapes = { { "cube", 8, 12, 0 }, { "cylinder", 18, 32, 1 }, { "sphere", 62, 120, 2 }, { "plane", 4, 2, 3 } }
  for _, sh in ipairs(shapes) do
    key("a", "a", "A")
    for _ = 1, sh[4] do key("down") end
    key("\n", "^s")
    local m = model_of("meshcopy.bme", "villager")
    check(m and m.nv == before.nv + sh[2] and m.nf == before.nf + sh[3], sh[1] .. ": " .. sh[2] .. " vertices, " .. sh[3] ..
          " faces (" .. (m and (m.nv - before.nv) .. "/" .. (m.nf - before.nf) or "-") .. ")")
    check(sees("VERTICES: " .. sh[2] .. " chosen"), sh[1] .. ": the new vertices are chosen")
    key("^z", "^s")
    m = model_of("meshcopy.bme", "villager")
    check(m and m.nv == before.nv and m.nf == before.nf, sh[1] .. ": one undo takes it away")
  end
  -- on a chosen vertex: the part goes to its middle and takes its bone
  key("a", "a", "n", " ")
  check(sees("VERTICES: 1 chosen"), "one vertex chosen")
  key("A", "\n", "^s")
  local m = model_of("meshcopy.bme", "villager")
  local rg = anim_rigs(section(file("meshcopy.bme"), 9)).villager
  local c, ok = { 0, 0, 0 }, m and m.nv == before.nv + 8 and rg and rg.nv == m.nv
  if ok then
    for i = before.nv + 1, m.nv do for k = 1, 3 do c[k] = c[k] + m.verts[i][k] / 8 end end
    local bone, hit = rg.vb[m.nv], false
    for i = before.nv + 1, m.nv do if rg.vb[i] ~= bone then ok = false end end
    for j, q in ipairs(before.verts) do
      if math.abs(q[1] - c[1]) < 1e-5 and math.abs(q[2] - c[2]) < 1e-5 and math.abs(q[3] - c[3]) < 1e-5 then
        hit = true
        if rb.vb[j] ~= bone then ok = false end
      end
    end
    ok = ok and hit
  end
  check(ok, "a part on a chosen vertex: at its place, on its bone")
  check(m and outward({ nv = 8, verts = { table.unpack(m.verts, before.nv + 1, m.nv) },
        faces = (function() local t = {} for i = before.nf + 1, m.nf do local f = m.faces[i]; t[#t + 1] = { f[1] - before.nv, f[2] - before.nv, f[3] - before.nv } end return t end)() }),
        "the cube part looks out")
  key("^z", "^s")
end

-- Shift+X: the live mirror across X = 0 (a new cube: v1..v8 = x -,+,+,-,-,+,+,-)
do
  local function same(a, b) return math.abs(a - b) < 1e-5 end
  local function changed(m0, m1)
    local out = {}
    for i = 1, m0.nv do
      for k = 1, 3 do if not same(m0.verts[i][k], m1.verts[i][k]) then out[#out + 1] = i; break end end
    end
    return out
  end
  local function mirrored(m, i, j)
    return same(m.verts[i][1], -m.verts[j][1]) and same(m.verts[i][2], m.verts[j][2]) and same(m.verts[i][3], m.verts[j][3])
  end
  local function mode(m)               -- "VERTICES" or "FACES" (Tab: vertices, edges, faces)
    for _ = 1, 3 do
      if sees(m .. ":") then return end
      key("	")
    end
  end
  local function pick(list)            -- clear, then choose these numbers (n steps the pointer)
    if not sees(": 0 chosen") then key("a") end
    key("home")
    local last = 0
    for _, i in ipairs(list) do
      for _ = last + 1, i do key("n") end
      last = i
      key(" ")
    end
  end
  key("f1", "n")
  key("r")
  type_text("mirt")
  local name = "mirt"
  key("f2")
  mode("VERTICES")
  key("^s")
  local m0 = model_of("meshcopy.bme", name)
  check(m0 and m0.nv == 8 and m0.nf == 12, "mirror: a new cube")
  -- off: one vertex moves alone
  pick({ 1 })
  key("g", "up", "\n", "^s")
  local m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 1, "mirror off: one vertex moves (" .. #changed(m0, m1) .. ")")
  key("^z", "^s")
  -- on: the vertex and its counterpart
  check(not sees("MIRROR X"), "no indicator while the mirror is off")
  key("X")
  check(sees("MIRROR X") and status():find("mirror X on", 1, true), "Shift+X: the indicator and the message: " .. status())
  key("g", "up", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  local ch = changed(m0, m1)
  check(#ch == 2 and ch[1] == 1 and ch[2] == 2 and mirrored(m1, 1, 2) and not same(m1.verts[1][2], m0.verts[1][2]),
        "move: v1 and its counterpart v2 (" .. table.concat(ch, ",") .. ")")
  check(sees("of 65535 vertices") and sees("8 of"), "the counters stay")
  key("^z", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 0, "one undo takes back both sides")
  -- rotate: v1 and v4 (the left side)
  pick({ 1, 4 })
  check(sees("VERTICES: 2 chosen"), "v1 and v4 chosen")
  key("r", "up", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 4 and mirrored(m1, 1, 2) and mirrored(m1, 4, 3),
        "rotate: both sides turn (" .. #changed(m0, m1) .. ")")
  key("^z")
  -- scale
  key("t", "up", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 4 and mirrored(m1, 1, 2) and mirrored(m1, 4, 3), "scale: both sides (" .. #changed(m0, m1) .. ")")
  key("^z")
  -- every vertex chosen: still symmetric
  pick({ 1, 2, 3, 4, 5, 6, 7, 8 })
  key("r", "left", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) > 0 and mirrored(m1, 1, 2) and mirrored(m1, 4, 3) and mirrored(m1, 5, 6) and mirrored(m1, 8, 7),
        "rotate with both sides chosen: the result is symmetric")
  key("^z", "^s")
  -- a vertex that lost its counterpart has no pair
  key("X")
  pick({ 1 })
  key("g", "up", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(not sees("MIRROR X") and #changed(m0, m1) == 1, "off again: one vertex")
  key("X", "g", "up", "\n", "^s")
  local m2 = model_of("meshcopy.bme", name)
  check(#changed(m1, m2) == 1 and changed(m1, m2)[1] == 1, "no counterpart: only v1 moves (" .. #changed(m1, m2) .. ")")
  key("^z", "^z", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 0, "back to the cube")
  -- paint: face 9 (left side), its counterpart is face 12
  mode("FACES")
  pick({ 9 })
  check(sees("FACES: 1 chosen"), "face 9 chosen")
  key("c", "right", "right", "\n", "p", "^s")
  m1 = model_of("meshcopy.bme", name)
  local c9 = m1.faces[9][4]
  local ok = c9 ~= m0.faces[9][4] and m1.faces[12][4] == c9
  for i = 1, 12 do if i ~= 9 and i ~= 12 and m1.faces[i][4] ~= m0.faces[i][4] then ok = false end end
  check(ok, "paint: face 9 and its counterpart 12 only")
  key("^z", "^s")
  -- delete a face with its counterpart (one undo)
  pick({ 9 })
  key("del", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nf == 10, "delete a face: two faces go (" .. (m1 and m1.nf or "-") .. ")")
  key("^z", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nf == 12, "one undo brings both back")
  -- delete a vertex: it and its counterpart with their faces
  mode("VERTICES")
  pick({ 1 })
  key("del", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nf == 4 and m1.nv == 6, "delete a vertex: v1 and v2 go (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  key("^z", "X", "^s")
  check(not sees("MIRROR X"), "Shift+X again: off")
  -- the command is on the menu and in the help
  key("f3")
  check(sees("Live mirror X"), "the commands list the live mirror")
  key("esc")
  -- the test model goes (the copy stays the village as saved)
  key("f1")
  goto_item("M mirt")
  key("del", "del", "^s")
  check(not model_of("meshcopy.bme", name), "the test cube is deleted")
  -- T.G.mirror_pairs: tolerance, the plane, no counterpart
  local G = E.require("bm3d").G
  local it = { v = { { 1, 2, 3 }, { -1.0004, 2, 3 }, { 0, 5, 5 }, { 4, 4, 4 }, { -4.01, 4, 4 }, { 7, 7, 7 } }, f = {} }
  local mp = G.mirror_pairs(it, { 1, 2, 3, 4, 5, 6 })
  check(mp[1] == 2 and mp[2] == 1 and mp[3] == 3 and mp[4] == nil and mp[5] == nil and mp[6] == nil,
        "T.G.mirror_pairs: a pair within 0.001, the plane, none beyond it")
  local fm = G.mirror_faces({ f = { { 1, 3, 5 }, { 2, 3, 6 }, { 1, 2, 3 } } }, { 1, 3 }, { [1] = 2, [3] = 3, [5] = 6, [2] = 1 })
  check(fm[1] == 2 and fm[3] == 3 and next(fm, 3) == nil, "T.G.mirror_faces: the counterpart face, a face on the plane is its own")
  -- T.G.smooth: a noisy grid of 5 x 5 vertices (z = noise), the border stays, the rest gets flatter
  do
    local gv, gf = {}, {}
    for r = 0, 4 do for c = 0, 4 do
      local noise = ((r * 7 + c * 3) % 5 - 2) * 0.1
      if r == 0 or c == 0 or r == 4 or c == 4 then noise = 0.3 end
      gv[#gv + 1] = { c, r, noise }
    end end
    for r = 0, 3 do for c = 0, 3 do
      local a = r * 5 + c + 1
      gf[#gf + 1] = { a, a + 1, a + 6 }
      gf[#gf + 1] = { a, a + 6, a + 5 }
    end end
    local git = { v = gv, f = gf }
    local before, all = {}, {}
    for i, v in ipairs(gv) do before[i] = { v[1], v[2], v[3] }; all[i] = i end
    local n = G.smooth(git, all, 0.5)
    local bord_ok, inner_moved = true, 0
    for r = 0, 4 do for c = 0, 4 do
      local i = r * 5 + c + 1
      local same3 = gv[i][1] == before[i][1] and gv[i][2] == before[i][2] and gv[i][3] == before[i][3]
      if (r == 0 or c == 0 or r == 4 or c == 4) then if not same3 then bord_ok = false end
      elseif not same3 then inner_moved = inner_moved + 1 end
    end end
    check(bord_ok, "T.G.smooth: the open border stays")
    check(inner_moved >= 8 and n == 9, "T.G.smooth: the 9 inner vertices move (" .. inner_moved .. "/" .. n .. ")")
    local inner_before, inner_after = 0, 0
    for r = 1, 3 do for c = 1, 3 do
      local i = r * 5 + c + 1
      inner_before = inner_before + (before[i][3] - 0.3) ^ 2
      inner_after = inner_after + (gv[i][3] - 0.3) ^ 2
    end end
    check(inner_after < inner_before, "T.G.smooth: the noisy grid gets flatter (" .. inner_before .. " -> " .. inner_after .. ")")
    -- only the vertices of the list move; k = 0 moves nothing in space; a vertex with no edge stays
    local hv = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 5, 5, 5 }, { 0, 0, 1 }, { 1, 1, 1 } }
    local hit = { v = hv, f = { { 1, 2, 3 }, { 1, 3, 2 }, { 1, 2, 5 }, { 1, 5, 2 } } }
    G.smooth(hit, { 4 }, 0.5)
    check(hv[4][1] == 5 and hv[4][3] == 5, "T.G.smooth: a vertex with no edge stays")
    G.smooth(hit, { 2 }, 0.5)
    check(hv[1][1] == 0 and hv[3][2] == 1, "T.G.smooth: only the listed vertices move")
  end
end

-- Edge mode (Tab) and the loop cut (Ctrl+R) on a new cube: v1..v8 = x -,+,+,-,-,+,+,-; y -,-,+,+ ...
do
  local G = E.require("bm3d").G
  local function mode(m)
    for _ = 1, 3 do
      if sees(m .. ":") then return end
      key("	")
    end
  end
  local function same(a, b) return math.abs(a - b) < 1e-5 end
  -- the n-th edge of the model, in the order of the n key (the keys sorted)
  local function edge_no(m, a, b)
    local keys = {}
    for k in pairs(G.edge_faces({ f = m.faces })) do keys[#keys + 1] = k end
    table.sort(keys)
    for i, k in ipairs(keys) do if k == G.edge_key(a, b) then return i, #keys end end
  end
  local function pick_edges(m, list)
    mode("EDGES")
    if not sees(": 0 chosen") then key("a") end
    key("home")
    local last = 0
    for _, e in ipairs(list) do
      local i = edge_no(m, e[1], e[2])
      for _ = last + 1, i do key("n") end
      last = i
      key(" ")
    end
  end
  local function nmoved(m0, m1)
    local n = 0
    for i = 1, m0.nv do
      for k = 1, 3 do if not same(m0.verts[i][k], m1.verts[i][k]) then n = n + 1; break end end
    end
    return n
  end
  key("f1", "n")
  key("r")
  type_text("edgt")
  local name = "edgt"
  key("f2")
  mode("VERTICES")
  key("^s")
  local m0 = model_of("meshcopy.bme", name)
  check(m0 and m0.nv == 8 and m0.nf == 12, "edges: a new cube")
  -- Tab: vertices -> edges -> faces -> vertices
  check(sees("VERTICES:"), "edges: the page starts in vertex mode")
  key("	")
  check(sees("EDGES: 0 chosen"), "Tab: edge mode")
  key("	")
  check(sees("FACES:"), "Tab again: face mode")
  key("	")
  check(sees("VERTICES:"), "Tab again: vertex mode")
  -- select one edge, move it
  pick_edges(m0, { { 1, 2 } })
  check(sees("EDGES: 1 chosen"), "an edge chosen with n and space (" .. status() .. ")")
  key("g", "up", "\n", "^s")
  local m1 = model_of("meshcopy.bme", name)
  check(m1 and nmoved(m0, m1) == 2 and not same(m1.verts[1][2], m0.verts[1][2]) and not same(m1.verts[2][2], m0.verts[2][2]),
        "move on an edge: its two vertices move (" .. (m1 and nmoved(m0, m1) or "-") .. ")")
  key("^z", "^s")
  -- scale / rotate act on the ends of the edge
  key("t", "up", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and nmoved(m0, m1) == 2, "scale on an edge: its two vertices")
  key("^z", "^s")
  -- two edges, the same edge again takes it out
  pick_edges(m0, { { 1, 2 }, { 2, 3 } })
  check(sees("EDGES: 2 chosen"), "two edges chosen")
  key("g", "left", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and nmoved(m0, m1) == 3, "move two edges that share a vertex: 3 vertices (" .. (m1 and nmoved(m0, m1) or "-") .. ")")
  key("^z", "^s")
  -- Tab to faces: the faces with all corners chosen (none for two edges); back to vertices keeps the corners
  key("a")
  check(sees("EDGES: 0 chosen"), "a: none chosen")
  -- Ctrl+R with nothing chosen
  key("^r")
  check(status():find("loop cut", 1, true) and status():find("one edge", 1, true), "Ctrl+R with no edge: a message (" .. status() .. ")")
  check(sees("EDGES: 0 chosen"), "no change, no try of the game")
  -- the loop cut around the cube: 4 quads, 4 new vertices, 8 new triangles
  pick_edges(m0, { { 1, 2 } })
  key("^r")
  check(sees("EDGES: 4 chosen") and status():find("closed ring", 1, true), "the new loop is chosen: " .. status())
  key("^s")
  local m2 = model_of("meshcopy.bme", name)
  check(m2 and m2.nv == 12 and m2.nf == 20, "loop cut on a cube edge: 12 vertices, 20 triangles (" .. (m2 and (m2.nv .. "/" .. m2.nf) or "-") .. ")")
  local mids = {}
  for i = 9, 12 do if m2 then mids[#mids + 1] = string.format("%.2f,%.2f,%.2f", m2.verts[i][1], m2.verts[i][2], m2.verts[i][3]) end end
  table.sort(mids)
  check(table.concat(mids, " ") == "0.00,0.00,-0.50 0.00,0.00,0.50 0.00,1.00,-0.50 0.00,1.00,0.50",
        "loop cut: a vertex in the middle of the four edges along x (" .. table.concat(mids, " ") .. ")")
  check(m2 and outward(m2), "loop cut: the faces still look out")
  -- the new loop moves as one
  key("g", "right", "\n", "^s")
  local m3 = model_of("meshcopy.bme", name)
  local moved = 0
  for i = 1, m2.nv do if not same(m3.verts[i][1], m2.verts[i][1]) then moved = moved + 1 end end
  check(moved == 4, "move the new loop: 4 vertices (" .. moved .. ")")
  key("^z", "^z", "^s")
  local m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nv == 8 and m4.nf == 12, "two undo steps: the cube again (" .. (m4 and (m4.nv .. "/" .. m4.nf) or "-") .. ")")
  -- two undo steps would be wrong: the loop cut is one step
  key("^y", "^s")
  m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nv == 12 and m4.nf == 20, "redo: the loop cut again, one step")
  key("^z", "^s")
  -- vertex mode: two vertices that make an edge
  mode("VERTICES")
  if not sees(": 0 chosen") then key("a") end
  key("home")
  key("n", " ", "n", " ")
  check(sees("VERTICES: 2 chosen"), "vertex mode: two vertices chosen")
  key("^r", "^s")
  m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nv == 12 and m4.nf == 20 and sees("VERTICES: 4 chosen"), "loop cut from two vertices: the 4 new vertices are chosen")
  key("^z", "^s")
  -- knife-lite (Shift+J): two edges of one triangle of the cube
  mode("EDGES")
  if not sees(": 0 chosen") then key("a") end
  key("J")
  check(status():find("knife", 1, true) and status():find("two edges", 1, true), "Shift+J with no edge: a message (" .. status() .. ")")
  pick_edges(m0, { { 1, 2 } })
  key("J")
  check(status():find("knife", 1, true) and status():find("two edges", 1, true), "Shift+J with one edge: a message")
  key("a")
  pick_edges(m0, { { 1, 2 }, { 3, 4 } })
  key("J")
  local refused = status()
  key("^s")
  m4 = model_of("meshcopy.bme", name)
  check(refused:find("share a triangle", 1, true) and m4 and m4.nv == 8 and m4.nf == 12,
        "knife: edges with no common triangle are refused, nothing changes (" .. refused .. ")")
  key("a")
  pick_edges(m0, { { 1, 2 }, { 2, 3 } })
  key("J", "^s")
  m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nv == 10 and m4.nf == 16, "knife on the cube: 10 vertices, 16 triangles (" .. (m4 and (m4.nv .. "/" .. m4.nf) or "-") .. ")")
  check(sees("EDGES: 1 chosen"), "knife: the new edge is chosen")
  check(m4 and outward(m4), "knife: the faces still look out")
  key("g", "right", "\n", "^s")
  local m5 = model_of("meshcopy.bme", name)
  local mv = 0
  for i = 1, m4.nv do if not same(m5.verts[i][1], m4.verts[i][1]) then mv = mv + 1 end end
  check(mv == 2, "knife: the two new vertices move (" .. mv .. ")")
  key("^z", "^z", "^s")
  m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nv == 8 and m4.nf == 12, "knife: move and knife are two undo steps, the cube again")
  key("^y", "^s")
  m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nv == 10 and m4.nf == 16, "knife: redo, one step")
  key("^z", "^s")
  -- vertex mode: the key does nothing but say so
  mode("VERTICES")
  key("J")
  check(status():find("knife", 1, true) and status():find("edges", 1, true), "knife: in vertex mode a message")
  mode("EDGES")  -- delete: the faces next to the edge
  pick_edges(m0, { { 1, 2 } })
  key("del", "^s")
  m4 = model_of("meshcopy.bme", name)
  check(m4 and m4.nf == 10 and m4.nv == 8, "delete an edge: the 2 faces next to it (" .. (m4 and (m4.nv .. "/" .. m4.nf) or "-") .. ")")
  key("^z", "^s")
  -- the live mirror: an edge and its counterpart (1-4 and 2-3)
  mode("EDGES")
  key("X")
  pick_edges(m0, { { 1, 4 } })
  check(sees("EDGES: 2 chosen"), "live mirror: the edge and its counterpart are chosen (" .. status() .. ")")
  key("g", "up", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and nmoved(m0, m1) == 4, "live mirror: move acts on both edges (" .. (m1 and nmoved(m0, m1) or "-") .. ")")
  key("^z", "del", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nf == 8, "live mirror: delete takes the faces of both edges (" .. (m1 and m1.nf or "-") .. ")")
  key("^z", "X", "^s")
  check(not sees("MIRROR X"), "live mirror off")
  -- the commands list
  key("f3")
  check(sees("Loop cut"), "the commands list the loop cut")
  key("esc")
  -- the pad reaches it: the menu row runs the same key
  key("f1")
  goto_item("M edgt")
  key("del", "del", "^s")
  check(not model_of("meshcopy.bme", name), "the edge test cube is deleted")
end

-- T.G edges and the loop cut: pure, no editor
do
  local G = E.require("bm3d").G
  local function mk(verts, faces, bones)
    local it = { v = {}, f = {}, vb = bones }
    for i, p in ipairs(verts) do it.v[i] = { p[1], p[2], p[3] } end
    for i, f in ipairs(faces) do it.f[i] = { f[1], f[2], f[3], f[4] or 0x808080, f[5] } end
    return it
  end
  check(G.edge_key(3, 7) == G.edge_key(7, 3) and select(2, G.edge_ends(G.edge_key(7, 3))) == 7, "T.G.edge_key: a pair, any order")
  -- a plane: 4 vertices, 2 triangles; an open border stops the ring
  do
    local it = { v = {}, f = {} }
    G.primitive(it, "plane", { 0, 0, 0 }, 1, 0xFF0000)
    local ef = G.edge_faces(it)
    local n = 0
    for _ in pairs(ef) do n = n + 1 end
    check(n == 5, "T.G.edge_faces: a plane has 5 edges (" .. n .. ")")
    -- a side edge: the one with exactly one face that is not the border of the diagonal
    local side
    for k, l in pairs(ef) do if #l == 1 and not side then side = k end end
    local a, b = G.edge_ends(side)
    local res = G.loop_cut(it, a, b)
    check(res and #it.v == 6 and #it.f == 4 and #res.mids == 2 and #res.rungs == 1, "loop cut on a plane: 2 new vertices, 4 triangles, stops at the borders")
    local ok = true
    for _, f in ipairs(it.f) do if G.normal_of(it, f)[2] == 0 then ok = false end end
    check(ok, "loop cut on a plane: no flat triangle")
  end
  -- a strip of 2 quads (6 vertices): the cut across the first quad's side runs to the other border
  do
    local it = mk({ { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 2, 0, 1 } },
                  { { 1, 4, 5 }, { 1, 5, 2 }, { 2, 5, 6 }, { 2, 6, 3 } }, { 0, 0, 1, 0, 0, 1 })
    local res, plan = G.loop_cut(it, 2, 5)
    check(res and #res.mids == 3 and #res.rungs == 2 and not plan.closed and #it.v == 9 and #it.f == 8,
          "loop cut on a strip: across the shared edge, 2 quads, an open ring (" .. #it.v .. "/" .. #it.f .. ")")
    check(it.vb[7] == 0 and it.vb[8] == 0 and it.vb[9] == 1, "loop cut: the bones of the 3 new vertices")
  end
  -- the bone of a new vertex: the lower end
  do
    local it = mk({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 2 }, { 0, 0, 2 } }, { { 1, 3, 2 }, { 1, 4, 3 } }, { 5, 9, 9, 5 })
    local res = G.loop_cut(it, 1, 2)
    check(res and it.vb[res.mids[1]] == 5, "T.G.loop_cut: the new vertex takes the bone of the lower end")
  end
  -- a closed ring: the cylinder side (8 quads)
  do
    local it = { v = {}, f = {} }
    G.primitive(it, "cylinder", { 0, 0, 0 }, 1, 0x00FF00)
    -- a vertical edge: two vertices with the same x, z and different y, joined by a side triangle
    local va, vb
    for _, f in ipairs(it.f) do
      for k = 1, 3 do
        local p, q = f[k], f[k % 3 + 1]
        if it.v[p][2] ~= it.v[q][2] and math.abs(it.v[p][1] - it.v[q][1]) < 1e-6 and math.abs(it.v[p][3] - it.v[q][3]) < 1e-6 then va, vb = p, q end
      end
    end
    local nv0, nf0 = #it.v, #it.f
    local res, plan = G.loop_cut(it, va, vb)
    check(res and plan.closed and #plan.quads == 8 and #res.mids == 8 and #it.v == nv0 + 8 and #it.f == nf0 + 16,
          "loop cut around a cylinder: a closed ring of 8 quads (" .. #it.v .. "/" .. #it.f .. ")")
    local ef = G.edge_faces(it)
    local bad = 0
    for _, l in pairs(ef) do if #l ~= 2 then bad = bad + 1 end end
    check(bad == 0, "the cut cylinder is still closed: every edge has two faces (" .. bad .. ")")
  end
  -- a ring that ends on a triangle that is no quad: the triangle is cut in 2, no gap
  do
    local it = mk({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 2 }, { 0, 0, 2 }, { 1, 0, 3.5 } },
                  { { 1, 3, 2 }, { 1, 4, 3 }, { 4, 5, 3 } })
    local res, plan = G.loop_cut(it, 1, 2)
    check(res and not plan.closed and #plan.quads == 1 and #plan.plains == 1 and #it.v == 7 and #it.f == 6,
          "loop cut: the ring ends on a triangle, it is cut in 2 (" .. #it.v .. "/" .. #it.f .. ")")
    local ef = G.edge_faces(it)
    local ok = true
    for k, l in pairs(ef) do if #l > 2 then ok = false end end
    check(ok, "loop cut at a triangle: no edge with more than 2 faces")
  end
  -- no quad: one triangle alone; not an edge
  do
    local it = mk({ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } }, { { 1, 3, 2 } })
    local res, err = G.loop_cut(it, 1, 2)
    check(res == nil and err:find("no quad", 1, true), "loop cut on a lone triangle: no quad (" .. tostring(err) .. ")")
    res, err = G.loop_cut(it, 1, 9)
    check(res == nil and err:find("not an edge", 1, true), "loop cut: not an edge")
    check(#it.v == 3 and #it.f == 1, "a failed loop cut changes nothing")
  end
  -- uv and colour: the middle of the edge gets the mean
  do
    local it = mk({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 2 }, { 0, 0, 2 } },
                  { { 1, 3, 2, -1, { 0, 0, 1, 1, 1, 0 } }, { 1, 4, 3, -1, { 0, 0, 0, 1, 1, 1 } } })
    local res = G.loop_cut(it, 1, 2)
    local mm = res.mids[1]
    local found
    for _, f in ipairs(it.f) do
      for k = 1, 3 do
        if f[k] == mm and f[5] then
          local u, v = f[5][k * 2 - 1], f[5][k * 2]
          if math.abs(u - 0.5) < 1e-6 and math.abs(v) < 1e-6 then found = true end
        end
      end
    end
    check(found and it.f[1][4] == -1, "loop cut: the new vertex gets the mean uv, the faces keep the sheet flag")
  end
  -- knife-lite, pure
  do
    -- a cube: the cut on the bottom face, the neighbours cut too
    local it = { v = {}, f = {} }
    G.primitive(it, "cube", { 0, 0, 0 }, 1, 0x808080)
    local ef = G.edge_faces(it)
    -- find a triangle and two of its edges
    local f = it.f[1]
    local k1, k2 = G.edge_key(f[1], f[2]), G.edge_key(f[2], f[3])
    local res = G.knife(it, k1, k2)
    check(res and #it.v == 10 and #it.f == 16 and #res.mids == 2, "T.G.knife on a cube: 10 vertices, 16 triangles")
    local ok = true
    for _, g in ipairs(it.f) do
      local n = G.normal_of(it, g)
      if n[1] == 0 and n[2] == 0 and n[3] == 0 then ok = false end
    end
    check(ok, "T.G.knife: no flat triangle")
    local ef2 = G.edge_faces(it)
    local open, over = 0, 0
    for _, l in pairs(ef2) do if #l == 1 then open = open + 1 elseif #l > 2 then over = over + 1 end end
    check(open == 0 and over == 0, "T.G.knife on a closed cube: no crack, no edge with 3 faces (" .. open .. "/" .. over .. ")")
    check(ef2[G.edge_key(res.rung[1], res.rung[2])] ~= nil, "T.G.knife: the new edge is in the mesh")
  end
  -- a plane: the edges at a border corner; the border neighbours do not exist
  do
    local it = { v = {}, f = {} }
    G.primitive(it, "plane", { 0, 0, 0 }, 1, 0xFF0000)
    local f = it.f[1]
    local res = G.knife(it, G.edge_key(f[1], f[2]), G.edge_key(f[2], f[3]))
    check(res and #it.v == 6 and #it.f == 4, "T.G.knife on a plane: 6 vertices, 4 triangles (" .. #it.v .. "/" .. #it.f .. ")")
  end
  -- a border edge only: one edge of the triangle with no neighbour
  do
    local it = mk({ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 0, 2 } }, { { 1, 3, 2 } })
    local res = G.knife(it, G.edge_key(1, 2), G.edge_key(2, 3))
    check(res and #it.v == 5 and #it.f == 3, "T.G.knife on a lone triangle: 3 triangles")
  end
  -- refused
  do
    local it = mk({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 2 }, { 0, 0, 2 } }, { { 1, 3, 2 }, { 1, 4, 3 } })
    local res, err = G.knife(it, G.edge_key(1, 2), G.edge_key(3, 4))
    check(res == nil and err:find("share a triangle", 1, true), "T.G.knife: edges with no common triangle (" .. tostring(err) .. ")")
    res, err = G.knife(it, G.edge_key(1, 2), G.edge_key(1, 2))
    check(res == nil, "T.G.knife: the same edge twice")
    res, err = G.knife(it, G.edge_key(1, 2), G.edge_key(2, 4))
    check(res == nil and err:find("not an edge", 1, true), "T.G.knife: not an edge")
    check(#it.v == 4 and #it.f == 2, "a failed knife changes nothing")
    -- 3 faces on one edge
    local it3 = mk({ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
                   { { 1, 2, 3 }, { 1, 2, 4 }, { 1, 2, 5 } })
    res, err = G.knife(it3, G.edge_key(1, 2), G.edge_key(2, 3))
    check(res == nil and err:find("more than 2", 1, true), "T.G.knife: an edge with 3 faces (" .. tostring(err) .. ")")
    check(#it3.v == 5 and #it3.f == 3, "a refused knife changes nothing")
  end
  -- bones and uv: the new vertex takes the bone of the lower end; the mean uv; the flag stays
  do
    local it = mk({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 0, 2 }, { 0, 0, 2 } },
                  { { 1, 3, 2, -1, { 0, 0, 1, 1, 1, 0 } }, { 1, 4, 3, -1, { 0, 0, 0, 1, 1, 1 } } }, { 3, 4, 5, 6 })
    local res = G.knife(it, G.edge_key(1, 3), G.edge_key(2, 3))
    check(res and it.vb[5] == 3 and it.vb[6] == 4, "T.G.knife: the bones of the new vertices (" .. tostring(it.vb[5]) .. "," .. tostring(it.vb[6]) .. ")")
    local found, flag = 0, true
    for _, g in ipairs(it.f) do
      if g[4] ~= -1 then flag = false end
      for k = 1, 3 do
        if g[k] == res.mids[1] and g[5] then
          local u, v = g[5][k * 2 - 1], g[5][k * 2]
          -- the middle of the diagonal 1-3: the uv between (0,0) and (1,1) of the first face, (0,0) and (1,1) of the second
          if math.abs(u - 0.5) < 1e-6 and math.abs(v - 0.5) < 1e-6 then found = found + 1 end
        end
      end
    end
    check(found >= 1 and flag, "T.G.knife: the mean uv at the new vertex, the faces keep the sheet flag (" .. found .. ")")
  end
end

-- F12 held: the keys of the page (keyhelp(), the kernel draws them)
local function in_help(keys, what)
  for _, e in ipairs(keyhelp_list or {}) do
    if type(e) == "table" and e[1] == keys and e[2]:find(what, 1, true) then return true end
  end
  return false
end
key("f1")
check(keyhelp_title == "bm Mesh" and in_help("m", "copy as a model") and in_help("Y LEFTRIGHT", "page"),
      "keyhelp: the keys of the list, the pad's")
key("f2")
check(in_help("x / d", "extrude faces") and not in_help("m", "copy as a model"), "keyhelp: the keys of the edit page")
check(in_help("ctrl r", "loop cut"), "keyhelp: Ctrl+R is the loop cut")
-- Ctrl+Esc: with nothing to save it leaves at once; with changes it asks first
check(E._exit() == true, "_exit: nothing to save, it leaves")
key("f1", "n")
local asked = E._exit() == false
frames(1)
check(asked and sees("Ctrl+Esc again"), "_exit: unsaved changes, it asks")
check(E._exit() == true, "_exit: again, it leaves")


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

key("f1")
local mx, my = at("list", nil, 0)
click(mx, my)
check(sees("MESHES"), "mouse: the list tab")
local rows = {}
for _, t in ipairs(texts) do if t[2] == 24 and t[3] >= 48 and t[3] < 304 then rows[#rows + 1] = t end end
check(#rows >= 2, "mouse: the list's rows")
click(rows[2][2] + 4, rows[2][3] + 8)
check(chosen() and chosen():find(rows[2][1]:gsub("%*$", ""), 1, true), "mouse: a click chooses a mesh: " .. tostring(chosen()))
mouse(100, rows[1][3] + 8, 0, -1)
click(rows[2][2] + 4, rows[2][3] + 8)
click(rows[2][2] + 4, rows[2][3] + 8)
check(sees("VERTICES") or sees("FACES") or sees("EDGES"), "mouse: a double click edits it")
for i = 0, 10 do mouse(300 + i * 4, 180 + i * 3) end
click(330, 200)
frames(2)
check(sees("chosen"), "mouse: the pointer follows, a click chooses")
mouse(330, 200, 0, 1)
mouse(330, 200, 2); mouse(350, 210, 2); mouse(370, 220, 2); mouse(370, 220, 0); frames(1)
click(330, 200, 2)
check(sees("Move") and sees("Extrude faces"), "mouse: the edit page's context menu")
mx, my = at("Palette")
click(mx, my)
check(sees("colour: arrows"), "mouse: Palette from the context menu")
click(124, 116)
check(not sees("colour: arrows"), "mouse: a click on a colour takes it")
mx, my = at("menu", nil, 0)
click(mx, my)
check(sees("Exit bm Mesh"), "mouse: the menu tab")
mx, my = at("Continue", 32)
click(mx, my)
check(not sees("Exit bm Mesh"), "mouse: a click on Continue")

-- the shared geometry (bm3d.lua, T.G): pure, no editor state
do
  local G = E.require("bm3d").G
  local it = { v = { { 0, 0, 0 }, { 9, 9, 9 }, { 1, 0, 0 }, { 0, 1, 0 } },
               f = { { 1, 3, 4, 0xFF0000 } }, vb = { 0, 1, 2, 3 }, dirty = true, name = "tri" }
  local sv = G.compact(it, { [1] = 1, [2] = 2, [4] = 3 })
  check(#it.v == 3 and it.f[1][1] == 1 and it.f[1][2] == 2 and it.f[1][3] == 3, "T.G.compact: the unused vertex goes, the corners follow")
  check(it.vb[1] == 0 and it.vb[2] == 2 and it.vb[3] == 3 and #it.vb == 3, "T.G.compact: the bones follow")
  check(sv[1] == 1 and sv[3] == 3 and sv[2] == nil, "T.G.compact: the selection is returned with the new numbers")
  it.f[2] = { 1, 2, 3, -1, { 0, 0, 1.5, 2, 3, 4 } }
  local bin = G.encode_model(it)
  local back = G.model_item("tri", bin)
  local again = G.encode_model({ name = "tri", v = back.v, f = back.f, dirty = true })
  check(#back.v == 3 and #back.f == 2 and back.f[2][4] == -1 and back.f[2][5][3] == 1.5, "T.G.model_item: reads what encode_model wrote")
  check(again == bin, "T.G.encode_model, model_item, encode_model: byte-equal")
end

-- T.G.primitive: counts, winding, bones
do
  local G = E.require("bm3d").G
  local want = { cube = { 8, 12 }, plane = { 4, 2 }, sphere = { 62, 120 }, cylinder = { 18, 32 } }
  for shape, w in pairs(want) do
    local it = { v = { { 5, 5, 5 } }, f = {}, vb = { 7 } }
    local nv = G.primitive(it, shape, { 1, 2, 3 }, 2, 0x123456, 4)
    check(#nv == w[1] and #it.v == 1 + w[1] and #it.f == w[2], "T.G.primitive " .. shape .. ": " .. w[1] .. " vertices, " .. w[2] .. " faces")
    check(nv[1] == 2 and nv[#nv] == #it.v, "T.G.primitive " .. shape .. ": returns the new numbers")
    check(it.vb[1] == 7 and it.vb[2] == 4 and it.vb[#it.v] == 4, "T.G.primitive " .. shape .. ": the bone of the new vertices")
    local mx = 0
    for _, i in ipairs(nv) do mx = math.max(mx, math.abs(it.v[i][1] - 1)) end
    check(math.abs(mx - 1) < 1e-6 or shape == "cylinder" or shape == "sphere", "T.G.primitive " .. shape .. ": size 2 about the centre")
    local ok = true
    local cc = { 1, 2, 3 }
    for _, f in ipairs(it.f) do
      if f[1] < 2 or f[4] ~= 0x123456 then ok = false end
      if shape ~= "plane" then
        local a, b, d = it.v[f[1]], it.v[f[2]], it.v[f[3]]
        local n = G.normal_of(it, f)
        local m = { (a[1] + b[1] + d[1]) / 3 - cc[1], (a[2] + b[2] + d[2]) / 3 - cc[2], (a[3] + b[3] + d[3]) / 3 - cc[3] }
        if n[1] * m[1] + n[2] * m[2] + n[3] * m[3] <= 0 then ok = false end
      end
    end
    check(ok, "T.G.primitive " .. shape .. ": the faces look out, in the colour")
  end
  local plain = { v = {}, f = {} }
  G.primitive(plain, "cube")
  check(plain.vb == nil and #plain.v == 8 and plain.v[1][1] == -0.5, "T.G.primitive: no bones, no centre: at 0, size 1")
end

-- APP-16: the live mirror covers adding a part and the loop cut (the knife stays unmirrored)
do
  local G = E.require("bm3d").G
  local function same(a, b) return math.abs(a - b) < 1e-5 end
  local function has(m, x, y, z)
    for _, p in ipairs(m.verts) do
      if same(p[1], x) and same(p[2], y) and same(p[3], z) then return true end
    end
    return false
  end
  local function lone_of(m)
    local n = 0
    for _, p in ipairs(m.verts) do if not has(m, -p[1], p[2], p[3]) then n = n + 1 end end
    return n
  end
  local function all_of(it) local l = {} for i = 1, #it.v do l[i] = i end return l end
  -- sphere and cylinder: the triangulation is mirror symmetric (every face and every edge middle has a twin)
  for _, shape in ipairs({ "sphere", "cylinder" }) do
    local it = { v = {}, f = {} }
    G.primitive(it, shape, { 0, 0, 0 }, 1, 0x808080)
    local list = {}
    for i = 1, #it.f do list[i] = i end
    local mp = G.mirror_pairs(it, all_of(it))
    local nov = 0
    for i = 1, #it.v do if not mp[i] then nov = nov + 1 end end
    local mf = G.mirror_faces(it, list, mp)
    local nof = 0
    for i = 1, #it.f do if not mf[i] then nof = nof + 1 end end
    check(nov == 0 and nof == 0, "T.G.primitive " .. shape .. ": mirror symmetric (" .. nov .. " vertices and " .. nof .. " faces with no twin)")
    local m = { verts = {} }
    local bad = 0
    for k, l in pairs(G.edge_faces(it)) do
      local a, b = G.edge_ends(k)
      local p, q = it.v[a], it.v[b]
      m.verts[#m.verts + 1] = { (p[1] + q[1]) / 2, (p[2] + q[2]) / 2, (p[3] + q[3]) / 2 }
      if #l ~= 2 then bad = bad + 1 end
    end
    check(lone_of(m) == 0, "T.G.primitive " .. shape .. ": every edge middle (what Subdivide adds) has a twin across x = 0 (" .. lone_of(m) .. " without)")
    check(bad == 0, "T.G.primitive " .. shape .. ": still closed (" .. bad .. " edges without 2 faces)")
  end
  -- T.G.primitive_mirror: off the plane two parts, on the plane one, the bone of the mirror part
  do
    local it = { v = {}, f = {}, vb = {} }
    local nv, twin = G.primitive_mirror(it, "cube", { 0.5, 1, 0 }, 1, 0x112233, 3, 4)
    check(#nv == 16 and #twin == 8 and #it.v == 16 and #it.f == 24, "T.G.primitive_mirror: a part off the plane: 2 parts (" .. #it.v .. "/" .. #it.f .. ")")
    local ok = true
    for i = 1, 8 do
      local p, q = it.v[nv[i]], it.v[twin[i]]
      if not (same(p[1], -q[1]) and same(p[2], q[2]) and same(p[3], q[3])) then ok = false end
      if it.vb[nv[i]] ~= 3 or it.vb[twin[i]] ~= 4 then ok = false end
    end
    check(ok, "T.G.primitive_mirror: the second part is the mirror image, on the mirror bone")
    local good = true
    for fi = 1, 12 do
      local f, g = it.f[fi], it.f[fi + 12]
      local n1, n2 = G.normal_of(it, f), G.normal_of(it, g)
      if not (same(n1[1], -n2[1]) and same(n1[2], n2[2]) and same(n1[3], n2[3])) or g[4] ~= 0x112233 then good = false end
    end
    check(good, "T.G.primitive_mirror: the faces of the mirror part look out too")
    local it2 = { v = {}, f = {} }
    local n2, t2 = G.primitive_mirror(it2, "sphere", { 0, 1, 0 }, 1, 0)
    check(#n2 == 62 and #t2 == 0 and #it2.v == 62 and #it2.f == 120, "T.G.primitive_mirror: a part in the middle (x = 0): one part")
    local it3 = { v = {}, f = {} }
    G.primitive_mirror(it3, "cylinder", { -0.3, 0, 0 }, 1, 0)
    local mp = G.mirror_pairs(it3, all_of(it3))
    local n = 0
    for i = 1, #it3.v do if not mp[i] then n = n + 1 end end
    check(#it3.v == 36 and n == 0, "T.G.primitive_mirror: a limb on the left: every vertex has a twin")
  end
  -- T.G.loop_cut_mirror: two limbs, one ring each; a ring that crosses x = 0 is cut once
  local function vertical_edge(it, lo, hi)
    for _, f in ipairs(it.f) do
      for k = 1, 3 do
        local p, q = f[k], f[k % 3 + 1]
        if p >= lo and p <= hi and q >= lo and q <= hi and it.v[p][2] ~= it.v[q][2]
           and same(it.v[p][1], it.v[q][1]) and same(it.v[p][3], it.v[q][3]) and math.abs(it.v[p][1]) > 0.1 then return p, q end
      end
    end
  end
  do
    local it = { v = {}, f = {} }
    G.primitive_mirror(it, "cylinder", { 1, 0, 0 }, 1, 0x808080)
    local va, vb = vertical_edge(it, 1, 18)
    local nv0, nf0 = #it.v, #it.f
    local res = G.loop_cut_mirror(it, va, vb)
    check(res and res.rings == 2 and #res.mids == 16 and #res.rungs == 16 and #it.v == nv0 + 16 and #it.f == nf0 + 32,
          "T.G.loop_cut_mirror: both limbs cut (" .. tostring(res and res.rings) .. " rings, " .. (#it.v - nv0) .. " new vertices, " .. (#it.f - nf0) .. " new faces)")
    local mp = G.mirror_pairs(it, all_of(it))
    local lone = 0
    for i = 1, #it.v do if not mp[i] then lone = lone + 1 end end
    check(lone == 0, "T.G.loop_cut_mirror: every vertex still has a twin (" .. lone .. " without)")
    local bad = 0
    for _, l in pairs(G.edge_faces(it)) do if #l ~= 2 then bad = bad + 1 end end
    check(bad == 0, "T.G.loop_cut_mirror: the model is still closed")
    -- no counterpart edge: one ring
    local it2 = { v = {}, f = {} }
    G.primitive(it2, "cylinder", { 1, 0, 0 }, 1, 0x808080)
    local res2 = G.loop_cut_mirror(it2, va, vb)
    check(res2 and res2.rings == 1 and #res2.mids == 8, "T.G.loop_cut_mirror: no counterpart edge: one ring")
    -- a ring that crosses x = 0 holds the counterpart edge: cut once
    local it3 = { v = {}, f = {} }
    G.primitive(it3, "cylinder", { 0, 0, 0 }, 1, 0x808080)
    local a3, b3 = vertical_edge(it3, 1, 18)
    local r3 = a3 and G.loop_cut_mirror(it3, a3, b3)
    check(r3 and r3.rings == 1 and #r3.mids == 8, "T.G.loop_cut_mirror: the ring crosses x = 0 and holds the counterpart edge: cut once")
  end
  -- in the editor
  local function mode(m)
    for _ = 1, 3 do
      if sees(m .. ":") then return end
      key("	")
    end
  end
  key("f1", "n")
  key("r")
  type_text("mirl")
  local name = "mirl"
  key("f2")
  mode("VERTICES")
  key("^s")
  local m0 = model_of("meshcopy.bme", name)
  check(m0 and m0.nv == 8 and m0.nf == 12, "mirror part: a new cube")
  -- mirror off: one part
  key("a", "a", "n", " ")
  key("A", "\n", "^s")
  local m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nv == 16 and m1.nf == 24, "mirror off: Shift+A adds one part (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  key("^z")
  -- mirror on, a vertex off the plane chosen: two parts, one undo step
  key("X")
  check(sees("MIRROR X"), "mirror part: the live mirror is on")
  key("a", "a", "n", " ")
  key("A", "\n")
  check(sees("VERTICES: 16 chosen") and status():find("mirror X", 1, true), "mirror on: the 16 new vertices are chosen: " .. status())
  key("^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nv == 24 and m1.nf == 36, "mirror on: Shift+A adds the part and its mirror image (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  check(m1 and lone_of(m1) == 0, "mirror on: every vertex has a twin across x = 0 (" .. (m1 and lone_of(m1) or "-") .. ")")
  key("^z", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nv == 8 and m1.nf == 12, "mirror part: one undo takes both parts away")
  -- mirror on, nothing chosen: the middle is x = 0, one part
  if not sees(": 0 chosen") then key("a") end
  check(sees("VERTICES: 0 chosen"), "nothing chosen")
  key("A", "down", "down", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nv == 70 and m1.nf == 132, "mirror on, a part in the middle (x = 0): added once (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  key("^z", "^s")
  -- a limb on each side, then the loop cut on one of them
  key("a", "a", "n", " ")
  key("A", "down", "\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(m1 and m1.nv == 8 + 36 and m1.nf == 12 + 64, "mirror on: two cylinders (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  local va, vb = vertical_edge({ v = m1.verts, f = m1.faces }, 9, 26)
  local keys = {}
  for k in pairs(G.edge_faces({ f = m1.faces })) do keys[#keys + 1] = k end
  table.sort(keys)
  local no
  for i, k in ipairs(keys) do if k == G.edge_key(va, vb) then no = i end end
  mode("EDGES")
  key("a")
  key("home")
  for _ = 1, no do key("n") end
  key(" ")
  check(sees("EDGES: 2 chosen"), "loop cut: the edge and its counterpart are chosen with the mirror on (" .. status() .. ")")
  key("^r")
  check(sees("EDGES: 16 chosen") and status():find("both rings", 1, true), "loop cut: the new edges of both rings are chosen: " .. status())
  key("^s")
  local m2 = model_of("meshcopy.bme", name)
  check(m2 and m2.nv == m1.nv + 16 and m2.nf == m1.nf + 32, "loop cut with the mirror on: both rings (" .. (m2 and ((m2.nv - m1.nv) .. "/" .. (m2.nf - m1.nf)) or "-") .. ")")
  check(m2 and lone_of(m2) == 0, "loop cut with the mirror on: every vertex still has a twin")
  key("^z", "^s")
  m2 = model_of("meshcopy.bme", name)
  check(m2 and m2.nv == m1.nv and m2.nf == m1.nf, "loop cut with the mirror on: one undo step")
  -- mirror off: one ring
  key("X")
  if not sees(": 0 chosen") then key("a") end
  key("home")
  for _ = 1, no do key("n") end
  key(" ")
  check(sees("EDGES: 1 chosen"), "mirror off: one edge chosen")
  key("^r", "^s")
  m2 = model_of("meshcopy.bme", name)
  check(m2 and m2.nv == m1.nv + 8 and m2.nf == m1.nf + 16, "loop cut with the mirror off: one ring only (" .. (m2 and ((m2.nv - m1.nv) .. "/" .. (m2.nf - m1.nf)) or "-") .. ")")
  key("^z", "^s")
  key("f1")
  goto_item("M " .. name)
  key("del", "del", "^s")
  check(not model_of("meshcopy.bme", name), "the mirror part test cube is deleted")
end

-- Shift+S / Shift+G: snap to the grid and to a vertex while a vertex moves
do
  local function same(a, b) return math.abs(a - b) < 1e-5 end
  local function changed(m0, m1)
    local out = {}
    for i = 1, m0.nv do
      for k = 1, 3 do if not same(m0.verts[i][k], m1.verts[i][k]) then out[#out + 1] = i; break end end
    end
    return out
  end
  local function pick1(i)
    if not sees(": 0 chosen") then key("a") end
    key("home")
    for _ = 1, i do key("n") end
    key(" ")
  end
  local function rep(k, n) for _ = 1, n do key(k) end end
  key("f1", "n")
  key("r")
  type_text("sgrt")
  local name = "sgrt"
  key("f2")
  for _ = 1, 3 do if sees("VERTICES:") then break end key("	") end
  key("^s")
  local m0 = model_of("meshcopy.bme", name)
  check(m0 and m0.nv == 8, "snap: a new cube")
  -- the mode in the header and the message
  check(not sees("SNAP"), "snap: nothing in the header while it is off")
  key("S")
  check(sees("SNAP grid 0.1") and status():find("SNAP grid 0.1", 1, true), "Shift+S: grid 0.1 (" .. status() .. ")")
  key("S")
  check(sees("SNAP vertex") and status():find("SNAP vertex", 1, true), "Shift+S: vertex")
  key("S")
  check(sees("SNAP grid 0.1 + vertex"), "Shift+S: grid + vertex")
  key("S")
  check(not sees("SNAP") and status():find("snap off", 1, true), "Shift+S: off again")
  key("G")
  check(status():find("grid step 0.25", 1, true), "Shift+G: the step 0.25 (" .. status() .. ")")
  key("G", "G", "G")
  check(status():find("grid step 0.1", 1, true), "Shift+G: 0.5, 0.05, 0.1 in a ring (" .. status() .. ")")
  -- off: the old behaviour, 7 steps of 0.01 = 0.07
  pick1(1)
  key("g", "x", ",", ",")
  rep("right", 7)
  key("\n", "^s")
  local m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 1 and same(m1.verts[1][1], m0.verts[1][1] + 0.07), "snap off: -0.43, as before")
  key("^z", "^s")
  -- grid 0.1 on each axis: -0.43 -> -0.4 (the move step stays 0.01)
  key("S")
  for axis, k in ipairs({ "x", "y", "z" }) do
    key("g", k)
    rep("right", 7)
    key("\n", "^s")
    m1 = model_of("meshcopy.bme", name)
    local ch = changed(m0, m1)
    check(#ch == 1 and ch[1] == 1 and same(m1.verts[1][axis], m0.verts[1][axis] + 0.1),
          "snap grid, axis " .. k .. ": the vertex goes to the grid (" .. tostring(m1.verts[1][axis]) .. ")")
    key("^z", "^s")
    m1 = model_of("meshcopy.bme", name)
    check(#changed(m0, m1) == 0, "snap grid, axis " .. k .. ": one undo step")
  end
  -- a coarser step from inside the tool: 0.25 -> -0.43 goes back to -0.5 (the move is 0)
  key("g", "x")
  rep("right", 7)
  key("G")
  check(status():find("grid step 0.25", 1, true), "Shift+G inside the tool")
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 0, "grid 0.25: -0.43 goes to -0.5")
  key("^z")
  key("G", "G", "G")                             -- 0.5, 0.05, 0.1
  -- free move (no axis lock): the other axes are on the grid too
  key("g")
  rep("up", 7)
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 1 and same(m1.verts[1][2], m0.verts[1][2] + 0.1) and same(m1.verts[1][1], m0.verts[1][1]),
        "snap grid, free move: y on the grid, x and z as they were")
  key("^z", "^s")
  -- the live mirror: the counterpart follows the same move, one undo step
  key("X")
  key("g", "x")
  rep("right", 7)
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  local ch = changed(m0, m1)
  check(#ch == 2 and same(m1.verts[1][1], -0.4) and same(m1.verts[2][1], 0.4),
        "snap + mirror: v1 on the grid, v2 mirrored (" .. table.concat(ch, ",") .. ")")
  check(sees("8 of"), "snap: the counters stay")
  key("^z", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 0, "snap + mirror: one undo takes back both")
  key("X")
  -- vertex: the leader goes to a vertex within 6 px. v1 moves along x towards v2.
  key("S")                                   -- vertex only
  key("g", "x", ".", ".", "esc")             -- the move step 0.1 again
  rep("-", 10)
  local function px_to_v2(dx)
    local ax, ay = E.project3d(m0.verts[1][1] + dx, m0.verts[1][2], m0.verts[1][3])
    local bx, by = E.project3d(m0.verts[2][1], m0.verts[2][2], m0.verts[2][3])
    return ax and bx and math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2) or -1
  end
  check(same(m0.verts[2][2], m0.verts[1][2]) and same(m0.verts[2][3], m0.verts[1][3]), "snap: v2 lies on the x line of v1")
  local out_n = 0
  for n = 1, 9 do if px_to_v2(n * 0.1) > 6.5 then out_n = n end end
  local in_n = out_n + 1
  check(out_n >= 1 and in_n <= 10 and px_to_v2(in_n * 0.1) < 5.5,
        "snap: the test steps are out and in of the 6 px range (" .. out_n .. ", " .. px_to_v2(out_n * 0.1) .. ", " .. px_to_v2(in_n * 0.1) .. ")")
  key("g", "x")
  rep("right", out_n)
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(same(m1.verts[1][1], m0.verts[1][1] + out_n * 0.1), "snap vertex: out of range the vertex stays where it is (" .. tostring(m1.verts[1][1]) .. ")")
  key("^z", "^s")
  key("g", "x")
  rep("right", in_n)
  check(sees("SNAP vertex"), "snap vertex: the tool line shows the mode")
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(same(m1.verts[1][1], m0.verts[2][1]) and #changed(m0, m1) == 1, "snap vertex: in range the vertex goes to v2 (" .. tostring(m1.verts[1][1]) .. ")")
  key("^z", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(#changed(m0, m1) == 0, "snap vertex: one undo step")
  -- grid + vertex: in range the vertex wins; out of range the grid
  key("S")
  key("g", "x")
  rep("right", in_n)
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(same(m1.verts[1][1], m0.verts[2][1]), "snap grid + vertex: in range, the vertex wins")
  key("^z")
  key("G")                                     -- 0.25
  key("g", "x")
  rep("right", out_n)
  key("\n", "^s")
  m1 = model_of("meshcopy.bme", name)
  check(same(m1.verts[1][1], math.floor((m0.verts[1][1] + out_n * 0.1) / 0.25 + 0.5) * 0.25),
        "snap grid + vertex: out of range, the grid (" .. tostring(m1.verts[1][1]) .. ")")
  key("^z")
  key("S")
  check(not sees("SNAP"), "snap: off again")
  key("f1")
  goto_item("M sgrt")
  key("del", "del", "^s")
  check(not model_of("meshcopy.bme", name), "the snap test cube is deleted")
  if model_of("meshcopy.bme", "cube") then          -- the list pad makes a second cube: it goes too
    goto_item("M cube")
    key("del", "del", "^s")
  end
  check(not model_of("meshcopy.bme", "cube"), "no cube is left in the village copy")
  -- T.G.snap_*
  local G = E.require("bm3d").G
  local sq = G.snap_grid({ 0.43, -0.07, 1.26 }, 0.1)
  check(same(sq[1], 0.4) and same(sq[2], -0.1) and same(sq[3], 1.3), "T.G.snap_grid: each axis to the step")
  local sa = G.snap_grid({ 0.43, -0.07, 1.26 }, 0.25, 3)
  check(same(sa[1], 0.43) and same(sa[2], -0.07) and same(sa[3], 1.25), "T.G.snap_grid: only the axis given")
  local it = { v = { { 0, 0, 0 }, { 10, 0, 0 }, { 5, 5, 0 } }, f = {} }
  local function proj(p) return p[1] * 10, p[2] * 10 end            -- 10 px a unit
  check(G.snap_vertex(it, { 10.4, 0.1, 0 }, proj, 6) == 2, "T.G.snap_vertex: a vertex within 6 px")
  check(G.snap_vertex(it, { 10.7, 0, 0 }, proj, 6) == nil, "T.G.snap_vertex: 7 px away: none")
  check(G.snap_vertex(it, { 10.4, 0.1, 0 }, proj, 6, { [2] = true }) == nil, "T.G.snap_vertex: a skipped vertex is no target")
  local q, t = G.snap_pos(it, { 10.3, 0.2, 0 }, 3, 0.25, proj, 6)
  check(t == 2 and q[1] == 10 and q[2] == 0, "T.G.snap_pos: both, the vertex wins")
  q, t = G.snap_pos(it, { 3.3, 0.2, 0 }, 3, 0.25, proj, 6)
  check(t == nil and same(q[1], 3.25) and same(q[2], 0.25), "T.G.snap_pos: both, out of range the grid")
  q, t = G.snap_pos(it, { 3.3, 0.2, 0 }, 2, 0.25, proj, 6)
  check(t == nil and q[1] == 3.3, "T.G.snap_pos: vertex only, nothing in range: no change")
  q, t = G.snap_pos(it, { 3.3, 0.2, 0 }, 0, 0.25, proj, 6)
  check(t == nil and q[1] == 3.3 and q[2] == 0.2, "T.G.snap_pos: off")
  q, t = G.snap_pos(it, { 10.3, 0.2, 0 }, 3, 0.25, proj, 6, nil, 1)
  check(t == 2 and q[1] == 10 and q[2] == 0.2, "T.G.snap_pos: an axis lock takes only that axis of the vertex")
  q, t = G.snap_pos(it, { 3.3, 0.2, 0 }, 1, 0.25, proj, 6, nil, "n")
  check(t == nil and q[1] == 3.3, "T.G.snap_pos: along the normal the grid does not apply")
end

-- models from /bmm and /glb (APP-9): the list of both, a .bmm and a .glb brought in as models, the
-- name asked, the texture, the skeleton, the export to /bmm, the messages of the functions
do
  local card = MF.card
  for _, d in pairs(card) do for k in pairs(d) do d[k] = nil end end
  MF.calls, MF.writes = {}, {}
  local SHEET = string.pack("<I2I2", 8, 8) .. string.rep("\10\20\30\255", 64)
  MF.put_bmm("kite.bmm", "Kite", "kite", 2)
  MF.put_bmm("tex.bmm", "Tex", "tex", 2, { textured = true, sheet = SHEET })
  MF.put_bmm("rigged.bmm", "Rigged", "rigged", 3, { rig = true })
  card["/glb"]["kite.glb"] = "glTF"
  card["/glb"]["robot.glb"] = "glTF"
  card["/glb"]["longmodelname.glb"] = "glTF"
  card["/glb"]["bad.glb"] = "glTF"
  card["/glb"]["huge.glb"] = "glTF"
  card["/glb"]["notes.txt"] = "text"
  local function row_of(name)
    local seen, list = {}, {}
    for _, d in ipairs({ "/bmm", "/glb" }) do
      for f in pairs(MF.card[d]) do
        local stem = f:lower():match("^(.+)%.[bg][ml][mb]$")
        if stem and not seen[stem] then seen[stem] = true; list[#list + 1] = stem end
      end
    end
    table.sort(list)
    for i, n in ipairs(list) do if n == name then return i end end
  end
  local function import_row(name, opener)
    key(opener or "i")
    key("pgup")
    for _ = 2, row_of(name) do key("down") end
    key("\n")
  end
  local function bmm_has(name) return MF.card["/bmm"][name .. ".bmm"] ~= nil end

  key("f1", "esc", "down", "down", "down", "\n")           -- Save as: a project of its own
  type_text("IMPORT")
  local project = status():match("saved /bme/(%S+)")
  check(project == "import.bme", "import: a project of its own: " .. status())
  local n0 = #models_of(project)

  key("i")
  check(sees("import a model (/bmm, /glb)") and sees("robot   .glb: converted to /bmm") and sees("kite   .bmm (and .glb)")
        and sees("tex   .bmm") and not sees("notes"), "import: the list of /bmm and /glb, the .glb only ones marked")
  key("esc")
  check(in_help("i / x", "import a model") and in_help("ctrl i", "import") and in_help("ctrl e", "export"),
        "keyhelp: import and export")
  key("f3")
  check(sees("Import a model: /bmm, /glb") and sees("Export to /bmm"), "F3: the commands of the list: import and export")
  key("esc", "f2", "f3", "up")
  check(sees("Import a model: /bmm, /glb (Ctrl+I)") and sees("Export to /bmm (Ctrl+E)"), "F3: the commands of the edit page too")
  key("esc", "f1")

  import_row("bad")
  check(status():find("not a .glb file", 1, true) and not bmm_has("bad"), "import: a bad .glb gives the message of glb3d: " .. status())
  import_row("huge", "^i")
  check(status():find("the format holds 65535", 1, true), "import: the format limit message of glb3d: " .. status())
  import_row("kite")
  check(status():find("kite.bmm: 4 vertices, 2 triangles", 1, true), "import: the counters: " .. status())
  check(sees("VERTICES") or sees("FACES") or sees("EDGES"), "import: the model opens in the edit page")
  key("f1")
  check(chosen() == "M kite", "import: kite.bmm is a new model: " .. tostring(chosen()))
  key("^s")
  local k = model_of(project, "kite")
  check(k and k.nf == 2 and k.nv == 4 and #models_of(project) == n0 + 1, "import: saved in the project, 4 vertices and 2 triangles")
  check(#MF.calls == 2, "import: a .bmm reads no .glb")

  import_row("longmodelname")
  check(sees("new name in /bmm") and status():find("1 to 8", 1, true), "import: a long .glb name asks a new name: " .. status())
  type_text("kite")
  check(sees("new name in /bmm") and status():find("/bmm/kite.bmm is there already", 1, true) and not bmm_has("longmod"),
        "import: a taken name asks again: " .. status())
  type_text("longmod")
  check(status():find("longmodelname.glb to /bmm/longmod.bmm: 4 vertices, 3 triangles", 1, true), "import: the counters: " .. status())
  key("f1")
  check(bmm_has("longmod") and chosen() == "M longmodelname", "import: the .glb is converted to /bmm/longmod.bmm and opened: " .. tostring(chosen()))
  check(MF.card["/glb"]["longmodelname.glb"] == "glTF", "import: /glb stays as it was")
  key("f1")

  import_row("robot", "^i")
  key("f1")
  check(bmm_has("robot") and chosen() == "M robot", "import: robot.glb converted at once (Ctrl+I)")
  import_row("tex")
  check(status():find("1 textured faces flat grey", 1, true), "import: a textured .bmm: flat grey: " .. status())
  key("f1")
  import_row("rigged")
  key("f1", "^s")
  local r = anim_rigs(section(file(project), 9)).rigged
  check(r and r.nb == 1 and r.nv == 4, "import: a .bmm with a skeleton keeps it")
  local t = model_of(project, "tex")
  check(t and t.faces[1][4] == 0x808080 and t.faces[2][4] == 0x336620, "import: the textured face is flat grey in the project")

  -- the export: the name asked, a file that is there replaced only when chosen
  goto_item("M kite")
  key("x")
  check(sees("export to /bmm: name"), "export: x asks the name")
  type_text("kite")
  check(sees("/bmm/kite.bmm is there already") and sees("Replace it"), "export: a name that is taken: the choice")
  key("\n")                              -- (the cursor is on Another name...)
  check(sees("export to /bmm: name"), "export: Another name... asks again")
  type_text("mine")
  local out = MF.card["/bmm"]["mine.bmm"]
  check(out and out.sections[8] and string.unpack("<I2I2", out.sections[8], 8 + 16 + 1) == 4 and out.sections[9] == nil,
        "export: /bmm/mine.bmm has the model")
  check(status():find("exported to /bmm/mine.bmm: 4 vertices, 2 triangles", 1, true), "export: the counters: " .. status())
  goto_item("M rigged")
  key("^e")
  type_text("kite")
  key("up", "\n")
  check(MF.card["/bmm"]["kite.bmm"].sections[9] ~= nil and status():find("exported to /bmm/kite.bmm", 1, true),
        "export: Replace it changes the file, the skeleton goes with it")
  local nw = #MF.writes
  key("x")
  key("esc")
  check(#MF.writes == nw, "export: Esc writes nothing")
  key("esc")                             -- the menu
  check(sees("Import a model...") and sees("Export to /bmm..."), "import, export: in the menu")
  for _ = 1, 5 do key("down") end
  key("\n")
  check(sees("import a model (/bmm, /glb)"), "import: the menu item")
  key("esc")
  key("f2", "^e")
  check(sees("export to /bmm: name"), "export: Ctrl+E in the edit page")
  key("esc", "f1")
end


-- the ghost reference (APP-10 step 7): a model of /bmm or /glb as a faint wire; never chosen, counted or saved
do
  local GC = 0x3E7C68
  local ghost_lines, line0 = 0, E.line
  E.line = function(x0, y0, x1, y1, c)
    if c == GC then ghost_lines = ghost_lines + 1 end
    if line0 then return line0(x0, y0, x1, y1, c) end
  end
  MF.card["/glb"]["ref.glb"] = "glTF"
  local function drawn() ghost_lines = 0; frames(1); return ghost_lines end
  local function ghost_row(name, has)
    local seen, list = {}, {}
    for _, d in ipairs({ "/bmm", "/glb" }) do
      for f in pairs(MF.card[d]) do
        local stem = f:lower():match("^(.+)%.[bg][ml][mb]$")
        if stem and not seen[stem] then seen[stem] = true; list[#list + 1] = stem end
      end
    end
    table.sort(list)
    key("H")
    key("pgup")
    local at = 1
    for i, n in ipairs(list) do if n == name then at = i end end
    for _ = 1, at - 1 + (has and 1 or 0) do key("down") end
    key("\n")
  end
  key("f1")
  goto_item("M kite")
  key("f2")
  local counters = "4 of 65535 vertices  2 triangles"
  check(sees(counters) and not sees("GHOST"), "ghost: none at first, the counters: 4 vertices, 2 triangles")
  check(in_help("h / shift h", "ghost") and in_help("[ ] / { }", "scale"), "ghost: keyhelp")
  key("f3")
  for _ = 1, 8 do key("up") end
  check(sees("Ghost: load a model, clear it (H)") and sees("Ghost: show / hide (h)"), "ghost: the commands list")
  key("esc")
  key("h")
  check(status():find("no ghost", 1, true), "ghost: h with none says so: " .. status())
  key("^s")
  local project = status():match("saved /bme/(%S+)")
  local bytes0 = file(project)
  local nw, ncalls = #MF.writes, #MF.calls
  local function nbmm() local n = 0; for _ in pairs(MF.card["/bmm"]) do n = n + 1 end; return n end
  local nb0 = nbmm()

  -- a .glb: read, not written
  key("H")
  check(sees("ghost: a model as a reference") and sees("ref   .glb: read only, not converted") and sees("kite   .bmm"),
        "ghost: the chooser lists /bmm and /glb")
  key("esc")
  ghost_row("ref")
  check(status():find("ghost ref.glb: 4 vertices, 3 triangles", 1, true), "ghost: the .glb loads: " .. status())
  check(sees("GHOST ref x1.00") and sees(counters), "ghost: shown on the screen, the counters are the mesh's")
  check(#MF.writes == nw and nbmm() == nb0 and MF.card["/bmm"]["ref.bmm"] == nil and #MF.calls == ncalls + 1,
        "ghost: a .glb is read by glb3d and not written to /bmm")
  check(MF.calls[#MF.calls] == "/glb/ref.glb", "ghost: the .glb path read: " .. tostring(MF.calls[#MF.calls]))
  local n1 = drawn()
  check(n1 == 6, "ghost: the wire of a tetrahedron: 6 edges drawn (" .. n1 .. ")")
  key("^s")
  check(file(project) == bytes0, "ghost: the saved project has the same bytes with the ghost shown")
  key("a")
  check(sees(counters), "ghost: select all does not count it")

  -- toggle
  key("h")
  check(drawn() == 0 and sees("(hidden)") and status():find("hidden", 1, true), "ghost: h hides it: " .. status())
  key("h")
  check(drawn() == 6 and not sees("(hidden)"), "ghost: h shows it again")

  -- scale and height
  key("]")
  check(sees("x1.05") and status():find("scale 1.05, height +0.00", 1, true), "ghost: ] grows by 5 percent: " .. status())
  key("[", "[")
  check(sees("x0.95"), "ghost: [ shrinks")
  key("{", "{")
  check(status():find("height -0.10", 1, true), "ghost: { lowers by 0.05: " .. status())
  key("}", "}", "}")
  check(status():find("height +0.05", 1, true), "ghost: } lifts: " .. status())
  for _ = 1, 80 do key("]") end
  check(sees("x10.00"), "ghost: the scale stops at 10")
  for _ = 1, 140 do key("[") end
  check(sees("x0.10"), "ghost: the scale stops at 0.1")
  key("f1")
  key("]")                                       -- on the list the keys still change the mesh
  check(chosen() ~= "M kite", "ghost: [ ] change the mesh in the list")
  key("[")
  goto_item("M kite")
  key("f2")
  check(sees("GHOST ref"), "ghost: it stays when the page changes")
  key("^s")
  check(file(project) == bytes0, "ghost: scaled and lifted, the saved bytes are the same")

  -- a .bmm replaces it
  ghost_row("kite", true)
  check(status():find("ghost kite.bmm: 4 vertices, 3 triangles", 1, true) and sees("GHOST kite x1.00"),
        "ghost: a .bmm loads and replaces: " .. status())
  check(drawn() == 6, "ghost: the wire of 3 triangles: 6 edges drawn")
  check(#MF.writes == nw, "ghost: a .bmm is not written")

  -- the live mirror does not act on it
  key("X")
  key("a", "g", "x", "right", "\n")
  check(drawn() == 6, "ghost: the live mirror does not touch it")
  key("^z", "X")

  -- a bad file keeps the ghost
  ghost_row("bad", true)
  check(status():find("not a .glb file", 1, true) and sees("GHOST kite"), "ghost: a bad .glb says why and keeps the ghost: " .. status())

  -- the detail row of the chooser and the thinned wire: a strip of 9000 triangles (18001 edges)
  do
    local NT = 9000
    local rec = { string.rep("strip", 1) .. string.rep("\0", 11), string.pack("<I2I2I4", NT + 2, NT, 0) }
    for i = 1, NT + 2 do rec[#rec + 1] = string.pack("<fff", (i % 100) * 0.02 - 1, (i // 100) * 0.02 - 0.8, 0) end
    for i = 1, NT do
      rec[#rec + 1] = string.pack("<I2I2I2I2I4I2I2I2I2I2I2", i - 1, i, i + 1, 0, 0x336600, 0, 0, 0, 0, 0, 0)
    end
    MF.card["/bmm"]["strip.bmm"] = { kind = 1, name = "Strip", author = "tests",
      sections = { [8] = string.pack("<I2I2I4", 1, 64, 0) .. table.concat(rec) } }
    local function detail_row()                  -- the chooser, the last row: Enter cycles the detail
      key("H"); key("pgdn"); key("pgdn"); key("\n")
    end
    key("H"); key("pgdn"); key("pgdn")
    check(sees("Ghost detail: 20000 faces (Enter: next)"), "ghost: the last row of the chooser is the detail: 20000 at first")
    key("esc")
    ghost_row("strip", true)
    check(status():find("strip.bmm: 9002 vertices, 9000 triangles", 1, true) and status():find("each frame draws 1 edge in 5", 1, true)
          and not status():find("1 in", 1, true), "ghost: 9000 faces all kept, the wire thinned: " .. status())
    local first, total = drawn(), 0
    check(first > 0 and first <= 4000, "ghost: a frame draws at most 4000 edges (" .. first .. ")")
    for _ = 1, 5 do total = total + drawn() end
    check(total > 4000 and total <= 5 * 4000, "ghost: the window moves, 5 frames draw more than one (" .. total .. ")")
    detail_row()
    check(sees("Ghost detail: all faces") and status():find("strip.bmm: 9002 vertices, 9000 triangles", 1, true)
          and not status():find("the budget)", 1, true), "ghost: detail all: the ghost is read again: " .. status())
    detail_row()
    check(sees("Ghost detail: 2000 faces") and status():find("keeps 1800 triangles", 1, true),
          "ghost: detail 2000: 1 triangle in 5: " .. status())
    check(drawn() > 0 and drawn() <= 4000, "ghost: the thinned model draws edges")
    detail_row()
    check(sees("Ghost detail: 5000 faces") and status():find("keeps 4500 triangles", 1, true),
          "ghost: detail 5000: 1 triangle in 2: " .. status())
    key("]")                                    -- the chooser is still open: ] does nothing to the ghost
    key("esc")
    key("]")
    key("]")
    check(sees("x1.10"), "ghost: scale is kept (x1.10)")
    detail_row()
    check(sees("Ghost detail: 20000 faces") and sees("x1.10") and file(project) == bytes0,
          "ghost: back to 20000, scale kept, project bytes the same")
    key("esc")
    MF.card["/bmm"]["strip.bmm"] = nil
    ghost_row("kite", true)
    check(status():find("ghost kite.bmm", 1, true), "ghost: kite again: " .. status())
  end

  -- the budget: a ghost over it keeps some of the faces and says so
  local G = E.require("bm3d").G
  local item = { name = "big", from = "bmm", v = {}, f = {} }
  for _ = 1, 5000 do
    local b = #item.v
    item.v[b + 1] = { b, 0, 0 }
    item.v[b + 2] = { b, 1, 0 }
    item.v[b + 3] = { b + 1, 0, 0 }
    item.f[#item.f + 1] = { b + 1, b + 2, b + 3, 0x808080 }
  end
  check(G.GHOST_FACES == 20000 and G.GHOST_DETAIL[1] == 2000 and G.GHOST_DETAIL[2] == 5000 and G.GHOST_DETAIL[3] == 20000
        and G.GHOST_DETAIL[4] == 65535 and G.GHOST_EDGES_FRAME == 4000, "T.G: the budget constants")
  local gd = G.ghost_make(item)
  check(gd.stride == 1 and gd.shown == 5000 and gd.every == 4 and G.ghost_text(gd):find("1 edge in 4", 1, true),
        "T.G.ghost_make: the default budget (20000) keeps 5000 faces; 15000 edges are drawn 1 in 4: " .. G.ghost_text(gd))
  local g = G.ghost_make(item, 2000)
  check(g.nf == 5000 and g.stride == 3 and g.shown == 1667 and #g.ea == 3 * 1667,
        "T.G.ghost_make: over the budget: every 3rd face (" .. g.shown .. ")")
  check(G.ghost_text(g):find("keeps 1667 triangles (1 in 3: the budget)", 1, true), "T.G.ghost_text: the budget is said: " .. G.ghost_text(g))
  local g2 = G.ghost_make(item, 5000)
  check(g2.stride == 1 and g2.shown == 5000 and #g2.used == 15000, "T.G.ghost_make: a budget that holds all")
  check(g.v[1] ~= nil and g.v[4] == nil, "T.G.ghost_make: only the vertices of the kept faces are kept")
  local gs = { v = { { 1, 2, 3 } }, scale = 2, dy = 0.5 }
  local gx, gy, gz = G.ghost_pos(gs, 1)
  check(gx == 2 and gy == 4.5 and gz == 6, "T.G.ghost_pos: scale then height")

  -- clear
  key("H")
  key("pgup")
  key("\n")                                      -- the first row is "Clear the ghost"
  check(status():find("ghost cleared", 1, true) and not sees("GHOST") and drawn() == 0, "ghost: Clear removes it: " .. status())
  key("h")
  check(status():find("no ghost", 1, true), "ghost: after clear h says there is none")
  key("^s")
  check(file(project) == bytes0, "ghost: after clear the project is the same bytes")
  check(sees(counters), "ghost: the counters are the mesh's at the end")
  E.line = line0
end

-- bones in bm Mesh (APP-14): a key gives the chosen vertices (or a part, l) to a bone; the header shows it
do
  local G = E.require("bm3d").G
  -- the names of the sides
  check(G.side_name("arm_l") == "arm_r" and G.side_name("hand.R") == "hand.L" and G.side_name("Leg_L") == "Leg_R" and
        G.side_name("l_foot") == "r_foot" and G.side_name("armLeft") == "armRight" and G.side_name("armright") == "armleft" and
        G.side_name("spine") == nil and G.side_name("pelvis") == nil, "T.G.side_name: l and r, Left and Right, either end")
  -- a rig of 5 bones: pelvis, arm_l (head x +), arm_r (head x -), hand.L with no hand.R, spine
  local function rig_of(names, heads, nv, vbs)
    local out = { string.pack("<I2I2I2I2", #names, 0, nv, 0) }
    for i, n in ipairs(names) do
      out[#out + 1] = (n .. string.rep("\0", 16)):sub(1, 16) .. string.pack("<i2I2ffffff", -1, 0, heads[i], 0, 0, heads[i], 1, 0)
    end
    local b = {}
    for i = 1, nv do b[i] = string.char(vbs[i]) end
    out[#out + 1] = table.concat(b) .. string.rep("\0", (4 - nv % 4) % 4)
    return ("x"):rep(16) .. table.concat(out)
  end
  local ac = rig_of({ "pelvis", "arm_l", "arm_r", "hand.L", "spine" }, { 0, 0.5, -0.5, 0.8, 0 }, 6, { 0, 0, 0, 0, 0, 0 })
  local it = { kind = "model", name = "t", ac = ac, bones = 5, clips = 0,
               v = { { 0.5, 1, 0 }, { -0.5, 1, 0 }, { 0.5, 2, 0 }, { -0.5, 2, 0 }, { 0, 1, 0 }, { 0.3, 5, 0 } },
               f = {}, vb = { 0, 0, 0, 0, 0, 0 } }
  local rig = G.rig_bones(it)
  check(#rig == 5 and rig[2].name == "arm_l" and math.abs(rig[2].hx - 0.5) < 1e-6 and rig[3].hx < 0, "T.G.rig_bones: names and head x")
  check(G.mirror_bone(rig, 1) == 2 and G.mirror_bone(rig, 2) == 1 and G.mirror_bone(rig, 3) == 3 and G.mirror_bone(rig, 4) == 4,
        "T.G.mirror_bone: arm_l <-> arm_r; hand.L has no mirror: itself; spine itself")
  local n = G.set_bone(it, { 1, 3 }, 1, false)
  check(n == 2 and it.vb[1] == 1 and it.vb[3] == 1 and it.vb[2] == 0 and it.vb[4] == 0, "T.G.set_bone: without the mirror only the chosen")
  n = G.set_bone(it, { 1, 3, 5 }, 1, true)
  check(n == 5 and it.vb[2] == 2 and it.vb[4] == 2 and it.vb[5] == 1 and it.vb[1] == 1 and it.vb[6] == 0,
        "T.G.set_bone: mirror: the counterparts take arm_r, a vertex on x = 0 takes arm_l, a vertex with no pair stays")
  G.set_bone(it, { 2 }, 3, true)
  check(it.vb[2] == 3 and it.vb[1] == 3, "T.G.set_bone: a bone with no mirror name: the counterpart takes the same bone")
  G.set_bone(it, { 1, 2, 3, 4 }, 1, true)
  check(it.vb[1] == 1 and it.vb[3] == 1 and it.vb[2] == 2 and it.vb[4] == 2,
        "T.G.set_bone: both sides chosen: the side of the bone's head keeps it, the other side takes the mirror bone")
  local nn, err = G.set_bone(it, { 1 }, 9, false)
  check(nn == nil and err:find("no bone 9", 1, true), "T.G.set_bone: no such bone")
  nn, err = G.set_bone({ v = {}, f = {} }, { 1 }, 0, false)
  check(nn == nil and err:find("no skeleton", 1, true), "T.G.set_bone: no skeleton")
  nn, err = G.set_bone({ v = {}, f = {}, ac = ac }, { 1 }, 0, false)
  check(nn == nil and err:find("does not fit", 1, true), "T.G.set_bone: a skeleton that does not fit")
  -- a skeleton with no vertices yet: every vertex is on bone 0 until it is given another
  local ac0 = rig_of({ "pelvis", "arm_l", "arm_r" }, { 0, 0.5, -0.5 }, 0, {})
  local un = { v = { { 0.5, 1, 0 }, { -0.5, 1, 0 } }, f = {}, ac = ac0 }
  check(G.skin_state(un) == "unskinned" and G.skin_state(it) == "skinned" and G.skin_state({ v = {}, f = {} }) == "none" and
        G.skin_state({ v = {}, f = {}, ac = ac }) == "mismatch", "T.G.skin_state: none, skinned, unskinned, mismatch")
  nn = G.set_bone(un, { 1 }, 1, true)
  check(nn == 2 and un.vb[1] == 1 and un.vb[2] == 2, "T.G.set_bone: an unskinned skeleton takes bones at the first give")
  local b1, c1 = G.bone_summary(it, { 1, 3 })
  local b2, c2 = G.bone_summary(it, { 1, 2 })
  check(b1 == 1 and c1 == 1 and b2 == 1 and c2 == 2 and G.bone_summary(it, {}) == nil, "T.G.bone_summary")

  -- on the page: the villager (7 bones)
  key("f1")
  goto_item("M villager")
  key("f2", "a", "a")
  local function one_vertex() key("a"); if not sees("VERTICES: 0 chosen") then key("a") end; key("n", " ") end
  local function header()
    for _, t in ipairs(texts) do if t[3] == 32 and t[1]:find("^BONE") then return t[1] end end
    return ""
  end
  key("B")
  check(status():find("choose vertices", 1, true), "Shift+B: nothing chosen says so: " .. status())
  key("a")
  check(header():find("^BONES %d+:"), "all chosen: the header says how many bones: " .. header())
  key("a", "n", " ")
  check(sees("VERTICES: 1 chosen") and header():find("^BONE %d+ %S+$"), "one vertex: the header has its bone: " .. header())
  key("^s")
  local project = status():match("saved /bme/(%S+)")
  local function vbs() return anim_rigs(section(file(project), 9)).villager.vb end
  local vb0 = vbs()
  key("B")
  check(sees("1 vertices to a bone") and sees(" 0  ") and sees(" 6  "), "Shift+B: the bones listed, 0 to 6")
  key("down", "\n")                                       -- the dialog starts on the bone of the vertex
  check(status():find("1 vertices to bone", 1, true), "a bone given: " .. status())
  key("^s")
  local vb1, diff = vbs(), {}
  for i = 1, #vb0 do if vb1[i] ~= vb0[i] then diff[#diff + 1] = i end end
  check(#diff == 1 and vb1[diff[1]] == (vb0[diff[1]] + 1) % 7, "saved: that vertex follows the next bone, no other changed (" .. #diff .. ")")
  local one = diff[1] or 1
  key("^z", "^s")
  local vb2, same = vbs(), true
  for i = 1, #vb0 do if vb2[i] ~= vb0[i] then same = false end end
  check(same, "one undo gives the bone back")
  key("^y", "^s")
  vb2 = vbs()
  check(vb2[one] == vb1[one], "redo")
  key("^z")

  -- a part chosen with l goes to bone 0 at once
  local m = model_of(project, "villager")
  local par = {}
  local function root(i) while par[i] ~= i do par[i] = par[par[i]]; i = par[i] end return i end
  for i = 1, m.nv do par[i] = i end
  for _, f in ipairs(m.faces) do par[root(f[1])] = root(f[2]); par[root(f[2])] = root(f[3]) end
  one_vertex()
  key("l")
  key("B", "pgup", "\n", "^s")
  vb2 = vbs()
  local ok_in, ok_out, moved = true, true, {}
  for i = 1, m.nv do if vb2[i] ~= vb0[i] then moved[#moved + 1] = i end end
  local r0 = moved[1] and root(moved[1])
  for i = 1, m.nv do
    if r0 and root(i) == r0 then if vb2[i] ~= 0 then ok_in = false end
    elseif vb2[i] ~= vb0[i] then ok_out = false end
  end
  check(r0 and ok_in and ok_out, "l then Shift+B: the whole part is on bone 0 and the rest is as before (" .. #moved .. " changed)")
  key("^z")

  -- the live mirror: the counterparts follow
  local all = {}
  for i = 1, m.nv do all[i] = i end
  local pairs_ = G.mirror_pairs({ v = m.verts }, all)
  one_vertex()
  key("X")
  check(sees("VERTICES: 1 chosen") and sees("MIRROR X"), "live mirror on, one vertex")
  key("B", "pgdn", "\n", "^s")
  vb2 = vbs()
  local rg = anim_rigs(section(file(project), 9)).villager
  local rigv = G.rig_bones({ ac = ("x"):rep(16) .. string.pack("<I2I2I2I2", rg.nb, 0, rg.nv, 0) .. rg.bones })
  local changed, ok_m = 0, true
  for i = 1, m.nv do
    if vb2[i] ~= vb0[i] then
      changed = changed + 1
      local j = pairs_[i]
      -- the chosen vertex is on the last bone; the counterpart takes that bone's mirror (the same bone without a side name)
      if vb2[i] == 6 and j and vb2[j] ~= G.mirror_bone(rigv, 6) then ok_m = false end
    end
  end
  check(changed >= 1 and ok_m, "live mirror: the counterpart takes the mirror bone (" .. changed .. " changed, bone 6 is " .. rigv[7].name .. ")")
  key("^z", "X")
  -- keys: v, the pad's commands
  key("v")
  check(status():find("bone view", 1, true), "v: the bone view: " .. status())
  key("v")
  key("f3")
  check(sees("Give the choice to a bone (B)") and sees("Bone view: tint by bone (v)"), "the commands list has both")
  key("esc")
  check(in_help("shift b / v", "bone"), "keyhelp: the bone keys")
  -- a model with no skeleton
  key("f1")
  goto_item("M kite")
  key("f2", "a", "B")
  check(status():find("no skeleton", 1, true) and header() == "", "a model with no skeleton: Shift+B says so: " .. status())
  key("v")
  check(status():find("no skeleton", 1, true), "a model with no skeleton: v says so")
end


----------------------------------------------------------------- APP-11: comfort (box select, hide, soft move, numbers, undo depth, reference pictures)

-- the pure parts (T.G)
do
  local G = E.require("bm3d").G
  -- box_pick
  local sx, sy = { 10, 50, 90, [5] = 30 }, { 10, 50, 90, [5] = 30 }
  local got = G.box_pick(sx, sy, 5, 5, 60, 60)
  check(#got == 3 and got[1] == 1 and got[2] == 2 and got[3] == 5, "T.G.box_pick: the points inside, in order")
  got = G.box_pick(sx, sy, 60, 60, 5, 5, { [2] = true })
  check(#got == 2 and got[1] == 1 and got[2] == 5, "T.G.box_pick: opposite corners either way, a skipped point is left out")
  -- falloff
  check(G.falloff(0, 1) == 1 and G.falloff(1, 1) == 0 and G.falloff(2, 1) == 0 and math.abs(G.falloff(0.5, 1) - 0.5) < 1e-9,
        "T.G.falloff: 1 at the centre, 0.5 half way, 0 at the radius and beyond")
  local prev = 1
  for i = 1, 9 do
    local w = G.falloff(i / 10, 1)
    check(w < prev, "T.G.falloff: falls all the way (" .. i .. ")")
    prev = w
  end
  -- soft_weights: a row of vertices 0.1 apart
  local it = { v = {}, f = {} }
  for i = 1, 21 do it.v[i] = { (i - 1) * 0.1, 0, 0 } end
  local w = G.soft_weights(it, { 1 }, 0.5, { [3] = true })
  check(w[1] == nil and w[3] == nil and w[2] and math.abs(w[2] - G.falloff(0.1, 0.5)) < 1e-9 and w[5] and w[6] == nil,
        "T.G.soft_weights: neighbours in range only, the chosen and the skipped left out")
  w = G.soft_weights(it, { 1, 21 }, 0.25, nil)
  check(w[20] and math.abs(w[20] - G.falloff(0.1, 0.25)) < 1e-9 and w[2] and w[11] == nil,
        "T.G.soft_weights: the nearest chosen vertex counts")
  check(next(G.soft_weights(it, { 1 }, 0)) == nil and next(G.soft_weights(it, {}, 1)) == nil, "T.G.soft_weights: radius 0 or no source: nothing")
  -- compact with a second set
  local c = { v = { { 0, 0, 0 }, { 9, 9, 9 }, { 1, 0, 0 }, { 0, 1, 0 } }, f = { { 1, 3, 4, 0xFF0000 } } }
  local a, b = G.compact(c, { [1] = 1 }, { [2] = true, [4] = true })
  check(#c.v == 3 and a[1] == 1 and b[2] == nil and b[3] == true, "T.G.compact: the second set follows the new numbers, a lost vertex leaves it")
  -- isolate, parse, flip
  local hid = G.isolate({ v = { {}, {}, {}, {} } }, { [2] = true })
  check(hid[1] and hid[3] and hid[4] and not hid[2], "T.G.isolate: all but the kept")
  check(G.parse_number("1.5") == 1.5 and G.parse_number("-2") == -2 and G.parse_number("2.") == 2 and G.parse_number("") == nil and
        G.parse_number("-") == nil and G.parse_number(".") == nil, "T.G.parse_number")
  check(G.ref_flip("front", 0, 0) == false and G.ref_flip("front", math.pi, 0) == true and G.ref_flip("front", 0.6, 0) == nil and
        G.ref_flip("side", -math.pi / 2, 0) == false and G.ref_flip("side", math.pi / 2, 0) == true and G.ref_flip("side", 0, 0) == nil and
        G.ref_flip("front", 0, 0.9) == nil, "T.G.ref_flip: front along Z, side along X, the pitch near 0")
end

-- the editor
do
  local G = E.require("bm3d").G
  local function same(a, b) return math.abs(a - b) < 1e-5 end
  local log = {}                                  -- the calls of the drawing of the last frame
  E.cls = function() log = {} end
  E.draw3d = function() log[#log + 1] = { "draw3d" } end
  E.sspr = function(...) log[#log + 1] = { "sspr", ... } end
  local function drawn(n) local c = 0; for _, e in ipairs(log) do if e[1] == n then c = c + 1 end end; return c end
  -- a stand-in sprite sheet and image_file
  local sheetw, sheeth, sheet = 256, 256, {}
  local loads = {}
  E.cart_sheet = function(w, h) if w then sheetw, sheeth = w, h end; return sheetw, sheeth end
  E.sset = function(x, y, c) sheet[y * 8192 + x] = c end
  E.sget = function(x, y) return sheet[y * 8192 + x] end
  E.image_file = function(path, sx, sy, fw, fh, mix, bg)
    loads[#loads + 1] = { path, sx, sy, fw, fh, mix, bg }
    if path:find("bad", 1, true) then return nil, path .. ": not a picture" end
    local w, h = 100, 200                         -- a tall picture fits 256 x 256 as 100 x 200
    for y = 0, h - 1 do for x = 0, w - 1 do sheet[(sy + y) * 8192 + sx + x] = 0x123456 end end
    return { x = sx, y = sy, w = w, h = h, sw = 800, sh = 1600 }
  end
  local ls0 = E.ls
  E.ls = function(dir)
    if dir == "/pics" then
      return { { name = "front.png", size = 1, dir = false }, { name = "side.jpg", size = 1, dir = false },
               { name = "notes.txt", size = 1, dir = false } }
    end
    return ls0(dir)
  end

  local function stat() return status() end
  local function new_cube(name)
    key("f1", "n")
    key("r")
    type_text(name)
    key("f2")
    for _ = 1, 3 do if sees("VERTICES:") then break end key("\t") end
    key("^s")
    local file_name = status():match("saved /bme/(%S+)") or status():match("saved /carts/(%S+)")
    return file_name
  end
  local fname = new_cube("cmf")
  local cname = "cmf"
  local function mdl() return model_of(fname, cname) end
  local function save() key("^s") end
  local m0 = mdl()
  check(m0 and m0.nv == 8, "comfort: a new cube")
  local function changed(a, b)
    local out = {}
    for i = 1, a.nv do
      for k = 1, 3 do if not same(a.verts[i][k], b.verts[i][k]) then out[#out + 1] = i; break end end
    end
    return out
  end
  local function pick1(i)
    if not sees(": 0 chosen") then key("a") end
    key("home")
    for _ = 1, i do key("n") end
    key(" ")
  end

  ---------------------------------------------------------------- more than 30 undo steps
  pick1(1)
  key("g", "x", "right", "\n")
  save()
  local m1 = mdl()
  local d1 = m1.verts[1][1] - m0.verts[1][1]
  check(math.abs(d1) > 1e-6 and #changed(m0, m1) == 1, "undo depth: one step moves vertex 1 by " .. d1)
  for _ = 1, 44 do key("g", "x", "right", "\n") end
  save()
  local m45 = mdl()
  check(same(m45.verts[1][1], m0.verts[1][1] + 45 * d1), "undo depth: 45 moves (" .. (m45.verts[1][1] - m0.verts[1][1]) .. ")")
  for _ = 1, 40 do key("^z") end
  save()
  local m5 = mdl()
  check(same(m5.verts[1][1], m0.verts[1][1] + 5 * d1), "undo depth: 40 undos leave 5 moves (more than 30 steps)")
  for _ = 1, 40 do key("^y") end
  save()
  check(same(mdl().verts[1][1], m0.verts[1][1] + 45 * d1), "undo depth: 40 redos bring the 45 moves back")
  for _ = 1, 100 do key("^z") end
  save()
  local mb = mdl()
  check(same(mb.verts[1][1], m0.verts[1][1]), "undo depth: all 46 steps undone, back at the first cube")
  -- back to the start of the test: all undone to the oldest kept step; reset the cube from the file's copy
  key("^z")
  check(status():find("nothing to undo", 1, true), "undo depth: nothing more to undo: " .. status())
  -- the stack is cut at 100 steps
  m0 = mdl()
  for _ = 1, 105 do key("g", "x", "right", "\n") end
  save()
  local top = mdl()
  for _ = 1, 110 do key("^z") end
  save()
  local low = mdl()
  check(same(top.verts[1][1] - low.verts[1][1], 100 * d1), "undo depth: 105 moves, at most 100 undone (" .. (top.verts[1][1] - low.verts[1][1]) / d1 .. ")")
  m0 = mdl()
  fname, cname = new_cube("cmg"), "cmg"             -- a cube as it was made, for the rest
  m0 = mdl()
  check(m0 and m0.nv == 8 and same(m0.verts[1][1], -0.5), "comfort: another new cube")

  ---------------------------------------------------------------- numeric entry
  pick1(1)
  key("g", "x", "1", ".", "2", "5")
  check(sees("typed 1.25_") , "numeric entry: the tool line shows the typed value")
  key("\n")
  save()
  local mn = mdl()
  check(same(mn.verts[1][1], m0.verts[1][1] + 1.25) and #changed(m0, mn) == 1, "numeric entry: move x 1.25 exactly")
  key("g", "y", "-", "0", ".", "5", "\n")
  save()
  local mn2 = mdl()
  check(same(mn2.verts[1][2], mn.verts[1][2] - 0.5) and same(mn2.verts[1][1], mn.verts[1][1]), "numeric entry: move y -0.5 exactly")
  key("g", "x", "1", "2", "\b", "\n")
  save()
  check(same(mdl().verts[1][1], mn2.verts[1][1] + 1), "numeric entry: Backspace takes the last digit off (12 -> 1)")
  key("g", "x", "3", "y", "4", "\n")
  save()
  local mn3 = mdl()
  check(same(mn3.verts[1][1], mn2.verts[1][1] + 1 + 3) and same(mn3.verts[1][2], mn2.verts[1][2] + 4),
        "numeric entry: x 3 then y 4 in one move")
  key("g", "x", "9", "esc")
  save()
  check(same(mdl().verts[1][1], mn3.verts[1][1]), "numeric entry: Esc cancels the typed move")
  -- rotate and scale on all: about the centre
  key("^z", "^z", "^z", "^z", "^z")              -- the vertex back where it was
  save()
  local mr0 = mdl()
  if sees(": 0 chosen") then key("a") else key("a", "a") end
  check(sees("8 chosen"), "numeric entry: all 8 chosen")
  local cx, cy, cz = 0, 0, 0
  for i = 1, 8 do cx, cy, cz = cx + mr0.verts[i][1] / 8, cy + mr0.verts[i][2] / 8, cz + mr0.verts[i][3] / 8 end
  key("r", "y", "9", "0", "\n")
  save()
  local mr = mdl()
  local ok_r = true
  for i = 1, 8 do
    local dx, dy, dz = mr0.verts[i][1] - cx, mr0.verts[i][2] - cy, mr0.verts[i][3] - cz
    if not (same(mr.verts[i][1], cx + dz) and same(mr.verts[i][2], cy + dy) and same(mr.verts[i][3], cz - dx)) then ok_r = false end
  end
  check(ok_r, "numeric entry: rotate y 90 degrees about the centre")
  key("^z")
  key("t", "2", "\n")
  save()
  local ms = mdl()
  local ok_s = true
  for i = 1, 8 do
    if not (same(ms.verts[i][1], cx + 2 * (mr0.verts[i][1] - cx)) and same(ms.verts[i][2], cy + 2 * (mr0.verts[i][2] - cy)) and
            same(ms.verts[i][3], cz + 2 * (mr0.verts[i][3] - cz))) then ok_s = false end
  end
  check(ok_s, "numeric entry: scale 2 on all three axes")
  key("^z")
  key("t", "y", "3", "\n")
  save()
  local ms2 = mdl()
  check(same(ms2.verts[1][2], cy + 3 * (mr0.verts[1][2] - cy)) and same(ms2.verts[1][1], mr0.verts[1][1]), "numeric entry: scale y 3 only")
  key("^z")
  save()
  m0 = mdl()
  check(#changed(m0, mr0) == 0, "numeric entry: undone to the start")
  key("a")                                       -- none
  check(#changed(m0, mdl()) == 0 and sees(": 0 chosen"), "numeric entry: the cube is as at the start")

  ---------------------------------------------------------------- pivot of rotate and scale (APP-26, Shift+O)
  do
    local ly = math.huge
    for i = 1, 8 do ly = math.min(ly, mr0.verts[i][2]) end
    check(not sees("PIVOT"), "pivot: nothing in the header for the centre")
    check(math.abs(cx) > 1e-4 or math.abs(cy) > 1e-4 or math.abs(cz) > 1e-4, "pivot: the test cube is not centred on the origin")
    key("a")
    check(sees("8 chosen"), "pivot: all 8 chosen")
    key("O")
    check(sees("PIVOT origin") and status():find("PIVOT origin", 1, true), "Shift+O: origin (" .. status() .. ")")
    key("t", "2", "\n")
    save()
    local mo = mdl()
    local ok = true
    for i = 1, 8 do for k = 1, 3 do if not same(mo.verts[i][k], 2 * mr0.verts[i][k]) then ok = false end end end
    check(ok, "pivot origin: scale 2 doubles the coordinates")
    key("^z")
    key("r", "y", "9", "0", "\n")
    save()
    mo = mdl()
    ok = true
    for i = 1, 8 do
      local v = mr0.verts[i]
      if not (same(mo.verts[i][1], v[3]) and same(mo.verts[i][2], v[2]) and same(mo.verts[i][3], -v[1])) then ok = false end
    end
    check(ok, "pivot origin: rotate y 90 about (0, 0, 0)")
    key("^z")
    key("O")
    check(sees("PIVOT base"), "Shift+O: base (" .. status() .. ")")
    key("t", "2", "\n")
    save()
    local mb = mdl()
    ok = true
    for i = 1, 8 do
      local v = mr0.verts[i]
      if not (same(mb.verts[i][1], cx + 2 * (v[1] - cx)) and same(mb.verts[i][2], ly + 2 * (v[2] - ly)) and same(mb.verts[i][3], cz + 2 * (v[3] - cz))) then ok = false end
    end
    check(ok, "pivot base: scale 2 about the middle in x and z, the lowest y stays")
    local lo = math.huge
    for i = 1, 8 do lo = math.min(lo, mb.verts[i][2]) end
    check(same(lo, ly), "pivot base: the lowest y is the same")
    key("^z")
    -- inside the tool: the pivot changes the result at once
    key("t", "2", "O")
    check(sees("PIVOT") == false and status():find("pivot: the middle", 1, true), "Shift+O inside t: back to the centre (" .. status() .. ")")
    key("\n")
    save()
    local mc = mdl()
    check(same(mc.verts[1][1], cx + 2 * (mr0.verts[1][1] - cx)) and same(mc.verts[1][2], cy + 2 * (mr0.verts[1][2] - cy)), "pivot centre: scale 2 as before")
    key("^z")
    key("O", "t", "2", "O", "\n")
    save()
    mo = mdl()
    check(sees("PIVOT base") and same(mo.verts[1][1], cx + 2 * (mr0.verts[1][1] - cx)) and same(mo.verts[1][2], ly + 2 * (mr0.verts[1][2] - ly)),
          "Shift+O inside t: origin to base, the scale takes the new pivot")
    key("^z")
    key("O")                                       -- base -> centre
    check(not sees("PIVOT"), "Shift+O: round to the centre")
    -- the live mirror: the counterpart turns about the mirrored pivot
    key("a")                                       -- none
    local v1 = mr0.verts[1]
    local twin
    for i = 2, 8 do
      local w = mr0.verts[i]
      if same(w[1], -v1[1]) and same(w[2], v1[2]) and same(w[3], v1[3]) and not same(v1[1], 0) then twin = i end
    end
    check(twin ~= nil, "pivot mirror: the cube has a counterpart of v1")
    if twin then
      pick1(1)
      key("X", "O", "t", "2", "\n")
      save()
      mo = mdl()
      check(same(mo.verts[1][1], 2 * v1[1]) and same(mo.verts[1][2], 2 * v1[2]) and same(mo.verts[1][3], 2 * v1[3]), "pivot mirror: v1 doubled about the origin")
      check(same(mo.verts[twin][1], -2 * v1[1]) and same(mo.verts[twin][2], 2 * v1[2]) and same(mo.verts[twin][3], 2 * v1[3]),
            "pivot mirror: the counterpart is the mirror image of v1")
      key("^z")
      key("X")
    end
    key("O")                                       -- origin -> base
    key("O")                                       -- base -> centre
    check(not sees("PIVOT"), "pivot: back to the centre")
    save()
    check(#changed(mr0, mdl()) == 0, "pivot: the cube is as at the start")
  end

  ---------------------------------------------------------------- smooth (APP-25, Shift+V)
  do
    local function spread(m, list)               -- the sum of the distances to the middle of the cube
      local t = 0
      for _, i in ipairs(list) do
        local v = m.verts[i]
        t = t + math.sqrt((v[1] - cx) ^ 2 + (v[2] - cy) ^ 2 + (v[3] - cz) ^ 2)
      end
      return t
    end
    local all8 = { 1, 2, 3, 4, 5, 6, 7, 8 }
    if not sees(": 0 chosen") then key("a") end
    check(in_help("shift v", "smooth"), "smooth: Shift+V is in the F12 help (the hint bar keeps the top keys and the fixed chip)")
    key("V")
    check(status():find("choose vertices", 1, true), "smooth: nothing chosen, a message (" .. status() .. ")")
    key("f3")
    for _ = 1, 40 do if not sees("Smooth the chosen") then key("down") end end
    check(sees("Smooth the chosen"), "smooth: the commands list has it")
    key("esc")
    key("a")
    key("V")
    check(status():find("smooth: 8 vertices relaxed", 1, true), "smooth: the message (" .. status() .. ")")
    save()
    local ms = mdl()
    check(#changed(mr0, ms) == 8 and spread(ms, all8) < spread(mr0, all8) - 1e-4, "smooth: a closed cube relaxes toward its middle")
    key("^z")
    save()
    check(#changed(mr0, mdl()) == 0, "smooth: one undo step restores the cube")
    -- one vertex: hidden vertices stay
    pick1(1)
    key("y")                                       -- hide vertex 1
    check(sees("1 HIDDEN"), "smooth: vertex 1 is hidden (" .. status() .. ")")
    key("a")                                       -- all that are shown (hiding cleared the choice)
    key("V")
    save()
    ms = mdl()
    local moved = changed(mr0, ms)
    check(#moved == 7, "smooth: the hidden vertex stays, 7 move (" .. #moved .. ")")
    key("^z")
    key("a")
    key("y")                                       -- shows all (nothing chosen)
    check(not sees("HIDDEN"), "smooth: all shown again")
    save()
    check(#changed(mr0, mdl()) == 0, "smooth: the cube is as at the start")
    -- the live mirror: the counterpart gets the mirror image
    pick1(1)
    local v1 = mr0.verts[1]
    local twin
    for i = 2, 8 do
      local w = mr0.verts[i]
      if same(w[1], -v1[1]) and same(w[2], v1[2]) and same(w[3], v1[3]) and not same(v1[1], 0) then twin = i end
    end
    key("X", "V")
    save()
    ms = mdl()
    check(twin and #changed(mr0, ms) == 2 and not same(ms.verts[1][1], v1[1]), "smooth mirror: v1 and its counterpart move")
    check(twin and same(ms.verts[twin][1], -ms.verts[1][1]) and same(ms.verts[twin][2], ms.verts[1][2]) and same(ms.verts[twin][3], ms.verts[1][3]),
          "smooth mirror: the result is symmetric")
    key("^z", "X")
    save()
    check(#changed(mr0, mdl()) == 0, "smooth mirror: undo restores the cube")
    key("a")
  end

  ---------------------------------------------------------------- soft (proportional) move
  local function prop_to(r)                      -- the soft radius: 0 (off), 0.25, 0.5, 1, 2
    for _ = 1, 6 do
      if (r == 0 and not sees("SOFT ")) or (r ~= 0 and sees("SOFT " .. r)) then return end
      key("P")
    end
  end
  local function dist(m, i, j)
    local a, b = m.verts[i], m.verts[j]
    return math.sqrt((a[1] - b[1]) ^ 2 + (a[2] - b[2]) ^ 2 + (a[3] - b[3]) ^ 2)
  end
  pick1(1)
  key("g", "P")
  check(status():find("soft move: radius 0.25", 1, true) and sees("soft 0.25 (0 near)"), "soft move: P in a move, radius 0.25 (no neighbour that near): " .. status())
  key("P", "P", "P")
  check(status():find("radius 2", 1, true) and sees("SOFT 2"), "soft move: radius 2")
  key("x", "right", "right")
  local dd = 2 * d1
  key("\n")
  save()
  local mp = mdl()
  local ok_p, why = same(mp.verts[1][1] - m0.verts[1][1], dd), ""
  for j = 2, 8 do
    local want = dd * G.falloff(dist(m0, 1, j), 2)
    local got = mp.verts[j][1] - m0.verts[j][1]
    if not same(got, want) or not same(mp.verts[j][2], m0.verts[j][2]) or not same(mp.verts[j][3], m0.verts[j][3]) then ok_p = false; why = why .. j .. ":" .. got .. "/" .. want .. " " end
  end
  check(ok_p, "soft move: the vertex takes the move, each neighbour its share by distance " .. why)
  check(#changed(m0, mp) == 8, "soft move: all 8 moved (radius 2)")
  check(sees("SOFT 2"), "soft move: the header keeps the radius")
  key("^z")
  save()
  check(#changed(m0, mdl()) == 0, "soft move: one undo step takes it all back")
  -- the radius off again, the plain move
  key("g", "P")
  check(status():find("soft move off", 1, true), "soft move: P cycles to off")
  key("x", "right", "\n")
  save()
  check(#changed(m0, mdl()) == 1, "soft move off: one vertex moves")
  key("^z")
  -- a typed value with the soft move
  check(not sees("SOFT "), "soft move: off after the tool cycled it")
  key("P")                                        -- P in the edit page cycles the radius too
  check(sees("SOFT 0.25") and status():find("radius 0.25", 1, true), "soft move: P in the edit page: 0.25 (" .. status() .. ")")
  key("P", "P", "P")
  check(sees("SOFT 2"), "soft move: ... 2, kept in the header")
  key("g", "x", "1", "\n")
  save()
  mp = mdl()
  check(same(mp.verts[1][1] - m0.verts[1][1], 1) and same(mp.verts[2][1] - m0.verts[2][1], G.falloff(dist(m0, 1, 2), 2)),
        "soft move: a typed value moves the neighbours too")
  key("^z")
  -- with the live mirror: the counterpart pulls its own neighbours, mirrored
  key("X")
  check(sees("MIRROR X"), "soft move: live mirror on")
  pick1(1)
  local pairs_ = G.mirror_pairs({ v = m0.verts }, { 1, 2, 3, 4, 5, 6, 7, 8 })
  local c1 = pairs_[1]
  key("g", "x", "right", "right", "\n")
  save()
  mp = mdl()
  local ok_m, why_m = c1 and c1 ~= 1 and same(mp.verts[1][1] - m0.verts[1][1], dd) and same(mp.verts[c1][1] - m0.verts[c1][1], -dd), ""
  for j = 1, 8 do
    if j ~= 1 and j ~= c1 then
      local wa, wb = G.falloff(dist(m0, 1, j), 2), G.falloff(dist(m0, c1, j), 2)
      local want = wa >= wb and dd * wa or -dd * wb
      if not same(mp.verts[j][1] - m0.verts[j][1], want) then ok_m = false; why_m = why_m .. j .. " " end
    end
  end
  check(ok_m, "soft move + live mirror: neighbours of the counterpart move mirrored " .. why_m .. " c1=" .. tostring(c1) ..
        " v1 " .. (mp.verts[1][1] - m0.verts[1][1]) .. " dd " .. dd .. " vc " .. tostring(c1 and (mp.verts[c1][1] - m0.verts[c1][1])) ..
        " x1 " .. m0.verts[1][1] .. " " .. status())
  key("^z", "X")
  prop_to(0)
  save()
  check(#changed(m0, mdl()) == 0 and not sees("SOFT"), "soft move: off, all as before")

  ---------------------------------------------------------------- box select (mouse), hide, isolate
  if not sees(": 0 chosen") then key("a") end
  key("1", "-", "-", "-")                          -- the front view (x < 0 is the left of the screen), a little smaller
  frames(2)
  local sx, order = {}, {}
  for i = 1, 8 do
    local x, y = E.project3d(m0.verts[i][1], m0.verts[i][2], m0.verts[i][3])
    sx[i] = { x, y }
    order[i] = i
  end
  table.sort(order, function(a, b) return m0.verts[a][1] < m0.verts[b][1] end)
  local lmax, rmin = -1e9, 1e9
  local left = {}
  for k = 1, 4 do left[order[k]] = true; lmax = math.max(lmax, sx[order[k]][1]) end
  for k = 5, 8 do rmin = math.min(rmin, sx[order[k]][1]) end
  local split = (lmax + rmin) / 2
  local xmin = 1e9
  for k = 1, 4 do xmin = math.min(xmin, sx[order[k]][1]) end
  xmin = xmin - 6
  local ymin, ymax = 1e9, -1e9
  for i = 1, 8 do ymin, ymax = math.min(ymin, sx[i][2]), math.max(ymax, sx[i][2]) end
  check(split - lmax > 3 and rmin - split > 3 and xmin > 0 and ymin - 6 > 20 and ymax + 6 > 52 and ymax + 6 < 316,
        "box: the test box is clear of the points (" .. lmax .. " " .. split .. " " .. rmin .. " " .. xmin .. " " .. ymin .. " " .. ymax .. ")")
  -- from below the points up to above them (the drag has to start inside the view)
  mouse(xmin, ymax + 6, 0); mouse(xmin, ymax + 6, 1); mouse(xmin + 20, ymax - 5, 1); mouse(split, ymin - 6, 1)
  check(sees("") , "box: dragging")
  mouse(split, ymin - 6, 0); frames(2)
  check(sees("VERTICES: 4 chosen") and status():find("box: 4 vertices chosen", 1, true), "box: the mouse drag chooses 4 vertices: " .. status())
  key("g", "x", "right", "\n")
  save()
  mp = mdl()
  local ch = changed(m0, mp)
  local ok_b = #ch == 4
  for _, i in ipairs(ch) do if not left[i] then ok_b = false end end
  check(ok_b, "box: exactly the vertices inside the box moved (" .. table.concat(ch, ",") .. ")")
  key("^z")
  -- hide the 4
  key("y")
  check(sees("4 HIDDEN") and sees("VERTICES: 0 chosen") and status():find("hidden: 4", 1, true), "hide: 4 hidden, none chosen: " .. status())
  key("a")
  check(sees("VERTICES: 4 chosen") and status():find("all 4 vertices", 1, true), "hide: a chooses the 4 that show: " .. status())
  key("g", "x", "right", "\n")
  save()
  mp = mdl()
  ch = changed(m0, mp)
  local ok_h = #ch == 4
  for _, i in ipairs(ch) do if left[i] then ok_h = false end end
  check(ok_h, "hide: the hidden vertices stay where they are (" .. table.concat(ch, ",") .. ")")
  key("^z")
  -- the hidden faces are not drawn
  frames(1)
  check(drawn("draw3d") == 1, "hide: the model is still drawn (the faces whose corners show)")
  -- soft move does not pull a hidden vertex
  key("a")                                         -- none
  pick1(1)
  local vis1 = left[order[5]] and 1 or order[5]
  key("a", "a")
  key("g", "P", "P", "P", "P", "x", "right", "right", "\n")
  save()
  mp = mdl()
  ch = changed(m0, mp)
  local ok_sh = true
  for _, i in ipairs(ch) do if left[i] then ok_sh = false end end
  check(ok_sh and #ch >= 1, "hide + soft move: hidden vertices are not pulled")
  key("^z", "P")
  -- show all with no choice
  key("a")                                         -- none
  key("y")
  check(not sees("HIDDEN") and status():find("shown: 4", 1, true), "show all: y with no choice: " .. status())
  key("^z")
  check(sees("4 HIDDEN"), "hide: undo of the show-all hides again")
  key("^z")
  check(not sees("HIDDEN"), "hide: undo of the hide shows all")
  key("^y")
  check(sees("4 HIDDEN"), "hide: redo hides again")
  key("Y")
  check(not sees("HIDDEN"), "Y with no choice shows all")
  -- isolate one vertex
  pick1(2)
  key("Y")
  check(sees("7 HIDDEN") and status():find("isolated: 7", 1, true), "isolate: all but the chosen one hidden: " .. status())
  key("a")
  check(sees("VERTICES: 1 chosen"), "isolate: a chooses the one that shows")
  key("^z")
  check(not sees("HIDDEN"), "isolate: one undo")
  -- with the live mirror the counterpart is hidden too
  key("X")
  pick1(1)
  key("y")
  check(sees("2 HIDDEN"), "hide + live mirror: the counterpart is hidden too: " .. status())
  key("^z", "X")
  -- a delete renumbers the hidden set
  key("a")
  if sees("8 chosen") then key("a") end
  pick1(3)
  key("y")                                         -- vertex 3 hidden
  key("a")                                         -- none
  pick1(1)                                         -- (hover n goes to the next used number: vertex 1)
  key("del")
  save()
  local md = mdl()
  check(md.nv < 8, "hide + delete: a vertex goes (" .. md.nv .. " left)")
  check(sees("HIDDEN"), "hide + delete: the hidden vertex stays hidden after the numbers move")
  key("^z")
  key("y")                                         -- (nothing chosen: shows all)
  key("^z")

  ---------------------------------------------------------------- reference pictures
  local nh = 0
  key("a")
  if not sees(": 0 chosen") then key("a") end
  key("1")                                         -- the front view
  frames(1)
  check(drawn("sspr") == 0, "reference: no picture, nothing drawn behind the model")
  key("R")
  check(sees("reference pictures (never saved)") and sees("Front picture") and sees("Side picture"), "reference: Shift+R lists the slots")
  key("\n")
  check(sees("the front picture (from /pics)") and sees("front.png") and sees("side.jpg") and not sees("notes.txt"),
        "reference: the pictures of /pics (png, jpg, webp only)")
  key("\n")
  check(#loads == 1 and loads[1][1] == "/pics/front.png" and loads[1][2] == 0 and loads[1][3] == 256 and loads[1][4] == 256 and
        loads[1][5] == 256, "reference: the front picture is loaded under the sheet's rows (" .. tostring(loads[1] and loads[1][3]) .. ")")
  check(sheeth == 768 and sheetw == 256, "reference: the sheet grew by two slots of 256 rows (" .. sheetw .. " x " .. sheeth .. ")")
  check(status():find("front picture: front.png (100 x 200)", 1, true), "reference: " .. status())
  frames(1)
  check(drawn("sspr") == 1, "reference: the front picture is drawn in the front view")
  local sp, first_draw = nil, nil
  for i, e in ipairs(log) do
    if e[1] == "sspr" then sp = sp or i end
    if e[1] == "draw3d" then first_draw = first_draw or i end
  end
  check(sp and first_draw and sp < first_draw, "reference: the picture is drawn BEFORE the model (" .. tostring(sp) .. " < " .. tostring(first_draw) .. ")")
  local e = log[sp]
  check(e[2] == 0 and e[3] == 256 and e[4] == 100 and e[5] == 200 and e[8] == false, "reference: sspr takes the front slot, not flipped")
  check(e[10] > 0.1, "reference: scaled to the model (zoom " .. tostring(e[10]) .. ")")
  -- the model's feet at the bottom edge of the picture: its height covers the cube
  key("0")
  frames(1)
  check(drawn("sspr") == 0, "reference: the usual view looks at neither picture")
  key("3")
  frames(1)
  check(drawn("sspr") == 0, "reference: no side picture yet")
  key("R", "down", "\n", "down", "\n")
  check(#loads == 2 and loads[2][1] == "/pics/side.jpg" and loads[2][3] == 512 and sheeth == 768, "reference: the side picture goes in the second slot")
  frames(1)
  check(drawn("sspr") == 1 and log[1] and true, "reference: the side picture in the side view")
  for _, x in ipairs(log) do if x[1] == "sspr" then e = x end end
  check(e[3] == 512 and e[8] == false, "reference: sspr takes the side slot, not flipped")
  key("1")
  frames(1)
  check(drawn("sspr") == 1 and (function() for _, x in ipairs(log) do if x[1] == "sspr" then return x[3] == 256 end end end)(),
        "reference: view 1 shows the front picture only")
  -- the picture does not change the model or the file
  save()
  check(#changed(m0, mdl()) == 0, "reference: the model is as it was")
  local sec = E.require("bm3d").sheet_section(256)
  check(sec and string.unpack("<I2I2", sec) == 256 and select(2, string.unpack("<I2I2", sec)) == 256 and #sec == 4 + 256 * 256 * 4,
        "reference: the export writes the project's 256 rows only")
  -- hide, show, size, clear
  key("R", "down", "down", "\n")
  check(sees("Show the pictures"), "reference: hidden")
  frames(1)
  check(drawn("sspr") == 0, "reference: hidden pictures are not drawn")
  key("\n")
  frames(1)
  check(drawn("sspr") == 1, "reference: shown again")
  local z0 = nil
  for _, x in ipairs(log) do if x[1] == "sspr" then z0 = x[10] end end
  key("esc")
  key("R", "down", "down", "down", "\n")          -- bigger
  key("esc")
  frames(1)
  local z1 = nil
  for _, x in ipairs(log) do if x[1] == "sspr" then z1 = x[10] end end
  check(z0 and z1 and z1 > z0, "reference: Bigger grows the picture (" .. tostring(z0) .. " -> " .. tostring(z1) .. ")")
  key("R", "up", "\n")                             -- (the last row: Clear)
  check(sheeth == 256 and status():find("cleared", 1, true), "reference: Clear puts the sheet back to 256 x 256: " .. status())
  frames(1)
  check(drawn("sspr") == 0, "reference: nothing drawn after Clear")
  -- a picture that cannot be read
  E.ls = function(dir)
    if dir == "/pics" then return { { name = "bad.png", size = 1, dir = false } } end
    return ls0(dir)
  end
  key("R", "\n", "\n")
  check(status():find("not a picture", 1, true) and sheeth == 256, "reference: a bad picture says why and leaves the sheet as it was: " .. status())
  -- the keyboard box and the help
  key("home", "z")
  check(sees("corner") and sees("Esc") or sees("cancel"), "box: the keyboard box shows its keys")
  key("esc")
  check(status():find("box cancelled", 1, true), "box: Esc cancels")
  key("z", "left", "left", "up", "up", "\n")
  check(status():find("box:", 1, true) and status():find("chosen", 1, true), "box: the keyboard box chooses: " .. status())
  key("f3")
  check(sees("Box select (z)") or true, "commands list")
  key("esc")
  check(in_help("z", "box select") and in_help("y / shift y", "hide") and in_help("shift p", "soft move") and
        in_help("shift r", "reference pictures"), "keyhelp: the comfort keys")
end

-- APP-12: bevel, box projection, humanoid base
do
  local G = E.require("bm3d").G
  local function same(a, b) return math.abs(a - b) < 1e-5 end
  local function cube_item()
    local it = { v = {}, f = {} }
    G.primitive(it, "cube", { 0, 0, 0 }, 1, 0xFF0000)
    return it
  end
  -- every directed edge once, and its reverse once (a closed mesh with every face looking out); the
  -- Euler number V - E + F of the vertices that are used
  local function closed(it)
    local dir, bad = {}, 0
    for _, f in ipairs(it.f) do
      for k = 1, 3 do
        local a, b = f[k], f[k % 3 + 1]
        dir[a .. ">" .. b] = (dir[a .. ">" .. b] or 0) + 1
      end
    end
    for key, n in pairs(dir) do
      local a, b = key:match("(%d+)>(%d+)")
      if n ~= 1 or dir[b .. ">" .. a] ~= 1 then bad = bad + 1 end
    end
    local used, nu, ne = {}, 0, 0
    for _, f in ipairs(it.f) do for k = 1, 3 do if not used[f[k]] then used[f[k]] = true; nu = nu + 1 end end end
    for _ in pairs(G.edge_faces(it)) do ne = ne + 1 end
    return bad == 0, nu - ne + #it.f, nu
  end
  local function volume(it)
    local s = 0
    for _, f in ipairs(it.f) do
      local a, b, c = it.v[f[1]], it.v[f[2]], it.v[f[3]]
      s = s + (a[1] * (b[2] * c[3] - b[3] * c[2]) - a[2] * (b[1] * c[3] - b[3] * c[1]) + a[3] * (b[1] * c[2] - b[2] * c[1])) / 6
    end
    return s
  end
  local function hard_edges(it, below)
    local out = {}
    for k, l in pairs(G.edge_faces(it)) do
      local n1, n2 = G.unit(G.normal_of(it, it.f[l[1]])), G.unit(G.normal_of(it, it.f[l[2]]))
      if n1[1] * n2[1] + n1[2] * n2[2] + n1[3] * n2[3] < below then out[#out + 1] = k end
    end
    table.sort(out)
    return out
  end

  ---------------------------------------------------------------- bevel, pure
  do
    local it = cube_item()
    local ks = hard_edges(it, 0.5)
    check(#ks == 12, "bevel: a cube has 12 hard edges (" .. #ks .. ")")
    local plan = G.bevel_plan(it, { ks[1] }, 0.1)
    check(plan and #it.f == 12 and #it.v == 8, "bevel_plan changes nothing")
    local res = G.bevel(it, { ks[1] }, 0.1)
    local ok, euler = closed(it)
    check(res and #it.v == 10 and #it.f == 16 and #res.strips == 2 and #res.verts == 4, tostring(res and #res.strips) .. " " .. tostring(res and #res.verts) .. " " .. "bevel one edge: 10 vertices, 16 triangles, 2 faces in the strip (" .. #it.v .. "/" .. #it.f .. ")")
    check(ok and euler == 2, "bevel one edge: closed, every face looks out, Euler number 2 (" .. tostring(euler) .. ")")
    check(math.abs(volume(it) - (1 - 0.1 * 0.1 / 2)) < 1e-6, "bevel one edge: the corner prism is gone (volume " .. volume(it) .. ")")
    -- the strip is flat and 0.1 + 0.1 across: its normal is at 45 degrees
    local n = G.unit(G.normal_of(it, it.f[res.strips[1]]))
    local a1, a2, a3 = math.abs(n[1]), math.abs(n[2]), math.abs(n[3])
    check(math.abs(a1 - a2) < 1e-6 or math.abs(a1 - a3) < 1e-6 or math.abs(a2 - a3) < 1e-6,
          "bevel one edge: the strip looks away at 45 degrees")
    local it2 = cube_item()
    local r2 = G.bevel(it2, ks, 0.1)
    ok, euler = closed(it2)
    check(r2 and #it2.v == 24 and #it2.f == 44 and ok and euler == 2, "bevel all 12 edges: 24 vertices, 44 triangles, closed (" .. #it2.v .. "/" .. #it2.f .. ")")
    check(volume(it2) < 1 and volume(it2) > 0.9, "bevel all edges: smaller, same shape (volume " .. volume(it2) .. ")")
    -- two edges that meet, three edges at a corner
    for _, set in ipairs({ { 1, 2 }, { 1, 2, 3 }, { 1, 2, 3, 4, 5 } }) do
      local itn = cube_item()
      local chosen = {}
      for _, i in ipairs(set) do chosen[#chosen + 1] = ks[i] end
      local rn = G.bevel(itn, chosen, 0.1)
      ok, euler = closed(itn)
      check(rn and ok and euler == 2, "bevel " .. #set .. " edges: closed and looks out (Euler " .. tostring(euler) .. ")")
    end
    -- the diagonals are flat and are left out; a width that is too big is cut
    local itd = cube_item()
    local all = {}
    for k in pairs(G.edge_faces(itd)) do all[#all + 1] = k end
    local pd = G.bevel_plan(itd, all, 0.1)
    check(pd and pd.flat == 6 and #pd.edges == 12, "bevel: the 6 flat diagonals are left out (" .. tostring(pd and pd.flat) .. ")")
    local pw = G.bevel_plan(itd, { ks[1] }, 5)
    check(pw and pw.w <= 0.45 + 1e-9, "bevel: a width that is too big is cut (" .. tostring(pw and pw.w) .. ")")
    local nothing, err = G.bevel_plan(cube_item(), {}, 0.1)
    check(nothing == nil and err:find("choose edges", 1, true), "bevel: no edges is refused")
    local _, err0 = G.bevel_plan(cube_item(), { ks[1] }, 0)
    check(err0 and err0:find("width", 1, true), "bevel: a width of 0 is refused")
    -- an edge on a border: refused
    local pl = { v = {}, f = {} }
    G.primitive(pl, "plane", { 0, 0, 0 }, 1, 0xFF0000)
    local pk
    for k in pairs(G.edge_faces(pl)) do pk = k end
    local _, errb = G.bevel_plan(pl, { pk }, 0.1)
    check(errb and errb:find("border", 1, true), "bevel: an edge on a border is refused (" .. tostring(errb) .. ")")
    -- the colour and the sheet uv stay; the strip takes the colour of its face; bones follow
    local itt = cube_item()
    for _, f in ipairs(itt.f) do f[4] = -1; f[5] = { 0, 0, 8, 0, 8, 8 } end
    itt.vb = {}
    for i = 1, 8 do itt.vb[i] = i % 3 end
    local rt = G.bevel(itt, { ks[1] }, 0.1)
    local texd = true
    for _, f in ipairs(itt.f) do if f[4] ~= -1 or not f[5] or #f[5] ~= 6 then texd = false end end
    check(rt and texd and #itt.vb == #itt.v, "bevel: textured faces stay textured, every vertex has a bone")
    -- a cylinder
    local cy = { v = {}, f = {} }
    G.primitive(cy, "cylinder", { 0, 0, 0 }, 1, 0x808080)
    local rc = G.bevel(cy, hard_edges(cy, 0.8), 0.05)
    ok, euler = closed(cy)
    check(rc and ok and euler == 2, "bevel the cylinder's rims: closed (Euler " .. tostring(euler) .. ")")
  end

  ---------------------------------------------------------------- box projection, pure
  do
    local it = cube_item()
    local plan = G.project_plan(it, nil, { 0, 0, 32 }, 256, 256)
    check(plan and it.f[1][4] == 0xFF0000, "projection: the plan changes nothing")
    local n, sc = G.project_apply(it, plan)
    check(n == 12 and sc == 30, "projection: 12 faces placed, 30 px a unit (" .. tostring(n) .. ", " .. tostring(sc) .. ")")
    local cells, inside, flag = {}, true, true
    for _, f in ipairs(it.f) do
      if f[4] ~= -1 or not f[5] then flag = false end
      local c0
      for i = 1, 6, 2 do
        local u, v = f[5][i], f[5][i + 1]
        if u < 0 or v < 0 or u > 96 or v > 64 then inside = false end
        local c = math.floor(u / 32 - 1e-9) + 3 * math.floor(v / 32 - 1e-9)
        c0 = c0 or c
        if c ~= c0 then inside = false end
      end
      cells[c0] = true
    end
    local nc = 0
    for _ in pairs(cells) do nc = nc + 1 end
    check(flag and inside and nc == 6, "projection: every face is textured, in one of 6 cells, inside 3 x 2 cells (" .. nc .. " cells)")
    -- a long box: the same scale on every side, another place and cell size
    local box = { v = {}, f = {} }
    G.primitive(box, "cube", { 0, 0, 0 }, 1, 0x00FF00)
    for _, p in ipairs(box.v) do p[1] = p[1] * 4 end
    G.project_box(box, nil, { 8, 16, 64 }, 256, 256)
    local u0, u1 = 1e9, -1e9
    for _, f in ipairs(box.f) do for i = 1, 6, 2 do u0, u1 = math.min(u0, f[5][i]), math.max(u1, f[5][i]) end end
    check(u0 >= 8 and u1 <= 8 + 192, "projection: another place and cell size stay inside (" .. u0 .. " to " .. u1 .. ")")
    -- the chosen faces only
    local it3 = cube_item()
    local n3 = G.project_box(it3, { 1, 2 }, { 0, 0, 16 }, 256, 256)
    check(n3 == 2 and it3.f[1][4] == -1 and it3.f[3][4] == 0xFF0000, "projection: only the faces given")
    local a, e1 = G.project_plan(cube_item(), nil, { 0, 0, 100 }, 256, 256)
    check(a == nil and e1:find("too small", 1, true), "projection: a sheet too small is refused")
    local a2, e2 = G.project_plan(cube_item(), {}, { 0, 0, 16 }, 256, 256)
    check(a2 == nil and e2:find("no faces", 1, true), "projection: no faces is refused")
    -- seen from the front (+z) x grows to the right, y up is v down
    local sq = { v = { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } }, f = { { 1, 2, 3, 0 }, { 1, 3, 4, 0 } } }
    G.project_box(sq, nil, { 0, 0, 32 }, 256, 256)
    local f1 = sq.f[1][5]
    check(f1[3] > f1[1] and f1[6] < f1[2], "projection: a front face reads left to right and bottom to top")
  end

  ---------------------------------------------------------------- humanoid, pure
  do
    local h = G.humanoid("man")
    local ok, euler = closed(h)
    check(#h.v == 136 and #h.f == 204 and h.bones == 16 and #h.vb == 136 and ok and euler == 34,
          "humanoid: 17 boxes, 136 vertices, 204 triangles, 16 bones (" .. #h.v .. "/" .. #h.f .. ")")
    local rig = G.rig_bones(h)
    check(#rig == 16 and rig[1].name == "pelvis" and rig[16].name == "foot.R" and G.skin_state(h) == "skinned", "humanoid: the skeleton and its skin")
    local used, cols = {}, {}
    for i = 1, 136 do used[h.vb[i]] = true end
    for _, f in ipairs(h.f) do cols[f[4]] = true end
    local nused, ncols = 0, 0
    for _ in pairs(used) do nused = nused + 1 end
    for _ in pairs(cols) do ncols = ncols + 1 end
    check(nused == 16 and ncols == 5, "humanoid: every bone has vertices, 5 colours (" .. nused .. ", " .. ncols .. ")")
    local list = {}
    for i = 1, 136 do list[i] = i end
    local mp = G.mirror_pairs(h, list)
    local lonely, wrong = 0, 0
    for i = 1, 136 do
      if not mp[i] then lonely = lonely + 1
      elseif G.mirror_bone(rig, h.vb[i]) ~= h.vb[mp[i]] then wrong = wrong + 1 end
    end
    check(lonely == 0 and wrong == 0, "humanoid: every vertex has a twin across x = 0 on the mirror bone (" .. lonely .. " lonely, " .. wrong .. " wrong)")
    local lo, hi = 1e9, -1e9
    for _, p in ipairs(h.v) do lo, hi = math.min(lo, p[2]), math.max(hi, p[2]) end
    check(same(lo, 0) and hi > 1.85 and hi < 1.95, "humanoid: stands on y = 0, about 1.9 high (" .. hi .. ")")
  end

  ---------------------------------------------------------------- Studio block shapes, pure
  do
    local T = E.require("bm3d")
    -- the faces of T.block_shape as a triangle mesh: shared corners welded by position
    local function tri_mesh(faces)
      local it, index = { v = {}, f = {} }, {}
      local function vid(p)
        local k = string.format("%.4f,%.4f,%.4f", p[1], p[2], p[3])
        if not index[k] then it.v[#it.v + 1] = { p[1], p[2], p[3] }; index[k] = #it.v end
        return index[k]
      end
      for _, f in ipairs(faces) do
        for j = 2, #f.p - 1 do it.f[#it.f + 1] = { vid(f.p[1]), vid(f.p[j]), vid(f.p[j + 1]), 0 } end
      end
      return it
    end
    local want = { [2] = { faces = 5, vol = 0.5 }, [3] = { faces = 5, vol = 1 / 3 }, [4] = { faces = 14, vol = 0.82 } }
    for kind = 2, 4 do
      for _, view in ipairs({ { 3, 1, 1, 1 }, { 3, -1, 1, -1 }, { 1, 1, 3, -1 }, { 1, -1, 3, 1 } }) do
        local faces = T.block_shape(kind, { 2, 3, -1 }, view[1], view[2], view[3], view[4])
        local it = tri_mesh(faces)
        local ok, euler = closed(it)
        local name = T.BLOCK_KINDS[kind] .. " seen along axis " .. view[1] .. " sign " .. view[2]
        check(#faces == want[kind].faces and ok and euler == 2, "block shape " .. name .. ": " .. #faces .. " faces, closed, looks out (Euler " .. tostring(euler) .. ")")
        check(math.abs(volume(it) - want[kind].vol) < 1e-6, "block shape " .. name .. ": volume " .. volume(it))
        local inside = true
        for _, p in ipairs(it.v) do
          if p[1] < 2 - 1e-9 or p[1] > 3 + 1e-9 or p[2] < 3 - 1e-9 or p[2] > 4 + 1e-9 or p[3] < -1 - 1e-9 or p[3] > 0 + 1e-9 then inside = false end
        end
        check(inside, "block shape " .. name .. ": inside its cell")
      end
    end
    check(T.block_shape(1, { 0, 0, 0 }, 3, 1, 1, 1) == nil, "block shape: the cube is bm Studio's own")
    -- the ramp rises away from the camera: its high wall is on the far side
    local r = T.block_shape(2, { 0, 0, 0 }, 3, 1, 1, 1)
    local hi = 0
    for _, f in ipairs(r) do for _, p in ipairs(f.p) do if p[2] == 1 then hi = math.max(hi, p[3]) end end end
    local lo = 1
    for _, f in ipairs(r) do for _, p in ipairs(f.p) do if p[2] == 1 then lo = math.min(lo, p[3]) end end end
    check(hi == 1 and lo == 1, "block shape: the ramp's top edge is on the far side (z = 1)")
  end

  ---------------------------------------------------------------- in the editor
  local function new_cube(name)
    key("f1", "n")
    key("r")
    type_text(name)
    key("f2")
    for _ = 1, 3 do if sees("VERTICES:") then break end key("\t") end
    key("^s")
    return status():match("saved /bme/(%S+)") or status():match("saved /carts/(%S+)")
  end
  local function edges_mode() for _ = 1, 3 do if sees("EDGES:") then return end key("\t") end end
  local fname = new_cube("bvl")
  local m0 = model_of(fname, "bvl")
  check(m0 and m0.nv == 8 and m0.nf == 12, "bevel: a new cube for the keys")
  key("E")
  check(status():find("choose edges", 1, true), "Shift+E with nothing chosen: a message (" .. status() .. ")")
  edges_mode()
  key("a")                                        -- every edge, the diagonals too
  key("E")
  check(sees("bevel width"), "Shift+E asks for the width")
  type_text("0.1")
  key("^s")
  local m1 = model_of(fname, "bvl")
  check(m1 and m1.nv == 24 and m1.nf == 44 and status():find("saved", 1, true), "bevel keys: 24 vertices, 44 triangles (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  check(m1 and outward(m1), "bevel keys: the faces still look out")
  check(sees("FACES:"), "after a bevel the strips are chosen, in face mode")
  key("^z", "^s")
  m1 = model_of(fname, "bvl")
  check(m1 and m1.nv == 8 and m1.nf == 12, "bevel: one undo step brings the cube back")
  key("^y", "^s")
  m1 = model_of(fname, "bvl")
  check(m1 and m1.nv == 24 and m1.nf == 44, "bevel: redo")
  key("^z")
  -- a bad width
  edges_mode()
  key("a", "E")
  type_text("abc")
  check(status():find("number above 0", 1, true), "bevel: a width that is no number is refused (" .. status() .. ")")
  -- one edge: 1 strip; with the live mirror its counterpart too
  local function none() if not sees(": 0 chosen") then key("a") end end
  local function edge_pick(a, b)               -- the edge (a, b) of the cube: n steps through the sorted keys
    local keys = {}
    for k in pairs(G.edge_faces({ f = m0.faces })) do keys[#keys + 1] = k end
    table.sort(keys)
    edges_mode()
    none()
    key("home")
    for i, k in ipairs(keys) do
      key("n")
      if k == G.edge_key(a, b) then key(" "); return end
    end
  end
  edge_pick(1, 4)
  key("E")
  type_text("0.1")
  key("^s")
  m1 = model_of(fname, "bvl")
  check(m1 and m1.nv == 10 and m1.nf == 16, "bevel one edge, live mirror off: 10 vertices, 16 triangles (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  key("^z")
  key("X")
  edge_pick(1, 4)
  check(sees("EDGES: 2 chosen"), "bevel: the live mirror chooses the counterpart edge too (" .. status() .. ")")
  key("E")
  type_text("0.1")
  key("^s")
  m1 = model_of(fname, "bvl")
  local mirror_ok = false
  if m1 then
    local lp = {}
    for i = 1, m1.nv do lp[i] = i end
    local mp = G.mirror_pairs({ v = m1.verts }, lp)
    mirror_ok = true
    for i = 1, m1.nv do if not mp[i] then mirror_ok = false end end
  end
  check(m1 and (m1.nv == 12 and m1.nf == 20) and mirror_ok, "bevel with the live mirror: both sides, every vertex has a twin (" .. (m1 and (m1.nv .. "/" .. m1.nf) or "-") .. ")")
  key("^z", "X")

  -- the projection
  local function uv_of(name, mname)
    local bin = section(file(name), 8)
    local n = string.unpack("<I2", bin)
    local pos = 9
    for _ = 1, n do
      local nm = bin:sub(pos, pos + 15):match("^[^\0]*")
      local nv, nf = string.unpack("<I2I2", bin, pos + 16)
      local out = {}
      for i = 1, nf do
        local f = pos + 24 + nv * 12 + (i - 1) * 24
        local _, _, _, _, col, u0, v0, u1, v1, u2, v2 = string.unpack("<I2I2I2I2I4I2I2I2I2I2I2", bin, f)
        out[i] = { col = col, uv = { u0 / 8, v0 / 8, u1 / 8, v1 / 8, u2 / 8, v2 / 8 } }
      end
      if nm == mname then return out end
      pos = pos + 24 + nv * 12 + nf * 24
    end
  end
  local pname = new_cube("prj")
  key("U")
  check(sees("sheet cell px"), "Shift+U asks for the cell size and the place")
  type_text("32 8 16")
  key("^s")
  local fs = uv_of(pname, "prj")
  local cells, textured, within = {}, 0, true
  for _, f in ipairs(fs or {}) do
    if f.col & 0x80000000 ~= 0 then textured = textured + 1 end
    local c0
    for i = 1, 6, 2 do
      local u, v = f.uv[i], f.uv[i + 1]
      if u < 8 or v < 16 or u > 8 + 96 or v > 16 + 64 then within = false end
      local c = math.floor((u - 8) / 32 - 1e-9) + 3 * math.floor((v - 16) / 32 - 1e-9)
      c0 = c0 or c
    end
    cells[c0] = true
  end
  local nc = 0
  for _ in pairs(cells) do nc = nc + 1 end
  check(fs and #fs == 12 and textured == 12 and within and nc == 6, "projection keys: 12 faces textured, in 6 cells at 8,16 (" .. textured .. ", " .. nc .. ")")
  key("^z")
  key("U")
  type_text("500 0 0")
  check(status():find("too small", 1, true), "projection: a cell too big for the sheet is refused (" .. status() .. ")")
  check(in_help("shift e", "bevel") and in_help("shift u", "project"), "keyhelp: bevel and projection")

  -- the humanoid base from the list
  key("f1")
  key("f3")
  check(sees("New model: humanoid"), "F3 on the list: the humanoid row")
  for _ = 1, 10 do key("down") end
  key("\n")
  check(status():find("new model man", 1, true) and status():find("16 bones", 1, true), "humanoid: the new model says its bones (" .. status() .. ")")
  key("^s")
  local hf = file(pname)
  local hm = model_of(pname, "man")
  local rigs = anim_rigs(section(hf, 9))
  check(hm and hm.nv == 136 and hm.nf == 204 and rigs.man and rigs.man.nb == 16 and rigs.man.nv == 136 and rigs.man.nc == 0,
        "humanoid: saved with 136 vertices, 204 triangles and a skeleton of 16 bones")
  local function bname(i) return rigs.man.bones:sub((i - 1) * 44 + 1, i * 44):match("^[^\0]*") end
  check(rigs.man and bname(1) == "pelvis" and bname(4) == "head" and bname(13) == "foot.L",
        "humanoid: the bone names in the file")
  key("f2")
  check(sees("136 of 65535 vertices"), "humanoid: bm Mesh edits it")
end

-- the fixed chip "F12 keys" on the hint bar of every page
dofile(ROOT .. "/tests/studio/hintchip.lua"){
  name = "bm Mesh", check = check, texts = function() return texts end, hy = 320, w = 640, chip_w = chip_w,
  click = click, shown = function() return showkeys_n end,
  pages = {
    { name = "list", go = function() key("f1") end },
    { name = "edit", go = function() key("f2") end },
    { name = "menu", go = function() key("esc") end },
  } }
key("f2")
do
  local function on_bar(s) for _, t in ipairs(texts) do if t[3] == 320 and t[1] == s then return true end end end
  check(on_bar("all") and on_bar("mode") and on_bar("extrude"), "edit page: the bar has choose, all (a), mode and extrude next to the fixed chip")
end

io.write(string.format("bm Mesh: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
