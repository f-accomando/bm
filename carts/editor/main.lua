-- bm editor: code, sprites and map of a .bm cartridge, on the console.
-- F1 code, F2 sprites, F3 map, Esc menu; Ctrl+S save, Ctrl+R (or F5) try it.
-- Hold F12 for the list of keys. On a gamepad: Y + left/right changes page,
-- Y + B opens the menu.

local W, H = SCREEN_W, SCREEN_H
local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x707890, 0xFFC050, 0xFF6060, 0x3050A0
local ROWS = 20                          -- text rows between the tab bar and the status bar

local TEMPLATE = [[
-- my game
local x, y = 320, 180

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
  print("hello!", 296, 40, 0xFFFFFF)
end
]]

----------------------------------------------------------------- state

local page = "menu"                      -- code, sprite, map, menu
local proj = { title = "New game", author = "", res = "640x360", save = nil, path = nil }
local lines = { "" }
local dirty = false
local msg, msg_c, msg_t = nil, C_TEXT, 0
local err_text, err_line = nil, nil
local frame = 0
-- sprite and map pages (declared here: the project loader resets them)
local sel, size, px, py = 0, 2, 0, 0     -- selected cell, 1 = 8x8 / 2 = 16x16, pixel cursor
local sheet_w, sheet_h, map_w, map_h = 256, 256, 256, 256
local mx, my, vx0, vy0 = 0, 0, 0, 0      -- map cursor cell, top-left cell of the view

local function say(s, c, t) msg, msg_c, msg_t = s, c or C_TEXT, t or 180 end

local function split_lines(s)
  local out = {}
  s = s:gsub("\r\n", "\n")
  for l in (s .. "\n"):gmatch("(.-)\n") do out[#out + 1] = l:gsub("\t", "  ") end
  if #out > 1 and out[#out] == "" then out[#out] = nil end
  if #out == 0 then out[1] = "" end
  return out
end

-- "/carts/pong.bm" -> "/carts/PONG.BM"; longer names are cut to 8 characters
local function short_path(path)
  local dir, base = path:match("^(.*)/([^/]+)$")
  dir = dir or "/carts"
  if dir == "" then dir = "/" end
  local stem = (base or path):gsub("%.[^.]*$", ""):upper():gsub("[^%w_]", "")
  if stem == "" then stem = "GAME" end
  return (dir == "/" and "" or dir) .. "/" .. stem:sub(1, 8) .. ".BM"
end

----------------------------------------------------------------- pad

local held, rp = {}, {}
local function read_pad()
  for i = 0, 7 do
    if btn(i) then held[i] = (held[i] or 0) + 1 else held[i] = 0 end
    local h = held[i]
    rp[i] = h == 1 or (h > 14 and h % 4 == 0)
  end
end

----------------------------------------------------------------- project

local function load_project(path)
  local p, e = cart_load(path)
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C_ERR); return false end
  proj = { title = p.title, author = p.author, res = p.res, path = path, save = short_path(path) }
  lines = split_lines(p.lua)
  sheet_w, sheet_h, map_w, map_h = p.sheet_w, p.sheet_h, p.map_w, p.map_h
  sel, px, py, mx, my, vx0, vy0 = 0, 0, 0, 0, 0, 0, 0
  dirty = false
  err_text, err_line = nil, nil
  say("opened " .. path .. "  (saves as " .. proj.save .. ")", C_ACC)
  return true
end

local function new_project()
  cart_new()
  sheet_w, sheet_h, map_w, map_h = 256, 256, 256, 256
  proj = { title = "New game", author = "", res = "640x360", save = nil, path = nil }
  lines = split_lines(TEMPLATE)
  dirty = false
  err_text, err_line = nil, nil
  say("new project: Esc > Save as to give it a name", C_ACC)
end

local function save_project()
  if not proj.save then return false, "no name yet: use Save as" end
  local ok, e = cart_save(proj.save, { title = proj.title, author = proj.author, res = proj.res,
                                       lua = table.concat(lines, "\n") .. "\n" })
  if ok then dirty = false; say("saved " .. proj.save, C_ACC) else say("save failed: " .. tostring(e), C_ERR) end
  return ok
end

local function run_project()
  if not proj.save then say("give the project a name first: Esc > Save as", C_ERR); return end
  if save_project() then cart_run(proj.save) end
end

----------------------------------------------------------------- code page

