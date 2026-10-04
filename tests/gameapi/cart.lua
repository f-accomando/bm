-- The map's layers and the flags of the tiles (R11), the named zones of the
-- sheet, and bmlib (R10, require "bmlib"): run by bmhost with
-- tests/gameapi/input.txt (make test-gameapi). The cartridge is packed by
-- mkbm.py with tests/gameapi/: map.csv, map_front.csv (layer "front"),
-- flags.csv, sprites.txt and the sheet of mksheet.py.

local lib = require "bmlib"
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("gameapi: FAIL " .. what) end
end
local function near(a, b, e) return math.abs(a - b) <= (e or 1e-6) end
local function same(a, b)
  if #a ~= #b then return false end
  for i = 1, #a do if a[i] ~= b[i] then return false end end
  return true
end

local RED, GREEN, BLUE, YELLOW, CYAN, WHITE, MAGENTA = 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF,
                                                        0xFFFFFF, 0xFF00FF

---------------------------------------------------------------- the map (R11)

local function map_api()
  local w, h, n = msize()
  check(w == 40 and h == 23 and n == 2, "msize(): 40x23, 2 layers")
  check(same(mlayers(), { "main", "front" }), "mlayers(): main, front")
  check(mget(0, 20) == 1 and mget(12, 15) == 2 and mget(20, 17) == 3, "mget of layer 1")
  check(mget(5, 5) == 0 and mget(5, 5, 2) == 4 and mget(5, 5, "front") == 4, "mget of the layer front")
  check(mget(-1, 0) == 0 and mget(40, 0, "front") == 0, "mget out of the map: 0")
  check(not pcall(mget, 0, 0, "nope") and not pcall(mget, 0, 0, 3) and not pcall(mget, 0, 0, 0),
        "an unknown layer is an error")
  mset(1, 1, 3, 2)
  check(mget(1, 1, "front") == 3 and mget(1, 1) == 0, "mset on a layer")
  mset(1, 1, 0, "front")

  -- the flags (flags.csv: 1=1 2, 4)
  check(fget(1) == 1 and fget(2) == 2 and fget(3) == 4 and fget(4) == 0, "fget(n): the flags of flags.csv")
  check(fget(1, 0) and not fget(1, 1) and fget(2, 1) and fget(3, 2), "fget(n, f)")
  check(fget(-1) == 0 and fget(100000) == 0 and not fget(100000, 3), "fget out of the sheet: 0")
  check(not pcall(fget, 1, 8) and not pcall(fset, 1, 9, true), "a flag is 0 to 7")
  fset(4, 3, true)
  check(fget(4) == 8 and fget(4, 3), "fset(n, f, true)")
  fset(4, 5, true); fset(4, 3, false)
  check(fget(4) == 32, "fset(n, f, false)")
  fset(4, 0)
  check(fget(4) == 0, "fset(n, byte)")

  -- mflags(x, y, [w, h, layer]): pixels
  check(mflags(4, 164) == 1 and mflags(4, 4) == 1, "mflags of a point: the wall and the floor")
  check(mflags(16, 16, 8, 8) == 0, "mflags of an empty rectangle: 0")
  check(mflags(80, 116, 8, 8) == 2, "mflags under the platform's top")
  check(mflags(82, 100, 8, 20) == 0 and mflags(82, 100, 8, 20.5) == 2, "mflags: the rectangle's bottom edge is out")
  check(mflags(150, 150, 20, 20) == 1 | 4, "mflags: the flags together")
  check(mflags(40, 40, 0, 0, "front") == 0, "mflags of layer front: tile 4 has none")
  check(mflags(-50, -50) == 0 and mflags(1000, 1000, 8, 8) == 0, "mflags out of the map: 0")

  -- mlayers(list): add, keep, rename, take away
  check(same(mlayers({ "main", "front", "top" }), { "main", "front", "top" }), "mlayers: a new layer")
  check(mget(5, 5, "front") == 4 and mget(5, 5, "top") == 0 and mget(0, 20) == 1, "mlayers: the old layers kept")
  mset(2, 2, 9, "top")
  mlayers({ "back", "main", { "over", "front" }, { "copy", "top" } })
  check(same(mlayers(), { "back", "main", "over", "copy" }), "mlayers: moved, renamed, copied")
  check(mget(0, 20, 2) == 1 and mget(5, 5, "over") == 4 and mget(2, 2, "copy") == 9 and mget(0, 20, 1) == 0,
        "mlayers: the cells follow their layer")
  check(not pcall(mlayers, { "a", "a" }) and not pcall(mlayers, {}) and
        not pcall(mlayers, { "1", "2", "3", "4", "5", "6", "7", "8", "9" }) and
        not pcall(mlayers, { "a_name_far_too_long" }), "mlayers: bad lists are errors")
  mlayers({ "main", { "front", "over" } })
  check(same(mlayers(), { "main", "front" }) and mget(5, 5, "front") == 4, "mlayers: back as it was")

  -- msize(w, h)
  msize(48, 30)
  local w2, h2 = msize()
  check(w2 == 48 and h2 == 30 and mget(0, 20) == 1 and mget(5, 5, 2) == 4 and mget(45, 25) == 0,
        "msize(w, h): bigger, the cells in place")
  msize(40, 23)
  check(mget(39, 22) == 1, "msize back")
  check(not pcall(msize, 0, 10), "msize(0, h): an error")

  -- the named zones (sprites.txt)
  check(same(zones(), { "coin", "big" }), "zones()")
  local x, y, zw, zh, fr, fps = zone("coin")
  check(x == 0 and y == 8 and zw == 8 and zh == 8 and fr == 3 and fps == 10, "zone(coin)")
  local bx, by, bw, bh, bf, bfps = zone("big")
  check(bx == 0 and by == 16 and bw == 16 and bh == 16 and bf == 1 and bfps == 0, "zone(big)")
  check(zone("none") == nil and not pcall(zspr, "none", 0, 0), "an unknown zone")
