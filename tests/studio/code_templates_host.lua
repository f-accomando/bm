-- bm Code, "New project" templates (R16): the starters of carts/code/main.lua.
-- The test reads the block between the "-- templates: begin" and
-- "-- templates: end" lines, then for each template it checks the module
-- lines, compiles the code and runs it for many frames with stand-ins for
-- the console's drawing and input calls. A stand-in records the calls, so
-- the test can see that the hero lands, the foes come, a punch wins.
--   luahost tests/studio/code_templates_host.lua ROOT
local ROOT = arg and arg[1] or "."
local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; print("FAIL " .. what) end
end

local f = assert(io.open(ROOT .. "/carts/code/main.lua", "rb"))
local src = f:read("a")
f:close()
local a = assert(src:find("local TEMPLATE = [[", 1, true), "TEMPLATE not found")
local b1, b2 = src:find("-- templates: end", a, true)
assert(b2, "templates end line not found")
local TEMPLATES = assert(load(src:sub(a, b2) .. "\nreturn TEMPLATES", "=templates", "t", {}))()

local U = assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", setmetatable({
  mouse = function(on) if on ~= nil then return true end end,
  mousep = function() return false end, keyp = function() end, time = function() return 0 end,
  screen_size = function() return 640, 360 end, rectfill = function() end, rect = function() end,
  print = function() end }, { __index = _G })))()

local ids = {}
for i, t in ipairs(TEMPLATES) do ids[i] = t.id end
check(table.concat(ids, ",") == "empty,platform,topdown,shooter,versus,scene3d", "the list of templates: " .. table.concat(ids, ","))

