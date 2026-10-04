-- bm SDK: the hub of a .bm project, in the bm Suite. The other programs of
-- the suite open on the same file (1-6 on the project page: bm Code, bm
-- Pixel, bm Studio, bm Animator, bm Mesh, bm Sound) and come back to it.
--
-- F1 project (F1 again: the dev kit), F2 code, F3 2D (sprites, with the
-- flags of their tiles on 0-7; F3 again: the map, its layers on l / L), F4
-- 3D (models, animations, their code), Esc menu. Ctrl+N a new
-- project from a template (2D and 3D), Ctrl+S save, F5 (Ctrl+R) try it: the
-- dev kit keeps the numbers of the run (fps, ms, memory, tokens). F6 the
-- assistant: guides to 2D and 3D games on the project page, code, sprites
-- and models on the others. Hold F12 for the keys. On a gamepad: Y +
-- left/right the page, Y + up/down the view, Y + B the menu, Y + X the
-- assistant.

local B = require "bm3d"                 -- the suite's colours, lists, 3D view, MESH and ANIM
local ok_assist, assist = pcall(require, "assist")
if not ok_assist then assist = nil end

local C = B.C
local W, H = SCREEN_W, SCREEN_H
local HINT_Y, STATUS_Y = B.HINT_Y, B.STATUS_Y
local PANEL_W = 168                      -- the left panel, as bm Studio and bm Animator
local INFO_X = PANEL_W + 8               -- the text right of the panel
local ROWS = (HINT_Y - 16) // 16         -- text rows between the tab bar and the hint row
local max, min, floor = math.max, math.min, math.floor
local spack, sunpack = string.pack, string.unpack
local C_KEY = 0xFFC050                   -- the key of a row in the panel

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end
local function snap(x) return (x + 7) // 8 * 8 end   -- text stays on its 8 px columns
local function pad_used() local li = lastinput(); return li == "ds4" or li == "pad" end

-- keys as chips (prompt(); "ctrl g" is two), or the pad's button when a pad
-- was used last and the action has one; then the label. Returns the x after.
local function chips(keys, x, y)
  for k in keys:gmatch("%S+") do
    if y then x = prompt(k, x, y) + 1 else x = x + prompt(k) + 1 end
  end
  return x
end

local function chip_hint(key, pad, label, x, y, c)
  x = chips(pad and pad_used() and pad or key, x, y)
  return print(label, snap(x + 2), y, c or C.DIM) + 12
end

-- the hint row: { {keys, pad or nil, label}, ... } while it fits
local function hint(list, y)
  y = y or HINT_Y
  local x = 8
  for _, h in ipairs(list) do
    local k = h[2] and pad_used() and h[2] or h[1]
    if snap(chips(k, x) + 2) + #h[3] * 8 > W then break end
    x = chip_hint(h[1], h[2], h[3], x, y)
  end
end

-- "512k", "1.5M": KiB in a few columns, as the dev kit's overlay
local function kib(k)
  if not k then return "-" end
  if k == 0 then return "0k" end
  if k < 10 then return string.format("%.1fk", k) end
  if k < 1000 then return floor(k + 0.5) .. "k" end
  if k < 10240 then return string.format("%.1fM", k / 1024) end
  return floor(k / 1024) .. "M"
end

----------------------------------------------------------------- state

local S = {
  page = "project", last = "project",    -- project, code, d2, d3, menu
  hubview = "hub", d2view = "sprite",     -- F1 again: the dev kit; F3 again: the map
  proj = { title = "New game", author = "", res = "640x360" },   -- path: opened; save: 8.3 written
  target = "bm",                          -- or "b16": the cartridge for the handhelds (docs/B16.md)
  lines = { "" }, edits = 0, dirty = false,
  msg = nil, msg_c = C.TEXT, msg_t = 0, frame = 0,
  err_text = nil, err_line = nil,
  run = nil,                              -- cart_arg().run: the dev kit's numbers of the last try
  sheet_w = 256, sheet_h = 256, map_w = 256, map_h = 256,
  info = nil, measuring = nil,            -- the project's numbers, and the coroutine measuring them
}

local P = {}                             -- the pages: project, code, sprite, map, d3, menu

local function say(s, c, t) S.msg, S.msg_c, S.msg_t = s, c or C.TEXT, t or 200 end
-- a page, and the one Esc's menu goes back to
local function show(p)
  S.page = p
  if p ~= "menu" then S.last = p end
end

-- the pad: held frames and repeated presses of each button (read each frame)
local PAD = { held = {}, rp = {} }
local function read_pad()
  for i = 0, 7 do
    local h = btn(i) and (PAD.held[i] or 0) + 1 or 0
    PAD.held[i] = h
    PAD.rp[i] = h == 1 or (h > 14 and h % 4 == 0)
  end
end
-- a change: the numbers of the project page are measured again
local function touched() S.dirty, S.info, S.measuring = true, nil, nil end
local function code_touched() S.edits = S.edits + 1; touched() end