end

-- drawing: the map by layer and by flags, the zones' frames
local function draw_checks()
  cls(0)
  map(0, 0, 0, 0, 40, 23, 1, 2)                -- only the platform
  check(pget(84, 124) == GREEN and pget(4, 164) == 0 and pget(164, 124) == 0, "map(..., layer, mask): the platform only")
  cls(0)
  map(0, 0, 0, 0, 40, 23)
  check(pget(4, 164) == RED and pget(84, 124) == GREEN and pget(164, 124) == BLUE and pget(44, 44) == 0,
        "map(): layer 1")
  map(0, 0, 0, 0, 40, 23, "front")
  check(pget(44, 44) == YELLOW and pget(84, 124) == GREEN, "map(..., \"front\") over layer 1")
  cls(0)
  check(zspr("coin", 100, 100, 1) == 1 and pget(103, 103) == CYAN, "zspr frame 1")
  check(zspr("coin", 100, 100, 2) == 2 and pget(103, 103) == WHITE, "zspr frame 2")
  check(zspr("coin", 100, 100, 6) == 3 and pget(103, 103) == MAGENTA, "zspr frame 6 = 3 (round)")
  zspr("big", 200, 100)
  check(pget(203, 103) == RED and pget(212, 112) == WHITE, "zspr of a zone 16x16")
  zspr("big", 200, 100, nil, true)
  check(pget(212, 103) == RED and pget(203, 103) == WHITE, "zspr flipped")
  cls(0)
  zspr("coin", 0, 0, 1, false, false, 2)
  check(pget(15, 15) == CYAN and pget(16, 16) == 0, "zspr zoom 2")
  local f = zspr("coin", 0, 0)                 -- by the clock: 10 frames a second
  check(f == math.floor(time() * 10) % 3 + 1, "zspr animated by time()")
end

-- saving: the layers, the flags and the zones go to the file and come back
local function save_checks()
  mlayers({ "main", "front", "extra" })
  mset(3, 3, 7, "extra")
  fset(5, 7, true)
  local ok, e = cart_save("/carts/GAPI.BM", { title = "game api", author = "tests", res = "640x360",
                                               lua = "function _draw() end" })
  check(ok, "cart_save: " .. tostring(e))
  mset(3, 3, 0, "extra")
  fset(5, 0)
  mlayers({ "main" })
  local p = cart_load("/carts/GAPI.BM")
  check(p and same(p.layers, { "main", "front", "extra" }), "cart_load: the layers' names")
  check(mget(3, 3, "extra") == 7 and mget(5, 5, "front") == 4 and mget(0, 20) == 1, "cart_load: the layers' cells")
  check(fget(5, 7) and fget(1) == 1 and fget(3) == 4, "cart_load: the flags")
  check(same(zones(), { "coin", "big" }), "cart_load: the zones")
  -- cart_write(sheet = true) writes the flags changed since
  fset(6, 2, true)
  check(cart_write("/carts/GAPI.BM", { sheet = true }), "cart_write(sheet = true)")
  fset(6, 0)
  cart_load("/carts/GAPI.BM")
  check(fget(6) == 4 and fget(5, 7), "cart_write: the flags")
  -- cart_sheet: a wider sheet keeps the flags on their cell (cell 9 is
  -- column 1 of row 1: 17 when there are 16 cells a row)
  fset(9, 1)
  cart_sheet(128, 32)
  check(fget(1) == 1 and fget(3) == 4 and fget(17) == 1 and fget(9) == 0, "cart_sheet: the flags follow their cell")
  cart_sheet(64, 32)
  check(fget(3) == 4 and fget(6) == 4 and fget(9) == 1, "cart_sheet back")
  fset(9, 0)
  cart_new()
  local w, h, n = msize()
  check(w == 256 and h == 256 and n == 1 and same(mlayers(), { "main" }) and fget(1) == 0 and #zones() == 0,
        "cart_new: one empty layer, no flags, no zones")
  cart_load("/carts/GAPI.BM")
