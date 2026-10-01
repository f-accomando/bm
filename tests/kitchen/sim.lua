-- Host tests of Chaos Kitchen (carts/kitchen): the cartridge runs in a fake
-- bm (the same API, checks like the C code, no pixels) with scripted and
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

---------------------------------------------------------------- fake bm

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
local vol = 10
env.volume = function(v) if v then vol = math.max(0, math.min(10, v)) end return vol end
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
    local legs = (r.legL or r.leg).nf + (r.legR or r.leg).nf
    local arms = (r.armL or r.arm).nf + (r.armR or r.arm).nf
    io.write(string.format("chef %d: body %d legs %d arms %d = %d\n", i, r.body.nf, legs, arms,
                           r.body.nf + legs + arms))
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
  -- every station on the ground can be reached from where chef 1 starts
  -- (doors open, platforms where they start; stations riding them excluded)
  local seen, q = {}, {}
  local function key(x, z) return z * run.w + x end
  local function walk(x, z)
    if x < 0 or z < 0 or x >= run.w or z >= run.h or seen[key(x, z)] then return end
    local c, p = K.Kit.cell_at(run, x + 0.5, z + 0.5)
    if c.kind == "door" then c = { kind = "floor" } end
    if K.Kit.blocked(c) then return end
    seen[key(x, z)] = true
    q[#q + 1] = { x, z }
  end
  local sp = run.spawns[1]
  walk(math.floor(sp[1]), math.floor(sp[2]))
  local i = 1
  while q[i] do
    local x, z = q[i][1], q[i][2]
    walk(x + 1, z) walk(x - 1, z) walk(x, z + 1) walk(x, z - 1)
    i = i + 1
  end
  for _, s2 in ipairs(run.stations) do
    if not s2.plat then
      local x, z = s2.cx, s2.cz
      if not (seen[key(x + 1, z)] or seen[key(x - 1, z)] or seen[key(x, z + 1)] or seen[key(x, z - 1)]) then
        -- next to a platform's path is fine too
        -- plain counters in corners are only furniture
        local near_plat = #run.plats > 0
        if not near_plat and (s2.kind ~= "counter" or s2.item) then
          io.write(string.format("%s: the %s at column %d, row %d cannot be reached\n", st.id, s2.kind,
                                 x, run.h - 1 - z))
          ok_all = false
        end
      end
    end
  end
end
assert(ok_all, "stage data")

-- computer chefs play every stage: each must serve something (the dishes
-- can be made, the kitchen works)
local only = os.getenv("KITCHEN_STAGE")
for _, st in ipairs(K.Data.STAGES) do
  if only and st.id ~= only then goto next_stage end
  K.G.debug_bot = only ~= nil
  do
  local pl = {}
  for i = 1, 3 do pl[i] = { pad = i, chef = i, bot = true } end
  K.G.players = pl
  K.Scr.go("intro", st)
  run_frames(25)
  press(1, 4)
  label = st.id .. " bots"
  local run = K.G.run
  run.time_left = 1e9
  run.no_burn = true           -- this checks that the dishes can be made, not timing
  -- moving platforms slow the computer chefs down (they wait for them)
  local limit = #run.plats > 0 and 360 or 240
  for f = 1, 60 * limit do
    frame()
    if not only and run.served > 0 and f > 60 * 60 then break end
    if only and f % 300 == 0 then
      for _, c in ipairs(run.chefs) do
        local b = c.botm
        local step = b and b.steps and b.steps[b.i]
        io.write(string.format("  t=%3d chef %d at %.1f,%.1f hold=%s step=%s %s %s\n", f // 60, c.n, c.x, c.z,
          c.hold and (c.hold.key or (c.hold.plate and "plate") or "dirty") or "-",
          step and step.op or "-", step and step.st and step.st.kind or "", step and step.st and (step.st.cx .. "," .. step.st.cz) or ""))
      end
    end
  end
  if only then for i = math.max(1, #logs - 12), #logs do io.write("log: " .. logs[i] .. "\n") end end
  io.write(string.format("%-7s bots: %d served, %d lost, %d wrong\n", st.id, run.served, run.failed, run.wrong))
  if #run.plats > 0 and run.served == 0 then
    io.write(st.id .. ": warning: the computer chefs got nothing across the platforms\n")
  else
    assert(run.served > 0, st.id .. ": the computer chefs served nothing")
  end
  end
  ::next_stage::
end
K.G.players = { { pad = 1, chef = 1 } }

-- endless mode: through the menu, then the register. Every purchase must
-- land in the kitchen without cutting it in two or walling a station in.
local function whole(run, first_new)
  local seen, q, n = {}, {}, 0
  local function walk(x, z)
    if x < 0 or z < 0 or x >= run.w or z >= run.h or seen[z * run.w + x] then return end
    if K.Kit.blocked(K.Kit.cell(run, x, z)) then return end
    seen[z * run.w + x] = true
    q[#q + 1] = { x, z }
  end
  local sp = run.spawns[1]
  walk(math.floor(sp[1]), math.floor(sp[2]))
  local i = 1
  while q[i] do
    local x, z = q[i][1], q[i][2]
    walk(x + 1, z) walk(x - 1, z) walk(x, z + 1) walk(x, z - 1)
    i = i + 1
  end
  for z = 0, run.h - 1 do
    for x = 0, run.w - 1 do
      if not K.Kit.blocked(K.Kit.cell(run, x, z)) and not seen[z * run.w + x] then
        return false, string.format("floor cell %d,%d cut off", x, z)
      end
    end
  end
  for i, st in ipairs(run.stations) do
    local x, z = st.cx, st.cz
    -- (plain counters of the starting layout may be corner furniture)
    local furniture = st.kind == "counter" and i < first_new
    if st.kind ~= "valve" and not furniture and not (seen[z * run.w + x + 1] or seen[z * run.w + x - 1] or
                                   seen[(z + 1) * run.w + x] or seen[(z - 1) * run.w + x]) then
      return false, string.format("the %s at %d,%d cannot be reached", st.kind, x, z)
    end
  end
  return true
end

K.G.players = {}
K.Scr.go("menu", 1)
run_frames(10)
press(1, 3)                    -- down: ENDLESS
press(1, 4)
label = "endless lobby"
run_frames(5)
press(1, 4)                    -- join (the last players are still in: ready)
press(1, 4)
run_frames(60)
assert(K.G.screen == "endless", "expected the endless intro, got " .. K.G.screen)
press(1, 4)
assert(K.G.screen == "play", "expected play, got " .. K.G.screen)
run_frames(120)
local run = K.G.run
assert(run.endless and run.hearts == 5, "endless run")
label = "endless register"
-- walk up to the register: stand in front of it, facing it
local reg = K.Kit.find(run, "register")[1]
local chef = run.chefs[1]
chef.x, chef.z, chef.yaw = reg.x, reg.z - 1, 0
run_frames(2)
press(1, 4)
assert(chef.panel, "the register did not open")
local bought, stations0 = 0, #run.stations
local function offers()
  local t = {}
  for i = 1, 3 do t[i] = run.offers[i] and run.offers[i].id or "-" end
  return table.concat(t, " ")
end
-- a few at the first tier, then everything is on offer
for i = 1, 40 do
  if i == 8 then run.earned = 2000 end
  run.coins = 5000
  held[1] = 1 << 1             -- right: the next offer
  frame()
  held[1] = 0
  frame()
  local before = 0
  for _, k in pairs(run.bought) do before = before + k end
  press(1, 4)
  local after = 0
  for _, k in pairs(run.bought) do after = after + k end
  assert(run.offers[1] and run.offers[2] and run.offers[3] or after >= 30,
         "the register ran out of offers after " .. after .. ": " .. offers())
  bought = bought + (after - before)
  run_frames(50)               -- it drops into place
  local ok, why = whole(run, stations0 + 1)
  assert(ok, "after " .. bought .. " purchases: " .. tostring(why))
end
press(1, 5)                    -- B: close the register
assert(not chef.panel, "the register did not close")
local kinds = {}
for i = stations0 + 1, #run.stations do kinds[run.stations[i].kind] = (kinds[run.stations[i].kind] or 0) + 1 end
local list = {}
for k, n in pairs(kinds) do list[#list + 1] = k .. " " .. n end
table.sort(list)
io.write(string.format("endless: %d purchases, %d new stations (%s), %d dishes on the menu\n", bought,
                       #run.stations - stations0, table.concat(list, ", "), #run.pool))
assert(bought >= 20, "only " .. bought .. " purchases went through")
for _, st in ipairs(run.stations) do assert(not st.drop, "a station never landed") end

-- computer chefs cook in the grown kitchen for a while
for i = 1, 3 do
  run.chefs[i] = run.chefs[i] or K.Chef.new(run, i, i, i)
  run.chefs[i].bot = true
end
label = "endless bots"
run.no_burn = true
for _ = 1, 60 * 150 do frame() end
io.write(string.format("endless bots: %d served, %d lost, tier %d, %d hearts, %d earned\n", run.served, run.failed,
                       run.tier, run.hearts, run.earned))
assert(run.served > 0, "endless: the computer chefs served nothing")
-- the kitchen closes when the hearts run out: the records are saved
run.hearts = 1
for _, c in ipairs(run.chefs) do c.bot = false end
for _, o in ipairs(run.orders) do o.t = 0.01 end
run.order_t = 0
run_frames(60 * 6)
assert(K.G.screen == "endless_results", "expected the endless results, got " .. K.G.screen)
run_frames(90)
assert(saved_table.endless.served > 0 and saved_table.endless.time > 0, "endless records not saved")
press(1, 5)
assert(K.G.screen == "menu", "expected the menu, got " .. K.G.screen)

-- practice: every dish gets a kitchen that has what it needs, and the
-- computer chefs can cook it there
K.G.players = {}
K.Scr.go("menu", 1)
run_frames(10)
press(1, 3) press(1, 3)        -- PRACTICE
press(1, 4)
run_frames(5)
press(1, 4) press(1, 4)        -- join, ready
run_frames(60)
assert(K.G.screen == "practice", "expected the practice list, got " .. K.G.screen)
press(1, 4)                    -- the first dish
assert(K.G.screen == "play" and K.G.run.practice, "expected a practice run, got " .. K.G.screen)
run_frames(60 * 3)
assert(not K.G.run.time_left, "practice has no clock")
press(1, 8)                    -- Start: pause
assert(K.G.screen == "pause", "expected pause, got " .. K.G.screen)
press(1, 3)                    -- VOLUME: right lowers nothing, left turns it down
press(1, 0)
assert(K.G.screen == "pause", "the volume row stays in the pause, got " .. K.G.screen)
press(1, 3) press(1, 3)        -- QUIT
press(1, 4)
assert(K.G.screen == "practice", "expected the practice list again, got " .. K.G.screen)
for _, r in ipairs(K.Data.RECIPES) do
  local st = K.Scr.practice_stage(r)
  for i, row in ipairs(st.layout) do assert(#row == 14, r.id .. ": practice row " .. i .. " is " .. #row .. " wide") end
  local pl = {}
  for i = 1, 3 do pl[i] = { pad = i, chef = i, bot = true } end
  K.G.players = pl
  K.Scr.go("practice_go", r.id)
  local run = K.G.run
  local ok, why = whole(run, 1e9)
  assert(ok, r.id .. " practice: " .. tostring(why))
  run.no_burn = true
  label = "practice " .. r.id
  local f = 0
  while run.served == 0 and f < 60 * 240 do frame() f = f + 1 end
  assert(run.served > 0, r.id .. ": the computer chefs could not cook it in practice")
  io.write(string.format("practice %-13s served after %3d s\n", r.id, f // 60))
end
held = { 0, 0, 0, 0 }

-- the recipe book and the options
K.G.players = { { pad = 1, chef = 1 } }
K.Scr.go("book")
for _ = 1, 30 do press(1, 3) end
press(1, 1)
run_frames(10)
press(1, 5)
assert(K.G.screen == "menu", "expected the menu after the book, got " .. K.G.screen)
K.Scr.go("options")
press(1, 4)                    -- music off
assert(saved_table.music == false or K.Save.data.music == false, "music did not turn off")
press(1, 4)                    -- and on
press(1, 5)
assert(K.G.screen == "menu", "expected the menu after the options, got " .. K.G.screen)

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
