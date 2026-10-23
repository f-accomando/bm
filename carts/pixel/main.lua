-- bm Pixel: the pixel art of a .bm, on the console: its sprite sheet.
-- F1 draw (a sprite, zoomed: pencil, eraser, fill, lines, rectangles,
-- ovals, selections, mirror, onion skin and the animation playing), F2
-- sheet (the whole sheet: choose the sprite, copy, paste, size), F3
-- palette (the colours: edit, from the sheet, the presets), Esc menu;
-- Ctrl+S save, F5 try the game. Hold F12 (or press ?) for the keys.
-- Gamepad: Y + left/right page, X tap the commands, X + pad colour / tool.
-- The sheet is the project's (cart_load, sset, cart_sheet) and is saved
-- with cart_write(path, {sheet = true, palette = ...}): only the sheet
-- changes in the file, as SHEET8 with this palette first when it has at
-- most 256 colours; the SDK, bm Studio and bm Animator read it as theirs.

local W, H = SCREEN_W, SCREEN_H
local U = require "bmui"                 -- the mouse: what the keys do, by clicking
local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x707890, 0xFFC050, 0xFF6060, 0x3050A0
local C_CHK1, C_CHK2, C_GRID, C_GRID8, C_PT = 0x2A2E3A, 0x343846, 0x3C4254, 0x5A6280, 0xFFE070
local HINT_Y, STATUS_Y = H - 40, H - 24          -- text on rows of 16 pixels
local CANVAS_X, CANVAS_Y, CANVAS = 8, 32, 288    -- the zoomed sprite
local PANEL_X = 312
local SIZES = { 8, 16, 32, 64, 128 }
local ZOOMS = { 0.25, 0.5, 1, 2, 4 }

local floor, abs, max, min, sqrt = math.floor, math.abs, math.max, math.min, math.sqrt
local spack, sunpack = string.pack, string.unpack

local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end

-- the palettes of the suite: the SDK's sprite page, bm Studio and the 3D tools
local PAL_SDK = {
  0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
  0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
  0x14161E, 0x2E2832, 0x4A3E36, 0x6A5A48, 0x8A7A60, 0xB09078, 0x3A5A40, 0x6CC04A,
  0x203A6A, 0x3060D0, 0x70A8F0, 0x5A0A0A, 0xA01818, 0xE04040, 0xFF9030, 0xFFE080,
}
local PAL_STUDIO = {
  0x000000, 0x1C2030, 0x404450, 0x808490, 0xC0C4D0, 0xFFFFFF, 0x5A2A22, 0xA84632, 0xE84A5A, 0xE890B0,
  0xF09030, 0xF0D040, 0xFFF4C0, 0x8A5A30, 0xC49A5E, 0xE0C888, 0x285A28, 0x3E8A3A, 0x5CB048, 0xA8E070,
  0x1E4E6E, 0x2E6EB8, 0x3478C4, 0x7ABCE8, 0x2E8A70, 0x60D0C0, 0x3A2A6A, 0x6A50C8, 0xB060D8, 0xFFC050,
}

-- the code of a new cartridge: it shows its sheet
local VIEWER = [[
-- bm Pixel: a new sprite sheet. Replace this with your game: spr(n, x, y)
-- draws sprite n (8x8 cells, row by row), sspr(sx, sy, w, h, x, y) any
-- part of the sheet, sspr(..., false, false, 2) twice as big.

function _draw()
  cls(0x14161E)
  local w, h = cart_sheet()
  local z = math.min(1, 300 / w, 300 / h)
  sspr(0, 0, w, h, (SCREEN_W - w * z) // 2, 40, false, false, z)
  print("the sprite sheet of this cartridge (" .. w .. "x" .. h .. ")", 16, 12, 0xE0E4F0)
end
]]

-- the console keeps RGB565: a colour as sget() gives it back
local function q565(c)
  local r, g, b = c >> 19 & 31, c >> 10 & 63, c >> 3 & 31
  return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2)
end

local function hue(c)
  local r, g, b = (c >> 16 & 255) / 255, (c >> 8 & 255) / 255, (c & 255) / 255
  local mx, mn = max(r, g, b), min(r, g, b)
  if mx - mn < 0.08 then return -1 + mx end           -- greys first, dark to light
  local h
  if mx == r then h = (g - b) / (mx - mn) % 6
  elseif mx == g then h = (b - r) / (mx - mn) + 2
  else h = (r - g) / (mx - mn) + 4 end
  return h + mx * 0.01
end

----------------------------------------------------------------- state

local proj = { path = nil, title = "", palette = {} }
local sw, sh = 256, 256              -- the sheet
local rx, ry, rs = 0, 0, 16          -- the sprite being drawn: its corner and size
local px, py = 0, 0                  -- the pointer, in the sprite's pixels
local tool = "pencil"
local ci, ci_prev = 1, 0             -- the colour: palette index (0 = transparent)
local dirty = false
local page = "draw"
local msg, msg_c, msg_t = nil, C_TEXT, 0
local frame, edits = 0, 0
local undo, redo = {}, {}
local anim = { frames = 4, fps = 8, onion = false, play = true, t = 0, boxes = true, wrap = false }
local grid, mirror = true, false
local clip_b = nil                   -- the clipboard: {w, h, px}
local pick, input = nil, nil
local confirm_t, confirm_what = 0, nil
local files_seen = {}                -- per file: sprite, size, animation (save())
local busy = nil                     -- {text, fn, wait}: a save, after a frame that says so

local function box_colour(kind)
  if kind == "hurt" or kind == 0 then return 0x40A0FF       -- blue / cyan
  elseif kind == "hit" or kind == 1 then return 0xFF4040    -- red
  elseif kind == "body" or kind == 2 then return 0x40FF40   -- green
  else return 0xFFFF40                                      -- yellow
  end
end

-- the zone and frame that matches the sprite at (rx, ry)
local function current_zone()
  if not zones or not zone then return nil end
  local all = zones()
  if not all or #all == 0 then return nil end
  for _, zn in ipairs(all) do
    local zx, zy, zw, zh, zf, zfps = zone(zn)
    if zx and ry == zy and rs == zw and rs == zh and rx >= zx and rx < zx + zw * zf then
      local f = (rx - zx) // zw + 1
      return zn, zx, zy, zw, zh, zf, zfps, f
    end
  end
  for _, zn in ipairs(all) do
    local zx, zy, zw, zh, zf, zfps = zone(zn)
    if zx and rx == zx and ry == zy then
      return zn, zx, zy, zw, zh, zf, zfps, 1
    end
  end
  return nil
end

local function get_frame_boxes(zn, f)
  if not zn or not zboxes then return {} end
  local ok, res = pcall(zboxes, zn, f)
  return ok and res or {}
end

local function say(s, c, t) msg, msg_c, msg_t = s, c or C_TEXT, t or 200 end
local function pal() return proj.palette end
local function colour() return ci > 0 and pal()[ci] or nil end

local function colour_name(c)
  return c and string.format("#%06X", c) or "transparent"
end

-- the index of the palette colour that sget() would give as v (nil: none)
local function find_colour(v)
  if v == nil then return 0 end
  for i, c in ipairs(pal()) do if q565(c) == v then return i end end
end