end

---------------------------------------------------------------- bmlib (R10)

local function lib_math()
  check(lib.clamp(5, 0, 3) == 3 and lib.clamp(-1, 0, 3) == 0 and lib.clamp(2, 0, 3) == 2, "clamp")
  check(lib.lerp(2, 4, 0.5) == 3 and lib.unlerp(2, 4, 3) == 0.5 and lib.remap(5, 0, 10, 100, 200) == 150, "lerp")
  check(lib.approach(5, 10, 2) == 7 and lib.approach(5, 6, 2) == 6 and lib.approach(5, 0, 2) == 3, "approach")
  check(lib.sign(-3) == -1 and lib.sign(0) == 0 and lib.sign(2) == 1, "sign: 0 for 0")
  check(lib.round(2.5) == 3 and lib.round(-2.4) == -2 and lib.round(17, 5) == 15, "round")
  check(lib.wrap(-1, 0, 10) == 9 and lib.wrap(12, 0, 10) == 2 and lib.cycle(3, 1, 3) == 1 and lib.cycle(1, -1, 3) == 3,
        "wrap and cycle")
  check(lib.dist(0, 0, 3, 4) == 5 and lib.dist2(0, 0, 3, 4) == 25 and lib.len(3, 4) == 5, "dist")
  local nx, ny, l = lib.norm(3, 4)
  check(near(nx, 0.6) and near(ny, 0.8) and l == 5 and lib.norm(0, 0) == 0, "norm")
  check(near(lib.angle(0, 0, 0, 1), math.pi / 2) and near(lib.angdiff(0.1, lib.TAU - 0.1), -0.2), "angle, angdiff")
  check(near(lib.turn(0, 1, 0.25), 0.25) and near(lib.turn(0, 0.1, 0.25), 0.1), "turn")
  check(lib.dir8(1, 0) == 0 and lib.dir8(0, 1) == 2 and lib.dir8(-1, -1) == 5 and lib.dir8(0, 0) == nil, "dir8")
  local a, b = lib.rng(42), lib.rng(42)
  local same_seq, inside = true, true
  for _ = 1, 100 do
    local u = a:next()
    if u ~= b:next() then same_seq = false end
    local k = a:int(3, 6); b:int(3, 6)
    if u < 0 or u >= 1 or k < 3 or k > 6 or k ~= math.floor(k) then inside = false end
  end
  check(same_seq and inside, "rng: the same numbers from the same seed, in range")
  check(lib.rng(1):next() ~= lib.rng(2):next(), "rng: another seed, other numbers")
  local t = lib.shuffle({ 1, 2, 3, 4, 5 })
  table.sort(t)
  check(same(t, { 1, 2, 3, 4, 5 }) and lib.choose({}) == nil and lib.choose({ 7 }) == 7, "shuffle, choose")
  local r = lib.rnd(2, 3)
  check(r >= 2 and r < 3, "rnd(a, b)")
end

local function lib_collide()
  check(lib.overlap(0, 0, 10, 10, 5, 5, 10, 10) and not lib.overlap(0, 0, 10, 10, 10, 0, 5, 5), "overlap")
  check(lib.hit({ x = 0, y = 0 }, { x = 7, y = 7 }) and not lib.hit({ x = 0, y = 0 }, { x = 8, y = 0 }), "hit")
  check(lib.inside(5, 5, 0, 0, 10, 10) and not lib.inside(10, 5, 0, 0, 10, 10), "inside")
  check(lib.circles(0, 0, 5, 9, 0, 5) and not lib.circles(0, 0, 5, 10, 0, 5), "circles")
  check(lib.circrect(-3, 5, 4, 0, 0, 10, 10) and not lib.circrect(-5, -5, 4, 0, 0, 10, 10), "circrect")
