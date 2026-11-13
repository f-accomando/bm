-- bm SNES SDK (carts/snes_sdk) on the PC (the stubs are those of pixel_host.lua): stand-ins for the bm API (a folder is
-- the SD card; the sheet is a table of RGB565 colours as the kernel keeps
-- them; cart_write writes the sheet as the kernel does, SHEET8 with the
-- palette first when it fits), keys typed into it, and checks on what it
-- draws in the sheet and writes. The files are read again by the build's
-- Python (check_files.py) and the kernel (test_meshcap).
--
--   luahost tests/studio/pixel_host.lua ROOT SDDIR     (make test-studio)
--
-- SDDIR/carts/testkit.bm and mapkit.bm must be there (games: the saves go into their .bme copies).

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
for _, n in ipairs({ "cls", "rect", "line", "circ", "circfill", "pset", "clip", "camera" }) do E[n] = nop end
local rects = 0
E.rectfill = function() rects = rects + 1 end
local last_stack                          -- the last stack() / stackv() call: { name, args... }
E.stack = function(...) last_stack = { "stack", ... } end
E.stackv = function(...) last_stack = { "stackv", ... } end
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
-- the mouse (bmui): MS as a USB mouse would give it
local MS = { x = 0, y = 0, b = 0, w = 0, p = 0 }
E.mouse = function(on) if on ~= nil then return true end; return MS.x, MS.y, MS.b, MS.w, true end
E.mousep = function(i) return MS.p >> (i or 0) & 1 == 1 end
local bmui, bmlib
E.require = function(name)
  if name == "bmlib" then                -- the layout (lib.layout): once
    bmlib = bmlib or assert(loadfile(ROOT .. "/src/script/bmlib.lua", "t", E))()
    return bmlib
  end
  if name == "snesgfx" and not arg[3] then              -- the SNES converters (the build puts them into the cart)
    return assert(loadfile(ROOT .. "/carts/shared/snesgfx.lua", "t", E))()
  end
  assert(name == "bmui", "require: only bmui and bmlib here (a built cart has snesgfx in SNESGFX)")
  bmui = bmui or assert(loadfile(ROOT .. "/src/script/bmui.lua", "t", E))()   -- the mouse (it takes keyp over)
  return bmui
