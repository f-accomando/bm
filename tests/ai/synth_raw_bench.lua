-- Raw-Specs & 3-Layer (INT8 -> Raw Specs -> Lisp) Benchmark (0..100)
-- Tests atomic parameterized mechanics WITHOUT magic game-template words.
-- Run: build/host/luaai build/assist.bin tests/ai/synth_raw_bench.lua

local total_pts, max_pts = 0, 0
local results = {}

local function check_pts(pts, ok, label, info)
  max_pts = max_pts + pts
  if ok then total_pts = total_pts + pts end
  results[#results + 1] = string.format("[%s] %-50s (%d/%d)%s",
    ok and "PASS" or "FAIL", label, ok and pts or 0, pts, info and (" -- " .. info) or "")
end

local function make_env()
  local btns, btnps = {}, {}
  local draws = { cls = 0, rects = {}, circs = {}, texts = {} }
  local env = {
    math = math, table = table, string = string, ipairs = ipairs, pairs = pairs,
    tonumber = tonumber, tostring = tostring, type = type,
    abs = math.abs, min = math.min, max = math.max, floor = math.floor,
    btn = function(b) return btns[b] or false end,
    btnp = function(b) return btnps[b] or false end,
    cls = function(c) draws.cls = draws.cls + 1 end,
    rectfill = function(x1, y1, x2, y2, c)
      draws.rects[#draws.rects + 1] = { x1 = x1, y1 = y1, x2 = x2, y2 = y2, c = c }
    end,
    circfill = function(x, y, r, c)
      draws.circs[#draws.circs + 1] = { x = x, y = y, r = r, c = c }
    end,
    print = function(s, x, y, c)
      draws.texts[#draws.texts + 1] = tostring(s)
    end,
    sfx = function() end,
    _btns = btns,
    _btnps = btnps,
    _draws = draws,
  }
  return env
end

local function boot(code)
  if not code or code == "" then return nil end
  local env = make_env()
  local fn = load(code, "raw_bench", "t", env)
  if not fn then return nil end
  if not pcall(fn) then return nil end
  if type(env._init) == "function" then pcall(env._init) end
  return env
end

-- ============================================================================
-- 1. Geometric actor with parameterized color & size (15 pts)
-- ============================================================================
do
  local r1 = ai.synth("crea rettangolo hero colore 11 larg 12 alt 14")
  local r2 = ai.synth("crea rettangolo hero colore 9 larg 20 alt 10")
  local e1, e2 = boot(r1.code), boot(r2.code)
  local ok1, ok2 = false, false
  if e1 and type(e1._draw) == "function" then
    pcall(e1._draw)
    for _, rc in ipairs(e1._draws.rects) do
      if rc.c == 11 and math.abs((rc.x2 - rc.x1) - 12) <= 1 and math.abs((rc.y2 - rc.y1) - 14) <= 1 then ok1 = true end
    end
  end
  if e2 and type(e2._draw) == "function" then
    pcall(e2._draw)
    for _, rc in ipairs(e2._draws.rects) do
      if rc.c == 9 and math.abs((rc.x2 - rc.x1) - 20) <= 1 and math.abs((rc.y2 - rc.y1) - 10) <= 1 then ok2 = true end
    end
  end
  check_pts(15, ok1 and ok2, "1. Parametric rect-actor (color & WxH vary)")
end

-- ============================================================================
-- 2. Horizontal acceleration + friction + max speed (15 pts)
-- ============================================================================
do
  local r = ai.synth("crea rettangolo hero colore 11 larg 12 alt 14, movimento orizzontale hero accelerazione 2 attrito 1 max 5")
  local env = boot(r.code)
  local ok = false
  if env and type(env._update) == "function" and type(env.hero) == "table" then
    local x0 = env.hero.x
    env._btns[1] = true
    pcall(env._update)
    local vx1 = env.hero.vx
    for _ = 1, 10 do pcall(env._update) end
    local vx_max = env.hero.vx
    env._btns[1] = false
    pcall(env._update)
    local vx_dec = env.hero.vx
    ok = (vx1 == 2) and (vx_max == 5) and (vx_dec < 5) and (env.hero.x > x0)
  end
  check_pts(15, ok, "2. Horizontal accel=2, friction=1, max=5")
end

-- ============================================================================
-- 3. Vertical gravity + custom jump button + floor Y (20 pts)
-- ============================================================================
do
  local r = ai.synth("crea rettangolo hero colore 11 larg 12 alt 14, gravita 2, salto 8 con tasto 5, pavimento y 160")
  local env = boot(r.code)
  local ok = false
  if env and type(env._update) == "function" and type(env.hero) == "table" then
    for _ = 1, 40 do pcall(env._update) end
    local y_floor = env.hero.y
    -- btn 4 must NOT jump; btn 5 MUST jump
    env._btnps[4] = true
    pcall(env._update)
    env._btnps[4] = false
    local y_after_b4 = env.hero.y
    env._btnps[5] = true
    pcall(env._update)
    env._btnps[5] = false
    local jumped = env.hero.y < y_floor
    for _ = 1, 30 do pcall(env._update) end
    local landed = math.abs(env.hero.y - y_floor) <= 1
    ok = (y_floor == 160 - 14) and (y_after_b4 == y_floor) and jumped and landed
  end
  check_pts(20, ok, "3. Gravity=2, jump=8 on btnp(5), floor_y=160")
end

-- ============================================================================
-- 4. Patrol bounds reversal + stomp-or-hit (20 pts)
-- ============================================================================
do
  local r = ai.synth("crea rettangolo hero colore 11 larg 12 alt 14, pattuglia enemy colore 8 velocita 2 min 50 max 200, schiaccia dall'alto")
  local env = boot(r.code)
  local ok = false
  if env and type(env._update) == "function" and type(env.enemy) == "table" and type(env.hero) == "table" then
    local ex0 = env.enemy.x
    for _ = 1, 10 do pcall(env._update) end
    local moved = env.enemy.x ~= ex0
    -- test boundary reversal near max=200
    env.enemy.x = 199
    env.enemy.dir = 1
    pcall(env._update)
    pcall(env._update)
    local reversed = (env.enemy.dir == -1) and (env.enemy.x <= 200)
    -- test stomp from above
    env.hero.x = env.enemy.x
    env.hero.y = env.enemy.y - 12
    env.hero.vy = 4
    pcall(env._update)
    local stomped = (env.enemy.alive == false)
    ok = moved and reversed and stomped
  end
  check_pts(20, ok, "4. Patrol bounds [50..200] + stomp from above")
end

-- ============================================================================
-- 5. Full raw-spec wire-cart + mkbm.py .bm + 180 frames (15 pts)
-- ============================================================================
do
  local q = "crea rettangolo hero colore 11 larg 12 alt 14, movimento orizzontale hero accelerazione 1 attrito 1 max 3, gravita 1, salto 6 con tasto 4, pavimento y 144, pattuglia enemy colore 8 velocita 1 min 80 max 220, schiaccia dall'alto"
  local r = ai.synth(q)
  local env = boot(r.code)
  local sim_ok = false
  if env and type(env._update) == "function" and type(env._draw) == "function" then
    local ok_all = true
    for f = 1, 180 do
      env._btns[1] = (f % 60) < 40
      env._btnps[4] = (f % 45 == 1)
      if not pcall(env._update) or not pcall(env._draw) then ok_all = false; break end
    end
    sim_ok = ok_all and #env._draws.rects >= 180
  end
  local bm_ok = false
  if r.ok and r.code and r.code ~= "" then
    local tl, tb = os.tmpname() .. ".lua", os.tmpname() .. ".bm"
    local f = assert(io.open(tl, "w")); f:write(r.code .. "\n"); f:close()
    local ok = os.execute("python3 core/scripts/mkbm.py -o " .. tb .. " --lua " .. tl .. " --title \"Raw\" --res 320x180 > /dev/null 2>&1")
    if ok == true or ok == 0 then
      local bf = io.open(tb, "rb")
      if bf then bm_ok = (bf:read(6) == "BMCART"); bf:close() end
    end
    os.remove(tl); os.remove(tb)
  end
  check_pts(15, sim_ok and bm_ok, "5. Full raw-spec wire-cart + .bm + 180-frame sim")
end

-- ============================================================================
-- 6. INT8 Genre Classification -> Raw-Spec Expansion (15 pts)
-- ============================================================================
do
  local queries = {
    { "voglio fare uno sparatutto spaziale con navicella e laser", "genre.shmup" },
    { "gioco con racchetta e pallina che rimbalza sui mattoni", "genre.breakout" },
    { "avventura dall'alto con stanze muri e nemici", "genre.topdown" },
  }
  local passed = 0
  for _, item in ipairs(queries) do
    local hits = ai.ask(item[1], 3)
    local best = hits and hits[1]
    if best and best.id == item[2] then
      local s = ai.synth(item[1], { entry = best.id, target = "lua" })
      local env = boot(s and s.code)
      if env and type(env._init) == "function" and type(env._update) == "function" and type(env._draw) == "function" then
        local ok_u = pcall(env._update)
        local ok_d = pcall(env._draw)
        if ok_u and ok_d and #env._draws.rects + #env._draws.circs >= 2 then
          passed = passed + 1
        end
      end
    end
  end
  check_pts(15, passed == 3, string.format("6. INT8 genre->raw-spec expansion (%d/3 genres)", passed))
end

print("========================================")
print("  bm ai.synth Raw-Specs & 3-Layer Bench")
print("========================================")
for _, line in ipairs(results) do print(line) end
print("----------------------------------------")
print(string.format("TOTAL SCORE: %d / %d (%.1f%%)", total_pts, max_pts, (total_pts * 100.0) / max_pts))
print("========================================")