end

local function lib_map()
  lib.tiles({ edge = true })
  check(lib.solid(4, 4) and lib.solid(16, 159, 8, 2) and not lib.solid(16, 150, 8, 8), "solid")
  check(lib.solid(-1, 50) and lib.solid(320, 50) and not lib.solid(319 - 8, 50), "solid: the edge (edge = true)")
  -- falling on the floor and on the platform
  local b = { x = 40, y = 100, w = 8, h = 8 }
  for _ = 1, 120 do lib.step(b) end
  check(b.y == 152 and b.ground and b.vy == 0, "step: on the floor (y 152)")
  local p = { x = 88, y = 90, w = 8, h = 8 }
  for _ = 1, 120 do lib.step(p) end
  check(p.y == 112 and p.ground, "step: on the platform (y 112)")
  p.drop = true
  for _ = 1, 120 do lib.step(p) end
  check(p.y == 152, "step: down through the platform with drop")
  local u = { x = 88, y = 130, w = 8, h = 8 }
  local _, hy = lib.move(u, 0, -20)
  check(hy == 0 and near(u.y, 110), "move: up through a platform")
  local s = { x = 290, y = 152, w = 8, h = 8 }
  local hx = lib.move(s, 30, 0)
  check(hx == 1 and s.x == 304, "move: against the right wall (x 304)")
  hx = lib.move(s, -400, 0)
  check(hx == -1 and s.x == 8, "move: against the left wall (x 8)")
  local f = { x = 100.5, y = 140, w = 6, h = 10 }
  local _, hy2 = lib.move(f, 0, 30)
  check(hy2 == 1 and f.y == 150, "move: fractional x, lands on y 150")
  hx, hy = lib.move(f, 0, 0)
  check(hx == 0 and hy == 0 and f.x == 100.5, "move by 0: stays")
  lib.tiles({ edge = false })
  local rx, ry, mx, my = lib.ray(20, 100, 20, 200)
  check(rx == 20 and ry == 160 and mx == 2 and my == 20, "ray down: the floor")
  check(lib.ray(20, 100, 60, 100) == nil, "ray: a free way")
  local lx, _, lmx = lib.ray(100, 50, 400, 50)
  check(near(lx, 312) and lmx == 39, "ray right: the wall")
  local ax, _, amx, amy = lib.ray(150, 130, 170, 130, lib.LADDER)
  check(near(ax, 160) and amx == 20 and amy == 16, "ray with another mask: the ladder")
  check(lib.ray(150, 130, 170, 130) == nil, "ray: a ladder is not solid")
end

