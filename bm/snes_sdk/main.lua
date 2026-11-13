-- bm SNES SDK: one app to make SNES games, with tabs: Code | Sprites | Map | Music | Run.
-- This step has the frame (the tabs, the project, the save) and the Sprites tab. The other tabs say
-- "coming soon". One SDK, many tabs: a new tool of the SDK is a new tab here, not a new app.
--
-- The project is a .bme in /bme. The sprite sheet is its SHEET8 (the colours of the sub-palettes first);
-- the rest of the Sprites data (depth, sizes, sub-palettes, the sub-palette of each tile) is text between
-- the marks "-- snes-sdk:begin" and "-- snes-sdk:end" in its Lua section (read back when it opens).
-- Keys: F1..F5 the tabs, Esc the menu, Ctrl+S save, Ctrl+O open, Ctrl+N new, F12 (held) the keys.
-- The converters are in bm/shared/snesgfx.lua (no interface; tests/snes/snesgfx_test.lua).

local LY = require("bmlib").layout
LY.auto()
local W, H = SCREEN_W, SCREEN_H
local U = require "bmui"
-- the converters: the build puts the module in the global SNESGFX after this file, so G looks it up at the first call
local G = setmetatable({}, { __index = function(_, k) return (SNESGFX or require "snesgfx")[k] end })
-- the same for the Code tab: bm/shared/snescode.lua (SNESCODE) and bm/shared/asmbuf.lua (ASMBUF)
local SC = setmetatable({}, { __index = function(_, k) return (SNESCODE or require "snescode")[k] end })
local BF = setmetatable({}, { __index = function(_, k) return (ASMBUF or require "asmbuf")[k] end })
-- and for the Run tab: bm/shared/snesrun.lua (SNESRUN)
local RR = setmetatable({}, { __index = function(_, k) return (SNESRUN or require "snesrun")[k] end })
-- and for the Learn tab: bm/shared/sneslearn.lua (SNESLEARN) and the lesson files (the table SNESLESSONS)
local SL = setmetatable({}, { __index = function(_, k) return (SNESLEARN or require "sneslearn")[k] end })

local C_BG, C_PANEL, C_BAR = 0x14161E, 0x1C2030, 0x2A3048
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x707890, 0xFFC050, 0xFF6060, 0x3050A0
local C_GRID, C_GRID8, C_PT, C_CHK1, C_CHK2 = 0x3C4254, 0x5A6280, 0xFFE070, 0x2A2E3A, 0x343846
local PALCOL = { 0xE04040, 0x40C040, 0x4080F0, 0xE0C040, 0xC050D0, 0x40C0C0, 0xF08030, 0xA0A0A0 }
local HINT_Y, STATUS_Y = H - 40, H - 24
local SV_X, SV_Y, SV_W, SV_H = 8, 32, 296, 272    -- the sheet view; the panel is right of it
local PX = 320
local floor, min, max = math.floor, math.min, math.max
local function clamp(v, a, b) if v < a then return a elseif v > b then return b end return v end
local function snap8(x) return (x + 7) // 8 * 8 end

LY.on_resize(function(w, h)
  W, H = w, h
  HINT_Y, STATUS_Y = H - 40, H - 24
  SV_W, SV_H = w // 2 - 24, HINT_Y - 48
  PX = w // 2
end)

local TABS = {
  { "Code", "f1", "the 65C816 assembly editor: colours, errors of the assembler, build" },
  { "Sprites", "f2", "" },
  { "Map", "f3", "the tilemap editor for the BG layers" },
  { "Music", "f4", "patterns, BRR samples and the SPC700 limits" },
  { "Run", "f5", "build the ROM and run it beside the registers and the memory" },
  { "Learn", "^l", "lessons that you read beside your code" },
}
local tab = 2

----------------------------------------------------------------- state

local P = { path = nil, title = "", dirty = false, lua = "" }
-- the Code tab: the open source files (buffers), the main one, the errors of the last build, the help panel
local CD
local function code_reset()
  CD = { files = {}, order = {}, cur = nil, main = nil, errs = {}, esel = 0, show_errs = false, help = nil, find = "" }
end
code_reset()
-- the Run tab: state "off" (nothing loaded), "run" or "stop"; set = the breakpoints { [addr24] = true }, info = where each
-- one is in the source { [addr24] = {file, line} }; lines = the map of the last build (res.lines); built = the text of
-- each file at the last build (the map is old when a text is not the same)
local RN = { state = "off", set = {}, info = {}, lines = nil, built = {}, space = "ram", addr = 0x7E0000, regs = nil, mem = "",
             pw = 0, ph = 0, rows = 8, bpr = 4 }
-- the Learn tab: the lesson shown (cur), the first row on the screen (top), the selected code block (sel), the view
-- (split: lesson and Code buffer side by side; focus "lesson" or "code"), and what the project keeps (st: see sneslearn.lua)
local LN = { cur = 1, top = 1, sel = 1, split = false, focus = "lesson", st = nil, rows = nil, rows_key = nil }
local M = { bpp = 4, set = 0, large = false, cur = 1, sel = 2, ch = 1, fx = false, fy = false, prio = 0,
            pals = {}, tp = {} }                  -- the Sprites data of the project
local sw, sh = 128, 128                          -- the sheet
local ix = {}                                    -- the pixels: ix[y * sw + x] = index in the sub-palette (0 clear)
local rx, ry, rs = 0, 0, 16                      -- the sprite: its corner and size
local ex, ey = 0, 0                              -- the pixel cursor inside the sprite
local edit, fill_tool = false, false
local undo, redo = {}, {}
local clip = nil
local pick, input, busy = nil, nil, nil
local msg, msg_c, msg_t = nil, C_TEXT, 0
local frame = 0
local confirm_t, confirm_what = 0, nil
local held, rp, tap = {}, {}, {}
local xy_down, xy_combo = {}, {}

local function say(s, c, t) msg, msg_c, msg_t = s, c or C_TEXT, t or 200 end
local function choose(title, rows, sel) pick = { title = title, rows = rows, sel = sel or 1 } end
local function ask(label, text, done) input = { label = label, text = text, done = done } end
local function confirmed(what, text)
  if confirm_what == what and confirm_t > 0 then confirm_what = nil; return true end
  confirm_what, confirm_t = what, 150
  say(text, C_ERR)
  return false
end

local function npal() return 1 << M.bpp end
local function nsub() return M.bpp == 8 and 1 or 8 end
-- the sub-palette i: 256 BGR555 words kept (the first npal() are used); entry 1 is colour 0, the clear one
local function subpal(i)
  local p = M.pals[i]
  if not p then
    p = {}
    for k = 1, 256 do p[k] = 0 end
    M.pals[i] = p
  end
  return p
