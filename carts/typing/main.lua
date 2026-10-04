-- Pad Typing: practice and test of writing with the controller, in
-- Italian, English or Lua (require "padtype", docs/PADTYPE.md).
--
-- The menu chooses the language, how one starts (compose or the on-screen
-- keyboard: Share switches while writing), how long a press waits for its
-- double, the text (eight a language, or free writing) and the hints. A
-- text is copied: what is right turns white, a mistake red; the next
-- press is shown under it (the coach of padtype). At the end: characters
-- a minute, presses a character, how many times circle erased, against
-- the best (saved).
-- Start pauses.

local pt = require "padtype"
local B = pt.BITS

local W, H = 640, 360
local C = {
  bg = 0x0B0E16, panel = 0x141A26, edge = 0x2E3A52, text = 0xFFFFFF, dim = 0x7C8AA6,
  todo = 0x5C6680, bad = 0xFF5A5A, badbg = 0x4A1414, acc = 0xFFC050, good = 0x50E0B0,
  sel = 0x22304A,
}

local LANGS = { { id = "it", name = "Italiano" }, { id = "en", name = "English" }, { id = "lua", name = "Lua" } }
local MODES = { "compose", "keyboard" }
local DELAYS = { 150, 200, 250, 300, 350, 400, 500, 600 }
local NTEXTS = 8

local cfg = { lang = 1, mode = 1, delay = 3, text = 1, hints = true }
local best = {}                 -- best[lang] = { cpm = , ppc = }

local state = "menu"            -- menu, practice, free, pause, result
local sel = 1                   -- the menu's row
local psel = 1                  -- the pause menu's row
local host, target
local run                       -- the text being copied: its start, the erasures
local result
local words_ready = false

------------------------------------------------------------------ helpers

local function lang_id() return LANGS[cfg.lang].id end

local function save_all()
  save({ cfg = cfg, best = best })
end

