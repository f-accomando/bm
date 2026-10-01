-- bm assistant (M30): the development assistant on its own, in the Dev tab.
-- It shows what the tools will get from it: the code it inserts and the
-- sprites it draws, plus how fast it answers. The panel is the library
-- every tool uses (require "assist"); here F6 opens it, as in the tools.

local assist = require "assist"

local W, H = SCREEN_W, SCREEN_H
local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC = 0xE0E4F0, 0x8088A0, 0xFFC050

local code = {}                          -- the lines inserted so far
local sprites = {}                       -- the sprites drawn, newest last
local status = "F6: ask a question   F7: draw a sprite   F8: speed test"
local bench                              -- the speed test, once run

local function on_insert(text)
  if #code > 0 then code[#code + 1] = "" end
  for l in (text .. "\n"):gmatch("(.-)\n") do code[#code + 1] = l end
  while #code > 300 do table.remove(code, 1) end
  status = "code inserted (" .. select(2, text:gsub("\n", "")) + 1 .. " lines)"
  log("assistant: inserted " .. (text:match("^[^\n]*") or ""))
end

-- a sprite goes into the sheet, as an editor would put it in a cell
local function on_sprite(s)
  local slot = #sprites % 16
  local sx, sy = (slot % 8) * 32, (slot // 8) * 32
  for y = 0, s.h - 1 do
    for x = 0, s.w - 1 do
      local c = s.px[y * s.w + x + 1]
      if c >= 0 then sset(sx + x, sy + y, c) else sset(sx + x, sy + y) end
    end
  end
  sprites[#sprites + 1] = s
  status = s.name .. " " .. s.w .. "x" .. s.h .. " in the sheet at " .. sx .. "," .. sy
  log("assistant: sprite " .. s.gen .. " " .. s.w .. "x" .. s.h)
end

local function open(mode)
  assist.open{ mode = mode, on_insert = on_insert, on_sprite = on_sprite, size = 16 }
end

-- the speed test: questions answered by the network on this CPU
local QUESTIONS = {
  "come muovo il personaggio con le frecce",
  "how do I play a sound",
  "salvare il record sulla SD",
  "collisione tra due rettangoli",
  "far saltare il giocatore con la gravita'",
}

local function speed_test()
  local total, n = 0, 0
  local t0 = time()
  for r = 1, 20 do
    for _, q in ipairs(QUESTIONS) do
      local _, us = ai.ask(q)
      total, n = total + us, n + 1
    end
  end
  local wall = (time() - t0) * 1000
  bench = { avg = total / n, n = n, wall = wall, answers = {} }
  for i, q in ipairs(QUESTIONS) do
    local hits = ai.ask(q, { n = 1 })
    bench.answers[i] = { q = q, a = hits[1] and hits[1].title or "-" }
  end
  local t1 = time()
  local s = ai.sprite("slime", { size = 16 })
  bench.sprite = (time() - t1) * 1000
  status = string.format("%d questions: %.2f ms each", n, bench.avg / 1000)
  log(string.format("assistant: speed %d questions, %.3f ms each", n, bench.avg / 1000))
end

function _init()
  log("assistant: ready, " .. #ai.list() .. " entries, " .. #ai.recipes() .. " sprite recipes")
  open("any")
end

function _update()
  if assist.update() then return end
  local k = keyp()
  while k do
    if k == "f6" or k == "^a" then open("code")
    elseif k == "f7" then open("sprite")
    elseif k == "f8" then speed_test()
    elseif k == "esc" then quit() end
    k = keyp()
  end
  if btnp(4) then open("any") end
end

local function draw_sprite(s, x, y, z)
  for j = 0, s.h - 1 do
    for i = 0, s.w - 1 do
      local c = s.px[j * s.w + i + 1]
      if c >= 0 then rectfill(x + i * z, y + j * z, z, z, c) end
    end
  end
end

function _draw()
  cls(C_BG)
  rectfill(0, 0, W, 16, C_BAR)
  print("bm assistant", 8, 0, C_ACC)
  local keys = "F6 ask  F7 sprite  F8 speed  Esc exit"
  print(keys, W - 8 - #keys * 8, 0, C_DIM)
  -- text on the 8x16 cells (x a multiple of 8, y of 16)
  -- the code the panel inserted
  local cw = W * 3 // 5 // 8 * 8
  local bottom = (H - 24) // 16 * 16 - 16          -- the status row is the last one
  rectfill(8, 24, cw - 16, bottom - 24, C_PANEL)
  print("code", 16, 32, C_DIM)
  local rows = (bottom - 48) // 16
  local first = math.max(1, #code - rows + 1)
  for i = first, #code do
    print(code[i]:sub(1, (cw - 32) // 8), 16, 48 + (i - first) * 16, C_TEXT)
  end
  if #code == 0 then
    print("nothing yet: F6 (or A on the pad), ask,", 16, 64, C_DIM)
    print("then Enter (A) on an answer", 16, 80, C_DIM)
  end
  -- the sprites, and the speed test
  local sx = cw
  rectfill(sx, 24, W - sx - 8, bottom - 24, C_PANEL)
  print("sprites", sx + 8, 32, C_DIM)
  local first_s = math.max(1, #sprites - 7)
  for i = first_s, #sprites do
    local k = i - first_s
    local s = sprites[i]
    local z = s.w >= 32 and 1 or s.w >= 16 and 2 or 4
    draw_sprite(s, sx + 8 + (k % 4) * 56, 52 + (k // 4) * 52, z)
  end
  if bench then
    local by = 160
    print(string.format("%d questions", bench.n), sx + 8, by, C_TEXT)
    print(string.format("%.2f ms each", bench.avg / 1000), sx + 8, by + 16, C_ACC)
    print(string.format("sprite %.1f ms", bench.sprite), sx + 8, by + 32, C_TEXT)
    for i, a in ipairs(bench.answers) do
      if by + 48 + i * 16 <= bottom - 16 then
        print(a.a:sub(1, (W - sx - 24) // 8), sx + 8, by + 32 + i * 16, C_DIM)
      end
    end
  end
  print(status:sub(1, W // 8 - 16), 8, bottom, C_TEXT)
  print(string.format("Lua %d KiB", stat(0)), W - 112, bottom, C_DIM)
  assist.draw()
end