local function lines_of(text)
  local ls = {}
  for l in (text .. "\n"):gmatch("(.-)\n") do ls[#ls + 1] = l end
  if ls[#ls] == "" then ls[#ls] = nil end
  return ls
end

-- a run of one template: input is a function(kind, name, pad, frame) -> bool
local function run(t, frames, input, each)
  local calls, texts = {}, {}
  local frame = 0
  local E
  E = setmetatable({
    SCREEN_W = 640, SCREEN_H = 360,
    require = function() return { input = function() end } end,
    btn = function(n, p) return input("btn", n, p, frame) end,
    btnp = function(n, p) return input("btnp", n, p, frame) end,
    print = function(s, x, y, c) texts[#texts + 1] = s end,
    rectfill = function(x, y, w, h, c) calls[#calls + 1] = { "rect", x, y, w, h, c } end,
    circfill = function(x, y, r, c) calls[#calls + 1] = { "circ", x, y, r, c } end,
    mesh = function() return {} end, mesh_cube = function() return {} end, mesh_sphere = function() return {} end,
    draw3d = function(m, x, y, z) calls[#calls + 1] = { "d3", x, y, z } end,
  }, { __index = function(_, k)
    if _G[k] ~= nil then return _G[k] end
    return function() end                     -- any other drawing call: nothing
  end })
  E._G = E
  local fn, err = load(t.code, "=" .. t.id, "t", E)
  check(fn, t.id .. " compiles: " .. tostring(err))
  if not fn then return nil end
  local ok, e = pcall(fn)
  check(ok, t.id .. " loads: " .. tostring(e))
  if not ok then return nil end
  if rawget(E, "_init") then check(pcall(E._init), t.id .. " _init") end
  local last
  for n = 1, frames do
    frame = n
    calls, texts = {}, {}
    local ok1, e1 = pcall(E._update)
    local ok2, e2 = pcall(E._draw)
    if not (ok1 and ok2) then
      check(false, t.id .. " frame " .. n .. ": " .. tostring(e1) .. " " .. tostring(e2))
      return nil
    end
    last = { calls = calls, texts = texts }
    if each then each(last, n) end
  end
  return last, E
end

local function none() return false end
local function has_text(r, word)
  for _, s in ipairs(r.texts) do if tostring(s):find(word, 1, true) then return true end end
  return false
end
local function rect_of(r, color)
  for _, c in ipairs(r.calls) do if c[1] == "rect" and c[6] == color then return c end end
end

local expect_mods = { empty = 1, platform = 4, topdown = 4, shooter = 4, versus = 3, scene3d = 2 }
for _, t in ipairs(TEMPLATES) do
  local mods = U.modules_split(lines_of(t.code))
  check(#mods == expect_mods[t.id], t.id .. " has " .. expect_mods[t.id] .. " modules, found " .. #mods)
  check(mods[1].name == "main", t.id .. " first module is main")
  for _, m in ipairs(mods) do check(U.module_len(m) > 0, t.id .. " module " .. m.name .. " is not empty") end
  check(#t.name <= 14, t.id .. " name fits the list column")
  check(#t.desc <= 50, t.id .. " description fits the list")
  check(not t.code:find("\r", 1, true), t.id .. " has no CR")
  -- the join of the modules is the code, byte for byte (what Ctrl+S writes)
  check(table.concat(U.modules_join(mods), "\n") .. "\n" == t.code, t.id .. " join gives the code back")
end

-- empty: the old template, a ball that moves
local r = run(TEMPLATES[1], 30, function(kind, n) return kind == "btn" and n == 1 end)
check(r and r.calls[1] and r.calls[1][1] == "circ", "empty draws a circle")

-- platform: with no input the hero falls and lands on the floor (y = GROUND - h)
r = run(TEMPLATES[2], 200, none)
local hero = r and rect_of(r, 0xFFD050)
check(hero and hero[3] + hero[5] == 360 - 40, "platform: the hero stands on the floor")
-- holding right moves it; act1 pressed jumps (the hero leaves the floor)
r = run(TEMPLATES[2], 120, function(k, n) return k == "btn" and n == "right" end)
hero = r and rect_of(r, 0xFFD050)
check(hero and hero[2] > 40, "platform: right runs")
r = run(TEMPLATES[2], 120, function(k, n, p, fr) return k == "btnp" and n == "act1" and fr == 100 end)
hero = r and rect_of(r, 0xFFD050)
check(hero and hero[3] + hero[5] < 360 - 40, "platform: act1 jumps")

-- topdown: the player walks right; the first coin text starts at 0
r = run(TEMPLATES[3], 1, none)
check(r and has_text(r, "coins 0/8"), "topdown: 0 of 8 coins")
r = run(TEMPLATES[3], 60, function(k, n) return k == "btn" and n == "right" end)
local pl = r and rect_of(r, 0x60C0FF)
check(pl and pl[2] > 40, "topdown: the player walks")
-- down 120 frames along x = 40: the left of the wall at 120 stops nothing; the wall at 120..136 stops a walk right at y = 100
r = run(TEMPLATES[3], 400, function(k, n, p, fr)
  if k ~= "btn" then return false end
  if fr <= 60 then return n == "down" end           -- y from 40 to 160
  return n == "right"
end)
pl = r and rect_of(r, 0x60C0FF)
check(pl and pl[2] + pl[4] <= 120, "topdown: the wall at x = 120 stops the player (x " .. tostring(pl and pl[2]) .. ")")

-- shooter: foes come; the ship goes up and down and fires; a foe on the ship ends the game
math.randomseed(1)
r = run(TEMPLATES[4], 120, none)
check(r and rect_of(r, 0xFF6060) ~= nil, "shooter: foes come")
local best, over = 0, false
local function sweep(k, n, p, fr)
  if k == "btnp" then return n == "act1" and fr % 5 == 0 end
  local up = (fr // 45) % 2 == 0
  return n == (up and "up" or "down")
end
math.randomseed(1)
run(TEMPLATES[4], 4000, sweep, function(rr)
  for _, s in ipairs(rr.texts) do
    local sc = tostring(s):match("^score (%d+)")
    if sc then best = math.max(best, tonumber(sc)) end
  end
end)
check(best >= 1, "shooter: a shot kills a foe (best score " .. best .. ")")
math.randomseed(1)
run(TEMPLATES[4], 6000, none, function(rr) if has_text(rr, "game over") then over = true end end)
check(over, "shooter: a foe that touches the ship ends the game")

-- versus: P1 walks 12 px, punches, and again (a push is 12 px) until P2 has no life
r = run(TEMPLATES[5], 2500, function(k, n, p, fr)
  if p ~= 1 then return false end
  if k == "btn" then return n == "right" and fr % 30 < 6 end
  return n == "act2" and fr % 30 == 7
end)
check(r and has_text(r, "P1 wins"), "versus: five punches win")
r = run(TEMPLATES[5], 200, none)
check(r and not has_text(r, "wins"), "versus: nobody wins by standing")

-- scene3d: draws the floor, four cubes and the ball twice (shadow, ball)
r = run(TEMPLATES[6], 30, none)
local n3 = 0
for _, c in ipairs(r and r.calls or {}) do if c[1] == "d3" then n3 = n3 + 1 end end
check(n3 == 7, "scene3d draws 7 objects, drew " .. n3)

print(string.format("code_templates_host: %d checks, %d failed", checks, fails))
if fails > 0 then os.exit(1) end
