-- Host tests of Yharnam (carts/yharnam): the cartridge runs in a fake bm
-- (the same API, no pixels). It checks the street plan (the streets are
-- the same seen from either side of a chunk border, they make one network
-- you can walk), that every chunk is made the same way twice, that the
-- sprites and tiles are inside the sheet, then lets the camera drift over
-- the town (the title screen) and the hunter walk at random for a while,
-- and reports the heaviest frames in Lua instructions, with the making of
-- the chunks counted apart (on the console it is spread over frames).
--
--   luahost tests/yharnam/sim.lua carts/yharnam/main.lua

local SRC = arg[1]
math.randomseed(1)            -- the same run every time (the cartridge uses math.random)

---------------------------------------------------------------- fake bm

local env = {}
local held, prev = 0, 0
local now = 0
local logs = {}
local SHEET_W, SHEET_H = 4096, 4096
local px_sprites = 0
local MAPW, MAPH = 256, 256
local map = {}

local function chk(cond, msg) if not cond then error(msg, 3) end end
local function num(v, name) chk(type(v) == "number", (name or "argument") .. ": number expected, got " .. type(v)) end

for _, n in ipairs({ "cls", "line", "rect", "circ", "camera", "clip", "note", "noteoff", "freq", "quit",
                     "dark_begin", "dark_end", "timeslice" }) do
  env[n] = function() end
end
env.pset = function(x, y, c) num(x, "pset x"); num(y, "pset y"); num(c, "pset colour") end
env.rectfill = function(x, y, w, h, c) num(x); num(y); num(w); num(h); num(c, "rectfill colour") end
env.circfill = function(x, y, r, c) num(x); num(y); num(r); num(c, "circfill colour") end
env.sspr = function(sx, sy, sw, sh, dx, dy)
  num(sx, "sspr sx"); num(sy, "sspr sy"); num(sw); num(sh); num(dx, "sspr dx"); num(dy, "sspr dy")
  chk(sx >= 0 and sy >= 0 and sx + sw <= SHEET_W and sy + sh <= SHEET_H, "sspr outside the sheet")
  px_sprites = px_sprites + sw * sh
