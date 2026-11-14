-- Advanced Benchmark: Paraphrased NL + Multi-Entity Interactions (0..100)
-- Run: build/host/luaai build/assist.bin tests/ai/synth_adv_bench.lua

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
  local draws = { cls = 0, rects = {}, circs = {} }
  return {
    math = math, table = table, string = string, ipairs = ipairs, pairs = pairs,
    tonumber = tonumber, tostring = tostring, type = type,
    abs = math.abs, min = math.min, max = math.max, floor = math.floor,
    btn = function(b) return btns[b] or false end,
    btnp = function(b) return btnps[b] or false end,
    cls = function() draws.cls = draws.cls + 1 end,
    rectfill = function(x1, y1, x2, y2, c)
      draws.rects[#draws.rects + 1] = { x1 = x1, y1 = y1, x2 = x2, y2 = y2, c = c }
    end,
    circfill = function(x, y, r, c)
      draws.circs[#draws.circs + 1] = { x = x, y = y, r = r, c = c }
    end,
    print = function() end,
    sfx = function() end,
    _btns = btns, _btnps = btnps, _draws = draws,
  }
end

local function boot(code)
  if not code or code == "" then return nil end
  local env = make_env()
  local fn = load(code, "adv_bench", "t", env)
  if not fn or not pcall(fn) then return nil end
  if type(env._init) == "function" then pcall(env._init) end
  return env
end

-- ============================================================================
-- 1. Paraphrased / Free-form NL (25 pts)
-- ============================================================================
do
  local q = "disegna un quadrato verde colore 11 di 16 per 16, " ..
            "quando premo le frecce accelera fino a 4 e quando lascio frena con attrito 1, " ..
            "cade con gravita 1 e salta di 7 col tasto 5 fermandosi a 150 pixel dal bordo"
  local r = ai.synth(q)
  local env = boot(r.code)
  local ok = false
  if env and type(env._update) == "function" and type(env._draw) == "function" then
    local actor = env.player or env.hero
    if type(actor) == "table" then
      pcall(env._draw)
      local drew16 = false
      for _, rc in ipairs(env._draws.rects) do
        if rc.c == 11 and math.abs((rc.x2 - rc.x1) - 16) <= 1 and math.abs((rc.y2 - rc.y1) - 16) <= 1 then
          drew16 = true
        end
      end
      env._btns[1] = true
      for _ = 1, 10 do pcall(env._update) end
      local max_vx = actor.vx
      env._btns[1] = false
      for _ = 1, 30 do pcall(env._update) end
      local floor_y = actor.y
      env._btnps[5] = true
      pcall(env._update)
      env._btnps[5] = false
      local jumped = actor.y < floor_y
      ok = drew16 and max_vx == 4 and floor_y == 150 - 16 and jumped
    end
  end
  check_pts(25, ok, "1. Free-form paraphrased NL (16x16, max=4, btn=5, y=150)")
end

-- ============================================================================
-- 2. Two independent actors + diagonal bounce + AABB bounce (25 pts)
-- ============================================================================
do
  local q = "crea rettangolo paddle colore 7 larg 40 alt 6 con accelerazione 2 attrito 1 max 6, " ..
            "crea palla rimbalzante ball colore 10 larg 6 alt 6 velocita 2, rimbalza su paddle"
  local r = ai.synth(q)
  local env = boot(r.code)
  local ok = false
  if env and type(env.paddle) == "table" and type(env.ball) == "table" then
    env._btns[1] = true
    pcall(env._update)
    local pad_moved = env.paddle.x > 40
    env._btns[1] = false
    -- place ball falling onto paddle
    env.ball.x = env.paddle.x + 10
    env.ball.y = env.paddle.y - 5
    env.ball.vy = 2
    pcall(env._update)
    local bounced = env.ball.vy < 0
    ok = pad_moved and bounced
  end
  check_pts(25, ok, "2. Multi-entity: paddle + bouncing ball collision")
end

-- ============================================================================
-- 3. Moving platform + actor landing on platform (25 pts)
-- ============================================================================
do
  local q = "crea rettangolo hero colore 11 larg 12 alt 14, gravita 1 salto 6 con tasto 4 pavimento y 170, " ..
            "piattaforma mobile plat colore 9 larg 48 alt 8 velocita 2 min 60 max 220, atterra su plat"
  local r = ai.synth(q)
  local env = boot(r.code)
  local ok = false
  if env and type(env.hero) == "table" and type(env.plat) == "table" then
    local px0 = env.plat.x
    env.hero.x = env.plat.x + 10
    env.hero.y = env.plat.y - 20
    env.hero.vy = 2
    for _ = 1, 15 do pcall(env._update) end
    local plat_moved = env.plat.x ~= px0
    local landed_on_plat = math.abs(env.hero.y - (env.plat.y - env.hero.h)) <= 2 and env.hero.on_ground == true
    ok = plat_moved and landed_on_plat
  end
  check_pts(25, ok, "3. Moving platform + actor landing on plat")
end

-- ============================================================================
-- 4. Paraphrased INT8 Genre Routing (25 pts)
-- ============================================================================
do
  local cases = {
    { "voglio costruire un cabinato con navicella che spara raggi laser alle ondate nemiche", "genre.shmup" },
    { "vorrei un giochino stile arkanoid con barra che respinge la sfera contro i mattoncini", "genre.breakout" },
  }
  local passed = 0
  for _, c in ipairs(cases) do
    local hits = ai.ask(c[1], 3)
    local best = hits and hits[1]
    if best and best.id == c[2] then
      local s = ai.synth(c[1], { entry = best.id, target = "lua" })
      local env = boot(s and s.code)
      if env and pcall(env._update) and pcall(env._draw) then
        passed = passed + 1
      end
    end
  end
  check_pts(25, passed == 2, string.format("4. Paraphrased INT8 genre routing (%d/2)", passed))
end

print("========================================")
print("  bm ai.synth Advanced Benchmark")
print("========================================")
for _, line in ipairs(results) do print(line) end
print("----------------------------------------")
print(string.format("TOTAL SCORE: %d / %d (%.1f%%)", total_pts, max_pts, (total_pts * 100.0) / max_pts))
print("========================================")