end
local function tile_of(x, y) return (y // 8) * (sw // 8) + x // 8 end
local function sub_of(x, y)
  if M.bpp == 8 then return 1 end
  return M.tp[tile_of(x, y)] or M.cur
end
local function eff(i) if i >= npal() then return 0 end return i end

-- a pixel of the sheet in the runtime: the colour of its index in the sub-palette of its tile
local function paint(x, y)
  local i = eff(ix[y * sw + x] or 0)
  if i == 0 then sset(x, y, nil) else sset(x, y, G.to_rgb(subpal(sub_of(x, y))[i + 1])) end
end
local function paint_rect(x0, y0, w, h)
  for y = y0, min(sh, y0 + h) - 1 do for x = x0, min(sw, x0 + w) - 1 do paint(x, y) end end
end
-- every pixel of the tiles that use a sub-palette (nil: all the sheet)
local function paint_sub(s)
  for ty = 0, sh // 8 - 1 do
    for tx = 0, sw // 8 - 1 do
      if not s or sub_of(tx * 8, ty * 8) == s then paint_rect(tx * 8, ty * 8, 8, 8) end
    end
  end
end

local function dims() return G.sprite_dims(M.set, M.large) end

local function fit_sprite()
  rs = min(rs, sw, sh)
  rx = clamp(rx - rx % rs, 0, sw - rs)
  ry = clamp(ry - ry % rs, 0, sh - rs)
  ex, ey = clamp(ex, 0, rs - 1), clamp(ey, 0, rs - 1)
end
local function set_size() rs = (dims()); fit_sprite() end

----------------------------------------------------------------- undo

local function snap_rect(x, y, w, h)
  local t = {}
  for j = 0, h - 1 do for i = 0, w - 1 do t[#t + 1] = ix[(y + j) * sw + x + i] or 0 end end
  return { x = x, y = y, w = w, h = h, d = t }
end
local function put_rect(s)
  local k = 1
  for j = 0, s.h - 1 do
    for i = 0, s.w - 1 do ix[(s.y + j) * sw + s.x + i] = s.d[k]; k = k + 1 end
  end
  paint_rect(s.x, s.y, s.w, s.h)
end
local function begin(x, y, w, h)
  undo[#undo + 1] = snap_rect(x or rx, y or ry, w or rs, h or rs)
  if #undo > 30 then table.remove(undo, 1) end
  redo = {}
  P.dirty = true
end
local function do_undo(from, to, what)
  local s = table.remove(from)
  if not s then say("nothing to " .. what, C_DIM, 60); return end
  if s.x + s.w > sw or s.y + s.h > sh then say("cannot " .. what .. ": the sheet changed size", C_ERR); return end
  to[#to + 1] = snap_rect(s.x, s.y, s.w, s.h)
  put_rect(s)
  P.dirty = true
  say(what, C_ACC, 60)
end

----------------------------------------------------------------- the project: open, save

local BEGIN, END = "-- snes-sdk:begin", "-- snes-sdk:end"
local VIEWER = [[
-- bm SNES SDK project: its sprite sheet. The ROM code will come from the Code tab.
function _draw()
  cls(0x14161E)
  local w, h = cart_sheet()
  local z = math.min(1, 300 / w, 300 / h)
  sspr(0, 0, w, h, (SCREEN_W - w * z) // 2, 40, false, false, z)
  print("a bm SNES SDK project (" .. w .. "x" .. h .. ")", 16, 12, 0xE0E4F0)
end
]]

-- the data of the Sprites tab as text lines (comments), between the marks
local function meta_text()
  local out = { BEGIN, "--cfg bpp=" .. M.bpp .. " set=" .. M.set .. " large=" .. (M.large and 1 or 0) .. " cur=" .. M.cur
                       .. " sel=" .. M.sel }
  for i = 1, nsub() do
    local p, w = subpal(i), {}
    for k = 1, npal() do w[k] = string.format("%04X", p[k]) end
    out[#out + 1] = "--pal " .. i .. " " .. table.concat(w, " ")
  end
  local keys = {}
  for t in pairs(M.tp) do keys[#keys + 1] = t end
  table.sort(keys)
  for k = 1, #keys, 24 do
    local w = {}
    for j = k, min(#keys, k + 23) do w[#w + 1] = keys[j] .. "=" .. M.tp[keys[j]] end
    out[#out + 1] = "--tp " .. table.concat(w, " ")
  end
  if #CD.order > 0 then out[#out + 1] = "--code main=" .. (CD.main or "") .. " files=" .. table.concat(CD.order, ",") end
  out[#out + 1] = END
  return table.concat(out, "\n")
end

local function parse_meta(lua)
  local a = lua and lua:find(BEGIN, 1, true)
  if not a then return false end
  local body = lua:sub(a):match("^(.-)" .. END:gsub("%p", "%%%0")) or lua:sub(a)
  for line in body:gmatch("[^\r\n]+") do
    local cfg = line:match("^%-%-cfg (.*)$")
    if cfg then
      local v = {}
      for k, n in cfg:gmatch("(%a+)=(%d+)") do v[k] = tonumber(n) end
      if v.bpp == 2 or v.bpp == 4 or v.bpp == 8 then M.bpp = v.bpp end
      M.set = clamp(v.set or 0, 0, 5)
      M.large = v.large == 1
      M.cur, M.sel = max(1, v.cur or 1), max(1, v.sel or 2)
    end
    local i, words = line:match("^%-%-pal (%d+) (.*)$")
    if i then
      local p, k = subpal(tonumber(i)), 0
      for w in words:gmatch("%x+") do k = k + 1; p[k] = tonumber(w, 16) & 0x7FFF end
    end
    local code = line:match("^%-%-code (.*)$")
    if code then
      CD.want_main = code:match("main=([%w_%.%-]*)")
      CD.want_files = {}
      for n in (code:match("files=(%S*)") or ""):gmatch("[^,]+") do CD.want_files[#CD.want_files + 1] = n end
    end
    local tps = line:match("^%-%-tp (.*)$")
    if tps then for t, s in tps:gmatch("(%d+)=(%d+)") do M.tp[tonumber(t)] = tonumber(s) end end
  end
  return true
end

-- the project text of the Lua section: the old text with the data between the marks replaced
local function sdk_lua()
  local old = P.lua or ""
  local a = old:find(BEGIN, 1, true)
  local b = a and select(2, old:find(END, a, true))
  if a and b then return old:sub(1, a - 1) .. meta_text() .. old:sub(b + 1) end
  if old == "" then old = VIEWER end
  return old .. "\n" .. meta_text() .. "\n"
end
-- the Learn data has its own marks, outside the Sprites marks
local function project_lua()
  LN.sync()
  return SL.merge_lua(sdk_lua(), LN.st)
end

local function reset_view()
  undo, redo, edit = {}, {}, false
  M.cur = clamp(M.cur, 1, nsub())
  M.sel = clamp(M.sel, 1, npal())
  rs = (dims())
  rx, ry, ex, ey = 0, 0, 0, 0
  fit_sprite()
end

local function new_project()
  code_reset()
  LN.reset()
  cart_new()
  pcall(cart_sheet, 128, 128)
  sw, sh = cart_sheet()
  P = { path = nil, title = "", dirty = false, lua = "" }
  M.bpp, M.set, M.large, M.cur, M.sel, M.ch, M.pals, M.tp = 4, 0, false, 1, 2, 1, {}, {}
  M.fx, M.fy, M.prio = false, false, 0
  ix = {}
  -- a first palette: a few colours to start with
  local first = { 0x0000, 0x001F, 0x03E0, 0x7C00, 0x7FFF, 0x0000, 0x03FF, 0x7FE0, 0x7C1F, 0x4210 }
  local p = subpal(1)
  for k, v in ipairs(first) do p[k] = v end
  reset_view()
  say("a new 128x128 sheet: Esc > Save as gives it a name", C_ACC)
end

local function open_file(path)
  local p, e = cart_load(path)
  if not p then say("cannot open " .. path .. ": " .. tostring(e), C_ERR); return false end
  code_reset()
  sw, sh = cart_sheet()
  P = { path = path, title = p.title or "", dirty = false, lua = p.lua or "" }
  LN.load(P.lua)
  M.bpp, M.set, M.large, M.cur, M.sel, M.ch, M.pals, M.tp = 4, 0, false, 1, 2, 1, {}, {}
  M.fx, M.fy, M.prio = false, false, 0
  local had = parse_meta(P.lua)
  subpal(1)
  ix = {}
  for y = 0, sh - 1 do
    for x = 0, sw - 1 do
      local c = sget(x, y)
      ix[y * sw + x] = c and G.nearest(subpal(sub_of(x, y)), c) or 0
    end
  end
  reset_view()
  say(path .. ": sheet " .. sw .. "x" .. sh .. (had and "" or " (not an SDK project: the colours were matched to sub-palette 1)"), C_ACC)
  return true
end

local function save_to(path, from, after)
  if not path then say("no name yet: Esc > Save as", C_ERR); return end
  local pal = {}
  for s = 1, nsub() do
    local p = subpal(s)
    for k = 1, npal() do pal[#pal + 1] = G.to_rgb(p[k]) end
  end
  local lua = project_lua()
  local t = { sheet = true, palette = pal, from = from, lua = lua }
  if not P.path then t.title = path:match("([^/]+)%.[^.]*$") or "snes" end
  busy = { text = "saving " .. path .. " ...", wait = 3, fn = function()
    local ok, e = cart_write(path, t)
    if not ok then say("save failed: " .. tostring(e), C_ERR); return end
    if type(e) == "string" then path = e end
    P.path, P.lua, P.dirty = path, lua, false
    say(U.saved_text(path), C_ACC)
    if after then after() end
  end }
end

local function save_as()
  ask("file name (8.3, in /bme)", P.path and P.path:match("([^/]+)$") or "mygame.bme", function(t)
    if t == "" then return end
    save_to(U.project_path(t), P.path)
  end)
end
local function save_now() if P.path then save_to(P.path) else save_as() end end

local function open_chooser()
  local files = U.project_files({ bme = true })
  table.sort(files)
  if #files == 0 then say("no projects in /bme yet: Save as makes one", C_ERR); return end
  local rows, sel = {}, 1
  for i, f in ipairs(files) do
    rows[i] = { f, function() open_file(f) end }
    if P.path and f:lower() == P.path:lower() then sel = i end
  end
  choose("open a project", rows, sel)
end
local function open_other()
  if not P.dirty or confirmed("open", "unsaved changes: choose again to open another project") then open_chooser() end
end
local function new_other()
  if not P.dirty or confirmed("new", "unsaved changes: choose again for a new project") then new_project() end
end

local function resize()
  ask("sheet size (width x height, multiples of 8, up to 512)", sw .. "x" .. sh, function(t)
    local w, h = t:match("^%s*(%d+)%s*[xX*, ]%s*(%d+)%s*$")
    w, h = tonumber(w), tonumber(h)
    if not w or w < 8 or h < 8 or w > 512 or h > 512 or w % 8 ~= 0 or h % 8 ~= 0 then
      say("a size like 128x128, multiples of 8, up to 512", C_ERR)
      return
    end
    local old, ow, oh = ix, sw, sh
    local ok, e = pcall(cart_sheet, w, h)
    if not ok then say(tostring(e), C_ERR); return end
    sw, sh = cart_sheet()
    ix = {}
    for y = 0, sh - 1 do for x = 0, sw - 1 do ix[y * sw + x] = (x < ow and y < oh) and old[y * ow + x] or 0 end end
    -- the tile numbers of the sub-palette table follow the new width
    local tp = {}
    for t2, s in pairs(M.tp) do
      local tx, ty = t2 % (ow // 8), t2 // (ow // 8)
      if tx < sw // 8 and ty < sh // 8 then tp[ty * (sw // 8) + tx] = s end
    end
    M.tp, undo, redo = tp, {}, {}
    fit_sprite()
    P.dirty = true
    say("the sheet is " .. sw .. "x" .. sh, C_ACC)
  end)
end

----------------------------------------------------------------- the Sprites tab: actions

local function chan_get(v, k) return (v >> ((k - 1) * 5)) & 31 end
local function chan_add(d)
  if M.sel <= 1 then return end
  local p = subpal(M.cur)
  local at = (M.ch - 1) * 5
  p[M.sel] = (p[M.sel] & ~(31 << at)) | (clamp(chan_get(p[M.sel], M.ch) + d, 0, 31) << at)
  P.dirty = true
  paint_sub(M.cur)
end
local function chan_set(k, n)
  if M.sel <= 1 then return end
  local p = subpal(M.cur)
  local at = (k - 1) * 5
  p[M.sel] = (p[M.sel] & ~(31 << at)) | (clamp(n, 0, 31) << at)
  P.dirty = true
  paint_sub(M.cur)
end

local function pal_step(d)
  M.cur = (M.cur - 1 + d) % nsub() + 1
  say("sub-palette " .. M.cur .. " (sprite palette bits " .. ((M.cur - 1) & 7) .. ")", C_DIM, 90)
end

-- the tiles of the sprite use the sub-palette M.cur: their pixels change colour, their indices stay
local function assign_sprite()
  for y = ry, ry + rs - 1, 8 do for x = rx, rx + rs - 1, 8 do M.tp[tile_of(x, y)] = M.cur end end
  paint_rect(rx, ry, rs, rs)
  P.dirty = true
  say("the " .. (rs // 8) * (rs // 8) .. " tile(s) of the sprite use sub-palette " .. M.cur, C_ACC, 120)
end

local function put_pixel(x, y, i)
  local at = (ry + y) * sw + rx + x
  if ix[at] ~= i then ix[at] = i; paint(rx + x, ry + y) end
end

local function flood(x0, y0, i)
  local from = ix[(ry + y0) * sw + rx + x0] or 0
  if from == i then return end
  local stack, n = { { x0, y0 } }, 0
  while #stack > 0 and n < 20000 do
    local c = table.remove(stack)
    local x, y = c[1], c[2]
    if x >= 0 and y >= 0 and x < rs and y < rs and (ix[(ry + y) * sw + rx + x] or 0) == from then
      put_pixel(x, y, i)
      n = n + 1
      stack[#stack + 1] = { x + 1, y }; stack[#stack + 1] = { x - 1, y }
      stack[#stack + 1] = { x, y + 1 }; stack[#stack + 1] = { x, y - 1 }
    end
  end
end

-- one pixel of the sprite: the colour M.sel (or clear)
local function draw_at(x, y, erase)
  local i = erase and 0 or (M.sel - 1)
  if fill_tool then begin(); flood(x, y, i) else put_pixel(x, y, i) end
end

local function transform(what)
  begin()
  local t = {}
  for y = 0, rs - 1 do for x = 0, rs - 1 do t[y * rs + x] = ix[(ry + y) * sw + rx + x] or 0 end end
  for y = 0, rs - 1 do
    for x = 0, rs - 1 do
      local sx, sy = x, y
      if what == "h" then sx = rs - 1 - x elseif what == "v" then sy = rs - 1 - y end
      ix[(ry + y) * sw + rx + x] = t[sy * rs + sx]
    end
  end
  paint_rect(rx, ry, rs, rs)
end

local function copy_sprite() clip = snap_rect(rx, ry, rs, rs); say("copied the sprite", C_DIM, 60) end
local function paste_sprite()
  if not clip then say("nothing copied yet (Ctrl+C)", C_ERR); return end
  local w, h = min(clip.w, sw - rx), min(clip.h, sh - ry)
  begin(rx, ry, w, h)
  for j = 0, h - 1 do for i = 0, w - 1 do ix[(ry + j) * sw + rx + i] = clip.d[j * clip.w + i + 1] end end
  paint_rect(rx, ry, w, h)
end

local function export_files()
  local base = (P.path and P.path:match("([^/]+)$") or "sprites"):gsub("%.[^.]*$", ""):lower():gsub("[^%w_]", "")
  if base == "" then base = "sprites" end
  ask("name for the .chr and .pal files (8.3, in /docs)", base:sub(1, 8), function(t)
    local stem = t:lower():gsub("%.[^.]*$", ""):gsub("[^%w_]", ""):sub(1, 8)
    if stem == "" then return end
    if not doc_write then say("export: this console cannot write files", C_ERR); return end
    busy = { text = "exporting the tiles...", wait = 2, fn = function()
      local chr = G.encode_sheet(function(x, y) return eff(ix[y * sw + x] or 0) end, sw, sh, M.bpp, max(16, sw // 8))
      local words = {}
      for i = 1, nsub() do
        local p = subpal(i)
        for k = 1, npal() do words[#words + 1] = p[k] end
      end
      local ok1, e1 = doc_write(stem .. ".chr", chr)
      local ok2, e2 = doc_write(stem .. ".pal", G.pal_bytes(words))
      if not ok1 or not ok2 then say("export failed: " .. tostring(e1 or e2), C_ERR, 300); return end
      say("saved /docs/" .. stem .. ".chr (" .. #chr .. " bytes) and .pal (" .. #words * 2 .. " bytes)"
          .. (sw ~= 128 and "; OAM wants 16 tiles a row: a sheet 128 wide" or ""), C_ACC, 400)
    end }
  end)
end

local function load_pal()
  ask("a .pal file in /docs (8.3)", "sprites.pal", function(t)
    local bytes, why = doc_read(t:match("([^/]+)$") or "")
    if not bytes then say("cannot read it: " .. tostring(why), C_ERR, 300); return end
    local words, n = G.pal_words(bytes), 0
    for i = 1, nsub() do
      local p = subpal(i)
      for k = 1, npal() do n = n + 1; if words[n] then p[k] = words[n] end end
    end
    paint_sub(nil)
    P.dirty = true
    say(#words .. " colours read", C_ACC, 200)
  end)
end

-- a PNG of /docs at the top left of the sheet: its colours are quantised to the sub-palette (2^bpp - 1 + clear)
local function import_png()
  ask("PNG name in /docs (8.3)", "spr1.png", function(t)
    local name = t:match("([^/]+)$") or ""
    if name == "" then return end
    local path = "/docs/" .. name
    local info, why = image_file(path, 0, 0, 1, 1, 100, 0)           -- its size (pixel 0,0 is put back)
    paint(0, 0)
    if not info then say("import failed: " .. tostring(why), C_ERR, 300); return end
    local w, h = min(info.sw, sw), min(info.sh, sh)
    local keep = snap_rect(0, 0, w, h)
    local r, e2 = image_file(path, 0, 0, w, h, 100, 0)
    if not r then put_rect(keep); say("import failed: " .. tostring(e2), C_ERR, 300); return end
    local list = {}
    for y = 0, h - 1 do for x = 0, w - 1 do list[#list + 1] = sget(x, y) or false end end
    local pal, idx = G.quantize(list, npal() - 1)
    local p = subpal(M.cur)
    for k = 1, npal() do p[k] = pal[k] or 0 end
    undo[#undo + 1] = keep
    redo = {}
    for y = 0, h - 1 do for x = 0, w - 1 do ix[y * sw + x] = idx[y * w + x + 1] end end
    for ty = 0, (h - 1) // 8 do for tx = 0, (w - 1) // 8 do M.tp[ty * (sw // 8) + tx] = M.cur end end
    paint_sub(nil)
    P.dirty = true
    say("imported " .. w .. "x" .. h .. ": " .. (#pal - 1) .. " colours + clear in sub-palette " .. M.cur, C_ACC, 400)
  end)
end

local ACTIONS = {}
local function sprites_actions() choose("Sprites: " .. M.bpp .. " bpp", ACTIONS) end

local function sprites_key(k)
  local step = edit and 1 or rs
  if k == "left" then if edit then ex = max(0, ex - 1) else rx = max(0, rx - rs) end
  elseif k == "right" then if edit then ex = min(rs - 1, ex + 1) else rx = min(sw - rs, rx + rs) end
  elseif k == "up" then if edit then ey = max(0, ey - 1) else ry = max(0, ry - rs) end
  elseif k == "down" then if edit then ey = min(rs - 1, ey + 1) else ry = min(sh - rs, ry + rs) end
  elseif k == "pgup" then ry = max(0, ry - rs * 8)
  elseif k == "pgdn" then ry = min(sh - rs, ry + rs * 8)
  elseif k == "home" then rx, ry = 0, 0
  elseif k == "\n" then edit = not edit; say(edit and "drawing: arrows move the pixel, Space draws" or "arrows choose the sprite", C_DIM, 120)
  elseif k == " " then if edit then draw_at(ex, ey, false) end
  elseif k == "del" or k == "\b" then
    if edit then draw_at(ex, ey, true) else
      begin()
      for y = 0, rs - 1 do for x = 0, rs - 1 do ix[(ry + y) * sw + rx + x] = 0 end end
      paint_rect(rx, ry, rs, rs)
    end
  elseif k == "g" then fill_tool = not fill_tool; say(fill_tool and "fill: Space or a click fills" or "pencil", C_DIM, 90)
  elseif k == "b" then
    M.bpp = M.bpp == 2 and 4 or (M.bpp == 4 and 8 or 2)
    M.cur, M.sel = clamp(M.cur, 1, nsub()), clamp(M.sel, 1, npal())
    paint_sub(nil)
    P.dirty = true
    say(M.bpp .. " bits per pixel: " .. npal() .. " colours in a sub-palette, " .. nsub() .. " sub-palette(s)", C_ACC, 150)
  elseif k == "s" or k == "S" then M.set = (M.set + (k == "s" and 1 or 5)) % 6; set_size(); P.dirty = true
  elseif k == "l" then M.large = not M.large; set_size(); P.dirty = true
  elseif k == "," then pal_step(-1)
  elseif k == "." then pal_step(1)
  elseif k == "[" then M.sel = clamp(M.sel - 1, 1, npal())
  elseif k == "]" then M.sel = clamp(M.sel + 1, 1, npal())
  elseif k == "1" or k == "2" or k == "3" then M.ch = tonumber(k)
  elseif k == "+" or k == "=" then chan_add(1)
  elseif k == "-" then chan_add(-1)
  elseif k == "x" then M.fx = not M.fx
  elseif k == "y" then M.fy = not M.fy
  elseif k == "p" then M.prio = (M.prio + 1) % 4
  elseif k == "a" then assign_sprite()
  elseif k == "h" then transform("h")
  elseif k == "v" then transform("v")
  elseif k == "^c" then copy_sprite()
  elseif k == "^v" then paste_sprite()
  elseif k == "i" then import_png()
  elseif k == "e" then export_files()
  elseif k == "L" then load_pal()
  elseif k == "R" then resize()
  end
  fit_sprite()
end

ACTIONS = {
  { "Draw / choose sprite (Enter)", function() sprites_key("\n") end },
  { "Depth 2 / 4 / 8 bpp (b)", function() sprites_key("b") end },
  { "Sprite sizes: next pair (s)", function() sprites_key("s") end },
  { "Sprite: small or large (l)", function() sprites_key("l") end },
  { "Next sub-palette (.)", function() sprites_key(".") end },
  { "The sprite's tiles use this sub-palette (a)", function() sprites_key("a") end },
  { "Pencil or fill (g)", function() sprites_key("g") end },
  { "Flip the sprite across (h)", function() sprites_key("h") end },
  { "Flip the sprite down (v)", function() sprites_key("v") end },
  { "Import a PNG and quantise (i)", function() sprites_key("i") end },
  { "Read a .pal from /docs (shift l)", function() sprites_key("L") end },
  { "Export .chr and .pal to /docs (e)", function() sprites_key("e") end },
  { "Sheet size... (shift r)", function() sprites_key("R") end },
}

----------------------------------------------------------------- drawing

local function checker(x, y, w, h, step)
  rectfill(x, y, w, h, C_CHK1)
  for j = 0, (h - 1) // step do
    for i = (j % 2), (w - 1) // step, 2 do
      rectfill(x + i * step, y + j * step, min(step, w - i * step), min(step, h - j * step), C_CHK2)
    end
  end
end

local function snap(x) return snap8(x) end
local function hint(list)
  local items = {}
  for i, h in ipairs(list) do items[i] = { keys = h[1], label = h[2] } end
  U.hint_row(items, { y = HINT_Y, w = W, snap = snap, dim = C_DIM })
end

local zoom_sheet, ED_X, ED_Y, ED_Z = 1, 0, 0, 1
local ZOOMS = { 0.25, 0.5, 1, 2, 4 }

local function draw_sprites()
  cls(C_BG)
  -- the sheet, its tile grid and the sub-palette of each tile
  local z = ZOOMS[1]
  for _, zz in ipairs(ZOOMS) do if sw * zz <= SV_W and sh * zz <= SV_H then z = zz end end
  zoom_sheet = z
  local vw, vh = min(sw, floor(SV_W / z)), min(sh, floor(SV_H / z))
  checker(SV_X, SV_Y, floor(vw * z), floor(vh * z), 16)
  U.zone(SV_X, SV_Y, floor(vw * z), floor(vh * z), "view", z)
  sspr(0, 0, vw, vh, SV_X, SV_Y, false, false, z)
  local step = 8 * z
  if step >= 4 then
    for gx = 0, vw // 8 do rectfill(SV_X + floor(gx * step), SV_Y, 1, floor(vh * z), C_GRID) end
    for gy = 0, vh // 8 do rectfill(SV_X, SV_Y + floor(gy * step), floor(vw * z), 1, C_GRID) end
  end
  local tw = sw // 8
  for t, s in pairs(M.tp) do
    local tx, ty = t % tw, t // tw
    if tx < vw // 8 and ty < vh // 8 then
      local x, y = SV_X + floor(tx * step), SV_Y + floor(ty * step)
      local c = PALCOL[(s - 1) % 8 + 1]
      local m = max(2, min(5, floor(step / 3)))
      rectfill(x + 1, y + 1, m, m, c)
      if step >= 16 then print(tostring(s), x + 4, y + 1, c) end
    end
  end
  local sx, sy = SV_X + floor(rx * z), SV_Y + floor(ry * z)
  rect(sx - 1, sy - 1, floor(rs * z) + 2, floor(rs * z) + 2, 0x000000)
  rect(sx, sy, floor(rs * z), floor(rs * z), (frame // 10) % 2 == 0 and C_PT or 0xFFFFFF)

  -- the panel: depth, sizes, sub-palette
  local p = subpal(M.cur)
  local y = 32
  local w0, h0 = dims()
  local function item(txt, key, x, yy, c)
    print(txt, x, yy, c or C_TEXT)
    U.key_zone(x, yy, #txt * 8, 16, key)
    return x + #txt * 8 + 16
  end
  local x = PX
  x = item(M.bpp .. " bpp", "b", x, y)
  x = item("sizes " .. G.SIZE_SETS[M.set][1][1] .. "/" .. G.SIZE_SETS[M.set][2][1], "s", x, y)
  item((M.large and "large " or "small ") .. w0 .. "x" .. h0, "l", x, y, C_ACC)
  y = y + 16
  local line = string.format("sub-palette %d of %d (OAM %d, CGRAM %d)", M.cur, nsub(), (M.cur - 1) & 7,
                             128 + ((M.cur - 1) & 7) * 16)
  print(line, PX, y, C_TEXT)
  U.key_zone(PX, y, #line * 8, 16, ".")
  y = y + 16
  local n = npal()
  local cell, per = 16, 16
  if n > 16 then cell, per = 8, 32 end
  if n < 16 then per = n end
  for i = 1, n do
    local cx, cy = PX + ((i - 1) % per) * cell, y + ((i - 1) // per) * cell
    if i == 1 then checker(cx, cy, cell - 1, cell - 1, cell // 2) else rectfill(cx, cy, cell - 1, cell - 1, G.to_rgb(p[i])) end
    if i == M.sel then rect(cx - 1, cy - 1, cell + 1, cell + 1, C_ACC) end
    U.zone(cx, cy, cell, cell, "pcol", i)
  end
  y = y + ((n - 1) // per + 1) * cell + 8
  local v = p[M.sel] or 0
  print(string.format("colour %d%s  BGR555 $%04X", M.sel - 1, M.sel == 1 and " (clear)" or "", v), PX, y, C_TEXT)
  y = y + 16
  local by0 = y
  for k = 1, 3 do
    local val = chan_get(v, k)
    print(({ "R", "G", "B" })[k] .. string.format(" %2d", val), PX, y, M.ch == k and C_ACC or C_DIM)
    rectfill(PX + 40, y + 4, 128, 8, C_PANEL)
    rectfill(PX + 40, y + 4, val * 4 + 4, 8, k == 1 and 0xE04040 or (k == 2 and 0x40C040 or 0x4080F0))
    U.zone(PX + 40, y, 128, 16, "chan", k)
    y = y + 16
  end
  -- the 5-bit preview: the colour as 8 bits (all 8 bits of the red, say) and what the SNES shows
  local r8, g8, b8 = G.rgb8(v)
  rectfill(PX + 176, by0, 24, 16, (r8 << 16) | (g8 << 8) | b8)
  print("= " .. r8 .. "," .. g8 .. "," .. b8, PX + 208, by0, C_DIM)
  print("5 bits a channel", PX + 176, by0 + 16, C_DIM)
  y = y + 8

  -- the sprite editor (left) and the OAM preview (right)
  local ez = max(1, 96 // rs)
  local ew = rs * ez
  if ew > 96 then ez = 1; ew = rs end
  ED_X, ED_Y, ED_Z = PX, y, ez
  checker(ED_X, ED_Y, ew, ew, 8)
  sspr(rx, ry, rs, rs, ED_X, ED_Y, false, false, ez)
  if ez >= 6 then
    for g = 0, rs do
      rectfill(ED_X + g * ez, ED_Y, 1, ew, C_GRID)
      rectfill(ED_X, ED_Y + g * ez, ew, 1, C_GRID)
    end
  end
  U.zone(ED_X, ED_Y, ew, ew, "ed", ez)
  if edit then rect(ED_X + ex * ez, ED_Y + ey * ez, max(ez, 2), max(ez, 2), C_PT) end
  rect(ED_X - 1, ED_Y - 1, ew + 2, ew + 2, edit and C_ACC or C_GRID8)
  local ox = ED_X + 104
  local pw = rs * max(1, 80 // rs)
  local pz = max(1, 80 // rs)
  if pw > 80 then pz = 1; pw = rs end
  checker(ox, y, 80, 80, 8)
  sspr(rx, ry, rs, rs, ox + (80 - pw) // 2, y + (80 - pw) // 2, M.fx, M.fy, pz)
  rect(ox + (80 - pw) // 2 - 1, y + (80 - pw) // 2 - 1, pw + 2, pw + 2, C_GRID8)
  local tile = (ry // 8) * 16 + (rx // 8)
  local e = G.oam_entry(0, 0, tile, (M.cur - 1) & 7, M.prio, M.fx, M.fy)
  local tx = ox + 88
  print(string.format("OAM $%03X", tile), tx, y, C_TEXT)
  print(string.format("attr $%02X", e:byte(4)), tx, y + 16, C_TEXT)
  item(M.fx and "[x] flipX" or "[ ] flipX", "x", tx, y + 32, M.fx and C_ACC or C_DIM)
  item(M.fy and "[y] flipY" or "[ ] flipY", "y", tx, y + 48, M.fy and C_ACC or C_DIM)
  item("prio " .. M.prio, "p", tx, y + 64, C_TEXT)

  rectfill(0, 16, W, 16, C_BG)
  print(string.format("sheet %dx%d  sprite at (%d,%d) %dx%d  %s%s", sw, sh, rx, ry, rs, rs, fill_tool and "fill" or "pencil",
                      sw ~= 128 and "  (OAM: 128 wide)" or ""), 8, 16, C_TEXT)
  hint({ { { "enter" }, edit and "choose" or "draw" }, { { "space" }, "paint" }, { { "b" }, "depth" }, { { "s" }, "sizes" },
         { { "l" }, "large" }, { { ",", "." }, "sub-palette" }, { { "+", "-" }, "channel" }, { { "i" }, "import PNG" },
         { { "e" }, "export" }, { { "tab" }, "more" } })
end

local function draw_soon(i)
  cls(C_BG)
  local t = TABS[i]
  local cx = W // 2
  local title = t[1] .. " - coming soon"
  print(title, cx - #title * 4, H // 2 - 40, C_ACC)
  print(t[3], cx - #t[3] * 4, H // 2 - 8, C_DIM)
  local s = "The tab is here already: the SDK is one app, each tool is one tab."
  print(s, max(8, cx - #s * 4), H // 2 + 24, C_DIM)
  hint({ { { "f2" }, "sprites" }, { { "esc" }, "menu" }, { { "ctrl", "s" }, "save" } })
end

----------------------------------------------------------------- the Code tab

-- The text of the project's source files (.s and .inc) is in /docs (doc_read, doc_write): a .bme is one file.
-- The project keeps only the names of the files and the main one (the line --code in its Lua section).
-- The helpers are in bm/shared/snescode.lua (colours, F1 texts, errors in plain words, snippets) and
-- bm/shared/asmbuf.lua (the text buffer); tests/snes/code_test.lua runs them on the PC.
local GUT = 56
local CC = { mn = 0x80C0FF, dir = 0xC890FF, label = 0xFFC050, hex = 0x80E0A0, num = 0xF0A070, cmt = 0x6A7288,
             str = 0xE8D878, op = 0xA8B0C8, txt = C_TEXT }
local C_ERRBG, C_CURLINE = 0x40202A, 0x1C2030

local function src_ok(n)
  local stem, ext = n:match("^([%w_%-]+)%.(%a+)$")
  return stem ~= nil and #stem <= 8 and (ext == "s" or ext == "inc")
end
local function cur_buf() return CD.cur and CD.files[CD.cur] end

local function code_geom()
  local pan = 0
  if CD.help then pan = #CD.help + 1
  elseif CD.show_errs and #CD.errs > 0 then pan = min(#CD.errs, 6) + 1 end
  local bottom = HINT_Y - pan * 16
  return 32, max(1, (bottom - 32) // 16), max(8, (W - GUT - 8) // 8), pan, bottom
end

local function wrap(text, cols)
  local out = {}
  for para in text:gmatch("[^\n]+") do
    local line = ""
    for w in para:gmatch("%S+") do
      if line ~= "" and #line + #w + 1 > cols then out[#out + 1] = line; line = "  " .. w
      else line = (line == "" and w) or (line .. " " .. w) end
    end
    if line ~= "" then out[#out + 1] = line end
  end
  return out
end

local function code_open(name, text_if_new, quiet)
  name = name:lower()
  if not CD.files[name] then
    local t = doc_read and doc_read(name)
    local b
    if t then b = BF.new(t) else b = BF.new(text_if_new or ""); b.dirty = true end
    CD.files[name] = b
    CD.order[#CD.order + 1] = name
    CD.main = CD.main or name
    if not quiet then P.dirty = true end
  end
  CD.cur = name
  CD.help = nil
  return CD.files[name]
end

-- the first visit: the files the project names, or main.s (from /docs, or a new one with the reset snippet)
local function code_ensure()
  if CD.cur then return end
  if CD.want_files and #CD.want_files > 0 then
    for _, n in ipairs(CD.want_files) do if src_ok(n) then code_open(n, "", true) end end
    if CD.want_main and CD.files[CD.want_main] then CD.main = CD.want_main end
    CD.cur = CD.main or CD.order[1]
  end
  if not CD.cur then
    code_open("main.s", SC.snippet("reset").text, true)
    CD.main = "main.s"
  end
end

local function code_save_all(quiet)
  if not doc_write then say("save: this console cannot write files", C_ERR); return false end
  local n = 0
  for _, name in ipairs(CD.order) do
    local b = CD.files[name]
    if b.dirty then
      local ok, e = doc_write(name, BF.text(b))
      if not ok then say("cannot save " .. name .. ": " .. tostring(e), C_ERR); return false end
      b.dirty = false
      n = n + 1
    end
  end
  if n > 0 and not quiet then say("saved " .. n .. " source file" .. (n > 1 and "s" or "") .. " in /docs", C_ACC) end
  return true
end

local function code_scroll_to(b, rows, middle)
  if middle then b.top = max(1, b.cy - rows // 2) end
  BF.scroll(b, rows, (select(3, code_geom())))
end

local function code_goto(file, line, col)
  file = file and file:lower()
  if not file or not src_ok(file) then return false end
  local b = code_open(file)
  BF.set_cursor(b, line or 1, col or 0)
  local _, rows = code_geom()
  code_scroll_to(b, rows, true)
  return true
end

local function rom_name()
  local stem = P.path and P.path:match("([^/]+)%.[^.]*$") or (CD.main or "main"):match("^(.-)%.[^.]*$") or "main"
  return stem:sub(1, 8):lower() .. ".sfc"
end

local function code_build()
  code_ensure()
  if not code_save_all(true) then return end
  local files = {}
  for n, b in pairs(CD.files) do files[n] = BF.text(b) end
  local main = CD.files[CD.main or ""] and CD.main or CD.cur
  local title = (P.path and P.path:match("([^/]+)%.[^.]*$")) or main:match("^(.-)%.[^.]*$")
  local rom, res = SC.build(files, main, title, function(n) return doc_read and doc_read(n) end)
  CD.help = nil
  if not rom then
    CD.errs, CD.esel, CD.show_errs = res, 1, true
    local e = res[1]
    if e and e.file and e.line then code_goto(e.file, e.line) end
    say(#res .. (#res == 1 and " error" or " errors") .. ": " .. (e and e.msg or "?"), C_ERR, 400)
    return
  end
  CD.errs, CD.show_errs = {}, false
  RN.lines, RN.built = res.lines, files
  local name = rom_name()
  if not doc_write then say("built, but this console cannot write the ROM file", C_ERR); return rom end
  local ok, e = doc_write(name, rom)
  if not ok then say("cannot write " .. name .. ": " .. tostring(e), C_ERR); return rom end
  local code = 0
  for _, s in ipairs(res.segments) do code = code + #s.data end
  CD.rom = { name = name, size = #rom }
  say(string.format("built %s in /docs: %d KB, %d bytes of code, %d labels", name, #rom // 1024, code, #res.symlist), C_ACC, 400)
  return rom
end

local function code_jump_error(d)
  if not CD.errs or #CD.errs == 0 then say("no errors: Ctrl+B builds", C_DIM); return end
  CD.esel = (CD.esel or 0) + d
  if CD.esel > #CD.errs then CD.esel = 1 elseif CD.esel < 1 then CD.esel = #CD.errs end
  CD.show_errs = true
  local e = CD.errs[CD.esel]
  if not code_goto(e.file, e.line) then say(e.msg, C_ERR, 300) end
end

local function word_under(b)
  local l = b.lines[b.cy]
  local w = SC.word_at(l, b.cx + 1)
  if not w and b.cx > 0 then w = SC.word_at(l, b.cx) end
  return w
end

local function label_def(name)
  for _, fname in ipairs(CD.order) do
    for _, d in ipairs(BF.labels(CD.files[fname])) do
      if d.name == name then return fname, d.line end
    end
  end
end

local function code_help()
  code_ensure()
  local b = cur_buf()
  local w = word_under(b)
  local inf = w and SC.info(w)
  if not inf then
    local m = SC.mnemonic_of(b.lines[b.cy])
    inf = m and SC.info(m)
  end
  if not inf then say("F1: put the cursor on an instruction (lda, sta, rep ...)", C_DIM); return end
  local _, _, cols = code_geom()
  local lines = {}
  for _, l in ipairs(SC.info_lines(inf)) do
    for _, w2 in ipairs(wrap(l, cols + 4)) do lines[#lines + 1] = w2 end
  end
  while #lines > 9 do lines[#lines] = nil end
  CD.help, CD.help_name = lines, inf.name
  CD.show_errs = false
  local _, rows = code_geom()
  code_scroll_to(b, rows)
end

local function code_labels_menu()
  code_ensure()
  local b = cur_buf()
  local w = word_under(b)
  if w then
    local f, ln = label_def(w)
    if f then code_goto(f, ln); say(w .. ": " .. f .. " line " .. ln, C_DIM); return end
  end
  local rows = {}
  for _, d in ipairs(BF.labels(b)) do
    local ln = d.line
    rows[#rows + 1] = { string.format("%-24s line %d", d.name, ln), function() code_goto(CD.cur, ln) end }
  end
  if #rows == 0 then say("no labels in " .. CD.cur .. " (a label ends with a colon)", C_DIM); return end
  choose("go to a label in " .. CD.cur, rows)
end

local function code_find_ask()
  code_ensure()
  ask("find in " .. CD.cur, CD.find or "", function(t)
    if t == "" then return end
    CD.find = t
    local b = cur_buf()
    if BF.find(b, t) then local _, rows = code_geom(); code_scroll_to(b, rows) else say("not found: " .. t, C_ERR) end
  end)
end
local function code_find_next(back)
  local b = cur_buf()
  if not b then return end
  if not CD.find or CD.find == "" then code_find_ask(); return end
  if BF.find(b, CD.find, back) then local _, rows = code_geom(); code_scroll_to(b, rows) else say("not found: " .. CD.find, C_ERR) end
end

local function code_snippets()
  code_ensure()
  local rows = {}
  for _, sn in ipairs(SC.snippets) do
    rows[#rows + 1] = { string.format("%-8s %s", sn.key, sn.name), function()
      local b = cur_buf()
      BF.insert_block(b, sn.text)
      CD.help = nil
      local _, r = code_geom()
      code_scroll_to(b, r)
      say(sn.about .. " Each line has its comment.", C_ACC, 500)
    end }
  end
  choose("insert a snippet at the cursor", rows)
end

local function new_source()
  ask("new file (8.3, .s or .inc)", "", function(t)
    t = t:lower():gsub("%s", "")
    if t == "" then return end
    if not t:find(".", 1, true) then t = t .. ".s" end
    if not src_ok(t) then say("a name like main.s or sprites.inc (up to 8 letters)", C_ERR); return end
    code_open(t, "")
  end)
end

local function close_source()
  local b = cur_buf()
  if not b then return end
  if b.dirty and not confirmed("closefile", CD.cur .. " has unsaved changes: choose again to close it") then return end
  local name = CD.cur
  CD.files[name] = nil
  for i, n in ipairs(CD.order) do if n == name then table.remove(CD.order, i); break end end
  if CD.main == name then CD.main = CD.order[1] end
  CD.cur = CD.order[1]
  P.dirty = true
end

local function code_files_menu()
  code_ensure()
  local names, seen = {}, {}
  for _, n in ipairs(CD.order) do names[#names + 1] = n; seen[n] = true end
  for _, d in ipairs(doc_list and doc_list() or {}) do
    local n = d.name:lower()
    if src_ok(n) and not seen[n] then names[#names + 1] = n; seen[n] = true end
  end
  table.sort(names)
  local rows = {}
  for _, n in ipairs(names) do
    rows[#rows + 1] = { n .. (CD.files[n] and "  (open)" or "") .. (CD.main == n and "  [main]" or ""), function() code_open(n) end }
  end
  rows[#rows + 1] = { "New file...", new_source }
  rows[#rows + 1] = { "Save all the source files", function() code_save_all() end }
  if CD.cur then
    rows[#rows + 1] = { "Make " .. CD.cur .. " the main file (the build starts there)", function() CD.main = CD.cur; P.dirty = true; say(CD.cur .. " is the main file", C_ACC) end }
    rows[#rows + 1] = { "Close " .. CD.cur, close_source }
  end
  choose("source files (in /docs)", rows)
end

----------------------------------------------------------------- the Run tab
-- Build the project of the Code tab, load the ROM in the machine and run it beside a debug panel. The helpers are in
-- bm/shared/snesrun.lua (flags, registers, hex view, breakpoints, lines and addresses); tests/snes/run_test.lua runs them on the PC.
-- With no breakpoint armed the loop is the normal one (snes_run is snes_frame then); the C core costs nothing more.
local BTNS = { { "b", 0x8000 }, { "y", 0x4000 }, { "select", 0x2000 }, { "start", 0x1000 }, { "up", 0x0800 },
               { "down", 0x0400 }, { "left", 0x0200 }, { "right", 0x0100 }, { "a", 0x0080 }, { "x", 0x0040 },
               { "l1", 0x0020 }, { "r1", 0x0010 } }
-- the keyboard (USB usages): WASD and the arrows move, U I O P are Y X B A, Q and E are L and R, "," Select, "." Start
local KBD = { { 0x1A, 0x0800 }, { 0x16, 0x0400 }, { 0x04, 0x0200 }, { 0x07, 0x0100 }, { 0x52, 0x0800 }, { 0x51, 0x0400 }, { 0x50, 0x0200 }, { 0x4F, 0x0100 },
              { 0x18, 0x4000 }, { 0x0C, 0x0040 }, { 0x12, 0x8000 }, { 0x13, 0x0080 }, { 0x14, 0x0020 }, { 0x08, 0x0010 }, { 0x36, 0x2000 }, { 0x37, 0x1000 } }

local function rn_pad()
  local v = 0
  if keydown then for _, e in ipairs(KBD) do if keydown(e[1]) then v = v | e[2] end end end
  for _, e in ipairs(BTNS) do if btn(e[1], 1) then v = v | e[2] end end
  if v & 0x0300 == 0x0300 then v = v & ~0x0300 end
  if v & 0x0C00 == 0x0C00 then v = v & ~0x0C00 end
  return v
end

-- the panel: its geometry (the picture is left, the panel right), also used to size the memory window
local function rn_geom()
  local top = 32
  local avail = HINT_Y - top - 4
  local pw = max(64, min(W // 2 - 12, avail * 256 // 224))
  local px = W // 2 + 4
  local cols = max(16, (W - px - 4) // 8)
  RN.bpr = clamp((cols - 10) // 4, 2, 16)
  RN.rows = max(2, (HINT_Y - (top + 16 * 10)) // 16)
  return top, pw, pw * 224 // 256, px, cols
end

local function rn_refresh()
  if RN.state == "off" then return end
  RN.regs = snes_regs()
  rn_geom()
  RN.mem = RR.read(RN.space, RN.addr, RN.rows * RN.bpr, { peek = snes_peek, mem = snes_mem }) or ""
end

local function rn_arm()
  if RN.state == "off" then return end
  snes_break(nil)
  for _, a in ipairs(RR.list(RN.set)) do snes_break(a) end
end

-- after a build the breakpoints keep their source lines: the new addresses come from the new map
local function rn_resolve()
  local set, info = {}, {}
  for a, i in pairs(RN.info) do
    if i.file then
      local na, l = RR.addr_of_line(RN.lines, i.file, i.line)
      if na and not set[na] then set[na] = true; info[na] = { file = i.file, line = l } end
    else
      set[a] = true; info[a] = i
    end
  end
  RN.set, RN.info = set, info
end

local function rn_build()
  local rom = code_build()
  if rom then rn_resolve() end
  return rom
end

local function rn_fresh()
  if not RN.lines or RN.scratch then return false end
  for n, b in pairs(CD.files) do if RN.built[n] ~= BF.text(b) then return false end end
  return true
end

local function rn_toggle(addr, file, line)
  local on, why = RR.toggle(RN.set, addr)
  if on == nil then say(why, C_ERR); return nil end
  RN.info[addr] = on and { file = file, line = line } or nil
  rn_arm()
  return on
end

local function rn_addr_text(a) return string.format("%02X:%04X", (a >> 16) & 0xFF, a & 0xFFFF) end

-- the CPU stopped (a breakpoint, a step or a pause): the registers, the memory, the line in the Code tab
local function rn_stopped(pc)
  RN.state = "stop"
  rn_refresh()
  local file, line = RR.line_of_addr(RN.lines, pc)
  RN.pcf, RN.pcln = file, line
  if file and not RN.scratch then code_goto(file, line) end
  say("stopped at " .. rn_addr_text(pc) .. (file and (" (" .. file .. " line " .. line .. ")") or ""), C_ACC, 300)
end

local function rn_run()
  local rom = rn_build()
  if not rom then tab = 1; return end
  local ok, err = snes_load(rom)
  if not ok then say("cannot load the ROM: " .. tostring(err), C_ERR); return end
  RN.state, RN.pcf, RN.pcln, RN.scratch = "run", nil, nil, nil
  rn_arm()
  rn_refresh()
  tab = 5
end

local function rn_step()
  if RN.state == "off" then say("F5 builds the code and runs it", C_DIM); return end
  if RN.state == "run" then rn_stopped(snes_regs().pc24); return end      -- the first F10 pauses
  rn_stopped(snes_step())
end

local function rn_f5()
  if RN.state == "stop" then RN.state = "run"; tab = 5; say("running", C_ACC, 100); return end
  if RN.state == "off" or tab == 1 then rn_run() else tab = 5 end
end

local function rn_break_ask()
  ask("breakpoint at (hex)", "", function(t)
    local a = RR.parse_addr(t)
    if not a then say("an address is hex: $8000 or 00:8000", C_ERR); return end
    local f, l = RR.line_of_addr(RN.lines, a)
    rn_toggle(a, f, l)
  end)
end

local function rn_f9()
  if tab == 1 then
    local b = cur_buf()
    if not b then say("Ctrl+E opens or makes a source file", C_DIM); return end
    for a, i in pairs(RN.info) do
      if i.file == CD.cur and i.line == b.cy then rn_toggle(a); say("breakpoint cleared", C_DIM); return end
    end
    if not rn_fresh() then
      say("building to find the address", C_DIM)
      if not rn_build() then return end
      if RN.state ~= "off" then say("rebuilt: Ctrl+B runs the new ROM", C_ACC, 300) end
    end
    local a, l = RR.addr_of_line(RN.lines, CD.cur, b.cy)
    if not a then say("no instruction on or after this line", C_ERR); return end
    for _, i in pairs(RN.info) do if i.file == CD.cur and i.line == l then say("line " .. l .. " has a breakpoint already", C_DIM); return end end
    if rn_toggle(a, CD.cur, l) then say("breakpoint at " .. rn_addr_text(a) .. ", line " .. l, C_ACC) end
  elseif tab == 5 then
    if RN.state == "stop" and RN.regs then
      local a = RN.regs.pc24
      local f, l = RR.line_of_addr(RN.lines, a)
      rn_toggle(a, f, l)
    else
      rn_break_ask()
    end
  end
end

local function rn_poke(text)
  local a, bytes = RR.parse_poke(text)
  if not a then say(bytes, C_ERR); return end
  for i, v in ipairs(bytes) do snes_poke(a + i - 1, v) end
  rn_refresh()
  say(string.format("wrote %d byte%s at %s", #bytes, #bytes == 1 and "" or "s", rn_addr_text(a)), C_ACC)
end

local function rn_goto(a)
  local size = RR.space_size[RN.space]
  a = clamp(a, 0, size - 1)
  RN.addr = a
  rn_refresh()
end

local function rn_space(name)
  RN.space = name
  RN.addr = name == "ram" and 0x7E0000 or 0
  rn_refresh()
end

local function rn_update()
  if RN.state ~= "run" or tab ~= 5 then return end      -- the machine runs while the Run tab shows
  local pc = snes_run(rn_pad(), 0, true)
  if pc then rn_stopped(pc) elseif frame % 4 == 0 then rn_refresh() end
end

local function rn_scroll(rows) rn_goto(RN.addr + rows * RN.bpr) end

local function run_key(k)
  if RN.state ~= "stop" then
    if k == " " and RN.state == "run" then rn_stopped(snes_regs().pc24) end     -- Space pauses; the other keys are the pad
    return
  end
  if k == "g" then
    ask("go to (hex)", "", function(t) local a = RR.parse_addr(t); if a then rn_goto(a) else say("an address is hex: $7E0010", C_ERR) end end)
  elseif k == "w" then ask("write address=bytes (hex)", "", rn_poke)
  elseif k == "b" then rn_break_ask()
  elseif k == "c" then RN.set, RN.info = {}, {}; rn_arm(); say("breakpoints cleared", C_DIM)
  elseif k == "\t" then
    local n = RR.spaces
    for i, s in ipairs(n) do if s == RN.space then rn_space(n[i % #n + 1]); break end end
  elseif k == "up" then rn_scroll(-1)
  elseif k == "down" then rn_scroll(1)
  elseif k == "pgup" then rn_scroll(-RN.rows)
  elseif k == "pgdn" then rn_scroll(RN.rows)
  elseif k == "^r" then snes_reset(); rn_stopped(snes_regs().pc24); RN.state = "run"
  end
end

local function run_mouse()
  if U.menu_open() then return end
  local z = U.at()
  if not z then return end
  if U.wheel ~= 0 and RN.state ~= "off" then rn_scroll(-U.wheel * 2) end
  if not U.pressed(0) then return end
  if z.kind == "rspace" then rn_space(z.a)
  elseif z.kind == "rgo" and RN.state ~= "off" then run_key("g")
  elseif z.kind == "rpoke" and RN.state ~= "off" then ask("write address=bytes (hex)", "", rn_poke)
  end
end

local function draw_run()
  cls(C_BG)
  local top, pw, ph, px, cols = rn_geom()
  if RN.state ~= "off" then
    if not RN.scr or RN.pw ~= pw then RN.scr = target_create(pw, ph); RN.pw, RN.ph = pw, ph end
    snes_draw(RN.scr, pw, ph)
    RN.scr:draw(8, top)
    rect(7, top - 1, pw + 2, ph + 2, C_GRID8)
  else
    rectfill(8, top, pw, ph, C_PANEL)
    print("F5 builds and runs", 16, top + 8, C_DIM)
  end
  local y = top
  local st = RN.state == "run" and "RUNNING" or RN.state == "stop" and "STOPPED" or "NOT RUNNING"
  print(st .. (RN.scratch and RN.state ~= "off" and "  (lesson example)" or ""), px, y, RN.state == "stop" and C_ERR or RN.state == "run" and C_ACC or C_DIM)
  if RN.state == "off" then
    print("Press F5 (or Ctrl+B) to build the", px, y + 24, C_DIM)
    print("project of the Code tab and run it.", px, y + 40, C_DIM)
  elseif RN.regs then
    local r = RN.regs
    for i, l in ipairs(RR.regs_text(r)) do print(l, px, y + i * 16, i == 4 and C_ACC or C_TEXT) end
    local words = RR.flags_words(r.p, r.e)
    print(words:sub(1, cols), px, y + 80, C_DIM)
    if #words > cols then print(words:sub(cols + 1, cols * 2), px, y + 96, C_DIM) end
    local f, l = RR.line_of_addr(RN.lines, r.pc24)
    local b = f and CD.files[f]
    local txt = b and b.lines[l] and b.lines[l]:gsub("^%s+", "") or ""
    print(((f and (l .. " ") or "") .. txt):sub(1, cols), px, y + 112, C_TEXT)
    local bl = {}
    for _, a in ipairs(RR.list(RN.set)) do bl[#bl + 1] = string.format("%04X", a & 0xFFFF) end
    print(("break " .. (#bl > 0 and table.concat(bl, " ") or "none")):sub(1, cols), px, y + 128, C_ERR)
  end
  -- the memory
  local my = y + 152
  local x = px
  for _, s in ipairs(RR.spaces) do
    local label = s:upper()
    local w = #label * 8 + 8
    if s == RN.space then rectfill(x - 2, my, w, 16, C_SEL) end
    U.zone(x - 2, my, w, 16, "rspace", s)
    print(label, x + 2, my, s == RN.space and 0xFFFFFF or C_DIM)
    x = x + w + 4
  end
  U.zone(x, my, 24, 16, "rgo"); print("go", x + 4, my, C_ACC); x = x + 32
  U.zone(x, my, 40, 16, "rpoke"); print("poke", x + 4, my, C_ACC)
  if RN.state ~= "off" then
    for i, l in ipairs(RR.hexdump(RN.mem, RN.addr, RN.bpr)) do
      if my + i * 16 + 16 > HINT_Y then break end
      print(l:sub(1, cols), px, my + i * 16, C_TEXT)
    end
  end
  hint({ { { "f5" }, RN.state == "stop" and "continue" or "run" }, { { "f9" }, "break" }, { { "f10" }, "step" }, { { "ctrl", "b" }, "build" },
         { { "g" }, "go to" }, { { "w" }, "poke" }, { { "tab" }, "memory" }, { { "esc" }, "menu" } })
end

local function code_key(k)
  code_ensure()
  if k == "f1" then code_help()
  elseif k == "^g" then code_labels_menu()
  elseif k == "^f" then code_find_ask()
  elseif k == "f6" then code_find_next(false)
  elseif k == "f7" then code_find_next(true)
  elseif k == "f8" then code_jump_error(1)
  elseif k == "^j" then code_snippets()
  elseif k == "^e" then code_files_menu()
  elseif k == "^w" then close_source()
  else
    local b = cur_buf()
    if not b then say("Ctrl+E opens or makes a source file", C_DIM); return end
    local _, rows = code_geom()
    if BF.key(b, k, rows - 1) then
      CD.help = nil
      code_scroll_to(b, rows)
    end
  end
end

local function code_mouse()
  local b = cur_buf()
  if not b or U.menu_open() then return end
  local top, rows, cols = code_geom()
  local z = U.at()
  if not z then return end
  if z.kind == "code" then
    local ly = clamp(b.top + (U.y - top) // 16, 1, #b.lines)
    local lx = max(0, b.left + (U.x - GUT + 4) // 8)
    if U.wheel ~= 0 then b.top = clamp(b.top - U.wheel * 3, 1, #b.lines); return end
    if U.pressed(0) then
      BF.set_cursor(b, ly, lx)
      b.mark = { cy = b.cy, cx = b.cx }
      CD.help = nil
      if U.double then
        local w = word_under(b)
        if w then
          local f, ln = label_def(w)
          if f then b.mark = nil; code_goto(f, ln) end
        end
      end
    elseif U.down(0) and U.moved and b.mark then
      BF.set_cursor(b, ly, lx, true)
      code_scroll_to(b, rows)
    end
  elseif z.kind == "ferr" and U.pressed(0) then
    CD.esel = z.a
    local e = CD.errs[z.a]
    if e then code_goto(e.file, e.line) end
  elseif z.kind == "ftab" and U.pressed(0) then
    if CD.files[z.a] then CD.cur = z.a; CD.help = nil end
  end
end

local function code_status()
  local b = cur_buf()
  if not b then return "Code: Ctrl+E opens or makes a source file" end
  local w = word_under(b)
  local inf = (w and SC.info(w)) or nil
  if inf then return inf.short .. "   (F1: more)" end
  return string.format("%s  line %d of %d, column %d%s", CD.cur, b.cy, #b.lines, b.cx + 1, CD.main == CD.cur and "  [main]" or "")
end

local function draw_code()
  code_ensure()
  cls(C_BG)
  local b = cur_buf()
  local top, rows, cols, pan, bottom = code_geom()
  -- the file strip
  rectfill(0, 16, W, 16, C_PANEL)
  local x = 8
  for _, n in ipairs(CD.order) do
    local f = CD.files[n]
    local label = n .. (f.dirty and "*" or "") .. (CD.main == n and " >" or "")
    local w = #label * 8 + 16
    if n == CD.cur then rectfill(x - 4, 16, w, 16, C_SEL) end
    U.zone(x - 4, 16, w, 16, "ftab", n)
    print(label, x + 4, 16, n == CD.cur and 0xFFFFFF or C_DIM)
    x = x + w + 4
  end
  if not b then
    print("no source file: Ctrl+E opens or makes one", 16, 56, C_DIM)
  else
    BF.scroll(b, rows, cols)
    local y0, x0, y1, x1 = BF.sel_range(b)
    RN.bpl, RN.pcl = {}, nil
    for _, i in pairs(RN.info) do if i.file == CD.cur then RN.bpl[i.line] = true end end
    if RN.state == "stop" and RN.pcf == CD.cur then RN.pcl = RN.pcln end
    local err_at = {}
    for _, e in ipairs(CD.errs) do
      if e.file == CD.cur and e.line and not err_at[e.line] then err_at[e.line] = e end
    end
    U.zone(GUT, top, W - GUT, rows * 16, "code")
    local blink = (frame // 20) % 2 == 0
    for r = 0, rows - 1 do
      local ln, y = b.top + r, top + r * 16
      if ln > #b.lines then break end
      local l = b.lines[ln]
      if ln == b.cy then rectfill(0, y, W, 16, C_CURLINE) end
      if y0 and ln >= y0 and ln <= y1 then       -- the selection
        local a = (ln == y0) and x0 or 0
        local z = (ln == y1) and x1 or #l + 1
        a, z = max(a, b.left), min(z, b.left + cols)
        if z > a then rectfill(GUT + (a - b.left) * 8, y, (z - a) * 8, 16, C_SEL) end
      end
      print(string.format("%4d", ln), 4, y, err_at[ln] and C_ERR or C_DIM)
      if err_at[ln] then print("!", GUT - 12, y, C_ERR) end
      if RN.bpl and RN.bpl[ln] then rectfill(GUT - 22, y + 4, 8, 8, 0xE03030) end      -- a breakpoint
      if RN.pcl == ln then print(">", GUT - 12, y, C_ACC) end                          -- the line the CPU stopped at
      for _, t in ipairs(SC.tokens(l)) do
        local a, z = max(t[1], b.left + 1), min(t[2], b.left + cols)
        if z >= a then print(l:sub(a, z), GUT + (a - 1 - b.left) * 8, y, CC[t[3]] or C_TEXT) end
      end
      if ln == b.cy and blink then rectfill(GUT + (b.cx - b.left) * 8, y, 2, 16, 0xFFFFFF) end
    end
    -- the error of the cursor line, under it (above it on the last row)
    local e = err_at[b.cy]
    if e then
      local r = b.cy - b.top
      local lines = wrap("! " .. e.msg, cols)
      local n = min(#lines, 2)
      local ry = (r + n <= rows - 1) and (top + (r + 1) * 16) or (top + (r - n) * 16)
      rectfill(0, ry, W, n * 16, C_ERRBG)
      for i = 1, n do print(lines[i]:sub(1, cols), GUT, ry + (i - 1) * 16, 0xFF9090) end
    end
  end
  -- the panel: the help of an instruction, or the list of errors
  if CD.help then
    rectfill(0, bottom, W, pan * 16, C_BAR)
    print("F1 " .. string.upper(CD.help_name or "") .. "   (Esc closes)", 8, bottom, C_ACC)
    for i, l in ipairs(CD.help) do print(l:sub(1, (W - 16) // 8), 8, bottom + i * 16, C_TEXT) end
  elseif pan > 0 then
    rectfill(0, bottom, W, pan * 16, C_BAR)
    print(#CD.errs .. (#CD.errs == 1 and " error" or " errors") .. "   (F8 next, Esc closes)", 8, bottom, C_ERR)
    for i = 1, pan - 1 do
      local e = CD.errs[i]
      local y = bottom + i * 16
      U.zone(0, y, W, 16, "ferr", i)
      if i == CD.esel then rectfill(0, y, W, 16, C_SEL) end
      print(((e.file or "?") .. ":" .. (e.line or "?") .. "  " .. e.msg):sub(1, (W - 16) // 8), 8, y, C_TEXT)
    end
  end
  hint({ { { "ctrl", "b" }, "build" }, { { "f1" }, "help" }, { { "ctrl", "g" }, "go to" }, { { "ctrl", "f" }, "find" },
         { { "ctrl", "j" }, "snippet" }, { { "ctrl", "e" }, "files" } })
end

----------------------------------------------------------------- the Learn tab
-- Lessons are read-only data (SNESLESSONS: bm/snes_sdk/lessons/*.txt, made into a Lua table by the Makefile). The tab never
-- changes the open files by itself. Each code block has three actions: Insert (at the cursor of the Code tab, Ctrl+Z takes it
-- back), Open (a new file in /docs, never over an old one) and Run (a scratch build in the Run tab; the project, its ROM and its
-- files stay as they are). The read position, the done marks and the notes are saved in the project (sneslearn.lua).
;(function()                                  -- a function of its own: the main chunk has no local left
local LESSONS = nil
local function lessons()
  if not LESSONS then
    LESSONS = {}
    for _, e in ipairs(SNESLESSONS or {}) do LESSONS[#LESSONS + 1] = SL.parse(e.name, e.text) end
  end
  return LESSONS
end

function LN.reset()
  LN.st = SL.new_state()
  LN.cur, LN.top, LN.sel, LN.split, LN.focus, LN.rows, LN.visited = 1, 1, 1, false, "lesson", nil, false
end

function LN.load(lua)
  LN.reset()
  LN.st = SL.parse_state(lua)
  LN.split = LN.st.split
  for i, L in ipairs(lessons()) do
    if L.name == LN.st.last then LN.cur, LN.top = i, SL.entry(LN.st, L.name).pos end
  end
end

function LN.sync()
  if not LN.visited then return end
  local L = lessons()[LN.cur]
  if L then SL.entry(LN.st, L.name).pos = LN.top; LN.st.last = L.name end
  LN.st.split = LN.split
end

local function learn_geom()
  local top = 32
  local nrows = max(1, (HINT_Y - top) // 16)
  if LN.split then return top, nrows, 8, max(16, (W // 2 - 20) // 8), 0, W // 2, W - W // 2 - 4 end
  return top, nrows, 224, max(16, (W - 232) // 8), 216, 0, 0
end

local function learn_rows()
  local ls = lessons()
  local L = ls[LN.cur]
  if not L then return {} end
  local _, _, _, cols = learn_geom()
  local key = LN.cur .. ":" .. cols
  if LN.rows_key ~= key then LN.rows, LN.rows_key = SL.layout(L, cols), key end
  return LN.rows
end

local function learn_clamp()
  local rows = learn_rows()
  local _, nrows = learn_geom()
  LN.top = clamp(LN.top, 1, max(1, #rows - nrows + 1))
  local L = lessons()[LN.cur]
  LN.sel = L and clamp(LN.sel, 1, max(1, #SL.blocks(L))) or 1
end

local function learn_scroll(d) LN.top = LN.top + d; learn_clamp() end

local function learn_show(i, row)
  local ls = lessons()
  if i < 1 or i > #ls then return end
  LN.sync()
  LN.visited = true
  LN.cur, LN.rows_key, LN.sel = i, nil, 1
  LN.top = row or SL.entry(LN.st, ls[i].name).pos
  LN.st.last = ls[i].name
  learn_clamp()
end

local function learn_select(n)
  LN.sel = n
  local rows = learn_rows()
  local _, nrows = learn_geom()
  for i, r in ipairs(rows) do
    if r.kind == "bar" and r.block == n then
      if i < LN.top or i + 3 > LN.top + nrows - 1 then LN.top = i - 1 end
      break
    end
  end
  learn_clamp()
end

local function learn_block(n)
  local L = lessons()[LN.cur]
  return L, L and SL.blocks(L)[n or LN.sel]
end

local function learn_insert(n)
  local L, blk = learn_block(n)
  if not blk then return end
  code_ensure()
  local b = cur_buf()
  if not b then say("Ctrl+E in the Code tab opens or makes a source file first", C_ERR); return end
  BF.insert_block(b, blk.text)
  CD.help = nil
  say(string.format("inserted in %s at line %d: Ctrl+Z in the Code tab takes it back", CD.cur, b.cy), C_ACC, 300)
end

local function learn_open(n)
  local L, blk = learn_block(n)
  if not blk then return end
  if not doc_write then say("this console cannot write files", C_ERR); return end
  local listed = {}
  for _, d in ipairs(doc_list and doc_list() or {}) do listed[d.name:lower()] = true end
  local function exists(name) name = name:lower(); return CD.files[name] ~= nil or listed[name] == true end
  local name, e = SL.open_as_file(L, blk.n, exists, function(nm, text) return doc_write(nm, text) end)
  if not name then say("cannot make the file: " .. tostring(e), C_ERR); return end
  local keep = CD.cur
  code_open(name)
  if keep then CD.cur = keep end                        -- you stay in your own file
  say(name .. " is a new file in /docs and in the project (Ctrl+E in the Code tab opens it)", C_ACC, 300)
end

local function learn_run(n)
  local L, blk = learn_block(n)
  if not blk then return end
  local rom, res = SL.run_alone(L, blk.n, SC.build)
  if not rom then say(tostring(res), C_ERR, 400); return end
  local ok, err = snes_load(rom)
  if not ok then say("cannot load the example: " .. tostring(err), C_ERR); return end
  snes_break(nil)
  RN.state, RN.scratch, RN.lines, RN.built, RN.pcf, RN.pcln = "run", true, res.lines, {}, nil, nil
  rn_refresh()
  tab = 5
  say("running the example of lesson " .. L.id .. " alone: your project and its ROM are not changed", C_ACC, 300)
end

local function learn_note(n)
  local L, blk = learn_block(n)
  if not blk then return end
  local on = SL.toggle_note(LN.st, SL.note_key(L, blk.n))
  P.dirty = true
  say(on and "block marked: l lists the marked blocks" or "mark removed", C_ACC, 150)
end

local function learn_done()
  local L = lessons()[LN.cur]
  if not L then return end
  local e = SL.entry(LN.st, L.name)
  e.done = not e.done
  P.dirty = true
  say("lesson " .. L.id .. (e.done and " is marked as done" or " is not done"), C_ACC, 150)
end

local function learn_search_ask()
  ask("search all the lessons", LN.query or "", function(t)
    LN.query = t
    local hits = SL.search(lessons(), t)
    if #hits == 0 then say("no lesson has: " .. t, C_ERR); return end
    local rows = {}
    for i = 1, min(#hits, 200) do
      local h = hits[i]
      local L = lessons()[h.lesson]
      rows[i] = { string.format("%02d %s%s", L.id, h.block and ("[block " .. h.block .. "] ") or "", h.text), function()
        learn_show(h.lesson)
        local r = SL.find_row(learn_rows(), t, 1)
        if h.block then learn_select(h.block) elseif r then LN.top = r - 1; learn_clamp() end
      end }
    end
    choose(#hits .. (#hits == 1 and " line" or " lines") .. " with: " .. t, rows)
  end)
end

local function learn_notes_list()
  local rows = {}
  for _, key in ipairs(LN.st.notes) do
    local name, n = key:match("^(.-):(%d+)$")
    for i, L in ipairs(lessons()) do
      if L.name == name then
        local blk = SL.blocks(L)[tonumber(n)]
        if blk then
          local first = blk.text:match("[^\n]*%S[^\n]*") or ""
          rows[#rows + 1] = { string.format("%02d block %d: %s", L.id, blk.n, first:gsub("^%s+", "")), function() learn_show(i); learn_select(blk.n) end }
        end
      end
    end
  end
  if #rows == 0 then say("no marked blocks yet: m marks the selected block", C_DIM); return end
  choose("marked blocks (Notes)", rows)
end

local function learn_view()                       -- Tab: lesson list > lesson and Code buffer > the Code buffer typing > list
  if not LN.split then LN.split, LN.focus = true, "lesson"; code_ensure()
  elseif LN.focus == "lesson" then LN.focus = "code"
  else LN.split, LN.focus = false, "lesson" end
  LN.rows_key = nil
  learn_clamp()
end

function LN.key(k)
  learn_clamp()
  if k == "\t" then learn_view(); return end
  if k == "^f" and not (LN.split and LN.focus == "code") then learn_search_ask(); return end
  if LN.split and LN.focus == "code" then
    local b = cur_buf()
    if not b then say("Ctrl+E in the Code tab opens or makes a source file first", C_DIM); return end
    local _, nrows = learn_geom()
    if BF.key(b, k, nrows - 3) then CD.help = nil end
    return
  end
  local _, nrows = learn_geom()
  local L = lessons()[LN.cur]
  if k == "up" then learn_scroll(-1)
  elseif k == "down" then learn_scroll(1)
  elseif k == "pgup" then learn_scroll(-(nrows - 1))
  elseif k == "pgdn" or k == " " then learn_scroll(nrows - 1)
  elseif k == "home" then LN.top = 1; learn_clamp()
  elseif k == "end" then LN.top = 1e9; learn_clamp()
  elseif k == "left" or k == "[" then learn_show(LN.cur - 1)
  elseif k == "right" or k == "]" then learn_show(LN.cur + 1)
  elseif k == "n" and L then learn_select(min(LN.sel + 1, max(1, #SL.blocks(L))))
  elseif k == "p" and L then learn_select(max(LN.sel - 1, 1))
  elseif k == "i" then learn_insert()
  elseif k == "o" then learn_open()
  elseif k == "r" or k == "\n" then
    local _, blk = learn_block()
    if blk and blk.run then learn_run() else learn_insert() end
  elseif k == "m" then learn_note()
  elseif k == "d" then learn_done()
  elseif k == "l" then learn_notes_list()
  end
end

function LN.pad()
  if rp[2] then learn_scroll(-1) elseif rp[3] then learn_scroll(1)
  elseif rp[0] then learn_show(LN.cur - 1) elseif rp[1] then learn_show(LN.cur + 1)
  elseif btnp(4) then local _, blk = learn_block(); if blk and blk.run then learn_run() else learn_insert() end
  elseif btnp(5) then tab = 1
  elseif tap[6] then learn_insert()
  elseif tap[7] then local L = lessons()[LN.cur]; learn_select(LN.sel % max(1, L and #SL.blocks(L) or 1) + 1) end
end

function LN.mouse()
  if U.menu_open() then return end
  local z = U.at()
  if not z then return end
  local top, nrows, rx0, cols, lw, cx0 = learn_geom()
  if U.wheel ~= 0 then
    local b = cur_buf()
    if LN.split and U.x >= cx0 and b then b.top = clamp(b.top - U.wheel * 3, 1, #b.lines) else learn_scroll(-U.wheel * 3) end
    return
  end
  if not U.pressed(0) then return end
  if z.kind == "llist" then learn_show(z.a); LN.focus = "lesson"
  elseif z.kind == "lbtn" then
    LN.sel = z.a
    if z.b == "insert" then learn_insert(z.a) elseif z.b == "open" then learn_open(z.a)
    elseif z.b == "run" then learn_run(z.a) elseif z.b == "note" then learn_note(z.a) end
  elseif z.kind == "lcode" then learn_select(z.a); LN.focus = "lesson"
  elseif z.kind == "lread" then LN.focus = "lesson"
  elseif z.kind == "ldone" then learn_done()
  elseif z.kind == "lpane" then
    LN.focus = "code"
    local b = cur_buf()
    if b then
      local ly = clamp(b.top + (U.y - 48) // 16, 1, #b.lines)
      BF.set_cursor(b, ly, max(0, b.left + (U.x - cx0 - 40 + 4) // 8))
    end
  end
end

function LN.status()
  local L = lessons()[LN.cur]
  if not L then return "Learn: no lessons in this build" end
  local e = SL.entry(LN.st, L.name)
  local n = #SL.blocks(L)
  return string.format("Lesson %d of %d: %s%s   block %d of %d", LN.cur, #lessons(), L.title, e.done and "  [done]" or "", min(LN.sel, n), n)
end

function LN.draw()
  LN.visited = true
  cls(C_BG)
  local ls = lessons()
  local top, nrows, rx0, cols, lw, cx0, cw = learn_geom()
  if #ls == 0 then
    print("no lessons in this build (SNESLESSONS is empty)", 16, 48, C_ERR)
    hint({ { { "esc" }, "code" } })
    return
  end
  learn_clamp()
  local L = ls[LN.cur]
  local rows = learn_rows()
  if not LN.split then                            -- the list of the lessons
    rectfill(0, 16, lw, HINT_Y - 16, C_PANEL)
    local first = clamp(LN.cur - nrows // 2, 1, max(1, #ls - nrows + 1))
    for i = first, min(#ls, first + nrows - 1) do
      local y = top + (i - first) * 16
      local e = LN.st.lessons[ls[i].name]
      U.zone(0, y, lw, 16, "llist", i)
      if i == LN.cur then rectfill(0, y, lw, 16, C_SEL) end
      local t = string.format("%s%2d %s", (e and e.done) and "*" or " ", ls[i].id, ls[i].title)
      print(t:sub(1, lw // 8 - 1), 4, y, i == LN.cur and 0xFFFFFF or ((e and e.done) and C_ACC or C_TEXT))
    end
  end
  U.zone(0, 16, W, HINT_Y - 16, "lread")
  local width = cols * 8 + 8
  local notes = LN.st.notes
  for r = 0, nrows - 1 do
    local row = rows[LN.top + r]
    if not row then break end
    local y, k = top + r * 16, row.kind
    if k == "title" then
      print(row.text:sub(1, cols), rx0, y, C_ACC)
    elseif k == "goal" then print(row.text:sub(1, cols), rx0, y, 0xB0C0E0)
    elseif k == "h" then print(row.text:sub(1, cols), rx0, y, 0x80C0FF)
    elseif k == "p" then print(row.text:sub(1, cols), rx0, y, C_TEXT)
    elseif k == "try" then print(row.text:sub(1, cols), rx0, y, 0x80E080)
    elseif k == "link" then print(row.text:sub(1, cols), rx0, y, 0x70A0F0)
    elseif k == "bar" then
      local blk = SL.blocks(L)[row.block]
      rectfill(rx0 - 4, y, width, 16, row.block == LN.sel and C_SEL or C_BAR)
      local label = (blk.run and "program " or "text ") .. row.block
      print(label, rx0, y, 0xFFFFFF)
      local bx = rx0 + (#label + 2) * 8
      local acts = { { "Insert", "insert" }, { "Open", "open" } }
      if blk.run then acts[#acts + 1] = { "Run", "run" } end
      acts[#acts + 1] = { SL.has_note(LN.st, SL.note_key(L, row.block)) and "Noted" or "Note", "note" }
      for _, a in ipairs(acts) do
        local w = #a[1] * 8 + 8
        if bx + w < rx0 + width - 4 then
          U.zone(bx - 2, y, w, 16, "lbtn", row.block, a[2])
          rectfill(bx - 2, y + 1, w, 14, 0x3C4670)
          print(a[1], bx + 2, y, C_ACC)
          bx = bx + w + 4
        end
      end
    elseif k == "code" then
      rectfill(rx0 - 4, y, width, 16, row.block == LN.sel and 0x1C2440 or C_PANEL)
      U.zone(rx0 - 4, y, width, 16, "lcode", row.block)
      for _, t in ipairs(SC.tokens(row.text)) do
        local a, z = t[1], min(t[2], cols)
        if z >= a then print(row.text:sub(a, z), rx0 + (a - 1) * 8, y, CC[t[3]] or C_TEXT) end
      end
    end
  end
  -- the done mark, at the right of the first line
  local e = SL.entry(LN.st, L.name)
  if LN.top == 1 and not LN.split then
    local t = e.done and "[done]" or "[ ]"
    U.zone(rx0 + width - #t * 8 - 8, top, #t * 8 + 8, 16, "ldone")
    print(t, rx0 + width - #t * 8 - 4, top, e.done and C_ACC or C_DIM)
  end
  if #rows > nrows then                           -- where we are in the lesson
    local h = HINT_Y - top
    rectfill(rx0 + width - 2, top + (LN.top - 1) * h // #rows, 3, max(4, nrows * h // #rows), C_GRID8)
  end
  if LN.split then                                -- the Code buffer beside the lesson
    rectfill(cx0 - 6, 16, 2, HINT_Y - 16, LN.focus == "code" and C_ACC or C_GRID)
    code_ensure()
    local b = cur_buf()
    local prows, pcols = nrows - 1, max(8, (cw - 40) // 8)
    print(CD.cur and (CD.cur .. (b and b.dirty and "*" or "")) or "no file", cx0, top, LN.focus == "code" and C_ACC or C_DIM)
    if b then
      BF.scroll(b, prows, pcols)
      U.zone(cx0, 48, cw, prows * 16, "lpane")
      local blink = (frame // 20) % 2 == 0
      for r = 0, prows - 1 do
        local ln, y = b.top + r, 48 + r * 16
        if ln > #b.lines then break end
        local l = b.lines[ln]
        if ln == b.cy then rectfill(cx0, y, cw, 16, C_CURLINE) end
        print(string.format("%4d", ln), cx0, y, C_DIM)
        for _, t in ipairs(SC.tokens(l)) do
          local a, z = max(t[1], b.left + 1), min(t[2], b.left + pcols)
          if z >= a then print(l:sub(a, z), cx0 + 40 + (a - 1 - b.left) * 8, y, CC[t[3]] or C_TEXT) end
        end
        if ln == b.cy and LN.focus == "code" and blink then rectfill(cx0 + 40 + (b.cx - b.left) * 8, y, 2, 16, 0xFFFFFF) end
      end
    end
  end
  hint({ { { "i" }, "insert" }, { { "o" }, "open file" }, { { "r" }, "run" }, { { "m" }, "note" }, { { "d" }, "done" },
         { { "tab" }, LN.split and "view" or "split" }, { { "ctrl", "f" }, "search" }, { { "esc" }, "code" } })
end
end)()

local function draw_pick()
  local p = pick
  local rows = min(#p.rows, 14)
  local h = (rows + 2) * 16
  local y0 = max(32, (H - h) // 32 * 16)
  U.zone(0, 0, W, H, "shade")
  U.zone(40, y0, W - 80, h, "box")
  rectfill(40, y0, W - 80, h, C_PANEL)
  rect(40, y0, W - 80, h, C_ACC)
  print(p.title, 56, y0, C_ACC)
  local first = clamp(p.sel - rows // 2, 1, max(1, #p.rows - rows + 1))
  for i = first, min(#p.rows, first + rows - 1) do
    local y = y0 + 16 + (i - first) * 16
    U.zone(48, y, W - 96, 16, "pick", i)
    if i == p.sel then rectfill(48, y, W - 96, 16, C_SEL) end
    print(p.rows[i][1]:sub(1, 66), 56, y, i == p.sel and 0xFFFFFF or C_TEXT)
  end
end

local function draw_input()
  U.zone(0, 0, W, H, "shade")
  local x0 = (W - 480) // 16 * 8
  U.zone(x0, 144, 480, 64, "box")
  rectfill(x0, 144, 480, 64, C_PANEL)
  rect(x0, 144, 480, 64, C_ACC)
  print(input.label, x0 + 16, 160, C_DIM)
  U.zone(x0 + 8, 176, 464, 16, "field")
  U.line_draw(input, x0 + 16, 176, C_TEXT, (frame // 20) % 2 == 0)
  input.x0 = x0 + 16
end

----------------------------------------------------------------- input

local MENU
local function menu_actions()
  choose("bm SNES SDK" .. (P.path and (" - " .. P.path) or ""), MENU)
end
MENU = {
  { "Continue", function() end },
  { "New project   (Ctrl+N)", new_other },
  { "Open...   (Ctrl+O)", open_other },
  { "Save   (Ctrl+S)", save_now },
  { "Save as...   (Ctrl+Shift+S)", save_as },
  { "Sheet size...", resize },
  { LY.label(), LY.switch },
  { "Exit bm SNES SDK", function() if not P.dirty or confirmed("exit", "unsaved changes: choose again to exit") then quit() end end },
}
LY.screen_row = MENU[#MENU - 1]

local function global_key(k)
  if k == "^b" then rn_run(); return true
  elseif k == "f5" then rn_f5(); return true
  elseif k == "f9" and (tab == 1 or tab == 5) then rn_f9(); return true
  elseif k == "f10" and (tab == 1 or tab == 5) then rn_step(); return true
  elseif tab == 6 and k == "f1" then tab = 1; return true             -- F1 and Esc: back to the Code tab, same cursor
  end
  for i, t in ipairs(TABS) do
    if k == t[2] then
      if i == 1 and tab == 1 then return false end        -- F1 in the Code tab is the help of an instruction
      tab = i
      return true
    end
  end
  if k == "esc" then
    if tab == 6 then tab = 1
    elseif tab == 1 and (CD.help or CD.show_errs) then CD.help, CD.show_errs = nil, false else menu_actions() end
    return true
  elseif k == "^s" then
    if tab == 1 or tab == 6 then code_save_all() end
    if (tab ~= 1 and tab ~= 6) or P.path then save_now() end
    return true
  elseif k == "^S" then save_as(); return true
  elseif k == "^o" then open_other(); return true
  elseif k == "^n" then new_other(); return true
  elseif k == "^z" then if tab == 1 or (tab == 6 and LN.split and LN.focus == "code") then return false end; if tab == 2 then do_undo(undo, redo, "undo") end; return true
  elseif k == "^y" then if tab == 1 or (tab == 6 and LN.split and LN.focus == "code") then return false end; if tab == 2 then do_undo(redo, undo, "redo") end; return true
  elseif k == "\t" and tab == 2 then sprites_actions(); return true
  end
  return false
end

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

local function sprites_mouse()
  local z = U.at()
  if not z then return end
  if z.kind == "view" then
    if U.pressed(0) then
      local x = clamp(floor((U.x - SV_X) / zoom_sheet), 0, sw - 1)
      local y = clamp(floor((U.y - SV_Y) / zoom_sheet), 0, sh - 1)
      local nx, ny = x // rs * rs, y // rs * rs
      if U.double and nx == rx and ny == ry then edit = true end
      rx, ry = nx, ny
      fit_sprite()
    end
  elseif z.kind == "pcol" then
    if U.pressed(0) then M.sel = z.a end
    if U.wheel ~= 0 then chan_add(U.wheel > 0 and 1 or -1) end
  elseif z.kind == "chan" then
    if U.pressed(0) then M.ch = z.a; chan_set(z.a, floor((U.x - z.x) / (z.w / 32))) end
    if U.wheel ~= 0 then M.ch = z.a; chan_add(U.wheel > 0 and 1 or -1) end
  elseif z.kind == "ed" then
    local x, y = clamp((U.x - ED_X) // ED_Z, 0, rs - 1), clamp((U.y - ED_Y) // ED_Z, 0, rs - 1)
    if U.pressed(0) or U.pressed(2) then begin(); edit = true; ex, ey = x, y end
    if U.down(0) or U.down(2) then
      ex, ey = x, y
      if fill_tool then
        if U.pressed(0) or U.pressed(2) then flood(x, y, U.down(2) and 0 or (M.sel - 1)) end
      else
        put_pixel(x, y, U.down(2) and 0 or (M.sel - 1))
      end
    end
  end
end

local function sprites_pad()
  if btn(6) then                               -- X held: the colour and its channel
    if rp[0] then sprites_key("[") end
    if rp[1] then sprites_key("]") end
    if rp[2] then sprites_key("+") end
    if rp[3] then sprites_key("-") end
    return
  end
  if rp[0] then sprites_key("left") end
  if rp[1] then sprites_key("right") end
  if rp[2] then sprites_key("up") end
  if rp[3] then sprites_key("down") end
  if btnp(4) then if edit then sprites_key(" ") else sprites_key("\n") end end
  if btnp(5) and edit then edit = false end
  if tap[6] then sprites_actions() end
  if tap[7] then menu_actions() end
end

local HELP = {
  { "f1 ... f5 / ctrl l", "the tabs: Code, Sprites, Map, Music, Run, Learn" },
  { "esc", "the menu (new, open, save, sheet size)" },
  { "ctrl s / o / n", "save, open, new" },
  { "ctrl z / y", "undo, redo" },
  "Code",
  { "ctrl b", "build: the ROM goes to /docs; errors in plain words" },
  { "f1", "help on the instruction under the cursor" },
  { "ctrl g / double click", "go to a label" },
  { "ctrl f / f6 / f7", "find; next; previous" },
  { "f8", "next error" },
  { "ctrl j", "insert a snippet (reset, nmi, dma, joypad, colour)" },
  { "ctrl e / ctrl w", "the source files; close the file" },
  { "ctrl q", "start or stop a selection (the mouse drags one too)" },
  { "ctrl c / x / v / k / d", "copy, cut, paste, cut line, copy line down" },
  "Learn",
  { "up down / pgup pgdn", "scroll the lesson; left right: the other lesson" },
  { "n / p", "select the next / previous code block" },
  { "i / o / r", "insert the block in your file; open it as a new file; run it alone in the Run tab" },
  { "m / l / d", "mark the block (Notes); list the marks; mark the lesson done" },
  { "tab", "lesson + Code buffer side by side; Tab again types in the Code buffer; again: back" },
  { "ctrl f / esc / f1", "search all the lessons; back to the Code tab" },
  "Sprites",
  { "arrows", "the sprite (or the pixel while drawing)" },
  { "enter / space", "draw on / off; paint the pixel" },
  { "del", "clear the pixel (or the sprite)" },
  { "g", "pencil or fill" },
  { "b / s / l", "depth 2, 4, 8 bpp; sizes; small or large" },
  { ", / .", "the sub-palette (OAM palette bits)" },
  { "[ / ]", "the colour of the sub-palette" },
  { "1 2 3 / + -", "the channel R G B and its value (5 bits)" },
  { "a", "the sprite's tiles use this sub-palette" },
  { "h / v", "flip the sprite" },
  { "x / y / p", "OAM preview: flip X, flip Y, priority" },
  { "ctrl c / ctrl v", "copy, paste the sprite" },
  { "i / shift l", "import a PNG (quantise) / read a .pal" },
  { "e", "export .chr and .pal to /docs" },
  { "shift r", "the sheet size" },
  "pad",
  { "DPAD A", "the sprite; draw the pixel" },
  { "X LEFTRIGHT", "the colour" },
  { "X UPDOWN", "the channel value" },
  { "X", "tap: next sub-palette" },
  { "Y", "tap: the menu" },
}

function _init()
  keyp()                                   -- typing on: the keyboard types
  U.wants()
  if keyhelp then keyhelp(HELP, "bm SNES SDK") end
  local a = cart_arg()
  if a and a.path and open_file(a.path) then return end
  new_project()
end

function _update()
  LY.poll()
  frame = frame + 1
  if busy then
    if busy.wait > 0 then busy.wait = busy.wait - 1 else local fn = busy.fn; busy = nil; fn() end
    return
  end
  if msg_t > 0 then msg_t = msg_t - 1 end
  if confirm_t > 0 then confirm_t = confirm_t - 1 end
  read_pad()
  rn_update()
  U.update()
  if U.menu_update() then return end
  if U.on and not U.keys() then
    if pick or input then
      if pick and U.wheel ~= 0 then pick.sel = clamp(pick.sel - U.wheel, 1, #pick.rows) end
      local z = U.click(0)
      if z and z.kind == "pick" and pick then
        if z.a == pick.sel or U.double then U.press("\n") else pick.sel = z.a end
      elseif z and z.kind == "shade" then U.press("esc")
      elseif z and z.kind == "field" and input then U.line_click(input, input.x0 or 96, 8) end
    elseif tab == 2 then sprites_mouse()
    elseif tab == 1 then code_mouse()
    elseif tab == 6 then LN.mouse()
    elseif tab == 5 then run_mouse() end
  end
  while true do
    local k = keyp()
    if not k then break end
    if input then
      if k == "\n" then local d, t = input.done, input.text; input = nil; d(t)
      elseif k == "esc" then input = nil
      else U.line_key(input, k, 40) end
    elseif pick then
      local p = pick
      if k == "up" then p.sel = (p.sel - 2) % #p.rows + 1
      elseif k == "down" then p.sel = p.sel % #p.rows + 1
      elseif k == "esc" or k == "back" then pick = nil
      elseif k == "\n" or k == "ok" then pick = nil; p.rows[p.sel][2]() end
    elseif not global_key(k) then
      if tab == 2 then sprites_key(k) elseif tab == 1 then code_key(k) elseif tab == 6 then LN.key(k) elseif tab == 5 then run_key(k) end
    end
  end
  if input then
    if btnp(5) then input = nil end
  elseif pick then
    if rp[2] then pick.sel = (pick.sel - 2) % #pick.rows + 1 elseif rp[3] then pick.sel = pick.sel % #pick.rows + 1
    elseif btnp(4) then local p = pick; pick = nil; p.rows[p.sel][2]() elseif btnp(5) then pick = nil end
  elseif tab == 2 then sprites_pad()
  elseif tab == 6 then LN.pad()
  elseif tap[7] then menu_actions() end
end

function _draw()
  LY.poll()
  U.begin()
  if tab == 2 then draw_sprites() elseif tab == 1 then draw_code() elseif tab == 5 then draw_run() elseif tab == 6 then LN.draw() else draw_soon(tab) end

  rectfill(0, 0, W, 16, C_BAR)                 -- the tabs: each with its key
  local x = U.bar_title("bm SNES SDK", C_ACC)
  for i, t in ipairs(TABS) do
    local ctrl = t[2]:sub(1, 1) == "^"           -- the Learn tab: Ctrl+L (no key glyph for it)
    local lx = snap8(x + (ctrl and 16 or prompt(t[2])) + 3)
    local w = lx + #t[1] * 8 - x
    if i == tab then rectfill(x - 4, 0, w + 8, 16, C_SEL) end
    if ctrl then U.zone(x - 4, 0, w + 8, 16, "key", t[2]); print("^" .. t[2]:sub(2):upper(), x, 0, C_DIM)
    else U.key_zone(x - 4, 0, w + 8, 16, t[2]); prompt(t[2], x, 0) end
    print(t[1], lx, 0, i == tab and 0xFFFFFF or C_DIM)
    x = x + w + 12
  end
  U.bar_file(P.path or "new project", P.dirty, x, W, C_ACC, C_DIM)

  rectfill(0, STATUS_Y, W, H - STATUS_Y, C_BAR)
  local status = msg_t > 0 and msg or tab == 1 and code_status() or tab == 6 and LN.status() or string.format("%s  colour %d  sub-palette %d  %d bpp", tab == 2 and "Sprites" or TABS[tab][1],
                                                      M.sel - 1, M.cur, M.bpp)
  print(status:sub(1, W // 8 - 1), 0, STATUS_Y, (msg_t > 0 and msg_c) or C_TEXT)
  if pick then draw_pick() end
  if input then draw_input() end
  if busy then
    local w = #busy.text * 8 + 32
    local bx = (W - w) // 16 * 8
    rectfill(bx, 160, w, 48, C_BAR)
    rect(bx, 160, w, 48, C_ACC)
    print(busy.text, bx + 16, 176, C_ACC)
  end
  U.menu_draw()
end
