-- bm SNES SDK: one app to make SNES games, with tabs: Code | Sprites | Map | Music | Run.
-- This step has the frame (the tabs, the project, the save) and the Sprites tab. The other tabs say
-- "coming soon". One SDK, many tabs: a new tool of the SDK is a new tab here, not a new app.
--
-- The project is a .bme in /bme. The sprite sheet is its SHEET8 (the colours of the sub-palettes first);
-- the rest of the Sprites data (depth, sizes, sub-palettes, the sub-palette of each tile) is text between
-- the marks "-- snes-sdk:begin" and "-- snes-sdk:end" in its Lua section (read back when it opens).
-- Keys: F1..F5 the tabs, Esc the menu, Ctrl+S save, Ctrl+O open, Ctrl+N new, F12 (held) the keys.
-- The converters are in carts/shared/snesgfx.lua (no interface; tests/snes/snesgfx_test.lua).

local LY = require("bmlib").layout
LY.auto()
local W, H = SCREEN_W, SCREEN_H
local U = require "bmui"
-- the converters: the build puts the module in the global SNESGFX after this file, so G looks it up at the first call
local G = setmetatable({}, { __index = function(_, k) return (SNESGFX or require "snesgfx")[k] end })

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
}
local tab = 2

----------------------------------------------------------------- state

local P = { path = nil, title = "", dirty = false, lua = "" }
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
    local tps = line:match("^%-%-tp (.*)$")
    if tps then for t, s in tps:gmatch("(%d+)=(%d+)") do M.tp[tonumber(t)] = tonumber(s) end end
  end
  return true
end

-- the project text of the Lua section: the old text with the data between the marks replaced
local function project_lua()
  local old = P.lua or ""
  local a = old:find(BEGIN, 1, true)
  local b = a and select(2, old:find(END, a, true))
  if a and b then return old:sub(1, a - 1) .. meta_text() .. old:sub(b + 1) end
  if old == "" then old = VIEWER end
  return old .. "\n" .. meta_text() .. "\n"
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
  sw, sh = cart_sheet()
  P = { path = path, title = p.title or "", dirty = false, lua = p.lua or "" }
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
  for i, t in ipairs(TABS) do
    if k == t[2] then tab = i; return true end
  end
  if k == "esc" then menu_actions(); return true
  elseif k == "^s" then save_now(); return true
  elseif k == "^S" then save_as(); return true
  elseif k == "^o" then open_other(); return true
  elseif k == "^n" then new_other(); return true
  elseif k == "^z" then if tab == 2 then do_undo(undo, redo, "undo") end; return true
  elseif k == "^y" then if tab == 2 then do_undo(redo, undo, "redo") end; return true
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
  { "f1 ... f5", "the tabs: Code, Sprites, Map, Music, Run" },
  { "esc", "the menu (new, open, save, sheet size)" },
  { "ctrl s / o / n", "save, open, new" },
  { "ctrl z / y", "undo, redo" },
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
    elseif tab == 2 then sprites_mouse() end
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
    elseif not global_key(k) and tab == 2 then sprites_key(k) end
  end
  if input then
    if btnp(5) then input = nil end
  elseif pick then
    if rp[2] then pick.sel = (pick.sel - 2) % #pick.rows + 1 elseif rp[3] then pick.sel = pick.sel % #pick.rows + 1
    elseif btnp(4) then local p = pick; pick = nil; p.rows[p.sel][2]() elseif btnp(5) then pick = nil end
  elseif tab == 2 then sprites_pad()
  elseif tap[7] then menu_actions() end
end

function _draw()
  LY.poll()
  U.begin()
  if tab == 2 then draw_sprites() else draw_soon(tab) end

  rectfill(0, 0, W, 16, C_BAR)                 -- the tabs: each with its key
  local x = U.bar_title("bm SNES SDK", C_ACC)
  for i, t in ipairs(TABS) do
    local lx = snap8(x + prompt(t[2]) + 3)
    local w = lx + #t[1] * 8 - x
    if i == tab then rectfill(x - 4, 0, w + 8, 16, C_SEL) end
    U.key_zone(x - 4, 0, w + 8, 16, t[2])
    prompt(t[2], x, 0)
    print(t[1], lx, 0, i == tab and 0xFFFFFF or C_DIM)
    x = x + w + 12
  end
  U.bar_file(P.path or "new project", P.dirty, x, W, C_ACC, C_DIM)

  rectfill(0, STATUS_Y, W, H - STATUS_Y, C_BAR)
  local status = msg_t > 0 and msg or string.format("%s  colour %d  sub-palette %d  %d bpp", tab == 2 and "Sprites" or TABS[tab][1],
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