end
local keyhelp_list, keyhelp_title
E.keyhelp = function(list, title) keyhelp_list, keyhelp_title = list, title; return 0 end
local showkeys_n = 0
E.showkeys = function() showkeys_n = showkeys_n + 1; return true end
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
local sprs = 0
E.spr = function() sprs = sprs + 1 end
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
-- the SPRITES section (cart_data(11)) and the zones read from it, as the kernel gives them
local zdata
local function zlist()
  local out = {}
  if zdata and #zdata >= 4 then
    for i = 0, string.unpack("<I2", zdata, 1) - 1 do
      local name, x, y, w, h, fr, fps, sl = string.unpack("<c16I2I2I2I2BBB", zdata, 5 + i * 28)
      out[#out + 1] = { name:match("^[^%c]*"), x, y, w, h, fr, fps, sl }
    end
  end
  return out
end
E.cart_data = function(kind, bytes)
  assert(kind == 11, "cart_data: only SPRITES here")
  if bytes == nil then return zdata end
  zdata = bytes ~= "" and bytes or nil
  return true
end
E.zones = function()
  local out = {}
  for _, z in ipairs(zlist()) do out[#out + 1] = z[1] end
  return out
end
E.zboxes = function() return {} end
E.zone = function(name)
  for _, z in ipairs(zlist()) do
    if z[1] == name then return table.unpack(z, 2) end
  end
end
E.ai = {
  sprite = function(req, opt)
    local n = opt and opt.size or 16
    local s = { w = n, h = n, name = "slime", px = {} }
    for i = 1, n * n do s.px[i] = (i % 3 == 0) and -1 or 0x30C040 end
    return s
  end,
}

local function host(path) return SD .. path:lower() end
local on_sd = { ["/carts"] = { "mapkit.bm", "testkit.bm" } }
E.ls = function(dir)
  local out = {}
  for _, n in ipairs(on_sd[dir] or {}) do out[#out + 1] = { name = n, size = 0, dir = false } end
  return out
end

E.cart_load = function(path)
  local data = read_file(host(path))
  if not data then return nil, "no such file" end
  local w, h, px, pal, nc = decode_sheet(data)
  zdata = section(data, 11)
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
local function listed(path)
  local dir, name = path:match("^(.*)/([^/]+)$")
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  for _, n in ipairs(list) do if n == name:lower() then return end end
  list[#list + 1] = name:lower()
end
local project_target = dofile(ROOT .. "/tests/studio/project_rules.lua")
E.cart_write = function(path, t)
  local moved
  path, moved = project_target(path, function(p) return read_file(host(p)) ~= nil end, function(a, b)
    local f = io.open(host(b), "wb")
    f:write(read_file(host(a)))
    f:close()
    listed(b)
  end)
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
  for kind, data in pairs(t.sections or {}) do       -- false takes the section away
    local out = {}
    for _, sec in ipairs(secs) do if sec[1] ~= kind then out[#out + 1] = sec end end
    if data then out[#out + 1] = { kind, data } end
    secs = out
  end
  local f = io.open(host(path), "wb")
  if not f then return false, "cannot write " .. path end
  f:write(pack_cart(t.title or (old and old:sub(25, 72):match("^[^\0]*")) or "", old and old:sub(73, 104):match("^[^\0]*") or "",
                    old and string.unpack("<I2", old, 13) or 640, secs))
  f:close()
  writes = writes + 1
  listed(path)
  if moved then return true, path end
  return true
end

----------------------------------------------------------------- driving it

-- the export: sheet_png and doc_write record what they are given
local png_calls, docs, png_fail, doc_fail = {}, {}, nil, nil
E.sheet_png = function(rects)
  png_calls[#png_calls + 1] = rects
  if png_fail then return nil, png_fail end
  return "PNG" .. #png_calls
end
local gif_calls, gif_fail = {}, nil
E.sheet_gif = function(rects, delay)
  gif_calls[#gif_calls + 1] = { rects = rects, delay = delay }
  if gif_fail then return nil, gif_fail end
  return "GIF" .. #gif_calls
end
E.doc_write = function(name, bytes)
  if doc_fail then return nil, doc_fail end
  docs[name] = bytes
  return true
end

-- the third argument: the built main.lua (build/snes_sdk/main.lua, with the module as SNESGFX); none: the source
local chunk = assert(loadfile(arg[3] or (ROOT .. "/carts/snes_sdk/main.lua"), "t", E))
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
local function file(name) return read_file(SD .. (name:lower():match("%.bme$") and "/bme/" or "/carts/") .. name) end

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

local function mouse(x, y, b, w)
  local was = MS.b
  MS.x, MS.y, MS.b, MS.w = x, y, b or 0, w or 0
  MS.p = MS.b & ~was
  frames(1)
  MS.p, MS.w = 0, 0
end
local function click(x, y, b)
  mouse(x, y, 0); mouse(x, y, b or 1); mouse(x, y, 0); frames(1)
end

----------------------------------------------------------------- the scenario

E._init()
frames(2)
check(sees("bm SNES SDK") and sees("Sprites") and sees("Code") and sees("Map") and sees("Music") and sees("Run"),
      "the tab bar: Code, Sprites, Map, Music, Run")
check(sees("4 bpp") and sees("sub-palette 1 of 8"), "it starts on Sprites: 4 bpp, 8 sub-palettes")
for _, t in ipairs({ { "f1", "Code" }, { "f3", "Map" }, { "f4", "Music" }, { "f5", "Run" } }) do
  key(t[1])
  check(sees(t[2] .. " - coming soon"), "the " .. t[2] .. " tab says coming soon")
end
key("f2")
check(sees("4 bpp"), "F2 is the Sprites tab")

-- draw one pixel: Enter (draw on), Space; colour 1 of the first palette is red $001F
key("\n", " ")
check(E.sget(0, 0) == q565(0xF80000), "Space paints pixel (0,0) in colour 1: " .. tostring(E.sget(0, 0)))
check(sees("OAM $000"), "the OAM preview shows the tile number")

-- the palette: R down by one, the pixel follows (the indices are the data, the colours come from the palette)
key("-")
check(E.sget(0, 0) == q565(0xF70000), "R 31 -> 30: the pixel is repainted from its index: " .. tostring(E.sget(0, 0)))
check(sees("BGR555 $001E") and sees("R 30"), "the colour is $001E, R 30")

-- the export: planar tile bytes and the palette
key("e")
type_text("tst")
local chr, pal = docs["tst.chr"] or "", docs["tst.pal"] or ""
check(chr:sub(1, 2) == "\x80\0" and chr:sub(17, 18) == "\0\0", "the .chr: pixel (0,0) = colour 1 is plane 0, bit 7")
check(#chr % 32 == 0 and #chr == 16 * 16 * 32, "128x128 at 4bpp: 16 x 16 tiles of 32 bytes: " .. #chr)
check(pal:sub(1, 4) == "\0\0\x1E\0" and #pal == 8 * 16 * 2, "the .pal: entry 1 is $001E, 8 x 16 words")

-- depth, sizes, flips
key("b")
check(sees("8 bpp") and sees("sub-palette 1 of 1"), "8 bpp: one palette of 256")
key("b")
check(sees("2 bpp"), "2 bpp")
key("b")
check(sees("4 bpp"), "4 bpp again")
key("\n")                                  -- draw off: the arrows choose a sprite
key("s")
check(sees("sizes 8/32") and sees("small 8x8"), "s: the pair 8/32")
key("l")
check(sees("large 32x32"), "l: large 32x32")
key("l", "S")
check(sees("sizes 8/16"), "shift S: back one pair")
key("x", "p")
check(sees("[x] flipX") and sees("prio 1"), "OAM preview: flip X and priority 1")
key("x", "p", "p", "p")

-- the sub-palette of the tiles
key("l", ".")
check(sees("sub-palette 2 of 8"), "the next sub-palette")
key("a")
check(status():find("use sub-palette 2", 1, true), "a: the tiles of the sprite use sub-palette 2: " .. status())
key(",")

-- save as a new project: the sheet is SHEET8, the data is text in the Lua section
key("^s")
check(sees("file name (8.3, in /bme)"), "Ctrl+S on a new project asks for a name")
type_text("snesdemo.bme")
local data = file("snesdemo.bme")
check(data ~= nil, "the project is saved in /bme")
local lua = data and section(data, 1) or ""
check(lua:find("-- snes-sdk:begin", 1, true) and lua:find("--pal 1 0000 001E", 1, true) and lua:find("--tp 0=2", 1, true),
      "the Lua section holds the marks, the sub-palette and the tile table")
check(data and section(data, 5) ~= nil, "the sheet is a SHEET8")
check(sees("saved /bme/snesdemo.bme"), "the status says saved: " .. status())

-- new, then open it again: the pixel, the palette and the tile table come back
on_sd["/bme"] = { "snesdemo.bme" }
key("^n")
check(E.sget(0, 0) == nil, "a new project: an empty sheet")
key("^o")
check(sees("open a project") and sees("/bme/snesdemo.bme"), "Ctrl+O lists the projects")
key("\n")
check(E.sget(0, 0) == 0, "opened: the tile uses sub-palette 2 (empty: colour 1 is black): " .. tostring(E.sget(0, 0)))
check(sees("BGR555 $001E"), "opened: the sub-palette is back")
key("^z")

-- a PNG with many colours: quantised to 15 colours + clear
E.image_file = function(path, sx, sy, w, h)
  for y = 0, 15 do for x = 0, 15 do E.sset(sx + x, sy + y, ((x * 16) << 16) | ((y * 16) << 8) | 0x40) end end
  return { x = sx, y = sy, w = 16, h = 16, sw = 16, sh = 16 }
end
key("i")
key("\n")
check(status():find("imported 16x16", 1, true), "the import ends with the quantiser: " .. status())
local seen, n = {}, 0
for y = 0, 15 do for x = 0, 15 do
  local c = E.sget(x, y)
  if c and not seen[c] then seen[c] = true; n = n + 1 end
end end
check(n >= 2 and n <= 15, "the 256-colour picture now has " .. n .. " colours (15 at most)")
E.image_file = nil

-- the mouse: a click on a colour, a press in the sprite editor paints
key("home")
click(320 + 2 * 16 + 4, 68)
check(sees("colour 2  BGR555"), "a click on a colour of the palette takes it")
click(320 + 3 * 12 + 2, 160 + 2 * 12 + 2)
check(E.sget(3, 2) ~= nil, "a click in the sprite editor paints the pixel")
key("\t")
check(sees("Depth 2 / 4 / 8 bpp"), "Tab lists the commands")
key("esc")
key("esc")
check(sees("bm SNES SDK") and sees("New project"), "Esc opens the menu")
key("esc")

dofile(ROOT .. "/tests/studio/hintchip.lua"){
  name = "bm SNES SDK", check = check, texts = function() return texts end, hy = 320, w = 640, chip_w = chip_w,
  click = click, shown = function() return showkeys_n end,
  pages = {
    { name = "code", go = function() key("f1") end },
    { name = "sprites", go = function() key("f2") end },
    { name = "map", go = function() key("f3") end },
    { name = "music", go = function() key("f4") end },
    { name = "run", go = function() key("f5") end },
  } }

io.write(string.format("bm SNES SDK: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
