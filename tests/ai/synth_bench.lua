-- Multi-level scored benchmark (0..100) for ai.synth (Lua 5.4 & .b / BML)
-- Run: build/host/luaai build/assist.bin tests/ai/synth_bench.lua

local function bml_check(src)
  if not src or src == "" then return false, "empty" end
  local tmp = os.tmpname() .. ".b"
  local f = assert(io.open(tmp, "w"))
  f:write(src .. "\n")
  f:close()
  local ok = os.execute("python3 core/tools/bml/bml.py " .. tmp .. " --check > /dev/null 2>&1")
  os.remove(tmp)
  return (ok == true or ok == 0)
end

local function lua_load(src, extra)
  if not src or src == "" then return nil, "empty" end
  return load(src .. (extra and ("\n" .. extra) or ""))
end

local function make_env(opts)
  opts = opts or {}
  local btns = opts.btns or {}
  local btnps = opts.btnps or {}
  local map = opts.map or {}
  local flags = opts.flags or {}
  local env = {
    math = math,
    abs = math.abs,
    min = math.min,
    max = math.max,
    btn = function(b) return btns[b] or false end,
    btnp = function(b) return btnps[b] or false end,
    mget = function(x, y) return map[y * 100 + x] or 0 end,
    fget = function(t, f) return ((flags[t] or 0) & (1 << f)) ~= 0 end,
  }
  return env
end

local function run_lua(src, call_expr, env)
  local fn, err = load(src .. "\nreturn " .. call_expr, "bench", "t", env or make_env())
  if not fn then return false, err end
  local ok, res = pcall(fn)
  return ok, res
end

local total_pts, max_pts = 0, 0
local level_scores = {}

local function begin_level(name)
  return { name = name, pts = 0, max = 0, details = {} }
end

