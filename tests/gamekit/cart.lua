-- The gameplay kit (lib.world, lib.states, lib.input, lib.tween, lib.after,
-- lib.best) used by one small game, "gate": a ship fires at drones that fly
-- to it. The game holds no entity loop, no collision loop, no state switch
-- and no timer of its own: the kit does it. Run by bmhost with
-- tests/gamekit/input.txt (make test-gamekit), and in QEMU.

local lib = require "bmlib"
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("gamekit: FAIL " .. what) end
end

local W = SCREEN_W or 640
local H = SCREEN_H or 360
local score, lives_lost = 0, 0
local world, S

-- the kinds: what each entity is and does
local kinds = {
  ship = { w = 16, h = 12, hp = 3, team = 1, layer = 2, cool = 0,
    update = function(e, w)
      local dx = (btn("right") and 1 or 0) - (btn("left") and 1 or 0)
      e.x = lib.clamp(e.x + dx * 3, 0, W - e.w)
      if e.cool > 0 then e.cool = e.cool - 1 end
      if btn("act1") and e.cool == 0 then
        e.cool = 8
        w:spawn("shot", { x = e.x + e.w / 2 - 2, y = e.y - 6 })
      end
    end,
    hurt = function(e) e.invuln = 60; lives_lost = lives_lost + 1 end,
    draw = function(e) rect(e.x, e.y, e.w, e.h, (e.invuln or 0) % 8 < 4 and 0x40C0FF or 0xFFFFFF) end,
    die = function() S:go("over", false) end },
  shot = { w = 4, h = 8, team = 1, dmg = 1, vy = -6, ttl = 90, layer = 1,
    draw = function(e) rect(e.x, e.y, e.w, e.h, 0xFFFF00) end },
  drone = { w = 16, h = 16, hp = 2, team = 2, dmg = 1, layer = 1,
    update = function(e, w)
      local s = w:find("ship")
      if s then e.vx = lib.clamp(s.x - e.x, -1.5, 1.5) end
    end,
    die = function(e, w)
      score = score + 10
      w:spawn("spark", { x = e.x, y = e.y })
    end,
    draw = function(e) rect(e.x, e.y, e.w, e.h, e.hp > 1 and 0xFF4040 or 0xFF9090) end },
  spark = { ttl = 20, layer = 3,
    draw = function(e) rect(e.x, e.y, 6, 6, 0xFFFFFF) end },
}

-- a wave is data; the ship is loaded the same way
local level = {
  { "ship", W / 2 - 8, H - 30 },
  { "drone", 40, 20, { vy = 0.5 } },
  { "drone", W - 60, 40, { vy = 0.5 } },
  { kind = "drone", x = W / 2, y = 0, vy = 0.5 },
}

S = lib.states({
  title = {
    enter = function(s) s.fade = { a = 0 }; lib.tween(s.fade, { a = 255 }, 0.5) end,
    update = function(s) if btnp("act1") then S:go("play") end end,
    draw = function(s)
      lib.printc("GATE", 100, 0xFFFFFF)
      lib.printc("PRESS A", 140, 0x808080)
    end },
  play = {
    enter = function()
      world = lib.world(kinds)
      world:load(level)
      score, lives_lost = 0, 0
    end,
    update = function(s)
      world:update()
      world:fight()
      if world:count("drone") == 0 and not s.won then
        s.won = true
        lib.after(0.5, function() S:go("over", true) end)
      end
    end,
    draw = function() world:draw() end },
  over = {
    enter = function(s, won)
      s.won = won
      s.record = lib.best("gatebest", score)
      if world then check(won == true, "the wave is cleared") end
    end,
    draw = function(s) lib.printc(s.won and "CLEAR" or "LOST", 120, 0xFFFFFF) end },
}, "title")

-- the checks of the kit itself (no cartridge loop in them)
local function kit_checks()
  local log_ = {}
  local w = lib.world({
    a = { w = 10, h = 10, hp = 2, team = 1, die = function(e) log_[#log_ + 1] = "a" end },
    b = { w = 10, h = 10, dmg = 1, team = 2, ttl = 3, pierce = true, layer = 1,
          draw = function(e) log_[#log_ + 1] = "b" end },
    c = { vx = 2, update = function(e) e.n = (e.n or 0) + 1 end, layer = 0,
          draw = function(e) log_[#log_ + 1] = "c" end },
  })
  local a = w:spawn("a", { x = 0, y = 0 })
  local b = w:spawn("b", { x = 5, y = 5 })
  local c = w:spawn("c")
  check(a.kind == "a" and a.w == 10 and rawget(a, "w") == nil, "an entity reads its kind, a spawn copies nothing")
  check(w:count("a") == 1 and w:find("b") == b and w:find("zzz") == nil, "find and count")
  w:draw()
  check(table.concat(log_) == "cb", "draw: the lower layer first")
  check(w:fight() == 1 and a.hp == 1 and not b.dead, "fight: a pierce hit hurts once, the attacker stays")
  w:update()
  check(c.x == 2 and c.n == 1 and c.age == 1, "update: moves, ages, runs the kind's update")
  check(w:fight() == 1 and a.hp == 0 and a.dead, "fight: a hit at hp 1 ...")
  w:update()
  check(a.dead and #w.list == 2 and table.concat(log_, "", 3) == "a", "... kills, and die runs once and the entity leaves")
  w:update(); w:update()
  check(w:count("b") == 0 and #w.list == 1, "ttl: the entity leaves when its time is over")
  check(not pcall(w.spawn, w, "nope"), "an unknown kind is an error")
  local h = w:spawn("a", { hp = 5 })
  check(w:hurt(h, 1) and h.hp == 4, "hurt")
  h.invuln = 2
  check(not w:hurt(h, 1) and h.hp == 4, "invuln: safe while it counts down")
  w:clear()
  check(#w.list == 0, "clear")
end

local f = 0
function _init()
  lib.input()
  kit_checks()
  log(string.format("gamekit: kit %d/%d ok", checks - fails, checks))   -- the QEMU test reads it
end

function _update()
  f = f + 1
  lib.update()
  S:update()
  if S.name == "over" and S:top().t > 0.2 then
    check(score == 30, "three drones shot: 30 points")
    check(lives_lost <= 2, "the ship lives")
    check(lib.store("gatebest") == 30, "the record is kept")
    log(string.format("gamekit: %d/%d checks passed", checks - fails, checks))
    quit()
  end
  if f > 1500 then
    log(string.format("gamekit: TIMEOUT after %d checks", checks))
    quit()
  end
end

function _draw()
  cls(0x101418)
  S:draw()
  print("game kit", 8, 192, 0xFFFFFF)   -- on the 16 px rows: the QEMU test reads it
end