local function split_lines(s)
  local out = {}
  s = s:gsub("\r\n", "\n")
  for l in (s .. "\n"):gmatch("(.-)\n") do out[#out + 1] = l:gsub("\t", "  ") end
  if #out > 1 and out[#out] == "" then out[#out] = nil end
  if #out == 0 then out[1] = "" end
  return out
end

local function code_text() return table.concat(S.lines, "\n") .. "\n" end

-- the tokens of the code (code_tokens(), the same count as the overlay's),
-- measured again a while after the last change
local tok = { edits = -1, n = nil, at = 0 }
local function tokens_now()
  if not code_tokens then return nil end
  if tok.edits ~= S.edits and (S.frame - tok.at > 20 or not tok.n) then
    tok.n, tok.edits, tok.at = code_tokens(code_text()), S.edits, S.frame
  end
  return tok.n
end

-- the tool's memory across the programs it opens and the games it tries
local function remember(extra)
  local t = saved() or {}
  t.page, t.hubview, t.d2view = S.page == "menu" and S.last or S.page, S.hubview, S.d2view
  for k, v in pairs(extra or {}) do t[k] = v end
  save(t)
end

----------------------------------------------------------------- templates

-- the art of the templates: rows of letters, one per pixel ("." transparent)
local ART = {
  hero = { "..yyyy..", ".yyyyyy.", "yykyykyy", "yyyyyyyy", "yykkkkyy", ".yyyyyy.", "..y..y..", ".kk..kk." },
  ground = { "gggggggg", "gGggggGg", "bggbbggb", "bbbbbbbb", "bbbdbbbb", "bbbbbbdb", "bdbbbbbb", "bbbbbbbb" },
  coin = { "........", "..oooo..", ".ooYYoo.", ".oYoooo.", ".oYoooo.", ".oooooo.", "..oooo..", "........" },
  wall = { "ssssssss", "sSSsSSSs", "ssssssss", "SSsSSSsS", "ssssssss", "sSSsSSSs", "ssssssss", "SSsSSSsS" },
  ship = { "........", "cc......", ".cccc...", ".cwwccc.", ".cccccrr", ".cccc...", "cc......", "........" },
  foe = { "...rr...", "..rrrr..", ".rkrrkr.", "rrrrrrrr", "r.rrrr.r", "..r..r..", ".r....r.", "........" },
}
local INK = { y = 0xFFD050, k = 0x202030, g = 0x5CB048, G = 0x8AD860, b = 0x8A5A30, d = 0x6A4224,
              o = 0xF0B030, Y = 0xFFF4C0, s = 0x606878, S = 0x808898, c = 0x70B8F0, w = 0xFFFFFF,
              r = 0xE84A5A }

local function paint(cell, rows)
  local cols = S.sheet_w // 8
  local x0, y0 = (cell % cols) * 8, (cell // cols) * 8
  for j, row in ipairs(rows) do
    for i = 1, 8 do sset(x0 + i - 1, y0 + j - 1, INK[row:sub(i, i)]) end
  end
end

local function res_cells()
  local w, h = S.proj.res:match("(%d+)x(%d+)")
  return tonumber(w) // 8, tonumber(h) // 8
end

local TEMPLATES = {
  { id = "empty", name = "Empty 2D", desc = "a ball that moves: the smallest game", code = [==[
-- my game
local x, y = SCREEN_W / 2, SCREEN_H / 2

function _init()
end

function _update()
  if btn(0) then x = x - 2 end
  if btn(1) then x = x + 2 end
  if btn(2) then y = y - 2 end
  if btn(3) then y = y + 2 end
end

function _draw()
  cls(0x101828)
  circfill(x, y, 12, 0xFFD050)
  print("hello!", SCREEN_W / 2 - 24, 40, 0xFFFFFF)
end
]==] },
  { id = "platform", name = "Platform 2D", desc = "run and jump on the map, the camera follows",
    paint = function()
      paint(1, ART.hero); paint(2, ART.ground)
      local _, ch = res_cells()
      local base = ch - 5
      for x = 0, 127 do
        if not ((x >= 30 and x <= 33) or (x >= 70 and x <= 74)) then
          for y = base, base + 4 do mset(x, y, 2) end
        end
      end
      local plats = { { 12, 6, 6 }, { 24, 10, 6 }, { 40, 5, 7 }, { 52, 9, 7 }, { 80, 6, 10 }, { 95, 12, 6 } }
      for _, p in ipairs(plats) do
        for x = p[1], p[1] + p[3] - 1 do mset(x, base - p[2], 2) end
      end
      for y = 0, base + 4 do mset(127, y, 2) end
    end,
    code = [==[
-- platform: arrows run, A (space) jumps; the map: the SDK's F3 F3
-- sprite 1 is the hero, 2 the ground (the solid tile)
local px, py, vy, ground = 64, 40, 0, false
local camx = 0

local function solid(x, y)
  return mget(x // 8, y // 8) == 2
end

local function free(x, y)
  return not (solid(x, y) or solid(x + 7, y) or
              solid(x, y + 7) or solid(x + 7, y + 7))
end

function _update()
  local vx = 0
  if btn(0) then vx = -2 end
  if btn(1) then vx = 2 end
  if ground and btnp(4) then vy = -5 end
  vy = math.min(vy + 0.25, 6)
  if free(px + vx, py) then px = px + vx end
  local ny = py + vy
  ground = false
  if vy > 0 and (solid(px, ny + 7) or solid(px + 7, ny + 7)) then
    ny = (ny + 7) // 8 * 8 - 8
    vy, ground = 0, true
  elseif vy < 0 and (solid(px, ny) or solid(px + 7, ny)) then
    ny = (ny // 8 + 1) * 8
    vy = 0
  end
  py = ny
  if py > SCREEN_H + 64 then px, py, vy = 64, 40, 0 end   -- fell: again
  camx = math.max(0, px - SCREEN_W // 2)
end

function _draw()
  cls(0x5080C0)
  camera(camx, 0)
  map(0, 0, 0, 0, 128, SCREEN_H // 8 + 1)
  spr(1, px, py)
  camera()
  print("arrows: run   A: jump", 8, 8, 0xFFFFFF)
end
]==] },
  { id = "topdown", name = "Top-down 2D", desc = "walk among walls and pick up the coins",
    paint = function()
      paint(1, ART.hero); paint(3, ART.coin); paint(4, ART.wall)
      local cw, ch = res_cells()
      for x = 0, cw - 1 do mset(x, 2, 4); mset(x, ch - 1, 4) end
      for y = 2, ch - 1 do mset(0, y, 4); mset(cw - 1, y, 4) end
      for y = 8, ch - 9 do mset(cw // 3, y, 4); mset(cw * 2 // 3, ch - y + 1, 4) end
      for k = 1, 24 do
        local x, y = 2 + (k * 37) % (cw - 4), 4 + (k * 23) % (ch - 6)
        if mget(x, y) == 0 then mset(x, y, 3) end
      end
    end,
    code = [==[
-- top-down: arrows walk, pick up the coins; walls and coins: the map
-- sprite 1 is the hero, 3 a coin, 4 a wall
local px, py, score = 40, 48, 0

local function wall(x, y) return mget(x // 8, y // 8) == 4 end

local function free(x, y)
  return not (wall(x, y) or wall(x + 7, y) or
              wall(x, y + 7) or wall(x + 7, y + 7))
end

function _update()
  local dx, dy = 0, 0
  if btn(0) then dx = -2 end
  if btn(1) then dx = 2 end
  if btn(2) then dy = -2 end
  if btn(3) then dy = 2 end
  if free(px + dx, py) then px = px + dx end
  if free(px, py + dy) then py = py + dy end
  local cx, cy = (px + 4) // 8, (py + 4) // 8
  if mget(cx, cy) == 3 then
    mset(cx, cy, 0)
    score = score + 1
  end
end

function _draw()
  cls(0x305030)
  map(0, 0, 0, 0, SCREEN_W // 8, SCREEN_H // 8)
  spr(1, px, py)
  print("coins " .. score, 8, 0, 0xFFE070)
end
]==] },
  { id = "shooter", name = "Shooter 2D", desc = "fly, fire, waves of enemies, game over",
    paint = function() paint(1, ART.ship); paint(5, ART.foe) end,
    code = [==[
-- shooter: arrows fly, A (space) fires; the enemies come from the right
-- sprite 1 is the ship, 5 an enemy
local ship = { x = 40, y = SCREEN_H // 2 }
local shots, foes, t, score, over = {}, {}, 0, 0, false

local function restart()
  ship.y, shots, foes, score, over = SCREEN_H // 2, {}, {}, 0, false
end

function _update()
  if over then
    if btnp(4) then restart() end
    return
  end
  t = t + 1
  if btn(0) then ship.x = math.max(0, ship.x - 3) end
  if btn(1) then ship.x = math.min(SCREEN_W // 2, ship.x + 3) end
  if btn(2) then ship.y = math.max(16, ship.y - 3) end
  if btn(3) then ship.y = math.min(SCREEN_H - 16, ship.y + 3) end
  if btnp(4) then
    shots[#shots + 1] = { x = ship.x + 8, y = ship.y + 3 }
  end
  if t % 40 == 0 then
    local y = math.random(16, SCREEN_H - 24)
    foes[#foes + 1] = { x = SCREEN_W, y = y, s = math.random(2, 4) }
  end
  for i = #shots, 1, -1 do
    local s = shots[i]
    s.x = s.x + 6
    if s.x > SCREEN_W then table.remove(shots, i) end
  end
  for i = #foes, 1, -1 do
    local f = foes[i]
    f.x = f.x - f.s
    for j = #shots, 1, -1 do
      local s = shots[j]
      if s.x < f.x + 8 and s.x + 4 > f.x and
         s.y < f.y + 8 and s.y + 2 > f.y then
        table.remove(shots, j)
        f.dead, score = true, score + 10
      end
    end
    if math.abs(f.x - ship.x) < 7 and math.abs(f.y - ship.y) < 7 then
      over = true
    end
    if f.dead or f.x < -8 then table.remove(foes, i) end
  end
end

function _draw()
  cls(0x080818)
  for i = 0, 40 do
    local x = (i * 97 - t * (1 + i % 3)) % SCREEN_W
    pset(x, i * 37 % SCREEN_H, 0x8090B0)
  end
  spr(1, ship.x, ship.y)
  for _, s in ipairs(shots) do rectfill(s.x, s.y, 4, 2, 0xFFE070) end
  for _, f in ipairs(foes) do spr(5, f.x, f.y) end
  print("score " .. score, 8, 0, 0xFFFFFF)
  if over then
    print("game over - A plays again", SCREEN_W // 2 - 100, SCREEN_H // 2,
          0xFF6060)
  end
end
]==] },
  { id = "scene3d", name = "3D scene", desc = "floor, cubes, a ball with its shadow, a camera that follows",
    code = [==[
-- 3D scene: arrows move the ball, A (space) jumps, the camera follows
local floor, cube, ball
local bx, by, bz, vy, t = 0, 0.5, 0, 0, 0
local boxes = { { -3, 2 }, { 2, 4 }, { 4, -2 }, { -4, -3 } }

function _init()
  floor = mesh({ -10, 0, -10, -10, 0, 10, 10, 0, 10, 10, 0, -10 },
               { 1, 2, 3, 0x3A6A3A, 1, 3, 4, 0x3A6A3A })
  cube = mesh_cube(0xD06040)
  ball = mesh_sphere(0.5, 12, 0xFFD050, 0xE0A030)
end

function _update()
  t = t + 1 / 60
  if btn(0) then bx = bx - 0.1 end
  if btn(1) then bx = bx + 0.1 end
  if btn(2) then bz = bz + 0.1 end
  if btn(3) then bz = bz - 0.1 end
  if btnp(4) and by <= 0.5 then vy = 0.18 end
  vy = vy - 0.01
  by = math.max(0.5, by + vy)
end

function _draw()
  cls(0x101828)
  camera3d(bx, 4, bz - 8, 0, -0.4, 60)
  light3d(-0.4, 0.8, -0.5, 0.45)
  fog3d(0x101828, 12, 26)
  zclear()
  draw3d(floor, 0, 0, 0, 0, 0, 0, 1, 1)   -- flag 1: no z-buffer, first
  for i, b in ipairs(boxes) do
    draw3d(cube, b[1], 0.5, b[2], 0, t * 0.5 + i, 0, 1)
  end
  draw3d(ball, bx, by, bz, 0, 0, 0, 1, 8) -- flag 8: its shadow
  draw3d(ball, bx, by, bz)
  print("arrows: move   A: jump   " .. stat(4) .. " triangles", 8, 8)
end
]==] },
  { id = "models3d", name = "3D with models", desc = "the models of bm Studio, animated (bm Animator)",
    code = [==[
-- 3D models: the models of the project (bm Studio, the SDK's F4) turn on
-- a stand; with a skeleton (bm Animator) they play their first animation.
-- left/right: the model, up/down: the zoom
local list, m, i, t, dist = {}, nil, 1, 0, 6

local function pick(k)
  i = (k - 1) % #list + 1
  m = model(list[i]) or mesh_cube(0xD06040)
  local x0, y0, z0, x1, y1, z1 = bounds3d(m)
  dist = math.max(x1 - x0, y1 - y0, z1 - z0) * 1.8 + 1
  t = 0
end

function _init()
  list = models()
  if #list == 0 then list = { "cube" } end
  pick(1)
end

function _update()
  t = t + 1 / 60
  if btnp(0) then pick(i - 1) end
  if btnp(1) then pick(i + 1) end
  if btn(2) then dist = math.max(1, dist * 0.98) end
  if btn(3) then dist = math.min(60, dist / 0.98) end
end

function _draw()
  cls(0x202838)
  local a = t * 0.6
  camera3d(-math.sin(a) * dist, dist * 0.4, -math.cos(a) * dist,
           a, -0.38, 60)
  light3d(-0.4, 0.8, -0.5, 0.45)
  zclear()
  local cl = clips(m)
  if #cl > 0 then animate(m, cl[1].name, t) end
  local x0, y0, z0, x1, y1, z1 = bounds3d(m)
  draw3d(m, -(x0 + x1) / 2, -y0, -(z0 + z1) / 2)
  print(list[i] .. "  " .. i .. "/" .. #list, 8, 8, 0xFFFFFF)
end
]==] },
}

----------------------------------------------------------------- project

-- "/carts/pong.bm" -> "/carts/PONG.BM"; longer names are cut to 8 characters
local function short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or "/carts"
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):upper():gsub("[^%w_]", "")
  if stem == "" then stem = "GAME" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".BM"
end

local function list_files()
  local out = {}
  for _, dir in ipairs({ "/carts", "/" }) do
    for _, f in ipairs(ls(dir)) do
      if not f.dir and f.name:lower():match("%.bm$") then
        out[#out + 1] = (dir == "/" and "" or dir) .. "/" .. f.name
      end
    end
  end
  table.sort(out)
  return out
end

local reset_pages                        -- (the pages, below)

local function target_of(path)
  local t = saved()
  return t and t.targets and path and t.targets[path:upper()] or "bm"
end

local function load_project(path)
  local p, e = cart_load(path)
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C.ERR); return false end
  S.proj = { title = p.title, author = p.author, res = p.res, path = path, save = short_path(path),
             palette = p.palette }
  S.lines = split_lines(p.lua)
  S.sheet_w, S.sheet_h, S.map_w, S.map_h = p.sheet_w, p.sheet_h, p.map_w, p.map_h
  S.target = target_of(S.proj.save)
  S.dirty, S.edits, S.info, S.measuring = false, S.edits + 1, nil, nil
  S.err_text, S.err_line = nil, nil
  reset_pages()
  say("opened " .. path .. "  (saves as " .. S.proj.save .. ")", C.ACC)
  return true
end

-- a new project from a template: its code, sprites and map
local function new_project(tp)
  tp = tp or TEMPLATES[1]
  cart_new()
  S.sheet_w, S.sheet_h, S.map_w, S.map_h = 256, 256, 256, 256
  S.proj = { title = tp.id == "empty" and "New game" or tp.name, author = "", res = "640x360" }
  S.lines = split_lines(tp.code)
  if tp.paint then tp.paint() end
  S.target = "bm"
  S.dirty, S.edits, S.info, S.measuring = false, S.edits + 1, nil, nil
  S.err_text, S.err_line = nil, nil
  reset_pages()
  if tp.paint then P.sprite.restore({ sel = 1, size = 1 }) end   -- its first sprite, 8x8
  say("new project (" .. tp.name .. "): Esc > Save as gives it a name, F5 tries it", C.ACC)
end

local function save_project()
  if not S.proj.save then return false, "no name yet: use Save as" end
  local ok, e = cart_save(S.proj.save, { title = S.proj.title, author = S.proj.author, res = S.proj.res,
                                         lua = code_text() })
  if ok then
    S.dirty, S.info, S.measuring = false, nil, nil
    S.proj.path = S.proj.path or S.proj.save
    say("saved " .. S.proj.save, C.ACC)
  else
    say("save failed: " .. tostring(e), C.ERR)
  end
  return ok
end

local D = {}                             -- the dialogs: ask (a line of text), choose (a list)

-- the project's target, remembered for its file (the SDK's saved())
local function store_target()
  if not S.proj.save then return end
  local sv = saved() or {}
  sv.targets = sv.targets or {}
  sv.targets[S.proj.save:upper()] = S.target ~= "bm" and S.target or nil
  save(sv)
end

local function save_as(after)
  D.ask("file name (8.3, in /carts)", S.proj.save and S.proj.save:match("([^/]+)$") or "MYGAME.BM", function(t)
    if t == "" then return end
    if not t:upper():match("%.BM$") then t = t .. ".BM" end
    S.proj.save = short_path("/carts/" .. t)
    S.proj.path = nil                    -- (the new file, once written)
    if save_project() then
      store_target()
      if after then after() end
    end
  end)
end

local function save_then(after)
  if not S.proj.save then save_as(after)
  elseif not S.dirty or save_project() then after() end
end

-- tries the game: saved first; the SDK comes back to the same page with the
-- dev kit's numbers of the run (cart_arg().run)
local function run_project()
  save_then(function()
    remember()
    cart_run(S.proj.save)
  end)
end

-- another program of the suite on this file; its menu brings back here
local TOOLS = {
  { key = "1", tool = "code", label = "bm Code", what = "the code, in tabs" },
  { key = "2", tool = "pixel", label = "bm Pixel", what = "the pixel art of the sheet" },
  { key = "3", tool = "studio", label = "bm Studio", what = "3D models from tiles" },
  { key = "4", tool = "animator", label = "bm Animator", what = "skeletons and animations" },
  { key = "5", tool = "mesh", label = "bm Mesh", what = "vertices and faces" },
  { key = "6", tool = "sound", label = "bm Sound", what = "sounds and music" },
}

local function open_in(t)
  if not S.proj.save then say("give the project a name to open it in " .. t.label, C.ACC) end
  save_then(function()
    remember()
    cart_tool(t.tool, S.proj.save)
  end)
end

----------------------------------------------------------------- the dev kit

-- the project's numbers, measured a piece at a time (timeslice) while the
-- project page shows them: the code, the sheet and the map in use, the
-- models, the sound bank, the memory the game's data takes, the file, the
-- biggest functions and, for a .b16, what the format will not have
local B16_LIMIT = 8 * 1024                -- KiB: the cap of a .b16 cartridge (docs/B16.md 5.1)
local B16_CHECKS = {                     -- (whole names: %f, not "mytime(")
  { "%f[%w_]math%.random", "math.random: left out of the sandbox (the same game on every machine, 6.3)" },
  { "%f[%w_.:]time%(", "time(): no clock (2.9); count the ticks instead" },
  { "%f[%w_.:]save%(", "save(): saves are not designed yet (10)" },
  { "%f[%w_.:]ls%(", "ls() and files: no file system (6.3)" },
  { "%f[%w_.:]cart_%w+%(", "cart_*: the tools' functions, not a game's (6.3)" },
  { "%f[%w_.:]udp_%w+%(", "udp: no network from the system (6.7)" },
  { "%f[%w_.:]require%f[^%w_]", "require: never in the sandbox (6.3)" },
  { "%f[%w_.:]load%(", "load(): never in the sandbox (6.3)" },
  { "%f[%w_.:]draw3d%(", "3D: the .b16 machine profile is not written yet (7)" },
  { "%f[%w_.:]screen%(", "screen(): one machine, one resolution (2.6)" },
}

local function measure()
  local I = { lines = #S.lines, bytes = 0, funcs = {}, nfuncs = 0, b16 = {} }
  for _, l in ipairs(S.lines) do I.bytes = I.bytes + #l + 1 end
  I.tokens = tokens_now()
  -- the biggest functions: a top-level function goes on to the next one
  if code_tokens then
    local start, name = nil, nil
    local function close(last)
      if start then
        I.funcs[#I.funcs + 1] = { name, code_tokens(table.concat(S.lines, "\n", start, last)) }
      end
    end
    for i, l in ipairs(S.lines) do
      local n = l:match("^function%s+([%w_%.:]+)") or l:match("^local%s+function%s+([%w_]+)")
      if n then close(i - 1); start, name = i, n; I.nfuncs = I.nfuncs + 1 end
    end
    close(#S.lines)
    table.sort(I.funcs, function(a, b) return a[2] > b[2] end)
  end
  -- what a .b16 will not have
  for _, ck in ipairs(B16_CHECKS) do
    for i, l in ipairs(S.lines) do
      local code = l:gsub("%-%-.*$", "")
      if code:find(ck[1]) then I.b16[#I.b16 + 1] = { i, ck[2] }; break end
    end
  end
  coroutine.yield()
  -- the sheet's cells with something drawn (a big sheet is not counted)
  local cols, rows = S.sheet_w // 8, S.sheet_h // 8
  if cols * rows <= 4096 then
    local used = 0
    for c = 0, cols * rows - 1 do
      local x0, y0 = (c % cols) * 8, (c // cols) * 8
      local found = false
      for j = 0, 7 do
        for i = 0, 7 do
          if sget(x0 + i, y0 + j) then found = true; break end
        end
        if found then break end
      end
      if found then used = used + 1 end
    end
    I.cells = used
  end
  I.ncells = cols * rows
  coroutine.yield()
  local mc = 0
  I.layers = #mlayers()
  for l = 1, I.layers do
    for y = 0, S.map_h - 1 do
      for x = 0, S.map_w - 1 do
        if mget(x, y, l) ~= 0 then mc = mc + 1 end
      end
    end
    coroutine.yield()
  end
  I.map_used = mc
  -- the tiles with flags, the named zones (zspr)
  local fl = 0
  if cols * rows <= 16384 then
    for c = 0, cols * rows - 1 do if fget(c) ~= 0 then fl = fl + 1 end end
  end
  I.flagged, I.zones = fl, #zones()
  coroutine.yield()
  -- the models and their skeletons (the sections the kernel holds)
  local mesh, anim = cart_data(8), cart_data(9)
  local parts = B.split_mesh(mesh)
  local rigs = B.split_anim(anim)
  I.models, I.tris, I.clips, I.rigs = {}, 0, 0, 0
  for _, p in ipairs(parts) do
    local nv, nt = sunpack("<I2I2", p[2], 17)
    I.models[#I.models + 1] = p[1]
    I.tris = I.tris + nt
    local ac = rigs[p[1]]
    if ac then
      I.rigs = I.rigs + 1
      I.clips = I.clips + sunpack("<I2", ac, 19)
    end
  end
  I.mesh_kb, I.anim_kb = (mesh and #mesh or 0) / 1024, (anim and #anim or 0) / 1024
  -- the sound bank, and the file as it is on the card
  I.audio_kb = 0
  if S.proj.path and cart_audio then
    local bank = cart_audio(S.proj.path)
    I.audio_kb = bank and #bank / 1024 or 0
    I.has_audio = bank and true or false
  end
  if S.proj.save then
    local dir, name = S.proj.save:match("^(.*)/([^/]+)$")
    for _, f in ipairs(ls(dir ~= "" and dir or "/")) do
      if f.name:upper() == name:upper() then I.file_kb = f.size / 1024 end
    end
  end
  -- the data in memory while the game runs (stat(13) counts the same)
  local w, h = S.proj.res:match("(%d+)x(%d+)")
  I.sheet_kb = (S.sheet_w * S.sheet_h * 3 + cols * rows * 2) / 1024
  I.map_kb = S.map_w * S.map_h * 2 * I.layers / 1024
  I.uses3d = false
  for _, l in ipairs(S.lines) do if l:find("draw3d%(") then I.uses3d = true; break end end
  I.zbuf_kb = I.uses3d and tonumber(w) * tonumber(h) * 2 / 1024 or 0
  I.data_kb = I.sheet_kb + I.map_kb + I.mesh_kb + I.anim_kb + I.audio_kb + I.zbuf_kb
  return I
end

-- one more piece of the measure, each frame the project page is shown
local function measure_step()
  if S.info then return end
  if not S.measuring then
    S.measuring = coroutine.create(function() return measure() end)
    if timeslice then timeslice(S.measuring, 200) end
  end
  local ok, res = coroutine.resume(S.measuring)
  if not ok then
    S.measuring = nil
    S.info = { error = tostring(res) }
    log("sdk: measure: " .. tostring(res))
  elseif coroutine.status(S.measuring) == "dead" then
    S.measuring = nil
    S.info = res
  end
end

----------------------------------------------------------------- dialogs

-- as bm Studio and bm Mesh: a line of text (ask) and a list to choose from
-- (choose: rows = { {label, fn, [info]} }); Esc (B) closes them
do
  local input, pick = nil, nil

  function D.ask(label, text, done) input = { label = label, text = text, done = done } end
  function D.choose(title, rows, sel) pick = { title = title, rows = rows, sel = sel or 1 } end
  function D.open() return input ~= nil or pick ~= nil end
  function D.close() input, pick = nil, nil end

  function D.key(k)
    if input then
      if k == "\n" or k == "ok" then local d, t = input.done, input.text; input = nil; d(t)
      elseif k == "esc" or k == "back" then input = nil
      elseif k == "\b" then input.text = input.text:sub(1, -2)
      elseif #k == 1 and k:byte() >= 32 and #input.text < 47 then input.text = input.text .. k end
      return true
    end
    if pick then
      local n = #pick.rows
      if k == "up" then pick.sel = (pick.sel - 2) % n + 1
      elseif k == "down" then pick.sel = pick.sel % n + 1
      elseif k == "pgup" then pick.sel = max(1, pick.sel - 12)
      elseif k == "pgdn" then pick.sel = min(n, pick.sel + 12)
      elseif k == "esc" or k == "back" then pick = nil
      elseif k == "\n" or k == " " or k == "ok" then
        local fn = pick.rows[pick.sel][2]
        pick = nil
        fn()
      end
      return true
    end
    return false
  end

  function D.draw()
    if pick then
      local p = pick
      local rows = min(#p.rows, 14)
      local info = p.rows[p.sel][3]
      local h = (rows + (info and 4 or 2)) * 16
      local y0 = max(32, (H - h) // 32 * 16)
      rectfill(40, y0 - 8, 560, h + 8, C.PANEL)        -- (the frame clear of the title's row)
      rect(40, y0 - 8, 560, h + 8, C.ACC)
      local tx = print(p.title, 56, y0, C.ACC) + 16
      chip_hint("esc", "B", "back", chip_hint("enter", "A", "choose", tx, y0), y0)
      local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
      for i = first, min(#p.rows, first + rows - 1) do
        local y = y0 + 16 + (i - first) * 16
        if i == p.sel then rectfill(48, y, 544, 16, C.SEL) end
        print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C.TEXT)
      end
      if info then print(info:sub(1, 66), 56, y0 + (rows + 2) * 16, C.DIM) end
    end
    if input then
      rectfill(80, 144, 480, 64, C.PANEL)
      rect(80, 144, 480, 64, C.ACC)
      print(input.label, 96, 160, C.DIM)
      print(input.text .. ((S.frame // 20) % 2 == 0 and "_" or ""), 96, 176, C.TEXT)
      chip_hint("esc", nil, "cancel", chip_hint("enter", nil, "ok", 336, 144), 144)
    end
  end
end

-- a new project from a template (Ctrl+N, the menu, n on the project page)
local function choose_template()
  local rows = {}
  for _, tp in ipairs(TEMPLATES) do
    rows[#rows + 1] = { tp.name, function()
      if S.dirty and not D.confirm_new then
        D.confirm_new = true
        say("unsaved changes: choose the template again to leave them", C.ERR)
        return
      end
      D.confirm_new = nil
      new_project(tp)
      show("project")
      S.hubview = "hub"
    end, tp.desc }
  end
  D.choose("new project from a template", rows)
end

----------------------------------------------------------------- code page


do
  local cx, cy, top, left = 0, 1, 1, 0   -- cursor column (0 = before the first char), line
  local undo, last_edit, clip_lines = {}, nil, nil
  local GUTTER = 5
  local VISIBLE_COLS = W // 8 - GUTTER

  local KEYWORDS = {}
  for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true until while"):gmatch("%S+") do KEYWORDS[w] = true end
  -- bm's API (runtime.c), and in its own colour the 3D
  local API, API3D = {}, {}
  for w in ([[cls pset pget line rect rectfill circ circfill spr sspr map mget mset fget fset mflags msize mlayers
    zspr zone zones zboxes sget sset print font
    camera prompt lastinput clip rgb btn btnp players stick time stat code_tokens tri screen log report keyhelp
    quit keymap controller online udp_open udp_send udp_recv udp_close net_ip net_resolve save saved keyp keyheld
    rawkeys keydown keys pad mouse mousep timeslice ls cart_load cart_new cart_save cart_run cart_tool cart_arg
    cart_read cart_write cart_sheet cart_audio cart_put_audio light_begin light light_end fades dark_begin glow
    dark_end note noteoff freq envelope duty playing apu hz slide vibrato arp sfx sfxpos music tempo mute volume
    audio_bank audio_pattern audio_play nnet require SCREEN_W SCREEN_H SQUARE TRIANGLE SAW NOISE SINE METAL math
    string table coroutine ipairs pairs tostring tonumber type select pcall error assert]]):gmatch("%S+") do
    API[w] = true
  end
  for w in ([[mesh mesh_sphere mesh_cube model models bounds3d animate clips bone3d bones3d bone_turn hit3d
    draw3d camera3d light3d sky3d shine3d shadow3d point3d line3d sprite3d world3d world_box world_ray world_move
    world_floor fog3d project3d lamp3d zclear gpu3d cart_data cart_meshes mesh_reduce picture3d cutout3d]]):gmatch("%S+") do
    API3D[w] = true
  end
  local C_KW, C_API, C_3D, C_STR, C_NUM, C_COM, C_PUN = 0xFF7AB0, 0x70D0FF, 0x60E0B0, 0x90E070, 0xFFB060, 0x607088, 0xB0B8D0
  local seg_cache, seg_count = {}, 0

  local function segments(l)
    local s = seg_cache[l]
    if s then return s end
    s = {}
    local i, n = 1, #l
    while i <= n do
      local c = l:sub(i, i)
      local j, col
      if c == "-" and l:sub(i + 1, i + 1) == "-" then j, col = n, C_COM
      elseif c == '"' or c == "'" then
        j = i + 1
        while j <= n and l:sub(j, j) ~= c do if l:sub(j, j) == "\\" then j = j + 1 end j = j + 1 end
        col = C_STR
      elseif c:match("%d") then j = (l:find("[^%w%.]", i) or n + 1) - 1; col = C_NUM
      elseif c:match("[%a_]") then
        j = (l:find("[^%w_]", i) or n + 1) - 1
        local w = l:sub(i, j)
        col = KEYWORDS[w] and C_KW or API3D[w] and C_3D or API[w] and C_API or
              (i > 4 and l:sub(i - 4, i - 1) == "lib.") and C_API or C.TEXT   -- bmlib's
      else j, col = i, C_PUN end
      if j > n then j = n end
      s[#s + 1] = { i, l:sub(i, j), col }
      i = j + 1
    end
    seg_count = seg_count + 1
    if seg_count > 600 then seg_cache, seg_count = {}, 0 end
    seg_cache[l] = s
    return s
  end

  local function snapshot(kind)
    if last_edit == kind .. cy then return end
    last_edit = kind .. cy
    undo[#undo + 1] = { lines = table.move(S.lines, 1, #S.lines, 1, {}), cx = cx, cy = cy }
    if #undo > 40 then table.remove(undo, 1) end
  end

  local function clamp_cursor()
    local lines = S.lines
    if cy < 1 then cy = 1 end
    if cy > #lines then cy = #lines end
    if cx > #lines[cy] then cx = #lines[cy] end
    if cx < 0 then cx = 0 end
    if cy < top then top = cy end
    if cy >= top + ROWS then top = cy - ROWS + 1 end
    if cx < left then left = cx end
    if cx >= left + VISIBLE_COLS - 1 then left = cx - VISIBLE_COLS + 2 end
  end

  -- code from the assistant or the 3D page: under the cursor's line
  -- (instead of it, when empty), with its indentation
  local function insert(code, what)
    snapshot("insert")
    last_edit = nil
    local lines = S.lines
    local indent = lines[cy]:match("^ *")
    local new = {}
    for line in (code:gsub("\n$", "") .. "\n"):gmatch("(.-)\n") do new[#new + 1] = indent .. line end
    local at = cy + 1
    if lines[cy]:match("^%s*$") then table.remove(lines, cy); at = cy end
    for i, l in ipairs(new) do table.insert(lines, at + i - 1, l) end
    cy, cx = at, #indent
    clamp_cursor()
    code_touched()
    say(#new .. " lines " .. (what or "inserted") .. " (Ctrl+Z takes them away)", C.ACC)
  end

  local function word_at()
    local l = S.lines[cy]
    local a, b = cx, cx + 1
    while a > 0 and l:sub(a, a):match("[%w_]") do a = a - 1 end
    while b <= #l and l:sub(b, b):match("[%w_]") do b = b + 1 end
    return l:sub(a + 1, b - 1)
  end

  local function key(k)
    local lines = S.lines
    local l = lines[cy]
    if k == "up" then cy = cy - 1
    elseif k == "down" then cy = cy + 1
    elseif k == "left" then if cx > 0 then cx = cx - 1 elseif cy > 1 then cy = cy - 1; cx = #lines[cy] end
    elseif k == "right" then if cx < #l then cx = cx + 1 elseif cy < #lines then cy = cy + 1; cx = 0 end
    elseif k == "home" then cx = cx == 0 and #l:match("^ *") or 0
    elseif k == "end" then cx = #l
    elseif k == "pgup" then cy = cy - ROWS
    elseif k == "pgdn" then cy = cy + ROWS
    elseif k == "\n" then
      snapshot("nl")
      local indent = l:match("^ *")
      lines[cy] = l:sub(1, cx)
      table.insert(lines, cy + 1, indent .. l:sub(cx + 1))
      cy, cx = cy + 1, #indent
      code_touched()
    elseif k == "\b" then
      snapshot("bs")
      if cx > 0 then lines[cy] = l:sub(1, cx - 1) .. l:sub(cx + 1); cx = cx - 1
      elseif cy > 1 then
        cx = #lines[cy - 1]
        lines[cy - 1] = lines[cy - 1] .. l
        table.remove(lines, cy)
        cy = cy - 1
      end
      code_touched()
    elseif k == "del" then
      snapshot("del")
      if cx < #l then lines[cy] = l:sub(1, cx) .. l:sub(cx + 2)
      elseif cy < #lines then lines[cy] = l .. lines[cy + 1]; table.remove(lines, cy + 1) end
      code_touched()
    elseif k == "\t" then
      snapshot("ins")
      lines[cy] = l:sub(1, cx) .. "  " .. l:sub(cx + 1)
      cx = cx + 2
      code_touched()
    elseif k == "^z" then
      local u = table.remove(undo)
      if u then S.lines, cx, cy = u.lines, u.cx, u.cy; last_edit = nil; code_touched(); say("undo") end
    elseif k == "^k" or k == "^x" then                -- cut the line (Ctrl+V puts it back)
      snapshot("kill")
      clip_lines = { l }
      if #lines > 1 then table.remove(lines, cy) else lines[1] = "" end
      last_edit = nil
      code_touched()
    elseif k == "^c" then
      clip_lines = { l }
      say("line copied: Ctrl+V puts it above the cursor")
    elseif k == "^v" then
      if clip_lines then
        snapshot("paste")
        for i, c in ipairs(clip_lines) do table.insert(lines, cy + i - 1, c) end
        cy = cy + #clip_lines
        last_edit = nil
        code_touched()
      end
    elseif k == "^d" then
      snapshot("dup")
      table.insert(lines, cy + 1, l)
      cy = cy + 1
      last_edit = nil
      code_touched()
    elseif k == "^g" then
      if S.err_line then cy, cx = S.err_line, 0 end
    elseif #k == 1 and k:byte() >= 32 then
      snapshot("ins")
      lines[cy] = l:sub(1, cx) .. k .. l:sub(cx + 1)
      cx = cx + 1
      code_touched()
    end
    if k ~= "\b" and k ~= "del" and not (#k == 1 and k:byte() >= 32) then
      if k ~= "\n" and k ~= "\t" then last_edit = nil end
    end
    clamp_cursor()
  end

  local function draw()
    rectfill(0, 16, W, ROWS * 16, C.BG)
    local lines = S.lines
    for r = 0, ROWS - 1 do
      local i = top + r
      local y = 16 + r * 16
      if i > #lines then break end
      local num = tostring(i)
      print(string.rep(" ", 4 - #num) .. num, 0, y, i == cy and C.ACC or C.DIM)
      if i == S.err_line then rectfill(GUTTER * 8 - 4, y, 3, 16, C.ERR) end
      for _, s in ipairs(segments(lines[i])) do
        local start, text, col = s[1], s[2], s[3]
        local a = start - 1 - left                 -- column on screen (0-based)
        if a + #text > 0 and a < VISIBLE_COLS then
          if a < 0 then text = text:sub(1 - a); a = 0 end
          if a + #text > VISIBLE_COLS then text = text:sub(1, VISIBLE_COLS - a) end
          print(text, (GUTTER + a) * 8, y, col)
        end
      end
    end
    if (S.frame // 20) % 2 == 0 then
      local x, y = (GUTTER + cx - left) * 8, 16 + (cy - top) * 16
      rectfill(x, y + 13, 8, 2, C.ACC)
    end
    hint({ { "f6", "X", "assistant" }, { "f9", nil, "explain the error" }, { "ctrl g", nil, "go to it" },
           { "f5", nil, "try it" }, { "ctrl s", nil, "save" } })
  end

  local function status()
    if S.err_text then return S.err_text, C.ERR end
    local t = tokens_now()
    return string.format("line %d/%d  col %d", cy, #S.lines, cx + 1) .. (t and ("   " .. t .. " tokens") or "")
  end

  P.code = { key = key, draw = draw, status = status, insert = insert, word_at = word_at,
             goto_line = function(n) cy, cx = clamp(n, 1, #S.lines), 0; clamp_cursor() end,
             cursor = function() return cy, cx end,
             reset = function() cx, cy, top, left, undo, last_edit = 0, 1, 1, 0, {}, nil end }
end

----------------------------------------------------------------- 2D page: sprites and map

-- the 32 colours of the sprite view (the SDK's palette, as in bm Pixel's "1")
local PALETTE = {
  0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
  0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
  0x14161E, 0x2E2832, 0x4A3E36, 0x6A5A48, 0x8A7A60, 0xB09078, 0x3A5A40, 0x6CC04A,
  0x203A6A, 0x3060D0, 0x70A8F0, 0x5A0A0A, 0xA01818, 0xE04040, 0xFF9030, 0xFFE080,
}

local function checker(x, y, w, h, s)
  rectfill(x, y, w, h, 0x2A2E3A)
  for j = 0, h - 1, s do
    for i = ((j // s) % 2) * s, w - 1, s * 2 do rectfill(x + i, y + j, min(s, w - i), min(s, h - j), 0x343846) end
  end
end

-- the two rows over a view: the name in orange, then what it shows
local function strip(x, name, info, info2)
  rectfill(x, 16, W - x, 32, C.BG)
  local nx = print(name, x + 8, 16, C.ACC)
  print(info:sub(1, (W - nx - 8) // 8), nx + 16, 16, C.TEXT)
  if info2 then print(info2:sub(1, (W - x - 16) // 8), x + 8, 32, C.DIM) end
end

do
  local sel, size, px, py = 0, 2, 0, 0   -- selected cell, 1 = 8x8 / 2 = 16x16, pixel cursor
  local color, transparent = 0xFFFFFF, false
  local focus = "canvas"                 -- or "sheet"
  local s_undo, clip_px, stroke = {}, nil, false
  local CX, CY = 16, 56                  -- the canvas
  local SX, SY, SW, SH = 336, 56, 256, 176   -- the sheet's window

  local function cells_per_row() return S.sheet_w // 8 end
  local function sel_xy() return (sel % cells_per_row()) * 8, (sel // cells_per_row()) * 8 end

  local function save_sel_undo()
    local sx, sy = sel_xy()
    local n = size * 8
    local pix = {}
    for j = 0, n - 1 do for i = 0, n - 1 do pix[j * n + i] = sget(sx + i, sy + j) or false end end
    s_undo[#s_undo + 1] = { sel = sel, size = size, pix = pix }
    if #s_undo > 40 then table.remove(s_undo, 1) end
  end

  local function put(i, j)
    local sx, sy = sel_xy()
    if transparent then sset(sx + i, sy + j) else sset(sx + i, sy + j, color) end
    touched()
  end

  local function fill(i0, j0)
    local sx, sy = sel_xy()
    local n = size * 8
    local target = sget(sx + i0, sy + j0) or false
    local new = (not transparent) and color or false
    if target == new then return end
    local stack, seen = { { i0, j0 } }, {}
    while #stack > 0 do
      local p = table.remove(stack)
      local i, j = p[1], p[2]
      local k = j * n + i
      if i >= 0 and j >= 0 and i < n and j < n and not seen[k] and (sget(sx + i, sy + j) or false) == target then
        seen[k] = true
        put(i, j)
        stack[#stack + 1] = { i + 1, j }; stack[#stack + 1] = { i - 1, j }
        stack[#stack + 1] = { i, j + 1 }; stack[#stack + 1] = { i, j - 1 }
      end
    end
  end

  local function flip(horizontal)
    save_sel_undo()
    local sx, sy = sel_xy()
    local n = size * 8
    local pix = {}
    for j = 0, n - 1 do for i = 0, n - 1 do pix[j * n + i] = sget(sx + i, sy + j) end end
    for j = 0, n - 1 do
      for i = 0, n - 1 do
        sset(sx + i, sy + j, horizontal and pix[j * n + (n - 1 - i)] or pix[(n - 1 - j) * n + i])
      end
    end
    touched()
  end

  local function next_color(d)
    local k = 1
    for i, c in ipairs(PALETTE) do if c == color then k = i end end
    if transparent then k = d > 0 and 0 or #PALETTE + 1 end
    k = k + d
    if k < 1 or k > #PALETTE then transparent = true else transparent = false; color = PALETTE[k] end
  end

  -- the flags of the chosen cells (fget/fset: 16x16, the four), 0-7
  local FLAG_C = { [0] = 0xE04040, 0x40C040, 0x4080F0, 0x40D0E0, 0xE040E0, 0xE0A040, 0xA0A0A0, 0xFFFFFF }
  local function sel_cells()
    local out, cols = {}, cells_per_row()
    for j = 0, size - 1 do for i = 0, size - 1 do out[#out + 1] = sel + j * cols + i end end
    return out
  end

  local function flags_text()
    local t = {}
    for f = 0, 7 do t[#t + 1] = fget(sel, f) and tostring(f) or "." end
    return table.concat(t)
  end

  local function move_sel(dx, dy)
    local cols, rows = cells_per_row(), S.sheet_h // 8
    local x, y = sel % cols + dx * size, sel // cols + dy * size
    x = clamp(x, 0, cols - size)
    y = clamp(y, 0, rows - size)
    sel = y * cols + x
  end

  local function act(a)
    local n = size * 8
    if focus == "sheet" then
      if a == "left" then move_sel(-1, 0) elseif a == "right" then move_sel(1, 0)
      elseif a == "up" then move_sel(0, -1) elseif a == "down" then move_sel(0, 1)
      elseif a == "ok" or a == "focus" then focus = "canvas" end
      return
    end
    if a == "left" then px = (px - 1) % n elseif a == "right" then px = (px + 1) % n
    elseif a == "up" then py = (py - 1) % n elseif a == "down" then py = (py + 1) % n
    elseif a == "paint" then if not stroke then save_sel_undo(); stroke = true end; put(px, py)
    elseif a == "erase" then save_sel_undo(); local t = transparent; transparent = true; put(px, py); transparent = t
    elseif a == "pick" then
      local sx, sy = sel_xy()
      local c = sget(sx + px, sy + py)
      if c then color, transparent = c, false else transparent = true end
    elseif a == "fill" then save_sel_undo(); fill(px, py)
    elseif a == "next" then next_color(1) elseif a == "prev" then next_color(-1)
    elseif a == "size" then size = 3 - size; px, py = px % (size * 8), py % (size * 8); move_sel(0, 0)
    elseif a == "fliph" then flip(true) elseif a == "flipv" then flip(false)
    elseif a == "copy" then
      local sx, sy = sel_xy()
      clip_px = { n = n, pix = {} }
      for j = 0, n - 1 do for i = 0, n - 1 do clip_px.pix[j * n + i] = sget(sx + i, sy + j) end end
      say("copied " .. n .. "x" .. n)
    elseif a:match("^flag%d$") then
      local f = tonumber(a:sub(5))
      local on = not fget(sel, f)
      for _, c in ipairs(sel_cells()) do fset(c, f, on) end
      touched()
      say(string.format("flag %d %s (fget(%d, %d))", f, on and "on" or "off", sel, f))
    elseif a == "paste" and clip_px then
      save_sel_undo()
      local sx, sy = sel_xy()
      local m = min(n, clip_px.n)
      for j = 0, m - 1 do for i = 0, m - 1 do sset(sx + i, sy + j, clip_px.pix[j * clip_px.n + i]) end end
      touched()
    elseif a == "undo" then
      local u = table.remove(s_undo)
      if u then
        sel, size = u.sel, u.size
        local sx, sy = sel_xy()
        local m = size * 8
        for j = 0, m - 1 do for i = 0, m - 1 do sset(sx + i, sy + j, u.pix[j * m + i] or nil) end end
        touched()
        say("undo")
      end
    elseif a == "focus" then focus = "sheet" end
  end

  -- the assistant's sprite (F6) in the chosen cell
  local function put_sprite(s)
    save_sel_undo()
    local sx, sy = sel_xy()
    for y = 0, s.h - 1 do
      for x = 0, s.w - 1 do
        local c = s.px[y * s.w + x + 1]
        if c >= 0 then sset(sx + x, sy + y, c) else sset(sx + x, sy + y) end
      end
    end
    touched()
    say("the assistant's " .. (s.name or "sprite") .. " in cell " .. sel .. " (u takes it away)", C.ACC)
  end

  local function draw()
    local n = size * 8
    local z = size == 1 and 32 or 16
    local sx, sy = sel_xy()
    strip(0, "SPRITES", string.format("cell %d  %dx%d   spr(%d, x, y%s)", sel, n, n, sel, size == 2 and ", 2, 2" or ""),
          focus == "sheet" and "choosing on the sheet: arrows, then Enter" or
          "F3 again: the map   2 on the project page: bm Pixel, every tool")
    checker(CX, CY, n * z, n * z, z)
    for j = 0, n - 1 do
      for i = 0, n - 1 do
        local c = sget(sx + i, sy + j)
        if c then rectfill(CX + i * z, CY + j * z, z - 1, z - 1, c) end
      end
    end
    if focus == "canvas" then
      rect(CX + px * z - 1, CY + py * z - 1, z + 1, z + 1, (S.frame // 10) % 2 == 0 and 0xFFFFFF or 0x000000)
    end
    -- the sheet at 1:1 around the chosen cell
    local vw, vh = min(SW, S.sheet_w), min(SH, S.sheet_h)
    local scx = clamp(sx - vw // 2, 0, S.sheet_w - vw) // 8 * 8
    local scy = clamp(sy - vh // 2, 0, S.sheet_h - vh) // 8 * 8
    checker(SX, SY, vw, vh, 4)
    sspr(scx, scy, vw, vh, SX, SY)
    clip(SX, SY, vw, vh)
    rect(SX + sx - scx - 1, SY + sy - scy - 1, n + 2, n + 2, focus == "sheet" and C.ACC or 0xFFFFFF)
    clip()
    -- the palette, the transparent swatch, the colour
    for i, c in ipairs(PALETTE) do
      local x, y = SX + ((i - 1) % 16) * 16, 240 + ((i - 1) // 16) * 14
      rectfill(x, y, 15, 13, c)
      if c == color and not transparent then rect(x - 1, y - 1, 17, 15, 0xFFFFFF) end
    end
    checker(SX + 264, 240, 28, 27, 4)
    if transparent then rect(SX + 263, 239, 30, 29, 0xFFFFFF) end
    rectfill(SX, 272, 20, 16, 0x000000)
    if transparent then checker(SX + 1, 273, 18, 14, 4) else rectfill(SX + 1, 273, 18, 14, color) end
    print(transparent and "transparent" or string.format("#%06X", color), SX + 32, 272, C.TEXT)
    -- the flags of the cell: what the tile is to the game (bmlib's convention)
    print("FLAGS", SX, 288, C.DIM)
    for f = 0, 7 do
      local x = SX + 48 + f * 24
      if fget(sel, f) then rectfill(x - 4, 288, 16, 16, FLAG_C[f]) else rect(x - 4, 288, 16, 16, C.BAR) end
      print(tostring(f), x, 288, fget(sel, f) and 0x000000 or C.DIM)
    end
    print("0 wall 1 plat. 2 ladder 3 water 4 hurt", SX, 304, C.DIM)
    hint({ { "space", "A", "draw" }, { "x", "B", "pick" }, { "f", nil, "fill" }, { "tab", "Y", "sheet" },
           { "z", nil, "8/16" }, { ",", "X", "colour" }, { "0", nil, "flags" }, { "f6", nil, "assistant" } })
  end

  local function status()
    return string.format("cell %d  %dx%d  (%d,%d)  %s  flags %s", sel, size * 8, size * 8, px, py,
                         transparent and "transparent" or string.format("#%06X", color), flags_text())
  end

  P.sprite = { act = act, draw = draw, status = status, put_sprite = put_sprite,
               size = function() return size * 8 end,
               lift = function() stroke = stroke and (btn(4) or false) end,
               stop = function() stroke = false end,
               state = function() return { sel = sel, size = size } end,
               restore = function(t) if t then sel, size = t.sel or 0, t.size or 2 end end,
               reset = function() sel, size, px, py, s_undo, focus = 0, 2, 0, 0, {}, "canvas" end }
end

do
  local mx, my, vx0, vy0 = 0, 0, 0, 0    -- cursor cell, top-left cell of the view
  local tile, picking = 1, false
  local layer, only, show_flags = 1, false, false   -- the layer edited; drawn alone; the flags over it
  local m_undo, m_stroke = {}, nil
  local VIEW_Y = 48
  local VIEW_CW, VIEW_CH = W // 8, (HINT_Y - VIEW_Y) // 8

  local function set_cell(x, y, v)
    local old = mget(x, y, layer)
    if old == v then return end
    if m_stroke then m_stroke[#m_stroke + 1] = { x, y, old, layer } end
    mset(x, y, v, layer)
    touched()
  end

  -- the next layer; add: a new one after the last (up to 8)
  local function next_layer(add)
    local names = mlayers()
    if add then
      if #names >= 8 then say("a map has at most 8 layers", C.ERR); return end
      local k = #names + 1
      local taken = {}
      for _, n in ipairs(names) do taken[n] = true end
      while taken["layer" .. k] do k = k + 1 end
      names[#names + 1] = "layer" .. k
      mlayers(names)
      layer = #names
      touched()
    else
      layer = layer % #names + 1
    end
    say(string.format("map layer %d/%d: %s   (map(..., \"%s\"))", layer, #names, names[layer], names[layer]), C.ACC)
  end

  local function map_fill(x0, y0)
    local target = mget(x0, y0, layer)
    if target == tile then return end
    local stack, count = { { x0, y0 } }, 0
    while #stack > 0 and count < 20000 do
      local p = table.remove(stack)
      local x, y = p[1], p[2]
      if x >= 0 and y >= 0 and x < S.map_w and y < S.map_h and mget(x, y, layer) == target then
        set_cell(x, y, tile)
        count = count + 1
        stack[#stack + 1] = { x + 1, y }; stack[#stack + 1] = { x - 1, y }
        stack[#stack + 1] = { x, y + 1 }; stack[#stack + 1] = { x, y - 1 }
      end
    end
  end

  local function end_stroke()
    if m_stroke then
      if #m_stroke > 0 then m_undo[#m_undo + 1] = m_stroke end
      m_stroke = nil
    end
  end

  local function act(a)
    if picking then
      local cols = S.sheet_w // 8
      if a == "left" then tile = max(0, tile - 1)
      elseif a == "right" then tile = tile + 1
      elseif a == "up" then tile = max(0, tile - cols)
      elseif a == "down" then tile = tile + cols
      elseif a == "paint" or a == "ok" or a == "focus" then picking = false end
      tile = min(tile, cols * (S.sheet_h // 8) - 1)
      return
    end
    if a == "left" then mx = mx - 1 elseif a == "right" then mx = mx + 1
    elseif a == "up" then my = my - 1 elseif a == "down" then my = my + 1
    elseif a == "pgup" then my = my - VIEW_CH elseif a == "pgdn" then my = my + VIEW_CH
    elseif a == "paint" then m_stroke = m_stroke or {}; set_cell(mx, my, tile)
    elseif a == "erase" then m_stroke = {}; set_cell(mx, my, 0); end_stroke()
    elseif a == "pick" then tile = mget(mx, my, layer)
    elseif a == "fill" then m_stroke = {}; map_fill(mx, my); end_stroke()
    elseif a == "layer" then next_layer()
    elseif a == "newlayer" then next_layer(true)
    elseif a == "only" then only = not only; say(only and "this layer only" or "every layer")
    elseif a == "flags" then show_flags = not show_flags; say(show_flags and "the flags of the tiles (fget)" or "flags hidden")
    elseif a == "next" then tile = tile + 1 elseif a == "prev" then tile = max(0, tile - 1)
    elseif a == "undo" then
      local u = table.remove(m_undo)
      if u then for i = #u, 1, -1 do mset(u[i][1], u[i][2], u[i][3], u[i][4]) end; touched(); say("undo") end
    elseif a == "focus" then picking = true end
    mx = clamp(mx, 0, S.map_w - 1)
    my = clamp(my, 0, S.map_h - 1)
    if mx < vx0 then vx0 = mx elseif mx >= vx0 + VIEW_CW then vx0 = mx - VIEW_CW + 1 end
    if my < vy0 then vy0 = my elseif my >= vy0 + VIEW_CH then vy0 = my - VIEW_CH + 1 end
  end

  local function draw()
    rectfill(0, VIEW_Y, W, HINT_Y - VIEW_Y, 0x000000)
    clip(0, VIEW_Y, W, HINT_Y - VIEW_Y)
    local names = mlayers()
    if layer > #names then layer = 1 end
    if only then
      map(vx0, vy0, 0, VIEW_Y, VIEW_CW, VIEW_CH, layer)
    else
      for l = 1, #names do map(vx0, vy0, 0, VIEW_Y, VIEW_CW, VIEW_CH, l) end
    end
    if show_flags then                     -- the collisions: a frame in the colour of each flag
      local FC = { 0xE04040, 0x40C040, 0x4080F0, 0x40D0E0, 0xE040E0 }
      for cy = 0, VIEW_CH - 1 do
        for cx = 0, VIEW_CW - 1 do
          local f = fget(mget(vx0 + cx, vy0 + cy, layer))
          if f ~= 0 then
            for k = 0, 4 do
              if f >> k & 1 == 1 then rect(cx * 8 + k, VIEW_Y + cy * 8 + k, 8 - 2 * k, 8 - 2 * k, FC[k + 1]); break end
            end
          end
        end
      end
    end
    local x, y = (mx - vx0) * 8, VIEW_Y + (my - vy0) * 8
    rect(x - 1, y - 1, 10, 10, (S.frame // 10) % 2 == 0 and 0xFFFFFF or C.ACC)
    clip()
    strip(0, "MAP", string.format("(%d,%d) = %d   tile %d   layer %d/%d %s", mx, my, mget(mx, my, layer), tile, layer,
                                  #names, names[layer]),
          picking and "choosing the tile: arrows, then Enter" or
          "F3 again: the sprites   tab: the tile   l / L: next / new layer   c: flags")
    rectfill(W - 28, 20, 8, 8, 0x000000)
    spr(tile, W - 28, 20)
    if picking then
      local cols = S.sheet_w // 8
      local vw, vh = min(256, S.sheet_w), min(224, S.sheet_h)
      local tx, ty = (tile % cols) * 8, (tile // cols) * 8
      local scx = clamp(tx - vw // 2, 0, S.sheet_w - vw) // 8 * 8
      local scy = clamp(ty - vh // 2, 0, S.sheet_h - vh) // 8 * 8
      local ox, oy = W - vw - 24, 64
      rectfill(ox - 8, oy - 8, vw + 16, vh + 16, C.PANEL)
      checker(ox, oy, vw, vh, 4)
      sspr(scx, scy, vw, vh, ox, oy)
      rect(ox + tx - scx - 1, oy + ty - scy - 1, 10, 10, C.ACC)
    end
    hint({ { "space", "A", "place" }, { "backspace", nil, "clear" }, { "x", "B", "pick" }, { "f", nil, "fill" },
           { "tab", "Y", "tiles" }, { ",", "X", "tile" }, { "l", nil, "layer" }, { "c", nil, "flags" },
           { "u", nil, "undo" } })
  end

  local function status()
    local names = mlayers()
    return picking and "choosing a tile" or string.format("(%d,%d) = %d  tile %d  layer %d/%d %s", mx, my,
                                                          mget(mx, my, layer), tile, layer, #names, names[layer] or "")
  end

  P.map = { act = act, draw = draw, status = status, end_stroke = end_stroke,
            new_layer = function() next_layer(true) end,
            stroking = function() return m_stroke ~= nil end,
            state = function() return { mx = mx, my = my } end,
            restore = function(t) if t then mx, my = t.mx or 0, t.my or 0; act("none") end end,
            reset = function()
              mx, my, vx0, vy0, m_undo, m_stroke, picking = 0, 0, 0, 0, {}, nil, false
              layer, only, show_flags = 1, false, false
            end }
end

----------------------------------------------------------------- 3D page

do
  local names, sel = {}, 1
  local m, cl, clip_i, t, play = nil, {}, 0, 0, true
  local spin = true
  local nfo = {}                         -- per model: vertices, triangles, bones (from the sections)
  local cam = { yaw = 0.6, pitch = -0.35, dist = 4, tx = 0, ty = 0, tz = 0 }
  local VY = 48                          -- the view, under the two rows

  local function read_sections()
    nfo = {}
    local rigs = B.split_anim(cart_data(9))
    for _, p in ipairs(B.split_mesh(cart_data(8))) do
      local nv, nt = sunpack("<I2I2", p[2], 17)
      local ac = rigs[p[1]]
      nfo[p[1]] = { nv = nv, nt = nt, bones = ac and sunpack("<I2", ac, 17) or 0 }
    end
  end

  local function pick(i)
    sel = clamp(i, 1, max(1, #names))
    m = names[sel] and model(names[sel]) or nil
    cl, clip_i, t = {}, 0, 0
    if m then
      cl = clips(m)
      clip_i = #cl > 0 and 1 or 0
      local x0, y0, z0, x1, y1, z1 = bounds3d(m)
      cam.tx, cam.ty, cam.tz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
      cam.size = max(x1 - x0, y1 - y0, z1 - z0, 0.2)
      cam.dist = cam.size * 1.9 + 0.5
      cam.floor = y0
    end
  end

  local function refresh(keep)
    names = models()
    read_sections()
    pick(keep and sel or 1)
  end

  -- an identifier for the model's name ("crate 2" -> crate_2)
  local function ident(n)
    local s = n:gsub("[^%w_]", "_")
    if s == "" or s:match("^%d") then s = "m_" .. s end
    return s
  end

  -- the code that draws the chosen model in a game, for the code page
  local function snippet()
    local n = names[sel]
    if not n then return nil end
    local id = ident(n)
    local c = cl[clip_i]
    local out = {
      "-- the model \"" .. n .. "\" (bm Studio" .. (#cl > 0 and ", animated in bm Animator" or "") .. ")",
      "local " .. id .. " = model(\"" .. n .. "\")",
      "",
      "-- in _draw, after camera3d(...) and zclear()",
      "local function draw_" .. id .. "(x, y, z, turn)",
    }
    if c then out[#out + 1] = "  animate(" .. id .. ", \"" .. c.name .. "\", time())" end
    out[#out + 1] = "  draw3d(" .. id .. ", x, y, z, 0, turn or 0, 0, 1)"
    out[#out + 1] = "end"
    out[#out + 1] = string.format("-- as the SDK shows it: camera3d(%.1f, %.1f, %.1f, %.2f, %.2f, 60)",
                                  cam.tx - cam.f[1] * cam.dist, cam.ty - cam.f[2] * cam.dist,
                                  cam.tz - cam.f[3] * cam.dist, cam.yaw, cam.pitch)
    return table.concat(out, "\n")
  end

  -- the assistant's model (F6) joins the project's MESH and ANIM sections
  local function add_model(am)
    if not am or not am.faces or #am.faces == 0 then return end
    local parts, inset = B.split_mesh(cart_data(8))
    local rigs = B.split_anim(cart_data(9))
    local used = {}
    for _, p in ipairs(parts) do used[p[1]] = true end
    local base = (am.gen or "model"):sub(1, 14)
    local name, k = base, 1
    while used[name] do k = k + 1; name = base:sub(1, 16 - #tostring(k)) .. k end
    local mm = { name = name, faces = am.faces }
    if am.bones and #am.bones > 0 then mm.rig = { bones = am.bones, clips = am.clips or {} }
    else for _, f in ipairs(mm.faces) do f.b = nil end end
    local ok, e = B.encode_mesh(mm)
    if not ok or not mm.mc then say("cannot: " .. tostring(e or "no faces"), C.ERR); return end
    B.encode_anim(mm)
    local mparts, aparts = {}, {}
    for _, p in ipairs(parts) do
      mparts[#mparts + 1] = p[2]
      if rigs[p[1]] then aparts[#aparts + 1] = rigs[p[1]] end
    end
    mparts[#mparts + 1] = mm.mc
    if mm.ac then aparts[#aparts + 1] = mm.ac end
    local mesh = spack("<I2I2I4", #mparts, clamp(floor(inset * 256 + 0.5), 0, 65535), 0) .. table.concat(mparts)
    local anim = #aparts > 0 and spack("<I2I2I4", #aparts, 0, 0) .. table.concat(aparts) or nil
    ok, e = cart_data(8, mesh)
    if ok then ok, e = cart_data(9, anim) end
    if not ok then say("cannot: " .. tostring(e), C.ERR); return end
    touched()
    names = models()
    read_sections()
    for i, n in ipairs(names) do if n == name then pick(i) end end
    say(string.format("the assistant's %s: model %s, %d faces%s", am.name or am.gen, name, #am.faces,
                      mm.rig and string.format(", %d bones, %d animations", #mm.rig.bones, #mm.rig.clips) or ""),
        C.ACC, 300)
  end

  local function key(k)
    if k == "up" then pick(sel - 1)
    elseif k == "down" then pick(sel + 1)
    elseif k == "left" then if #cl > 0 then clip_i = (clip_i - 1) % (#cl + 1); t = 0 end
    elseif k == "right" then if #cl > 0 then clip_i = (clip_i + 1) % (#cl + 1); t = 0 end
    elseif k == " " then play = not play
    elseif k == "r" then spin = not spin
    elseif k == "q" then cam.yaw, spin = cam.yaw - math.pi / 16, false
    elseif k == "e" then cam.yaw, spin = cam.yaw + math.pi / 16, false
    elseif k == "w" then cam.pitch = clamp(cam.pitch - 0.1, -1.45, 1.2)
    elseif k == "s" then cam.pitch = clamp(cam.pitch + 0.1, -1.45, 1.2)
    elseif k == "+" or k == "=" then cam.dist = max(0.3, cam.dist * 0.85)
    elseif k == "-" then cam.dist = min(400, cam.dist / 0.85)
    elseif k == "i" then
      local code = snippet()
      if code then show("code"); P.code.insert(code, "for " .. names[sel]) else say("no model to use yet", C.ERR) end
    elseif k == "\n" then open_in(TOOLS[3])
    elseif k == "a" then open_in(TOOLS[4])
    elseif k == "m" then open_in(TOOLS[5])
    end
  end

  local function pad()
    if PAD.rp[2] then key("up") elseif PAD.rp[3] then key("down") end
    if PAD.rp[0] then key("left") elseif PAD.rp[1] then key("right") end
    if btnp(4) then key(" ") end
    if btnp(6) then key("r") end
  end

  local function update()
    if play then t = t + 1 / 60 end
    if spin then cam.yaw = cam.yaw + 0.008 end
  end

  local function draw()
    rectfill(PANEL_W, VY, W - PANEL_W, HINT_Y - VY, C.SKY)
    clip(PANEL_W, VY, W - PANEL_W, HINT_Y - VY)
    B.look(cam, PANEL_W / 2, (VY - 16) / 2)
    light3d(-0.4, 0.8, -0.5, 0.45)
    zclear()
    if m then
      -- the floor's grid, around the model only (its far side in front of the camera)
      local size = cam.size or 1
      local step = size > 8 and 4 or size > 2 and 1 or 0.25
      B.draw_grid(floor(cam.tx / step + 0.5) * step, cam.floor or 0, floor(cam.tz / step + 0.5) * step,
                  floor(size / step / 2) + 1, step, C.GRID)
      local c = cl[clip_i]
      if c then animate(m, c.name, t) elseif #cl > 0 then animate(m) end
      draw3d(m, 0, 0, 0, 0, 0, 0, 1)
      B.gizmo(cam, PANEL_W + 40, HINT_Y - 40)
    else
      print("no 3D models in this project yet", PANEL_W + 56, 160, C.DIM)
      print("Enter: bm Studio makes them from tiles", PANEL_W + 56, 192, C.TEXT)
      print("F6: the assistant makes one (a dog, a house...)", PANEL_W + 56, 208, C.TEXT)
      print("or in code: mesh(), mesh_cube(), mesh_sphere()", PANEL_W + 56, 224, C.DIM)
    end
    clip()
    -- the panel: models, then the animations of the chosen one
    rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
    B.draw_list("MODELS " .. #names, names, sel, 0, 32, 9, PANEL_W)
    local cn = { "(rest pose)" }
    for _, c in ipairs(cl) do cn[#cn + 1] = c.name .. (c.loop and "" or " (once)") end
    if #cl > 0 then B.draw_list("ANIMATIONS " .. #cl, cn, clip_i + 1, 0, 208, 5, PANEL_W)
    elseif m then print("no skeleton: 4 on the", 8, 208, C.DIM); print("project page: bm Animator", 8, 224, C.DIM) end
    local n = names[sel]
    local f = n and nfo[n]
    if n then
      local c = cl[clip_i]
      strip(PANEL_W, n, f and string.format("%d vertices, %d triangles%s", f.nv, f.nt,
                                            f.bones > 0 and string.format(", %d bones", f.bones) or "") or "",
            string.format("model(\"%s\")%s", n, c and string.format("  animate(m, \"%s\", t)  %.1f/%.1f s",
                          c.name, t % max(c.length, 0.001), c.length) or "   i: the code to draw it"))
    else
      strip(PANEL_W, "3D", "the models and animations of the project")
    end
    hint({ { "up", "UPDOWN", "model" }, { "left", "LEFTRIGHT", "animation" }, { "i", nil, "code" },
           { "space", "A", "play" }, { "enter", nil, "bm Studio" }, { "a", nil, "bm Animator" },
           { "m", nil, "bm Mesh" }, { "f6", nil, "assistant" } })
  end

  local function status()
    if not names[sel] then return "no models: Enter opens bm Studio, F6 asks the assistant" end
    local tris = 0
    for _, f in pairs(nfo) do tris = tris + f.nt end
    return string.format("model %d/%d   %d triangles in all the models   about %d at 60 fps on the ARM",
                         sel, #names, tris, B.TRIS_60FPS)
  end

  P.d3 = { key = key, pad = pad, update = update, draw = draw, status = status, refresh = refresh,
           add_model = add_model, selected = function() return sel end }
end

----------------------------------------------------------------- project page: the hub and the dev kit

local confirm = { what = nil, t = 0 }
-- with unsaved changes the first request only warns; the same again goes on
local function needs_confirm(what, text)
  if not S.dirty then return false end
  if confirm.what == what and confirm.t > 0 then return false end
  confirm.what, confirm.t = what, 150
  say(text or "unsaved changes: choose again to confirm", C.ERR)
  return true
end

local function set_target(t)
  S.target = t
  store_target()
  if t == "b16" then
    say("target .b16 (RGB30): the format is not there yet; the dev kit shows its 8 MiB and what it will not have", C.ACC, 400)
  else
    say("target .bm: the console's cartridge", C.ACC)
  end
end

do
  local HUB = { { head = "OPEN IN" } }
  for _, t in ipairs(TOOLS) do HUB[#HUB + 1] = t end
  HUB[#HUB + 1] = { head = "PROJECT" }
  HUB[#HUB + 1] = { key = "t", act = "title" }
  HUB[#HUB + 1] = { key = "a", act = "author" }
  HUB[#HUB + 1] = { key = "r", act = "res" }
  HUB[#HUB + 1] = { key = "b", act = "target" }
  HUB[#HUB + 1] = { key = "n", act = "new" }
  local row = 2
  local RES_NEXT = { ["640x360"] = "480x270", ["480x270"] = "320x180", ["320x180"] = "256x256", ["256x256"] = "640x360" }

  local function label(h)
    if h.tool then return h.label end
    if h.act == "title" then return "title" end
    if h.act == "author" then return "author" end
    if h.act == "res" then return "screen " .. S.proj.res end
    if h.act == "target" then return "target ." .. S.target end
    return "new from template"
  end

  local function act(h)
    if h.tool then open_in(h)
    elseif h.act == "title" then
      D.ask("title", S.proj.title, function(t) if t ~= "" then S.proj.title = t; touched() end end)
    elseif h.act == "author" then
      D.ask("author", S.proj.author, function(t) S.proj.author = t; touched() end)
    elseif h.act == "res" then S.proj.res = RES_NEXT[S.proj.res] or "640x360"; touched()
    elseif h.act == "target" then set_target(S.target == "bm" and "b16" or "bm")
    elseif h.act == "new" then choose_template() end
  end

  local function move(d)
    repeat row = (row - 1 + d) % #HUB + 1 until not HUB[row].head
  end

  local function key(k)
    if k == "up" then move(-1)
    elseif k == "down" then move(1)
    elseif k == "\n" or k == "ok" then act(HUB[row])
    else
      for i, h in ipairs(HUB) do
        if h.key == k then row = i; act(h); return end
      end
    end
  end

  local function pad()
    if PAD.rp[2] then key("up") elseif PAD.rp[3] then key("down") end
    if btnp(4) then key("ok") end
  end

  local function draw_panel()
    rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
    for i, h in ipairs(HUB) do
      local y = 16 + i * 16
      if h.head then print(h.head, 16, y, C.DIM)
      else
        if i == row then rectfill(0, y, PANEL_W, 16, C.SEL) end
        print(h.key, 8, y, C_KEY)
        print(label(h):sub(1, 17), 24, y, i == row and 0xFFFFFF or C.TEXT)
      end
    end
  end

  local function row_at(y, name, value, tool)
    print(name, INFO_X, y, C.TEXT)
    print(value:sub(1, 34), INFO_X + 96, y, C.TEXT)
    if tool then print(tool, W - #tool * 8 - 8, y, C.DIM) end
  end

  local function bar(x, y, w, frac, c)
    rectfill(x, y + 4, w, 8, C.BAR)
    rectfill(x, y + 4, max(1, floor(w * min(frac, 1))), 8, c)
  end

  -- the last try: a line of performance, one of memory
  local function try_lines()
    local r = S.run
    if not r then return "not tried yet: F5 saves and tries it", nil end
    local a = string.format("%.1f fps   %.1f ms (max %.1f)   %d frames over 16.7 ms", r.fps, r.ms, r.ms_max, r.slow)
    local b = string.format("RAM %s peak (Lua %s, data %s)   %dk instr. at most",
                            kib(r.lua_peak_kb + r.data_kb), kib(r.lua_peak_kb), kib(r.data_kb), r.instr_max // 1000)
    return a, b
  end

  local function draw_hub()
    local p, I = S.proj, S.info
    strip(PANEL_W, p.title, p.author ~= "" and ("by " .. p.author) or "",
          (p.save or "not saved yet") .. (S.dirty and " (modified)" or "") .. "   " .. p.res .. "   target ." .. S.target)
    print("CONTENTS", INFO_X, 48, C.DIM)
    if not I then
      print("measuring...", INFO_X + 96, 64, C.DIM)
    elseif I.error then
      print("cannot measure: " .. I.error:sub(1, 40), INFO_X, 64, C.ERR)
    else
      row_at(64, "code", string.format("%d lines, %s, %s tokens", I.lines, kib(I.bytes / 1024),
                                       I.tokens or "?"), "F2  1")
      row_at(80, "sprites", string.format("%dx%d, %s of %d cells drawn", S.sheet_w, S.sheet_h,
                                          I.cells and tostring(I.cells) or "?", I.ncells), "F3  2")
      row_at(96, "map", string.format("%dx%d, %d layer%s, %d cells", S.map_w, S.map_h, I.layers,
                                      I.layers == 1 and "" or "s", I.map_used), "F3")
      row_at(112, "models", #I.models == 0 and "none" or (#I.models .. ": " .. table.concat(I.models, ", ")), "F4  3")
      row_at(128, "animation", I.rigs == 0 and "none" or string.format("%d skeleton%s, %d animation%s", I.rigs,
             I.rigs == 1 and "" or "s", I.clips, I.clips == 1 and "" or "s"), "F4  4")
      row_at(144, "sound", I.has_audio and ("bank " .. kib(I.audio_kb)) or "no bank", "6")
      row_at(160, "tiles", string.format("%d with flags, %d named zones", I.flagged, I.zones), "F3")
    end
    print("LAST TRY", INFO_X, 176, C.DIM)
    local a, b = try_lines()
    print(a:sub(1, 58), INFO_X, 192, S.run and C.TEXT or C.DIM)
    if b then print(b:sub(1, 58), INFO_X, 208, C.TEXT) end
    print("F1 again: the dev kit (memory, tokens, the file)", INFO_X, 224, C.DIM)
    print("ASSISTANT", INFO_X, 256, C.DIM)
    print("F6: how to make a 2D or a 3D game, step by step", INFO_X, 272, C.TEXT)
    print("\"come faccio un platform?\" \"how do I start a 3D game?\"", INFO_X, 288, C.DIM)
    hint({ { "1", nil, "bm Code" }, { "2", nil, "bm Pixel" }, { "3", nil, "bm Studio" }, { "n", nil, "template" },
           { "f1", "Y", "dev kit" }, { "f5", nil, "try it" }, { "f6", nil, "assistant" } })
  end

  local function draw_devkit()
    local I, r = S.info, S.run
    strip(PANEL_W, "dev kit", "performance, memory and tokens",
          "target ." .. S.target .. (S.target == "b16" and "   (RGB30, 8 MiB: the format is coming, docs/B16.md)" or
                                     "   F11 in any game: the overlay"))
    if not I or I.error then
      print(I and I.error and ("cannot measure: " .. I.error:sub(1, 40)) or "measuring...", INFO_X, 64, C.DIM)
    else
      print("CODE", INFO_X, 48, C.DIM)
      print(string.format("%s tokens   %d lines   %s   %d functions", I.tokens or "?", I.lines, kib(I.bytes / 1024),
                          I.nfuncs), INFO_X, 64, C.TEXT)
      local big = {}
      for k = 1, min(4, #I.funcs) do big[#big + 1] = I.funcs[k][1] .. " " .. I.funcs[k][2] end
      print(("biggest: " .. (#big > 0 and table.concat(big, ", ") or "-")):sub(1, 58), INFO_X, 80, C.DIM)
      print("DATA IN MEMORY WHILE IT RUNS (the Lua comes on top)", INFO_X, 96, C.DIM)
      print(string.format("sheet %dx%d %s   map %dx%d %s", S.sheet_w, S.sheet_h, kib(I.sheet_kb), S.map_w, S.map_h,
                          kib(I.map_kb)), INFO_X, 112, C.TEXT)
      print(string.format("models %s   skeletons %s   sound %s   z-buffer %s", kib(I.mesh_kb), kib(I.anim_kb),
                          kib(I.audio_kb), kib(I.zbuf_kb)), INFO_X, 128, C.TEXT)
      local total = I.data_kb
      print("data " .. kib(total), INFO_X, 144, C.ACC)
      print("CARTRIDGE FILE", INFO_X, 160, C.DIM)
      if I.file_kb then
        local frac = I.file_kb / B16_LIMIT
        bar(INFO_X, 176, 176, frac, frac > 1 and C.ERR or frac > 0.75 and C.ACC or C.OK)
        print(kib(I.file_kb) .. " of 8M (.b16)", INFO_X + 192, 176, frac > 1 and C.ERR or C.TEXT)
      else
        print("not saved yet: Save as, then the size of the file", INFO_X, 176, C.DIM)
      end
      if S.target == "b16" then
        print(#I.b16 == 0 and "nothing the .b16 contract leaves out (so far)" or
              (#I.b16 .. " things a .b16 will not have:"), INFO_X, 192, #I.b16 == 0 and C.OK or C.ACC)
      else
        print(".bm has no cap; b on the left: target .b16 for the RGB30", INFO_X, 192, C.DIM)
      end
    end
    print("LAST TRY (F5)", INFO_X, 208, C.DIM)
    local a, b = try_lines()
    print(a:sub(1, 58), INFO_X, 224, r and C.TEXT or C.DIM)
    if b then print(b:sub(1, 58), INFO_X, 240, C.TEXT) end
    if r then
      print(string.format("%d tokens   %d triangles   3D on the %s   %.0f s", r.tokens, r.tris, r.gpu and "GPU" or "ARM",
                          r.secs), INFO_X, 256, C.TEXT)
    end
    if I and not I.error and S.target == "b16" then
      for k = 1, min(3, #I.b16) do
        local f = I.b16[k]
        print(("line " .. f[1] .. ": " .. f[2]):sub(1, 58), INFO_X, 256 + k * 16, C.ACC)
      end
    else
      print("in the game: stat(1) ms, stat(2) fps, stat(11) tokens,", INFO_X, 288, C.DIM)
      print("stat(12) Lua KiB at most, stat(13) KiB of data", INFO_X, 304, C.DIM)
    end
    hint({ { "f1", "Y", "the project" }, { "b", nil, "target" }, { "f5", nil, "try it" },
           { "f6", nil, "assistant" } })
  end

  local function draw()
    draw_panel()
    if S.hubview == "devkit" then draw_devkit() else draw_hub() end
  end

  local function status()
    if S.hubview == "devkit" then return "the dev kit: F1 again goes back to the project" end
    return "1-6: open the project in the suite   n: from a template   Esc: the menu"
  end

  P.project = { key = key, pad = pad, draw = draw, status = status }
end

----------------------------------------------------------------- menu page

do
  local items, msel = {}, 1

  local function open_other()
    if needs_confirm("open") then return end
    local rows = {}
    for _, f in ipairs(list_files()) do
      rows[#rows + 1] = { f, function() if load_project(f) then show("project") end end }
    end
    if #rows == 0 then say("no .bm files on the SD card", C.ERR); return end
    D.choose("open a cartridge", rows)
  end
  P.menu_open = open_other

  local function build()
    items = {
      { "Continue", function() S.page = S.last end },
      { "New project...   (Ctrl+N)", choose_template },
      { "Open...   (Ctrl+O)", open_other },
      { "Save   (Ctrl+S)", function() if S.proj.save then save_project() else save_as() end end },
      { "Save as...   (Ctrl+Shift+S)", function() save_as() end },
      { "Try the game (F5)", run_project },
      { "New map layer (" .. #mlayers() .. "/8)", function() S.d2view = "map"; show("d2"); P.map.new_layer() end },
      { "Exit bm SDK", function() if not needs_confirm("exit") then quit() end end },
    }
  end

  local function key(k)
    build()
    if k == "up" then msel = (msel - 2) % #items + 1
    elseif k == "down" then msel = msel % #items + 1
    elseif k == "\n" or k == "ok" then items[msel][2]()
    elseif k == "esc" or k == "back" then S.page = S.last end
  end

  local function pad()
    if PAD.rp[2] then key("up") elseif PAD.rp[3] then key("down")
    elseif btnp(4) then key("ok") elseif btnp(5) then key("back") end
  end

  local function draw()
    build()
    rectfill(0, 16, W, HINT_Y - 16, C.BG)
    print("bm SDK", 32, 32, C.ACC)
    print((S.proj.save or "(not saved yet)") .. (S.dirty and "  *modified*" or ""), 96, 32, C.DIM)
    for i, it in ipairs(items) do
      local y = 64 + (i - 1) * 16
      if i == msel then rectfill(24, y, 272, 16, C.SEL) end
      print(it[1], 32, y, i == msel and 0xFFFFFF or C.TEXT)
    end
    local x = 320
    print(S.proj.title, x, 64, C.ACC)
    local keys = { { "f1", "project, dev kit" }, { "f2", "code" }, { "f3", "2D: sprites, map" },
                   { "f4", "3D: models" }, { "f5", "try the game" }, { "f6", "the assistant" } }
    for i, k in ipairs(keys) do chip_hint(k[1], nil, k[2], x, 80 + i * 16) end
    chip_hint("f12", nil, "held: the keys", x, 208)
    print("the suite on this project:", x, 240, C.DIM)
    print("bm Code, Pixel, Studio, Animator,", x, 256, C.DIM)
    print("Mesh, Sound (1-6 on the project page)", x, 272, C.DIM)
    hint({ { "up", "UPDOWN", "choose" }, { "enter", "A", "select" }, { "esc", "B", "back" } })
  end

  P.menu = { key = key, pad = pad, draw = draw, status = function() return "up/down choose, Enter select" end }
end

----------------------------------------------------------------- the assistant

local function open_assistant(err)
  if not assist or not ai then say("the assistant is not here", C.ERR); return end
  if err or S.page == "code" then
    show("code")
    if err and S.err_text then
      assist.open{ error = S.err_text, on_insert = function(c) P.code.insert(c, "from the assistant") end }
    else
      assist.open{ mode = "code", ctx = P.code.word_at(), on_insert = function(c) P.code.insert(c, "from the assistant") end }
    end
  elseif S.page == "d2" and S.d2view == "sprite" then
    assist.open{ mode = "sprite", size = P.sprite.size(), palette = PALETTE, on_sprite = P.sprite.put_sprite }
  elseif S.page == "d3" then
    assist.open{ mode = "mesh", on_mesh = P.d3.add_model }
  else
    -- the guides: how to make a 2D or a 3D game with the SDK; Enter puts
    -- their code in the code page
    assist.open{ mode = "guide", on_insert = function(c) show("code"); P.code.insert(c, "from the guide") end }
  end
end

----------------------------------------------------------------- keys

function reset_pages()
  P.code.reset()
  P.sprite.reset()
  P.map.reset()
  P.d3.refresh()
end

local function go(page)
  show(page)
  if page == "d3" then P.d3.refresh(true) end
end

local PAGES = { "project", "code", "d2", "d3", "menu" }

local function global_key(k)
  if k == "f1" then if S.page == "project" then S.hubview = S.hubview == "hub" and "devkit" or "hub" end go("project")
  elseif k == "f2" then go("code")
  elseif k == "f3" then if S.page == "d2" then S.d2view = S.d2view == "sprite" and "map" or "sprite" end go("d2")
  elseif k == "f4" then go("d3")
  elseif k == "esc" then if S.page == "menu" then S.page = S.last else S.page = "menu" end
  -- the system's keys (the kernel's syskeys.c)
  elseif k == "^s" then if S.proj.save then save_project() else save_as() end
  elseif k == "^S" then save_as()
  elseif k == "^o" then P.menu_open()
  elseif k == "^n" then choose_template()
  elseif k == "^r" or k == "f5" then run_project()
  elseif k == "f6" then open_assistant()
  elseif k == "f9" then if S.err_text then open_assistant(true) else say("no error to explain", C.DIM) end
  else return false end
  return true
end

local SPRITE_KEYS = { up = "up", down = "down", left = "left", right = "right", [" "] = "paint",
  ["\b"] = "erase", del = "erase", x = "pick", f = "fill", ["]"] = "next", ["["] = "prev",
  z = "size", h = "fliph", v = "flipv", ["^c"] = "copy", ["^v"] = "paste", ["^z"] = "undo",
  u = "undo", ["\t"] = "focus", ["\n"] = "ok", pgup = "pgup", pgdn = "pgdn",
  -- the same keys without AltGr/Option: , . on every layout, è + on the Italian one
  [","] = "prev", ["."] = "next", ["\138"] = "prev", ["\130"] = "prev", ["+"] = "next", ["*"] = "next" }

local function page_key(k)
  if S.page == "project" then P.project.key(k)
  elseif S.page == "code" then P.code.key(k)
  elseif S.page == "menu" then P.menu.key(k)
  elseif S.page == "d3" then P.d3.key(k)
  elseif S.d2view == "sprite" then
    local a = SPRITE_KEYS[k] or (k:match("^[0-7]$") and "flag" .. k)
    if a and a ~= "pgup" and a ~= "pgdn" then P.sprite.act(a) end
    if k ~= " " then P.sprite.stop() end
  else
    local a = SPRITE_KEYS[k] or ({ l = "layer", L = "newlayer", o = "only", c = "flags" })[k]
    if a then P.map.act(a) end
    if k ~= " " then P.map.end_stroke() end
  end
end

-- the keys while F12 is held, under the system's (keyhelp(), the kernel
-- shows them): keyboard keys in lower case, the pad's buttons in upper case
local HELP = {
  all = {
    { "f1", "the project; again: the dev kit" },
    { "f2", "the code" },
    { "f3", "2D: the sprites; again: the map" },
    { "f4", "3D: models and animations" },
    { "esc", "the menu" },
    { "ctrl n", "a new project from a template" },
    { "f5 / ctrl r", "try the game" },
    { "f6", "the assistant" },
  },
  project = {
    { "up / down", "choose" },
    { "enter", "do it" },
    { "1 - 6", "open in bm Code, Pixel, Studio, Animator, Mesh, Sound" },
    { "t / a", "title / author" },
    { "r", "the screen" },
    { "b", "target: .bm or .b16 (RGB30)" },
    { "n", "a template" },
  },
  code = {
    { "up down left right", "move" },
    { "home / end", "the start / end of the line" },
    { "pgup / pgdn", "a page up / down" },
    { "tab", "two spaces" },
    { "ctrl x / ctrl k", "cut the line" },
    { "ctrl c / ctrl v", "copy the line / put it above" },
    { "ctrl d", "duplicate the line" },
    { "ctrl g", "go to the error" },
    { "f9", "the assistant explains the error" },
  },
  sprite = {
    { "up down left right", "move" },
    { "space / backspace", "draw / erase" },
    { "x", "pick the colour" },
    { "f", "fill" },
    { ", / .", "colour before / after" },
    { "tab", "choose on the sheet" },
    { "z", "8x8 / 16x16" },
    { "h / v", "flip" },
    { "ctrl c / ctrl v", "copy, paste" },
    { "u", "undo" },
    { "0 - 7", "a flag of the cell (fget): 0 wall, 1 platform, 2 ladder..." },
  },
  map = {
    { "up down left right", "move" },
    { "pgup / pgdn", "move a page" },
    { "space / backspace", "place the tile / clear the cell" },
    { "x", "pick the tile" },
    { "f", "fill" },
    { ", / .", "tile before / after" },
    { "tab", "choose the tile" },
    { "u", "undo" },
    { "l / shift l", "the next layer / a new one" },
    { "o", "this layer only / every layer" },
    { "c", "show the flags of the tiles" },
  },
  d3 = {
    { "up / down", "the model" },
    { "left / right", "the animation" },
    { "space", "play, pause" },
    { "q / e", "turn" },
    { "w / s", "tilt" },
    { "+ / -", "zoom" },
    { "r", "turn by itself" },
    { "i", "the code that draws it" },
    { "enter", "open in bm Studio" },
    { "a / m", "open in bm Animator / bm Mesh" },
  },
  pad = {
    { "Y LEFTRIGHT", "page" },
    { "Y UPDOWN", "the view (dev kit, map)" },
    { "Y B", "menu" },
    { "Y X", "the assistant" },
    { "A / B", "draw / pick (2D)" },
    { "X", "next colour or tile (2D)" },
    { "Y", "the sheet, the tiles (2D)" },
  },
}

local help_page
local function sdk_keyhelp(p)
  if not keyhelp then return end                -- a kernel before them
  local list = {}
  local function add(t) for _, e in ipairs(t) do list[#list + 1] = e end end
  add(HELP.all)
  local name = ({ project = "project", d3 = "3D", sprite = "sprites", map = "map", code = "code" })[p]
  if HELP[p] then list[#list + 1] = name; add(HELP[p])
  else list[#list + 1] = "menu"; add({ { "up / down", "choose" }, { "enter", "select" } }) end
  list[#list + 1] = "pad"
  add(HELP.pad)
  keyhelp(list, "bm SDK")
end

----------------------------------------------------------------- main

local TOOL_NAMES = { code = "bm Code", pixel = "bm Pixel", studio = "bm Studio", animator = "bm Animator",
                     mesh = "bm Mesh", sound = "bm Sound" }

function _init()
  keyp()                                -- typing on: the keyboard types text
  local a = cart_arg()
  if a and a.path and load_project(a.path) then
    local t = saved() or {}
    S.page = "project"
    if a.back or a.from then            -- back from the game or from another program
      S.page = t.page or "project"
      S.hubview, S.d2view = t.hubview or "hub", t.d2view or "sprite"
      if t.code_line then P.code.goto_line(t.code_line) end
      P.sprite.restore(t.sprite)
      P.map.restore(t.map)
    end
    S.last = S.page ~= "menu" and S.page or "project"
    if a.run then S.run = a.run end
    if a.error then
      S.page, S.last = "code", "code"
      S.err_text = a.error
      S.err_line = tonumber(a.error:match("main%.lua:(%d+):"))
      if S.err_line then P.code.goto_line(S.err_line) end
      say("the game stopped: see the line in red (Ctrl+G jumps there, F9 explains it)", C.ERR, 400)
    elseif a.back and a.run then
      local r = a.run
      say(string.format("back from the game: %.1f fps, %.1f ms (max %.1f), RAM %s, %d tokens", r.fps, r.ms, r.ms_max,
                        kib(r.lua_peak_kb + r.data_kb), r.tokens), C.ACC, 400)
    elseif a.back then
      say("back from the game", C.ACC)
    elseif a.from then
      say("back from " .. (TOOL_NAMES[a.from] or a.from) .. ": " .. a.path, C.ACC)
    end
    if S.page == "d3" then P.d3.refresh(true) end
  else
    new_project()
    S.page, S.last = "project", "project"
  end
end

-- Ctrl+Esc or PS (the system's keys): back to bm's menu; with unsaved
-- changes it asks first, and the same again leaves without saving
function _exit()
  return not needs_confirm("exit", "unsaved changes: Ctrl+Esc again leaves without saving")
end

-- what the SDK remembers when it leaves for a game or another program
local remember_base = remember
remember = function()
  local cy = P.code.cursor()
  remember_base({ code_line = cy, sprite = P.sprite.state(), map = P.map.state() })
end

local ytap = { was = false, tap = false, combo = false }

function _update()
  S.frame = S.frame + 1
  if S.msg_t > 0 then S.msg_t = S.msg_t - 1 end
  if confirm.t > 0 then confirm.t = confirm.t - 1 end
  read_pad()
  local hp = S.page == "d2" and S.d2view or S.page
  if help_page ~= hp then help_page = hp; sdk_keyhelp(hp) end
  if assist and assist.update() then return end   -- the assistant has the keys
  P.sprite.lift()
  if P.map.stroking() and not btn(4) then P.map.end_stroke() end

  -- keyboard
  while true do
    local k = keyp()
    if not k then break end
    if D.open() then D.key(k)
    elseif not global_key(k) then page_key(k) end
  end

  -- gamepad: Y with a direction changes page or view, Y + B the menu,
  -- Y + X the assistant; Y alone (a tap) the sheet or the tiles in 2D
  if btn(7) then
    if PAD.rp[0] or PAD.rp[1] then
      local i = 1
      for n, p in ipairs(PAGES) do if p == S.page then i = n end end
      go(PAGES[(i - 1 + (PAD.rp[1] and 1 or -1)) % #PAGES + 1])
      ytap.combo = true
    elseif PAD.rp[2] or PAD.rp[3] then
      if S.page == "project" then S.hubview = S.hubview == "hub" and "devkit" or "hub"
      elseif S.page == "d2" then S.d2view = S.d2view == "sprite" and "map" or "sprite" end
      ytap.combo = true
    elseif btnp(5) then go("menu"); ytap.combo = true
    elseif btnp(6) then open_assistant(); ytap.combo = true end
    return
  elseif ytap.tap then
    ytap.tap = false
    if not ytap.combo and S.page == "d2" then
      if S.d2view == "sprite" then P.sprite.act("focus") else P.map.act("focus") end
    end
    ytap.combo = false
  end
  if D.open() then
    if PAD.rp[2] then D.key("up") elseif PAD.rp[3] then D.key("down")
    elseif btnp(4) then D.key("ok") elseif btnp(5) then D.key("back") end
  elseif S.page == "project" or S.page == "menu" or S.page == "d3" then
    P[S.page].pad()
  elseif S.page == "d2" then
    local act = S.d2view == "sprite" and P.sprite.act or P.map.act
    if PAD.rp[0] then act("left") end
    if PAD.rp[1] then act("right") end
    if PAD.rp[2] then act("up") end
    if PAD.rp[3] then act("down") end
    if btn(4) and (btnp(4) or PAD.rp[0] or PAD.rp[1] or PAD.rp[2] or PAD.rp[3]) then act("paint") end
    if btnp(5) then act("pick") end
    if btnp(6) then act("next") end
  end
  if S.page == "project" then measure_step() end
  if S.page == "d3" then P.d3.update() end
end

function _draw()
  local y = btn(7)                        -- Y: a press and release without a direction
  if ytap.was and not y then ytap.tap = true end
  ytap.was = y
  cls(C.BG)
  if S.page == "project" then P.project.draw()
  elseif S.page == "code" then P.code.draw()
  elseif S.page == "d2" then if S.d2view == "sprite" then P.sprite.draw() else P.map.draw() end
  elseif S.page == "d3" then P.d3.draw()
  else P.menu.draw() end

  -- tab bar: each page with its key, the program's name, the file
  rectfill(0, 0, W, 16, C.BAR)
  local tabs = { { "project", "f1", S.hubview == "devkit" and "dev kit" or "project" }, { "code", "f2", "code" },
                 { "d2", "f3", S.d2view == "map" and "map" or "2D" }, { "d3", "f4", "3D" }, { "menu", "esc", "menu" } }
  local x = 4
  for _, t in ipairs(tabs) do
    local lx = snap(x + prompt(t[2]) + 3)            -- the label on its column
    local w = lx + #t[3] * 8 - x
    if t[1] == S.page then rectfill(x - 4, 0, w + 8, 16, C.SEL) end
    prompt(t[2], x, 0)
    print(t[3], lx, 0, t[1] == S.page and 0xFFFFFF or C.DIM)
    x = x + w + 20
  end
  local name = (S.proj.save or "untitled") .. (S.dirty and "*" or "")
  if snap(x + 4) + 48 < W - #name * 8 - 16 then print("bm SDK", snap(x + 4), 0, C.ACC) end
  print(name, W - #name * 8 - 8, 0, S.dirty and C.ACC or C.DIM)

  -- status bar: a message, or the page's line
  rectfill(0, STATUS_Y, W, H - STATUS_Y, C.BAR)
  local status, sc
  if S.msg_t > 0 and S.msg then status, sc = S.msg, S.msg_c
  elseif S.page == "d2" then status = S.d2view == "sprite" and P.sprite.status() or P.map.status()
  else status, sc = P[S.page].status() end
  print(status:sub(1, #status <= 62 and 62 or 79), 0, STATUS_Y, sc or C.TEXT)
  if #status <= 62 then chip_hint("f12", nil, "held: keys", 528, STATUS_Y) end
  D.draw()
  if assist then assist.draw() end       -- the assistant's panel on top, if open
end
