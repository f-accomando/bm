-- Tests for the Lisp symbolic code synthesizer (ai.synth) -> .b (BML) & Lua 5.4
local fails, checks = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then
    fails = fails + 1
    print("FAIL: " .. msg)
  end
end

-- 1. Direct S-expression synthesis (Lua)
local r1 = ai.synth("(move-4dir hero 3 2)", { target = "lua" })
check(r1.ok, "move-4dir lua ok: " .. tostring(r1.err))
r1.code = r1.code or ""
check(r1.code:find("function move_hero(x, y)", 1, true) ~= nil, "move-4dir lua sig")
check(r1.code:find("fget(mget", 1, true) ~= nil, "move-4dir lua wall check")
local fn1, err1 = load(r1.code)
check(fn1 ~= nil, "move-4dir lua compiles: " .. tostring(err1))

-- 2. Direct S-expression synthesis (.b / BML)
local r2 = ai.synth("(move-4dir hero 3 2)", { target = "bml" })
check(r2.ok, "move-4dir bml ok")
check(r2.code:find("function move_hero(x: i32, y: i32, grid: i32[], cols: i32): i32", 1, true) ~= nil, "move-4dir bml typed sig")
check(r2.code:find("local nx: i32 = x", 1, true) ~= nil, "move-4dir bml typed local")

-- 3. Example-driven linear expression solver (micro-solver)
local r3 = ai.synth("(solve-expr 'lua '((0 4) (1 12) (2 20)) 'x)")
check(r3.ok, "solve-expr lua ok: " .. tostring(r3.err))
r3.code = r3.code or ""
if r3.ok then
  local fn3 = assert(load(r3.code .. "\nreturn calc(5)"))
  check(fn3() == 44, "solve-expr calc(5) == 44 (8*5+4)")
end

local r4 = ai.synth("(solve-expr 'bml '((0 4) (1 12) (2 20)) 'x)")
check(r4.ok and r4.code:find("function calc(x: i32): i32", 1, true) ~= nil, "solve-expr bml typed")

-- 4. Hybrid Italian/English natural language -> slot extraction -> Lisp -> code
local r5 = ai.synth("muovi hero con frecce velocita 4 flag 2 in .b", { entry = "move_arrows" })
check(r5.ok and r5.target == "bml", "hybrid NL detects .b target: " .. tostring(r5.target))
check(r5.goal == "(move-4dir hero 4 2)", "hybrid NL extracts slots: " .. tostring(r5.goal))
check(r5.code:find("move_hero(x: i32, y: i32, grid: i32[], cols: i32): i32", 1, true) ~= nil, "hybrid NL bml code")

local r6 = ai.synth("collisione aabb enemy larg 24 alt 12")
check(r6.ok and r6.target == "lua", "hybrid NL aabb lua")
check(r6.code:find("abs((ax - bx)) < 24", 1, true) ~= nil, "hybrid NL aabb width")
local fn6 = assert(load(r6.code .. "\nreturn hit_enemy(10, 10, 20, 15)"))
check(fn6() == true, "hybrid NL aabb executes true")

local r7 = ai.synth("(compose (cooldown shot 15) (platform-jump hero 1 7))", { target = "bml" })
check(r7.ok, "compose bml ok")
check(r7.code:find("tick_shot", 1, true) ~= nil and r7.code:find("jump_hero", 1, true) ~= nil, "compose emits both")

-- 5. Verify all generated .b snippets with core/tools/bml/bml.py --check
local bml_samples = { r2.code, r4.code, r5.code, r7.code }
for idx, src in ipairs(bml_samples) do
  local tmp = os.tmpname() .. ".b"
  local f = assert(io.open(tmp, "w"))
  f:write(src .. "\n")
  f:close()
  local ok = os.execute("python3 core/tools/bml/bml.py " .. tmp .. " --check > /dev/null 2>&1")
  os.remove(tmp)
  check(ok == true or ok == 0, "bml.py --check sample #" .. idx)
end

print(string.format("synth_test: %d checks, %d failed (last=%d us)", checks, fails, r7.us or 0))
if fails > 0 then os.exit(1) end