local function fmt_time(s)
  s = math.floor(s)
  return string.format("%d:%02d", s // 60, s % 60)
end

-- the button names of prompt(), from the bits of pad()
local CHIP = { [B.UP] = "UP", [B.DOWN] = "DOWN", [B.LEFT] = "LEFT", [B.RIGHT] = "RIGHT",
               [B.A] = "A", [B.B] = "B", [B.X] = "X", [B.Y] = "Y", [B.L1] = "L1", [B.R1] = "R1",
               [B.L2] = "L2", [B.R2] = "R2", [B.L3] = "L3", [B.R3] = "R3", [B.SELECT] = "SELECT" }

-- lines of at most cols characters, broken after a space or at a new
-- line: { first index, last index } of the text
local function layout(text, cols)
  local lines = {}
  local start = 1
  while true do
    local nl = text:find("\n", start, true)
    local stop = nl and nl - 1 or #text
    local a = start
    repeat
      local b = stop
      if b - a + 1 > cols then
        b = a + cols - 1
        local sp = text:sub(a, b):match(".*() ")
        if sp and sp > 1 then b = a + sp - 1 end
      end
      lines[#lines + 1] = { a, b }
      a = b + 1
    until a > stop
    if not nl then break end
    start = nl + 1
  end
  return lines
end

-- where the character k of a layout is drawn
local function place(lines, k, x0, y0, lh)
  for i, ln in ipairs(lines) do
    local nxt = lines[i + 1]
    if k <= ln[2] + 1 and (not nxt or k < nxt[1]) then
      return x0 + (k - ln[1]) * 8, y0 + (i - 1) * lh
    end
  end
  local ln = lines[#lines]
  return x0 + (ln[2] - ln[1] + 1) * 8, y0 + (#lines - 1) * lh
end

------------------------------------------------------------------ texts

local function start_text()
  pt.clear()
  pt.on(MODES[cfg.mode])
  pt.set({ delay = DELAYS[cfg.delay] / 1000, fallback = "keyboard" })
  host = pt.text_host(lang_id())
  host.now = time
  target = cfg.text <= NTEXTS and pt.TEXTS[lang_id()][cfg.text] or nil
  run = { t0 = nil, erased = 0 }
  pt.reset_count()
  state = target and "practice" or "free"
  log("typing: " .. LANGS[cfg.lang].name .. ", " .. (target and ("text " .. cfg.text) or "free") ..
      ", " .. MODES[cfg.mode] .. ", " .. DELAYS[cfg.delay] .. " ms")
end

-- how much of the target is written right (and the text, as compared)
local function progress()
  local typed = host.text()
  if lang_id() == "lua" then typed = pt.align(typed, target) end
  local n = 0
  while n < #typed and typed:byte(n + 1) == target:byte(n + 1) do n = n + 1 end
  return n, typed
end

local function finish()
  local secs = math.max(0.5, time() - (run.t0 or time()))
  local chars = #target
  local presses = pt.presses()
  local id = lang_id()
  result = {
    secs = secs, cpm = chars * 60 / secs, presses = presses, ppc = presses / chars,
    erased = run.erased, chars = chars,
  }
  local b = best[id]
  result.best_cpm = not b or result.cpm > b.cpm
  result.best_ppc = not b or result.ppc < b.ppc
  best[id] = { cpm = math.max(result.cpm, b and b.cpm or 0), ppc = math.min(result.ppc, b and b.ppc or 99) }
  save_all()
  log(string.format("typing: done in %.1f s, %d cpm, %d presses (%.2f a character), %d erased",
                    secs, math.floor(result.cpm + 0.5), presses, result.ppc, run.erased))
  state = "result"
end

------------------------------------------------------------------ menu

local MENU = { "lang", "mode", "delay", "text", "hints", "start" }

local function menu_value(item)
  if item == "lang" then return LANGS[cfg.lang].name
  elseif item == "mode" then return MODES[cfg.mode]
  elseif item == "delay" then return DELAYS[cfg.delay] .. " ms"
  elseif item == "text" then return cfg.text <= NTEXTS and ("text " .. cfg.text .. " of " .. NTEXTS) or "free writing"
  elseif item == "hints" then return cfg.hints and "on" or "off" end
  return ""
end

local function menu_change(item, d)
  if item == "lang" then cfg.lang = (cfg.lang - 1 + d) % #LANGS + 1; words_ready = false
  elseif item == "mode" then cfg.mode = (cfg.mode - 1 + d) % #MODES + 1
  elseif item == "delay" then cfg.delay = math.max(1, math.min(#DELAYS, cfg.delay + d))
  elseif item == "text" then cfg.text = (cfg.text - 1 + d) % (NTEXTS + 1) + 1
  elseif item == "hints" then cfg.hints = not cfg.hints end
end

local function update_menu()
  if not words_ready then words_ready = pt.preload({ lang_id() }) end
  if btnp("up") then sel = (sel - 2) % #MENU + 1 end
  if btnp("down") then sel = sel % #MENU + 1 end
  local item = MENU[sel]
  if btnp("left") then menu_change(item, -1) end
  if btnp("right") then menu_change(item, 1) end
  if btnp("start") or (btnp("ok") and item == "start") then
    save_all()
    start_text()
  elseif btnp("ok") then
    menu_change(item, 1)
  end
end

------------------------------------------------------------------ frame

function _init()
  local s = saved()
  if s then
    for k, v in pairs(s.cfg or {}) do cfg[k] = v end
    best = s.best or {}
  end
  cfg.lang = math.max(1, math.min(#LANGS, cfg.lang))
  cfg.mode = math.max(1, math.min(#MODES, cfg.mode))
  cfg.delay = math.max(1, math.min(#DELAYS, cfg.delay))
  cfg.text = math.max(1, math.min(NTEXTS + 1, cfg.text))
  if keyhelp then
    keyhelp({ { "SELECT", "compose / keyboard" }, { "START", "pause" }, { "L1", "back" }, { "R1", "forward" },
              { "B", "erase" }, { "A", "space (twice: a full stop)" }, { "X", "next syllable" },
              { "Y", "previous syllable" }, { "R2", "+ A X Y: the words" }, { "R3", "a symbol" } }, "Pad Typing")
  end
  log("typing: ready")
end

local PAUSE = { "Continue", "Start again", "Next text", "Menu" }

function _update()
  if state == "menu" then
    update_menu()
  elseif state == "practice" or state == "free" then
    if btnp("start") then
      pt.commit()
      state, psel, run.paused = "pause", 1, state
      return
    end
    local before = pt.presses()
    local was = run.pad or 0
    run.pad = pad()
    pt.update(host, run.pad)
    if state == "practice" then
      if not run.t0 and pt.presses() > before then run.t0 = time() end
      if run.pad & ~was & B.B ~= 0 then run.erased = run.erased + 1 end
      local typed = host.text()
      if not pt.pending() and typed:gsub("%s+$", "") == target:gsub("%s+$", "") then finish() end
    end
  elseif state == "pause" then
    if btnp("up") then psel = (psel - 2) % #PAUSE + 1 end
    if btnp("down") then psel = psel % #PAUSE + 1 end
    if btnp("start") or btnp("back") then state = run.paused end
    if btnp("ok") then
      local p = PAUSE[psel]
      if p == "Continue" then state = run.paused
      elseif p == "Start again" then start_text()
      elseif p == "Next text" then cfg.text = cfg.text % NTEXTS + 1; start_text()
      else pt.on(nil); state = "menu" end
    end
  elseif state == "result" then
    if btnp("ok") then cfg.text = cfg.text % NTEXTS + 1; start_text()
    elseif btnp("y") then start_text()
    elseif btnp("back") or btnp("start") then pt.on(nil); state = "menu" end
  end
end

------------------------------------------------------------------ drawing

local function panel(x, y, w, h)
  rectfill(x, y, w, h, C.panel)
  rect(x, y, w, h, C.edge)
end

local function header(sub)
  rectfill(0, 0, W, 22, C.panel)
  line(0, 22, W, 22, C.edge)
  font("8x16")
  local x = print("Pad Typing", 8, 3, C.acc)
  print(sub, x + 12, 3, C.dim)
end

-- the next press, as the pad's buttons
local function draw_hint(x, y)
  if not cfg.hints then return end
  local s = pt.coach(host, target)
  if not s or s.wait then return end
  font("8x16")
  x = print("next:", x, y, C.dim) + 6
  if s.both then
    x = prompt("L1", x, y) + 2
    x = print("+", x, y, C.dim) + 2
    x = prompt("R1", x, y) + 4
    print("hold: " .. (s.label or ""), x, y, C.dim)
    return
  end
  if s.hold and s.hold ~= 0 then
    if s.hold & B.L2 ~= 0 then x = prompt("L2", x, y) + 2 end
    if s.hold & B.R2 ~= 0 then x = prompt("R2", x, y) + 2 end
    x = print("+", x, y, C.dim) + 2
  end
  x = prompt(CHIP[s.tap] or "?", x, y) + 2
  if s.double then x = print("x2", x, y, C.acc) + 2 end
  if s.label then
    local l = s.label == " " and "space" or s.label:gsub("\n", "")
    print(l, x + 6, y, C.dim)
  end
end

-- the text being written over the one to copy
local function draw_target(x0, y0, cols, lh, maxl)
  local n, typed = progress()
  local lines = layout(target, cols)
  local flash, open = pt.flash(), pt.open_len()
  local cur = #typed + 1
  font("8x16")
  for i, ln in ipairs(lines) do
    if i > maxl then break end
    local y = y0 + (i - 1) * lh
    for k = ln[1], ln[2] do
      local x = x0 + (k - ln[1]) * 8
      local ch, col = target:sub(k, k), C.todo
      if k <= n then
        col = (k > n - flash and k <= #typed) and pt.C_PRED or C.text
      elseif k <= #typed then
        ch = typed:sub(k, k)
        if ch == " " or ch == "\n" then ch = "_" end
        rectfill(x, y, 8, 16, C.badbg)
        col = C.bad
      end
      if ch ~= " " then print(ch, x, y, col) end
    end
  end
  -- around the cursor: the press waiting, the syllable turning, the ghost
  local cx, cy = place(lines, cur, x0, y0, lh)
  if open > 0 then
    local ox = place(lines, cur - open, x0, y0, lh)
    line(ox, cy + 16, cx - 1, cy + 16, pt.C_OPEN)
  end
  local p = pt.pending()
  local gx = cx
  if p then
    rectfill(gx, cy, 8, 16, C.bg)
    print(p == " " and "_" or p, gx, cy, pt.C_PEND)
    gx = gx + 8
  end
  local g = pt.ghost()
  if g ~= "" then
    rectfill(gx, cy, #g * 8, 16, C.bg)
    print(g, gx, cy, pt.C_GHOST)
  end
  if (time() * 2) % 2 < 1.4 then rectfill(cx - 1, cy, 2, 16, C.acc) end
end

-- free writing: the text and its cursor
local function draw_free(x0, y0, cols, lh, maxl)
  local text = host.text()
  local lines = layout(text, cols)
  local cur = host.cursor() + 1
  local _, cy = place(lines, cur, x0, y0, lh)
  local first = math.max(1, (cy - y0) // lh + 1 - maxl + 1)
  font("8x16")
  for i = first, math.min(#lines, first + maxl - 1) do
    local ln = lines[i]
    print(text:sub(ln[1], ln[2]), x0, y0 + (i - first) * lh, C.text)
  end
  local cx
  cx, cy = place(lines, cur, x0, y0, lh)
  cy = cy - (first - 1) * lh
  local flash, open = pt.flash(), pt.open_len()
  if flash > 0 and host.cursor() >= flash then
    local fx = place(lines, cur - flash, x0, y0, lh)
    if fx < cx then print(text:sub(cur - flash, cur - 1), fx, cy, pt.C_PRED) end
  end
  if open > 0 then
    local ox = place(lines, cur - open, x0, y0, lh)
    if ox < cx then line(ox, cy + 16, cx - 1, cy + 16, pt.C_OPEN) end
  end
  local gx = cx
  local p = pt.pending()
  if p then print(p == " " and "_" or p, gx, cy, pt.C_PEND); gx = gx + 8 end
  local g = pt.ghost()
  if g ~= "" then print(g, gx, cy, pt.C_GHOST) end
  if (time() * 2) % 2 < 1.4 then rectfill(cx - 1, cy, 2, 16, C.acc) end
end

local function draw_side(y)
  font("6x12")
  local mode = pt.mode() or "-"
  panel(4, y, 140, 132)
  print("mode", 10, y + 6, C.dim)
  font("8x16")
  print(mode, 10, y + 18, C.acc)
  font("6x12")
  if state == "practice" then
    local secs = run.t0 and time() - run.t0 or 0
    local n = progress()
    local cpm = secs > 0.5 and n * 60 / secs or 0
    print("time", 10, y + 42, C.dim)
    print("chars a minute", 10, y + 70, C.dim)
    print("presses", 10, y + 98, C.dim)
    font("8x16")
    print(fmt_time(secs), 10, y + 52, C.text)
    print(tostring(math.floor(cpm + 0.5)), 10, y + 80, C.text)
    local pr = pt.presses()
    print(pr .. (n > 0 and string.format("  %.2f", pr / n) or ""), 10, y + 108, C.text)
  else
    print("presses", 10, y + 42, C.dim)
    font("8x16")
    print(tostring(pt.presses()), 10, y + 52, C.text)
  end
  font("6x12")
  panel(W - 144, y, 140, 132)
  local hy = y + 6
  local function help(name, what)
    local x = prompt(name, W - 138, hy, true) + 4
    print(what, x, hy, C.dim)
    hy = hy + 16
  end
  help("SELECT", pt.mode() == "keyboard" and "compose" or "keyboard")
  help("START", "pause")
  help("A", "space, x2: .")
  help("B", "erase")
  help("L1", "back")
  help("R1", "forward")
  local x = prompt("L1", W - 138, hy, true)
  x = prompt("R1", x + 2, hy, true)
  print("hold: enter", x + 4, hy, C.dim)
end

local function draw_menu()
  header("write with the controller")
  font("8x16")
  print("Choose, then Start.", 40, 40, C.dim)
  local y = 70
  for i, item in ipairs(MENU) do
    local on = i == sel
    if on then rectfill(32, y - 3, 400, 22, C.sel) end
    local label = ({ lang = "Language", mode = "Start with", delay = "Double press within",
                     text = "Text", hints = "Next press shown", start = "Start" })[item]
    print(label, 44, y, on and C.acc or C.text)
    if item ~= "start" then
      local v = menu_value(item)
      print("< " .. v .. " >", 260, y, on and C.text or C.dim)
    end
    y = y + 26
  end
  -- the best of each language
  font("6x12")
  y = y + 8
  print("best", 44, y, C.dim)
  for i, l in ipairs(LANGS) do
    local b = best[l.id]
    local s = b and string.format("%-9s %4d chars a minute   %.2f presses a character", l.name,
                                   math.floor(b.cpm + 0.5), b.ppc) or (l.name .. "  -")
    print(s, 44, y + 14 * i, C.text)
  end
  -- how compose works, in short
  local hx, hy = 450, 66
  panel(hx - 8, hy - 6, 190, 190)
  print("compose", hx, hy, C.acc)
  local rows = {
    "the cross writes a", "consonant, the syllable", "comes from the words:",
    "  up t, up up d", "  right n, m   down r, l", "  left s, c",
    "L2 R2 L2+R2: the others", "X Y turn the syllable", "R2 + A X Y: a word",
    "A space, twice a stop", "B erase   L1 R1 move", "L1+R1 held: new line",
  }
  for i, r in ipairs(rows) do print(r, hx, hy + i * 13, C.dim) end
  if not words_ready then print("reading the words...", 44, H - 20, C.dim) end
end

local function draw_writing()
  local sub = LANGS[cfg.lang].name .. "  -  " .. (target and ("text " .. cfg.text) or "free writing")
  header(sub)
  if target then
    panel(8, 28, W - 16, 150)
    draw_target(18, 36, 76, 20, 7)
    draw_hint(18, 186)
  else
    panel(8, 28, W - 16, 176)
    draw_free(18, 36, 76, 20, 8)
  end
  local ow, oh = pt.size()
  local ox, oy = (W - ow) // 2, H - oh - 2
  pt.draw(ox, oy)
  draw_side(oy)
end

local function draw_pause()
  draw_writing()
  panel(220, 90, 200, 24 + #PAUSE * 22)
  font("8x16")
  print("Pause", 236, 96, C.acc)
  for i, p in ipairs(PAUSE) do
    local y = 98 + i * 22
    if i == psel then rectfill(228, y - 3, 184, 20, C.sel) end
    print(p, 240, y, i == psel and C.acc or C.text)
  end
end

local function draw_result()
  header(LANGS[cfg.lang].name .. "  -  text " .. cfg.text .. " done")
  local r = result
  font("8x16")
  print("Done!", 60, 50, C.good, 2)
  local rows = {
    { "time", fmt_time(r.secs) },
    { "characters a minute", tostring(math.floor(r.cpm + 0.5)), r.best_cpm },
    { "presses", tostring(r.presses) },
    { "presses a character", string.format("%.2f", r.ppc), r.best_ppc },
    { "erased (circle)", tostring(r.erased) },
  }
  for i, row in ipairs(rows) do
    local y = 100 + i * 26
    print(row[1], 60, y, C.dim)
    print(row[2], 300, y, C.text)
    if row[3] then print("best!", 380, y, C.acc) end
  end
  local y = 300
  local x = prompt("A", 60, y) + 6
  x = print("next text", x, y, C.text) + 24
  x = prompt("Y", x, y) + 6
  x = print("again", x, y, C.text) + 24
  x = prompt("B", x, y) + 6
  print("menu", x, y, C.text)
end

function _draw()
  cls(C.bg)
  if state == "menu" then draw_menu()
  elseif state == "pause" then draw_pause()
  elseif state == "result" then draw_result()
  else draw_writing() end
  font("8x16")
end