local function score(lv, pts, ok, label)
  lv.max = lv.max + pts
  max_pts = max_pts + pts
  if ok then
    lv.pts = lv.pts + pts
    total_pts = total_pts + pts
  end
  lv.details[#lv.details + 1] = string.format("  [%s] %s (%d/%d)", ok and "PASS" or "FAIL", label, ok and pts or 0, pts)
end

local function end_level(lv)
  level_scores[#level_scores + 1] = lv
end

-- ============================================================================
-- LEVEL 1: Single patterns & BML/Lua parity (15 pts)
-- ============================================================================
local l1 = begin_level("L1: Single patterns & BML/Lua parity")

do
  local rl = ai.synth("(move-4dir hero 3 1)", { target = "lua" })
  local rb = ai.synth("(move-4dir hero 3 1)", { target = "bml" })
  local env = make_env({ btns = { [1] = true }, map = { [0] = 0 } })
  local ok, res = run_lua(rl.code or "", "move_hero(0, 0)", env)
  score(l1, 2, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 3, "move-4dir lua+bml+exec")
end

do
  local rl = ai.synth("(move-8dir ship 4)", { target = "lua" })
  local rb = ai.synth("(move-8dir ship 4)", { target = "bml" })
  local env = make_env({ btns = { [1] = true, [3] = true } })
  local ok, res = run_lua(rl.code or "", "move8_ship(10, 10)", env)
  score(l1, 2, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 14, "move-8dir lua+bml+exec")
end

do
  local rl = ai.synth("(tile-wall hero 8 1)", { target = "lua" })
  local rb = ai.synth("(tile-wall hero 8 1)", { target = "bml" })
  local env = make_env({ map = { [102] = 5 }, flags = { [5] = 2 } })
  local ok, res = run_lua(rl.code or "", "blocked_hero(16, 8)", env)
  score(l1, 2, rl.ok and rb.ok and bml_check(rb.code) and ok and res == true, "tile-wall lua+bml+exec")
end

do
  local rl = ai.synth("(aabb-overlap enemy 16 12)", { target = "lua" })
  local rb = ai.synth("(aabb-overlap enemy 16 12)", { target = "bml" })
  local ok1, r1 = run_lua(rl.code or "", "hit_enemy(10, 10, 20, 15)")
  local ok2, r2 = run_lua(rl.code or "", "hit_enemy(10, 10, 30, 15)")
  score(l1, 2, rl.ok and rb.ok and bml_check(rb.code) and ok1 and r1 == true and ok2 and r2 == false, "aabb-overlap lua+bml+exec")
end

do
  local rl = ai.synth("(platform-jump hero 1 7)", { target = "lua" })
  local rb = ai.synth("(platform-jump hero 1 7)", { target = "bml" })
  local env = make_env({ btnps = { [4] = true } })
  local ok1, r1 = run_lua(rl.code or "", "jump_hero(0, true)", env)
  local ok2, r2 = run_lua(rl.code or "", "jump_hero(2, false)", make_env())
  score(l1, 2, rl.ok and rb.ok and bml_check(rb.code) and ok1 and r1 == -7 and ok2 and r2 == 3, "platform-jump lua+bml+exec")
end

do
  local rl = ai.synth("(cooldown shot 15)", { target = "lua" })
  local rb = ai.synth("(cooldown shot 15)", { target = "bml" })
  local ok1, r1 = run_lua(rl.code or "", "tick_shot(5, false)")
  local ok2, r2 = run_lua(rl.code or "", "tick_shot(0, true)")
  score(l1, 2, rl.ok and rb.ok and bml_check(rb.code) and ok1 and r1 == 4 and ok2 and r2 == 15, "cooldown lua+bml+exec")
end

do
  local rl = ai.synth("(clamp-fn val 0 100)", { target = "lua" })
  local rb = ai.synth("(clamp-fn val 0 100)", { target = "bml" })
  local ok, res = run_lua(rl.code or "", "clamp_val(150)", make_env())
  score(l1, 1, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 100, "clamp-fn lua+bml+exec")
end

do
  local rl = ai.synth("(grid-index map 16)", { target = "lua" })
  local rb = ai.synth("(grid-index map 16)", { target = "bml" })
  local ok, res = run_lua(rl.code or "", "idx_map(3, 2)", make_env())
  score(l1, 1, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 36, "grid-index lua+bml+exec")
end

do
  local rl = ai.synth("(entity-loop shot 4)", { target = "lua" })
  local rb = ai.synth("(entity-loop shot 4)", { target = "bml" })
  local ok, res = run_lua(rl.code or "", "step_shot({1, 0, 2, 0}, {0, 0, 0, 0})", make_env())
  score(l1, 1, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 2, "entity-loop lua+bml+exec")
end

end_level(l1)

-- ============================================================================
-- LEVEL 2: Multi-intent NL & disambiguated slots (20 pts)
-- ============================================================================
local l2 = begin_level("L2: Multi-intent NL & slot extraction")

do
  local r = ai.synth("muovi hero con frecce velocita 4 flag 2, poi salto gravita 1 salto 8 in .b")
  local ok = r.ok and r.target == "bml" and bml_check(r.code)
    and r.code:find("move_hero", 1, true) ~= nil
    and r.code:find("jump_hero", 1, true) ~= nil
    and r.code:find("+ 4", 1, true) ~= nil
    and r.code:find("~= 2", 1, true) ~= nil
    and r.code:find("-8", 1, true) ~= nil
  score(l2, 5, ok, "NL IT 2-intent (move+jump) with distinct slots")
end

do
  local r = ai.synth("sparo cooldown 15 frame e collisione aabb enemy larg 24 alt 12")
  local ok = r.ok and r.target == "lua" and lua_load(r.code) ~= nil
    and r.code:find("tick_", 1, true) ~= nil
    and r.code:find("return 15", 1, true) ~= nil
    and r.code:find("hit_enemy", 1, true) ~= nil
    and r.code:find("< 24", 1, true) ~= nil
    and r.code:find("< 12", 1, true) ~= nil
  score(l2, 5, ok, "NL IT 2-intent (cooldown+aabb) with distinct slots")
end

do
  local r = ai.synth("muovi entita knight velocita 3 muri flag 1, limita bordo, griglia col 20 e pool particelle count 32 in .b")
  local ok = r.ok and r.target == "bml" and bml_check(r.code)
    and r.code:find("move_knight", 1, true) ~= nil
    and r.code:find("clamp_", 1, true) ~= nil
    and r.code:find("idx_", 1, true) ~= nil
    and r.code:find("* 20", 1, true) ~= nil
    and r.code:find("step_", 1, true) ~= nil
    and r.code:find("1, 32", 1, true) ~= nil
  score(l2, 5, ok, "NL IT 4-intent + custom name 'knight' in .b")
end

do
  local r = ai.synth("move player speed 5, jump grav 2 jump 9, shoot delay 10, hitbox boss w 32 h 24")
  local ok = r.ok and r.target == "lua" and lua_load(r.code) ~= nil
    and r.code:find("move_player", 1, true) ~= nil
    and r.code:find("jump_", 1, true) ~= nil
    and r.code:find("tick_", 1, true) ~= nil
    and r.code:find("hit_boss", 1, true) ~= nil
  score(l2, 5, ok, "NL EN 4-intent (move+jump+cooldown+aabb)")
end

end_level(l2)

-- ============================================================================
-- LEVEL 3: Intrinsic AST constructs & nested orchestration (25 pts)
-- ============================================================================
local l3 = begin_level("L3: Intrinsic constructs & nested orchestration")

do
  local ast = [[(fn classify ((v i32)) i32
    (cond ((binop < v 0) ((return -1)))
          ((binop == v 0) ((return 0)))
          (else ((return 1)))))]]
  local rl = ai.synth(ast, { target = "lua" })
  local rb = ai.synth(ast, { target = "bml" })
  local ok1, r1 = run_lua(rl.code or "", "classify(-5)")
  local ok2, r2 = run_lua(rl.code or "", "classify(0)")
  local ok3, r3 = run_lua(rl.code or "", "classify(7)")
  score(l3, 5, rl.ok and rb.ok and bml_check(rb.code) and ok1 and r1 == -1 and ok2 and r2 == 0 and ok3 and r3 == 1,
    "cond / elseif multi-branch lua+bml+exec")
end

do
  local ast = [[(fn fill_step ((arr i32[]) (n i32)) i32
    (var sum i32 0)
    (for-step i 1 n 2
      ((aset arr i (binop * i 10))
       (set sum (binop + sum (aref arr i)))))
    (return sum))]]
  local rl = ai.synth(ast, { target = "lua" })
  local rb = ai.synth(ast, { target = "bml" })
  local ok, res = run_lua(rl.code or "", "fill_step({0, 0, 0, 0, 0}, 5)")
  score(l3, 5, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 90,
    "for-step + aref/aset lua+bml+exec")
end

do
  local ast = [[(fn pack_flags ((a i32) (b i32)) i32
    (var hi i32 (binop << (binop & a 15) 4))
    (var lo i32 (binop & b 15))
    (return (binop | hi lo)))]]
  local rl = ai.synth(ast, { target = "lua" })
  local rb = ai.synth(ast, { target = "bml" })
  local ok, res = run_lua(rl.code or "", "pack_flags(3, 9)")
  score(l3, 5, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 57,
    "bitwise ops (<<, &, |) lua+bml+exec")
end

do
  local rb = ai.synth("(wired-actor hero 3 0 240)", { target = "bml" })
  local rl = ai.synth("(wired-actor hero 3 0 240)", { target = "lua" })
  local bml_ok = rb.ok and bml_check(rb.code)
    and rb.code:find("clamp_hero", 1, true) ~= nil
    and rb.code:find("step_hero", 1, true) ~= nil
  local env = make_env({ btns = { [1] = true } })
  local ok1, r1 = run_lua(rl.code or "", "step_hero(239, 10)", env)
  local ok2, r2 = run_lua(rl.code or "", "step_hero(10, 10)", env)
  score(l3, 10, bml_ok and rl.ok and ok1 and r1 == 240 and ok2 and r2 == 13,
    "nested wired-actor (step_hero calls move_hero & clamp_hero)")
end

end_level(l3)

-- ============================================================================
-- LEVEL 4: Synthesis-by-example (15 pts)
-- ============================================================================
local l4 = begin_level("L4: Synthesis-by-example (solve-expr)")

do
  local rl = ai.synth("(solve-expr 'lua '((0 4) (1 12) (2 20)) 'x)")
  local rb = ai.synth("(solve-expr 'bml '((0 4) (1 12) (2 20)) 'x)")
  local ok, res = run_lua(rl.code or "", "calc(5)")
  score(l4, 5, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 44, "solve-expr linear 8*x+4")
end

do
  local rl = ai.synth("(solve-expr 'lua '((0 10) (1 7) (2 4)) 'x)")
  local rb = ai.synth("(solve-expr 'bml '((0 10) (1 7) (2 4)) 'x)")
  local ok, res = run_lua(rl.code or "", "calc(3)")
  score(l4, 5, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 1, "solve-expr negative -3*x+10")
end

do
  local rl = ai.synth("(solve-expr 'lua '((0 1) (1 2) (2 5) (3 10)) 'x)")
  local rb = ai.synth("(solve-expr 'bml '((0 1) (1 2) (2 5) (3 10)) 'x)")
  local ok, res = run_lua(rl.code or "", "calc(4)")
  score(l4, 5, rl.ok and rb.ok and bml_check(rb.code) and ok and res == 17, "solve-expr quadratic x*x+1")
end

end_level(l4)

-- ============================================================================
-- LEVEL 5: Large End-to-End Task (25 pts)
-- ============================================================================
local l5 = begin_level("L5: Large End-to-End Platformer Module")

local e2e_prompt = "crea controller platformer hero: muovi con frecce velocita 3 muri flag 1, salto gravita 1 salto 7, sparo bullet cooldown 12, collisione aabb enemy larg 16 alt 16, limita bordo, griglia col 16, pool bullet count 16, patrol enemy"

local rb = ai.synth(e2e_prompt .. " in .b")
local rl = ai.synth(e2e_prompt, { target = "lua" })

local expected_fns = {
  "move_hero", "jump_hero", "tick_bullet", "hit_enemy",
  "clamp_hero", "idx_", "step_bullet", "patrol_enemy"
}
local found = 0
if rb.ok and rb.code then
  for _, fn in ipairs(expected_fns) do
    if rb.code:find(fn, 1, true) then found = found + 1 end
  end
end
score(l5, 10, found >= 7, string.format("E2E function coverage (%d/%d expected)", found, #expected_fns))
score(l5, 5, rb.ok and bml_check(rb.code), "E2E BML module passes bml.py --check")

local sim_ok = false
if rl.ok and rl.code then
  local env = make_env({ btns = { [1] = true }, btnps = { [4] = true } })
  local fn = load(rl.code .. [[
    local x, y, vy, cd = 10, 100, 0, 0
    local xs = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    local vxs = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    for f = 1, 60 do
      x = move_hero(x, y)
      x = clamp_hero(x)
      vy = jump_hero(vy, f == 1)
      cd = tick_bullet(cd, f % 15 == 1)
      local _ = hit_enemy(x, y, 40, 100)
      local _ = step_bullet(xs, vxs)
    end
    return x
  ]], "e2e", "t", env)
  if fn then
    local ok, res = pcall(fn)
    sim_ok = ok and type(res) == "number" and res > 10
  end
end
score(l5, 10, sim_ok, "E2E Lua module executes 60-frame simulation")

end_level(l5)

-- ============================================================================
-- Report
-- ============================================================================
print("========================================")
print("  bm ai.synth Multi-Level Benchmark")
print("========================================")
for _, lv in ipairs(level_scores) do
  print(string.format("%s: %d / %d", lv.name, lv.pts, lv.max))
  for _, d in ipairs(lv.details) do print(d) end
end
print("----------------------------------------")
print(string.format("TOTAL SCORE: %d / %d (%.1f%%)", total_pts, max_pts, (total_pts * 100.0) / max_pts))
print("========================================")