local cx, cy, top, left = 0, 1, 1, 0     -- cursor column (0 = before the first char), line
local undo, redo_last = {}, nil
local last_edit = nil
local GUTTER = 5
local VISIBLE_COLS = 80 - GUTTER

local function snapshot(kind)
  if last_edit == kind .. cy then return end
  last_edit = kind .. cy
  local copy = table.move(lines, 1, #lines, 1, {})
  undo[#undo + 1] = { lines = copy, cx = cx, cy = cy }
  if #undo > 40 then table.remove(undo, 1) end
end

local function clamp_cursor()
  if cy < 1 then cy = 1 end
  if cy > #lines then cy = #lines end
  if cx > #lines[cy] then cx = #lines[cy] end
  if cx < 0 then cx = 0 end
  if cy < top then top = cy end
  if cy >= top + ROWS then top = cy - ROWS + 1 end
  if cx < left then left = cx end
  if cx >= left + VISIBLE_COLS - 1 then left = cx - VISIBLE_COLS + 2 end
end

local KEYWORDS = {}
for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true until while"):gmatch("%S+") do KEYWORDS[w] = true end
local API = {}
for w in ("cls pset pget line rect rectfill circ circfill spr sspr map mget mset sget sset print camera clip rgb btn btnp time stat tri mesh mesh_sphere mesh_cube draw3d camera3d light3d fog3d project3d zclear log quit save saved note noteoff freq envelope duty playing apu light_begin light light_end keyp SCREEN_W SCREEN_H SQUARE TRIANGLE SAW NOISE math string table ipairs pairs tostring tonumber"):gmatch("%S+") do API[w] = true end

local C_KW, C_API, C_STR, C_NUM, C_COM, C_PUN = 0xFF7AB0, 0x70D0FF, 0x90E070, 0xFFB060, 0x607088, 0xB0B8D0
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
      col = KEYWORDS[w] and C_KW or API[w] and C_API or C_TEXT
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

local function code_key(k)
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
    dirty = true
  elseif k == "\b" then
    snapshot("bs")
    if cx > 0 then lines[cy] = l:sub(1, cx - 1) .. l:sub(cx + 1); cx = cx - 1
    elseif cy > 1 then
      cx = #lines[cy - 1]
      lines[cy - 1] = lines[cy - 1] .. l
      table.remove(lines, cy)
      cy = cy - 1
    end
    dirty = true
  elseif k == "del" then
    snapshot("del")
    if cx < #l then lines[cy] = l:sub(1, cx) .. l:sub(cx + 2)
    elseif cy < #lines then lines[cy] = l .. lines[cy + 1]; table.remove(lines, cy + 1) end
    dirty = true
  elseif k == "\t" then
    snapshot("ins")
    lines[cy] = l:sub(1, cx) .. "  " .. l:sub(cx + 1)
    cx = cx + 2
    dirty = true
  elseif k == "^z" then
    local u = table.remove(undo)
    if u then lines, cx, cy = u.lines, u.cx, u.cy; last_edit = nil; dirty = true; say("undo") end
  elseif k == "^k" then
    snapshot("kill")
    if #lines > 1 then table.remove(lines, cy) else lines[1] = "" end
    last_edit = nil
    dirty = true
  elseif k == "^d" then
    snapshot("dup")
    table.insert(lines, cy + 1, l)
    cy = cy + 1
    last_edit = nil
    dirty = true
  elseif k == "^g" then
    if err_line then cy, cx = err_line, 0 end
  elseif #k == 1 and k:byte() >= 32 then
    snapshot("ins")
    lines[cy] = l:sub(1, cx) .. k .. l:sub(cx + 1)
    cx = cx + 1
    dirty = true
  end
  if k ~= "\b" and k ~= "del" and not (#k == 1 and k:byte() >= 32) then
    if k ~= "\n" and k ~= "\t" then last_edit = nil end
  end
  clamp_cursor()
end

local function draw_code()
  rectfill(0, 16, W, ROWS * 16, C_BG)
  for r = 0, ROWS - 1 do
    local i = top + r
    local y = 16 + r * 16
    if i > #lines then break end
    local num = tostring(i)
    print(string.rep(" ", 4 - #num) .. num, 0, y, i == cy and C_ACC or C_DIM)
    if i == err_line then rectfill(GUTTER * 8 - 4, y, 3, 16, C_ERR) end
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
  if (frame // 20) % 2 == 0 then
    local x, y = (GUTTER + cx - left) * 8, 16 + (cy - top) * 16
    rectfill(x, y + 13, 8, 2, C_ACC)
  end
end

----------------------------------------------------------------- palette

local PALETTE = {
  0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
  0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
  0x14161E, 0x2E2832, 0x4A3E36, 0x6A5A48, 0x8A7A60, 0xB09078, 0x3A5A40, 0x6CC04A,
  0x203A6A, 0x3060D0, 0x70A8F0, 0x5A0A0A, 0xA01818, 0xE04040, 0xFF9030, 0xFFE080,
}

----------------------------------------------------------------- sprite page

local color, transparent = 0xFFFFFF, false
local focus = "canvas"                   -- or "sheet"
local s_undo, clip_px = {}, nil
local stroke = false

local function cells_per_row() return sheet_w // 8 end
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
  dirty = true
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
    local key = j * n + i
    if i >= 0 and j >= 0 and i < n and j < n and not seen[key] and (sget(sx + i, sy + j) or false) == target then
      seen[key] = true
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
      local c = horizontal and pix[j * n + (n - 1 - i)] or pix[(n - 1 - j) * n + i]
      sset(sx + i, sy + j, c)
    end
  end
  dirty = true
end

local function next_color(d)
  local k = 1
  for i, c in ipairs(PALETTE) do if c == color then k = i end end
  if transparent then k = d > 0 and 0 or #PALETTE + 1 end
  k = k + d
  if k < 1 or k > #PALETTE then transparent = true else transparent = false; color = PALETTE[k] end
end

local function move_sel(dx, dy)
  local cols, rows = cells_per_row(), sheet_h // 8
  local x, y = sel % cols + dx * size, sel // cols + dy * size
  x = math.max(0, math.min(cols - size, x))
  y = math.max(0, math.min(rows - size, y))
  sel = y * cols + x
end

local function sprite_action(a)
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
  elseif a == "paste" and clip_px then
    save_sel_undo()
    local sx, sy = sel_xy()
    local m = math.min(n, clip_px.n)
    for j = 0, m - 1 do for i = 0, m - 1 do sset(sx + i, sy + j, clip_px.pix[j * clip_px.n + i]) end end
    dirty = true
  elseif a == "undo" then
    local u = table.remove(s_undo)
    if u then
      sel, size = u.sel, u.size
      local sx, sy = sel_xy()
      local m = size * 8
      for j = 0, m - 1 do for i = 0, m - 1 do sset(sx + i, sy + j, u.pix[j * m + i] or nil) end end
      say("undo")
    end
  elseif a == "focus" then focus = "sheet" end
end

local SPRITE_KEYS = { up = "up", down = "down", left = "left", right = "right", [" "] = "paint",
  ["\b"] = "erase", del = "erase", x = "pick", f = "fill", ["]"] = "next", ["["] = "prev",
  z = "size", h = "fliph", v = "flipv", ["^c"] = "copy", ["^v"] = "paste", ["^z"] = "undo",
  u = "undo", ["\t"] = "focus", ["\n"] = "ok",
  -- the same keys without AltGr/Option: , . on every layout, è + on the Italian one
  [","] = "prev", ["."] = "next", ["\138"] = "prev", ["\130"] = "prev", ["+"] = "next", ["*"] = "next" }

local function checker(x, y, w, h, s)
  rectfill(x, y, w, h, 0x303440)
  for j = 0, h - 1, s do
    for i = ((j // s) % 2) * s, w - 1, s * 2 do rectfill(x + i, y + j, math.min(s, w - i), math.min(s, h - j), 0x484C58) end
  end
end

local function draw_sprite()
  rectfill(0, 16, W, ROWS * 16, C_BG)
  local n = size * 8
  local z = size == 1 and 36 or 18
  local ox, oy = 16, 24
  local sx, sy = sel_xy()
  checker(ox, oy, n * z, n * z, z)
  for j = 0, n - 1 do
    for i = 0, n - 1 do
      local c = sget(sx + i, sy + j)
      if c then rectfill(ox + i * z, oy + j * z, z - 1, z - 1, c) end
    end
  end
  if focus == "canvas" then
    rect(ox + px * z - 1, oy + py * z - 1, z + 1, z + 1, (frame // 10) % 2 == 0 and 0xFFFFFF or 0x000000)
  end
  -- the sheet at 1:1 (up to 256x256 of it)
  local vx, vy = 336, 28
  local vw, vh = math.min(256, sheet_w), math.min(256, sheet_h)
  local scx = math.max(0, math.min(sheet_w - vw, sx - vw // 2))
  local scy = math.max(0, math.min(sheet_h - vh, sy - vh // 2))
  scx, scy = scx // 8 * 8, scy // 8 * 8
  checker(vx, vy, vw, vh, 4)
  sspr(scx, scy, vw, vh, vx, vy)
  rect(vx + sx - scx - 1, vy + sy - scy - 1, n + 2, n + 2, focus == "sheet" and C_ACC or 0xFFFFFF)
  -- palette
  local py0 = 300
  for i, c in ipairs(PALETTE) do
    local x, y = 336 + ((i - 1) % 16) * 16, py0 + ((i - 1) // 16) * 14
    rectfill(x, y, 15, 13, c)
    if c == color and not transparent then rect(x - 1, y - 1, 17, 15, 0xFFFFFF) end
  end
  checker(600, py0, 28, 27, 4)
  if transparent then rect(599, py0 - 1, 30, 29, 0xFFFFFF) end
  -- current colour and info
  rectfill(16, 318, 20, 16, 0x000000)
  if transparent then checker(17, 319, 18, 14, 4) else rectfill(17, 319, 18, 14, color) end
  print(string.format("cell %d  %dx%d  (%d,%d)  %s", sel, n, n, px, py,
        transparent and "transparent" or string.format("#%06X", color)), 44, 318, C_TEXT)
end

----------------------------------------------------------------- map page

local tile = 1
local picking = false
local m_undo = {}
local m_stroke = nil
local VIEW_CW, VIEW_CH = 80, 20

local function set_cell(x, y, v)
  local old = mget(x, y)
  if old == v then return end
  if m_stroke then m_stroke[#m_stroke + 1] = { x, y, old } end
  mset(x, y, v)
  dirty = true
end

local function map_fill(x0, y0)
  local target = mget(x0, y0)
  if target == tile then return end
  local stack, count = { { x0, y0 } }, 0
  while #stack > 0 and count < 20000 do
    local p = table.remove(stack)
    local x, y = p[1], p[2]
    if x >= 0 and y >= 0 and x < map_w and y < map_h and mget(x, y) == target then
      set_cell(x, y, tile)
      count = count + 1
      stack[#stack + 1] = { x + 1, y }; stack[#stack + 1] = { x - 1, y }
      stack[#stack + 1] = { x, y + 1 }; stack[#stack + 1] = { x, y - 1 }
    end
  end
end

local function map_action(a)
  if picking then
    local cols = sheet_w // 8
    if a == "left" then tile = math.max(0, tile - 1)
    elseif a == "right" then tile = tile + 1
    elseif a == "up" then tile = math.max(0, tile - cols)
    elseif a == "down" then tile = tile + cols
    elseif a == "paint" or a == "ok" or a == "focus" then picking = false end
    tile = math.min(tile, cols * (sheet_h // 8) - 1)
    return
  end
  if a == "left" then mx = mx - 1 elseif a == "right" then mx = mx + 1
  elseif a == "up" then my = my - 1 elseif a == "down" then my = my + 1
  elseif a == "pgup" then my = my - VIEW_CH elseif a == "pgdn" then my = my + VIEW_CH
  elseif a == "paint" then m_stroke = m_stroke or {}; set_cell(mx, my, tile)
  elseif a == "erase" then m_stroke = {}; set_cell(mx, my, 0); m_undo[#m_undo + 1] = m_stroke; m_stroke = nil
  elseif a == "pick" then tile = mget(mx, my)
  elseif a == "fill" then m_stroke = {}; map_fill(mx, my); m_undo[#m_undo + 1] = m_stroke; m_stroke = nil
  elseif a == "next" then tile = tile + 1 elseif a == "prev" then tile = math.max(0, tile - 1)
  elseif a == "undo" then
    local u = table.remove(m_undo)
    if u then for i = #u, 1, -1 do mset(u[i][1], u[i][2], u[i][3]) end; say("undo") end
  elseif a == "focus" then picking = true end
  mx = math.max(0, math.min(map_w - 1, mx))
  my = math.max(0, math.min(map_h - 1, my))
  if mx < vx0 then vx0 = mx elseif mx >= vx0 + VIEW_CW then vx0 = mx - VIEW_CW + 1 end
  if my < vy0 then vy0 = my elseif my >= vy0 + VIEW_CH * 2 then vy0 = my - VIEW_CH * 2 + 1 end
end

local function draw_map()
  rectfill(0, 16, W, ROWS * 16, 0x000000)
  clip(0, 16, W, ROWS * 16)
  map(vx0, vy0, 0, 16, VIEW_CW, VIEW_CH * 2)
  local x, y = (mx - vx0) * 8, 16 + (my - vy0) * 8
  rect(x - 1, y - 1, 10, 10, (frame // 10) % 2 == 0 and 0xFFFFFF or C_ACC)
  clip()
  if picking then
    local cols = sheet_w // 8
    local vw, vh = math.min(256, sheet_w), math.min(256, sheet_h)
    local tx, ty = (tile % cols) * 8, (tile // cols) * 8
    local scx = math.max(0, math.min(sheet_w - vw, tx - vw // 2)) // 8 * 8
    local scy = math.max(0, math.min(sheet_h - vh, ty - vh // 2)) // 8 * 8
    local ox, oy = W - vw - 24, 32
    rectfill(ox - 8, oy - 8, vw + 16, vh + 16, C_PANEL)
    checker(ox, oy, vw, vh, 4)
    sspr(scx, scy, vw, vh, ox, oy)
    rect(ox + tx - scx - 1, oy + ty - scy - 1, 10, 10, C_ACC)
  end
end

----------------------------------------------------------------- menu page

local items, msel = {}, 1
local files, fsel, choosing = nil, 1, false
local input = nil                        -- { label, text, done }
local confirm_t, confirm_what = 0, nil

local function needs_confirm(what)
  if not dirty then return false end
  if confirm_what == what and confirm_t > 0 then return false end
  confirm_what, confirm_t = what, 150
  say("unsaved changes: choose again to confirm", C_ERR)
  return true
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

local function build_menu()
  items = {
    { "Continue", function() page = "code" end },
    { "New project", function() if not needs_confirm("new") then new_project(); page = "code" end end },
    { "Open...", function()
        if needs_confirm("open") then return end
        files, fsel, choosing = list_files(), 1, true
        if #files == 0 then choosing = false; say("no .bm files on the SD card", C_ERR) end
      end },
    { "Save   (Ctrl+S)", function() if not proj.save then say("no name yet: use Save as", C_ERR) else save_project() end end },
    { "Save as...", function()
        input = { label = "file name (8.3, in /carts)", text = proj.save and proj.save:match("([^/]+)$") or "MYGAME.BM",
                  done = function(t)
                    if not t:upper():match("%.BM$") then t = t .. ".BM" end
                    proj.save = short_path("/carts/" .. t)
                    save_project()
                  end }
      end },
    { "Try it (Ctrl+R)", function() run_project() end },
    { "Title: " .. proj.title, function()
        input = { label = "title", text = proj.title, done = function(t) proj.title = t; dirty = true end }
      end },
    { "Author: " .. (proj.author ~= "" and proj.author or "-"), function()
        input = { label = "author", text = proj.author, done = function(t) proj.author = t; dirty = true end }
      end },
    { "Screen: " .. proj.res, function()
        proj.res = proj.res == "640x360" and "320x180" or "640x360"
        dirty = true
      end },
    { "Exit editor", function() if not needs_confirm("exit") then quit() end end },
  }
end

local function menu_key(k)
  if input then
    if k == "\n" then local d = input.done; local t = input.text; input = nil; d(t)
    elseif k == "esc" then input = nil
    elseif k == "\b" then input.text = input.text:sub(1, -2)
    elseif #k == 1 and k:byte() >= 32 and #input.text < 47 then input.text = input.text .. k end
    return
  end
  if choosing then
    if k == "up" then fsel = math.max(1, fsel - 1)
    elseif k == "down" then fsel = math.min(#files, fsel + 1)
    elseif k == "esc" or k == "back" then choosing = false
    elseif k == "\n" or k == "ok" then
      choosing = false
      if load_project(files[fsel]) then page = "code" end
    end
    return
  end
  build_menu()
  if k == "up" then msel = (msel - 2) % #items + 1
  elseif k == "down" then msel = msel % #items + 1
  elseif k == "\n" or k == "ok" then items[msel][2]()
  elseif k == "esc" or k == "back" then page = "code" end
end

local function draw_menu()
  rectfill(0, 16, W, ROWS * 16, C_BG)
  build_menu()
  print("bm editor", 32, 32, C_ACC)
  print((proj.save or "(not saved yet)") .. (dirty and "  *modified*" or ""), 176, 32, C_DIM)
  for i, it in ipairs(items) do
    local y = 64 + (i - 1) * 16
    if i == msel and not choosing and not input then rectfill(24, y, 300, 16, C_SEL) end
    print(it[1], 32, y, C_TEXT)
  end
  print("hold F12 to see the keys", 344, 64, C_DIM)
  if choosing then
    rectfill(40, 40, 560, 280, C_PANEL)
    rect(40, 40, 560, 280, C_ACC)
    print("open a cartridge (Enter), Esc back", 56, 48, C_ACC)
    local first = math.max(1, math.min(fsel - 7, #files - 13))
    for i = first, math.min(#files, first + 13) do
      local y = 80 + (i - first) * 16
      if i == fsel then rectfill(52, y, 536, 16, C_SEL) end
      print(files[i], 56, y, C_TEXT)
    end
  end
  if input then
    rectfill(80, 144, 480, 64, C_PANEL)
    rect(80, 144, 480, 64, C_ACC)
    print(input.label, 96, 160, C_DIM)
    print(input.text .. ((frame // 20) % 2 == 0 and "_" or ""), 96, 176, C_TEXT)
  end
end

----------------------------------------------------------------- main

function _init()
  keyp()                                -- typing on: the keyboard types text
  local a = cart_arg()
  if a and a.path and load_project(a.path) then
    page = "code"
    if a.error then
      err_text = a.error
      err_line = tonumber(a.error:match("main%.lua:(%d+):"))
      if err_line then cy, cx = math.min(err_line, #lines), 0; clamp_cursor() end
      say("the game stopped: see the line in red (Ctrl+G jumps there)", C_ERR, 400)
    elseif a.back == false then
      say("opened " .. a.path, C_ACC)
    else
      say("back from the game", C_ACC)
    end
  else
    new_project()
  end
end

local function global_key(k)
  if k == "f1" then page = "code"; return true
  elseif k == "f2" then page = "sprite"; return true
  elseif k == "f3" then page = "map"; return true
  elseif k == "f4" or (k == "esc" and page ~= "menu") then page = "menu"; return true
  elseif k == "^s" then if proj.save then save_project() else page = "menu"; say("choose Save as", C_ERR) end; return true
  elseif k == "^r" or k == "f5" then run_project(); return true
  end
  return false
end

local PAGES = { "code", "sprite", "map", "menu" }

function _update()
  frame = frame + 1
  if msg_t > 0 then msg_t = msg_t - 1 end
  if confirm_t > 0 then confirm_t = confirm_t - 1 end
  read_pad()
  stroke = stroke and (btn(4) or false)
  if m_stroke and not btn(4) then m_undo[#m_undo + 1] = m_stroke; m_stroke = nil end

  -- keyboard
  while true do
    local k = keyp()
    if not k then break end
    if page == "menu" and (input or choosing) then menu_key(k)
    elseif not global_key(k) then
      if page == "code" then code_key(k)
      elseif page == "menu" then menu_key(k)
      elseif page == "sprite" then
        local a = SPRITE_KEYS[k]
        if a then sprite_action(a) end
        if k ~= " " then stroke = false end
      elseif page == "map" then
        local a = SPRITE_KEYS[k] or (k == "pgup" and "pgup") or (k == "pgdn" and "pgdn")
        if a then map_action(a) end
        if m_stroke and k ~= " " then m_undo[#m_undo + 1] = m_stroke; m_stroke = nil end
      end
    end
  end

  -- gamepad
  if btn(7) then
    if rp[0] or rp[1] then
      local i = 1
      for n, p in ipairs(PAGES) do if p == page then i = n end end
      page = PAGES[(i - 1 + (rp[1] and 1 or -1)) % #PAGES + 1]
      held.ycombo = true
    elseif btnp(5) then page = "menu"; held.ycombo = true end
    return
  elseif held.ytap then
    held.ytap = nil
    if not held.ycombo then
      if page == "sprite" then sprite_action("focus") elseif page == "map" then map_action("focus") end
    end
    held.ycombo = nil
  end
  if page == "menu" then
    if rp[2] then menu_key("up") elseif rp[3] then menu_key("down")
    elseif btnp(4) then menu_key("ok") elseif btnp(5) then menu_key("back") end
  elseif page == "sprite" or page == "map" then
    local act = page == "sprite" and sprite_action or map_action
    if rp[0] then act("left") end
    if rp[1] then act("right") end
    if rp[2] then act("up") end
    if rp[3] then act("down") end
    if btn(4) and (btnp(4) or rp[0] or rp[1] or rp[2] or rp[3]) then act("paint") end
    if btnp(5) then act("pick") end
    if btnp(6) then act("next") end
  end
end

-- Y: a tap (press and release without a direction) toggles the sheet
local y_was = false
local function watch_y()
  local y = btn(7)
  if y_was and not y then held.ytap = true end
  y_was = y
end

local KEYS = {
  all = { "F1 code   F2 sprites   F3 map   Esc or F4 menu", "Ctrl+S save   Ctrl+R or F5 try the game" },
  code = { "arrows Home End PgUp PgDn   move", "Tab            two spaces", "Ctrl+Z         undo",
           "Ctrl+K         cut the line", "Ctrl+D         duplicate the line", "Ctrl+G         go to the error" },
  sprite = { "arrows         move", "space          draw     Backspace  erase", "x              pick the colour",
             "f              fill", ", or [ or \138   colour before", ". or ] or +   colour after",
             "Tab            choose on the sheet", "z              8x8 / 16x16", "h / v          flip",
             "Ctrl+C Ctrl+V  copy, paste", "u or Ctrl+Z    undo" },
  map = { "arrows PgUp PgDn   move", "space          place the tile", "Backspace      clear the cell",
          "x              pick the tile", "f              fill", ", . \138 +      tile before / after",
          "Tab            choose the tile", "u or Ctrl+Z    undo" },
  menu = { "up/down        choose", "Enter          select", "Esc            back" },
  pad = { "gamepad: A draw  B pick  X next  Y sheet/tiles", "Y + left/right page   Y + B menu" },
}

local function draw_keys()
  local list = {}
  for _, l in ipairs(KEYS.all) do list[#list + 1] = l end
  list[#list + 1] = ""
  for _, l in ipairs(KEYS[page] or {}) do list[#list + 1] = l end
  list[#list + 1] = ""
  for _, l in ipairs(KEYS.pad) do list[#list + 1] = l end
  local h = (#list + 2) * 16
  local y0 = (H - h) // 32 * 16
  rectfill(64, y0, 512, h, C_PANEL)
  rect(64, y0, 512, h, C_ACC)
  for i, l in ipairs(list) do print(l, 80, y0 + i * 16, i <= #KEYS.all and C_ACC or C_TEXT) end
end

function _draw()
  watch_y()
  cls(C_BG)
  if page == "code" then draw_code()
  elseif page == "sprite" then draw_sprite()
  elseif page == "map" then draw_map()
  else draw_menu() end

  -- tab bar
  rectfill(0, 0, W, 16, C_BAR)
  local tabs = { { "code", "F1 code" }, { "sprite", "F2 sprites" }, { "map", "F3 map" }, { "menu", "Esc menu" } }
  local x = 0
  for _, t in ipairs(tabs) do
    local s = " " .. t[2] .. " "
    if t[1] == page then rectfill(x, 0, #s * 8, 16, C_SEL) end
    print(s, x, 0, t[1] == page and 0xFFFFFF or C_DIM)
    x = x + #s * 8 + 8
  end
  local name = (proj.save or "untitled") .. (dirty and "*" or "")
  print(name, W - #name * 8 - 8, 0, dirty and C_ACC or C_DIM)

  -- status bar
  rectfill(0, 16 + ROWS * 16, W, H - 16 - ROWS * 16, C_BAR)
  local status
  if msg_t > 0 and msg then status = msg
  elseif page == "code" then
    status = err_text or string.format("line %d/%d  col %d", cy, #lines, cx + 1)
  elseif page == "sprite" then status = focus == "sheet" and "choosing on the sheet" or ""
  elseif page == "map" then status = picking and "choosing a tile" or
    string.format("(%d,%d) = %d   tile %d", mx, my, mget(mx, my), tile)
  else status = "up/down choose, Enter select" end
  if #status < 58 then status = status .. string.rep(" ", 58 - #status) .. "hold F12: keys" end
  if keyheld("f12") then draw_keys() end
  print(status:sub(1, 79), 0, 16 + ROWS * 16, (msg_t > 0 and msg_c) or (err_text and page == "code" and C_ERR) or C_TEXT)
end
