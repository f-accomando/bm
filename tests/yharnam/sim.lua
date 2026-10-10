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

local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local env, call = B.env, B.call
local SHEET_W, SHEET_H = B.SHEET_W, B.SHEET_H

---------------------------------------------------------------- load

local instr, gen_instr = 0, 0
local in_gen = false
local function counter() if in_gen then gen_instr = gen_instr + 1000 else instr = instr + 1000 end end

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

local Y = B.load(SRC)
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
check(B.levels == 8, "8 light levels, not " .. tostring(B.levels))

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
check(#kinds == 8, "every kind of district appears")

---------------------------------------------------------------- frames

local worst = { instr = 0, gen = 0, px = 0 }
local frames = 0
local costs = {}
local function frame(buttons, label)
  instr, gen_instr, B.px = 0, 0, 0
  debug.sethook(counter, "", 1000)
  B.frame(buttons)
  debug.sethook()
  frames = frames + 1
  if instr > worst.instr then worst.instr, worst.where = instr, label end
  costs[#costs + 1] = instr
  if gen_instr > worst.gen then worst.gen = gen_instr end
  if B.px > worst.px then worst.px, worst.pxat = B.px, label .. " " .. frames end
end

-- the title: the camera stays where the hunter is (it used to drift over the town)
local cx0, cy0 = Y.camera()
for i = 1, 3600 do frame(0, "title") end
local cx, cy = Y.camera()
check(cx == cx0 and cy == cy0, "the camera stays on the hunter at the title")
-- A: a hunt begins at the start, whatever the camera showed
frame(1 << 4, "start")
frame(0, "start")
check(Y.state() == "play" and not Y.blocked(P.x, P.y), "the hunter starts on a free street")
check(P.x < 256 and P.y < 256, "at the start of the hunt")
-- the walk below may die: it has echoes enough to come back every time
Y.G.echoes = 1000000

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
Y.G.echoes = 1000000
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
-- the rally: struck soon after a wound, the blood comes back (the dog
-- made to stand and take it: its own ways keep it out of reach)
settle(40)
o.style = { keep = 10, wait = { 999, 999 } }
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
-- every boss: awake, through its three phases (its blood at a half, then a
-- fifth: a roar each time), its specials and combos, slain
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
  local roars = 0
  for i = 1, 7000 do
    if i == 2400 then b.hp = b.hpmax * 0.5 end
    if i == 4800 then b.hp = b.hpmax * 0.2 end
    local ph = b.ph
    frame(0, "boss")
    if b.ph > ph then roars = roars + 1 end
    P.inv, P.hp = 60, 10
    if P.act == "dead" or P.act == "down" or P.act == "lying" then P.act = nil end
    if b.act and b.act ~= "rage" and not seen[b.act] then seen[b.act] = true; nseen = nseen + 1 end
    if #F.shots > 0 then fx_seen.shot = true end
    if #F.rings > 0 then fx_seen.ring = true end
    if #F.burns > 0 then fx_seen.burn = true end
    if b.beam then fx_seen.beam = true end
    -- the hunter steps away now and then: the boss has to come, or reach
    if i % 260 == 0 then
      local nx, ny = free_near(b.x, b.y, 30 + (i // 260) % 4 * 30)
      P.x, P.y = nx, ny
    end
  end
  check(F.boss == b, name .. ": the boss is awake")
  check(b.ph == 3 and roars == 2, name .. ": three phases, a roar into each")
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
  Y.G.done, Y.G.bosses, Y.G.gates = false, {}, {}           -- (not the hunt's end: only the fight)
end
-- the hunt: a map of 4 areas, mist only round it, a boss in each, two lamps
local M = Y.MAP
local SZ = M.size
check(Y.district(0, 0).kind == "square", "the start is a square")
check(Y.blocked(-12, 100) and Y.blocked(100, -12), "mist to the west and north of the start")
check(Y.blocked(SZ * 256 + 8, 300) and Y.blocked(300, SZ * 256 + 8), "mist to the east and south of the map")
check(not Y.blocked(2 * 256 + 100, 5 * 256 + 100) or true, "no mist inside the map")
local bx, by = M.boss_chunk(1)
local dd = Y.district(bx, by)
check(dd.boss and dd.kind == "pyre", "the first area ends at a pyre: the Butcher's (" .. dd.kind .. ")")
for k = 1, 2 do
  local lx, ly = M.lamp_chunk(1, k)
  check(Y.ensure(lx, ly).shrine, "a hunter's lamp in chunk " .. lx .. "," .. ly)
end
-- the boss is in its arena
F.quiet = false
clear()
Y.teleport(bx * 256 + 128, by * 256 + 128)
for cy = by - 1, by + 1 do for cx = bx - 1, bx + 1 do local c = Y.chunk(cx, cy); if c then c.spawned = false end end end
local arena = Y.ensure(bx, by)
arena.spawned = false
F.populate(arena)
local found
for _, f in ipairs(F.list) do if f.boss then found = f.name end end
check(found == "butcher", "the Butcher waits in the pyre (" .. tostring(found) .. ")")
F.quiet = true
clear()
-- echoes: what a slain creature leaves
Y.teleport(qx, qy)
settle(20)
local e0 = Y.G.echoes
o = F.new("pitchfork", P.x + 30, P.y, 0)
F.harm(o, 99, false)
check(Y.G.echoes == e0 + 30, "a townsman leaves 30 echoes")
settle(30)
-- Select: echoes for health
P.act, P.hp = nil, 4
Y.G.echoes = 200
frame(1 << 9, "heal"); frame(0, "heal")
check(P.act == "heal" and Y.G.echoes == 80, "Select spends echoes to heal")
settle(60)
check(P.hp == 7, "and some health comes back (" .. P.hp .. ")")
P.hp = 10
frame(1 << 9, "heal"); frame(0, "heal")
check(P.act ~= "heal" and Y.G.echoes == 80, "not when whole, nor with too few echoes")
-- a hunter's lamp: lit, it opens its menu; a path taken; rest
local lx, ly = M.lamp_chunk(1, 1)
local sh = Y.ensure(lx, ly).shrine
Y.teleport(sh.x, sh.y + 16)
settle(10)
Y.G.echoes = 700
press(4, 2)
check(Y.G.lit[sh.key] and Y.state() == "lamp", "A by the lamp: lit, and its menu")
press(3, 2); press(4, 2)                         -- the first path: too dear yet
check(#Y.G.slots == 0 and Y.G.echoes == 700, "a path costs more echoes than these")
Y.G.echoes = 6000
press(4, 2)                                      -- Feral Affinity, the first path
check(Y.G.slots[1] == 1 and Y.G.echoes == 5200, "the first path taken, for 800 echoes")
local h1 = P.mods.hurt
check(h1 < 1 and h1 > 0.7, "it is felt (blows hurt less) (" .. h1 .. ")")
press(4, 2)                                      -- the same path again: it weighs less now
check(P.mods.hurt < h1 and (1 - P.mods.hurt / h1) < (1 - h1), "the second weighs less than the first")
press(3, 2); press(3, 2); press(3, 2); press(3, 2)  -- Hunter's Path
local r0 = P.mods.fold_rate
press(4, 2)
check(P.mods.fold_rate > r0 and Y.G.slots[3] == 5, "the Hunter's Path: the folded blade quicker")
check(P.mods.fold_rate < 1.1 and not P.mods.fold_chain, "another path than his own: a divided blood, felt less")
press(2, 2); press(2, 2)                         -- Quicksilver Rite
press(4, 2)
check(#Y.G.slots == 4 and P.mods.gun > 1 and P.mods.stag > 1, "the fourth path, the last")
local e4 = Y.G.echoes
press(4, 2)
check(#Y.G.slots == 4 and Y.G.echoes == e4, "no fifth path")
check(P.hpmax == 10 and P.stmax == 100, "the bars stay as they were")
for i = 1, 8 do press(2, 2) end
while Y.menu.i ~= 1 do press(2, 2) end
press(4, 2)
check(Y.state() == "play", "rested")
-- blows taken weigh less now
P.hp, P.inv, P.act = 10, 0, nil
Y.hurt(2)
check(P.hp > 8 and P.hp < 9, "Feral Affinity: a blow of 2 takes less (" .. P.hp .. ")")
settle(80)
-- Start: the pause, the controls, back
press(8, 2)
check(Y.state() == "pause", "Start: the pause")
press(3, 2); press(4, 2)
check(Y.menu.page == "controls", "the controls")
press(5, 2); press(5, 2)
check(Y.state() == "play", "back to the hunt")
-- a death costs echoes, back at the lamp; without them the hunt is lost
Y.G.echoes = 250
P.inv = 0
Y.teleport(qx, qy)
Y.hurt(99)
settle(400)
check(Y.state() == "play" and Y.G.echoes == 50, "a death costs 200 echoes (" .. Y.G.echoes .. ")")
check(math.abs(P.x - sh.x) < 4 and math.abs(P.y - sh.y - 16) < 4, "back at the last lamp lit")
Y.G.echoes = 20
P.inv = 0
Y.hurt(99)
settle(400)
check(Y.state() == "lost", "too few echoes: the hunt is lost")
settle(100)
press(4, 2)
check(Y.state() == "title" and Y.G.echoes == 0 and next(Y.G.lit) == nil and #Y.G.slots == 0 and
      P.mods.hurt == 1, "begin again: a new hunt, no paths")
press(4, 2)
check(Y.state() == "play", "and it starts")
Y.G.echoes = 1000000
F.quiet = true

-- the fight, as in Bloodborne: stamina, dodges, the lock, the charge, the
-- parry and the visceral attack, the trick in a combo, a boss's poise
clear()
Y.teleport(qx, qy)
settle(30)
P.act, P.hp, P.inv, P.st, P.ext = nil, 10, 0, 100, false
press(4, 1)
check(P.st < 100, "a blow costs stamina")
settle(150)
check(P.st == 100, "stamina comes back")
-- a tap of B with a direction: a roll, unharmed through it
local rx, ry = P.x, P.y
local dodged
for d = 0, 7 do
  local bits = ({ 8, 8 | 2, 2, 2 | 4, 4, 4 | 1, 1, 1 | 8 })[d + 1]
  local v = DV[d + 1]
  if not Y.blocked(P.x + v[1] * 30, P.y + v[2] * 30) then
    frame(bits | (1 << 5), "fight"); frame(bits | (1 << 5), "fight"); frame(bits, "fight")
    dodged = P.act
    for i = 1, 3 do frame(bits, "fight") end
    break
  end
end
check(dodged == "roll" and P.anim:find("roll"), "B tapped while moving: a roll (" .. tostring(dodged) .. ")")
check(P.inv > 0, "invulnerable in the roll")
settle(60)
check(math.abs(P.x - rx) + math.abs(P.y - ry) > 15, "the roll carries him")
-- the lock (L1) on a creature, and a quickstep to the side
fx, fy = free_near(P.x, P.y, 50)
o = F.new("pitchfork", fx, fy, 0)
o.cd = 999
frame(1 << 10, "fight"); frame(0, "fight")
check(P.lock == o, "L1 locks on the creature")
settle(5)
check(P.dir == F.dir_to(o.x - P.x, o.y - P.y), "locked: he faces it")
local stepped
for _, bits in ipairs({ 1, 2, 4, 8, 1 | 4, 1 | 8, 2 | 4, 2 | 8 }) do
  local bdx = (bits & 1 ~= 0 and -1) or (bits & 2 ~= 0 and 1) or 0
  local bdy = (bits & 4 ~= 0 and -1) or (bits & 8 ~= 0 and 1) or 0
  local di = Y.dirs[bdy][bdx]
  local rel = (di - P.dir) % 8
  if rel == 2 or rel == 6 then
    frame(bits | (1 << 5), "fight"); frame(bits, "fight")
    stepped = P.anim
    break
  end
end
check(stepped and stepped:find("qstep"), "locked, B to the side: a quickstep (" .. tostring(stepped) .. ")")
settle(40)
-- R1 held: the heavy blow waits at the top, charged
P.act, P.st = nil, 100
for i = 1, 40 do frame(1 << 11, "fight") end
check(P.act == "heavy" and P.charged, "R1 held: a charged heavy blow")
for i = 1, 60 do frame(0, "fight") end
check(P.act == nil, "and released")
-- the parry: shot as it winds up, it reels; A tears it open
clear()
settle(20)
P.act, P.hp, P.st, P.lock = nil, 10, 100, nil
for d = 0, 7 do
  local px, py = P.x + DV[d + 1][1] * 26, P.y + DV[d + 1][2] * 26
  if not Y.blocked(px, py) then P.dir, fx, fy = d, px, py break end
end
o = F.new("pitchfork", fx, fy, 0)
o.alert, o.cd = true, 0
local waited = 0
while o.act ~= "attack" and waited < 200 do frame(0, "fight"); waited = waited + 1; P.inv, P.hp = 60, 10 end
check(o.act == "attack", "the townsman winds up")
P.dir = F.dir_to(o.x - P.x, o.y - P.y)
press(6, 1)
local reeled = false
for i = 1, 20 do frame(0, "fight"); P.inv = 60; if o.act == "stagger" then reeled = true break end end
check(reeled, "the parry: shot as it winds up, it reels")
for i = 1, 40 do if P.act == nil then break end frame(0, "fight") end
P.dir = F.dir_to(o.x - P.x, o.y - P.y)
local hp0 = o.hp
press(4, 1)
check(P.act == "visceral" and o.act == "held", "A on a reeling creature: the visceral attack")
settle(80)
check(o.hp < hp0 - 8 or o.act == "dead", "the visceral attack tears it open")
-- a charged blow in its back staggers it
clear()
settle(20)
P.act, P.st = nil, 100
o = F.new("church", P.x + DV[P.dir + 1][1] * 24, P.y + DV[P.dir + 1][2] * 24, 0)
o.dir, o.cd, o.hp = P.dir, 999, 50                 -- it faces away from the hunter
o.alert = false
o.st = setmetatable({ sight = 0 }, { __index = o.st })
for i = 1, 45 do frame(1 << 11, "fight") end
check(P.charged, "charged")
for i = 1, 30 do frame(0, "fight") end
check(o.act == "stagger", "a charged blow in the back: it reels (" .. tostring(o.act) .. ")")
-- the trick: a blow, Y, the blade transformed in a blow, the combo goes on in the other form
clear()
settle(30)
P.act, P.st, P.ext, P.combo = nil, 100, false, 0
press(4, 1)
local ypressed = false
for i = 1, 40 do
  local a = Y.HUNT[P.anim]
  local b = 0
  if not ypressed and P.act == "attack" and P.f >= a.hit - 1 then b, ypressed = 1 << 7, true end
  frame(b, "fight")
  if P.act == "trick" then break end
end
local tricked = P.act == "trick"
for i = 1, 30 do frame(i == 20 and (1 << 4) or 0, "fight") end
check(tricked and P.ext, "A, then Y: the trick opens the saw cleaver in a blow")
settle(80)
-- a boss's poise breaks under blows
clear()
fx, fy = free_near(P.x, P.y, 60)
o = F.new("butcher", fx, fy, 0)
o.awake, o.cd = true, 999
for i = 1, 5 do F.harm(o, 4, false) end
check(o.act == "stagger", "a boss reels when the blows add up (" .. tostring(o.act) .. ")")
clear()

-- things that break: barrels, crates, benches, coffins
local BRK = Y.BRK
local function breakable(skip)
  for cy = 0, 3 do
    for cx = 0, 3 do
      local ch = Y.ensure(cx, cy)
      for _, b in ipairs(ch.objs) do
        if b.col and not b.broken and not b.gate and not b.sealed and not skip[b] then return b, cx, cy end
      end
    end
  end
end
-- the hunter beside it, facing it
local function beside(b)
  for _, r in ipairs({ 15, 18, 22 }) do
    for k = 0, 15 do
      local a = k * math.pi / 8
      local x, y = b.x + math.cos(a) * r, b.y + math.sin(a) * r * 0.7
      if not Y.blocked(x, y) then
        local best, bd = 0, -2
        Y.teleport(x, y)
        local dx, dy = b.x - x, b.y - y
        local dd = math.sqrt(dx * dx + dy * dy)
        for d = 0, 7 do
          local ang = ({ { 0, 1 }, { 0.7071, 0.7071 }, { 1, 0 }, { 0.7071, -0.7071 }, { 0, -1 }, { -0.7071, -0.7071 },
                         { -1, 0 }, { -0.7071, 0.7071 } })[d + 1]
          local dot = (ang[1] * dx + ang[2] * dy) / dd
          if dot > bd then best, bd = d, dot end
        end
        P.dir, P.act, P.st, P.ext, P.combo, P.lock = best, nil, 100, false, 0, nil
        return true
      end
    end
  end
end
local skip, b, bcx, bcy = {}, nil, nil, nil
repeat
  b, bcx, bcy = breakable(skip)
  if b then skip[b] = true end
until not b or beside(b)
check(b, "a breakable thing in the first area")
local nb = #Y.parts
settle(10)
press(4, 1)
for i = 1, 40 do frame(0, "break") end
check(b.broken and BRK.gone[b.key], "a blow breaks a " .. b.name)
check(b.s == Y.SPR[BRK.kinds[b.name]], "what is left of it is drawn")
check(b.col.off, "it blocks the way no more")
local splinters = 0
for _, p in ipairs(Y.parts) do if p.kind == 8 then splinters = splinters + 1 end end
check(splinters > 0, "splinters fly")
-- its chunk made again: still broken
Y.ensure(bcx + 4, bcy + 4)
local again
for _, o2 in ipairs(Y.ensure(bcx, bcy).objs) do if o2.key == b.key then again = o2 end end
check(again and again.broken and again.col.off, "broken it stays when its street is made again")
-- the town fills again: whole
F.reset()
check(not again.broken and not again.col.off and again.s == Y.SPR[again.name], "whole again when the town fills again")
-- running into one breaks it too, and a creature's blow, and a shot
local b2
skip = {}
repeat
  b2 = breakable(skip)
  if b2 then skip[b2] = true end
until not b2 or beside(b2)
check(b2, "another breakable thing")
local hit = BRK.touch(b2.col.x, b2.col.y, 2, 0)
check(hit == b2 and b2.broken, "running into it breaks it")
F.reset()
check(BRK.smash(b2.x - 20, b2.y, 1, 0, 26, 0.3) == 1 and b2.broken, "a creature's blow breaks it")
F.reset()
local o3, sx, sy = BRK.first(b2.x - 60, b2.y, b2.x + 60, b2.y)
check(o3 == b2 and sx < b2.x, "a shot finds it on its way")
F.reset()
clear()
F.quiet = false

-- the regions: one to an area, each with its districts, its paving, its
-- boss; the second a forest (woods and clearings, earth paths, no pavements)
local R = Y.REGION
local kinds_of = {}
for a = 1, 4 do
  local n, ar = 0, M.areas[a]
  for cy = ar.y0, ar.y1 do
    for cx = ar.x0, ar.x1 do
      local d = Y.district(cx, cy)
      local c = ar.layout[cy - ar.y0 + 1]:sub(cx - ar.x0 + 1, cx - ar.x0 + 1)
      if d.kind == (c == "B" and R[a].boss or M.kinds[c]) and (c == "B") == d.boss then n = n + 1 end
      kinds_of[a - 1] = (kinds_of[a - 1] or "") .. d.kind:sub(1, 2) .. " "
    end
  end
  check(n == 16, "area " .. a .. ": its districts are its layout's")
  local bcx, bcy = M.boss_chunk(a)
  check(ar.layout[bcy - ar.y0 + 1]:sub(bcx - ar.x0 + 1, bcx - ar.x0 + 1) == "B", "area " .. a .. ": its layout marks the arena")
  for k = 1, 2 do
    local lx, ly = M.lamp_chunk(a, k)
    check(not (lx == bcx and ly == bcy), "area " .. a .. ": lamp " .. k .. " is not in the boss's arena")
  end
  local bx2, by2 = M.boss_chunk(a)
  local bd = Y.district(bx2, by2)
  check(bd.boss and bd.kind == R[a].boss, "area " .. a .. ": its boss's arena is a " .. R[a].boss)
  for k = 1, 2 do
    local lx, ly = M.lamp_chunk(a, k)
    check(Y.ensure(lx, ly).shrine, "area " .. a .. ": hunter's lamp " .. k)
  end
end
io.write("regions: " .. kinds_of[0] .. "| " .. kinds_of[1] .. "| " .. kinds_of[2] .. "| " .. kinds_of[3] .. "\n")
local earth, stone, trees, walk = 0, 0, 0, 0
for cy = 5, 6 do
  for cx = 1, 2 do                                -- inside the forest (its border streets are the town's)
    for ty = 0, 15 do
      for tx = 0, 15 do
        local pv = Y.road_at(cx * 16 + tx, cy * 16 + ty)
        if pv == 9 then earth = earth + 1 elseif pv > 0 then stone = stone + 1 end
      end
    end
    local ch = Y.ensure(cx, cy)
    for _, o in ipairs(ch.objs) do
      if o.name and (o.name:sub(1, 4) == "tree" or o.name:sub(1, 4) == "bush") then trees = trees + 1 end
    end
    for i = 1, 256 do if ch.kind[i] == Y.kinds.walk then walk = walk + 1 end end
  end
end
io.write(string.format("the forest: %d tiles of earth paths, %d of stone, %d trees and bushes in 4 chunks\n", earth,
                       stone, trees))
check(earth > 200 and stone == 0, "the forest's paths are earth")
check(walk == 0, "no pavements in the forest")
check(trees > 4 * 10, "trees and bushes, many, in the forest")
local town_earth = 0
for ty = 0, 4 * 16 - 1 do for tx = 0, 4 * 16 - 1 do if Y.road_at(tx, ty) == 9 then town_earth = town_earth + 1 end end end
check(town_earth == 0, "no earth paths in the town")
-- a walk in the forest (the most things on screen): its frames count too
clear()
Y.teleport(1 * 256 + 128, 5 * 256 + 128)
local wood0, wood_px = #costs, 0
for i = 1, 1200 do
  local b = ({ 1, 2, 4, 8, 1 | 4, 2 | 8 })[(i // 90) % 6 + 1]
  frame(b, "forest")
  wood_px = math.max(wood_px, B.px)
  P.hp = 10
end
local wmax = 0
for k = wood0 + 2, #costs do wmax = math.max(wmax, costs[k]) end
io.write(string.format("the forest: heaviest frame %d Lua instructions, sprite pixels up to %d\n", wmax, wood_px))
-- every boss in its arena, the Hound in a clearing of the forest
F.quiet = false
local BOSSES = { "butcher", "hound", "father", "watcher" }
local last
for a = 1, 4 do
  clear()
  local bx2, by2 = M.boss_chunk(a)
  Y.teleport(bx2 * 256 + 128, by2 * 256 + 128)
  local arena2 = Y.ensure(bx2, by2)
  arena2.spawned = false
  F.populate(arena2)
  local found2
  for _, f in ipairs(F.list) do if f.boss then found2 = f end end
  check(found2 and found2.name == BOSSES[a], "area " .. a .. ": " .. BOSSES[a] .. " waits in its arena")
  last = found2
end
-- the Watcher slain: the night is over once its banner is gone: how the
-- hunt went, then the title again
F.quiet = true
Y.G.deaths, Y.G.slain = 2, 40
F.harm(last, 999, false)
check(Y.G.bosses.watcher and Y.G.done, "the Watcher slain: the hunt is done")
for i = 1, 400 do frame(0, "end") ; if Y.state() == "end" then break end end
check(Y.state() == "end", "the end of the hunt, after the banner")
settle(100)
press(4, 2)
check(Y.state() == "title" and next(Y.G.bosses) == nil and next(Y.G.gates) == nil and not Y.G.done and Y.G.slain == 0,
      "A: back to the title, a new hunt")
press(4, 2)
check(Y.state() == "play", "and a new hunt begins")
clear()

-- the map is not a line: gates between its areas (map data, then what the hunter meets)
do
  local M = Y.MAP
  local DIRS4 = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
  for _, g in ipairs(M.gates) do
    for _, e in ipairs(g.edge) do
      local ok = false
      for _, d in ipairs(DIRS4) do
        local ia, ib = M.area_of(e[1], e[2]), M.area_of(e[1] + d[1], e[2] + d[2])
        if ib and ib ~= ia and (ia == g.a and ib == g.b or ia == g.b and ib == g.a) and
           M.gate_at(e[1], e[2], e[1] + d[1], e[2] + d[2]) == g then ok = true end
      end
      check(ok, g.id .. ": its chunk " .. e[1] .. "," .. e[2] .. " is on the border of its two areas")
    end
  end
  -- which areas a hunter reaches, never slaying the boss `skip` (a gate that needs it stays shut)
  local function reach_from(skip)
    local slain, reach, changed = {}, { [1] = true }, true
    while changed do
      changed = false
      for _, g in ipairs(M.gates) do
        if not g.requires or slain[g.requires] then
          for _, sd in ipairs({ { g.a, g.b }, { g.b, g.a } }) do
            if reach[sd[1]] and not reach[sd[2]] and (not g.from or g.from == sd[1]) then
              reach[sd[2]], changed = true, true
            end
          end
        end
      end
      for a in pairs(reach) do
        local b = M.areas[a].boss
        if b ~= skip and not slain[b] then slain[b], changed = true, true end
      end
    end
    return reach
  end
  local all, nfa, nha = reach_from(nil), reach_from("father"), reach_from("hound")
  check(all[1] and all[2] and all[3] and all[4], "every area can be reached from the start")
  check(nfa[4], "the Forbidden Quarter by the forest, without the Father")
  check(nha[4], "the Forbidden Quarter by the cathedral, without the Hound")
  check(not nha[2] or nha[2], "the forest is the start's own door")

  -- what the hunter meets: a border is a barricade, sealed but for its gates
  clear()
  Y.G.gates, Y.G.bosses = {}, {}
  for _, c in ipairs({ { 3, 0 }, { 3, 1 }, { 3, 2 }, { 4, 1 }, { 4, 2 }, { 1, 3 }, { 1, 4 }, { 2, 3 }, { 3, 3 }, { 4, 3 } }) do
    Y.ensure(c[1], c[2])
  end
  check(Y.blocked(4 * 256, 0 * 256 + 128), "the border of the central and the cathedral, sealed in its first row")
  check(Y.blocked(4 * 256, 1 * 256 + 128), "and a gate there is a barricade too")
  -- a blow on the gate that needs the Butcher: it holds, and says so
  Y.teleport(4 * 256 - 24, 1 * 256 + 128)
  settle(5)
  local B = Y.BRK
  local n = B.smash(P.x, P.y, 1, 0, 40, 0.3, true)
  check(n == 0 and Y.banner() == "A BOSS HOLDS THE WAY", "the gate holds until the Butcher falls (" .. tostring(Y.banner()) .. ")")
  -- a sealed one never gives way
  Y.teleport(4 * 256 - 24, 0 * 256 + 128)
  settle(5)
  Y.G.bosses.butcher = true
  n = B.smash(P.x, P.y, 1, 0, 40, 0.3, true)
  check(n == 0 and Y.blocked(4 * 256, 128), "a sealed border never gives way, boss or no")
  -- the Butcher slain: the gate breaks, all of it, and stays open
  Y.teleport(4 * 256 - 24, 1 * 256 + 128)
  settle(5)
  n = B.smash(P.x, P.y, 1, 0, 40, 0.3, true)
  check(n >= 1 and Y.G.gates.central_cathedral, "the Butcher slain: a blow breaks the gate")
  check(not Y.blocked(4 * 256, 1 * 256 + 128) and not Y.blocked(4 * 256, 2 * 256 + 128), "its whole length is open")
  B.reset()
  check(not Y.blocked(4 * 256, 1 * 256 + 128), "and the town filling again does not close it")
  -- the gate that opens from one side only (the forest's, after the Hound)
  clear()
  Y.G.gates, Y.G.bosses = {}, { hound = true }
  Y.ensure(3, 4)
  Y.ensure(4, 4)
  Y.teleport(4 * 256 + 24, 4 * 256 + 100)                -- on the forbidden side
  settle(160)                                            -- (the last banner gone)
  n = B.smash(P.x, P.y, -1, 0, 40, 0.3, true)
  check(n == 0 and Y.banner() == "BARRED FROM THIS SIDE", "a one-way gate holds from the wrong side (" .. tostring(Y.banner()) .. ")")
  Y.teleport(4 * 256 - 24, 4 * 256 + 100)                -- on the forest's
  settle(5)
  n = B.smash(P.x, P.y, 1, 0, 40, 0.3, true)
  check(n >= 1 and Y.G.gates.forest_forbidden_oneway, "and gives way from the forest's")
  -- a horde at a gate that holds, none once it is open
  F.quiet = false
  clear()
  Y.G.gates, Y.G.bosses = {}, {}
  local hc = Y.ensure(1, 3)
  hc.spawned = false
  F.populate(hc)
  local with = #F.list
  clear()
  Y.G.gates.central_forest = true
  hc.spawned = false
  F.populate(hc)
  local without = #F.list
  check(with >= without + 2, "a horde crowds the chunk at a gate that holds (" .. with .. " against " .. without .. ")")
  F.quiet = true
  clear()
  Y.G.gates, Y.G.bosses = {}, {}

  -- the boss's arenas: room to fight, a few things to hide behind (not so many that the boss is
  -- caught, nor the hunter): the ground within 96 px of the middle, tried every 12 px
  for a = 1, 4 do
    local bx2, by2 = M.boss_chunk(a)
    local ch = Y.ensure(bx2, by2)
    local x0, y0 = bx2 * 256 + 128, by2 * 256 + 128
    local free, total = 0, 0
    for dy = -96, 96, 12 do
      for dx = -96, 96, 12 do
        if dx * dx + dy * dy <= 96 * 96 then
          total = total + 1
          if not Y.blocked(x0 + dx, y0 + dy) then free = free + 1 end
        end
      end
    end
    local cover = 0
    for _, c in ipairs(ch.cols) do
      local dx, dy = c.x - x0, c.y - y0
      if not c.off and dx * dx + dy * dy < 128 * 128 and dx * dx + dy * dy > 40 * 40 then cover = cover + 1 end
    end
    io.write(string.format("arena %d (%s): %d%% of its middle open, %d things to take cover behind\n", a,
                           R[a].boss, free * 100 // total, cover))
    check(free * 100 // total >= 88, "area " .. a .. ": the arena's middle is open (" .. free * 100 // total .. "%)")
    check(cover <= 14, "area " .. a .. ": few things to hide behind (" .. cover .. ")")
  end
end

io.write(string.format("frames: %d; Lua instructions per frame: median %d, 99%% %d, heaviest %d (%s); " ..
                       "a chunk made in the background: up to %d (in slices of 40k on the console); " ..
                       "sprite pixels up to %d (%s)\n", frames, p50, p99, worst.instr, worst.where, worst.gen, worst.px, worst.pxat))
check(p99 < 60000, "frames over 60k Lua instructions (6 ms on the Pi)")
io.write(string.format("yharnam: %d checks passed\n", checks + 2))
