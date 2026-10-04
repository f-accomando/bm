-- bm Pixel (carts/pixel) on the PC: stand-ins for the bm API (a folder is
-- the SD card; the sheet is a table of RGB565 colours as the kernel keeps
-- them; cart_write writes the sheet as the kernel does, SHEET8 with the
-- palette first when it fits), keys typed into it, and checks on what it
-- draws in the sheet and writes. The files are read again by bm Studio's
-- parser (check_pixel.js) and the kernel's (test_bm).
--
--   luahost tests/studio/pixel_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/carts/village.bm and demo.bm must be there.

local ROOT, SD = arg[1] or ".", arg[2] or "build/pixel-sd"
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

----------------------------------------------------------------- .bm files

local function crc32(s)
  local crc = 0xFFFFFFFF
  for i = 1, #s do
    crc = crc ~ s:byte(i)
    for _ = 1, 8 do crc = (crc >> 1) ~ (0xEDB88320 & -(crc & 1)) end
  end
  return (~crc) & 0xFFFFFFFF
end

local function read_file(p)
  local f = io.open(p, "rb")
  if not f then return nil end
  local d = f:read("a")
  f:close()
  return d
end

local function sections_of(data)
  local out = {}
  for i = 0, data:byte(18) - 1 do
    local t, off, size = string.unpack("<I4I4I4", data, 129 + i * 16)
    out[#out + 1] = { t, data:sub(off + 1, off + size) }
  end
  return out
end

local function section(data, t)
  for _, s in ipairs(sections_of(data)) do if s[1] == t then return s[2] end end
end

local function pack_cart(title, author, w, secs)
  local tab, body = {}, {}
  local pos = 128 + 16 * #secs
  for _, s in ipairs(secs) do
    tab[#tab + 1] = string.pack("<I4I4I4I4", s[1], pos, #s[2], 0)
    local pad = (4 - #s[2] % 4) % 4
    body[#body + 1] = s[2] .. string.rep("\0", pad)
    pos = pos + #s[2] + pad
  end
  local after = table.concat(tab) .. table.concat(body)
  local h = w == 320 and 180 or 360
  local head = "BMCART\0\0" .. string.pack("<I2I2I2I2BBI2I4", 1, 128, w, h, 1, #secs, 0, crc32(after))
  head = head .. (title:sub(1, 47) .. string.rep("\0", 48)):sub(1, 48) .. (author:sub(1, 31) .. string.rep("\0", 32)):sub(1, 32)
  return head .. string.rep("\0", 128 - #head) .. after
end

-- the sheet of a file: w, h, pixels (0xRRGGBB or nil, 24 bits), the SHEET8 palette
local function decode_sheet(data)
  local s8 = section(data, 5)
  if s8 then
    local w, h, nc = string.unpack("<I2I2I2", s8)
    local pal = {}
    for i = 0, nc - 1 do
      local r, g, b, a = s8:byte(9 + i * 4, 12 + i * 4)
      pal[i] = a >= 128 and (r << 16 | g << 8 | b) or false
    end
    local px, i, q = {}, 0, 9 + nc * 4
    while i < w * h do
      local t = s8:byte(q)
      q = q + 1
      if t < 128 then
        for k = 0, t do px[i] = pal[s8:byte(q + k)] or nil; i = i + 1 end
        q = q + t + 1
      else
        local c = pal[s8:byte(q)]
        for _ = 1, t - 126 do px[i] = c or nil; i = i + 1 end
        q = q + 1
      end
    end
    assert(q == #s8 + 1 and i == w * h, "SHEET8: the runs do not add up")
    return w, h, px, pal, nc
  end
  local s = section(data, 2)
  if not s then return nil end
  local w, h = string.unpack("<I2I2", s)
  local px = {}
  for i = 0, w * h - 1 do
    local r, g, b, a = s:byte(5 + i * 4, 8 + i * 4)
    if a >= 128 then px[i] = r << 16 | g << 8 | b end
  end
  return w, h, px
end

local function q565(c)
  local r, g, b = c >> 19 & 31, c >> 10 & 63, c >> 3 & 31
  return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2)
end

----------------------------------------------------------------- the API

local E = {}
local sheet = { w = 256, h = 256, px = {} }       -- RGB565 colours, as sget gives them
local texts = {}
local keyq, pad, padprev = {}, {}, {}
local frame = 0
local quitted, ran, saved_t = false, nil, nil
local space_held = false

for k, v in pairs(_G) do E[k] = v end
E.SCREEN_W, E.SCREEN_H = 640, 360
local function nop() end
for _, n in ipairs({ "cls", "rect", "rectfill", "line", "circ", "circfill", "pset", "clip", "camera" }) do E[n] = nop end
E.print = function(s, x, y) texts[#texts + 1] = { tostring(s), x or 0, y or 0 }; return (x or 0) + #tostring(s) * 8 end
-- the keys as chips: written as "[name]"; prompt(name) alone measures
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end
E.prompt = function(n, x, y)
  assert(type(n) == "string", "prompt: a name")
  if type(x) ~= "number" then return chip_w(n), 16 end
  texts[#texts + 1] = { "[" .. n .. "]", x, y }
  return x + chip_w(n)
end
E.lastinput = function() return nil end
E.time = function() return frame / 60 end
E.stat = function() return 0 end
E.log = function(...) io.write(table.concat({ ... }, "\t"), "\n") end
E.quit = function() quitted = true end
E.btn = function(i) return pad[i] == true end
E.btnp = function(i) return pad[i] == true and not padprev[i] end
E.keyp = function() return table.remove(keyq, 1) end
E.keyheld = function(n) return n == "space" and space_held end
local keyhelp_list, keyhelp_title
E.keyhelp = function(list, title) keyhelp_list, keyhelp_title = list, title; return 0 end
E.save = function(t) saved_t = t; return true end
E.saved = function() return saved_t end
E.cart_arg = function() return nil end
E.cart_run = function(p) ran = p end
E.sget = function(x, y)
  if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return nil end
  return sheet.px[y * sheet.w + x]
end
E.sset = function(x, y, c)
  if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return end
  sheet.px[y * sheet.w + x] = c and q565(c) or nil
end
local blits = 0
E.sspr = function(sx, sy, sw, sh, dx, dy, fx, fy, z)
  assert(sw >= 0 and sh >= 0 and (z == nil or z > 0), "sspr: bad arguments")
  blits = blits + 1
end
E.cart_sheet = function(w, h)
  if w then
    assert(w >= 8 and h >= 8 and w <= 4096 and h <= 4096 and w % 8 == 0 and h % 8 == 0, "cart_sheet: 8 to 4096, multiples of 8")
    local px = {}
    for y = 0, math.min(h, sheet.h) - 1 do
      for x = 0, math.min(w, sheet.w) - 1 do px[y * w + x] = sheet.px[y * sheet.w + x] end
    end
    sheet = { w = w, h = h, px = px }
  end
  return sheet.w, sheet.h
end
E.cart_new = function() sheet = { w = 256, h = 256, px = {} } end
E.ai = {
  sprite = function(req, opt)
    local n = opt and opt.size or 16
    local s = { w = n, h = n, name = "slime", px = {} }
    for i = 1, n * n do s.px[i] = (i % 3 == 0) and -1 or 0x30C040 end
    return s
  end,
}

local function host(path) return SD .. path:lower() end
local on_sd = { ["/carts"] = { "demo.bm", "village.bm" } }
E.ls = function(dir)
  local out = {}
  for _, n in ipairs(on_sd[dir] or {}) do out[#out + 1] = { name = n, size = 0, dir = false } end
  return out
end

E.cart_load = function(path)
  local data = read_file(host(path))
  if not data then return nil, "no such file" end
  local w, h, px, pal, nc = decode_sheet(data)
  sheet = { w = w or 256, h = h or 256, px = {} }
  for i, c in pairs(px or {}) do sheet.px[i] = q565(c) end
  local p = { title = data:sub(25, 72):match("^[^\0]*"), lua = section(data, 1) or "" }
  if pal then
    p.palette = {}
    for i = 0, nc - 1 do if pal[i] then p.palette[#p.palette + 1] = pal[i] end end
  end
  return p
end

-- the sheet as the kernel writes it (runtime.c sheet_section): a pixel not
-- drawn on keeps the 24 bits it had in the old file
local function sheet_section(palette, old)
  for _, c in ipairs(palette or {}) do assert(math.type(c) == "integer" and c >= 0 and c <= 0xFFFFFF, "palette: " .. tostring(c)) end
  local rgb = {}                          -- RGB565 -> 24 bits, for the pixels drawn
  for _, c in ipairs(palette or {}) do if not rgb[q565(c)] then rgb[q565(c)] = c end end
  local ow, oh, opx, clear_at = 0, 0, {}, nil
  if old then
    local w0, h0, p0, pal0, nc0 = decode_sheet(old)
    if w0 then ow, oh, opx = w0, h0, p0 end
    for i = 0, (pal0 and nc0 or 0) - 1 do      -- its transparent entry keeps its place
      if not pal0[i] then clear_at = i; break end
    end
    for i = 0, ow * oh - 1 do local c = opx[i]; if c and not rgb[q565(c)] then rgb[q565(c)] = c end end
  end
  local w, h = sheet.w, sheet.h
  local fin, used, nused, order = {}, {}, 0, {}
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      local k, c = sheet.px[y * w + x], -1
      if k then
        local o = x < ow and y < oh and opx[y * ow + x] or nil
        c = (o and q565(o) == k) and o or (rgb[k] or k)
      end
      fin[y * w + x] = c
      if not used[c] then used[c] = true; nused = nused + 1; order[#order + 1] = c end
    end
  end
  if nused > 256 then
    local t = { string.pack("<I2I2", w, h) }
    for i = 0, w * h - 1 do t[#t + 1] = fin[i] >= 0 and string.pack(">I3", fin[i]) .. "\255" or "\0\0\0\0" end
    return 2, table.concat(t)
  end
  local entries, slot, waiting = {}, {}, nused
  local function clear_here()
    if used[-1] and not slot[-1] and #entries == clear_at then
      slot[-1] = #entries; entries[#entries + 1] = -1; waiting = waiting - 1
    end
  end
  for _, c in ipairs(palette or {}) do
    clear_here()
    local takes = used[c] and not slot[c]
    if takes or #entries + waiting < 256 then
      if takes then slot[c] = #entries; waiting = waiting - 1 end
      entries[#entries + 1] = c
    end
  end
  clear_here()
  local idx = {}
  for i = 0, w * h - 1 do
    local c = fin[i]
    if not slot[c] then slot[c] = #entries; entries[#entries + 1] = c end
    idx[i] = slot[c]
  end
  local t = { string.pack("<I2I2I2I2", w, h, #entries, 0) }
  for _, c in ipairs(entries) do t[#t + 1] = c >= 0 and string.pack(">I3", c) .. "\255" or "\0\0\0\0" end
  local i, n, lit = 0, w * h, {}
  local function flush()
    while #lit > 0 do
      local k = math.min(128, #lit)
      t[#t + 1] = string.char(k - 1)
      for _ = 1, k do t[#t + 1] = string.char(table.remove(lit, 1)) end
    end
  end
  while i < n do
    local j = i
    while j < n and j - i < 129 and idx[j] == idx[i] do j = j + 1 end
    if j - i >= 3 then flush(); t[#t + 1] = string.char(j - i + 126, idx[i]); i = j
    else lit[#lit + 1] = idx[i]; i = i + 1 end
  end
  flush()
  return 5, table.concat(t)
end

local writes = 0
E.cart_write = function(path, t)
  local old = read_file(host(t.from or path))
  assert(old or t.lua, "a new cartridge needs its code")
  local secs = old and sections_of(old) or {}
  local function put(kinds, kind, data)
    local out, done = {}, false
    for _, s in ipairs(secs) do
      if kinds[s[1]] then
        if not done then out[#out + 1] = { kind, data } end
        done = true
      else
        out[#out + 1] = s
      end
    end
    if not done then out[#out + 1] = { kind, data } end
    secs = out
  end
  if t.lua then put({ [1] = true }, 1, t.lua) end
  if t.sheet then
    local kind, data = sheet_section(t.palette, old)
    put({ [2] = true, [5] = true }, kind, data)
  end
  local f = io.open(host(path), "wb")
  if not f then return false, "cannot write " .. path end
  f:write(pack_cart(t.title or (old and old:sub(25, 72):match("^[^\0]*")) or "", old and old:sub(73, 104):match("^[^\0]*") or "",
                    old and string.unpack("<I2", old, 13) or 640, secs))
  f:close()
  writes = writes + 1
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  local seen = false
  for _, n in ipairs(list) do if n == name:lower() then seen = true end end
  if not seen then list[#list + 1] = name:lower() end
  return true
end

----------------------------------------------------------------- driving it

local chunk = assert(loadfile(ROOT .. "/carts/pixel/main.lua", "t", E))
chunk()

local function screen()
  local lines = {}
  for _, t in ipairs(texts) do lines[#lines + 1] = t[1] end
  return table.concat(lines, "\n")
end

local function frames(n)
  for _ = 1, n or 1 do
    frame = frame + 1
    texts = {}
    E._update()
    E._draw()
    for i = 0, 7 do padprev[i] = pad[i] end
  end
end

local function key(...)
  for _, k in ipairs({ ... }) do
    keyq[#keyq + 1] = k
    frames(1)
  end
  frames(5)                             -- a save waits for frames that say so
end

local function sees(s) return screen():find(s, 1, true) ~= nil end
local function status()
  for _, t in ipairs(texts) do if t[3] == 336 then return t[1] end end
  return ""
end
local function file(name) return read_file(SD .. "/carts/" .. name) end

-- the sprite being drawn and the colour, from the panel
local function sprite_at()
  local s = screen():match("sprite %d+  %((%d+,%d+)%)") or screen():match("sprite %d+ at %((%d+,%d+)%)")
  local x, y = (s or "0,0"):match("(%d+),(%d+)")
  return tonumber(x), tonumber(y)
end
local function S(x, y)
  local rx, ry = sprite_at()
  return E.sget(rx + x, ry + y)
end
local function type_text(t)
  for _ = 1, 40 do key("\b") end
  for c in t:gmatch(".") do key(c) end
  key("\n")
end

----------------------------------------------------------------- the scenario

local village0 = file("village.bm")
local demo0 = file("demo.bm")

E._init()
frames(2)
check(sees("open a cartridge") and sees("/carts/village.bm"), "at the start: the list of the files")
key("down", "\n")
check(status():find("village.bm: sheet", 1, true) and status():find("(the file's palette)", 1, true),
      "the village: its sheet and the palette of its SHEET8: " .. status())
check(sees("[f1]") and sees("draw") and sees("COLOURS"), "the draw page, its tab with the F1 chip")
local npal0 = tonumber(screen():match("COLOURS (%d+)"))

-- an empty sprite to draw on: down the sheet until the sprite is empty
local function empty()
  for y = 0, 15 do for x = 0, 15 do if S(x, y) then return false end end end
  return true
end
for _ = 1, 400 do
  if empty() then break end
  key("pgdn")
end
check(empty(), "an empty 16x16 sprite")
local erx, ery = sprite_at()

-- pencil: colour 1 at (0,0); a stroke with space held
key("1", "b", "home")
for _ = 1, 8 do key("left") end
for _ = 1, 8 do key("up") end
key(" ")
local c1 = S(0, 0)
check(c1 ~= nil, "pencil: a pixel at (0,0)")
space_held = true
key(" ")
for _ = 1, 3 do key("right") end
space_held = false
key("down")
check(S(1, 0) == c1 and S(2, 0) == c1 and S(3, 0) == c1 and S(3, 1) == nil, "space held: a stroke along the row")
key("^z")
check(S(1, 0) == nil and S(0, 0) == c1, "undo: the stroke goes, the first pixel stays")
key("^y")
check(S(3, 0) == c1, "redo: the stroke is back")

-- line, rectangle, oval, fill
key("2", "l", "home")                    -- (8,8)
key(" ")
for _ = 1, 4 do key("down") end          -- (8,12)
key(" ")
local c2 = S(8, 8)
check(c2 and c2 ~= c1 and S(8, 10) == c2 and S(8, 12) == c2 and S(8, 13) == nil, "line: (8,8) to (8,12)")
key("U")
key(" ")
for _ = 1, 2 do key("right") end         -- (10,12)
for _ = 1, 2 do key("down") end          -- (10,14)
key(" ")
check(S(8, 14) == c2 and S(10, 12) == c2 and S(9, 13) == c2, "filled rectangle (8,12)-(10,14)")
key("e", " ")
check(S(10, 14) == nil, "eraser: " .. tostring(S(10, 14)) .. " " .. screen():match("colour %d+ [^\n]*"))
key("3", "o", "home")
for _ = 1, 8 do key("left") end
for _ = 1, 4 do key("down") end          -- (0,12)
key(" ")
for _ = 1, 6 do key("right") end
for _ = 1, 3 do key("down") end          -- (6,15)
key(" ")
local c3 = S(3, 12)
check(c3 and S(0, 13) == c3 and S(3, 13) == nil and S(6, 14) == c3, "oval: an outline, empty inside")
key("4", "g")
key(" ")                                 -- at (6,15): outside the oval? it is on its edge: move out first
key("^z")
key("home", " ")                         -- (8,8) is the line: fill it with colour 4
local c4 = S(8, 8)
check(c4 and c4 ~= c2 and S(8, 12) == c4 and S(9, 13) == c4, "fill: the line and the rectangle it touches")
key("^z")
check(S(8, 8) == c2, "undo the fill")

-- the colour from the sprite, the mirror
key("i")
key("home", " ")
check(sees("colour 2 "), "colour from the sprite: colour 2")
key("b", "y", "home")
for _ = 1, 8 do key("left") end
for _ = 1, 6 do key("down") end          -- (0,14)
key(" ")
check(S(0, 14) == c2 and S(15, 14) == c2, "mirror: the other side too")
key("y")

-- select, copy, paste, lift and move
key("m", "home")
for _ = 1, 8 do key("left") end
for _ = 1, 8 do key("up") end            -- (0,0)
key(" ", "right", "right", "right", " ") -- (0,0)-(3,0)
check(sees("[ctrl]") and sees("lift"), "a selection: its keys on the hint row")
key("^c", "esc", "home", "^v")           -- pasted at (8,8)
key("down", "down", "down", "down", "down", "down", "down", "\n")      -- at (8,15)
check(S(8, 15) == c1 and S(11, 15) == c1, "paste: the row of 4 at (8,15)")
key("m", "home")
for _ = 1, 8 do key("left") end
for _ = 1, 8 do key("up") end
key(" ", "right", "right", "right", " ", "\n")      -- lift (0,0)-(3,0)
key("down", "\n")
check(S(0, 0) == nil and S(0, 1) == c1 and S(3, 1) == c1, "lift and move: the row one lower")

-- flip, turn, shift the whole sprite
key("h")
check(S(15, 1) == c1 and S(0, 1) == nil, "flip left-right")
key("^z", "D")
check(S(1, 1) == c1 and S(4, 1) == c1 and S(0, 1) == nil, "shift right by one")
key("^z", "r")
check(S(14, 0) == c1 and S(14, 3) == c1, "a quarter turn")
key("^z")

-- the animation and the size
key("+", "+", ">", "k", "z")
check(screen():find("anim %d+/6") and sees("9 fps") and sees("onion"), "6 frames, 9 frames a second, onion skin")
check(screen():find("32x32", 1, true) ~= nil, "z: sprites of 32x32")
key("z", "z", "z", "z")
check(screen():find("16x16", 1, true) ~= nil, "back to 16x16")
erx, ery = sprite_at()                   -- the bigger sizes put it on their grid

-- the assistant
key("f6")
check(sees("a sprite base"), "F6: the assistant asks")
type_text("slime")
check(sees("the assistant's slime"), "the assistant's sprite floats")
key("^z")

-- the palette page: a new colour, edited, drawn with
key("f3")
check(sees("PALETTE") and sees("[e]") and sees("edit"), "F3: the palette")
key("a")
for _ = 1, 4 do key("right") end         -- R + 32
key("\n", "\n")
check(screen():find("COLOURS " .. (npal0 + 1), 1, true), "a colour more: " .. (npal0 + 1))
key("home")
for _ = 1, 8 do key("left") end
for _ = 1, 8 do key("up") end
key("b", " ")
check(S(0, 0) ~= nil, "drawn with the new colour")
local newc = S(0, 0)

-- the sheet page: copy the sprite to the next one, the size of the sheet
key("f2")
check(sees("sheet ") and sees("zoom"), "F2: the sheet")
key("^c", erx + 16 < E.cart_sheet() and "right" or "left", "^v")
local rx2, ry2 = sprite_at()
check(rx2 ~= erx and E.sget(rx2, ry2) == newc, "copied to the sprite beside")
key(rx2 > erx and "left" or "right", "\n")
check(sees("COLOURS"), "Enter: draw it")
key("f2", "R")
check(sees("sheet size"), "R: the size")
local w0, h0 = E.cart_sheet()
type_text(w0 .. "x" .. (h0 + 64))
check(select(2, E.cart_sheet()) == h0 + 64, "the sheet is 64 pixels taller")

-- save: only the sheet changes in the file; first a frame that says so
keyq[#keyq + 1] = "^s"
frames(1)
check(sees("saving /carts/village.bm ..."), "Ctrl+S: saving first")
key()
check(status():find("saved /carts/village.bm", 1, true), "Ctrl+S: " .. status())
local v1 = file("village.bm")
for _, t in ipairs({ 1, 4, 8, 9 }) do
  check(section(v1, t) == section(village0, t), "section " .. t .. " stays as it was")
end
check(section(v1, 5) and not section(v1, 2), "the sheet is a SHEET8")
local w, h, px, spal, snc = decode_sheet(v1)
check(w == w0 and h == h0 + 64, "the new size in the file")
local opaque, first = 0, nil
for i = 0, snc - 1 do if spal[i] then opaque = opaque + 1 end end
for i = 0, snc - 1 do if spal[i] then first = first or i end end
local _, _, _, pal0 = decode_sheet(village0)
local orig_first
for i = 0, 255 do if pal0[i] then orig_first = pal0[i]; break end end
check(opaque == npal0 + 1 and first == 0 and spal[0] == orig_first,
      "the palette first in the SHEET8, as it was plus the new colour (" .. opaque .. " of " .. snc .. ")")
local _, _, opx, opal = decode_sheet(village0)
local exact = 0
for i = 0, 255 do if opal[i] and spal[i - 1 + 1] then exact = exact + 1 end end
check(exact > 0, "colours kept with their 24 bits")

-- open it again: the palette comes back from the file
key("esc", "down", "\n")
for _ = 1, 2 do key("down") end
key("\n")
local st = status()
check(sees("zoom"), "it opens on the page it was saved from (the sheet)")
key("f1")
check(st:find("(the file's palette)", 1, true) and screen():find("COLOURS " .. (npal0 + 1), 1, true),
      "opened again: the palette from the file, " .. (npal0 + 1) .. " colours: " .. st)
check(S(0, 0) == newc, "and the drawing: " .. tostring(S(0, 0)) .. " vs " .. tostring(newc) .. " at " .. table.concat({ sprite_at() }, ",") .. " (drawn at " .. erx .. "," .. ery .. ")")

-- a new sheet, saved as a new cartridge with the viewer
key("esc", "down", "down", "\n")
check(sees("new sheet"), "a new sheet")
key("1", "b", " ")
key("^s")
check(sees("file name"), "Ctrl+S on a new sheet: Save as")
type_text("NEWSPR")
check(status():find("saved /carts/NEWSPR.BM", 1, true), "saved: " .. status())
local n1 = file("newspr.bm")
check(n1 and section(n1, 1):find("bm Pixel: a new sprite sheet", 1, true) and section(n1, 5), "the new cartridge: the viewer and a SHEET8")
local ok, err = pcall(load, section(n1, 1))
check(ok and err, "the viewer is Lua")

-- try the game
key("f5")
check(ran == "/carts/NEWSPR.BM", "F5: the game runs")

-- F12 held: the keys of the page (keyhelp(), the kernel draws them)
local function in_help(keys, what)
  for _, e in ipairs(keyhelp_list or {}) do
    if type(e) == "table" and e[1] == keys and e[2]:find(what, 1, true) then return true end
  end
  return false
end
key("f1")
check(keyhelp_title == "bm Pixel" and in_help("b / e / g / i", "pencil, eraser, fill") and in_help("Y A", "undo"),
      "keyhelp: the keys of the draw page, the pad's")
key("f3")
check(in_help("e", "edit: R G B") and not in_help("p", "play"), "keyhelp: the palette's")
check(E._exit() == true, "_exit: nothing to save, it leaves")

io.write(string.format("bm Pixel: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
