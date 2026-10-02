-- Yharnam 8 (carts/nano8/roms/yharnam8.p8) played by a bot inside nano8 on
-- the PC: the buttons are the bot's, the cart's own code runs. It checks
-- the hunter's ways (blows, combo, charge, trick, dodges, run, pistol and
-- parry, visceral, vial, rally), the lamps and their paths, a death and a
-- lost hunt, the four bosses (their phases and moves) and the end.
--
--   make test-yharnam8
--   (n8host ... --at 5:exec=dofile('tests/yharnam/y8.lua'))

local G = NANO8.Vm.G
local held, prev = {}, {}
G.btn = function(b) return held[b or 0] or false end
G.btnp = function(b) return (held[b or 0] and not prev[b or 0]) or false end

local function print(s) io.write(s, "\n"); io.flush() end
local checks, fails = 0, 0
local function check(c, msg)
  checks = checks + 1
  if not c then
    fails = fails + 1
    print("FAIL: " .. msg)
  end
end
local function say(...) print(string.format(...)) end

local function wait(n) for _ = 1, n or 1 do coroutine.yield() end end
local function set(...) held = {}; for _, b in ipairs({ ... }) do held[b] = true end end
local function tap(...) set(...); wait(2); set(); wait(1) end
local function hold(n, ...) set(...); wait(n); set(); wait(1) end

