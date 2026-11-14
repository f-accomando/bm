-- Out-of-distribution End-to-End Benchmark: Playable SMB-like .bm cartridge (0..100)
-- Run: build/host/luaai build/assist.bin tests/ai/synth_smb_bench.lua

local PROMPT = "crea un gioco platform tipo super mario bros con geometrie colorate: " ..
               "livello a blocchi con tubi e buchi, nemici che pattugliano e si schiacciano dall'alto, " ..
               "monete, bandiera finale, camera scrolling, vite e punteggio"

local total_pts, max_pts = 0, 0
local results = {}

local function check_pts(pts, ok, label, info)
  max_pts = max_pts + pts
  if ok then total_pts = total_pts + pts end
  results[#results + 1] = string.format("[%s] %-48s (%d/%d)%s",
    ok and "PASS" or "FAIL", label, ok and pts or 0, pts, info and (" -- " .. info) or "")
end

local function make_bm_env()
  local btns, btnps = {}, {}
  local draws = { cls = 0, rectfill = 0, circfill = 0, texts = {} }
  local env = {
    math = math, table = table, string = string, ipairs = ipairs, pairs = pairs,
    tonumber = tonumber, tostring = tostring, type = type,
    abs = math.abs, min = math.min, max = math.max, floor = math.floor,
    btn = function(b) return btns[b] or false end,
    btnp = function(b) return btnps[b] or false end,
    cls = function(c) draws.cls = draws.cls + 1 end,
    rectfill = function(x1, y1, x2, y2, c)
      draws.rectfill = draws.rectfill + 1
    end,
    rect = function(x1, y1, x2, y2, c)
      draws.rectfill = draws.rectfill + 1
    end,
    circfill = function(x, y, r, c)
      draws.circfill = draws.circfill + 1
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

print("Prompt (" .. #PROMPT .. " B): " .. PROMPT)
local r = ai.synth(PROMPT, { target = "lua" })
print(string.format("ai.synth ok=%s target=%s us=%s code_bytes=%d",
  tostring(r.ok), tostring(r.target), tostring(r.us), #(r.code or "")))
if r.goal then print("Goal: " .. r.goal) end
if r.err then print("Err: " .. r.err) end

-- 1. Valid .bm cartridge (10 pts)
local code = r.code or ""
local chunk, load_err = load(code, "smb_cart", "t", make_bm_env())
local bm_ok, bm_size = false, 0
if chunk and code ~= "" then
  local tmp_lua = os.tmpname() .. ".lua"
  local tmp_bm = os.tmpname() .. ".bm"
  local f = assert(io.open(tmp_lua, "w"))
  f:write(code .. "\n")
  f:close()
  local ok = os.execute("python3 core/scripts/mkbm.py -o " .. tmp_bm .. " --lua " .. tmp_lua .. " --title \"SMB Bench\" --res 320x180 > /dev/null 2>&1")
  if ok == true or ok == 0 then
    local bf = io.open(tmp_bm, "rb")
    if bf then
      local hdr = bf:read(6)
      bf:seek("end")
      bm_size = bf:seek() or 0
      bf:close()
      bm_ok = (hdr == "BMCART" and bm_size > 64)
    end
  end
  os.remove(tmp_lua)
  os.remove(tmp_bm)
end
check_pts(10, chunk ~= nil and bm_ok, "1. Valid Lua + mkbm.py .bm cartridge",
  bm_ok and string.format("%d bytes", bm_size) or tostring(load_err or "mkbm failed"))

-- Boot cartridge in sandbox if loadable
local env = make_bm_env()
local booted = false
if code ~= "" then
  local fn = load(code, "smb_cart", "t", env)
  if fn then
    local ok = pcall(fn)
    booted = ok and type(env._init) == "function" and type(env._update) == "function" and type(env._draw) == "function"
  end
end

-- 2. Game lifecycle & geometric rendering (15 pts)
local render_ok = false
if booted then
  pcall(env._init)
  env._draws.cls, env._draws.rectfill, env._draws.circfill, env._draws.texts = 0, 0, 0, {}
  local ok1 = pcall(env._update)
  local ok2 = pcall(env._draw)
  render_ok = ok1 and ok2 and env._draws.cls >= 1 and (env._draws.rectfill + env._draws.circfill) >= 5 and #env._draws.texts >= 1
end
check_pts(15, render_ok, "2. _init/_update/_draw + geometric rendering",
  booted and string.format("cls=%d shapes=%d hud=%d", env._draws.cls, env._draws.rectfill + env._draws.circfill, #env._draws.texts) or "missing lifecycle fns")

-- 3. Platformer X/Y physics (gravity + jump + solid blocks) (20 pts)
local phys_ok = false
if booted and type(env.player) == "table" and type(env.player.y) == "number" then
  pcall(env._init)
  local y0 = env.player.y
  -- settle on ground
  for _ = 1, 30 do pcall(env._update) end
  local yg = env.player.y
  -- jump
  env._btnps[4] = true
  env._btns[4] = true
  pcall(env._update)
  env._btnps[4] = false
  env._btns[4] = false
  local min_y = env.player.y
  for _ = 1, 20 do
    pcall(env._update)
    if env.player.y < min_y then min_y = env.player.y end
  end
  for _ = 1, 40 do pcall(env._update) end
  local y_land = env.player.y
  phys_ok = (min_y < yg - 10) and (math.abs(y_land - yg) <= 4)
end
check_pts(20, phys_ok, "3. Platformer X/Y physics (gravity + jump + land)")

-- 4. Level, camera scrolling & pit hazard (15 pts)
local cam_ok = false
if booted and type(env.player) == "table" then
  pcall(env._init)
  for _ = 1, 10 do pcall(env._update) end
  local x0 = env.player.x or 0
  local c0 = env.cam_x or 0
  env._btns[1] = true -- hold right
  for _ = 1, 90 do
    if (env.player.vx or 0) == 0 and env.player.on_ground then
      env._btnps[4] = true
    end
    pcall(env._update)
    env._btnps[4] = false
  end
  env._btns[1] = false
  local x1 = env.player.x or 0
  local c1 = env.cam_x or 0
  cam_ok = (x1 > x0 + 40) and (c1 > c0)
end
check_pts(15, cam_ok, "4. Level traversal + camera scrolling")

-- 5. Enemies: patrol + stomp vs side damage (15 pts)
local enemy_ok = false
if booted and type(env.enemies) == "table" and #env.enemies >= 1 and type(env.player) == "table" then
  pcall(env._init)
  local e1 = env.enemies[1]
  local ex0 = e1.x
  for _ = 1, 20 do pcall(env._update) end
  local moved = (e1.x ~= ex0)
  -- place player above enemy falling down (stomp)
  local sc0 = env.score or 0
  env.player.x = e1.x
  env.player.y = e1.y - 12
  env.player.vy = 4
  pcall(env._update)
  local stomped = (not e1.alive) or ((env.score or 0) > sc0)
  enemy_ok = moved and stomped
end
check_pts(15, enemy_ok, "5. Enemies: patrol + stomp from above")

-- 6. Objectives & Game Loop (coins, flag/win, lives) (15 pts)
local loop_ok = false
if booted and type(env.player) == "table" and type(env.coins) == "table" then
  pcall(env._init)
  local sc0 = env.score or 0
  if #env.coins >= 1 then
    env.player.x = env.coins[1].x
    env.player.y = env.coins[1].y
    pcall(env._update)
  end
  local got_coin = (env.score or 0) > sc0
  local has_flag = (type(env.flag_x) == "number" and env.flag_x > 100)
  if has_flag then
    env.player.x = env.flag_x
    pcall(env._update)
  end
  local won = (env.state == "win")
  loop_ok = got_coin and has_flag and won
end
check_pts(15, loop_ok, "6. Objectives: coins + flagpole win + state")

-- 7. 300-frame stability simulation (10 pts)
local sim_ok = false
if booted then
  pcall(env._init)
  local ok_all = true
  for f = 1, 300 do
    env._btns[1] = (f % 120) < 90
    env._btnps[4] = (f % 45 == 1)
    env._btns[4] = (f % 45 < 10)
    local u_ok = pcall(env._update)
    local d_ok = pcall(env._draw)
    if not u_ok or not d_ok then ok_all = false; break end
  end
  sim_ok = ok_all and env._draws.rectfill > 300
end
check_pts(10, sim_ok, "7. 300-frame simulation without errors")

print("\n========================================")
print("  bm ai.synth SMB-like .bm Benchmark")
print("========================================")
for _, line in ipairs(results) do print(line) end
print("----------------------------------------")
print(string.format("TOTAL SCORE: %d / %d (%.1f%%)", total_pts, max_pts, (total_pts * 100.0) / max_pts))
print("========================================")
