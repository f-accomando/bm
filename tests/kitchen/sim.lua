-- Host tests of Chaos Kitchen (carts/kitchen): the cartridge runs in a fake
-- bm33 (the same API, checks like the C code, no pixels) with scripted and
-- random input. It walks through the menus, plays every stage with 1 and 4
-- chefs, and reports the heaviest frames: Lua instructions and 3D triangles,
-- the two costs that matter on the Pi.
--
--   luahost tests/kitchen/sim.lua build/kitchen/main.lua build/kitchen/main.map [seconds]

local SRC, MAP, SECS = arg[1], arg[2], tonumber(arg[3] or "25")

---------------------------------------------------------------- line map

local spans = {}
for l in io.lines(MAP) do
  local a, b, name = l:match("^(%d+) (%d+) (.+)$")
  spans[#spans + 1] = { tonumber(a), tonumber(b), name }
end
local function where(msg)
  return (msg:gsub("main%.lua:(%d+)", function(n)
    n = tonumber(n)
    for _, s in ipairs(spans) do
      if n >= s[1] and n <= s[2] then return s[3] .. ":" .. (n - s[1] + 1) end
    end
    return "main.lua:" .. n
  end))
end

---------------------------------------------------------------- fake bm33

local env = {}
local frame_tris, frame_calls, frame_meshes = 0, 0, 0
local cam = { x = 0, y = 0, z = -5, yaw = 0, pitch = 0, fov = 60 }
local held, prev = { 0, 0, 0, 0 }, { 0, 0, 0, 0 }
local saved_table = nil
local now = 0
local logs = {}

local function chk(cond, msg) if not cond then error(msg, 3) end end
local function num(v, name) chk(type(v) == "number", (name or "argument") .. ": number expected, got " .. type(v)) end

for _, n in ipairs({ "cls", "pset", "line", "rect", "rectfill", "circ", "circfill", "tri", "camera",
                     "clip", "spr", "sspr", "map", "mset", "sset", "note", "noteoff", "freq", "envelope",
                     "duty", "light3d", "fog3d", "light_begin", "light", "light_end", "quit" }) do
  env[n] = function(...) frame_calls = frame_calls + 1 end
end
env.SCREEN_W, env.SCREEN_H = 640, 360
env.SQUARE, env.TRIANGLE, env.SAW, env.NOISE = 0, 1, 2, 3
env.print = function(s, x, y, c, scale)
  frame_calls = frame_calls + 1
  num(x, "print x"); num(y, "print y")
  local t = tostring(s)
  return x + #t * 8 * (scale or 1)
end
env.pget = function() return 0 end
env.mget = function() return 0 end
env.sget = function() return nil end
env.rgb = function(r, g, b) return (r & 255) << 16 | (g & 255) << 8 | (b & 255) end
env.playing = function() return false end
env.apu = function() return 0 end
env.log = function(...) local t = { ... } for i = 1, #t do t[i] = tostring(t[i]) end logs[#logs + 1] = table.concat(t, "\t") end
env.time = function() return now end
env.stat = function(n) if n == 3 then return math.floor(now * 60) elseif n == 4 then return frame_tris end return 0 end
env.save = function(t) saved_table = t return true end
env.saved = function() return saved_table end
env.keyp = function() return nil end
env.lamp3d = function(i, x, y, z, r)
  if i ~= nil then chk(i >= 1 and i <= 4, "lamp 1 to 4") end
end
env.zclear = function() end

local mesh_mt = {}
env.mesh = function(v, f, uv)
  chk(type(v) == "table" and type(f) == "table", "mesh: tables expected")
  local nv, nf = #v // 3, #f // 4
  chk(nv > 0 and nv <= 4096, "mesh: 1 to 4096 vertices, got " .. nv)
  chk(nf > 0 and nf <= 16384, "mesh: 1 to 16384 faces, got " .. nf)
  for i = 1, nv * 3 do chk(type(v[i]) == "number", "mesh: vertex " .. i .. " not a number") end
  for i = 1, nf do
    for k = 1, 3 do
      local idx = f[(i - 1) * 4 + k]
      chk(math.type(idx) == "integer" and idx >= 1 and idx <= nv,
          "face " .. i .. ": vertex index " .. tostring(idx) .. " out of range")
    end
    chk(type(f[i * 4]) == "number", "face " .. i .. ": colour")
  end
  if uv then chk(#uv >= nf * 6, "mesh: 6 texture coordinates per face") end
  return setmetatable({ nf = nf, nv = nv }, mesh_mt)
end
env.mesh_sphere = function() return setmetatable({ nf = 96, nv = 60 }, mesh_mt) end
env.mesh_cube = function() return setmetatable({ nf = 12, nv = 8 }, mesh_mt) end
env.draw3d = function(m, x, y, z, rx, ry, rz, s, flags)
  chk(getmetatable(m) == mesh_mt, "draw3d: bad mesh (" .. type(m) .. ")")
  num(x, "draw3d x"); num(y, "draw3d y"); num(z, "draw3d z")
  frame_tris = frame_tris + m.nf
  frame_meshes = frame_meshes + 1
end
env.camera3d = function(x, y, z, yaw, pitch, fov)
  num(x); num(y); num(z)
  cam = { x = x, y = y, z = z, yaw = yaw or 0, pitch = pitch or 0, fov = fov or 60 }
end
-- the same projection as r3d.c
env.project3d = function(x, y, z)
  local cy, sy = math.cos(cam.yaw), math.sin(cam.yaw)
  local cp, sp = math.cos(cam.pitch), math.sin(cam.pitch)
  local wx, wy, wz = x - cam.x, y - cam.y, z - cam.z
  local x1 = cy * wx - sy * wz
  local z1 = sy * wx + cy * wz
  local y2 = cp * wy - sp * z1
  local z2 = sp * wy + cp * z1
  if z2 < 0.1 then return nil end
  local f = 320 / math.tan(cam.fov * math.pi / 360)
  return 320 + x1 * f / z2, 180 - y2 * f / z2, z2
end
local function bits(p) return p == nil and (held[1] | held[2] | held[3] | held[4]) or (held[p] or 0) end
local function pbits(p) return p == nil and (prev[1] | prev[2] | prev[3] | prev[4]) or (prev[p] or 0) end
env.btn = function(b, p) return (bits(p) >> b) & 1 == 1 end
env.btnp = function(b, p) return (bits(p) >> b) & 1 == 1 and (pbits(p) >> b) & 1 == 0 end
env.players = function() return 4, 15 end
env.stick = function(p)
  local b = bits(p)
  local x = ((b >> 1) & 1) - (b & 1)
  local y = ((b >> 3) & 1) - ((b >> 2) & 1)
  if x ~= 0 and y ~= 0 then x, y = x * 0.7071, y * 0.7071 end
  return x, y
end
for _, lib in ipairs({ "string", "table", "math", "utf8", "coroutine" }) do env[lib] = _G[lib] end
for _, f in ipairs({ "assert", "error", "ipairs", "next", "pairs", "pcall", "rawequal", "rawget", "rawlen",
                     "rawset", "select", "setmetatable", "getmetatable", "tonumber", "tostring", "type", "xpcall" }) do
  env[f] = _G[f]
end

---------------------------------------------------------------- load

local f = assert(io.open(SRC))
local code = f:read("a")
f:close()
local chunk, err = load(code, "=main.lua", "t", env)
if not chunk then io.stderr:write(where(err) .. "\n") os.exit(1) end

local instr = 0
local function counter() instr = instr + 1000 end

local function call(name)
  local fn = env[name]
  local ok, e = xpcall(fn, debug.traceback)
  if not ok then
    io.stderr:write(where(e) .. "\n")
    for i = math.max(1, #logs - 5), #logs do io.stderr:write("log: " .. logs[i] .. "\n") end
    os.exit(1)
  end
end

local ok, e = xpcall(chunk, debug.traceback)
if not ok then io.stderr:write(where(e) .. "\n") os.exit(1) end

local K = env.KITCHEN
local worst = { tris = 0, calls = 0, instr = 0, meshes = 0, where = "" }
local label = "boot"

local function frame()
  now = now + 1 / 60
  instr, frame_tris, frame_calls, frame_meshes = 0, 0, 0, 0
  debug.sethook(counter, "", 1000)
  call("_update")
  call("_draw")
  debug.sethook()
  prev[1], prev[2], prev[3], prev[4] = held[1], held[2], held[3], held[4]
  if frame_tris > worst.tris then worst.tris, worst.where = frame_tris, label end
  worst.calls = math.max(worst.calls, frame_calls)
  worst.meshes = math.max(worst.meshes, frame_meshes)
  if instr > worst.instr then worst.instr, worst.iwhere = instr, label end
end

local function press(p, b, frames)
  held[p] = held[p] | (1 << b)
  for _ = 1, frames or 2 do frame() end
  held[p] = held[p] & ~(1 << b)
  frame()
end

local function run_frames(n) for _ = 1, n do frame() end end

-- random play: steer for a while, press buttons now and then
local rng = { dir = {}, t = {} }
local DIRS = { 0, 1, 2, 4, 8, 5, 9, 6, 10 }     -- none, left, right, up, down, diagonals
local function random_input(p)
  if (rng.t[p] or 0) <= 0 then
    rng.dir[p] = DIRS[math.random(#DIRS)]
    rng.t[p] = math.random(10, 50)
  end
  rng.t[p] = rng.t[p] - 1
  local b = rng.dir[p]
  local r = math.random()
  if r < 0.05 then b = b | (1 << 4) elseif r < 0.08 then b = b | (1 << 6)
  elseif r < 0.095 then b = b | (1 << 7) elseif r < 0.11 then b = b | (1 << 5) end
  held[p] = b
end

---------------------------------------------------------------- scenario

math.randomseed(42)
K.G.no_save = false
call("_init")
if os.getenv("KITCHEN_COUNTS") then
  -- triangles of each model, to keep the frame within budget
  for i, r in ipairs(K.Mesh.chef) do
    io.write(string.format("chef %d: body %d leg %d arm %d = %d\n", i, r.body.nf, r.leg.nf, r.arm.nf,
                           r.body.nf + 2 * r.leg.nf + 2 * r.arm.nf))
  end
  local items = {}
  for _, g in ipairs(K.Data.ING_LIST) do
    local a, b = K.Mesh.item(g.id), K.Mesh.item(g.id .. "/c")
    items[#items + 1] = string.format("%s %d/%d", g.id, a and a.nf or 0, b and b.nf or 0)
  end
  io.write(table.concat(items, "  ") .. "\n")
  for _, st in ipairs(K.Data.STAGES) do
    local run = K.Scr.new_run(st, { players = { { pad = 1, chef = 1 } } })
    local function sum(list) local n = 0 for _, m in ipairs(list) do n = n + m.nf end return n end
    io.write(string.format("%s: floor %d static %d water %d\n", st.id, sum(run.meshes.floor), sum(run.meshes.static),
                           run.meshes.water and run.meshes.water.nf or 0))
  end
end
label = "title"
run_frames(90)
press(1, 4)                    -- title -> menu
label = "menu"
run_frames(10)
press(1, 4)                    -- campaign -> lobby
label = "lobby"
run_frames(10)
press(1, 4)                    -- join
press(1, 1)                    -- next chef
press(1, 0)                    -- back
press(1, 4)                    -- ready
run_frames(60)
assert(K.G.screen == "map", "expected the map, got " .. K.G.screen)
press(1, 4)                    -- stage 1-1
run_frames(30)
assert(K.G.screen == "intro", "expected the intro, got " .. K.G.screen)
press(1, 4)
assert(K.G.screen == "play", "expected play, got " .. K.G.screen)
label = "1-1 x1"
for _ = 1, 60 * SECS do random_input(1); frame() end
held[1] = 0
local run = K.G.run
io.write(string.format("1-1 with one chef: %d orders open, %d served, %d lost\n",
                       #run.orders, run.served, run.failed))

-- every stage with 1 and 4 chefs, straight into play
for _, st in ipairs(K.Data.STAGES) do
  for _, n in ipairs({ 1, 4 }) do
    local pl = {}
    for i = 1, n do pl[i] = { pad = i, chef = i } end
    K.G.players = pl
    K.Scr.go("intro", st)
    run_frames(25)
    press(1, 4)
    label = st.id .. " x" .. n
    local sum, cnt, peak = 0, 0, 0
    for i = 1, 60 * SECS do
      for p = 1, n do random_input(p) end
      frame()
      if i > 120 then
        sum, cnt, peak = sum + instr, cnt + 1, math.max(peak, instr)
      end
    end
    local avg = sum / math.max(1, cnt)
    io.write(string.format("%-7s Lua %6d instr/frame avg, %6d max (~%.1f ms on the Pi)\n", label, math.floor(avg), peak,
                           avg * 40e-6))
    held = { 0, 0, 0, 0 }
  end
end

-- every stage: its dishes need only stations and crates it has
local ok_all = true
for _, st in ipairs(K.Data.STAGES) do
  local run = K.Scr.new_run(st, { players = { { pad = 1, chef = 1 } } })
  local have = {}
  for _, s in ipairs(run.stations) do have[s.kind] = true; if s.ing then have["ing:" .. s.ing] = true end end
  local BOX = { boil = "pot", fry = "pan", bake = "oven", blend = "blender", chop = "board" }
  for _, id in ipairs(st.recipes) do
    local r = K.Data.RECIPE[id]
    assert(r, st.id .. ": unknown recipe " .. id)
    for need in pairs(r.st) do
      if not have[BOX[need]] then io.write(st.id .. ": " .. id .. " needs a " .. BOX[need] .. "\n") ok_all = false end
    end
    for ing in pairs(r.ing) do
      if not have["ing:" .. ing] then io.write(st.id .. ": " .. id .. " needs " .. ing .. "\n") ok_all = false end
    end
  end
  for _, need in ipairs({ "serve", "plates" }) do
    if not have[need] then io.write(st.id .. ": no " .. need .. "\n") ok_all = false end
  end
  if st.wash and not (have.sink and have.ret) then io.write(st.id .. ": washing needs a sink and R\n") ok_all = false end
end
assert(ok_all, "stage data")

-- computer chefs play every stage: each must serve something (the dishes
-- can be made, the kitchen works)
for _, st in ipairs(K.Data.STAGES) do
  local pl = {}
  for i = 1, 2 do pl[i] = { pad = i, chef = i, bot = true } end
  K.G.players = pl
  K.Scr.go("intro", st)
  run_frames(25)
  press(1, 4)
  label = st.id .. " bots"
  local run = K.G.run
  run.time_left = 1e9
  for _ = 1, 60 * 90 do frame() end
  io.write(string.format("%-7s bots: %d served, %d lost, %d wrong\n", st.id, run.served, run.failed, run.wrong))
  assert(run.served > 0, st.id .. ": the computer chefs served nothing")
end
K.G.players = { { pad = 1, chef = 1 } }

-- let one stage run to the end: results, save
K.G.players = { { pad = 1, chef = 2 } }
K.Scr.go("intro", K.Data.STAGES[1])
run_frames(25)
press(1, 4)
K.G.run.time_left = 1
run_frames(60 * 6)
assert(K.G.screen == "results", "expected results, got " .. K.G.screen)
run_frames(120)
press(1, 5)
assert(saved_table and saved_table.v == 1, "nothing saved")

io.write(string.format("heaviest frame: %d triangles in %d meshes (%s), %d Lua instructions (%s), %d draw calls\n",
                       worst.tris, worst.meshes, worst.where, worst.instr, worst.iwhere or "", worst.calls))
io.write("kitchen sim: ok\n")
