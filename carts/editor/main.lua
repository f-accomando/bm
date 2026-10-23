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
local U = require "bmui"                 -- the mouse: what the keys do, by clicking
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
  local x0 = x
  x = chips(pad and pad_used() and pad or key, x, y)
  x = print(label, snap(x + 2), y, c or C.DIM)
  U.key_zone(x0, y, x - x0, 16, key)          -- a click: the key
  return x + 12
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
  plank = { "pppppppp", "PpPPpPPp", "dpdddpdd", "........", "........", "........", "........", "........" },
  flag = { "kwwwww..", "kwrrrw..", "kwwwww..", "k.......", "k.......", "k.......", "k.......", "k......." },
}
local INK = { y = 0xFFD050, k = 0x202030, g = 0x5CB048, G = 0x8AD860, b = 0x8A5A30, d = 0x6A4224,
              o = 0xF0B030, Y = 0xFFF4C0, s = 0x606878, S = 0x808898, c = 0x70B8F0, w = 0xFFFFFF,
              r = 0xE84A5A, p = 0xC08850, P = 0xE0B070 }

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
      paint(1, ART.hero); paint(2, ART.ground); paint(6, ART.plank)
      fset(2, 0, true)                   -- the ground: solid (flag 0)
      fset(6, 1, true)                   -- the planks: platforms (flag 1), from above only
      local _, ch = res_cells()
      local base = ch - 5
      for x = 0, 127 do
        if not ((x >= 30 and x <= 33) or (x >= 70 and x <= 74)) then
          for y = base, base + 4 do mset(x, y, 2) end
        end
      end
      local plats = { { 12, 6, 6 }, { 24, 10, 6 }, { 40, 5, 7 }, { 52, 9, 7 }, { 80, 6, 10 }, { 95, 12, 6 } }
      for _, p in ipairs(plats) do
        for x = p[1], p[1] + p[3] - 1 do mset(x, base - p[2], 6) end
      end
      for y = 0, base + 4 do mset(127, y, 2) end
    end,
    code = [==[
-- platform: arrows run, act1 jumps, down drops through a plank.
-- Sprite 1 is the hero; what stops it are the flags of the tiles (F3,
-- keys 0-7): 2 the ground has flag 0 (solid), 6 the planks flag 1
-- (platform: from above only). bmlib does the rest.
local lib = require "bmlib"
lib.input()
local hero = { x = 64, y = 40, w = 8, h = 8 }
local cam = lib.camera({ bounds = { 0, 0, 128 * 8, SCREEN_H } })

function _update()
  hero.vx = 0
  if btn(0) then hero.vx = -2 end
  if btn(1) then hero.vx = 2 end
  hero.drop = btn(3)                     -- down: through the planks
  if hero.ground and btnp("act1") then hero.vy = -5 end
  lib.step(hero)                         -- gravity and the map: hero.ground
  if hero.y > SCREEN_H + 64 then         -- fell: again
    hero.x, hero.y, hero.vy = 64, 40, 0
  end
  cam:follow(hero.x + 4, hero.y + 4)
end

function _draw()
  cls(0x5080C0)
  cam:apply()
  cam:map()
  spr(1, hero.x, hero.y)
  camera()
  local x = prompt("act1", 8, 8, true)
  print(" jump   arrows: run   down: drop", x + 4, 8, 0xFFFFFF)
end
]==] },
  { id = "topdown", name = "Top-down 2D", desc = "walk among walls and pick up the coins",
    paint = function()
      paint(1, ART.hero); paint(3, ART.coin); paint(4, ART.wall)
      fset(4, 0, true)                   -- the walls: solid (flag 0)
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
-- top-down: the arrows walk, pick up the coins; walls and coins: the map.
-- Sprite 1 is the hero, 3 a coin, 4 a wall (flag 0: solid, F3 key 0)
local lib = require "bmlib"
lib.input()
local hero = { x = 40, y = 48, w = 8, h = 8 }
local score = 0

function _update()
  local dx, dy = 0, 0
  if btn(0) then dx = -2 end
  if btn(1) then dx = 2 end
  if btn(2) then dy = -2 end
  if btn(3) then dy = 2 end
  lib.move(hero, dx, dy)                 -- the walls stop it, it slides
  local cx, cy = (hero.x + 4) // 8, (hero.y + 4) // 8
  if mget(cx, cy) == 3 then
    mset(cx, cy, 0)
    score = score + 1
    note(0, 1320, 60, SQUARE, 90)
  end
end

function _draw()
  cls(0x305030)
  map(0, 0, 0, 0, SCREEN_W // 8, SCREEN_H // 8)
  spr(1, hero.x, hero.y)
  print("coins " .. score, 8, 0, 0xFFE070)
end
]==] },
  { id = "shooter", name = "Shooter 2D", desc = "fly, fire, waves of enemies, game over",
    paint = function() paint(1, ART.ship); paint(5, ART.foe) end,
    code = [==[
-- shooter: arrows fly, act1 fires; the enemies come from the right.
-- Sprite 1 is the ship, 5 an enemy
local lib = require "bmlib"
lib.input()
local ship = { x = 40, y = SCREEN_H // 2 }
local shots, foes, t, score, over = {}, {}, 0, 0, false
local fx = lib.particles(200)
local cam = lib.camera({ smooth = 1 })

local function restart()
  ship.y, shots, foes, score, over = SCREEN_H // 2, {}, {}, 0, false
end

function _update()
  fx:update()
  if over then
    if btnp("act1") then restart() end
    return
  end
  t = t + 1
  if btn(0) then ship.x = math.max(0, ship.x - 3) end
  if btn(1) then ship.x = math.min(SCREEN_W // 2, ship.x + 3) end
  if btn(2) then ship.y = math.max(16, ship.y - 3) end
  if btn(3) then ship.y = math.min(SCREEN_H - 16, ship.y + 3) end
  if btnp("act1") then shots[#shots + 1] = { x = ship.x + 8, y = ship.y + 3, w = 4, h = 2 } end
  if t % 40 == 0 then
    foes[#foes + 1] = { x = SCREEN_W, y = math.random(16, SCREEN_H - 24), s = math.random(2, 4) }
  end
  lib.each(shots, function(s) s.x = s.x + 6; return s.x < SCREEN_W end)
  lib.each(foes, function(f)
    f.x = f.x - f.s
    for _, s in ipairs(shots) do
      if not s.dead and lib.hit(s, f) then            -- (w and h 8 if missing)
        s.dead, score = true, score + 10
        fx:burst(f.x + 4, f.y + 4, 14, { colors = { 0xFFE070, 0xE84A5A, 0x603040 } })
        note(1, 180, 120, NOISE, 140)
        return false
      end
    end
    if lib.overlap(f.x + 1, f.y + 1, 6, 6, ship.x + 1, ship.y + 1, 6, 6) then
      over = true
      cam:shake(6, 0.4)
      fx:burst(ship.x + 4, ship.y + 4, 40, { speed = 3, colors = { 0xFFFFFF, 0x70B8F0 } })
    end
    return f.x > -8
  end)
  lib.sweep(shots)
end

function _draw()
  cls(0x080818)
  cam:apply()
  for i = 0, 40 do
    local x = (i * 97 - t * (1 + i % 3)) % SCREEN_W
    pset(x, i * 37 % SCREEN_H, 0x8090B0)
  end
  if not over then spr(1, ship.x, ship.y) end
  for _, s in ipairs(shots) do rectfill(s.x, s.y, 4, 2, 0xFFE070) end
  for _, f in ipairs(foes) do spr(5, f.x, f.y) end
  fx:draw()
  camera()
  print("score " .. score, 8, 0, 0xFFFFFF)
  if over then lib.printc("game over - A plays again", SCREEN_H // 2, 0xFF6060) end
end
]==] },
  { id = "versus", name = "Versus 2D", desc = "two players on one console: punches with hitboxes",
    code = [==[
-- versus: two fighters on one console, each with a controller; alone,
-- the computer plays the second. Left/right walk, act2 punches, act1 jumps.
-- The punches are hitboxes and the bodies hurtboxes (lib.hits): a punch
-- hits once, the one hit flashes and is pushed back.
local lib = require "bmlib"
lib.input()
local FLOOR = SCREEN_H - 64
local party = lib.party({ min = 1, max = 2 })
local hits = lib.hits()
local fighters, winner

local function fighter(slot, pad, x)
  return { slot = slot, pad = pad, x = x, y = FLOOR, w = 16, h = 32, vy = 0, face = 1,
           life = 10, punch = 0, swing = 0, hurt = 0,
           color = lib.PLAYER_COLORS[pad or 2], name = pad and "P" .. pad or "CPU" }
end

local function controls(f, o)
  if not f.pad then                      -- the computer: closer, then a punch
    local d = o.x - f.x
    return d < -20, d > 20, math.abs(d) < 30 and lib.chance(0.06), lib.chance(0.01)
  end
  return btn("left", f.pad), btn("right", f.pad), btnp("act2", f.pad), btnp("act1", f.pad)
end

local function play(f, o)
  local left, right, punch, jump = controls(f, o)
  if f.hurt > 0 then
    f.hurt = f.hurt - 1
    left, right, punch = false, false, false
  end
  if left then f.x = f.x - 2 end
  if right then f.x = f.x + 2 end
  f.face = o.x > f.x and 1 or -1
  if jump and f.y >= FLOOR then f.vy = -6 end
  f.vy = f.vy + 0.3
  f.y = math.min(f.y + f.vy, FLOOR)
  f.x = lib.clamp(f.x, 0, SCREEN_W - f.w)
  if punch and f.punch == 0 then f.punch, f.swing = 16, f.swing + 1 end
  if f.punch > 0 then f.punch = f.punch - 1 end
  hits:hurt(f, f.x, f.y, f.w, f.h)
  if f.punch > 4 and f.punch < 12 then   -- the fist is out: a hitbox
    local x = f.face > 0 and f.x + f.w or f.x - 12
    hits:hit(f, x, f.y + 8, 12, 6, { id = f.swing, damage = 1 })
  end
end

function _update()
  if not fighters then
    local who = party:update()
    if who then
      fighters = { fighter(1, who[1], 120), fighter(2, who[2], SCREEN_W - 136) }
      winner = nil
    end
    return
  end
  if winner then
    if btnp("start") then fighters, party = nil, lib.party({ min = 1, max = 2 }) end
    return
  end
  hits:clear()
  play(fighters[1], fighters[2])
  play(fighters[2], fighters[1])
  lib.separate(fighters[1], fighters[2])
  for _, c in ipairs(hits:check()) do
    c.to.life = c.to.life - c.hit.damage
    c.to.hurt = 12
    c.to.x = c.to.x + c.by.face * 8
    note(2, 140, 90, NOISE, 150)
    if c.to.life <= 0 then winner = c.by end
  end
end

function _draw()
  cls(0x182030)
  rectfill(0, FLOOR + 32, SCREEN_W, SCREEN_H - FLOOR - 32, 0x30384A)
  if not fighters then
    lib.printc("VERSUS", 32, 0xFFD050, 3)
    party:draw(16, 104, SCREEN_W - 32, 208)
    return
  end
  for i, f in ipairs(fighters) do
    rectfill(f.x, f.y, f.w, f.h, f.hurt % 4 > 1 and 0xFFFFFF or f.color)
    if f.punch > 4 and f.punch < 12 then
      rectfill(f.face > 0 and f.x + f.w or f.x - 12, f.y + 8, 12, 6, f.color)
    end
    local bx = i == 1 and 16 or SCREEN_W - 216
    lib.bar(bx, 16, 200, 10, f.life, 10, f.color, 0x303846, 0xFFFFFF)
    print(f.name, bx, 32, f.color)
  end
  if winner then
    lib.printc(winner.name .. " WINS", 120, 0xFFFFFF, 3)
    lib.printc("START: again", 176, 0x9098A8)
  end
end
]==] },
  { id = "online", name = "Online 2D", desc = "two to four consoles on the network: a lobby and lockstep",
    code = [==[
-- online: two to four consoles on the same network, each moves its square;
-- A hosts a match or joins one of those found, the host starts it with
-- Start. Lockstep (bmnet): every console runs the whole game and only the
-- inputs travel, so the game's random numbers come from the match's seed
-- (lib.rng), never from math.random. Over the internet: a relay, see the
-- "relay" option of net.open (tools/overbit_relay.py).
local net = require "bmnet"
local lib = require "bmlib"
lib.input()
local sel, msg, squares, rng = 1, "", {}, nil

function _init()
  local ok, err = net.open({ game = "MYG1", name = "Player" })  -- the game's tag
  if not ok then msg = "no network: " .. tostring(err) end
end

function _leave() net.close() end        -- PS during the match: leaving

-- one frame of the match: the same on every console
local function step(inputs)
  for _, seat in ipairs(net.seats) do
    local s, v = squares[seat], inputs[seat]
    if v == false then squares[seat] = nil end           -- that player left
    if s and v then
      local _, sx, sy = net.unpad(v)
      s.x = lib.clamp(s.x + sx * 3, 0, SCREEN_W - 16)
      s.y = lib.clamp(s.y + sy * 3, 32, SCREEN_H - 16)
      if net.held(v, "a") and not s.a then s.color = rng:int(0x404040, 0xFFFFFF) end
      s.a = net.held(v, "a")
    end
  end
end

function _update()
  for _, e in ipairs(net.update()) do
    if e.type == "start" then
      rng = lib.rng(e.seed)
      for i, seat in ipairs(e.seats) do
        squares[seat] = { x = 40 + i * 80, y = 160, color = lib.PLAYER_COLORS[(i - 1) % 4 + 1] }
      end
    elseif e.type == "join" then msg = e.name .. " is in"
    elseif e.type == "leave" then msg = "a player left"
    elseif e.type == "lost" then msg = "the match is over: " .. e.why
    end
  end
  if net.state == "lobby" then
    local rows = net.hosts()
    if btnp("up") then sel = math.max(1, sel - 1) end
    if btnp("down") then sel = math.min(#rows + 1, sel + 1) end
    sel = math.min(sel, #rows + 1)
    if btnp("ok") then
      if sel == 1 then net.host({ max = 4, info = "squares" }) else net.join(rows[sel - 1].id) end
    end
  elseif net.state == "hosting" then
    if btnp("start") and #net.peers() >= 2 then net.start() end
  elseif net.state == "playing" then
    net.input(net.pad(1))
    for _, inputs in net.frames() do step(inputs) end
  end
end

function _draw()
  cls(0x101828)
  print("ONLINE  " .. net.state, 8, 8, 0xFFD050)
  print(msg, 8, SCREEN_H - 24, 0x9098A8)
  if net.state == "lobby" then
    local rows = net.hosts()
    print((sel == 1 and "> " or "  ") .. "HOST A MATCH", 16, 48, 0xFFFFFF)
    for i, h in ipairs(rows) do
      print((sel == i + 1 and "> " or "  ") .. "JOIN " .. h.name .. " (" .. h.players .. "/" .. h.max .. ")",
            16, 48 + i * 20, 0xFFFFFF)
    end
  elseif net.state == "hosting" or net.state == "joined" or net.state == "joining" then
    for i, p in ipairs(net.peers()) do print("seat " .. p.seat .. "  " .. p.name, 16, 48 + i * 20, 0xFFFFFF) end
    print(net.is_host and "START: begin" or "waiting for the host", 16, 160, 0x9098A8)
  elseif net.state == "playing" then
    for seat, s in pairs(squares) do
      rectfill(s.x, s.y, 16, 16, s.color)
      if seat == net.seat then rect(s.x - 2, s.y - 2, 20, 20, 0xFFFFFF) end
    end
  end
end
]==] },
  { id = "scene3d", name = "3D scene", desc = "floor, cubes, a ball with its shadow, a camera that follows",
    code = [==[
-- 3D scene: arrows move the ball, act1 jumps, the camera follows
local lib = require "bmlib"
lib.input()
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
  if btnp("act1") and by <= 0.5 then vy = 0.18 end
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
  local x = prompt("act1", 8, 8, true)
  print(" jump   arrows: move   " .. stat(4) .. " triangles", x + 4, 8)
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
  { id = "stages", name = "Stages 2D", desc = "title, three stages, an ending and a saved record",
    paint = function()
      paint(1, ART.hero); paint(2, ART.ground); paint(6, ART.plank); paint(7, ART.flag)
      fset(2, 0, true)                   -- the ground: solid (flag 0)
      fset(6, 1, true)                   -- the planks: platforms (flag 1), from above only
      local GAPS = { { { 30, 33 } }, { { 30, 33 }, { 70, 74 } }, { { 24, 27 }, { 50, 54 }, { 90, 94 } } }
      local PLANKS = { { { 12, 6, 6 }, { 40, 5, 7 } }, { { 24, 4, 6 }, { 52, 5, 7 }, { 80, 6, 10 } },
                       { { 12, 5, 6 }, { 40, 4, 7 }, { 64, 5, 6 }, { 100, 6, 8 } } }
      for n = 1, 3 do
        local top = (n - 1) * 48
        for x = 0, 127 do
          local gap = false
          for _, g in ipairs(GAPS[n]) do if x >= g[1] and x <= g[2] then gap = true end end
          if not gap then for y = top + 40, top + 44 do mset(x, y, 2) end end
        end
        for y = top, top + 44 do mset(127, y, 2) end
        for _, p in ipairs(PLANKS[n]) do                -- (x, h, len): the row top + 40 - h
          for x = p[1], p[1] + p[3] - 1 do mset(x, top + 40 - p[2], 6) end
        end
        mset(120, top + 38, 7); mset(120, top + 39, 7)  -- the exit
      end
    end,
    code = [==[
-- stages: three stages on one map, a band of 48 rows each (stage 1 rows
-- 0-47, stage 2 rows 48-95, stage 3 rows 96-143). The exit (tile 7) at the
-- right of each band takes the hero on to the next one. A fall down a gap
-- costs a life (3 at the start) and the hero starts the stage again.
-- Tile 2 is the ground (flag 0: solid), tile 6 a plank (flag 1: a platform,
-- from above only; down drops through it). The time of stage 3 is the record.
local lib = require "bmlib"
local EXIT, ROWS, BAND = 7, 45, 48
local state, sel, stage, top, hero, cam = "title", 1, 1, 0, nil, nil
local timer, lives, from_start, best, reached = 0, 3, true, nil, 1

local function mmss(f)
  local s = f // 60
  return (s // 60) .. ":" .. (s % 60 < 10 and "0" or "") .. (s % 60)
end

local function rows()
  local r = { "NEW GAME" }
  if reached >= 2 then r[2] = "CONTINUE STAGE " .. reached end
  return r
end

local function to_title()
  state, sel = "title", 1
  log("stages: title best " .. tostring(best or "-"))
end

local function start_stage(n)
  stage = n
  top = (n - 1) * BAND
  hero = { x = 16, y = (top + 30) * 8, w = 8, h = 8, vy = 0 }
  cam = lib.camera({ bounds = { 0, top * 8, 128 * 8, (top + ROWS) * 8 } })
  state = "play"
  log("stages: stage " .. n)
end

local function update_title()
  local r = rows()
  if btnp(2) then sel = math.max(1, sel - 1) end
  if btnp(3) then sel = math.min(#r, sel + 1) end
  if btnp("ok") then
    lives, timer = 3, 0
    if sel == 1 then
      from_start = true
      start_stage(1)
    else
      from_start = false
      start_stage(reached)
    end
  end
end

local function update_play()
  timer = timer + 1
  hero.vx = 0
  if btn(0) then hero.vx = -2 end
  if btn(1) then hero.vx = 2 end
  hero.drop = btn(3)                     -- down: through the planks
  if hero.ground and btnp(4) then hero.vy = -5 end
  if mget((hero.x + 4) // 8, (hero.y + 4) // 8) == EXIT then
    reached = math.max(reached, math.min(stage + 1, 3))
    log("stages: clear " .. stage)
    if stage < 3 then
      save({ best = best, reached = reached })
      state = "clear"
      return
    end
    if from_start and (not best or timer < best) then best = timer end
    local ok = save({ best = best, reached = reached })
    log("stages: ending " .. timer)
    if ok then log("stages: saved") end
    state = "ending"
    return
  end
  lib.step(hero)                         -- gravity and the map: hero.ground
  if hero.y > (top + ROWS) * 8 then      -- fell
    lives = lives - 1
    log("stages: lost " .. lives)
    if lives == 0 then
      state = "gameover"
      log("stages: gameover")
    else
      hero.x, hero.y, hero.vy = 16, (top + 30) * 8, 0
    end
  end
  cam:follow(hero.x + 4, hero.y + 4)
end

local function update_clear()
  if btnp("ok") then start_stage(stage + 1) end
end

local function update_gameover()
  if btnp("ok") then to_title() end
end

local function update_ending()
  if btnp("ok") then to_title() end
end

local function draw_title()
  cls(0x101828)
  lib.printc("STAGES", 60, 0xFFD050, 3)
  for i, t in ipairs(rows()) do
    print((sel == i and "> " or "  ") .. t, 96, 150 + i * 16, 0xFFFFFF)
  end
  if best then lib.printc("best " .. mmss(best), 260, 0x9098A8) end
end

local function draw_play()
  cls(0x5080C0)
  cam:apply()
  cam:map()
  spr(1, hero.x, hero.y)
  camera()
  print("stage " .. stage .. "/3  lives " .. lives .. "  " .. mmss(timer), 8, 8, 0xFFFFFF)
end

local function draw_clear()
  cls(0x101828)
  lib.printc("STAGE " .. stage .. " CLEAR", 150, 0xFFFFFF, 2)
  lib.printc("ok: next stage", 200, 0x9098A8)
end

local function draw_gameover()
  cls(0x101828)
  lib.printc("GAME OVER", 150, 0xFF6060, 2)
  lib.printc("ok: title", 200, 0x9098A8)
end

local function draw_ending()
  cls(0x101828)
  lib.printc("ALL CLEAR", 120, 0xFFD050, 2)
  lib.printc("time " .. mmss(timer), 160, 0xFFFFFF)
  if best then lib.printc("best " .. mmss(best), 180, 0x9098A8) end
  lib.printc("ok: title", 220, 0x9098A8)
end

function _init()
  local s = saved()
  best = s and s.best
  reached = s and s.reached or 1
  to_title()
end

function _update()
  if state == "title" then update_title()
  elseif state == "play" then update_play()
  elseif state == "clear" then update_clear()
  elseif state == "gameover" then update_gameover()
  elseif state == "ending" then update_ending() end
end

function _draw()
  if state == "title" then draw_title()
  elseif state == "play" then draw_play()
  elseif state == "clear" then draw_clear()
  elseif state == "gameover" then draw_gameover()
  elseif state == "ending" then draw_ending() end
end
]==] },
}

----------------------------------------------------------------- project

-- a project (.bme) is what the tools change; a game (.bm, .b16) is read
-- only: saving it makes its editable copy (the console asks), and a project
-- builds its game (cart_build)
local function is_project(path) return path and path:lower():match("%.bme$") ~= nil end
local function is_game(path) return path and (path:lower():match("%.bm$") or path:lower():match("%.b16$")) ~= nil end

-- "/bme/pong.bme" -> "/bme/pong.bme"; longer names are cut to 8 characters. A name
-- with no folder goes in /bme; an old project keeps its folder. A game stays as it
-- is: the console makes its copy.
local function short_path(path)
  if is_game(path) then return path end
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or U.PROJECTS
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):lower():gsub("[^%w_]", "")
  if stem == "" then stem = "game" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".bme"
end

local function list_files()
  local out = U.project_files({ bm = true, bme = true, b16 = true })
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
  if is_game(path) then
    say("opened " .. path .. "  (a game: saving makes its editable copy)", C.ACC)
  else
    say("opened " .. path .. "  (saves as " .. S.proj.save .. ")", C.ACC)
  end
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

local store_target

local function save_project()
  if not S.proj.save then return false, "no name yet: use Save as" end
  local ok, e = cart_save(S.proj.save, { title = S.proj.title, author = S.proj.author, res = S.proj.res,
                                         lua = code_text() })
  if ok then
    S.dirty, S.info, S.measuring = false, nil, nil
    if type(e) == "string" then          -- the editable copy of a game, or a new project
      S.proj.save, S.proj.path = e, e
      store_target()
    end
    S.proj.path = S.proj.path or S.proj.save
    say(U.saved_text(S.proj.save), C.ACC)
  else
    say("save failed: " .. tostring(e), C.ERR)
  end
  return ok
end

local D = {}                             -- the dialogs: ask (a line of text), choose (a list)

-- the project's target, remembered for its file (the SDK's saved())
function store_target()
  if not S.proj.save then return end
  local sv = saved() or {}
  sv.targets = sv.targets or {}
  sv.targets[S.proj.save:upper()] = S.target ~= "bm" and S.target or nil
  save(sv)
end

local function save_as(after)
  local def = S.proj.save and S.proj.save:match("([^/]+)$"):gsub("%.[Bb][Mm]$", ".bme"):gsub("%.[Bb]16$", ".bme") or "mygame.bme"
  D.ask("file name (8.3, in /bme)", def, function(t)
    if t == "" then return end
    if not t:lower():match("%.bme$") then t = t:gsub("%.[Bb][Mm]$", ""):gsub("%.[Bb]16$", "") .. ".bme" end
    S.proj.save = U.project_path(t)
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

-- Build .bm / .b16 (Ctrl+B, according to S.target) or Export .b16 (Ctrl+Shift+B):
-- the game of the project, next to it, in Games; a game opened here is saved
-- first (its editable copy)
local function build_project(b16)
  if b16 == nil then b16 = (S.target == "b16") end
  local function go()
    if not is_project(S.proj.save) then
      say("build: a game is built from its project (save it first)", C.ERR)
      return
    end
    local ok, e
    if b16 then ok, e = cart_build(S.proj.save, "b16") else ok, e = cart_build(S.proj.save) end
    if ok then say("built " .. e .. ": it is in Games", C.ACC) else say("build failed: " .. tostring(e), C.ERR) end
  end
  if S.proj.save and not is_project(S.proj.save) then
    if save_project() then go() end
  else
    save_then(go)
  end
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
local B16_LIMIT = 100 * 1024              -- KiB: the Market ceiling for one file (docs/BM-FORMAT.md, File size); the kernel has no cap
local B16_CHECKS = {                     -- (whole names: %f, not "mytime(")
  { "%f[%w_]math%.random", "math.random: left out of the sandbox (the same game on every machine, 6.3)" },
  { "%f[%w_.:]time%(", "time(): no clock (2.9); count the ticks instead" },
  { "%f[%w_.:]save%(", "save(): saves are not designed yet (10)" },
  { "%f[%w_.:]ls%(", "ls() and files: no file system (6.3)" },
  { "%f[%w_.:]cart_%w+%(", "cart_*: the tools' functions, not a game's (6.3)" },
  { "%f[%w_.:]udp_%w+%(", "udp: no network from the system (6.7)" },
  { "%f[%w_.:]require%f[^%w_]", "require: never in the sandbox (6.3)" },
  { "%f[%w_.:]load%(", "load(): never in the sandbox (6.3)" },
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
      else U.line_key(input, k, 47) end
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

  -- the mouse: a click chooses a row (on the chosen one, or a double
  -- click: takes it), the wheel moves, a click outside is Esc
  function D.mouse()
    if pick and U.wheel ~= 0 then pick.sel = clamp(pick.sel - U.wheel, 1, #pick.rows) end
    local z = U.click(0)
    if not z then return end
    if z.kind == "pick" and pick then
      if z.a == pick.sel or U.double then U.press("\n") else pick.sel = z.a end
    elseif z.kind == "shade" then
      U.press("esc")
    elseif z.kind == "field" and input then
      U.line_click(input, 96, 8)               -- the caret goes where the click is
    end
  end

  function D.draw()
    if pick then
      local p = pick
      local rows = min(#p.rows, 14)
      local info = p.rows[p.sel][3]
      local h = (rows + (info and 4 or 2)) * 16
      local y0 = max(32, (H - h) // 32 * 16)
      U.zone(0, 0, W, H, "shade")                  -- a click outside: Esc
      U.zone(40, y0 - 8, 560, h + 8, "box")
      rectfill(40, y0 - 8, 560, h + 8, C.PANEL)        -- (the frame clear of the title's row)
      rect(40, y0 - 8, 560, h + 8, C.ACC)
      local tx = print(p.title, 56, y0, C.ACC) + 16
      chip_hint("esc", "B", "back", chip_hint("enter", "A", "choose", tx, y0), y0)
      local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
      for i = first, min(#p.rows, first + rows - 1) do
        local y = y0 + 16 + (i - first) * 16
        U.zone(48, y, 544, 16, "pick", i)
        if i == p.sel then rectfill(48, y, 544, 16, C.SEL) end
        print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C.TEXT)
      end
      if info then print(info:sub(1, 66), 56, y0 + (rows + 2) * 16, C.DIM) end
    end
    if input then
      U.zone(0, 0, W, H, "shade")
      U.zone(80, 144, 480, 64, "box")
      rectfill(80, 144, 480, 64, C.PANEL)
      rect(80, 144, 480, 64, C.ACC)
      print(input.label, 96, 160, C.DIM)
      U.zone(88, 176, 464, 16, "field")
      U.line_draw(input, 96, 176, C.TEXT, (S.frame // 20) % 2 == 0)
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
    battery battery_low
    quit keymap controller online udp_open udp_send udp_recv udp_close net_ip net_resolve save saved keyp keyheld
    rawkeys keydown keys pad mouse mousep timeslice ls cart_load cart_new cart_save cart_build cart_run cart_tool cart_arg
    cart_read cart_write cart_sheet sheet_png cart_audio cart_put_audio light_begin light light_end fades dark_begin glow
    dark_end note noteoff freq envelope duty playing apu hz slide vibrato arp sfx sfxpos music tempo mute volume
    audio_bank audio_pattern audio_play nnet require SCREEN_W SCREEN_H SQUARE TRIANGLE SAW NOISE SINE METAL math
    string table coroutine ipairs pairs tostring tonumber type select pcall error assert]]):gmatch("%S+") do
    API[w] = true
  end
  for w in ([[mesh mesh_sphere mesh_cube model models bounds3d animate clips bone3d bones3d bone_turn hit3d
    draw3d camera3d light3d sky3d shine3d shadow3d point3d line3d sprite3d world3d world_box world_ray world_move
    world_floor fog3d project3d lamp3d zclear gpu3d cart_data cart_meshes mesh_reduce picture3d cutout3d glb3d res_write res_read]]):gmatch("%S+") do
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
        local pre = i > 4 and l:sub(i - 4, i - 1)
        col = KEYWORDS[w] and C_KW or API3D[w] and C_3D or API[w] and C_API or
              (pre == "lib." or pre == "net.") and C_API or C.TEXT   -- bmlib's, bmnet's
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

  -- the selection (M48): an anchor (sl.ay, sl.ax) and the cursor; drag or double click make it.
  -- Ctrl+C / Ctrl+X / Ctrl+K act on it (else on the line), a letter or Backspace replaces it.
  local sl = { clip = nil, md = false }
  local function sel_range()
    local ay, ax = sl.ay, sl.ax
    if not ay or ay > #S.lines or (ay == cy and ax == cx) then return nil end
    if ay < cy or (ay == cy and ax < cx) then return ay, ax, cy, cx end
    return cy, cx, ay, ax
  end
  local function sel_text()
    local y1, x1, y2, x2 = sel_range()
    if not y1 then return nil end
    local L = S.lines
    if y1 == y2 then return L[y1]:sub(x1 + 1, x2) end
    local t = { L[y1]:sub(x1 + 1) }
    for i = y1 + 1, y2 - 1 do t[#t + 1] = L[i] end
    t[#t + 1] = L[y2]:sub(1, x2)
    return table.concat(t, "\n")
  end
  local function sel_cut()                 -- takes the selected text out; the cursor goes to its start
    local y1, x1, y2, x2 = sel_range()
    sl.ay = nil
    if not y1 then return end
    local L = S.lines
    L[y1] = L[y1]:sub(1, x1) .. L[y2]:sub(x2 + 1)
    for _ = y2, y1 + 1, -1 do table.remove(L, y1 + 1) end
    cy, cx = y1, x1
  end
  local function put_text(s)               -- text (with line breaks) at the cursor
    local L = S.lines
    local head, tail = L[cy]:sub(1, cx), L[cy]:sub(cx + 1)
    local parts = {}
    for p in (s .. "\n"):gmatch("(.-)\n") do parts[#parts + 1] = p end
    L[cy] = head .. parts[1]
    for i = 2, #parts do table.insert(L, cy + i - 1, parts[i]) end
    cy = cy + #parts - 1
    cx = (#parts == 1 and #head or 0) + #parts[#parts]
    L[cy] = L[cy] .. tail
  end

  local function key(k)
    local lines = S.lines
    local l = lines[cy]
    if sel_range() then
      local edit = k == "\n" or k == "\t" or (#k == 1 and k:byte() >= 32)
      if k == "^c" then
        sl.clip, clip_lines = sel_text(), nil
        say("copied: Ctrl+V pastes it")
        return
      elseif k == "^x" or k == "^k" or k == "\b" or k == "del" or edit or (k == "^v" and sl.clip) then
        last_edit = nil
        snapshot("sel")
        if k == "^x" or k == "^k" then sl.clip, clip_lines = sel_text(), nil end
        sel_cut()
        if k == "^v" then put_text(sl.clip) end
        code_touched()
        if not edit then clamp_cursor(); last_edit = nil; return end
        lines = S.lines
        l = lines[cy]
      end
    end
    sl.ay = nil
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
      clip_lines, sl.clip = { l }, nil
      if #lines > 1 then table.remove(lines, cy) else lines[1] = "" end
      last_edit = nil
      code_touched()
    elseif k == "^c" then
      clip_lines, sl.clip = { l }, nil
      say("line copied: Ctrl+V puts it above the cursor")
    elseif k == "^v" and sl.clip then
      snapshot("paste")
      put_text(sl.clip)
      last_edit = nil
      code_touched()
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
    local sy1, sx1, sy2, sx2 = sel_range()
    for r = 0, ROWS - 1 do
      local i = top + r
      local y = 16 + r * 16
      if i > #lines then break end
      if sy1 and i >= sy1 and i <= sy2 then          -- the selection under the letters
        local a0 = (i == sy1 and sx1 or 0) - left
        local a1 = (i == sy2 and sx2 or #lines[i] + 1) - left
        if a0 < 0 then a0 = 0 end
        if a1 > VISIBLE_COLS then a1 = VISIBLE_COLS end
        if a1 > a0 then rectfill((GUTTER + a0) * 8, y, (a1 - a0) * 8, 16, C.SEL) end
      end
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

  -- the mouse: a click puts the cursor there, the wheel scrolls (the
  -- cursor goes with the page); the context menu has the line's keys
  local function mouse()
    if U.wheel ~= 0 and U.inside(0, 16, W, ROWS * 16) then
      local d = clamp(-U.wheel * 3, 1 - top, max(0, #S.lines - ROWS) - top + 1)
      top, cy = top + d, cy + d
      clamp_cursor()
    end
    if U.pressed(0) and U.inside(0, 16, W, ROWS * 16) then
      cy = top + (U.y - 16) // 16
      cx = left + (U.x + 4) // 8 - GUTTER
      last_edit = nil
      clamp_cursor()
      sl.ay, sl.ax, sl.md = cy, cx, true         -- where a drag starts
      if U.double then                           -- a double click: the word
        local l = S.lines[cy]
        local a, b = cx, cx
        while a > 0 and l:sub(a, a):match("[%w_]") do a = a - 1 end
        while b < #l and l:sub(b + 1, b + 1):match("[%w_]") do b = b + 1 end
        if b > a then sl.ay, sl.ax, cx = cy, a, b end
      end
    elseif sl.md and U.down(0) then
      if U.drag then                             -- a drag: the cursor follows, the anchor stays
        cy = top + clamp((U.y - 16) // 16, -1, ROWS)
        cx = left + (U.x + 4) // 8 - GUTTER
        clamp_cursor()
      end
    else
      sl.md = false
    end
  end

  local function menu_items()
    local list = { { "Cut the line", "^x" }, { "Copy the line", "^c" }, { "Paste", "^v" }, { "Duplicate", "^d" },
                   { "Undo", "^z" } }
    if S.err_line then
      list[#list + 1] = { "Go to the error", "^g" }
      list[#list + 1] = { "Explain the error", "f9" }
    end
    return list
  end

  P.code = { key = key, draw = draw, status = status, insert = insert, word_at = word_at, mouse = mouse,
             menu_items = menu_items,
             goto_line = function(n) cy, cx = clamp(n, 1, #S.lines), 0; clamp_cursor() end,
             cursor = function() return cy, cx end,
             reset = function() cx, cy, top, left, undo, last_edit = 0, 1, 1, 0, {}, nil; sl.ay = nil end }
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
    U.zone(CX, CY, n * z, n * z, "canvas", z)
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
    U.zone(SX, SY, vw, vh, "sheet", scx, scy)
    sspr(scx, scy, vw, vh, SX, SY)
    clip(SX, SY, vw, vh)
    rect(SX + sx - scx - 1, SY + sy - scy - 1, n + 2, n + 2, focus == "sheet" and C.ACC or 0xFFFFFF)
    clip()
    -- the palette, the transparent swatch, the colour
    for i, c in ipairs(PALETTE) do
      local x, y = SX + ((i - 1) % 16) * 16, 240 + ((i - 1) // 16) * 14
      U.zone(x, y, 16, 14, "colour", i)
      rectfill(x, y, 15, 13, c)
      if c == color and not transparent then rect(x - 1, y - 1, 17, 15, 0xFFFFFF) end
    end
    checker(SX + 264, 240, 28, 27, 4)
    U.zone(SX + 264, 240, 28, 27, "colour", 0)
    if transparent then rect(SX + 263, 239, 30, 29, 0xFFFFFF) end
    rectfill(SX, 272, 20, 16, 0x000000)
    if transparent then checker(SX + 1, 273, 18, 14, 4) else rectfill(SX + 1, 273, 18, 14, color) end
    print(transparent and "transparent" or string.format("#%06X", color), SX + 32, 272, C.TEXT)
    -- the flags of the cell: what the tile is to the game (bmlib's convention)
    print("FLAGS", SX, 288, C.DIM)
    for f = 0, 7 do
      local x = SX + 48 + f * 24
      U.key_zone(x - 4, 288, 16, 16, tostring(f))   -- a click: the flag on / off
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

  -- the mouse: on the canvas the left button draws (held: it goes on), the
  -- middle one picks the colour, the wheel changes it; a click on the sheet
  -- chooses the cell, on a colour takes it
  local function mouse()
    local z = U.at()
    if not z then return end
    if z.kind == "canvas" then
      focus = "canvas"
      local n = size * 8
      if U.down(0) or U.pressed(2) then
        px, py = clamp((U.x - CX) // z.a, 0, n - 1), clamp((U.y - CY) // z.a, 0, n - 1)
        act(U.pressed(2) and "pick" or "paint")
      end
      if U.wheel ~= 0 then act(U.wheel > 0 and "prev" or "next") end
    elseif z.kind == "sheet" and U.pressed(0) then
      local cols = cells_per_row()
      local cx, cy = (z.a + U.x - SX) // 8, (z.b + U.y - SY) // 8
      sel = cy * cols + cx
      move_sel(0, 0)
      focus = "canvas"
    elseif z.kind == "colour" and U.pressed(0) then
      if z.a == 0 then transparent = true else color, transparent = PALETTE[z.a], false end
    end
  end

  local function menu_items()
    return { { "Pick the colour", "x" }, { "Fill", "f" }, { "Erase", "\b" }, { "8x8 / 16x16", "z" },
             { "Mirror", "h" }, { "Upside down", "v" }, { "Copy", "^c" }, { "Paste", "^v" }, { "Undo", "u" } }
  end

  P.sprite = { act = act, draw = draw, status = status, put_sprite = put_sprite, mouse = mouse,
               menu_items = menu_items,
               size = function() return size * 8 end,
               lift = function() stroke = stroke and (btn(4) or U.down(0) or false) end,
               stop = function() stroke = false end,
               state = function() return { sel = sel, size = size } end,
               restore = function(t) if t then sel, size = t.sel or 0, t.size or 2 end end,
               reset = function() sel, size, px, py, s_undo, focus = 0, 2, 0, 0, {}, "canvas" end }
end

do
  local mx, my, vx0, vy0 = 0, 0, 0, 0    -- cursor cell, top-left cell of the view
  local tile, picking = 1, false
  local layer, only, show_flags = 1, false, false   -- the layer edited; drawn alone; the flags over it
  -- big (z): the 16x16 brush, a tile of 2 x 2 cells (n, n + 1 and the two under them on the sheet), as the
  -- 16x16 tiles of a big world (Yharnam's): the cursor and the tile on even cells
  -- zoom (+ / -, m): pixels per 8x8 cell (1 = 256x256 overview in one screen, 2, 4, 8 = 1:1, 16 = 2x in)
  local big, mini, zoom = false, false, 8
  local used, used_mode, used_rev = {}, false, -1
  local cell_col, row_runs, col_rev = {}, {}, -1
  local m_undo, m_stroke = {}, nil
  local VIEW_Y = 48
  local VIEW_CW, VIEW_CH = W // 8, (HINT_Y - VIEW_Y) // 8
  local OBJ_COL = { [1024] = 0xFFD860, [1026] = 0x9A6438, [1028] = 0x8A96A8, [1030] = 0xE04040, [1032] = 0xFFC888, [1034] = 0x70A0D0 }
  local view3d = false
  local w3d = nil
  local w3d_edits = -1
  local cam_yaw = 0.0
  local cam_pitch = -0.5
  local cam_dist = 12.0
  local tool_mode = nil
  local tool_p1 = nil
  local terrain = nil                    -- the terrain brush: the base tile of the 16-tile set, or nil
  local path_width = 1
  local obj_counts = { apples = 0, crates = 0, lamps = 0, floor = 0, tri = 0 }
  local obj_rev = -1

  local function view_dims(z)
    z = z or zoom
    return min(S.map_w, W // z), min(S.map_h, (HINT_Y - VIEW_Y) // z)
  end

  local function clamp_view()
    local s = big and 2 or 1
    mx = clamp(mx, 0, max(0, S.map_w - s))
    my = clamp(my, 0, max(0, S.map_h - s))
    local cw, ch = view_dims()
    if mx < vx0 then vx0 = mx elseif mx + s > vx0 + cw then vx0 = mx + s - cw end
    if my < vy0 then vy0 = my elseif my + s > vy0 + ch then vy0 = my + s - ch end
    vx0 = clamp(vx0, 0, max(0, S.map_w - cw))
    vy0 = clamp(vy0, 0, max(0, S.map_h - ch))
  end

  local function set_zoom(z, msg)
    zoom = z
    clamp_view()
    if msg then say(msg) end
  end

  local ZOOM_STEPS = { 1, 2, 4, 8, 16 }   -- px per cell (as in sdk_b16)
  local function step_zoom(d)
    local idx = 4
    for i, z in ipairs(ZOOM_STEPS) do if z == zoom then idx = i end end
    local nxt = ZOOM_STEPS[clamp(idx + d, 1, #ZOOM_STEPS)]
    local labels = { [1] = "1:8 overview (256x256 in one screen)", [2] = "1:4 (2 px/cell)",
                     [4] = "1:2 (4 px/cell)", [8] = "1:1 (8 px/cell)", [16] = "2x (16 px/cell)" }
    set_zoom(nxt, "map zoom " .. (labels[nxt] or (nxt .. " px")))
  end

  -- average colour of cell c on the sheet (cached per edit) for zoomed-out map rendering
  local function color_of_cell(c)
    if c <= 0 then return nil end
    if col_rev ~= S.edits then cell_col, row_runs, col_rev = {}, {}, S.edits end
    local col = cell_col[c]
    if col ~= nil then return col or nil end
    if OBJ_COL[c] or OBJ_COL[c - 1] or OBJ_COL[c - 32] or OBJ_COL[c - 33] then
      col = OBJ_COL[c] or OBJ_COL[c - 1] or OBJ_COL[c - 32] or OBJ_COL[c - 33]
      cell_col[c] = col
      return col
    end
    local cols = S.sheet_w // 8
    local sx, sy = (c % cols) * 8, (c // cols) * 8
    local r, g, b, n = 0, 0, 0, 0
    for j = 1, 6, 2 do
      for i = 1, 6, 2 do
        local p = sget(sx + i, sy + j)
        if p then
          r = r + (p >> 16 & 255)
          g = g + (p >> 8 & 255)
          b = b + (p & 255)
          n = n + 1
        end
      end
    end
    col = n > 0 and ((r // n) << 16 | (g // n) << 8 | (b // n)) or false
    cell_col[c] = col
    return col or nil
  end

  -- horizontal runs { x0, len, col } for map row y (cached per edit and layer mode)
  local function runs_of_row(y, nlayers)
    if col_rev ~= S.edits then cell_col, row_runs, col_rev = {}, {}, S.edits end
    local key = (only and layer or (nlayers * 16 + layer)) * 512 + y
    local r = row_runs[key]
    if r then return r end
    r = {}
    local x, w = 0, S.map_w
    while x < w do
      local c = nil
      if only then
        c = color_of_cell(mget(x, y, layer))
      else
        for l = nlayers, 1, -1 do
          c = color_of_cell(mget(x, y, l))
          if c then break end
        end
      end
      if not c then
        x = x + 1
      else
        local x0 = x
        x = x + 1
        while x < w do
          local c2 = nil
          if only then
            c2 = color_of_cell(mget(x, y, layer))
          else
            for l = nlayers, 1, -1 do
              c2 = color_of_cell(mget(x, y, l))
              if c2 then break end
            end
          end
          if c2 ~= c then break end
          x = x + 1
        end
        r[#r + 1] = { x0, x - x0, c }
      end
    end
    row_runs[key] = r
    return r
  end

  -- the 16x16 tiles already in use across the map (for the compact Tab palette on large worlds)
  local function refresh_used()
    local rev = S.edits * 16 + (only and layer or 0)
    if used_rev == rev then return end
    used_rev = rev
    local seen, list = {}, {}
    local cols = S.sheet_w // 8
    local l0, l1 = only and layer or 1, only and layer or #mlayers()
    for l = l0, l1 do
      for y = 0, S.map_h - 1, 2 do
        for x = 0, S.map_w - 1, 2 do
          local v = mget(x, y, l)
          if v > 0 then
            local t = (v // cols) // 2 * 2 * cols + (v % cols) // 2 * 2
            if not seen[t] then seen[t] = true; list[#list + 1] = t end
          end
        end
      end
    end
    table.sort(list)
    used = list
  end

  local function set_cell(x, y, v)
    if x < 0 or y < 0 or x >= S.map_w or y >= S.map_h then return end
    local old = mget(x, y, layer)
    if old == v then return end
    if m_stroke then m_stroke[#m_stroke + 1] = { x, y, old, layer } end
    mset(x, y, v, layer)
    touched()
  end

  -- the cells the brush puts for tile t: { dx, dy, cell }
  local function brush(t)
    if not big then return { { 0, 0, t } } end
    local c = S.sheet_w // 8
    return { { 0, 0, t }, { 1, 0, t + 1 }, { 0, 1, t + c }, { 1, 1, t + c + 1 } }
  end

  -- with the 16x16 brush, the cursor and the tile on even cells
  local function even()
    if not big then return end
    local c = S.sheet_w // 8
    mx, my = mx - mx % 2, my - my % 2
    tile = (tile // c) // 2 * 2 * c + (tile % c) // 2 * 2
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
    if names[layer] == "objects" then
      big = true
      local cur = mget(mx, my, layer)
      if cur > 0 then
        tile = cur
      elseif tile < 1024 then
        for y = 0, S.map_h - 1, 2 do
          for x = 0, S.map_w - 1, 2 do
            local v = mget(x, y, layer)
            if v > 0 and (tile < 1024 or v < tile) then tile = v end
          end
        end
      end
      even()
    end
    say(string.format("map layer %d/%d: %s   (map(..., \"%s\"))", layer, #names, names[layer], names[layer]), C.ACC)
  end

  -- the fill: the brush's cells over the area of what is under it (with the 16x16 brush, the 2 x 2 tiles
  -- equal to the one under the cursor)
  local function map_fill(x0, y0)
    local b, s = brush(tile), big and 2 or 1
    local target, same = {}, true
    for i, e in ipairs(b) do
      target[i] = mget(x0 + e[1], y0 + e[2], layer)
      if target[i] ~= e[3] then same = false end
    end
    if same then return end
    local stack, count = { { x0, y0 } }, 0
    while #stack > 0 and count < 20000 do
      local p = table.remove(stack)
      local x, y = p[1], p[2]
      local ok = x >= 0 and y >= 0 and x + s <= S.map_w and y + s <= S.map_h
      for i, e in ipairs(b) do
        if ok and mget(x + e[1], y + e[2], layer) ~= target[i] then ok = false end
      end
      if ok then
        for _, e in ipairs(b) do set_cell(x + e[1], y + e[2], e[3]) end
        count = count + #b
        stack[#stack + 1] = { x + s, y }; stack[#stack + 1] = { x - s, y }
        stack[#stack + 1] = { x, y + s }; stack[#stack + 1] = { x, y - s }
      end
    end
  end

  local function end_stroke()
    if m_stroke then
      if #m_stroke > 0 then m_undo[#m_undo + 1] = m_stroke end
      m_stroke = nil
    end
  end

  local function find_layer(name)
    local names = mlayers()
    for i, n in ipairs(names) do
      if n == name then return i end
    end
    return nil
  end

  local function refresh_stats()
    if obj_rev == S.edits then return end
    obj_rev = S.edits
    local names = mlayers()
    local fl_idx, obj_idx = nil, nil
    for i, n in ipairs(names) do
      if n == "floor" then fl_idx = i end
      if n == "objects" then obj_idx = i end
    end
    local na, nc, nl, nf = 0, 0, 0, 0
    local step = big and 2 or 1
    if obj_idx then
      for y = 0, S.map_h - 1, step do
        for x = 0, S.map_w - 1, step do
          local v = mget(x, y, obj_idx)
          if v == 1026 then na = na + 1
          elseif v == 1028 then nc = nc + 1
          elseif v == 1032 then nl = nl + 1 end
        end
      end
    end
    if fl_idx then
      for y = 0, S.map_h - 1, step do
        for x = 0, S.map_w - 1, step do
          if mget(x, y, fl_idx) > 0 then nf = nf + 1 end
        end
      end
    end
    local tri = nf * 4 + na * 60 + nc * 36 + nl * 30
    obj_counts = { apples = na, crates = nc, lamps = nl, floor = nf, tri = tri }
  end

  local function set_cell_layer(x, y, v, tgt_l)
    tgt_l = tgt_l or layer
    if x < 0 or y < 0 or x >= S.map_w or y >= S.map_h then return end
    local old = mget(x, y, tgt_l)
    if old == v then return end
    if m_stroke then m_stroke[#m_stroke + 1] = { x, y, old, tgt_l } end
    mset(x, y, v, tgt_l)
    touched()
  end

  local function draw_brush_layer(cx, cy, t, tgt_l)
    tgt_l = tgt_l or layer
    for _, e in ipairs(brush(t)) do
      set_cell_layer(cx + e[1], cy + e[2], e[3], tgt_l)
    end
  end

  local function commit_path(p1, p2)
    local tgt_l = find_layer("floor") or layer
    local x1, y1 = p1[1], p1[2]
    local x2, y2 = p2[1], p2[2]
    local s = big and 2 or 1
    local dx = x2 - x1
    local dy = y2 - y1
    local steps = max(math.abs(dx) // s, math.abs(dy) // s)
    m_stroke = {}
    if steps == 0 then
      for k = 0, path_width - 1 do
        local px = x1 + k * s
        if px + (big and 2 or 1) <= S.map_w then
          draw_brush_layer(px, y1, tile, tgt_l)
        end
      end
    else
      local vert = math.abs(dy) >= math.abs(dx)
      for i = 0, steps do
        local t_ratio = i / steps
        local cx = (math.floor((x1 + t_ratio * dx) / s + 0.5)) * s
        local cy = (math.floor((y1 + t_ratio * dy) / s + 0.5)) * s
        if vert then
          for k = 0, path_width - 1 do
            local px = cx + k * s
            if px + (big and 2 or 1) <= S.map_w then
              draw_brush_layer(px, cy, tile, tgt_l)
            end
          end
        else
          for k = 0, path_width - 1 do
            local py = cy + k * s
            if py + (big and 2 or 1) <= S.map_h then
              draw_brush_layer(cx, py, tile, tgt_l)
            end
          end
        end
      end
    end
    end_stroke()
  end

  local function commit_apples(p1, p2)
    local tgt_l = find_layer("objects") or layer
    local x1, y1 = p1[1], p1[2]
    local x2, y2 = p2[1], p2[2]
    local s = big and 2 or 1
    local dx = x2 - x1
    local dy = y2 - y1
    local steps = max(math.abs(dx) // s, math.abs(dy) // s)
    m_stroke = {}
    for i = 0, steps do
      local t_ratio = (steps > 0) and (i / steps) or 0
      local cx = (math.floor((x1 + t_ratio * dx) / s + 0.5)) * s
      local cy = (math.floor((y1 + t_ratio * dy) / s + 0.5)) * s
      draw_brush_layer(cx, cy, 1026, tgt_l)
    end
    end_stroke()
  end

  local function place_crate()
    local tgt_l = find_layer("objects") or layer
    m_stroke = {}
    draw_brush_layer(mx, my, 1028, tgt_l)
    end_stroke()
    say("crate placed (1028)")
  end

  local function place_lamp()
    local tgt_l = find_layer("objects") or layer
    m_stroke = {}
    draw_brush_layer(mx, my, 1032, tgt_l)
    end_stroke()
    say("lamp placed (1032)")
  end

  local function place_marker(id, name)
    local tgt_l = find_layer("objects") or layer
    m_stroke = {}
    draw_brush_layer(mx, my, id, tgt_l)
    end_stroke()
    say(name .. " placed (" .. id .. ")")
  end

  local function act(a)
    if a == "view3d" then
      view3d = not view3d
      say(view3d and "3D preview (arrows: orbit/zoom, Shift+Up/Down: tilt, v: 2D)" or "2D map editor")
      return
    end
    if a == "rename_layer" then
      local names = mlayers()
      local cur = names[layer] or ("layer" .. layer)
      D.ask("layer name", cur, function(t)
        if t and t ~= "" and t ~= cur then
          local list = {}
          for i, n in ipairs(names) do
            table.insert(list, i == layer and { t, i } or { n, i })
          end
          mlayers(list)
          say("layer renamed to: " .. t)
          touched()
        end
      end)
      return
    end
    if a == "map3d_template" then
      local names = mlayers()
      local existing = {}
      for i, n in ipairs(names) do existing[n] = i end
      local target = { "floor", "height", "walls", "objects" }
      local list = {}
      for _, req in ipairs(target) do
        if existing[req] then
          table.insert(list, { req, existing[req] })
        else
          table.insert(list, req)
        end
      end
      mlayers(list)
      big = true
      say("3D map template ready: floor, height, walls, objects")
      touched()
      return
    end
    if a == "tool_path" then
      if tool_mode == "path" and tool_p1 then
        commit_path(tool_p1, { mx, my })
        tool_mode = nil; tool_p1 = nil
        say(string.format("path drawn (width %d)", path_width))
      else
        tool_mode = "path"
        tool_p1 = nil
        say(string.format("path tool (width %d): press Enter at start cell", path_width))
      end
      return
    end
    if a == "tool_apples" then
      if tool_mode == "apples" and tool_p1 then
        commit_apples(tool_p1, { mx, my })
        tool_mode = nil; tool_p1 = nil
        say("apples line placed (1026)")
      else
        tool_mode = "apples"
        tool_p1 = nil
        say("apples line tool: press Enter at start cell")
      end
      return
    end
    if a == "tool_crates" then
      place_crate()
      return
    end
    if a == "tool_lamps" then
      place_lamp()
      return
    end
    if a == "tool_start" then
      place_marker(1024, "start")
      return
    end
    if a == "tool_goal" then
      place_marker(1030, "goal")
      return
    end
    if a == "cycle_width" then
      path_width = (path_width % 5) + 1
      say(string.format("path width: %d", path_width))
      return
    end
    if a == "ok" and not picking then
      if tool_mode == "path" then
        if not tool_p1 then
          tool_p1 = { mx, my }
          say(string.format("path start at (%d,%d): move to end cell and press Enter", mx, my))
        else
          commit_path(tool_p1, { mx, my })
          tool_mode = nil; tool_p1 = nil
          say(string.format("path drawn (width %d)", path_width))
        end
        return
      elseif tool_mode == "apples" then
        if not tool_p1 then
          tool_p1 = { mx, my }
          say(string.format("apples start at (%d,%d): move to end cell and press Enter", mx, my))
        else
          commit_apples(tool_p1, { mx, my })
          tool_mode = nil; tool_p1 = nil
          say("apples line placed (1026)")
        end
        return
      end
    end
    local s = big and 2 or 1               -- a step of the cursor: a cell, or a 16x16 tile
    if a == "terrain" and not view3d then
      terrain = (not terrain) and tile or nil
      say(terrain and string.format("TERRAIN base %d: space paints, backspace erases, borders choose themselves (t: off)", terrain)
                  or "terrain brush off")
      return
    end
    if view3d then
      local shift = keydown(0xE1) or keydown(0xE5)
      if a == "left" then cam_yaw = cam_yaw - 0.1
      elseif a == "right" then cam_yaw = cam_yaw + 0.1
      elseif a == "up" then
        if shift then cam_pitch = min(-0.08, cam_pitch + 0.08)
        else cam_dist = max(2.0, cam_dist - 1.0) end
      elseif a == "down" then
        if shift then cam_pitch = max(-1.4, cam_pitch - 0.08)
        else cam_dist = min(40.0, cam_dist + 1.0) end
      elseif a == "next" then tile = tile + s
      elseif a == "prev" then tile = max(0, tile - s)
      end
      even()
      return
    end
    local cols = S.sheet_w // 8
    local cw, ch = view_dims()
    if picking then
      if a == "size" and big and #used > 0 then
        used_mode = not used_mode
        say(used_mode and ("used 16x16 tiles (" .. #used .. " in map)") or "full sprite sheet")
        return
      end
      if used_mode and #used > 0 then
        local ucols = min(8, max(4, #used))
        local idx = 1
        for i, t in ipairs(used) do if t == tile then idx = i; break end end
        if a == "left" then idx = max(1, idx - 1)
        elseif a == "right" then idx = min(#used, idx + 1)
        elseif a == "up" then idx = max(1, idx - ucols)
        elseif a == "down" then idx = min(#used, idx + ucols)
        elseif a == "paint" or a == "ok" or a == "focus" then picking = false end
        tile = used[idx] or tile
        return
      end
      if a == "left" then tile = max(0, tile - s)
      elseif a == "right" then tile = tile + s
      elseif a == "up" then tile = max(0, tile - cols * s)
      elseif a == "down" then tile = tile + cols * s
      elseif a == "paint" or a == "ok" or a == "focus" then picking = false end
      tile = min(tile, cols * (S.sheet_h // 8) - 1)
      even()
      return
    end
    if a == "left" then mx = mx - s elseif a == "right" then mx = mx + s
    elseif a == "up" then my = my - s elseif a == "down" then my = my + s
    elseif a == "pgup" then my = my - max(s, ch) elseif a == "pgdn" then my = my + max(s, ch)
    elseif a == "pgleft" then mx = mx - max(s, cw) elseif a == "pgright" then mx = mx + max(s, cw)
    elseif terrain and (a == "paint" or a == "erase") then
      m_stroke = m_stroke or {}
      B.autotile_paint(function(x, y) return mget(x, y, layer) end,
                       function(x, y, t) for _, e in ipairs(brush(t)) do set_cell(x + e[1], y + e[2], t == 0 and 0 or e[3]) end end,
                       mx, my, terrain, s, cols, a == "erase", S.map_w, S.map_h)
      if a == "erase" then end_stroke() end
    elseif a == "paint" then
      m_stroke = m_stroke or {}
      for _, e in ipairs(brush(tile)) do set_cell(mx + e[1], my + e[2], e[3]) end
    elseif a == "erase" then
      m_stroke = {}
      for _, e in ipairs(brush(tile)) do set_cell(mx + e[1], my + e[2], 0) end
      end_stroke()
    elseif a == "pick" then tile = mget(mx, my, layer)
    elseif a == "fill" then m_stroke = {}; map_fill(mx, my); end_stroke()
    elseif a == "size" then
      big = not big
      say(big and "16x16: a tile of 2 x 2 cells, on even cells" or "8x8: one cell")
    elseif a == "layer" then next_layer()
    elseif a == "newlayer" then next_layer(true)
    elseif a == "only" then only = not only; say(only and "this layer only" or "every layer")
    elseif a == "flags" then show_flags = not show_flags; say(show_flags and "the flags of the tiles (fget)" or "flags hidden")
    elseif a == "mini" then
      if zoom == 1 then
        set_zoom(8, "map 1:1 (8 px/cell)")
      else
        set_zoom(1, "overview: 256x256 map in one screen (arrows/click move, m: back to 1:1)")
      end
    elseif a == "zoom_in" then step_zoom(1)
    elseif a == "zoom_out" then step_zoom(-1)
    elseif a == "next" then tile = tile + s elseif a == "prev" then tile = max(0, tile - s)
    elseif a == "undo" then
      local u = table.remove(m_undo)
      if u then for i = #u, 1, -1 do mset(u[i][1], u[i][2], u[i][3], u[i][4]) end; touched(); say("undo") end
    elseif a == "focus" then
      refresh_used()
      used_mode = big and #used > 0
      picking = true
    end
    even()
    clamp_view()
  end

  local function draw()
    if view3d then
      rectfill(0, VIEW_Y, W, HINT_Y - VIEW_Y, 0x182430)
      U.zone(0, VIEW_Y, W, HINT_Y - VIEW_Y, "map3dview")
      clip(0, VIEW_Y, W, HINT_Y - VIEW_Y)

      if not w3d or w3d_edits ~= S.edits then
        w3d = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25 })
        w3d_edits = S.edits
      end

      local target_x = (mx // 2 + 0.5) * 1.0
      local target_z = (my // 2 + 0.5) * 1.0
      local target_y = (w3d and map3d_height(w3d, target_x, target_z)) or 0.0

      local cx = target_x - math.sin(cam_yaw) * math.cos(cam_pitch) * cam_dist
      local cy = target_y - math.sin(cam_pitch) * cam_dist
      local cz = target_z - math.cos(cam_yaw) * math.cos(cam_pitch) * cam_dist

      camera3d(cx, cy, cz, cam_yaw, cam_pitch, 60)
      light3d(-0.4, 0.8, -0.5, 0.45)
      zclear()

      if w3d then
        map3d_draw(w3d)
      end

      -- Cell cursor: yellow square on the ground
      local tx0, tx1 = mx // 2 * 1.0, (mx // 2 + 1) * 1.0
      local tz0, tz1 = my // 2 * 1.0, (my // 2 + 1) * 1.0
      local cur_y = target_y + 0.03
      line3d(tx0, cur_y, tz0, tx1, cur_y, tz0, 0xFFE020, 2)
      line3d(tx1, cur_y, tz0, tx1, cur_y, tz1, 0xFFE020, 2)
      line3d(tx1, cur_y, tz1, tx0, cur_y, tz1, 0xFFE020, 2)
      line3d(tx0, cur_y, tz1, tx0, cur_y, tz0, 0xFFE020, 2)

      -- Objects
      if w3d then
        local objs = map3d_objects(w3d)
        local lamp_slot = 0
        for _, obj in ipairs(objs) do
          local m = model(obj.kind)
          if not m and (obj.kind == "PLAYER_START" or obj.kind == "PICO") then
            m = model("PICO")
          end
          if m then
            draw3d(m, obj.x, obj.y, obj.z, 0, 0, 0, 1.0)
          else
            if obj.kind == "LAMP" and lamp_slot < 4 then
              lamp_slot = lamp_slot + 1                      -- lamp3d slots are 1..4
              lamp3d(lamp_slot, obj.x, obj.y + 1.0, obj.z, 4, 1.0, 0xFFC888)
            end
            point3d(obj.x, obj.y + 0.5, obj.z, 0.3, OBJ_COL[1024] or 0xFFFF00)
          end
        end
      end

      if tool_mode and tool_p1 then
        local t1_x, t1_z = (tool_p1[1] // 2 + 0.5) * 1.0, (tool_p1[2] // 2 + 0.5) * 1.0
        local t1_y = (w3d and map3d_height(w3d, t1_x, t1_z)) or 0.0
        line3d(t1_x, t1_y + 0.1, t1_z, target_x, target_y + 0.1, target_z, 0xFFE020, 3)
      end

      clip()
      hint({ { "v", nil, "2D map" }, { "left", nil, "orbit" }, { "up", nil, "zoom" },
             { "shift", nil, "tilt" }, { "[ ]", nil, "tile" } })
      return
    end
    rectfill(0, VIEW_Y, W, HINT_Y - VIEW_Y, 0x000000)
    U.zone(0, VIEW_Y, W, HINT_Y - VIEW_Y, "mapview")
    clip(0, VIEW_Y, W, HINT_Y - VIEW_Y)
    local names = mlayers()
    if layer > #names then layer = 1 end
    local cw, ch = view_dims()
    if zoom == 8 then
      if only then
        map(vx0, vy0, 0, VIEW_Y, cw, ch, layer)
      else
        for l = 1, #names do map(vx0, vy0, 0, VIEW_Y, cw, ch, l) end
      end
    elseif zoom > 8 then
      local cols = S.sheet_w // 8
      local l0, l1 = only and layer or 1, only and layer or #names
      for cy = 0, ch - 1 do
        for cx = 0, cw - 1 do
          local sx, sy = cx * zoom, VIEW_Y + cy * zoom
          for l = l0, l1 do
            local v = mget(vx0 + cx, vy0 + cy, l)
            if v > 0 then
              local tx, ty = (v % cols) * 8, (v // cols) * 8
              sspr(tx, ty, 8, 8, sx, sy, zoom, zoom)
            end
          end
        end
      end
    else
      -- zoomed out (zoom = 1, 2 or 4 px per cell): draw cached horizontal colour runs
      local mw_px, mh_px = cw * zoom, ch * zoom
      rectfill(0, VIEW_Y, mw_px, mh_px, 0x080A10)
      local vx1 = vx0 + cw
      for cy = 0, ch - 1 do
        local sy = VIEW_Y + cy * zoom
        for _, r in ipairs(runs_of_row(vy0 + cy, #names)) do
          local x0, x1 = r[1], r[1] + r[2]
          if x1 > vx0 and x0 < vx1 then
            local a = max(x0, vx0) - vx0
            local b = min(x1, vx1) - vx0
            rectfill(a * zoom, sy, (b - a) * zoom, zoom, r[3])
          end
        end
      end
      rect(0, VIEW_Y, mw_px, mh_px, C.BAR)
      if zoom == 1 then
        -- 1:1 viewport frame around the cursor
        local v1x = clamp(mx - VIEW_CW // 2, 0, max(0, S.map_w - VIEW_CW))
        local v1y = clamp(my - VIEW_CH // 2, 0, max(0, S.map_h - VIEW_CH))
        rect(v1x, VIEW_Y + v1y, min(VIEW_CW, S.map_w), min(VIEW_CH, S.map_h), C.ACC)
        -- side legend on the right of the 256x256 overview
        local lx = S.map_w + 24
        if lx + 160 <= W then
          print("OVERVIEW 1:8 (256x256)", lx, VIEW_Y + 8, C.ACC)
          print(string.format("cursor (%d, %d)", mx, my), lx, VIEW_Y + 28, C.TEXT)
          print(string.format("layer %d/%d: %s%s", layer, #names, names[layer], only and " (only)" or ""),
                lx, VIEW_Y + 44, C.TEXT)
          print("+ / - : zoom in / out", lx, VIEW_Y + 68, C.DIM)
          print("m     : back to 1:1 (8 px)", lx, VIEW_Y + 84, C.DIM)
          print("click : jump & paint", lx, VIEW_Y + 100, C.DIM)
        end
      end
    end
    if show_flags and zoom >= 4 then          -- the collisions: a frame in the colour of each flag
      local FC = { 0xE04040, 0x40C040, 0x4080F0, 0x40D0E0, 0xE040E0 }
      for cy = 0, ch - 1 do
        for cx = 0, cw - 1 do
          local f = fget(mget(vx0 + cx, vy0 + cy, layer))
          if f ~= 0 then
            for k = 0, 4 do
              if f >> k & 1 == 1 then
                rect(cx * zoom + k, VIEW_Y + cy * zoom + k, max(2, zoom - 2 * k), max(2, zoom - 2 * k), FC[k + 1])
                break
              end
            end
          end
        end
      end
    end
    local n = big and 16 or 8                -- the brush, in pixels on the strip
    local cur_w = (big and 2 or 1) * zoom
    local x, y = (mx - vx0) * zoom, VIEW_Y + (my - vy0) * zoom
    rect(x - 1, y - 1, cur_w + 2, cur_w + 2, (S.frame // 10) % 2 == 0 and 0xFFFFFF or C.ACC)
    if tool_mode and tool_p1 then
      local s = big and 2 or 1
      local px1 = (tool_p1[1] - vx0) * zoom + (zoom * s) // 2
      local py1 = VIEW_Y + (tool_p1[2] - vy0) * zoom + (zoom * s) // 2
      local px2 = (mx - vx0) * zoom + (zoom * s) // 2
      local py2 = VIEW_Y + (my - vy0) * zoom + (zoom * s) // 2
      line(px1, py1, px2, py2, 0xFFE020)
      rect(px1 - 2, py1 - 2, 5, 5, 0xFFD860)
      rect(px2 - 2, py2 - 2, 5, 5, 0xFFD860)
    end
    clip()
    local ztag = zoom ~= 8 and string.format("   zoom %dpx", zoom) or ""
    strip(0, "MAP", string.format("(%d,%d) = %d   tile %d%s   layer %d/%d %s%s", mx, my, mget(mx, my, layer), tile,
                                  big and " 16x16" or "", layer, #names, names[layer], ztag),
          picking and (used_mode and ("choosing a used 16x16 tile (" .. #used .. " in map, z: full sheet): arrows, Enter")
                       or ("choosing the tile" .. (big and #used > 0 and " (z: used 16x16 tiles)" or "") .. ": arrows, then Enter")) or
          "F3 again: the sprites   tab: the tile   z: 8/16   l / L: a layer   + - / m: zoom / overview")
    rectfill(W - 20 - n, 20, n, n, 0x000000)
    spr(tile, W - 20 - n, 20, n // 8, n // 8)
    if mini and not picking and zoom >= 8 then
      local mw, mh = min(128, S.map_w), min(128, S.map_h)
      local ox, oy = W - mw - 16, VIEW_Y + 8
      U.zone(ox, oy, mw, mh, "minimap", { ox = ox, oy = oy, mw = mw, mh = mh })
      rectfill(ox - 4, oy - 4, mw + 8, mh + 8, C.PANEL)
      rectfill(ox, oy, mw, mh, 0x080A10)
      for py = 0, mh - 1 do
        local cy = py * S.map_h // mh
        for px = 0, mw - 1 do
          local cx = px * S.map_w // mw
          local v = mget(cx, cy, layer)
          local g = layer ~= 1 and mget(cx, cy, 1) or 0
          if v > 0 then pset(ox + px, oy + py, layer > 1 and 0xF0A840 or 0x486078)
          elseif g > 0 then pset(ox + px, oy + py, 0x24303C) end
        end
      end
      local rx = ox + vx0 * mw // max(1, S.map_w)
      local ry = oy + vy0 * mh // max(1, S.map_h)
      local rw = max(4, min(mw, cw * mw // max(1, S.map_w)))
      local rh = max(4, min(mh, ch * mh // max(1, S.map_h)))
      rect(rx, ry, rw, rh, C.ACC)
    end
    if picking then
      if used_mode and #used > 0 then
        local ucols = min(8, max(4, #used))
        local urows = (#used + ucols - 1) // ucols
        local cell = 20
        local vis_rows = min(10, urows)
        local sel_i = 1
        for i, t in ipairs(used) do if t == tile then sel_i = i; break end end
        local sel_r = (sel_i - 1) // ucols
        local r0 = clamp(sel_r - vis_rows // 2, 0, max(0, urows - vis_rows))
        local vw, vh = ucols * cell, vis_rows * cell
        local ox, oy = W - vw - 24, 64
        U.zone(0, VIEW_Y, W, HINT_Y - VIEW_Y, "shade")
        U.zone(ox, oy, vw, vh, "usedtiles", { ox = ox, oy = oy, ucols = ucols, cell = cell, r0 = r0 })
        rectfill(ox - 8, oy - 20, vw + 16, vh + 28, C.PANEL)
        rect(ox - 8, oy - 20, vw + 16, vh + 28, C.BAR)
        print(string.format("USED 16x16 (%d)", #used), ox - 2, oy - 16, C.ACC)
        for i = 1, #used do
          local r, c = (i - 1) // ucols - r0, (i - 1) % ucols
          if r >= 0 and r < vis_rows then
            local dx, dy = ox + c * cell + 2, oy + r * cell + 2
            checker(dx, dy, 16, 16, 4)
            spr(used[i], dx, dy, 2, 2)
            if used[i] == tile then rect(dx - 2, dy - 2, 20, 20, C.ACC) end
          end
        end
      else
        local cols = S.sheet_w // 8
        local vw, vh = min(256, S.sheet_w), min(224, S.sheet_h)
        local tx, ty = (tile % cols) * 8, (tile // cols) * 8
        local scx = clamp(tx - vw // 2, 0, S.sheet_w - vw) // 8 * 8
        local scy = clamp(ty - vh // 2, 0, S.sheet_h - vh) // 8 * 8
        local ox, oy = W - vw - 24, 64
        U.zone(0, VIEW_Y, W, HINT_Y - VIEW_Y, "shade")
        U.zone(ox, oy, vw, vh, "tiles", { scx = scx, scy = scy, ox = ox, oy = oy })
        rectfill(ox - 8, oy - 8, vw + 16, vh + 16, C.PANEL)
        checker(ox, oy, vw, vh, 4)
        sspr(scx, scy, vw, vh, ox, oy)
        rect(ox + tx - scx - 1, oy + ty - scy - 1, n + 2, n + 2, C.ACC)
      end
    end
    hint({ { "space", "A", "place" }, { "backspace", nil, "clear" }, { "x", "B", "pick" }, { "f", nil, "fill" },
           { "tab", "Y", "tiles" }, { "t", nil, "terrain" }, { "l", nil, "layer" }, { "c", nil, "flags" },
           { "u", nil, "undo" } })
  end

  local function status()
    refresh_stats()
    local names = mlayers()
    local cur_name = names[layer] or ""
    local val = mget(mx, my, layer)
    local obj_info = ""
    if cur_name == "objects" and val >= 1024 then
      local OBJ_NAMES = {
        [1024] = "PLAYER_START",
        [1026] = "APPLE",
        [1028] = "CRATE",
        [1030] = "GOAL",
        [1032] = "LAMP",
        [1034] = "CRATE_BOUNCE",
      }
      local kname = OBJ_NAMES[val] or ("OBJ_" .. val)
      obj_info = "  [" .. kname .. "]"
    end
    local tool_info = ""
    if tool_mode then
      if tool_p1 then
        tool_info = string.format("  [%s: (%d,%d)->(%d,%d)]", tool_mode:upper(), tool_p1[1], tool_p1[2], mx, my)
      else
        tool_info = string.format("  [%s: Enter start]", tool_mode:upper())
      end
    end
    local counts_info = string.format("  apples:%d crates:%d lamps:%d ~%dtri",
                                      obj_counts.apples, obj_counts.crates, obj_counts.lamps, obj_counts.tri)
    if view3d then
      return string.format("[3D PREVIEW] (%d,%d) tile %d%s  orbit: yaw %.1f dist %.1f  (v: 2D)%s%s%s",
                           mx, my, tile, big and " 16x16" or "", cam_yaw, cam_dist, obj_info, tool_info, counts_info)
    end
    if terrain and not picking then
      return string.format("TERRAIN base %d  (%d,%d) = %d%s  layer %d/%d %s  (t: off)", terrain, mx, my, val,
                           big and " 16x16" or "", layer, #names, cur_name)
    end
    return picking and (used_mode and ("choosing a used 16x16 tile (" .. #used .. " in map)") or "choosing a tile") or
           string.format("(%d,%d) = %d  tile %d%s  layer %d/%d %s%s%s%s%s", mx, my,
                         val, tile, big and " 16x16" or "", layer,
                         #names, cur_name, zoom ~= 8 and ("  zoom " .. zoom .. "px") or "", obj_info, tool_info, counts_info)
  end

  -- the mouse: the left button places the tile (held: it goes on), the
  -- middle one picks it; the right or the middle button dragged moves the
  -- view, the wheel scrolls it; the tiles (Tab): a click takes one
  local pan = { 0, 0 }
  local function mouse()
    local z = U.at()
    if picking then
      if U.pressed(0) then
        if z and z.kind == "usedtiles" then
          local t = z.a
          local c = clamp((U.x - t.ox) // t.cell, 0, t.ucols - 1)
          local r = t.r0 + (U.y - t.oy) // t.cell
          local idx = r * t.ucols + c + 1
          if used[idx] then tile = used[idx] end
        elseif z and z.kind == "tiles" then
          local t = z.a
          local cols = S.sheet_w // 8
          tile = clamp(((t.scy + U.y - t.oy) // 8) * cols + (t.scx + U.x - t.ox) // 8, 0, cols * (S.sheet_h // 8) - 1)
          even()
        end
        picking = false
      end
      return
    end
    local cw, ch = view_dims()
    if z and z.kind == "minimap" and U.down(0) then
      local t = z.a
      mx = clamp((U.x - t.ox) * S.map_w // t.mw, 0, S.map_w - 1)
      my = clamp((U.y - t.oy) * S.map_h // t.mh, 0, S.map_h - 1)
      even()
      vx0 = clamp(mx - cw // 2, 0, max(0, S.map_w - cw))
      vy0 = clamp(my - ch // 2, 0, max(0, S.map_h - ch))
      return
    end
    local d = U.drag
    if d and (d.b == 1 or d.b == 2) and d.y0 >= VIEW_Y and d.y0 < HINT_Y then
      pan[1], pan[2] = pan[1] - d.dx, pan[2] - d.dy
      local sx, sy = pan[1] // zoom, pan[2] // zoom
      if sx ~= 0 or sy ~= 0 then
        vx0 = clamp(vx0 + sx, 0, max(0, S.map_w - cw))
        vy0 = clamp(vy0 + sy, 0, max(0, S.map_h - ch))
        pan[1], pan[2] = pan[1] - sx * zoom, pan[2] - sy * zoom
      end
      return
    end
    if not z or z.kind ~= "mapview" then return end
    if U.wheel ~= 0 then vy0 = clamp(vy0 - U.wheel * 4, 0, max(0, S.map_h - ch)) end
    if U.down(0) or U.pressed(2) then
      if U.x >= cw * zoom or (U.y - VIEW_Y) >= ch * zoom then return end
      mx = clamp(vx0 + U.x // zoom, 0, S.map_w - 1)
      my = clamp(vy0 + (U.y - VIEW_Y) // zoom, 0, S.map_h - 1)
      even()
      act(U.pressed(2) and "pick" or "paint")
    end
  end

  local function menu_items()
    return { { "3D preview", "v" }, "-",
             { "Path tool", "Shift+P" }, { "Path width", "Shift+W" },
             { "Apples row", "Shift+A" }, { "Place crate", "Shift+C" }, { "Place lamp", "Shift+O" }, "-",
             { "Pick the tile", "x" }, { "Clear", "\b" }, { "Fill", "f" }, { "Terrain brush", "t" }, { "Tiles...", "\t" },
             { "8x8 / 16x16", "z" }, "-", { "Zoom in", "+" }, { "Zoom out", "-" }, { "Overview (256x256)", "m" }, "-",
             { "Next layer", "l" }, { "New layer", "L" }, { "Rename layer", "r" },
             { "Template: 3D map", "T" }, { "This layer only", "o" },
             { "Flags", "c" }, "-", { "Undo", "u" } }
  end

  P.map = { act = act, draw = draw, status = status, end_stroke = end_stroke, mouse = mouse, menu_items = menu_items,
            new_layer = function() next_layer(true) end,
            stroking = function() return m_stroke ~= nil end,
            state = function() return { mx = mx, my = my, big = big, zoom = zoom } end,
            restore = function(t)
              if t then mx, my, big, zoom = t.mx or 0, t.my or 0, t.big or false, t.zoom or 8; act("none") end
            end,
            reset = function()
              mx, my, vx0, vy0, m_undo, m_stroke, picking = 0, 0, 0, 0, {}, nil, false
              layer, only, show_flags, big, mini, zoom = 1, false, false, false, false, 8
              used, used_mode, used_rev, col_rev = {}, false, -1, -1
              tool_mode, tool_p1, path_width, obj_rev, terrain = nil, nil, 1, -1, nil
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
    B.draw_list("MODELS " .. #names, names, sel, 0, 32, 9, PANEL_W, nil, function(i, again)
      if i ~= sel then pick(i) end
      if again then open_in(TOOLS[3]) end          -- a double click: bm Studio on it
    end)
    local cn = { "(rest pose)" }
    for _, c in ipairs(cl) do cn[#cn + 1] = c.name .. (c.loop and "" or " (once)") end
    if #cl > 0 then
      B.draw_list("ANIMATIONS " .. #cl, cn, clip_i + 1, 0, 208, 5, PANEL_W, nil, function(i)
        if i - 1 ~= clip_i then clip_i, t = i - 1, 0 end
      end)
    elseif m then print("no skeleton: 4 on the", 8, 208, C.DIM); print("project page: bm Animator", 8, 224, C.DIM) end
    U.zone(PANEL_W, VY, W - PANEL_W, HINT_Y - VY, "view3d")
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

  -- the mouse: right drag turns (and stops the spinning), middle moves, the
  -- wheel zooms; a click in the view plays or stops
  local function mouse()
    if B.mouse_cam(cam, PANEL_W, VY, W - PANEL_W, HINT_Y - VY, { lo = 0.3, hi = 400 }) and U.drag then spin = false end
    local z = U.click(0)
    if z and z.kind == "view3d" then play = not play end
  end

  local function menu_items()
    return { { play and "Stop" or "Play", " " }, { "Spin", "r" }, { "The code to draw it", "i" }, "-",
             { "Open in bm Studio", "\n" }, { "Open in bm Animator", "a" }, { "Open in bm Mesh", "m" } }
  end

  P.d3 = { key = key, pad = pad, update = update, draw = draw, status = status, refresh = refresh, mouse = mouse,
           menu_items = menu_items,
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
    local w, h = S.proj.res:match("(%d+)x(%d+)")
    if w ~= h then S.proj.res = "360x360"; touched() end
    say("target .b16 (RGB30): no size cap; Ctrl+B builds the .b16 edition", C.ACC, 400)
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
  local RES_NEXT = { ["640x360"] = "480x270", ["480x270"] = "320x180", ["320x180"] = "256x256", ["256x256"] = "360x360", ["360x360"] = "640x360" }

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

  -- the mouse: a click chooses a row, a click on the chosen one (or a
  -- double click) does it
  local function mouse()
    if U.wheel ~= 0 then U.press(U.wheel > 0 and "up" or "down") end     -- the wheel: the keys
    local z = U.click(0)
    if z and z.kind == "hub" then
      if z.a == row or U.double then row = z.a; U.press("\n") else row = z.a end
    end
  end

  local function draw_panel()
    rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
    for i, h in ipairs(HUB) do
      local y = 16 + i * 16
      if h.head then print(h.head, 16, y, C.DIM)
      else
        U.zone(0, y, PANEL_W, 16, "hub", i)
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
          "target ." .. S.target .. (S.target == "b16" and "   (RGB30, no cap: Esc > Export .b16, docs/B16.md)" or
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
        print(kib(I.file_kb) .. " of 100M (Market)", INFO_X + 192, 176, frac > 1 and C.ERR or C.TEXT)
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

  P.project = { key = key, pad = pad, draw = draw, status = status, mouse = mouse,
                menu_items = function()
                  local list = {}
                  for _, h in ipairs(HUB) do
                    if h.key and not h.head then list[#list + 1] = { label(h), h.key } end
                  end
                  list[#list + 1] = "-"
                  list[#list + 1] = { S.hubview == "devkit" and "The project" or "The dev kit", "f1" }
                  return list
                end }
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
    if #rows == 0 then say("no .bm or .bme files on the SD card", C.ERR); return end
    D.choose("open a cartridge", rows)
  end
  P.menu_open = open_other

  -- R11: a Tiled map (.tmj/.json in /docs) into the map's layers
  local function import_tiled()
    local files, why = doc_list()
    if not files then say("Tiled import: " .. tostring(why), C.ERR); return end
    local rows = {}
    for _, f in ipairs(files) do
      local n = f.name:lower()
      if n:match("%.tmj$") or n:match("%.json$") then
        rows[#rows + 1] = { f.name, function()
          local text, why2 = doc_read(f.name)
          local res, err = nil, why2
          if text then res, err = B.tiled_import(text, S.sheet_w // 8) end
          if not res then say("Tiled: " .. tostring(err), C.ERR); return end
          local function go(mode)
            local w, h, n2 = B.tiled_apply(res, mode)
            if not w then say("Tiled: " .. tostring(h), C.ERR); return end
            S.map_w, S.map_h = w, h
            P.map.reset()
            touched()
            S.d2view = "map"; show("d2")
            say(string.format("Tiled: %d layers in, %d in the map, %dx%d cells, tiles %dx%d%s", #res.layers, n2, w, h, res.tile, res.tile,
                #res.ignored > 0 and (", ignored: " .. #res.ignored) or ""), C.ACC)
          end
          D.choose("Tiled " .. f.name .. ": " .. #res.layers .. " layers", {
            { "add the layers to the map", function() go("add") end },
            { "replace the map", function() go("replace") end } })
        end }
      end
    end
    if #rows == 0 then say("no .tmj or .json Tiled file in /docs on the SD card", C.ERR); return end
    D.choose("import a Tiled map (JSON)", rows)
  end

  local function build()
    items = {
      { "Continue", function() S.page = S.last end },
      { "New project...   (Ctrl+N)", choose_template },
      { "Open...   (Ctrl+O)", open_other },
      { "Save   (Ctrl+S)", function() if S.proj.save then save_project() else save_as() end end },
      { "Save as...   (Ctrl+Shift+S)", function() save_as() end },
      { "Build the game ." .. S.target .. "   (Ctrl+B)", function() build_project() end },
      { "Export .b16   (Ctrl+Shift+B)", function() build_project(true) end },
      { "Try the game (F5)", run_project },
      { "New map layer (" .. #mlayers() .. "/8)", function() S.d2view = "map"; show("d2"); P.map.new_layer() end },
      { "Import a Tiled map (.tmj)...", import_tiled },
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

  -- the mouse: a click on an entry does it, the wheel moves
  local function mouse()
    build()
    if U.wheel ~= 0 then msel = clamp(msel - U.wheel, 1, #items) end
    local z = U.click(0)
    if z and z.kind == "menuitem" then msel = z.a; U.press("\n") end
  end

  local function draw()
    build()
    rectfill(0, 16, W, HINT_Y - 16, C.BG)
    print("bm SDK", 32, 32, C.ACC)
    print((S.proj.save or "(not saved yet)") .. (S.dirty and "  *modified*" or ""), 96, 32, C.DIM)
    for i, it in ipairs(items) do
      local y = 64 + (i - 1) * 16
      U.zone(24, y, 272, 16, "menuitem", i)
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

  P.menu = { key = key, pad = pad, draw = draw, mouse = mouse, status = function() return "up/down choose, Enter select" end }
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
  elseif k == "^b" then build_project()
  elseif k == "^B" then build_project(true)
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
    local a = ({ ["+"] = "zoom_in", ["="] = "zoom_in", ["-"] = "zoom_out", l = "layer", L = "newlayer", o = "only",
                 c = "flags", m = "mini", t = "terrain", home = "pgleft", ["end"] = "pgright",
                 v = "view3d", r = "rename_layer", R = "rename_layer", T = "map3d_template",
                 P = "tool_path", W = "cycle_width", A = "tool_apples", C = "tool_crates", O = "tool_lamps",
                 S = "tool_start", G = "tool_goal" })[k] or SPRITE_KEYS[k]
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
    { "ctrl b", "build the game (.bm or .b16) of the project" },
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
    { "pgup / pgdn", "move a page up / down" },
    { "home / end", "move a page left / right" },
    { "space / backspace", "place the tile / clear the cell" },
    { "shift p / shift w", "path tool (start, end: Enter) / width (1-5)" },
    { "shift a", "apples line tool (start, end: Enter)" },
    { "shift c / shift o", "place crate (1028) / place lamp (1032)" },
    { "shift s / shift g", "place the start (1024) / the goal (1030) of the level" },
    { "v", "toggle 2D / 3D preview" },
    { "x", "pick the tile" },
    { "f", "fill" },
    { "t", "terrain brush on / off: the base is the tile; 16 tiles (4 x 4 of the sheet) tile the borders" },
    { ", / .", "tile before / after" },
    { "tab", "choose the tile (z: used 16x16 tiles / full sheet)" },
    { "z", "8x8 / 16x16: a tile of 2 x 2 cells" },
    { "+ / - / m", "zoom in / out, or 256x256 overview in one screen" },
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
  U.wants()                             -- the pointer, when there is a mouse
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

-- the mouse, each frame: the chips and the tabs press their keys, the
-- dialogs and the lists choose, the page does its own, the right button
-- opens the context menu (the page's keys, then save, try, menu)
local function page_of()
  if S.page == "d2" then return S.d2view == "sprite" and P.sprite or P.map end
  return P[S.page]
end

local function mouse_frame()
  if not U.on then return end
  if U.keys() then return end
  if D.open() then D.mouse(); return end
  local z = U.at()
  if z and (z.kind == "row" or z.kind == "list") and U.wheel ~= 0 then
    local L = z.b
    L.pick(clamp(L.sel - U.wheel, 1, max(1, L.n)), false)
  end
  local c = U.click(0)
  if c and c.kind == "row" then c.b.pick(c.a, U.double); return end
  local pg = page_of()
  if pg and pg.mouse then pg.mouse() end
  if U.clicked(1) then
    local list = pg and pg.menu_items and pg.menu_items() or {}
    if #list > 0 then list[#list + 1] = "-" end
    for _, it in ipairs({ { "Save", "^s" }, { "Build the game .bm", "^b" }, { "Try the game", "f5" },
                          { "Assistant", "f6" }, { "Menu", "esc" } }) do list[#list + 1] = it end
    U.menu(list)
  end
end

function _update()
  S.frame = S.frame + 1
  if S.msg_t > 0 then S.msg_t = S.msg_t - 1 end
  if confirm.t > 0 then confirm.t = confirm.t - 1 end
  read_pad()
  U.update()
  local hp = S.page == "d2" and S.d2view or S.page
  if help_page ~= hp then help_page = hp; sdk_keyhelp(hp) end
  if assist and assist.update() then return end   -- the assistant has the keys
  if U.menu_update() then return end              -- the context menu has the input
  mouse_frame()
  P.sprite.lift()
  if P.map.stroking() and not btn(4) and not U.down(0) then P.map.end_stroke() end

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
  U.begin()
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
  local x = U.bar_title("bm SDK", C.ACC)            -- the name first, at the left
  for _, t in ipairs(tabs) do
    local lx = snap(x + prompt(t[2]) + 3)            -- the label on its column
    local w = lx + #t[3] * 8 - x
    if t[1] == S.page then rectfill(x - 4, 0, w + 8, 16, C.SEL) end
    U.key_zone(x - 4, 0, w + 8, 16, t[2])            -- a click: the page
    prompt(t[2], x, 0)
    print(t[3], lx, 0, t[1] == S.page and 0xFFFFFF or C.DIM)
    x = x + w + 20
  end
  U.bar_file(S.proj.save or "untitled", S.dirty, x, W, C.ACC, C.DIM)

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
  U.menu_draw()                          -- the context menu over everything
end