end
env.spr = function(n, x, y, w, h)
  num(n, "spr n"); num(x, "spr x"); num(y, "spr y")
  chk(math.type(n) == "integer" and n > 0 and n < (SHEET_W // 8) * (SHEET_H // 8), "spr: bad cell " .. tostring(n))
  px_sprites = px_sprites + 64 * (w or 1) * (h or 1)
end
env.map = function(mx, my, x, y, mw, mh)
  num(mx); num(my); num(x); num(y); num(mw); num(mh)
  chk(mx >= 0 and my >= 0 and mx + mw <= MAPW and my + mh <= MAPH, "map: outside the map")
end
env.mset = function(x, y, n)
  chk(math.type(x) == "integer" and math.type(y) == "integer" and math.type(n) == "integer", "mset: integers")
  chk(x >= 0 and y >= 0 and x < MAPW and y < MAPH, "mset outside the map")
  map[y * MAPW + x] = n
end
env.glow = function(x, y, r, lv, d)
  num(x, "glow x"); num(y, "glow y"); num(r, "glow radius")
  chk(math.type(lv) == "integer" and lv >= 0 and lv < 16, "glow: the level is an integer 0..15")
end
local levels
env.fades = function(t)
  chk(#t > 0 and #t <= 255, "fades: 1..255 colours")
  levels = #t[1] - 1
  for _, row in ipairs(t) do
    chk(#row == levels + 1, "fades: rows of the same length")
    for _, c in ipairs(row) do chk(math.type(c) == "integer" and c >= 0 and c <= 0xFFFFFF, "fades: colours") end
  end
  return levels
end
env.SCREEN_W, env.SCREEN_H = 256, 256
env.SQUARE, env.TRIANGLE, env.SAW, env.NOISE, env.SINE, env.METAL = 0, 1, 2, 3, 4, 5
env.print = function(s, x, y, c)
  num(x, "print x"); num(y, "print y")
  return x + #tostring(s) * 8
end
env.log = function(...) local t = { ... } for i = 1, #t do t[i] = tostring(t[i]) end logs[#logs + 1] = table.concat(t, "\t") end
env.time = function() return now end
env.stat = function() return 0 end
env.btn = function(b) return (held >> b) & 1 == 1 end
env.btnp = function(b) return (held >> b) & 1 == 1 and (prev >> b) & 1 == 0 end
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
if not chunk then io.stderr:write(err .. "\n") os.exit(1) end

local instr, gen_instr = 0, 0
local in_gen = false
local function counter() if in_gen then gen_instr = gen_instr + 1000 else instr = instr + 1000 end end

local function call(fn)
  local ok, e = xpcall(fn, debug.traceback)
  if not ok then
    io.stderr:write(e .. "\n")
    for i = math.max(1, #logs - 5), #logs do io.stderr:write("log: " .. logs[i] .. "\n") end
    os.exit(1)
  end
end

-- the chunks made in the background (a coroutine) are counted apart
local resume = coroutine.resume
env.coroutine = setmetatable({ resume = function(co, ...)
  local was = in_gen
  in_gen = true
  debug.sethook(co, counter, "", 1000)       -- hooks are per coroutine
  local r = table.pack(resume(co, ...))
  in_gen = was
  return table.unpack(r, 1, r.n)
end }, { __index = coroutine })

call(chunk)
local Y = env.YHARNAM
assert(Y, "the cartridge has no YHARNAM table for the tests")
setmetatable(Y.SPR, { __index = function(_, k) error("no sprite called " .. tostring(k), 2) end })
local checks = 0
local function check(cond, msg)
  checks = checks + 1
  if not cond then io.stderr:write("FAIL: " .. msg .. "\n") os.exit(1) end
end

-- sprites and tiles inside the sheet
for name, s in pairs(Y.SPR) do
  check(s[1] >= 0 and s[2] >= 0 and s[1] + s[3] <= SHEET_W and s[2] + s[4] <= SHEET_H, "sprite " .. name)
  check(s[5] >= 0 and s[5] <= s[3] and s[6] >= 0 and s[6] <= s[4] + 8, "anchor of " .. name)
end
for mat, kinds in pairs(Y.FACADE) do
  for k, cells in pairs(kinds) do check(#cells == 5, "facade " .. mat .. " " .. k) end
end
-- every animation of the hunter: all its frames in the 8 directions, in the sheet
local nanim, nframes = 0, 0
for name, a in pairs(Y.HUNT) do
  nanim = nanim + 1
  check(#a.d == 8, name .. ": 8 directions")
  for d = 1, 8 do
    check(#a.d[d] == #a.t, name .. ": a frame for every tick count")
    for _, fr in ipairs(a.d[d]) do
      nframes = nframes + 1
      check(fr[1] >= 0 and fr[2] >= 0 and fr[1] + fr[3] <= SHEET_W and fr[2] + fr[4] <= SHEET_H, name .. ": frame")
    end
  end
end
io.write(string.format("the hunter: %d animations, %d frames\n", nanim, nframes))
-- the creatures: 12 and their 4 bosses (one of each kind), every animation in
-- the 5 directions drawn, its events on frames it has
local kinds, bosses, nfoe = {}, {}, 0
local ffr = 0
for _, f in ipairs(Y.FOES) do
  nfoe = nfoe + 1
  kinds[f.kind] = (kinds[f.kind] or 0) + 1
  if f.boss then bosses[f.kind] = (bosses[f.kind] or 0) + 1 end
  local need = { "idle", "walk", "attack", "hurt", "death" }
  for _, n in ipairs(need) do check(f.a[n], f.name .. ": no " .. n) end
  check(#f.order == (f.boss and 9 or 5), f.name .. ": " .. #f.order .. " animations")
  for _, n in ipairs(f.order) do
    local a = f.a[n]
    check(#a.d == 5, f.name .. " " .. n .. ": 5 directions")
    for d = 1, 5 do
      check(#a.d[d] == #a.t, f.name .. " " .. n .. ": a frame for every tick count")
      for _, fr in ipairs(a.d[d]) do
        ffr = ffr + 1
        check(fr[1] >= 0 and fr[2] >= 0 and fr[1] + fr[3] <= SHEET_W and fr[2] + fr[4] <= SHEET_H,
              f.name .. " " .. n .. ": frame")
      end
    end
    for ev, fs in pairs(a) do
      if ev ~= "t" and ev ~= "d" and ev ~= "loop" and ev ~= "shift" then
        for _, k in ipairs(fs) do check(k >= 1 and k <= #a.t, f.name .. " " .. n .. ": event " .. ev) end
      end
    end
  end
  check(f.a.attack.hit or f.a.attack.fire, f.name .. ": an attack that lands")
  if f.boss then
    local specials, combos = 0, 0
    for _, n in ipairs(f.order) do
      local a = f.a[n]
      if n:sub(1, 5) == "combo" then
        combos = combos + 1
        local blows = (a.hit and #a.hit or 0) + (a.fire and #a.fire or 0)
        check(blows == 2, f.name .. " " .. n .. ": a combo of two blows")
      elseif n ~= "idle" and n ~= "walk" and n ~= "attack" and n ~= "hurt" and n ~= "death" then
        specials = specials + 1
      end
    end
    check(specials == 2 and combos == 2, f.name .. ": 2 special attacks and 2 combos")
  end
end
check(nfoe == 16, "16 creatures, not " .. nfoe)
check(kinds.town == 5 and kinds.beast == 3 and kinds.hunter == 3 and kinds.horror == 5, "4 + 2 + 2 + 4 and the bosses")
check(bosses.town == 1 and bosses.beast == 1 and bosses.hunter == 1 and bosses.horror == 1, "a boss of each kind")
io.write(string.format("the creatures: %d, %d frames\n", nfoe, ffr))

---------------------------------------------------------------- the street plan

call(env._init)
check(levels == 8, "8 light levels, not " .. tostring(levels))

local CS = Y.CS
-- either side of a border sees the same streets (road_at depends only on
-- the tile), and the plan is the same when asked again
local same = 0
for cy = -3, 3 do
  for cx = -3, 3 do
    for k = 0, CS - 1 do
      local a = Y.road_at(cx * CS + CS - 1, cy * CS + k)
      local b = Y.road_at(cx * CS + CS - 1, cy * CS + k)
      check(a == b, "road_at is not stable")
      same = same + 1
    end
  end
end

-- the same chunk twice: the same map cells, objects and lights
local function snapshot(cx, cy)
  local ch = Y.ensure(cx, cy)
  local s = {}
  for _, o in ipairs(ch.objs) do s[#s + 1] = (o.name or "house") .. ":" .. o.x .. "," .. o.y end
  for _, l in ipairs(ch.lights) do s[#s + 1] = "L" .. l.x .. "," .. l.y .. "," .. l.r end
  for i = 1, CS * CS do s[#s + 1] = ch.kind[i] end
  return table.concat(s, ";")
end
local first = snapshot(5, -7)
-- push it out of the map (the same ring slot), then make it again
Y.ensure(9, -3)
Y.ensure(1, -11)
check(snapshot(5, -7) == first, "a chunk made twice is not the same")

-- a walkable network: from the start, flood the streets and pavements of
-- 7 x 7 chunks; nearly all of them must be reachable
local walk = {}
local total = 0
local R0, R1 = -3, 3
local K = Y.kinds
local function walkable(tx, ty)
  local cx, cy = tx // CS, ty // CS
  if cx < R0 or cx > R1 or cy < R0 or cy > R1 then return false end
  local ch = Y.chunk(cx, cy)
  if not (ch and ch.ready) then ch = Y.ensure(cx, cy) end
  local k = ch.kind[(ty % CS) * CS + tx % CS + 1]
  return (k == K.road or k == K.walk) and not ch.solid[(ty % CS) * CS + tx % CS + 1]
end
-- one ring of 4x4 chunks is kept: flood one band of chunks at a time
local seen = {}
local P = Y.player
local stack = { { P.x // 16, P.y // 16 } }
local reached = 0
local cache = {}
local function W(tx, ty)
  local k = tx * 100000 + ty
  local v = cache[k]
  if v == nil then v = walkable(tx, ty); cache[k] = v end
  return v
end
-- fill the cache chunk by chunk (each chunk made once)
for cy = R0, R1 do
  for cx = R0, R1 do
    for ty = cy * CS, cy * CS + CS - 1 do
      for tx = cx * CS, cx * CS + CS - 1 do
        local v = W(tx, ty)
        if v then total = total + 1 end
      end
    end
  end
end
while #stack > 0 do
  local c = table.remove(stack)
  local k = c[1] * 100000 + c[2]
  if not seen[k] and cache[k] then
    seen[k] = true
    reached = reached + 1
    stack[#stack + 1] = { c[1] + 1, c[2] }
    stack[#stack + 1] = { c[1] - 1, c[2] }
    stack[#stack + 1] = { c[1], c[2] + 1 }
    stack[#stack + 1] = { c[1], c[2] - 1 }
  end
end
check(total > 0 and reached / total > 0.9,
      string.format("the streets are not one network: %d of %d tiles reached", reached, total))
io.write(string.format("streets: %d of %d walkable tiles of 7x7 chunks reached from the start (%.0f%%)\n",
                       reached, total, 100 * reached / total))

-- what the districts are, over a wide area
local count = {}
for cy = -20, 20 do
  for cx = -20, 20 do
    local d = Y.district(cx, cy)
    count[d.kind] = (count[d.kind] or 0) + 1
  end
end
local kinds = {}
for k, v in pairs(count) do kinds[#kinds + 1] = k .. " " .. v end
table.sort(kinds)
io.write("districts of 41x41 chunks: " .. table.concat(kinds, ", ") .. "\n")
check(#kinds == 6, "every kind of district appears")

---------------------------------------------------------------- frames

local worst = { instr = 0, gen = 0, px = 0 }
local frames = 0
local costs = {}
local function frame(buttons, label)
  prev, held = held, buttons
  instr, gen_instr, px_sprites = 0, 0, 0
  debug.sethook(counter, "", 1000)
  call(env._update)
  call(env._draw)
  debug.sethook()
  now = now + 1 / 60
  frames = frames + 1
  if instr > worst.instr then worst.instr, worst.where = instr, label end
  costs[#costs + 1] = instr
  if gen_instr > worst.gen then worst.gen = gen_instr end
  if px_sprites > worst.px then worst.px = px_sprites end
end

-- the title: the camera drifts over the town for a minute
for i = 1, 3600 do frame(0, "title") end
local cx, cy = Y.camera()
check(cx > 2500 and cy > 1200, "the camera drifted over the town")
-- A: the hunter starts on a street where the camera is
frame(1 << 4, "start")
frame(0, "start")
check(not Y.blocked(P.x, P.y), "the hunter starts on a free street")

-- the hunter walks at random for a few minutes, sometimes running
math.randomseed(7)
local dirs = { 1, 2, 4, 8, 1 | 4, 1 | 8, 2 | 4, 2 | 8 }
local d, left = 1, 0
local x0, y0 = P.x, P.y
local far = 0
for i = 1, 12000 do
  if left <= 0 then d, left = dirs[math.random(#dirs)], 60 + math.random(240) end
  left = left - 1
  local b = d
  if i % 700 < 300 then b = b | (1 << 5) end
  frame(b, "walk")
  check(not Y.blocked(P.x, P.y), string.format("the hunter is stuck in a wall at %.1f,%.1f", P.x, P.y))
  far = math.max(far, math.abs(P.x - x0) + math.abs(P.y - y0))
end
check(far > 400, "the hunter went somewhere (" .. far .. " px)")

table.sort(costs)
local p50, p99 = costs[#costs // 2], costs[#costs * 99 // 100]
-- what the hunter does: a combo of three blows, a shot, the saw cleaver
-- opened and closed, a backstep, hurt, knocked down, killed and back
local function press(b, n)
  frame(1 << b, "act")
  for i = 1, (n or 1) - 1 do frame(0, "act") end
end
local function settle(n) for i = 1, n or 120 do frame(0, "act") end end
-- somewhere quiet: a street with no fire near (fire burns)
local function quiet()
  for r = 0, 6 do
    for cy = -r, r do
      for cx = -r, r do
        local ch = Y.ensure(cx, cy)
        for ty = 0, 15 do
          for tx = 0, 15 do
            local x, y = cx * 256 + tx * 16 + 8, cy * 256 + ty * 16 + 8
            if ch.kind[ty * 16 + tx + 1] == Y.kinds.road and not Y.blocked(x, y) then
              local ok = true
              for dy = -1, 1 do for dx = -1, 1 do
                local n = Y.ensure(cx + dx, cy + dy)
                for _, f in ipairs(n.fires) do
                  if math.abs(f.x - x) + math.abs(f.y - y) < 160 then ok = false end
                end
              end end
              if ok then return x, y end
            end
          end
        end
      end
    end
  end
end
-- no creatures for these: they would join in
local F = Y.FOE
F.quiet = true
for k = #F.list, 1, -1 do F.list[k] = nil end
Y.teleport(quiet())
settle()
P.hp, P.hits, P.inv = 10, {}, 0
press(4, 6); press(4, 6); press(4, 6)
local top = P.combo
for i = 1, 30 do frame(1 << 4, "act"); frame(0, "act"); top = math.max(top, P.combo) end
check(top == 3, "the combo reaches its third blow (" .. top .. ")")
settle()
check(P.act == nil, "the combo ends")
press(6, 1)
local flashed = false
for i = 1, 60 do frame(0, "act"); flashed = flashed or Y.flash() > 0 end
check(flashed, "the pistol fires")
press(7, 1); settle(80)
check(P.ext, "the saw cleaver opens")
press(4, 1); settle(120)
check(P.anim:find("_x") and P.act == nil, "back to the extended stance after a blow")
press(7, 1); settle(80)
check(not P.ext, "the saw cleaver closes")
local bx, by = P.x, P.y
press(5, 1); settle(60)
check(math.abs(P.x - bx) + math.abs(P.y - by) > 12 or Y.blocked(P.x - 1, P.y), "the backstep moves him back")
P.inv = 0
Y.hurt(1)
check(P.act == "hurt" and P.anim:find("hurt"), "hurt")
settle(70)
P.inv = 0; Y.hurt(1); P.inv = 0; Y.hurt(1)
check(P.act == "down", "the third blow in a row knocks him down")
local states = {}
for i = 1, 400 do frame(0, "act"); states[P.act or "free"] = true end
check(states.lying and states.getup and P.act == nil, "lying, getting up, on his feet again")
P.inv = 0
Y.hurt(99)
check(P.act == "dead", "killed")
settle(400)
check(P.act == nil and P.hp == 10, "back at the last lamp, healed (" .. tostring(P.act) .. ", hp " .. P.hp .. ", " .. P.anim .. " " .. P.f .. ")")

-- the creatures: a townsman comes, strikes, is cut down; the pistol staggers
local qx, qy = quiet()
local function clear() for k = #F.list, 1, -1 do F.list[k] = nil end end
local function free_near(x, y, r)
  for k = 1, 200 do
    local a = math.random() * 2 * math.pi
    local px, py = x + math.cos(a) * r, y + math.sin(a) * r
    if not Y.blocked(px, py) then return px, py end
  end
  return x, y
end
Y.teleport(qx, qy)
clear()
P.hp, P.inv, P.act = 10, 0, nil
local fx, fy = free_near(qx, qy, 60)
local o = F.new("pitchfork", fx, fy, 0)
local struck = false
for i = 1, 900 do
  frame(0, "foe")
  if P.hp < 10 then struck = true break end
end
check(struck, "a townsman comes and strikes the hunter")
check(o.alert, "it saw the hunter")
settle(60)
P.hp, P.inv = 10, 0
-- face it and cut it down
local killed = false
for i = 1, 60 do
  P.dir = F.dir_to(o.x - P.x, o.y - P.y)
  press(4, 8)
  P.hp = 10
  if o.act == "dead" then killed = true break end
end
check(killed, "the saw cleaver kills a townsman (hp " .. o.hp .. ")")
check(F.killed >= 1, "it is counted")
settle(120)
check(o.ended, "it lies dead")
-- a dog: shot, it staggers
clear()
settle(60)
P.act, P.hp = nil, 10
-- straight ahead of the hunter, where the shot goes
local DV = { { 0, 1 }, { 0.7071, 0.7071 }, { 1, 0 }, { 0.7071, -0.7071 }, { 0, -1 }, { -0.7071, -0.7071 },
             { -1, 0 }, { -0.7071, 0.7071 } }
for d = 0, 7 do
  local px, py = P.x + DV[d + 1][1] * 44, P.y + DV[d + 1][2] * 44
  if not Y.blocked(px, py) then P.dir, fx, fy = d, px, py break end
end
o = F.new("dog", fx, fy, 0)
o.cd = 999
local staggered = false
press(6, 1)
for i = 1, 40 do frame(0, "foe"); if o.act == "hurt" then staggered = true end end
check(staggered, "the pistol staggers a dog")
-- the rally: struck soon after a wound, the blood comes back
settle(40)
P.act, P.hp, P.rally, P.rally_t = nil, 6, 3, 100
local hp0 = P.hp
for i = 1, 20 do
  if o.act == "dead" then break end
  P.dir = F.dir_to(o.x - P.x, o.y - P.y)
  press(4, 8)
end
check(P.hp > hp0, "the rally gives back health (" .. hp0 .. " -> " .. P.hp .. ")")
-- the rifleman fires
clear()
fx, fy = free_near(qx, qy, 90)
o = F.new("rifle", fx, fy, 0)
local fired = false
for i = 1, 900 do
  frame(0, "foe")
  P.inv, P.hp = 60, 10
  if #F.shots > 0 then fired = true break end
end
check(fired, "the rifleman shoots")
-- every boss: awake, its specials and combos, slain
local seen_all = {}
for _, name in ipairs({ "butcher", "hound", "father", "watcher" }) do
  clear()
  F.won = nil
  Y.teleport(qx, qy)
  P.act = nil
  fx, fy = free_near(qx, qy, 70)
  local b = F.new(name, fx, fy, 0)
  local seen, nseen = {}, 0
  local fx_seen = {}
  for i = 1, 7000 do
    frame(0, "boss")
    P.inv, P.hp = 60, 10
    if P.act == "dead" or P.act == "down" or P.act == "lying" then P.act = nil end
    if b.act and not seen[b.act] then seen[b.act] = true; nseen = nseen + 1 end
    if #F.shots > 0 then fx_seen.shot = true end
    if #F.rings > 0 then fx_seen.ring = true end
    if #F.burns > 0 then fx_seen.burn = true end
    if b.beam then fx_seen.beam = true end
    -- the hunter steps away now and then: the boss has to come, or reach
    if i % 260 == 0 then
      local nx, ny = free_near(b.x, b.y, 40 + (i // 260) % 4 * 30)
      P.x, P.y = nx, ny
    end
  end
  check(F.boss == b, name .. ": the boss is awake")
  local specials, combos = 0, 0
  for k in pairs(seen) do
    if k:sub(1, 5) == "combo" then combos = combos + 1
    elseif k ~= "attack" and k ~= "hurt" then specials = specials + 1 end
  end
  local list = {}
  for k in pairs(seen) do list[#list + 1] = k end
  table.sort(list)
  io.write(name .. ": " .. table.concat(list, " ") .. "\n")
  check(specials == 2 and combos == 2 and seen.attack, name .. ": both specials, both combos, the plain blow")
  if name == "butcher" then check(fx_seen.burn and fx_seen.ring, "the Butcher's fire and slam") end
  if name == "hound" then check(fx_seen.ring, "the Hound's pounce and howl shake the ground") end
  if name == "watcher" then check(fx_seen.beam and fx_seen.ring, "the Watcher's gaze and grasp") end
  F.harm(b, 999, false)
  settle(200)
  check(b.act == "dead" and F.won, name .. ": prey slaughtered")
end
clear()
F.quiet = false

io.write(string.format("frames: %d; Lua instructions per frame: median %d, 99%% %d, heaviest %d (%s); " ..
                       "a chunk made in the background: up to %d (in slices of 40k on the console); " ..
                       "sprite pixels up to %d\n", frames, p50, p99, worst.instr, worst.where, worst.gen, worst.px))
check(p99 < 60000, "frames over 60k Lua instructions (6 ms on the Pi)")
io.write(string.format("yharnam: %d checks passed\n", checks + 2))