local function lib_time()
  local o, done = { x = 0, y = 10 }, false
  lib.tween(o, { x = 10, y = 0 }, 0.5, "outquad", function() done = true end)
  local timer, ticks = 0, 0
  lib.after(0.2, function() timer = timer + 1 end)
  local h = lib.every(0.105, function() ticks = ticks + 1 end)
  local steps = {}
  lib.script(function()
    steps[#steps + 1] = "a"
    lib.wait(0.2)
    steps[#steps + 1] = "b"
    lib.waitfor(function() return done end)
    steps[#steps + 1] = "c"
  end)
  check(same(steps, { "a" }), "script: runs until its first wait")
  for i = 1, 15 do lib.update() if i == 7 then check(ticks == 1, "every: once in 0.105 s") end end
  check(o.x > 5 and o.x < 10 and not done and timer == 1 and same(steps, { "a", "b" }), "half way")
  for _ = 1, 16 do lib.update() end
  check(o.x == 10 and o.y == 0 and done and same(steps, { "a", "b", "c" }), "tween over, script on")
  h:cancel()
  local before = ticks
  for _ = 1, 30 do lib.update() end
  check(ticks == before and ticks == 4, "every and cancel")
  check(not pcall(lib.wait, 1), "wait outside a script: an error")
  check(not pcall(lib.tween, o, { x = 1 }, 1, "nope"), "an unknown easing: an error")
  check(lib.ease.outbounce(1) == 1 and lib.ease.inoutback(0) == 0 and near(lib.ease.smooth(0.5), 0.5), "easings")
  for name, e in pairs(lib.ease) do
    check(near(e(0), 0, 1e-9) and near(e(1), 1, 1e-9), "ease " .. name .. ": 0 at 0, 1 at 1")
  end
  local cd = { a = 1, b = 0.01 }
  lib.countdown(cd, { "a", "b", "c" })
  check(near(cd.a, 1 - 1 / 60) and cd.b == 0, "countdown")
  lib.jingle({ { "C5", 0.1 }, { 0, 0.05 }, { 523.25, 0.1 } }, 4)
  check(lib.jingling(4), "jingle: playing")
  for _ = 1, 3 do lib.update() end
  check(lib.jingling(4) and playing(4), "jingle: the voice sounds")
  for _ = 1, 30 do lib.update() end
  check(not lib.jingling(4), "jingle: over")
  lib.after(1, function() error("cleared timers never run") end)
  lib.clear()
  for _ = 1, 90 do lib.update() end
end

local function lib_things()
  local l = { { v = 1 }, { v = 2, dead = true }, { v = 3 } }
  lib.sweep(l)
  check(#l == 2 and l[2].v == 3, "sweep")
  lib.each(l, function(it) return it.v ~= 1 end)
  check(#l == 1 and l[1].v == 3, "each")
  local P = lib.particles(20)
  P:burst(100, 100, 15, { speed = 3, life = 0.5, colors = { RED, BLUE } })
  check(P:count() == 15, "burst")
  P:burst(100, 100, 15)
  check(P:count() == 20, "particles: at most max")
  for _ = 1, 60 do P:update() end
  check(P:count() == 0, "particles: their life is over")
  -- camera
  local cam = lib.camera({ bounds = true, w = 160, h = 90 })
  cam:follow(10, 10, true)
  check(cam.x == 0 and cam.y == 0, "camera: held by the map's corner")
  cam:follow(400, 400, true)
  check(cam.x == 320 - 160 and cam.y == 184 - 90, "camera: held by the other corner")
  cam:follow(160, 92, true)
  check(cam.x == 80 and cam.y == 47, "camera: the target in the middle")
  cam:follow(200, 92)
  check(cam.x > 80 and cam.x < 120, "camera: smooth")
  local d = lib.camera({ dead = { 40, 20 }, smooth = 1, w = 160, h = 90 })
  d:follow(80, 45, true)
  d:follow(90, 50)
  check(d.x == 0 and d.y == 0, "camera: inside the dead zone it stays")
  d:follow(130, 45)
  check(d.x == 30, "camera: out of the dead zone it follows")
  d:shake(4, 0.5)
  local moved = false
  for _ = 1, 10 do
    local x, y = d:apply()
    if x ~= 30 or y ~= 0 then moved = true end
    if math.abs(x - 30) > 4 or math.abs(y) > 4 then moved = "far" end
  end
  check(moved == true, "camera: the shake")
  for _ = 1, 40 do d:apply() end
  local x, y = d:apply()
  check(x == 30 and y == 0, "camera: the shake is over")
  camera()
  -- states
  local log_ = {}
  local defs = {
    title = { enter = function(s, a) log_[#log_ + 1] = "title+" .. tostring(a) end,
              exit = function() log_[#log_ + 1] = "title-" end },
    play = { enter = function() log_[#log_ + 1] = "play+" end, update = function(s) s.n = (s.n or 0) + 1 end,
             draw = function() log_[#log_ + 1] = "pdraw" end },
    pause = { enter = function() log_[#log_ + 1] = "pause+" end, exit = function() log_[#log_ + 1] = "pause-" end,
              draw = function() log_[#log_ + 1] = "zdraw" end },
  }
  local sm = lib.states(defs, "title", 1)
  sm:go("play")
  sm:update(); sm:update()
  check(sm:is("play") and defs.play.n == 2 and near(defs.play.t, 2 / 60), "states: go, update, t")
  sm:push("pause")
  sm:update()
  check(sm.name == "pause" and defs.play.n == 2, "states: push stops the one under")
  sm:draw()
  sm:pop()
  check(sm.name == "play" and same(log_, { "title+1", "title-", "play+", "pause+", "pdraw", "zdraw", "pause-" }),
        "states: the order of enter, exit and draw")
  check(not pcall(sm.go, sm, "nope"), "states: an unknown state")
  -- text
  font("8x16")
  local tw, th = lib.textw("abc")
  check(tw == 24 and th == 16 and lib.textw("ab\nabcd", 2) == 64, "textw")
  check(lib.printc("abc", 0) == (SCREEN_W - 24) // 2 and lib.printr("abc", 100, 0) == 76, "printc, printr")
  check(lib.timestr(75) == "1:15" and lib.timestr(3725) == "1:02:05" and lib.timestr(5.25, true) == "0:05.2",
        "timestr")
  check(lib.blink(0.5, 0.2) and not lib.blink(0.5, 0.7), "blink")
  -- colours, frames
  check(lib.mix(0x000000, 0xFF0080, 0.5) == 0x800040 and lib.shade(0x204080, 2) == 0x4080FF, "mix, shade")
  check(lib.frame({ "a", "b" }, 2, 0.6) == "b" and lib.frame({ "a", "b" }, 2, 1.1) == "a", "frame")
  local an = lib.anim({ 1, 2, 3 }, 10, false)
  for _ = 1, 30 do an:update() end
  check(an:get() == 3 and an.done, "anim: over, on its last frame")
  -- saves
  local best, new = lib.best("score", 10)
  check(best == 10 and new, "best: a new record")
  best, new = lib.best("score", 5)
  check(best == 10 and not new and saved().score == 10, "best: kept, saved")
  lib.store("name", "bm")
  check(lib.store("name") == "bm" and saved().name == "bm" and saved().score == 10, "store")
  -- 3D
  local m = lib.builder():box(-1, 0, -2, 1, 3, 2, 0xFF0000, 0x00FF00):tetra({ 0, 4, 0 }, { 1, 3, 0 }, { -1, 3, 0 },
                                                                           { 0, 3, 1 }, 0x0000FF):build()
  local x0, y0, z0, x1, y1, z1 = bounds3d(m)
  check(x0 == -1 and y0 == 0 and z0 == -2 and x1 == 1 and y1 == 4 and z1 == 2, "builder: box and tetra")
end

---------------------------------------------------------------- input: btnr, menu, pause

local f, phase = 0, 1
local menu, chosen = nil, nil
local pause
local volume0

function _init()
  map_api()
  save_checks()
  lib_math()
  lib_collide()
  lib_map()
  lib_time()
  lib_things()
  menu = lib.menu({ "one", { label = "two", off = true }, { label = "three", value = "3" } }, { wrap = false })
  volume0 = volume()
  pause = lib.pause({ quit = function() chosen = "quit" end, when = function() return phase >= 4 end })
end

local reps, held_n, down_at = 0, 0, nil

function _update()
  f = f + 1
  if pause:update() then return end
  if phase == 1 then
    -- input.txt: down held for 30 frames -> pressed, then repeated; without
    -- presses (QEMU) the phases of the input never start
    if lib.btnr("down") then reps = reps + 1 end
    if btn("down") then
      held_n = held_n + 1
    elseif held_n > 0 then
      check(reps == 1 + math.max(0, held_n - 15) // 4 and held_n >= 19,
            "btnr: once, then every 4 frames after 15 (" .. reps .. " in " .. held_n .. ")")
      phase = 2
    end
  elseif phase == 2 then
    local it, a = menu:update()
    if btnp("down") then down_at = f end
    if down_at and f == down_at + 2 then check(menu.sel == 3, "menu: down skips the row that is off") end
    if a == "ok" then chosen = it; phase = 3 end
  elseif phase == 3 then
    check(type(chosen) == "table" and chosen.label == "three", "menu: ok chooses the row")
    phase = 4
  elseif phase == 5 and f == 200 then
    check(not pause.open and volume() == math.min(volume0 + 1, 10), "pause: VOLUME up, then closed")
    phase = 6
  elseif phase == 6 and f == 260 then
    check(chosen == "quit", "pause: QUIT calls quit")
    volume(volume0)
    log(string.format("gameapi: %d/%d checks passed", checks - fails, checks))
    quit()
  end
  if phase == 4 and f >= 100 then phase = 5 end
end

function _draw()
  if f == 2 then draw_checks() end
  cls(0x101418)
  map(0, 0, 0, 0, 40, 23)
  map(0, 0, 0, 0, 40, 23, "front")
  zspr("coin", 100, 60)
  zspr("big", 120, 60)
  print("game api", 8, 192, 0xFFFFFF)   -- on the 16 px rows: the QEMU test reads it
  menu:draw(340, 40, { w = 200 })
  pause:draw()
  if f > 400 then
    log(string.format("gameapi: %d/%d checks passed (timeout)", checks - fails, checks))
    quit()
  end
end