local function P() return G.P end
local function foes(name)
  local t = {}
  for _, f in ipairs(G.F) do if f.T.nm == name and f.st ~= "dead" then t[#t + 1] = f end end
  return t
end
local function clear(keep) for i = #G.F, 1, -1 do if G.F[i] ~= keep then table.remove(G.F, i) end end end
local function put(x, y)
  local p = P()
  p.x, p.y, p.act, p.q = x, y, nil, nil
  G.cx, G.cy = G.cam()
end
-- the hunter beside f, facing it
local function beside(f, dx)
  dx = dx or -11
  put(f.x + dx, f.y)
  P().fx, P().fy = dx < 0 and 1 or -1, 0
end
local function god() P().hp, P().st, P().inv = 20, 100, 30 end

local function scenarios()
  wait(3)
  tap(4); wait(4)
  check(G.st == "play", "O on the title: the hunt begins")
  local p = P()
  check(p.hp == 20 and p.form == 0, "the hunter whole, the saw folded")

  -- walking
  local x0 = p.x
  hold(20, 1)
  check(p.x > x0 + 10, "right: he walks")

  -- a townsman: quick blows, a combo
  local f = foes("townsman")[1]
  clear(f)
  f.st, f.cd = "move", 999
  beside(f)
  local hp0 = f.hp
  tap(4); wait(8)
  check(f.hp < hp0, "a quick blow cuts (" .. hp0 .. " -> " .. f.hp .. ")")
  local h1 = f.hp
  tap(4); wait(3)
  check(p.cmb == 1, "again in the recovery: the combo goes on")
  wait(10)
  for _ = 1, 6 do if f.st ~= "dead" then beside(f); f.cd = 999; tap(4); wait(12) end end
  check(f.st == "dead" or f.hp <= 0, "the townsman cut down")
  check(G.G.kills == 1 and G.G.ec >= 20, "it leaves echoes (" .. G.G.ec .. ")")
  wait(45)

  -- a charged blow: more than a quick one
  G.respawn()
  f = foes("townsman")[1]
  clear(f)
  f.hp, f.st, f.cd = 99, "move", 999
  beside(f)
  god()
  tap(4); wait(14)
  local quick = 99 - f.hp
  hp0 = f.hp
  beside(f)
  hold(26, 4); wait(14)
  local charged = hp0 - f.hp
  say("quick blow %.1f, charged %.1f", quick, charged)
  check(charged > quick * 1.8, "the charged blow bites deeper")

  -- the trick: after a blow, O and X: the saw opens, the combo goes on
  beside(f)
  tap(4); wait(7)
  set(4, 5); wait(2); set(); wait(2)
  check(p.form == 1 and p.act == "atk", "the trick: the saw opens in a blow")
  wait(20)
  hp0 = f.hp
  beside(f)
  tap(4); wait(14)
  local open = hp0 - f.hp
  say("open quick blow %.1f", open)
  check(open > quick, "open, the saw bites deeper")
  beside(f)
  tap(4); wait(7)
  set(4, 5); wait(2); set(); wait(20)
  check(p.form == 0, "and closes again with another trick")

  -- dodges: a roll, a step back, by a foe a quick step
  clear()
  put(G.sp[2] * 8 + 4, G.sp[3] * 8 + 6)
  god()
  set(1); wait(3); set(1, 5); wait(2); set(1); wait(3)
  check(p.act == "dodge" and p.roll and p.inv > 0, "X while moving: a roll, out of harm's way")
  set(); wait(20)
  tap(5); wait(2)
  check(p.act == "dodge" and not p.roll and p.vx < 0 == (p.fx > 0), "X standing: a step back")
  wait(20)
  -- running
  set(1, 5); wait(12)
  local s0 = p.st
  wait(20)
  check(p.act == "run", "X held moving: he runs (" .. tostring(p.act) .. ")")
  check(p.st < s0 - 8, "running, the stamina goes (" .. s0 .. " -> " .. p.st .. ")")
  set(); wait(1)
  wait(40)
  -- the blood vial: X held standing
  G.G.ec, p.hp = 300, 10
  hold(30, 5); wait(20)
  check(p.hp > 10 and G.G.ec == 200, "X held standing: a blood vial (" .. p.hp .. ")")

  -- the pistol, the parry, the visceral
  G.respawn()
  f = foes("townsman")[1]
  clear(f)
  f.st, f.cd = "move", 0
  beside(f, -12)
  god()
  local parried = false
  for _ = 1, 200 do
    god()
    if f.st == "wind" and f.tm <= 8 then
      set(4, 5); wait(2); set(); wait(4)
      parried = f.st == "stag"
      break
    end
    wait()
  end
  check(parried, "the pistol in the wind-up: a parry, it reels")
  tap(4); wait(3)
  check(p.act == "vis", "O by it reeling: a visceral attack")
  wait(15)
  check(f.st == "dead", "the visceral kills a townsman")
  wait(45)

  -- the rally: struck, a blow soon after takes blood back
  G.respawn()
  f = foes("townsman")[1]
  clear(f)
  f.hp, f.st, f.cd = 99, "move", 999
  beside(f)
  p.hp, p.inv = 20, 0
  G.hurt_me(6, p.x + 5, p.y)
  local hurt = p.hp
  wait(14)
  check(p.ral > 0, "struck: blood to rally")
  beside(f)
  tap(4); wait(10)
  check(p.hp > hurt, "a blow soon after: some blood back (" .. hurt .. " -> " .. p.hp .. ")")

  -- the lamp: lit, rest, a path taken
  clear()
  local s = G.shr[1]
  put(s.x, s.y + 6)
  G.G.ec = 1000
  tap(4); wait(3)
  check(G.st == "lamp" and s.lit, "O at a hunter's lamp: lit, its menu")
  tap(4); wait(2)
  check(#G.G.path == 1 and G.G.ec == 700, "a path taken (" .. G.G.ec .. " echoes left)")
  check(p.m.def < 1, "feral affinity: less blood lost")
  tap(5); wait(2)
  check(G.st == "play", "X: back to the hunt")

  -- death: echoes paid, back at the lamp; without echoes, the hunt lost
  G.G.ec = 150
  p.hp, p.inv = 1, 0
  G.hurt_me(5, p.x, p.y)
  check(p.act == "dead", "struck down")
  wait(95)
  check(p.act == nil and p.hp == 20 and G.G.ec == 50 and G.G.deaths == 1, "back at the lamp, 100 echoes paid")
  p.hp, p.inv = 1, 0
  G.hurt_me(5, p.x, p.y)
  wait(95)
  check(G.st == "lost", "no echoes to pay: the hunt is lost")
  wait(70)
  tap(4); wait(3)
  check(G.st == "title", "O: the title again")

  -- the bosses: each in its arena, its phases, its moves; slain, the way on
  tap(4); wait(4)
  for b = 1, 4 do
    local bf
    for _, o in ipairs(G.F) do if o.boss == b then bf = o end end
    check(bf, "boss " .. b .. " in its arena")
    if not bf then break end
    clear(bf)
    put(bf.x - 40, bf.y)
    local seen, roars = {}, 0
    for i = 1, 2400 do
      god()
      if i == 800 then bf.hp = bf.T.hp * 0.6 end
      if i == 1600 then bf.hp = bf.T.hp * 0.3 end
      local st = bf.st
      wait()
      if bf.st == "roar" and st ~= "roar" then roars = roars + 1 end
      if bf.st == "wind" then seen[bf.mv] = true end
      -- the hunter steps about: the boss has to come, or reach
      if i % 200 == 0 then put(bf.x + (i % 400 == 0 and 50 or -30), bf.y + 6) end
    end
    local l = {}
    for k in pairs(seen) do l[#l + 1] = k end
    table.sort(l)
    say("%s: phases %d, moves %s", bf.T.nm, bf.ph, table.concat(l, " "))
    check(bf.ph == 3 and roars == 2, bf.T.nm .. ": three phases, a roar into each")
    check(#l >= 4, bf.T.nm .. ": its moves")
    check(G.boss == bf, bf.T.nm .. ": awake, its name on the screen")
    -- slain
    bf.hp = 0.5
    beside(bf, -14)
    bf.st, bf.cd = "move", 999
    for _ = 1, 10 do if bf.st ~= "dead" then god(); beside(bf, -14); tap(4); wait(12) end end
    check(G.G.won[b], bf.T.nm .. ": prey slaughtered")
    if b < 4 then
      local gx, gy = G.gt[(b - 1) * 5 + 1], G.gt[(b - 1) * 5 + 2]
      check(not G.solid(gx * 8 + 4, gy * 8 + 4), "the mist is gone: the way on")
      G.respawn()
    end
  end
  wait(100)
  check(G.st == "end", "the fourth boss slain: the night is over")
  wait(70)
  tap(4); wait(3)
  check(G.st == "title", "O: the title again")
  say("yharnam8: %d checks, %d failed", checks, fails)
  if fails == 0 then say("yharnam8: all passed") end
  os.exit(fails == 0 and 0 or 1)
end

local co = coroutine.create(scenarios)
local upd = G._update
G._update = function()
  local ok, e = coroutine.resume(co)
  if not ok then
    print("ERROR: " .. tostring(e))
    print(debug.traceback(co))
    os.exit(1)
  end
  upd()
  for b = 0, 5 do prev[b] = held[b] end
end