local function sprite_index() return (ry // 8) * (sw // 8) + rx // 8 end

----------------------------------------------------------------- undo

-- a snapshot: a rectangle of the sheet, 4 bytes a pixel (-1 transparent)
local function snap(x, y, w, h)
  local t = {}
  for j = 0, h - 1 do
    for i = 0, w - 1 do t[#t + 1] = spack("<i4", sget(x + i, y + j) or -1) end
  end
  return { x = x, y = y, w = w, h = h, data = table.concat(t) }
end

local function put_snap(s)
  local k = 1
  for j = 0, s.h - 1 do
    for i = 0, s.w - 1 do
      local c = sunpack("<i4", s.data, k)
      k = k + 4
      sset(s.x + i, s.y + j, c >= 0 and c or nil)
    end
  end
end

-- before a change of that rectangle of the sheet (the sprite if none)
local function begin(x, y, w, h)
  undo[#undo + 1] = snap(x or rx, y or ry, w or rs, h or rs)
  if #undo > 40 then table.remove(undo, 1) end
  redo = {}
  dirty = true
  edits = edits + 1
end

local function do_undo(from, to, what)
  local s = table.remove(from)
  if not s then say("nothing to " .. what, C_DIM, 60); return end
  if s.x + s.w > sw or s.y + s.h > sh then say("cannot " .. what .. ": the sheet changed size", C_ERR); return end
  to[#to + 1] = snap(s.x, s.y, s.w, s.h)
  put_snap(s)
  if sp then sp.ghost = nil end
  dirty = true
  edits = edits + 1
  say(what, C_ACC, 60)
end

----------------------------------------------------------------- files

local function short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or U.PROJECTS
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):lower():gsub("[^%w_]", "")
  if stem == "" then stem = "sprites" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".bme"
end

-- a game (.bm, .b16) is read only: saving it makes its editable copy
local function is_game(path)
  local p = path and path:lower() or ""
  return p:match("%.bm$") ~= nil or p:match("%.b16$") ~= nil
end

local function list_files()
  local out = U.project_files({ bm = true, bme = true })
  table.sort(out)
  return out
end

local function fit_region()
  rs = min(rs, sw, sh)
  while rs > 8 and (rs > sw or rs > sh) do rs = rs // 2 end
  rx = clamp(rx - rx % rs, 0, sw - rs)
  ry = clamp(ry - ry % rs, 0, sh - rs)
  px, py = clamp(px, 0, rs - 1), clamp(py, 0, rs - 1)
end

-- what is kept for each file, in this cartridge's save()
local function remember()
  if not proj.path then return end
  files_seen[proj.path:lower()] = { rx = rx, ry = ry, rs = rs, frames = anim.frames, fps = anim.fps, ci = ci,
                            palette = pal(), page = page ~= "menu" and page or "draw", t = frame }
  local n, oldest, ot = 0, nil, nil
  for p, v in pairs(files_seen) do
    n = n + 1
    if not ot or v.t < ot then oldest, ot = p, v.t end
  end
  if n > 6 then files_seen[oldest] = nil end
  pcall(save, { files = files_seen })
end

local reset_pages, go                -- with the pages

local function open_file(path)
  local p, e = cart_load(path)
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C_ERR); return false end
  proj = { path = path, title = p.title or "", palette = {} }
  sw, sh = cart_sheet()
  local seen = files_seen[path:lower()] or {}
  local from
  if p.palette and #p.palette > 0 then
    for i, c in ipairs(p.palette) do proj.palette[i] = c end
    from = "the file's"
  elseif seen.palette and #seen.palette > 0 then
    for i, c in ipairs(seen.palette) do proj.palette[i] = c end
    from = "the last one"
  else
    for i, c in ipairs(PAL_SDK) do proj.palette[i] = c end
    from = "the SDK's"
  end
  rx, ry, rs = seen.rx or 0, seen.ry or 0, seen.rs or 16
  anim.frames, anim.fps = seen.frames or 4, seen.fps or 8
  ci = clamp(seen.ci or 1, 0, #proj.palette)
  undo, redo, dirty = {}, {}, false
  fit_region()
  reset_pages()
  say(string.format("%s: sheet %dx%d, %d colours (%s palette)%s", path, sw, sh, #proj.palette, from,
                    is_game(path) and "; a game: saving makes its copy" or ""), C_ACC)
  return true
end

local function new_sheet()
  cart_new()
  sw, sh = cart_sheet()
  proj = { path = nil, title = "", palette = {} }
  for i, c in ipairs(PAL_SDK) do proj.palette[i] = c end
  rx, ry, rs, ci = 0, 0, 16, 9
  undo, redo, dirty = {}, {}, false
  fit_region()
  reset_pages()
  say("a new 256x256 sheet: Esc > Save as gives it a name", C_ACC)
end

-- A big sheet takes seconds to write: the save waits for a frame that
-- says "saving", then `after` runs if it went well.
local function save_to(path, from, after)
  if not path then say("no name yet: Esc > Save as", C_ERR); return end
  local t = { sheet = true, palette = pal(), from = from }
  if sp and sp.zdirty then t.sections = { [11] = cart_data(11) or false } end   -- the zones changed (Stack page)
  if not proj.path then
    t.lua = VIEWER
    t.title = path:match("([^/]+)%.[^.]*$") or "sprites"
  end
  -- (wait: the box drawn on 4 frames, one per page of the screen and more)
  busy = { text = "saving " .. path .. " ...", wait = 3, fn = function()
    local ok, e = cart_write(path, t)
    if not ok then say("save failed: " .. tostring(e), C_ERR); return end
    if type(e) == "string" then path = e end    -- the editable copy of a game, or a new project
    proj.path = path
    dirty = false
    sp.zdirty = false
    remember()
    say(U.saved_text(path), C_ACC)
    if after then after() end
  end }
end

----------------------------------------------------------------- input

local held, rp = {}, {}
local xy_down, xy_combo, tap = {}, {}, {}
local function read_pad()
  for i = 0, 7 do
    if btn(i) then held[i] = (held[i] or 0) + 1 else held[i] = 0 end
    local h = held[i]
    rp[i] = h == 1 or (h > 14 and h % 4 == 0)
  end
  for _, b in ipairs({ 6, 7 }) do
    tap[b] = false
    if btn(b) then
      if not xy_down[b] then xy_down[b], xy_combo[b] = true, false end
      for i = 0, 7 do if i ~= b and btn(i) then xy_combo[b] = true end end
    elseif xy_down[b] then
      xy_down[b] = false
      tap[b] = not xy_combo[b]
    end
  end
end

-- the keys as chips (prompt(), the look of all the Dev apps): the labels
-- stay on the font's 8 px columns
local function snap(x) return (x + 7) // 8 * 8 end

-- the keys of the page on the hint row: { { {keys...}, label }, ... }, as many as fit
local function hint(list)
  local x = 0
  for _, h in ipairs(list) do
    local kx = x
    for _, k in ipairs(h[1]) do kx = kx + prompt(k) + 1 end
    if snap(kx + 2) + #h[2] * 8 > W then break end
    local x0 = x
    for _, k in ipairs(h[1]) do x = prompt(k, x, HINT_Y) + 1 end
    x = print(h[2], snap(x + 2), HINT_Y, C_DIM)
    U.key_zone(x0, HINT_Y, x - x0, 16, h[1])        -- a click: the key
    x = x + 12
  end
end

-- a key as a chip, or the pad's button when a pad was used last and the
-- action has one; then its label. Returns the x after it.
local function chip_hint(key, pad, label, x, y, c)
  local li = lastinput()
  local x0 = x
  x = prompt(pad and (li == "ds4" or li == "pad") and pad or key, x, y)
  x = print(label, snap(x + 3), y, c or C_DIM)
  U.key_zone(x0, y, x - x0, 16, key)
  return x + 12
end

local function choose(title, rows, sel) pick = { title = title, rows = rows, sel = sel or 1 } end
local function ask(label, text, done) input = { label = label, text = text, done = done } end

local function confirmed(what, text)
  if confirm_what == what and confirm_t > 0 then confirm_what = nil; return true end
  confirm_what, confirm_t = what, 150
  say(text, C_ERR)
  return false
end

local function pick_key(k)
  local p = pick
  if k == "up" then p.sel = (p.sel - 2) % #p.rows + 1
  elseif k == "down" then p.sel = p.sel % #p.rows + 1
  elseif k == "pgup" then p.sel = max(1, p.sel - 12)
  elseif k == "pgdn" then p.sel = min(#p.rows, p.sel + 12)
  elseif k == "esc" or k == "back" then pick = nil
  elseif k == "\n" or k == "ok" then
    pick = nil
    p.rows[p.sel][2]()
  end
end

local function input_key(k)
  if k == "\n" then local d, t = input.done, input.text; input = nil; d(t)
  elseif k == "esc" then input = nil
  else U.line_key(input, k, 40) end
end

local function draw_pick()
  local p = pick
  local rows = min(#p.rows, 14)
  local h = (rows + 2) * 16
  local y0 = max(32, (H - h) // 32 * 16)
  U.zone(0, 0, W, H, "shade")                    -- a click outside: Esc
  U.zone(40, y0, 560, h, "box")
  rectfill(40, y0, 560, h, C_PANEL)
  rect(40, y0, 560, h, C_ACC)
  local tx = print(p.title, 56, y0, C_ACC) + 16
  chip_hint("esc", "B", "back", chip_hint("enter", "A", "choose", tx, y0), y0)
  local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
  for i = first, min(#p.rows, first + rows - 1) do
    local y = y0 + 16 + (i - first) * 16
    U.zone(48, y, 544, 16, "pick", i)
    if i == p.sel then rectfill(48, y, 544, 16, C_SEL) end
    print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C_TEXT)
  end
  if first + rows - 1 < #p.rows then print("v", 576, y0 + rows * 16, C_DIM) end
end

local function draw_input()
  U.zone(0, 0, W, H, "shade")
  U.zone(80, 144, 480, 64, "box")
  rectfill(80, 144, 480, 64, C_PANEL)
  rect(80, 144, 480, 64, C_ACC)
  print(input.label, 96, 160, C_DIM)
  U.zone(88, 176, 464, 16, "field")              -- a click puts the caret
  U.line_draw(input, 96, 176, C_TEXT, (frame // 20) % 2 == 0)
  chip_hint("esc", nil, "cancel", chip_hint("enter", nil, "ok", 336, 144), 144)
end

-- the mouse in the dialogs: a click chooses a row (on the chosen one, or a
-- double click: takes it), the wheel moves, a click outside is Esc. True
-- when a dialog is open (it has the mouse).
local function dialog_mouse()
  if not pick and not input then return false end
  if pick and U.wheel ~= 0 then pick.sel = clamp(pick.sel - U.wheel, 1, #pick.rows) end
  local z = U.click(0)
  if z and z.kind == "pick" and pick then
    if z.a == pick.sel or U.double then U.press("\n") else pick.sel = z.a end
  elseif z and z.kind == "shade" then
    U.press("esc")
  elseif z and z.kind == "field" and input then
    U.line_click(input, 96, 8)
  end
  return true
end

-- a checkerboard under the transparent pixels
local function checker(x, y, w, h, step)
  rectfill(x, y, w, h, C_CHK1)
  step = step or 8
  for j = 0, (h - 1) // step do
    for i = (j % 2), (w - 1) // step, 2 do
      rectfill(x + i * step, y + j * step, min(step, w - i * step), min(step, h - j * step), C_CHK2)
    end
  end
end

local function swatch(x, y, s, c)
  if c then rectfill(x, y, s, s, c) else checker(x, y, s, s, s // 2) end
end


-- the pages: what each one gives the others
local dp, draw_reset, draw_key, draw_pad, draw_update, draw_actions, draw_draw, draw_mouse
local shp, sheet_reset, sheet_key, sheet_pad, sheet_actions, draw_sheet, sheet_mouse
local pp, pal_reset, pal_key, pal_pad, pal_actions, draw_palette, pal_mouse

----------------------------------------------------------------- blocks of pixels

-- {w, h, px}: the pixels of a rectangle of the sprite (false = transparent)
local function read_block(x, y, w, h)
  local b = { w = w, h = h, px = {} }
  for j = 0, h - 1 do
    for i = 0, w - 1 do b.px[j * w + i + 1] = sget(rx + x + i, ry + y + j) or false end
  end
  return b
end

-- written at (x, y) of the sprite; skip: its transparent pixels leave what is under
local function write_block(b, x, y, skip)
  for j = 0, b.h - 1 do
    for i = 0, b.w - 1 do
      local c = b.px[j * b.w + i + 1]
      local tx, ty = x + i, y + j
      if (c or not skip) and tx >= 0 and ty >= 0 and tx < rs and ty < rs then sset(rx + tx, ry + ty, c or nil) end
    end
  end
end

local function flip_block(b, hor)
  local o = { w = b.w, h = b.h, px = {} }
  for j = 0, b.h - 1 do
    for i = 0, b.w - 1 do
      local si, sj = hor and b.w - 1 - i or i, hor and j or b.h - 1 - j
      o.px[j * b.w + i + 1] = b.px[sj * b.w + si + 1]
    end
  end
  return o
end

-- a quarter turn clockwise
local function turn_block(b)
  local o = { w = b.h, h = b.w, px = {} }
  for j = 0, o.h - 1 do
    for i = 0, o.w - 1 do o.px[j * o.w + i + 1] = b.px[(b.h - 1 - i) * b.w + j + 1] end
  end
  return o
end

----------------------------------------------------------------- draw page
do

dp = { anchor = nil, sel = nil, float = nil, stroke = nil, last_k = nil, last_f = 0, ghost = nil, ghost_key = nil,
       preview = nil, preview_key = nil }

local TOOLS = {
  { "pencil", "b" }, { "eraser", "e" }, { "fill", "g" }, { "pick", "i" }, { "line", "l" }, { "rect", "u" },
  { "rectfill", "U" }, { "oval", "o" }, { "ovalfill", "O" }, { "select", "m" },
  { "wand", "w" },
}
local TWO_STEP = { line = true, rect = true, rectfill = true, oval = true, ovalfill = true, select = true }

local function zoom() return max(1, CANVAS // rs) end

function draw_reset()
  dp.anchor, dp.sel, dp.float, dp.stroke, dp.ghost_key, dp.preview_key = nil, nil, nil, nil, nil, nil
  px, py = clamp(px, 0, rs - 1), clamp(py, 0, rs - 1)
end

local function inside(x, y) return x >= 0 and y >= 0 and x < rs and y < rs end

local function plot(x, y, c)
  if inside(x, y) then sset(rx + x, ry + y, c) end
  if mirror and inside(rs - 1 - x, y) then sset(rx + rs - 1 - x, ry + y, c) end
end

local function get(x, y) return sget(rx + x, ry + y) end

local function line_pts(x0, y0, x1, y1)
  local pts = {}
  local dx, dy = abs(x1 - x0), -abs(y1 - y0)
  local sx, sy = x0 < x1 and 1 or -1, y0 < y1 and 1 or -1
  local err = dx + dy
  while true do
    pts[#pts + 1] = { x0, y0 }
    if x0 == x1 and y0 == y1 then break end
    local e2 = 2 * err
    if e2 >= dy then err = err + dy; x0 = x0 + sx end
    if e2 <= dx then err = err + dx; y0 = y0 + sy end
  end
  return pts
end

local function box(x0, y0, x1, y1) return min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1) end

local function rect_pts(x0, y0, x1, y1, fill)
  x0, y0, x1, y1 = box(x0, y0, x1, y1)
  local pts = {}
  for y = y0, y1 do
    for x = x0, x1 do
      if fill or x == x0 or x == x1 or y == y0 or y == y1 then pts[#pts + 1] = { x, y } end
    end
  end
  return pts
end

-- the oval inside the box: its pixels, or those on its edge
local function oval_pts(x0, y0, x1, y1, fill)
  x0, y0, x1, y1 = box(x0, y0, x1, y1)
  local w, h = x1 - x0 + 1, y1 - y0 + 1
  if w <= 2 or h <= 2 then return rect_pts(x0, y0, x1, y1, true) end
  local cx, cy, a, b = x0 + w / 2, y0 + h / 2, w / 2, h / 2
  local function ins(x, y)
    if x < x0 or x > x1 or y < y0 or y > y1 then return false end
    local u, v = (x + 0.5 - cx) / a, (y + 0.5 - cy) / b
    return u * u + v * v <= 1.0001
  end
  local pts = {}
  for y = y0, y1 do
    for x = x0, x1 do
      if ins(x, y) and (fill or not (ins(x - 1, y) and ins(x + 1, y) and ins(x, y - 1) and ins(x, y + 1))) then
        pts[#pts + 1] = { x, y }
      end
    end
  end
  return pts
end

local function shape_pts(x0, y0, x1, y1)
  if tool == "line" then return line_pts(x0, y0, x1, y1)
  elseif tool == "rect" or tool == "rectfill" then return rect_pts(x0, y0, x1, y1, tool == "rectfill")
  elseif tool == "oval" or tool == "ovalfill" then return oval_pts(x0, y0, x1, y1, tool == "ovalfill") end
  return {}
end

-- 4-connected, inside the sprite
local function flood(x, y, c)
  local target = get(x, y)
  if target == (c and q565(c) or nil) then return 0 end
  local stack, n = { x, y }, 0
  while #stack > 0 do
    local yy = table.remove(stack)
    local xx = table.remove(stack)
    if inside(xx, yy) and get(xx, yy) == target then
      sset(rx + xx, ry + yy, c)
      n = n + 1
      stack[#stack + 1] = xx + 1; stack[#stack + 1] = yy
      stack[#stack + 1] = xx - 1; stack[#stack + 1] = yy
      stack[#stack + 1] = xx; stack[#stack + 1] = yy + 1
      stack[#stack + 1] = xx; stack[#stack + 1] = yy - 1
    end
  end
  return n
end

local function set_colour(i)
  if i ~= ci then ci_prev = ci end
  ci = clamp(i, 0, #pal())
end

-- the eyedropper: the colour under the pointer (a new one joins the palette)
local function pick_at(x, y)
  local v = get(x, y)
  local i = find_colour(v)
  if not i then
    if #pal() >= 256 then say("the palette is full (256 colours)", C_ERR); return end
    pal()[#pal() + 1] = v
    i = #pal()
    dirty = true
    say("a new colour in the palette: " .. colour_name(v), C_ACC, 90)
  else
    say("colour " .. i .. ": " .. colour_name(v), C_DIM, 60)
  end
  set_colour(i)
end

local function sel_box()
  local s = dp.sel
  return s[1], s[2], s[3] - s[1] + 1, s[4] - s[2] + 1
end

-- the wand's mask (a set keyed y*rs+x) keeps the other tools to the pixels of one colour
local MK = {}
function MK.wand(x, y)
  local target = get(x, y)
  local m, stack, sp, n = {}, { x, y }, 2, 0
  local x0, y0, x1, y1 = x, y, x, y
  while sp > 0 do
    local yy, xx = stack[sp], stack[sp - 1]
    sp = sp - 2
    if inside(xx, yy) and not m[yy * rs + xx] and get(xx, yy) == target then
      m[yy * rs + xx] = true
      n = n + 1
      if xx < x0 then x0 = xx elseif xx > x1 then x1 = xx end
      if yy < y0 then y0 = yy elseif yy > y1 then y1 = yy end
      stack[sp + 1], stack[sp + 2], stack[sp + 3], stack[sp + 4] = xx + 1, yy, xx - 1, yy
      stack[sp + 5], stack[sp + 6], stack[sp + 7], stack[sp + 8] = xx, yy + 1, xx, yy - 1
      sp = sp + 8
    end
  end
  return { x0, y0, x1, y1, mask = m }, n
end
-- the block with the pixels outside the mask transparent (no mask: as it is)
function MK.cut(b, x, y)
  local m = dp.sel and dp.sel.mask
  if not m then return b end
  for j = 0, b.h - 1 do
    for i = 0, b.w - 1 do
      if not m[(y + j) * rs + x + i] then b.px[j * b.w + i + 1] = false end
    end
  end
  return b
end
-- the pixels of the box that the tools reach become transparent
function MK.wipe(x, y, w, h)
  local m = dp.sel and dp.sel.mask
  for j = 0, h - 1 do
    for i = 0, w - 1 do
      if not m or m[(y + j) * rs + x + i] then sset(rx + x + i, ry + y + j, nil) end
    end
  end
end

local function use_tool()
  local c = colour()
  if dp.float then                          -- a block floating: put it down
    local f = dp.float
    if not f.lifted then begin() end
    write_block(f.b, f.x, f.y, true)
    dp.float = nil
    edits = edits + 1
    say("put down", C_ACC, 60)
    return
  end
  if tool == "pencil" or tool == "eraser" then
    begin()
    if tool == "eraser" then c = nil end
    plot(px, py, c)
    dp.stroke = { px, py }
  elseif tool == "fill" then
    begin()
    local n = flood(px, py, c)
    if n == 0 then table.remove(undo) else say(n .. " pixels filled", C_DIM, 60) end
  elseif tool == "pick" then
    pick_at(px, py)
  elseif tool == "wand" then
    local n
    dp.sel, n = MK.wand(px, py)
    dp.anchor = nil
    say(n .. " pixels chosen: Ctrl+C copy, Ctrl+X cut, Enter lift, h v r, Del", C_DIM, 200)
  elseif TWO_STEP[tool] then
    if not dp.anchor then
      dp.anchor = { px, py }
      if tool == "select" then dp.sel = nil end
    else
      local a = dp.anchor
      dp.anchor = nil
      if tool == "select" then
        local x0, y0, x1, y1 = box(a[1], a[2], px, py)
        dp.sel = { x0, y0, x1, y1 }
        say(string.format("chosen %dx%d: Ctrl+C copy, Ctrl+X cut, Enter lift, h v r, Del", x1 - x0 + 1, y1 - y0 + 1),
            C_DIM, 200)
      else
        begin()
        for _, p in ipairs(shape_pts(a[1], a[2], px, py)) do plot(p[1], p[2], c) end
        edits = edits + 1
      end
    end
  end
end

-- the pencil held down: a line from where it was
local function stroke_to()
  local s = dp.stroke
  if not s or (s[1] == px and s[2] == py) then return end
  local c = colour()
  if tool == "eraser" then c = nil end
  for _, p in ipairs(line_pts(s[1], s[2], px, py)) do plot(p[1], p[2], c) end
  dp.stroke = { px, py }
  edits = edits + 1
end

-- the selection (or the whole sprite): flipped, turned, cleared, copied
local function target_box()
  if dp.sel then return sel_box() end
  return 0, 0, rs, rs
end

local function transform(what)
  local x, y, w, h = target_box()
  if what == "turn" and w ~= h then say("only a square turns: choose one, or the whole sprite", C_ERR); return end
  begin()
  local b = MK.cut(read_block(x, y, w, h), x, y)
  local m = dp.sel and dp.sel.mask
  local mb
  if m then
    mb = { w = w, h = h, px = {} }
    for j = 0, h - 1 do for i = 0, w - 1 do mb.px[j * w + i + 1] = m[(y + j) * rs + x + i] or false end end
    MK.wipe(x, y, w, h)
  end
  if what == "flip h" then b = flip_block(b, true)
  elseif what == "flip v" then b = flip_block(b, false)
  elseif what == "turn" then b = turn_block(b) end
  if mb then                                -- the mask moves with the pixels
    local nm = {}
    if what == "flip h" then mb = flip_block(mb, true)
    elseif what == "flip v" then mb = flip_block(mb, false)
    else mb = turn_block(mb) end
    for j = 0, mb.h - 1 do for i = 0, mb.w - 1 do if mb.px[j * mb.w + i + 1] then nm[(y + j) * rs + x + i] = true end end end
    dp.sel.mask = nm
  end
  write_block(b, x, y, mb ~= nil)
  edits = edits + 1
  say(what .. (dp.sel and " (the selection)" or ""), C_DIM, 60)
end

local function clear_box()
  local x, y, w, h = target_box()
  begin()
  MK.wipe(x, y, w, h)
  edits = edits + 1
  say("cleared", C_DIM, 60)
end

local function copy_box(cut)
  local x, y, w, h = target_box()
  clip_b = MK.cut(read_block(x, y, w, h), x, y)
  if cut then
    begin()
    MK.wipe(x, y, w, h)
    edits = edits + 1
  end
  say((cut and "cut " or "copied ") .. w .. "x" .. h, C_DIM, 60)
end

local function paste()
  if not clip_b then say("nothing copied yet (Ctrl+C)", C_ERR); return end
  dp.float = { b = clip_b, x = px, y = py }
  dp.sel, dp.anchor = nil, nil
  say("arrows move it, space or Enter puts it down, Esc drops it", C_DIM, 200)
end

-- the selection lifted off the sprite: it moves with the arrows
local function lift()
  if not dp.sel then return end
  local x, y, w, h = sel_box()
  begin()
  local b = MK.cut(read_block(x, y, w, h), x, y)
  MK.wipe(x, y, w, h)
  dp.float = { b = b, x = x, y = y, lifted = true }
  dp.sel = nil
  edits = edits + 1
  say("arrows move it, space or Enter puts it down, Esc puts it back", C_DIM, 200)
end

local function drop_float()
  local f = dp.float
  dp.float = nil
  if f and f.lifted then
    local s = table.remove(undo)
    if s then put_snap(s) end
    edits = edits + 1
  end
end

-- the content of the sprite one pixel over, round the edge
local function shift(dx, dy)
  begin()
  local b = read_block(0, 0, rs, rs)
  local o = { w = rs, h = rs, px = {} }
  for j = 0, rs - 1 do
    for i = 0, rs - 1 do o.px[((j + dy) % rs) * rs + (i + dx) % rs + 1] = b.px[j * rs + i + 1] end
  end
  write_block(o, 0, 0, false)
  edits = edits + 1
end

local function go_sprite(d)
  local per = sw // rs
  local n = (ry // rs) * per + rx // rs + d
  local total = per * (sh // rs)
  n = n % total
  rx, ry = (n % per) * rs, (n // per) * rs
  draw_reset()
end

local function next_size()
  local i = 1
  for k, s in ipairs(SIZES) do if s == rs then i = k end end
  local n = SIZES[i % #SIZES + 1]
  if n > sw or n > sh then n = 8 end
  rs = n
  fit_region()
  draw_reset()
  say("sprites of " .. rs .. "x" .. rs, C_DIM, 60)
end

local function frame_xy(k)                -- the k-th frame from this sprite (0 = this one)
  local per = sw // rs
  local total = per * (sh // rs)
  local n = ((ry // rs) * per + rx // rs + k) % total
  return (n % per) * rs, (n // per) * rs
end

local function assistant()
  if not ai or not ai.sprite then say("the assistant is not here", C_ERR); return end
  ask("a sprite base: what (e.g. green slime, coin, ship)", "", function(t)
    if t == "" then return end
    local ok, s = pcall(ai.sprite, t, { size = min(rs, 32), palette = pal() })
    if not ok or type(s) ~= "table" then say("the assistant: " .. tostring(s), C_ERR); return end
    local b = { w = s.w, h = s.h, px = {} }
    for i = 1, s.w * s.h do
      local c = s.px[i]
      b.px[i] = (c and c >= 0) and q565(c) or false
    end
    clip_b = b
    dp.float = { b = b, x = 0, y = 0 }
    say("the assistant's " .. (s.name or s.gen or "sprite") .. ": move it, Enter puts it down", C_ACC, 300)
  end)
end

local COMMANDS = {
  { "Pencil (b)", "b" }, { "Eraser (e)", "e" }, { "Fill (g)", "g" }, { "Colour from the sprite (i)", "i" },
  { "Line (l)", "l" }, { "Rectangle (u)", "u" }, { "Filled rectangle (U)", "U" }, { "Oval (o)", "o" },
  { "Filled oval (O)", "O" }, { "Select (m)", "m" }, { "Wand select (w)", "w" }, { "Copy (Ctrl+C)", "^c" }, { "Cut (Ctrl+X)", "^x" },
  { "Paste (Ctrl+V)", "^v" }, { "Flip left-right (h)", "h" }, { "Flip up-down (v)", "v" }, { "Turn (r)", "r" },
  { "Clear (Del)", "del" }, { "Mirror drawing (y)", "y" }, { "Grid (t)", "t" }, { "Onion skin (k)", "k" },
  { "Play the animation (p)", "p" }, { "More frames (+)", "+" }, { "Fewer frames (-)", "-" },
  { "Faster (>)", ">" }, { "Slower (<)", "<" }, { "Sprite size (z)", "z" }, { "Next sprite (PgDn)", "pgdn" },
  { "Previous sprite (PgUp)", "pgup" }, { "Tile view (q)", "q" }, { "Assistant: a sprite base (F6)", "f6" },
}

function draw_actions()
  local rows = {}
  for _, c in ipairs(COMMANDS) do rows[#rows + 1] = { c[1], function() draw_key(c[2]) end } end
  choose("draw: " .. tool, rows)
end

function draw_key(k)
  if k == "left" or k == "right" or k == "up" or k == "down" then
    local dx = k == "left" and -1 or (k == "right" and 1 or 0)
    local dy = k == "up" and -1 or (k == "down" and 1 or 0)
    if dp.float then
      dp.float.x, dp.float.y = dp.float.x + dx, dp.float.y + dy
    else
      px, py = clamp(px + dx, 0, rs - 1), clamp(py + dy, 0, rs - 1)
    end
  elseif k == " " or k == "\n" then
    if k == "\n" and dp.sel and not dp.float then lift() else use_tool() end
  elseif k == "esc" then
    if dp.float then drop_float(); say("dropped", C_DIM, 60)
    elseif dp.anchor then dp.anchor = nil
    elseif dp.sel then dp.sel = nil end
  elseif k == "home" then px, py = rs // 2, rs // 2
  elseif k == "^c" then copy_box(false)
  elseif k == "^x" then copy_box(true)
  elseif k == "^v" then paste()
  elseif k == "h" then transform("flip h")
  elseif k == "v" then transform("flip v")
  elseif k == "r" then transform("turn")
  elseif k == "del" or k == "\b" then clear_box()
  elseif k == "W" then shift(0, -1)
  elseif k == "S" then shift(0, 1)
  elseif k == "A" then shift(-1, 0)
  elseif k == "D" then shift(1, 0)
  elseif k == "y" then mirror = not mirror; say(mirror and "mirror: what you draw on one side is drawn on the other" or "mirror off", C_DIM, 90)
  elseif k == "t" then grid = not grid
  elseif k == "k" then anim.onion = not anim.onion; say(anim.onion and "onion skin: the frame before shows under" or "onion skin off", C_DIM, 90)
  elseif k == "p" then anim.play = not anim.play
  elseif k == "+" or k == "=" then anim.frames = min(16, anim.frames + 1); say(anim.frames .. " frames", C_DIM, 60)
  elseif k == "-" then anim.frames = max(1, anim.frames - 1); say(anim.frames .. " frames", C_DIM, 60)
  elseif k == ">" then anim.fps = min(30, anim.fps + 1); say(anim.fps .. " frames a second", C_DIM, 60)
  elseif k == "<" then anim.fps = max(1, anim.fps - 1); say(anim.fps .. " frames a second", C_DIM, 60)
  elseif k == "z" then next_size()
  elseif k == "j" then anim.boxes = not anim.boxes; say(anim.boxes and "hitboxes on (BOXES)" or "hitboxes off", C_DIM, 90)
  elseif k == "q" then anim.wrap = not anim.wrap; say(anim.wrap and "tile view: the seams show" or "tile view off", C_DIM, 90)
  elseif k == "pgdn" then go_sprite(1)
  elseif k == "pgup" then go_sprite(-1)
  elseif k == "," then set_colour((ci - 1) % (#pal() + 1))
  elseif k == "." then set_colour((ci + 1) % (#pal() + 1))
  elseif k == "x" then set_colour(ci_prev)
  elseif k:match("^%d$") then set_colour(k == "0" and 10 or tonumber(k))
  elseif k == "f6" then assistant()
  else
    for _, t in ipairs(TOOLS) do
      if k == t[2] then
        tool, dp.anchor = t[1], nil
        say(tool, C_DIM, 40)
      end
    end
  end
end

function draw_pad()
  if btn(6) then                      -- X + pad: the colour (left/right), the tool (up/down)
    if rp[0] then draw_key(",") end
    if rp[1] then draw_key(".") end
    if rp[2] or rp[3] then
      local i = 1
      for k, t in ipairs(TOOLS) do if t[1] == tool then i = k end end
      i = (i - 1 + (rp[3] and 1 or -1)) % #TOOLS + 1
      draw_key(TOOLS[i][2])
    end
    return
  end
  if rp[0] then draw_key("left") end
  if rp[1] then draw_key("right") end
  if rp[2] then draw_key("up") end
  if rp[3] then draw_key("down") end
  if btnp(4) then draw_key(" ") end
  if btnp(5) then
    if dp.float or dp.anchor or dp.sel then draw_key("esc") else pick_at(px, py) end
  end
  if tap[6] then draw_actions() end
end

-- every frame: the pencil held down (space or A), the animation's clock
function draw_update(dt)
  if dp.stroke and (tool == "pencil" or tool == "eraser") and (keyheld("space") or btn(4) or U.down(0)) then
    stroke_to()
  else
    dp.stroke = nil
  end
  if anim.play then anim.t = anim.t + dt end
end

-- the mouse: over the canvas the pointer is the mouse's; the left button
-- uses the tool (the pencil goes on while held; a shape or a selection:
-- pressed at one corner, let go at the other), the middle one picks the
-- colour, the wheel changes it; a click on a colour of the panel takes it
local shaping                          -- a shape begun by the mouse's press
function draw_mouse()
  local z = U.at()
  if z and z.kind == "swatch" and U.pressed(0) then ci = z.a; return end
  local zc = z and z.kind == "canvas" and z
  if zc and (U.moved or U.pressed(0) or U.pressed(2)) and not dp.float then
    px, py = clamp((U.x - CANVAS_X) // zc.a, 0, rs - 1), clamp((U.y - CANVAS_Y) // zc.a, 0, rs - 1)
  end
  if not zc then
    if shaping and U.released(0) and dp.anchor then draw_key(" ") end
    if not U.down(0) then shaping = nil end
    return
  end
  if U.wheel ~= 0 then draw_key(U.wheel > 0 and "," or ".") end
  if U.pressed(2) then pick_at(px, py) end
  if U.pressed(0) then
    local had = dp.anchor ~= nil
    draw_key(" ")
    shaping = TWO_STEP[tool] and not had and dp.anchor ~= nil
  elseif U.released(0) and shaping then
    if dp.anchor and (px ~= dp.anchor[1] or py ~= dp.anchor[2]) then draw_key(" ") end   -- the other corner
    shaping = nil
  end
end

-- the frame before, as dots under the transparent pixels (cached)
local function ghost()
  local key = rx .. "," .. ry .. "," .. rs .. "," .. edits
  if dp.ghost_key ~= key then
    local gx, gy = frame_xy(-1)
    local dots = {}
    for j = 0, rs - 1 do
      for i = 0, rs - 1 do
        local c = sget(gx + i, gy + j)
        if c and not sget(rx + i, ry + j) then dots[#dots + 1] = { i, j, c } end
      end
    end
    dp.ghost, dp.ghost_key = dots, key
  end
  return dp.ghost
end

local function draw_points(pts, c, z)
  local d = max(1, z // 3)
  for _, p in ipairs(pts) do
    if inside(p[1], p[2]) then
      local x, y = CANVAS_X + p[1] * z, CANVAS_Y + p[2] * z
      if c then rectfill(x, y, z, z, c) else rectfill(x + z // 2 - d // 2, y + z // 2 - d // 2, d, d, 0xFFFFFF) end
    end
  end
end

function draw_draw()
  cls(C_BG)
  local z = zoom()
  local cw = rs * z
  checker(CANVAS_X, CANVAS_Y, cw, cw, max(8, z))
  U.zone(CANVAS_X, CANVAS_Y, cw, cw, "canvas", z)
  if anim.onion and anim.frames > 1 then
    local d = max(2, z // 3)
    for _, g in ipairs(ghost()) do
      rectfill(CANVAS_X + g[1] * z + (z - d) // 2, CANVAS_Y + g[2] * z + (z - d) // 2, d, d, g[3])
    end
  end
  sspr(rx, ry, rs, rs, CANVAS_X, CANVAS_Y, false, false, z)
  if grid and z >= 4 then
    for i = 1, rs - 1 do
      local c = i % 8 == 0 and C_GRID8 or C_GRID
      if z >= 6 or i % 8 == 0 then
        line(CANVAS_X + i * z, CANVAS_Y, CANVAS_X + i * z, CANVAS_Y + cw - 1, c)
        line(CANVAS_X, CANVAS_Y + i * z, CANVAS_X + cw - 1, CANVAS_Y + i * z, c)
      end
    end
  end
  rect(CANVAS_X - 1, CANVAS_Y - 1, cw + 2, cw + 2, C_GRID8)
  if mirror then
    local mx = CANVAS_X + cw // 2
    line(mx, CANVAS_Y, mx, CANVAS_Y + cw - 1, C_ACC)
  end
  -- what is being made: the shape, the selection, the floating block
  if dp.anchor and tool ~= "select" then
    local key = dp.anchor[1] .. "," .. dp.anchor[2] .. "," .. px .. "," .. py .. tool
    if dp.preview_key ~= key then dp.preview, dp.preview_key = shape_pts(dp.anchor[1], dp.anchor[2], px, py), key end
    draw_points(dp.preview, colour(), z)
  end
  local function frame_box(x0, y0, x1, y1, c)
    rect(CANVAS_X + x0 * z, CANVAS_Y + y0 * z, (x1 - x0 + 1) * z, (y1 - y0 + 1) * z, c)
  end
  if dp.anchor and tool == "select" then
    local x0, y0, x1, y1 = box(dp.anchor[1], dp.anchor[2], px, py)
    frame_box(x0, y0, x1, y1, (frame // 8) % 2 == 0 and 0xFFFFFF or C_ACC)
  elseif dp.sel and dp.sel.mask then          -- the wand: a dotted line round the pixels only
    local m, col = dp.sel.mask, (frame // 8) % 2 == 0 and 0xFFFFFF or C_ACC
    for k in pairs(m) do
      local x, y = k % rs, k // rs
      local sx, sy = CANVAS_X + x * z, CANVAS_Y + y * z
      for t = 0, z - 1, 4 do
        local l = min(2, z - t)
        if not m[k - rs] then rectfill(sx + t, sy, l, 1, col) end
        if not m[k + rs] then rectfill(sx + t, sy + z - 1, l, 1, col) end
        if not m[k - 1] or x == 0 then rectfill(sx, sy + t, 1, l, col) end
        if not m[k + 1] or x == rs - 1 then rectfill(sx + z - 1, sy + t, 1, l, col) end
      end
    end
  elseif dp.sel then
    frame_box(dp.sel[1], dp.sel[2], dp.sel[3], dp.sel[4], (frame // 8) % 2 == 0 and 0xFFFFFF or C_ACC)
  end
  if dp.float then
    local f = dp.float
    for j = 0, f.b.h - 1 do
      for i = 0, f.b.w - 1 do
        local c = f.b.px[j * f.b.w + i + 1]
        if c and inside(f.x + i, f.y + j) then
          rectfill(CANVAS_X + (f.x + i) * z, CANVAS_Y + (f.y + j) * z, z, z, c)
        end
      end
    end
    frame_box(f.x, f.y, f.x + f.b.w - 1, f.y + f.b.h - 1, C_ACC)
  end
  if anim.boxes then
    local zn, _, _, _, _, _, _, zf = current_zone()
    if zn then
      local bxs = get_frame_boxes(zn, zf)
      for _, b in ipairs(bxs) do
        local col = box_colour(b.kind)
        rect(CANVAS_X + b.x * z, CANVAS_Y + b.y * z, b.w * z, b.h * z, col)
      end
    end
  end
  -- the pointer
  local cx, cy = CANVAS_X + px * z, CANVAS_Y + py * z
  rect(cx - 1, cy - 1, z + 2, z + 2, 0x000000)
  rect(cx, cy, z, z, C_PT)
  if mirror and px ~= rs - 1 - px then rect(CANVAS_X + (rs - 1 - px) * z, cy, z, z, C_DIM) end

  -- the panel: colours, tool, sprite, previews
  local x = PANEL_X
  rectfill(x - 8, 16, W - x + 8, HINT_Y - 16, C_PANEL)
  print("COLOURS " .. #pal(), x, 16, C_DIM)
  local cells, per = #pal() + 1, 16
  local row = ci // per
  local first = clamp(row - 1, 0, max(0, (cells - 1) // per - 3))
  for i = first * per, min(cells - 1, (first + 4) * per - 1) do
    local sx, sy = x + (i % per) * 16, 32 + (i // per - first) * 16
    U.zone(sx, sy, 16, 16, "swatch", i)
    swatch(sx + 1, sy + 1, 14, i > 0 and pal()[i] or nil)
    if i == ci then rect(sx, sy, 16, 16, 0xFFFFFF) end
  end
  if first > 0 then print("^", x + 264, 32, C_DIM) end
  if (first + 4) * per < cells then print("v", x + 264, 80, C_DIM) end
  local c = colour()
  print(tool, x, 112, C_ACC)
  print("colour " .. ci .. " " .. colour_name(c), x + 96, 112, C_TEXT)
  local n = sprite_index()
  local zn, zx, zy, zw, zh, zf, zfps, cur_f = current_zone()
  print(string.format("sprite %d  (%d,%d)  %dx%d", n, rx, ry, rs, rs), x, 128, C_TEXT)
  if zn then
    print(string.format("zone \"%s\" frame %d/%d", zn, cur_f, zf), x, 144, C_ACC)
  else
    print(string.format("spr(%d, x, y, %d, %d)", n, rs // 8, rs // 8), x, 144, C_DIM)
  end
  -- this sprite as the game draws it, and the animation from it
  local pz = rs <= 32 and 2 or (rs == 64 and 1 or 0.5)
  local ps = floor(rs * pz)
  local py0 = 176
  local k = anim.frames > 1 and floor(anim.t * anim.fps) % anim.frames or 0
  local fx, fy = frame_xy(k)
  local ax = x + 160
  if anim.wrap then                   -- tile view: the sprite 3 x 3, so the seams show
    local c = max(1, rs // 8)
    for ty = 0, 2 do
      for tx = 0, 2 do spr(n, x + tx * rs, py0 + ty * rs, c, c) end
    end
  else
    checker(x, py0, ps, ps, 8)
    sspr(rx, ry, rs, rs, x, py0, false, false, pz)
    checker(ax, py0, ps, ps, 8)
    sspr(fx, fy, rs, rs, ax, py0, false, false, pz)
  end
  if anim.boxes and zn then
    for _, b in ipairs(get_frame_boxes(zn, cur_f)) do
      rect(x + floor(b.x * pz), py0 + floor(b.y * pz), max(1, floor(b.w * pz)), max(1, floor(b.h * pz)), box_colour(b.kind))
    end
    for _, b in ipairs(get_frame_boxes(zn, k + 1)) do
      rect(ax + floor(b.x * pz), py0 + floor(b.y * pz), max(1, floor(b.w * pz)), max(1, floor(b.h * pz)), box_colour(b.kind))
    end
  end
  if anim.wrap then print("tile view 3x3 (q)", x, 160, C_DIM)
  else
    print("x" .. (pz < 1 and "1/2" or pz), x, 160, C_DIM)
    print("anim " .. (k + 1) .. "/" .. anim.frames, ax, 160, C_DIM)
  end
  print(string.format("%d fps%s%s%s%s", anim.fps, anim.play and "" or " ||", anim.onion and "  onion" or "",
                      anim.boxes and "  boxes" or "", mirror and "  mirror" or ""), x, 304, C_DIM)
  if dp.float then hint({ { { "up", "down", "left", "right" }, "move" }, { { "space" }, "put it down" }, { { "esc" }, "drop it" } })
  elseif dp.sel then
    hint({ { { "ctrl", "c" }, "copy" }, { { "ctrl", "x" }, "cut" }, { { "enter" }, "lift" }, { { "h", "v" }, "flip" },
           { { "r" }, "turn" }, { { "del" }, "clear" }, { { "esc" }, "none" } })
  elseif dp.anchor then hint({ { { "up", "down", "left", "right" }, "the other corner" }, { { "space" }, "done" }, { { "esc" }, "cancel" } })
  else
    hint({ { { "up", "down", "left", "right" }, "point" }, { { "space" }, "draw" }, { { "b", "e", "g", "l", "u", "o", "m", "w" }, "tools" },
           { { ",", "." }, "colour" }, { { "z" }, "size" }, { { "j" }, "boxes" }, { { "q" }, "tile" }, { { "tab" }, "more" } })
  end
end
end

----------------------------------------------------------------- sheet page
do

shp = { zoom = 1, vx = 0, vy = 0 }
local VIEW_X, VIEW_Y, VIEW_W, VIEW_H = 8, 32, 624, 272

function sheet_reset()
  shp.zoom = ZOOMS[1]                                -- a big sheet: as far as it goes
  for _, z in ipairs(ZOOMS) do if sw * z <= VIEW_W and sh * z <= VIEW_H then shp.zoom = z end end
end

local function follow()
  local vw, vh = VIEW_W / shp.zoom, VIEW_H / shp.zoom
  if rx < shp.vx then shp.vx = rx end
  if ry < shp.vy then shp.vy = ry end
  if rx + rs > shp.vx + vw then shp.vx = rx + rs - vw end
  if ry + rs > shp.vy + vh then shp.vy = ry + rs - vh end
  shp.vx = floor(clamp(shp.vx, 0, max(0, sw - vw)) / 8) * 8
  shp.vy = floor(clamp(shp.vy, 0, max(0, sh - vh)) / 8) * 8
end

local function resize()
  ask("sheet size (width x height, multiples of 8, up to 4096)", sw .. "x" .. sh, function(t)
    local w, h = t:match("^%s*(%d+)%s*[xX*, ]%s*(%d+)%s*$")
    w, h = tonumber(w), tonumber(h)
    if not w or w < 8 or h < 8 or w > 4096 or h > 4096 or w % 8 ~= 0 or h % 8 ~= 0 then
      say("a size like 256x256, multiples of 8", C_ERR)
      return
    end
    local ok, e = pcall(cart_sheet, w, h)
    if not ok then say(tostring(e), C_ERR); return end
    sw, sh = cart_sheet()
    undo, redo = {}, {}
    dirty = true
    fit_region()
    sheet_reset()
    draw_reset()
    local bad = stack_resized and stack_resized() or 0
    if bad > 0 then
      say("the sheet is " .. sw .. "x" .. sh .. ": " .. bad .. " stack zone(s) are now outside it (Stack page: fix them)", C_ERR, 400)
    else
      say("the sheet is " .. sw .. "x" .. sh .. " (Ctrl+S saves)", C_ACC)
    end
  end)
end
shp.resize = resize

local function paste_here()
  if not clip_b then say("nothing copied yet (Ctrl+C)", C_ERR); return end
  local w, h = min(clip_b.w, sw - rx), min(clip_b.h, sh - ry)
  begin(rx, ry, w, h)
  for j = 0, h - 1 do
    for i = 0, w - 1 do sset(rx + i, ry + j, clip_b.px[j * clip_b.w + i + 1] or nil) end
  end
  edits = edits + 1
  say("pasted " .. w .. "x" .. h .. " at (" .. rx .. "," .. ry .. ")", C_DIM, 90)
end

function sheet_actions()
  choose("sheet " .. sw .. "x" .. sh, {
    { "Draw this sprite (Enter)", function() go("draw") end },
    { "Copy the sprite (Ctrl+C)", function() sheet_key("^c") end },
    { "Paste here (Ctrl+V)", function() sheet_key("^v") end },
    { "Clear the sprite (Del)", function() sheet_key("del") end },
    { "Sprite size (z)", function() sheet_key("z") end },
    { "Zoom in (+)", function() sheet_key("+") end },
    { "Zoom out (-)", function() sheet_key("-") end },
    { "Sheet size... (R)", resize },
  })
end

function sheet_key(k)
  if k == "left" then rx = max(0, rx - rs)
  elseif k == "right" then rx = min(sw - rs, rx + rs)
  elseif k == "up" then ry = max(0, ry - rs)
  elseif k == "down" then ry = min(sh - rs, ry + rs)
  elseif k == "pgup" then ry = max(0, ry - rs * 8)
  elseif k == "pgdn" then ry = min(sh - rs, ry + rs * 8)
  elseif k == "home" then rx, ry = 0, 0
  elseif k == "\n" or k == " " then go("draw"); return
  elseif k == "z" then draw_key("z")
  elseif k == "j" then anim.boxes = not anim.boxes; say(anim.boxes and "hitboxes on (BOXES)" or "hitboxes off", C_DIM, 90)
  elseif k == "+" or k == "=" then
    for _, z in ipairs(ZOOMS) do if z > shp.zoom then shp.zoom = z; break end end
  elseif k == "-" then
    for i = #ZOOMS, 1, -1 do if ZOOMS[i] < shp.zoom then shp.zoom = ZOOMS[i]; break end end
  elseif k == "^c" then
    clip_b = read_block(0, 0, rs, rs)
    say("copied sprite " .. sprite_index(), C_DIM, 60)
  elseif k == "^v" then paste_here()
  elseif k == "del" or k == "\b" then
    begin()
    for j = 0, rs - 1 do for i = 0, rs - 1 do sset(rx + i, ry + j, nil) end end
    edits = edits + 1
    say("cleared sprite " .. sprite_index(), C_DIM, 60)
  elseif k == "R" then resize()
  end
  draw_reset()
end

function sheet_pad()
  if btn(6) then
    if rp[2] then sheet_key("+") end
    if rp[3] then sheet_key("-") end
    return
  end
  if rp[0] then sheet_key("left") end
  if rp[1] then sheet_key("right") end
  if rp[2] then sheet_key("up") end
  if rp[3] then sheet_key("down") end
  if btnp(4) then sheet_key("\n") end
  if tap[6] then sheet_actions() end
end

-- the mouse: a click chooses the sprite under it, a double click draws it,
-- the wheel zooms
function sheet_mouse()
  local z = U.at()
  if not z or z.kind ~= "sheetview" then return end
  if U.wheel ~= 0 then sheet_key(U.wheel > 0 and "+" or "-") end
  if U.click(0) then
    local x = clamp(floor(shp.vx + (U.x - VIEW_X) / z.a), 0, sw - 1)
    local y = clamp(floor(shp.vy + (U.y - VIEW_Y) / z.a), 0, sh - 1)
    local nx, ny = x // rs * rs, y // rs * rs
    if U.double and nx == rx and ny == ry then sheet_key("\n"); return end
    rx, ry = nx, ny
    sheet_key("none")                    -- (it follows the sprite and resets the drawing page)
  end
end

function draw_sheet()
  cls(C_BG)
  follow()
  local z = shp.zoom
  local vw, vh = min(sw - shp.vx, floor(VIEW_W / z)), min(sh - shp.vy, floor(VIEW_H / z))
  checker(VIEW_X, VIEW_Y, floor(vw * z), floor(vh * z), 16)
  U.zone(VIEW_X, VIEW_Y, floor(vw * z), floor(vh * z), "sheetview", z)
  sspr(shp.vx, shp.vy, vw, vh, VIEW_X, VIEW_Y, false, false, z)
  -- the sprite chosen
  local x, y = VIEW_X + (rx - shp.vx) * z, VIEW_Y + (ry - shp.vy) * z
  rect(x - 1, y - 1, rs * z + 2, rs * z + 2, 0x000000)
  rect(x, y, rs * z, rs * z, (frame // 10) % 2 == 0 and C_PT or 0xFFFFFF)
  if anim.frames > 1 then
    for k = 1, anim.frames - 1 do
      local per = sw // rs
      local n = ((ry // rs) * per + rx // rs + k) % (per * (sh // rs))
      local fx, fy = (n % per) * rs, (n // per) * rs
      rect(VIEW_X + (fx - shp.vx) * z, VIEW_Y + (fy - shp.vy) * z, rs * z, rs * z, C_DIM)
    end
  end
  if anim.boxes and zones and zboxes then
    for _, zn in ipairs(zones()) do
      local zx, zy, zw, zh, zf = zone(zn)
      if zx then
        for f = 1, zf do
          local fx, fy = zx + (f - 1) * zw, zy
          local sx = VIEW_X + (fx - shp.vx) * z
          local sy = VIEW_Y + (fy - shp.vy) * z
          if sx + zw * z >= VIEW_X and sx <= VIEW_X + VIEW_W and sy + zh * z >= VIEW_Y and sy <= VIEW_Y + VIEW_H then
            for _, b in ipairs(get_frame_boxes(zn, f)) do
              local bx = VIEW_X + (fx + b.x - shp.vx) * z
              local by = VIEW_Y + (fy + b.y - shp.vy) * z
              rect(bx, by, max(1, floor(b.w * z)), max(1, floor(b.h * z)), box_colour(b.kind))
            end
          end
        end
      end
    end
  end
  rectfill(0, 16, W, 16, C_BG)
  print(string.format("sheet %dx%d  zoom %s  sprite %d at (%d,%d) %dx%d", sw, sh, z < 1 and ("1/" .. floor(1 / z)) or z,
                      sprite_index(), rx, ry, rs, rs), 8, 16, C_TEXT)
  rectfill(0, 304, W, 16, C_BG)
  print(string.format("view (%d,%d)  %d frames from here%s  spr(%d, x, y, %d, %d)", shp.vx, shp.vy, anim.frames,
                      anim.boxes and "  boxes" or "", sprite_index(), rs // 8, rs // 8), 8, 304, C_DIM)
  hint({ { { "up", "down", "left", "right" }, "sprite" }, { { "enter" }, "draw it" }, { { "z" }, "size" }, { { "+", "-" }, "zoom" },
         { { "j" }, "boxes" }, { { "ctrl", "c" }, "copy" }, { { "ctrl", "v" }, "paste" }, { { "shift", "r" }, "sheet size" } })
end
end

----------------------------------------------------------------- palette page
do

pp = { sel = 1, edit = nil }
local GX, GY, CELL = 16, 48, 16

function pal_reset()
  pp.sel = clamp(ci > 0 and ci or 1, 1, max(1, #pal()))
  pp.edit = nil
end

local function set_pal(list, what)
  proj.palette = {}
  for i, c in ipairs(list) do proj.palette[i] = c end
  ci = clamp(ci, 0, #proj.palette)
  pp.sel = clamp(pp.sel, 1, max(1, #proj.palette))
  dirty = true
  say(what .. ": " .. #proj.palette .. " colours", C_ACC, 90)
end

-- the colours the sheet uses (at most 256; a big sheet is sampled)
local function from_sheet()
  local count, order = {}, {}
  local step = max(1, floor(sqrt(sw * sh / 262144)))       -- at most 512x512 looked at
  for y = 0, sh - 1, step do
    for x = 0, sw - 1, step do
      local c = sget(x, y)
      if c then
        if not count[c] then count[c] = 0; order[#order + 1] = c end
        count[c] = count[c] + 1
      end
    end
  end
  table.sort(order, function(a, b) return count[a] > count[b] end)
  local list = {}
  for i = 1, min(256, #order) do list[i] = order[i] end
  table.sort(list, function(a, b) return hue(a) < hue(b) end)
  if #list == 0 then say("the sheet has no colours yet", C_ERR); return end
  set_pal(list, "the colours of the sheet" .. (#order > 256 and " (the 256 used most)" or ""))
end

-- the pixels of the drawing colour, in the sprite or the sheet, become the chosen one
local function replace(all)
  local from, to = colour(), pal()[pp.sel]
  if not to then return end
  if all and sw * sh > 512 * 512 then              -- a frame has room for about that much Lua
    say("the sheet is too big for that (over 512x512): x does it in the sprite", C_ERR)
    return
  end
  local fv = from and q565(from) or nil
  local x0, y0, x1, y1 = rx, ry, rx + rs - 1, ry + rs - 1
  if all then x0, y0, x1, y1 = 0, 0, sw - 1, sh - 1 end
  begin(x0, y0, x1 - x0 + 1, y1 - y0 + 1)
  local n = 0
  for y = y0, y1 do
    for x = x0, x1 do
      if sget(x, y) == fv then sset(x, y, to); n = n + 1 end
    end
  end
  edits = edits + 1
  say(n .. " pixels: " .. colour_name(from) .. " -> " .. colour_name(to), C_ACC, 120)
end

function pal_actions()
  choose("palette: " .. #pal() .. " colours", {
    { "Draw with it (Enter)", function() pal_key("\n") end },
    { "Edit it (e)", function() pal_key("e") end },
    { "Add a colour (a)", function() pal_key("a") end },
    { "Remove it (Del)", function() pal_key("del") end },
    { "Sort by hue (s)", function() pal_key("s") end },
    { "The colours of the sheet (f)", function() pal_key("f") end },
    { "The SDK's palette (1)", function() pal_key("1") end },
    { "bm Studio's palette (2)", function() pal_key("2") end },
    { "Replace in the sprite (x)", function() pal_key("x") end },
    { "Replace in the whole sheet (X)", function() pal_key("X") end },
  })
end

function pal_key(k)
  local p = pal()
  if pp.edit then                       -- R G B: up/down which, left/right 8, < > 1
    local c = p[pp.sel]
    local sh_ = ({ 16, 8, 0 })[pp.edit]
    local v = c >> sh_ & 255
    if k == "up" then pp.edit = (pp.edit - 2) % 3 + 1
    elseif k == "down" then pp.edit = pp.edit % 3 + 1
    elseif k == "left" or k == "right" or k == "<" or k == ">" or k == "," or k == "." then
      local d = (k == "left" and -8) or (k == "right" and 8) or ((k == "<" or k == ",") and -1) or 1
      v = clamp(v + d, 0, 255)
      p[pp.sel] = c & ~(255 << sh_) | v << sh_
      dirty = true
    elseif k == "\n" or k == "esc" or k == "e" then pp.edit = nil end
    return
  end
  local n = #p
  if k == "left" then pp.sel = max(1, pp.sel - 1)
  elseif k == "right" then pp.sel = min(n, pp.sel + 1)
  elseif k == "up" then pp.sel = max(1, pp.sel - 16)
  elseif k == "down" then pp.sel = min(n, pp.sel + 16)
  elseif k == "\n" or k == " " then
    if n > 0 then ci_prev, ci = ci, pp.sel end
    go("draw")
  elseif k == "e" then if n > 0 then pp.edit = 1 end
  elseif k == "a" then
    if n >= 256 then say("at most 256 colours", C_ERR); return end
    table.insert(p, pp.sel + 1, p[pp.sel] or 0x808080)
    pp.sel = pp.sel + 1
    pp.edit = 1
    dirty = true
    say("a new colour: up/down R G B, left/right change it, Enter done", C_DIM, 200)
  elseif k == "del" or k == "\b" then
    if n <= 1 then say("the palette keeps one colour at least", C_ERR); return end
    table.remove(p, pp.sel)
    if ci > #p then ci = #p end
    pp.sel = clamp(pp.sel, 1, #p)
    dirty = true
  elseif k == "[" or k == "]" then
    local j = pp.sel + (k == "]" and 1 or -1)
    if j >= 1 and j <= n then p[pp.sel], p[j] = p[j], p[pp.sel]; pp.sel = j; dirty = true end
  elseif k == "s" then
    table.sort(p, function(a, b) return hue(a) < hue(b) end)
    dirty = true
    say("sorted by hue", C_DIM, 60)
  elseif k == "f" then from_sheet()
  elseif k == "1" then set_pal(PAL_SDK, "the SDK's palette")
  elseif k == "2" then set_pal(PAL_STUDIO, "bm Studio's palette")
  elseif k == "x" then replace(false)
  elseif k == "X" then replace(true)
  end
end

function pal_pad()
  if rp[0] then pal_key("left") end
  if rp[1] then pal_key("right") end
  if rp[2] then pal_key("up") end
  if rp[3] then pal_key("down") end
  if btnp(4) then pal_key("\n") end
  if btnp(5) and pp.edit then pp.edit = nil end
  if tap[6] then pal_actions() end
end

-- the mouse: a click chooses a colour, a double click draws with it; on
-- R, G or B a click edits that one and dragging left or right changes it
-- (as the arrows)
function pal_mouse()
  local z = U.at()
  if U.wheel ~= 0 then                    -- the wheel: on R, G or B the value (as left/right), else the row
    if z and z.kind == "channel" then
      if not pp.edit then pal_key("e") end
      pp.edit = z.a
      for _ = 1, math.abs(U.wheel) do pal_key(U.wheel > 0 and "right" or "left") end
    elseif not pp.edit then
      for _ = 1, math.abs(U.wheel) do pal_key(U.wheel > 0 and "up" or "down") end
    end
    return
  end
  if pp.edit and U.drag_arrows(4) then return end
  if not z then return end
  if z.kind == "pcol" and U.pressed(0) then
    if pp.edit then pal_key("esc") end
    if U.double and z.a == pp.sel then pal_key("\n") else pp.sel = z.a end
  elseif z.kind == "channel" and U.pressed(0) then
    if not pp.edit then pal_key("e") end
    pp.edit = z.a
  end
end

function draw_palette()
  cls(C_BG)
  local p = pal()
  print("PALETTE " .. #p .. " colours (at most 256: saved with the sheet)", 16, 16, C_DIM)
  for i, c in ipairs(p) do
    local x, y = GX + ((i - 1) % 16) * CELL, GY + ((i - 1) // 16) * CELL
    U.zone(x, y, CELL, CELL, "pcol", i)
    rectfill(x + 1, y + 1, CELL - 2, CELL - 2, c)
    if i == ci then rectfill(x + 6, y + 6, 4, 4, (c >> 8 & 255) > 128 and 0x000000 or 0xFFFFFF) end
    if i == pp.sel then rect(x, y, CELL, CELL, (frame // 10) % 2 == 0 and 0xFFFFFF or C_ACC) end
  end
  local x = 304
  local c = p[pp.sel]
  if c then
    rectfill(x, 48, 64, 64, c)
    print("colour " .. pp.sel, x + 80, 48, C_ACC)
    print(string.format("#%06X", c), x + 80, 64, C_TEXT)
    print(string.format("on the console #%06X", q565(c)), x + 80, 80, C_DIM)
    local names = { "R", "G", "B" }
    for k = 1, 3 do
      local v = c >> ({ 16, 8, 0 })[k] & 255
      local y = 128 + (k - 1) * 16
      U.zone(x - 4, y, 320, 16, "channel", k)
      if pp.edit == k then rectfill(x - 4, y, 320, 16, C_SEL) end
      print(names[k] .. string.format(" %3d", v), x, y, C_TEXT)
      rectfill(x + 64, y + 4, 200, 8, C_PANEL)
      rectfill(x + 64, y + 4, floor(v * 200 / 255), 8, ({ 0xE04040, 0x40C040, 0x4070E0 })[k])
    end
  end
  print("drawing with " .. ci .. " " .. colour_name(colour()), x, 192, C_TEXT)
  chip_hint("[", nil, "move", chip_hint("del", nil, "remove", chip_hint("a", nil, "add", chip_hint("e", nil, "edit", x, 224), 224), 224), 224)
  chip_hint("f", nil, "from the sheet", chip_hint("s", nil, "sort", x, 240), 240)
  chip_hint("2", nil, "Studio palette", chip_hint("1", nil, "SDK palette", x, 256), 256)
  chip_hint("x", nil, "drawing colour -> this: sprite", x, 272)
  print("drawing colour -> this: sheet", snap(prompt("x", prompt("shift", x, 288) + 1, 288) + 3), 288, C_DIM)
  if pp.edit then
    hint({ { { "up", "down" }, "R G B" }, { { "left", "right" }, "8" }, { { "<", ">" }, "1" }, { { "enter" }, "done" } })
  else
    hint({ { { "up", "down", "left", "right" }, "choose" }, { { "enter" }, "draw with it" }, { { "e" }, "edit" }, { { "a" }, "add" },
           { { "del" }, "remove" }, { { "s" }, "sort" }, { { "f" }, "from the sheet" } })
  end
end
end

----------------------------------------------------------------- stack page
-- Sprite stacking: a stack zone ("stack name x y w h n" in the SPRITES
-- section) is n slices of w x h that lie side by side on the sheet. The
-- page shows one slice, the stack turning (stack()) and the zones; Enter
-- draws the slice on F1. A zone has one of two layouts (z.vert): slices from
-- left to right (stack) or from top to bottom (stackv, bit 0 of byte 27 of the
-- zone in SPRITES). Keys: k onion skin, c copy the slice to the next one,
-- Del delete the zone. State lives in sp (the main chunk has 200 locals at most).
do

sp = { zi = 1, k = 1, rot = 0, spin = true, zdirty = false, onion = false, ghost = nil }
local PREV_X, PREV_Y, PREV_W, PREV_H = 312, 144, 320, 160
local TWO_PI = 2 * math.pi

-- the stack zones of the file (SPRITES with 2 or more slices)
local zcache                              -- the list below, rebuilt only when the zones change
local function stack_zones()
  if zcache then return zcache end
  local list = {}
  if not zones or not zone then return list end
  local vert = {}                         -- the layout is bit 0 of byte 27 of a zone in SPRITES
  local cur = cart_data and cart_data(11)
  if cur and #cur >= 4 then
    for i = 0, sunpack("<I2", cur, 1) - 1 do
      local o = 5 + i * 28
      if #cur < o + 27 then break end
      vert[cur:sub(o, o + 15):match("^[^\0]*")] = cur:byte(o + 27) & 1 == 1
    end
  end
  for _, zn in ipairs(zones() or {}) do
    local x, y, w, h, fr, fps, n = zone(zn)
    if x and n and n >= 2 then
      list[#list + 1] = { name = zn, x = x, y = y, w = w, h = h, frames = fr, fps = fps, n = n, vert = vert[zn] or false }
    end
  end
  zcache = list
  return list
end

-- the corner of slice k (1 is the bottom one)
local function cell(z, k)
  if z.vert then return z.x, z.y + (k - 1) * z.h end
  return z.x + (k - 1) * z.w, z.y
end

local function fits(x, y, w, h, n, vert)
  if vert then return x + w <= sw and y + h * n <= sh end
  return x + w * n <= sw and y + h <= sh
end

local function current()
  local list = stack_zones()
  sp.zi = clamp(sp.zi, 1, max(1, #list))
  local z = list[sp.zi]
  if z then sp.k = clamp(sp.k, 1, z.n) end
  return z, list
end

-- the stack zone and slice that the F1 sprite is (for the status line)
function stack_at()
  for _, z in ipairs(stack_zones()) do
    if z.w == z.h and rs == z.w then
      for k = 1, z.n do
        local cx, cy = cell(z, k)
        if cx == rx and cy == ry then return z.name, k, z.n end
      end
    end
  end
  return nil
end

function stack_reset()
  sp.zi, sp.k, sp.zdirty, sp.ghost = 1, 1, false, nil
  zcache = nil
end

-- the sheet got another size: the zones are checked again; the file is
-- written with them, and the count of the ones that no longer fit is returned
function stack_resized()
  zcache = nil
  local bad = 0
  for _, z in ipairs(stack_zones()) do
    if not fits(z.x, z.y, z.w, z.h, z.n, z.vert) then bad = bad + 1 end
  end
  if cart_data and cart_data(11) then sp.zdirty = true end
  return bad
end

function stack_enter()
  sp.ghost = nil                          -- the sheet may have changed on the other pages
  current()
end

-- one zone into the SPRITES section (a new one at the end, or in place of
-- the one of that name); the file is written by the next save
local function put_zone(z)
  if not cart_data then return false, "this kernel has no cart_data" end
  local cur = cart_data(11)
  local parts, done, count = {}, false, 0
  local function add(name, x, y, w, h, fr, fps, sl, vt)
    parts[#parts + 1] = spack("<c16I2I2I2I2BBBB", name, x, y, w, h, fr, fps, sl, vt and 1 or 0)
    count = count + 1
  end
  if cur and #cur >= 4 then
    for i = 0, sunpack("<I2", cur, 1) - 1 do
      local name, x, y, w, h, fr, fps, sl, vt = sunpack("<c16I2I2I2I2BBBB", cur, 5 + i * 28)
      name = name:match("^[^\0]*")
      if name == z.name then
        if not z.delete then add(z.name, z.x, z.y, z.w, z.h, fr, fps, z.n, z.vert) end
        done = true
      else
        add(name, x, y, w, h, fr, fps, sl, vt & 1 == 1)
      end
    end
  end
  if not done and not z.delete then add(z.name, z.x, z.y, z.w, z.h, 1, 0, z.n, z.vert) end
  local ok, e = cart_data(11, count > 0 and spack("<I2I2", count, 0) .. table.concat(parts) or "")
  if not ok then return false, tostring(e) end
  sp.zdirty, dirty = true, true
  zcache, sp.ghost = nil, nil
  return true
end

local function find_zone(name)
  for _, zn in ipairs(zones and zones() or {}) do
    if zn == name then
      local x, y, w, h, fr, fps, n = zone(zn)
      return { x = x, y = y, w = w, h = h, n = n or 0 }
    end
  end
end

local function define_zone(t)
  local name, x, y, w, h, n, lay = t:match("^%s*(%S+)%s+(%d+)%s+(%d+)%s+(%d+)%s+(%d+)%s+(%d+)%s*(%a?)%s*$")
  if not name or (lay ~= "" and lay ~= "h" and lay ~= "v") then
    say("name x y w h n [h or v], like: tree 0 0 16 16 8 v", C_ERR, 300); return
  end
  x, y, w, h, n = tonumber(x), tonumber(y), tonumber(w), tonumber(h), tonumber(n)
  if #name > 15 then say("the name has at most 15 letters", C_ERR); return end
  if w < 1 or h < 1 or n < 2 or n > 255 then say("w and h at least 1, n (slices) from 2 to 255", C_ERR); return end
  local vert = lay == "v"
  if not fits(x, y, w, h, n, vert) then say("out of the sheet (" .. sw .. "x" .. sh .. ")", C_ERR); return end
  local old = find_zone(name)
  if old and old.n < 2 then say("the zone " .. name .. " is not a stack: use another name", C_ERR, 300); return end
  local z = { name = name, x = x, y = y, w = w, h = h, n = n, vert = vert }
  local ok, e = put_zone(z)
  if not ok then say("the zone: " .. e, C_ERR, 300); return end
  for i, s in ipairs(stack_zones()) do if s.name == name then sp.zi = i end end
  sp.k = 1
  say(string.format("stack %s: %d slices of %dx%d, %s (Ctrl+S saves)", name, n, w, h, vert and "stackv" or "stack"), C_ACC)
end

local function new_zone()
  local k, name = 1, "stack"
  while find_zone(name) do k = k + 1; name = "stack" .. k end
  ask("new stack zone: name x y w h n h|v", string.format("%s %d %d %d %d 8 h", name, rx, ry, rs, rs), define_zone)
end

local function change_zone()
  local z = current()
  if not z then new_zone(); return end
  ask("stack zone: name x y w h n h|v", string.format("%s %d %d %d %d %d %s", z.name, z.x, z.y, z.w, z.h, z.n, z.vert and "v" or "h"),
      define_zone)
end

local function edit_slice()
  local z = current()
  if not z then say("no stack zone yet: n makes one", C_ERR); return end
  local cx, cy = cell(z, sp.k)
  if z.w ~= z.h then say("F1 draws square sprites: this zone is " .. z.w .. "x" .. z.h, C_ERR, 300); return end
  if z.w > CANVAS or cx + z.w > sw or cy + z.h > sh then say("this slice does not fit F1 or the sheet", C_ERR); return end
  rs, rx, ry = z.w, cx, cy
  draw_reset()
  go("draw")
  say(string.format("%s slice %d/%d: F4 goes back to the stack", z.name, sp.k, z.n), C_ACC, 300)
end

-- copy slice k onto slice k+1 (extrusion: draw the next level from the last), then go to it;
-- one undo step
local function copy_slice()
  local z = current()
  if not z then say("no stack zone yet: n makes one", C_ERR); return end
  if sp.k >= z.n then say("this is the last slice: nothing after it", C_ERR, 200); return end
  if not fits(z.x, z.y, z.w, z.h, z.n, z.vert) then say("the zone is outside the sheet", C_ERR); return end
  local ax, ay = cell(z, sp.k)
  local bx, by = cell(z, sp.k + 1)
  begin(bx, by, z.w, z.h)
  for j = 0, z.h - 1 do
    for i = 0, z.w - 1 do sset(bx + i, by + j, sget(ax + i, ay + j)) end
  end
  sp.k = sp.k + 1
  sp.ghost = nil
  say(string.format("%s: slice %d copied to %d (Ctrl+Z undoes)", z.name, sp.k - 1, sp.k), C_ACC, 200)
end

-- take the zone out of SPRITES; the pixels of its slices stay on the sheet
local function delete_zone()
  local z = current()
  if not z then say("no stack zone to delete", C_ERR); return end
  if not confirmed("delzone", "Del again deletes the zone " .. z.name .. " (its pixels stay)") then return end
  local ok, e = put_zone({ name = z.name, delete = true })
  if not ok then say("the zone: " .. e, C_ERR, 300); return end
  sp.zi, sp.k = max(1, sp.zi - 1), 1
  say("zone " .. z.name .. " deleted (Ctrl+S saves)", C_ACC, 200)
end

local function slice_by(d)
  local z = current()
  if z then sp.k = (sp.k - 1 + d) % z.n + 1 end
end

local function zone_by(d)
  local z, list = current()
  if z then sp.zi = (sp.zi - 1 + d) % #list + 1; sp.k = 1 end
end

local function turn_by(d)
  sp.spin = false
  sp.rot = (sp.rot + d * TWO_PI / 24) % TWO_PI
end

function stack_actions()
  choose("stack", {
    { "Draw this slice (Enter)", function() stack_key("\n") end },
    { "Next slice (right)", function() stack_key("right") end },
    { "Previous slice (left)", function() stack_key("left") end },
    { "Next zone (down)", function() stack_key("down") end },
    { "Previous zone (up)", function() stack_key("up") end },
    { "Onion skin (k)", function() stack_key("k") end },
    { "Copy the slice to the next (c)", copy_slice },
    { "New zone (n)", new_zone },
    { "Change the zone (e)", change_zone },
    { "Delete the zone (Del)", delete_zone },
    { "Turn or stop the stack (p)", function() stack_key("p") end },
    { "Turn the stack by hand (. and ,)", function() stack_key(".") end },
  })
end

function stack_key(k)
  if k == "left" then slice_by(-1)
  elseif k == "right" then slice_by(1)
  elseif k == "up" then zone_by(-1)
  elseif k == "down" then zone_by(1)
  elseif k == "pgup" then slice_by(-8)
  elseif k == "pgdn" then slice_by(8)
  elseif k == "home" then sp.k = 1
  elseif k == "end" then local z = current(); if z then sp.k = z.n end
  elseif k == "\n" or k == " " then edit_slice()
  elseif k == "n" then new_zone()
  elseif k == "e" then change_zone()
  elseif k == "k" then sp.onion = not sp.onion; sp.ghost = nil
  elseif k == "c" then copy_slice()
  elseif k == "del" or k == "x" then delete_zone()
  elseif k == "p" then sp.spin = not sp.spin
  elseif k == "," then turn_by(-1)
  elseif k == "." then turn_by(1)
  end
end

function stack_pad()
  if btn(6) then                       -- X + left/right: turn the stack by hand
    if rp[0] then turn_by(-1) end
    if rp[1] then turn_by(1) end
    return
  end
  if rp[0] then stack_key("left") end
  if rp[1] then stack_key("right") end
  if rp[2] then stack_key("up") end
  if rp[3] then stack_key("down") end
  if btnp(4) then stack_key("\n") end
  if btnp(5) then stack_key("p") end
  if tap[6] then stack_actions() end
end

function stack_mouse()
  if U.wheel ~= 0 then slice_by(U.wheel > 0 and -1 or 1) end
  local z = U.click(0)
  if z and z.kind == "stackzone" then sp.zi, sp.k = z.a, 1
  elseif z and z.kind == "stackslice" then
    if U.double and sp.k == z.a then stack_key("\n") else sp.k = z.a end
  end
  -- the turning view: a drag turns the stack by hand, a click starts or stops it
  local d = U.drag
  if d and d.b == 0 and U.inside(PREV_X, PREV_Y, PREV_W, PREV_H) then
    sp.spin = false
    sp.rot = (sp.rot + d.dx * 0.03) % TWO_PI
  elseif U.clicked(0) and U.inside(PREV_X, PREV_Y, PREV_W, PREV_H) then
    sp.spin = not sp.spin
  end
end

-- every frame on this page: the stack turns
function stack_update()
  if sp.spin then sp.rot = (sp.rot + 0.03) % TWO_PI end
end

function draw_stack()
  cls(C_BG)
  local z, list = current()
  if not z then
    print("This file has no stack zone.", 8, 32, C_TEXT)
    print("A stack zone is n slices of the same size that", 8, 64, C_DIM)
    print("lie side by side on the sheet (h), or one under the", 8, 80, C_DIM)
    print("other (v). Press n to make one: name x y w h n h|v,", 8, 96, C_DIM)
    print("for example tree 0 0 16 16 8 h (slice 1 is the bottom).", 8, 112, C_DIM)
    hint({ { { "n" }, "new zone" } })
    return
  end
  -- the slice, zoomed
  local m = max(z.w, z.h)
  local zz = CANVAS // m
  if zz < 1 then zz = CANVAS / m end
  local cw, ch = floor(z.w * zz), floor(z.h * zz)
  local cx, cy = cell(z, sp.k)
  checker(CANVAS_X, CANVAS_Y, cw, ch, 8)
  local ok = fits(z.x, z.y, z.w, z.h, z.n, z.vert)
  if ok then
    if sp.onion and sp.k > 1 then
      -- onion skin: the slice before, dimmed and dithered, where this one is empty (the list is
      -- built once per slice, a frame only draws it)
      local g = sp.ghost
      if not g or g.zi ~= sp.zi or g.k ~= sp.k then
        g = { zi = sp.zi, k = sp.k, n = 0, x = {}, y = {}, c = {} }
        local px, py = cell(z, sp.k - 1)
        for j = 0, z.h - 1 do
          for i = 0, z.w - 1 do
            if (i + j) % 2 == 0 and g.n < 600 and not sget(cx + i, cy + j) then
              local c = sget(px + i, py + j)
              if c then
                g.n = g.n + 1
                g.x[g.n], g.y[g.n] = i, j
                g.c[g.n] = ((c & 0xFEFEFE) >> 1) + ((C_BG & 0xFEFEFE) >> 1)
              end
            end
          end
        end
        sp.ghost = g
      end
      local gs = max(1, floor(zz))
      for i = 1, g.n do rectfill(CANVAS_X + floor(g.x[i] * zz), CANVAS_Y + floor(g.y[i] * zz), gs, gs, g.c[i]) end
    end
    sspr(cx, cy, z.w, z.h, CANVAS_X, CANVAS_Y, false, false, zz)
  end
  rect(CANVAS_X - 1, CANVAS_Y - 1, cw + 2, ch + 2, C_GRID8)
  -- the zones
  local top = clamp(sp.zi - 2, 0, max(0, #list - 4))
  for i = 1, min(#list, 4) do
    local zi = i + top
    local s = list[zi]
    local y = 32 + (i - 1) * 16
    U.zone(PANEL_X, y, 320, 16, "stackzone", zi)
    if zi == sp.zi then rectfill(PANEL_X, y, 320, 16, C_SEL) end
    print(string.format("%s  %dx%d  %d slices", s.name, s.w, s.h, s.n):sub(1, 38), PANEL_X + 4, y,
          zi == sp.zi and 0xFFFFFF or C_TEXT)
  end
  -- the slices: a row of thumbnails around the chosen one
  local first = clamp(sp.k - 4, 1, max(1, z.n - 9))
  local tz = min(2, 24 / m)
  for i = 0, min(10, z.n) - 1 do
    local k = first + i
    local x = PANEL_X + i * 28
    local tx, ty = cell(z, k)
    U.zone(x, 104, 28, 28, "stackslice", k)
    rectfill(x, 104, 26, 26, C_PANEL)
    if ok then
      sspr(tx, ty, z.w, z.h, x + 1, 105, false, false, tz)
    end
    if k == sp.k then rect(x, 104, 26, 26, (frame // 10) % 2 == 0 and C_PT or 0xFFFFFF) end
  end
  -- the stack, turning
  U.zone(PREV_X, PREV_Y, PREV_W, PREV_H, "stackview")
  rectfill(PREV_X, PREV_Y, PREV_W, PREV_H, C_PANEL)
  rect(PREV_X, PREV_Y, PREV_W, PREV_H, C_GRID8)
  local s = clamp(min(105 // (z.n + z.h), 150 // max(1, floor(z.w * 1.5))), 1, 4)
  if ok then
    (z.vert and stackv or stack)(z.x, z.y, z.w, z.h, z.n, PREV_X + PREV_W // 2, PREV_Y + PREV_H - 28 - z.h * s // 2, sp.rot, s)
  end
  -- text over what the stack may have covered
  rectfill(0, 16, W, 16, C_BG)
  print(string.format("%s %s  slice %d/%d  at (%d,%d)  %dx%d  zstack(\"%s\", x, y, rz)", z.vert and "stackv" or "stack", z.name,
                      sp.k, z.n, cx, cy, z.w, z.h, z.name):sub(1, 79), 8, 16, C_TEXT)
  rectfill(PANEL_X, 304, W - PANEL_X, 16, C_BG)
  print(string.format("turn %d deg%s  x%d", floor(sp.rot * 180 / math.pi), sp.spin and " (turning)" or "", s), PREV_X, 304, C_DIM)
  rectfill(0, HINT_Y, W, 16, C_BG)
  hint({ { { "left", "right" }, "slice" }, { { "up", "down" }, "zone" }, { { "enter" }, "draw it" }, { { "k" }, "onion" },
         { { "c" }, "copy up" }, { { "del" }, "delete" }, { { "n" }, "new" }, { { "e" }, "change" }, { { "p" }, "turn" },
         { { ",", "." }, "by hand" } })
end
end

----------------------------------------------------------------- menu

local msel = 1
local last_page = "draw"

-- the keys while F12 is held, under the system's (keyhelp(), the kernel
-- shows them): keyboard keys in lower case, the pad's buttons in upper case
local HELP = {
  all = {
    { "f1 / f2 / f3 / f4", "draw / sheet / palette / stack" },
    { "tab", "the commands" },
  },
  draw = {
    { "up down left right", "the pointer" },
    { "space", "draw (held: a stroke)" },
    { "b / e / g / i", "pencil, eraser, fill, colour from the sprite" },
    { "l / u / shift u", "line, rectangle (filled)" },
    { "o / shift o", "oval (filled)" },
    { "m", "select a rectangle" },
    { "w", "wand: select the area of one colour" },
    { ", / .", "colour back / on" },
    { "x", "the colour before" },
    { "1 - 9 / 0", "the first ten colours" },
    { "ctrl c / ctrl x / ctrl v", "copy, cut, paste" },
    { "del", "clear (the selection or all)" },
    { "h / v / r", "flip, turn" },
    { "shift w a s d", "shift the sprite" },
    { "enter", "lift the selection to move it" },
    { "y / t / k", "mirror drawing, grid, onion skin" },
    { "+ / -", "frames of the animation" },
    { "< / >", "speed" },
    { "p", "play" },
    { "z", "sprite size" },
    { "q", "tile view: the sprite 3 x 3" },
    { "pgup / pgdn", "the sprite before / after" },
  },
  sheet = {
    { "up down left right", "the sprite" },
    { "pgup / pgdn", "8 rows" },
    { "enter", "draw it" },
    { "z", "sprite size" },
    { "+ / -", "zoom" },
    { "ctrl c / ctrl v", "copy, paste here" },
    { "del", "clear" },
    { "shift r", "the sheet's size (keeps what fits)" },
  },
  stack = {
    { "left / right", "the slice before / after" },
    { "up / down", "the stack zone" },
    { "enter", "draw the slice on F1" },
    { "k", "onion skin (the slice before)" },
    { "c", "copy the slice to the next one" },
    { "del", "delete the zone (twice; pixels stay)" },
    { "n / e", "new zone / change it (name x y w h n h|v)" },
    { "p", "the stack turns / stops" },
    { ", / .", "turn it by hand" },
    { "pgup / pgdn", "8 slices" },
  },
  palette = {
    { "up down left right", "the colour" },
    { "enter", "draw with it" },
    { "e", "edit: R G B" },
    { "a / del", "add, remove" },
    { "[ / ]", "move it" },
    { "s / f", "sort, from the sheet" },
    { "1 / 2", "the SDK's, bm Studio's" },
    { "x / shift x", "replace the drawing colour (sprite / sheet)" },
  },
  pad = {
    { "Y LEFTRIGHT", "page" },
    { "Y B", "menu" },
    { "Y A", "undo" },
    { "Y UPDOWN", "sprite" },
    { "DPAD", "the pointer" },
    { "A", "draw (held)" },
    { "B", "colour from the sprite / cancel" },
    { "X LEFTRIGHT", "colour" },
    { "X UPDOWN", "tool" },
    { "X", "the commands (tap)" },
  },
}

local function pixel_keyhelp(p)
  if not keyhelp then return end                -- a kernel before them
  local list = {}
  local function add(t) for _, e in ipairs(t) do list[#list + 1] = e end end
  add(HELP.all)
  if HELP[p] then list[#list + 1] = p; add(HELP[p])
  else list[#list + 1] = "menu"; add({ { "up / down", "choose" }, { "enter", "select" } }) end
  list[#list + 1] = "pad"
  add(HELP.pad)
  keyhelp(list, "bm Pixel")
end

go = function(p)
  if p == "menu" then if page ~= "menu" then msel = 1 end
  else last_page = p end
  page = p
  if p == "palette" then pal_reset() end
  if p == "stack" then stack_enter() end
  pixel_keyhelp(p)
end

reset_pages = function()
  draw_reset()
  sheet_reset()
  pal_reset()
  stack_reset()
end

local function open_chooser()
  local files = list_files()
  if #files == 0 then say("no .bm files on the SD card", C_ERR); return end
  local rows, sel = {}, 1
  for i, f in ipairs(files) do
    rows[i] = { f, function() if open_file(f) then go(files_seen[f:lower()] and files_seen[f:lower()].page or "draw") end end }
    if proj.path and f:lower() == proj.path:lower() then sel = i end
  end
  choose("open a cartridge", rows, sel)
end

local function save_as()
  ask("file name (8.3, in /bme)", proj.path and short_path(proj.path):match("([^/]+)$") or "sprites.bme", function(t)
    if t == "" then return end
    local to = U.project_path(t)
    save_to(to, proj.path, function() go(last_page) end)
  end)
end

local function save_now()
  if proj.path then save_to(proj.path) else save_as() end
end

local function try_game()
  if not proj.path then say("give it a name first: Esc > Save as", C_ERR); return end
  local function run() remember(); cart_run(proj.path) end
  if dirty then save_to(proj.path, nil, run) else run() end
end

-- opened by the bm SDK on this file: the way back to it (saved first)
local function back_to_sdk()
  if not proj.path then say("give it a name first: Esc > Save as", C_ERR); return end
  local function go_back() remember(); cart_tool("sdk", proj.path) end
  if dirty then save_to(proj.path, nil, go_back) else go_back() end
end

local function open_other()
  if not dirty or confirmed("open", "unsaved changes: choose again to open another file") then open_chooser() end
end

local function new_other()
  if not dirty or confirmed("new", "unsaved changes: choose again for a new sheet") then new_sheet(); go("draw") end
end

local MENU = {
  { "Continue", function() go(last_page) end },
  { "Open...   (Ctrl+O)", open_other },
  { "New sheet   (Ctrl+N)", new_other },
  { "Save   (Ctrl+S)", save_now },
  { "Save as...   (Ctrl+Shift+S)", save_as },
  { "Try the game (F5)", try_game },
  { "Sheet size...", function() go("sheet"); shp.resize() end },
  { "Exit bm Pixel", function() if not dirty or confirmed("exit", "unsaved changes: choose again to exit") then remember(); quit() end end },
}

local function menu_key(k)
  if k == "up" then msel = (msel - 2) % #MENU + 1
  elseif k == "down" then msel = msel % #MENU + 1
  elseif k == "\n" or k == "ok" then MENU[msel][2]()
  elseif k == "esc" or k == "back" then go(last_page) end
end

local function draw_menu()
  cls(C_BG)
  print("bm Pixel", 32, 32, C_ACC)
  print((proj.path or "(a new sheet, not saved yet)") .. (dirty and "  *modified*" or ""), 128, 32, C_DIM)
  for i, it in ipairs(MENU) do
    local y = 64 + (i - 1) * 16
    U.zone(24, y, 272, 16, "menuitem", i)
    if i == msel and not pick and not input then rectfill(24, y, 272, 16, C_SEL) end
    print(it[1], 32, y, C_TEXT)
  end
  local x = 320
  print(proj.title ~= "" and proj.title:sub(1, 38) or "", x, 64, C_TEXT)
  print("sheet " .. sw .. "x" .. sh .. ", " .. #pal() .. " colours", x, 96, C_TEXT)
  print("sprite " .. sprite_index() .. ", " .. rs .. "x" .. rs, x, 112, C_TEXT)
  chip_hint("f3", nil, "palette", chip_hint("f2", nil, "sheet", chip_hint("f1", nil, "draw", x, 144), 144), 144)
  chip_hint("f5", nil, "try the game", x, 160)
  chip_hint("f12", nil, "held: the keys", x, 176)
  print("the sheet as the SDK, bm Studio, bm", x, 208, C_DIM)
  print("Animator and the games read it; the", x, 224, C_DIM)
  print("palette is saved with it (SHEET8)", x, 240, C_DIM)
  hint({ { { "up", "down" }, "choose" }, { { "enter" }, "select" }, { { "esc" }, "back" } })
end

----------------------------------------------------------------- main

local last_t = 0

function _init()
  keyp()                                 -- typing on: the keyboard types
  U.wants()                              -- the pointer, when there is a mouse
  local s = saved()
  files_seen = s and s.files or {}
  last_t = time()
  local a = cart_arg()
  if a and a.from == "sdk" then table.insert(MENU, #MENU, { "Back to bm SDK", back_to_sdk }) end
  if a and a.path and open_file(a.path) then
    local back = a.back and files_seen[a.path:lower()]
    if a.error then
      go("menu")
      say("the game stopped: " .. a.error:sub(1, 60), C_ERR, 400)
    elseif back then
      go(back.page or "draw")
      say("back from the game", C_ACC)
    else
      go("draw")
    end
  else
    new_sheet()
    go("menu")
    open_chooser()
    say("open a .bm to draw its sprites, or Esc for a new sheet", C_ACC, 400)
  end
end

local function global_key(k)
  if k == "f1" then go("draw"); return true
  elseif k == "f2" then go("sheet"); return true
  elseif k == "f3" then go("palette"); return true
  elseif k == "f4" then go("stack"); return true
  elseif k == "\t" then
    if page == "draw" then draw_actions() elseif page == "sheet" then sheet_actions()
    elseif page == "palette" then pal_actions() elseif page == "stack" then stack_actions() end
    return true
  elseif k == "esc" and page ~= "menu" and not (page == "draw" and (dp.float or dp.anchor or dp.sel)) and
         not (page == "palette" and pp.edit) then go("menu"); return true
  -- the system's keys (the kernel's syskeys.c)
  elseif k == "^s" then save_now(); return true
  elseif k == "^S" then save_as(); return true
  elseif k == "^o" then open_other(); return true
  elseif k == "^n" then new_other(); return true
  elseif k == "f5" or k == "^r" then try_game(); return true
  elseif k == "^z" then
    if page == "draw" and dp.float then draw_key("esc") else do_undo(undo, redo, "undo") end
    return true
  elseif k == "^y" then do_undo(redo, undo, "redo"); return true
  elseif k == "f6" then go("draw"); draw_key("f6"); return true
  end
  return false
end

local PAGES = { "draw", "sheet", "palette", "stack", "menu" }

-- Ctrl+Esc or PS (the system's keys): back to bm's menu; with unsaved
-- changes it asks first, and the same again leaves without saving
function _exit()
  if dirty and not confirmed("exit", "unsaved changes: Ctrl+Esc again leaves without saving") then return false end
  remember()
  return true
end

-- the mouse, each frame: the chips and the tabs press their keys, the
-- dialogs choose, the page does its own, the right button opens the
-- context menu (the page's keys)
local function mouse_frame()
  if not U.on then return end
  if U.keys() then return end
  if dialog_mouse() then return end
  if page == "draw" then draw_mouse()
  elseif page == "sheet" then sheet_mouse()
  elseif page == "palette" then pal_mouse()
  elseif page == "stack" then stack_mouse()
  else
    if U.wheel ~= 0 then msel = clamp(msel - U.wheel, 1, #MENU) end
    local z = U.click(0)
    if z and z.kind == "menuitem" then msel = z.a; U.press("\n") end
  end
  if not U.clicked(1) or page == "menu" then return end
  local list
  if page == "draw" then
    list = { { "Pencil", "b" }, { "Eraser", "e" }, { "Fill", "g" }, { "Pick", "i" }, { "Line", "l" }, { "Rectangle", "u" },
             { "Filled rectangle", "U" }, { "Oval", "o" }, { "Filled oval", "O" }, { "Select", "m" }, { "Wand select", "w" }, "-",
             { "Copy", "^c" }, { "Cut", "^x" }, { "Paste", "^v" }, { "Mirror", "h" }, { "Upside down", "v" },
             { "Turn", "r" }, { "Clear", "del" }, "-", { "Mirror drawing", "y" }, { "Grid", "t" }, { "Onion skin", "k" },
             { "Play", "p" }, { "Size", "z" }, { "Tile view", "q" } }
  elseif page == "stack" then
    list = { { "Draw this slice", "\n" }, { "Onion skin", "k" }, { "Copy the slice to the next", "c" }, { "New zone", "n" },
             { "Change the zone", "e" }, { "Delete the zone", "del" }, { "Turn or stop", "p" } }
  elseif page == "sheet" then
    list = { { "Draw it", "\n" }, { "Size", "z" }, { "Zoom in", "+" }, { "Zoom out", "-" }, { "Copy", "^c" },
             { "Paste", "^v" }, { "Clear", "del" }, { "Sheet size...", "R" } }
  else
    list = { { "Draw with it", "\n" }, { "Edit", "e" }, { "Add", "a" }, { "Remove", "del" }, { "Sort", "s" },
             { "From the sheet", "f" } }
  end
  list[#list + 1] = "-"
  for _, it in ipairs({ { "Undo", "^z" }, { "Save", "^s" }, { "Try the game", "f5" }, { "Menu", "esc" } }) do
    list[#list + 1] = it
  end
  U.menu(list)
end

function _update()
  frame = frame + 1
  if busy then                          -- nothing else until the save is done
    if busy.wait > 0 then busy.wait = busy.wait - 1
    else
      local fn = busy.fn
      busy = nil
      fn()
      last_t = time()
    end
    return
  end
  if msg_t > 0 then msg_t = msg_t - 1 end
  if confirm_t > 0 then confirm_t = confirm_t - 1 end
  local now = time()
  local dt = clamp(now - last_t, 0, 0.1)
  last_t = now
  read_pad()
  U.update()
  if U.menu_update() then return end      -- the context menu has the input
  mouse_frame()

  while true do
    local k = keyp()
    if not k then break end
    if input then input_key(k)
    elseif pick then pick_key(k)
    elseif not global_key(k) then
      if page == "draw" then draw_key(k)
      elseif page == "sheet" then sheet_key(k)
      elseif page == "palette" then pal_key(k)
      elseif page == "stack" then stack_key(k)
      else menu_key(k) end
    end
  end

  -- gamepad
  if input then
    if btnp(5) then input = nil end
  elseif pick then
    if rp[2] then pick_key("up") elseif rp[3] then pick_key("down")
    elseif btnp(4) then pick_key("ok") elseif btnp(5) then pick_key("back") end
  elseif btn(7) then
    if btnp(0) or btnp(1) then
      local i = 1
      for n, p in ipairs(PAGES) do if p == page then i = n end end
      go(PAGES[(i - 1 + (btnp(1) and 1 or -1)) % #PAGES + 1])
    elseif btnp(5) then go("menu")
    elseif btnp(6) then go("draw"); draw_key("f6")
    elseif btnp(4) then do_undo(undo, redo, "undo")
    elseif btnp(2) then draw_key("pgup")
    elseif btnp(3) then draw_key("pgdn") end
  elseif page == "menu" then
    if rp[2] then menu_key("up") elseif rp[3] then menu_key("down")
    elseif btnp(4) then menu_key("ok") elseif btnp(5) then menu_key("back") end
  elseif page == "draw" then draw_pad()
  elseif page == "sheet" then sheet_pad()
  elseif page == "palette" then pal_pad()
  elseif page == "stack" then stack_pad() end

  if page == "draw" then draw_update(dt) elseif anim.play then anim.t = anim.t + dt end
  if page == "stack" then stack_update() end
end

function _draw()
  U.begin()
  if page == "draw" then draw_draw()
  elseif page == "sheet" then draw_sheet()
  elseif page == "palette" then draw_palette()
  elseif page == "stack" then draw_stack()
  else draw_menu() end

  rectfill(0, 0, W, 16, C_BAR)               -- the tabs: each page with its key
  local tabs = { { "draw", "f1", "draw" }, { "sheet", "f2", "sheet" }, { "palette", "f3", "palette" },
                 { "stack", "f4", "stack" }, { "menu", "esc", "menu" } }
  local x = U.bar_title("bm Pixel", C_ACC)      -- the name first, at the left
  for _, t in ipairs(tabs) do
    local lx = snap(x + prompt(t[2]) + 3)
    local w = lx + #t[3] * 8 - x
    if t[1] == page then rectfill(x - 4, 0, w + 8, 16, C_SEL) end
    U.key_zone(x - 4, 0, w + 8, 16, t[2])          -- a click: the page
    prompt(t[2], x, 0)
    print(t[3], lx, 0, t[1] == page and 0xFFFFFF or C_DIM)
    x = x + w + 12
  end
  U.bar_file(proj.path or "new sheet", dirty, x, W, C_ACC, C_DIM)

  rectfill(0, STATUS_Y, W, H - STATUS_Y, C_BAR)
  local status
  if msg_t > 0 and msg then status = msg
  elseif page == "menu" then status = "up/down choose, Enter select"
  else
    local sn, sk, sc = stack_at()
    status = string.format("%s  %s  colour %s", sn and string.format("stack %s %d/%d", sn, sk, sc) or ("sprite " .. sprite_index()),
                           tool, colour_name(colour()))
  end
  print(status:sub(1, 79), 0, STATUS_Y, (msg_t > 0 and msg_c) or C_TEXT)
  if #status <= 62 then                  -- room on the right: F12 held for the keys
    chip_hint("f12", nil, "held: keys", 528, STATUS_Y)
  end
  if pick then draw_pick() end
  if input then draw_input() end
  if busy then                           -- the text on the columns and rows of the font
    local w = #busy.text * 8 + 32
    local x = (W - w) // 16 * 8
    rectfill(x, 160, w, 48, C_BAR)
    rect(x, 160, w, 48, C_ACC)
    print(busy.text, x + 16, 176, C_ACC)
  end
  U.menu_draw()                          -- the context menu over everything
end
