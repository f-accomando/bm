-- bm SDK (RGB30), the .b16 edition of the SDK (carts/sdk_b16), on the PC:
-- its layout module on its own, then the cartridge at 360x360 on stand-ins
-- for the bm API (a folder is the SD card, as tests/studio/sdk_host.lua),
-- driven by the RGB30's pad alone (btn by names: "ok" is B, "back" is A,
-- as the console's default) and by a keyboard. Every frame drawn is
-- checked: nothing outside the 360x360 screen, the SDK's text on the 8x16
-- grid (45 columns, 22 rows), no text or chip over another on its row. The
-- pad reaches every page, every action of every page (X) and back; a name
-- is typed with padtype's keyboard. The SDK runs as the RGB30's kernel runs
-- it, a tool (src/rgb30/ui.c, src/kernel/devtool.c): the stand-ins of
-- cart_save, cart_build, cart_run and cart_tool follow the runtime's rules
-- for a tool (src/bm/runtime.c write_target: any file but a game; saving a
-- game, the console asks and makes its editable copy next to it), and the
-- console has no other program of the suite, which the SDK must not offer.
--
--   luahost tests/studio/sdk_b16_host.lua ROOT SDDIR CART   (make test-sdk-b16)
--
-- CART is the built script (build/sdk_b16/main.lua); SDDIR/bm/village.b16
-- must be there (a copy of build/carts/village.bm).

local ROOT, SD, CART = arg[1] or ".", arg[2] or "build/sdk-b16-sd", arg[3] or "build/sdk_b16/main.lua"
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

----------------------------------------------------------------- the layout module on its own

do
  local env = setmetatable({ SCREEN_W = 360, SCREEN_H = 360 }, { __index = _G })
  local LY = assert(loadfile(ROOT .. "/carts/sdk_b16/layout.lua", "t", env))()
  local scr = LY.rect(0, 0, 360, 360)
  local t = LY.split(scr, "v", { 16, "*", 16, 24 })
  check(#t == 4 and t[2].y == 16 and t[2].h == 304 and t[3].y == 320 and t[4].y == 336 and t[4].h == 24,
        "split v: tabs, body 304, hints at 320, status at 336")
  local h = LY.split(t[2], "h", { "16c", "*" })
  check(h[1].w == 128 and h[2].x == 128 and h[2].w == 232, "split h: 16 columns and the rest")
  local s = LY.split(scr, "h", { "*", "*", "*" })
  check(s[1].w % 8 == 0 and s[2].w % 8 == 0 and s[1].w + s[2].w + s[3].w == 360 and s[2].x == s[1].w,
        "shares on whole columns, the last one the rest: " .. s[1].w .. " " .. s[2].w .. " " .. s[3].w)
  local v = LY.split(LY.rect(0, 16, 360, 304), "v", { "2r", "*", "2*" })
  check(v[1].h == 32 and v[2].h % 16 == 0 and v[2].h + v[3].h == 272 and v[3].h >= 2 * v[2].h - 16,
        "rows and weights: " .. v[2].h .. " " .. v[3].h)
  check(not pcall(LY.split, scr, "v", { 300, 100 }), "split: more than fits is an error")
  check(not pcall(LY.split, scr, "x", { "*" }), "split: \"v\" or \"h\"")
  check(not pcall(LY.split, scr, "v", { "3q" }), "split: a size it does not know")
  check(LY.narrow(360) and not LY.narrow(640) and LY.narrow(), "narrow: 360 yes, 640 no, the screen's")
  check(LY.cols(scr) == 45 and LY.rows(scr) == 22, "45 columns, 22 rows")
  check(LY.snap(1) == 8 and LY.snap(8) == 8 and LY.snap(0) == 0, "snap: the next column")
  local w = LY.wrap("the game stopped: see the line in red, Ctrl+G jumps there", 20)
  local ok = #w >= 3
  for _, l in ipairs(w) do if #l > 20 then ok = false end end
  check(ok, "wrap: lines of at most 20")
  check(#LY.wrap("a b c d e f g", 3, 2) == 2 and LY.wrap("abcdefgh", 3)[1] == "abc", "wrap: n lines, long words cut")
  check(LY.fit("abcdef", 3) == "abc" and LY.fit("ab", 3) == "ab" and LY.fit("ab", 0) == "", "fit")
  check(LY.inside(LY.rect(8, 8, 8, 8), scr) and not LY.inside(LY.rect(350, 0, 16, 8), scr), "inside")
  check(LY.overlap(LY.rect(0, 0, 10, 10), LY.rect(9, 9, 5, 5)) and not LY.overlap(LY.rect(0, 0, 8, 8), LY.rect(8, 0, 8, 8)),
        "overlap")
  local p360, w360 = LY.side_by_side(scr)
  check(p360.w == 128 and w360.x == 128 and w360.w == 232, "side_by_side narrow: 16 columns panel (128px), rest work (232px)")
  local scr720 = LY.rect(0, 0, 720, 720)
  local p720, w720 = LY.side_by_side(scr720)
  check(p720.w == 240 and w720.x == 240 and w720.w == 480, "side_by_side wide: 30 columns panel (240px), rest work (480px)")
  local p_cust, w_cust = LY.side_by_side(scr720, { narrow = 16, wide = 25 })
  check(p_cust.w == 200 and w_cust.x == 200 and w_cust.w == 520, "side_by_side custom cols")
end

----------------------------------------------------------------- .bm files (as sdk_host.lua)

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
  local n = data:byte(18)
  for i = 0, n - 1 do
    local t, off, size = string.unpack("<I4I4I4", data, 129 + i * 16)
    out[#out + 1] = { t, data:sub(off + 1, off + size) }
  end
  return out
end

local RES = { ["640x360"] = { 640, 360 }, ["480x270"] = { 480, 270 }, ["320x180"] = { 320, 180 },
              ["256x256"] = { 256, 256 }, ["360x360"] = { 360, 360 } }
local function pack_cart(title, author, res, secs)
  local tab, body = {}, {}
  local pos = 128 + 16 * #secs
  for _, s in ipairs(secs) do
    tab[#tab + 1] = string.pack("<I4I4I4I4", s[1], pos, #s[2], 0)
    local pad = (4 - #s[2] % 4) % 4
    body[#body + 1] = s[2] .. string.rep("\0", pad)
    pos = pos + #s[2] + pad
  end
  local after = table.concat(tab) .. table.concat(body)
  local wh = RES[res] or RES["640x360"]
  local head = "BMCART\0\0" .. string.pack("<I2I2I2I2BBI2I4", 1, 128, wh[1], wh[2], 1, #secs, 0, crc32(after))
  head = head .. (title:sub(1, 47) .. string.rep("\0", 48)):sub(1, 48) .. (author:sub(1, 31) .. string.rep("\0", 32)):sub(1, 32)
  head = head .. string.rep("\0", 128 - #head)
  return head .. after
end

local function res_of(data)
  local w, h = string.unpack("<I2I2", data, 13)
  return w .. "x" .. h
end

-- the sheet of a .bm (SHEET or SHEET8): w, h, { [y * w + x] = 0xRRGGBB }
local function decode_sheet(data)
  for _, s in ipairs(sections_of(data)) do
    if s[1] == 2 then
      local w, h = string.unpack("<I2I2", s[2])
      local px = {}
      for i = 0, w * h - 1 do
        local r, g, b, a = s[2]:byte(5 + i * 4, 8 + i * 4)
        if a >= 128 then px[i] = r << 16 | g << 8 | b end
      end
      return w, h, px
    elseif s[1] == 5 then
      local b = s[2]
      local w, h, nc = string.unpack("<I2I2I2", b)
      local pal = {}
      for i = 0, nc - 1 do
        local r, g, bb, a = b:byte(9 + i * 4, 12 + i * 4)
        pal[i] = a >= 128 and (r << 16 | g << 8 | bb) or false
      end
      local px, i, q = {}, 0, 9 + nc * 4
      while i < w * h do
        local t = b:byte(q)
        q = q + 1
        if t < 128 then
          for k = 0, t do px[i] = pal[b:byte(q + k)] or nil; i = i + 1 end
          q = q + t + 1
        else
          local c = pal[b:byte(q)]
          for _ = 1, t - 126 do px[i] = c or nil; i = i + 1 end
          q = q + 1
        end
      end
      return w, h, px
    end
  end
  return 256, 256, {}
end

local function mesh_models(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nv, nf = string.unpack("<I2I2", bin, pos + 16)
    local verts = {}
    for i = 1, nv do verts[i] = { string.unpack("<fff", bin, pos + 24 + (i - 1) * 12) } end
    out[#out + 1] = { name = name, nv = nv, nf = nf, verts = verts }
    pos = pos + 24 + nv * 12 + nf * 24
  end
  return out
end

local function anim_rigs(bin)
  local out = {}
  if not bin then return out end
  local n = string.unpack("<I2", bin)
  local pos = 9
  for _ = 1, n do
    local name = bin:sub(pos, pos + 15):match("^[^\0]*")
    local nb, nc, nv = string.unpack("<I2I2I2", bin, pos + 16)
    local r = { nb = nb, nv = nv, clips = {} }
    local p = pos + 24 + nb * 44 + ((nv + 3) & ~3)
    for c = 1, nc do
      local nk, _, flags, length = string.unpack("<I2BBf", bin, p + 16)
      r.clips[c] = { name = bin:sub(p, p + 15):match("^[^\0]*"), length = length, loop = flags & 1 == 1 }
      p = p + 24 + nk * (4 + nb * 28)
    end
    out[name] = r
    pos = p
  end
  return out
end

----------------------------------------------------------------- the frame's checks

-- every draw: inside the screen (the clipped ones are the kernel's to
-- cut); the SDK's text and chips on their rows of the grid, never over
-- another (an opaque rectangle clears what is under it)
local CARTSRC = "@" .. CART
local G = { fails = {}, seen = {}, frame_tag = "" }
local occ, clipped
local function g_begin(tag) occ, clipped, G.frame_tag = {}, false, tag or "" end
local function g_fail(msg)
  local key = G.frame_tag .. ": " .. msg
  if G.seen[key] then return end
  G.seen[key] = true
  G.fails[#G.fails + 1] = key
end
local function mine(src) return src == CARTSRC or src:find("bmui.lua", 1, true) ~= nil end
-- the assistant's panel (src/ai/assist.lua, shared with the Pi's tools) is
-- not the SDK's: on the RGB30 it is not there (no ai.ask); where it is, its
-- row of keys is wider than 360 and the screen cuts it (listed, not failed)
local ASSIST_OUT = {}
local function theirs(src) return src:find("assist.lua", 1, true) ~= nil end
local function g_box(kind, x, y, w, h, what)
  if clipped or G.off then return end
  if x < 0 or y < 0 or x + w > 360 or y + h > 360 then
    g_fail(string.format("%s %q at %d,%d %dx%d leaves the 360x360 screen", kind, what or "", x, y, w, h))
  end
end
local function g_clear(x, y, w, h)
  for r = y // 16, (y + h - 1) // 16 do
    local row = occ[r]
    if row then for i = math.max(0, x), math.min(359, x + w - 1) do row[i] = nil end end
  end
end
local function g_mark(x, y, w, what, src)
  if G.off or not mine(src) then return end
  if y % 16 ~= 0 then g_fail(string.format("%q at y %d: not on a row of 16", what, y)); return end
  local row = occ[y // 16] or {}
  occ[y // 16] = row
  for i = math.max(0, x), math.min(359, x + w - 1) do
    if row[i] and row[i] ~= what then
      g_fail(string.format("%q over %q at %d,%d", what, row[i], i, y))
      break
    end
    row[i] = what
  end
end

----------------------------------------------------------------- the SD card

local on_sd = { ["/carts"] = {}, ["/bm"] = { "village.b16" }, ["/"] = {} }
local function host(path) return SD .. path:lower() end
-- the card as this test made it (a file left in the folder by an earlier
-- run is not on it)
local function exists(p)
  local dir, name = p:match("^(.*)/([^/]+)$")
  for _, n in ipairs(on_sd[dir == "" and "/" or dir] or {}) do if n == name:lower() then return true end end
  return false
end
local function add_file(path)
  local dir, name = path:match("^(.*)/([^/]+)$")
  dir = dir == "" and "/" or dir
  local list = on_sd[dir] or {}
  on_sd[dir] = list
  for _, n in ipairs(list) do if n == name:lower() then return end end
  list[#list + 1] = name:lower()
end

----------------------------------------------------------------- the SDK on the "console"

local E, texts, keyq, held, frame, saved_t, quitted, draws, arg_t
local sec, sheet, mapc, lnames, mlay, flg
local refused, copies, saves, last_save, built, ran, tooled
local audio_files, audio_puts, audio_last = {}, 0, nil      -- the sound banks by file; what was played
local keyhelp_list, keyhelp_title, MS, ai_full, last_kb, numeric_btn
local loaded, cur_font
local CUTS = {}
local MESH_MT = {}
local function chip_w(n) return #n == 1 and 16 or math.max(16, #n * 6 + 10) end
local PADN = { A = 1, B = 1, X = 1, Y = 1, START = 1, SELECT = 1, L1 = 1, R1 = 1, L2 = 1, R2 = 1, L3 = 1, R3 = 1,
               UP = 1, DOWN = 1, LEFT = 1, RIGHT = 1, UPDOWN = 1, LEFTRIGHT = 1, DPAD = 1, LSTICK = 1, RSTICK = 1, PS = 1,
               ok = 1, back = 1 }
local NAMES = { left = 1, right = 1, up = 1, down = 1, ok = 1, back = 1, a = 1, b = 1, x = 1, y = 1, l1 = 1, r1 = 1,
                l2 = 1, r2 = 1, l3 = 1, r3 = 1, start = 1, select = 1 }
-- the RGB30's own buttons as pad() gives them, by their letters: ok is
-- the bottom one (B), back the right one (A)
local HID = { left = 1, right = 2, up = 4, down = 8, back = 16, ok = 32, start = 64, select = 128, x = 256, y = 512,
              l1 = 1024, r1 = 2048, l2 = 4096, r2 = 8192, l3 = 16384, r3 = 32768 }
local prev_held = {}

local ENTRIES = {
  ["guide.platformer"] = { id = "guide.platformer", kind = "guide", title = "Un platform 2D con l'SDK", name = "",
                           text = "1. Ctrl+N, Platform 2D.", gen = "", see = {}, code = "-- platform\nlocal vy = 0\n" },
  ["mesh.cube"] = { id = "mesh.cube", kind = "mesh", title = "Cubo (un blocco)", name = "", code = "",
                    text = "Un blocco.", gen = "cube", see = {} },
}
local function entries_of(kinds)
  local out = {}
  for _, id in ipairs({ "guide.platformer", "mesh.cube" }) do
    local e = ENTRIES[id]
    if not kinds or kinds:find(e.kind, 1, true) then out[#out + 1] = { id = id, title = e.title, kind = e.kind } end
  end
  return out
end

local function caller() return debug.getinfo(3, "S").source end

local function new_env(arg, o)
  o = o or {}
  E = {}
  texts, keyq, held, frame, quitted, draws = {}, {}, {}, 0, false, 0
  arg_t, loaded, refused, copies, built, ran, tooled = arg, {}, 0, 0, nil, nil, nil
  sec, sheet, mapc, lnames, mlay, flg = {}, { w = 256, h = 256, px = {} }, {}, { "main" }, {}, {}
  last_kb, numeric_btn, cur_font, prev_held = o.keyboard, nil, { 8, 16 }, {}
  for k, v in pairs(_G) do E[k] = v end
  E.SCREEN_W, E.SCREEN_H = 360, 360
  -- what layout.fit cut, for SDK_DEBUG=1 (a long name, a model list... are
  -- cut on purpose; the SDK's own lines should not be)
  E.LAYOUT_CUT = function(s, cols) CUTS[s] = cols end
  local function nop() end
  E.rectfill = function(x, y, w, h)
    assert(type(x) == "number" and type(y) == "number" and type(w) == "number" and type(h) == "number", "rectfill: numbers")
    g_box("rectfill", x, y, w, h)
    g_clear(x, y, w, h)
  end
  E.rect = function(x, y, w, h)
    assert(type(x) == "number" and type(h) == "number", "rect: numbers")
    g_box("rect", x, y, w, h)
    -- a frame (a dialog's) stays over the hint row, which the dialog's keys take
    if not clipped and not G.off and caller() == CARTSRC and y < 320 and y + h > 320 then
      g_fail(string.format("a frame at %d,%d %dx%d crosses the hint row", x, y, w, h))
    end
  end
  E.line = function(x0, y0, x1, y1)
    assert(type(x0) == "number" and type(y1) == "number", "line: numbers")
    g_box("line", math.min(x0, x1), math.min(y0, y1), math.abs(x1 - x0) + 1, math.abs(y1 - y0) + 1)
  end
  E.sspr = function(sx, sy, w, h, dx, dy, dw, dh) g_box("sspr", dx, dy, dw or w, dh or h) end
  E.spr = function(n, x, y, w, h) g_box("spr", x, y, (w or 1) * 8, (h or 1) * 8) end
  E.map = function(cx, cy, sx, sy, w, h) g_box("map", sx, sy, w * 8, h * 8) end
  for _, n in ipairs({ "circ", "circfill", "tri", "pset" }) do E[n] = nop end
  for _, n in ipairs({ "cls", "zclear", "light3d", "fog3d", "lamp3d", "camera", "sky3d" }) do E[n] = nop end
  -- the 3D map preview (AGY-04): the host has no map3d engine, so a stub world
  E.map3d_build = function() return {} end
  E.map3d_height = function() return 0.0 end
  E.map3d_objects = function() return {} end
  E.map3d_draw = nop
  E.line3d = nop
  E.point3d = nop
  E.clip = function(x) clipped = x ~= nil end
  E.font = function(name)
    if name then cur_font = name == "6x12" and { 6, 12 } or { 8, 16 } end
    return cur_font[1], cur_font[2]
  end
  E.print = function(s, x, y)
    s, x, y = tostring(s), x or 0, y or 0
    texts[#texts + 1] = { s, x, y }
    local w = #s * cur_font[1]
    if theirs(caller()) then
      if x + w > 360 then ASSIST_OUT[s] = x end
      return x + w
    end
    g_box("print", x, y, w, cur_font[2], s)
    if cur_font[1] == 8 then
      local src = caller()
      if src == CARTSRC and x % 8 ~= 0 then g_fail(string.format("%q at x %d: not on a column of 8", s, x)) end
      g_mark(x, y, w, s, src)
    end
    return x + w
  end
  E.prompt = function(n, x, y)
    assert(type(n) == "string", "prompt: a name")
    assert(PADN[n] or n == n:lower(), "prompt: not a button or a key: " .. n)
    assert(not n:find(" "), "prompt: one key at a time: " .. n)
    if type(x) ~= "number" then return chip_w(n), 16 end
    texts[#texts + 1] = { "[" .. n .. "]", x, y }
    if theirs(caller()) then
      if x + chip_w(n) > 360 then ASSIST_OUT["[" .. n .. "]"] = x end
      return x + chip_w(n)
    end
    g_box("prompt", x, y, chip_w(n), 16, n)
    g_mark(x, y, chip_w(n), "[" .. n .. "]", caller())
    return x + chip_w(n)
  end
  E.lastinput = function() return last_kb and "keyboard" or nil end
  E.keydown = function() return false end
  E.time = function() return frame / 60 end
  E.stat = function() return 0 end
  E.log = function(...) io.write(table.concat({ ... }, "\t"), "\n") end
  E.quit = function() quitted = true end
  -- the buttons by name only: the SDK never asks btn(4) / btn(5) (on the
  -- RGB30 A and B are back and ok)
  local function name_of(i)
    if type(i) ~= "string" then
      if caller() == CARTSRC then numeric_btn = numeric_btn or i end
      return ({ [0] = "left", "right", "up", "down", "back", "ok", "x", "y", "start", "select" })[i]
    end
    assert(NAMES[i], "btn: not a button: " .. tostring(i))
    if i == "a" then return "back" elseif i == "b" then return "ok" end
    return i
  end
  E.btn = function(i) return held[name_of(i)] == true end
  E.btnp = function(i) local n = name_of(i); return held[n] == true and not prev_held[n] end
  E.pad = function()
    local b = 0
    for n in pairs(held) do b = b | (HID[n] or 0) end
    return b
  end
  E.stick = function() return 0, 0 end
  E.controller = function(p)
    p = p or 1
    return { kind = p == 1 and "builtin" or "none", layout = p == 1 and "nintendo" or "none", bluetooth = false,
             ok = "b", back = "a", color = 0x3070FF }
  end
  E.players = function() return 1, 1 end
  E.keyp = function() return table.remove(keyq, 1) end
  E.keyheld = function() return false end
  -- no mouse on the RGB30, unless a test plugs one in (MS.on)
  MS = { x = 0, y = 0, b = 0, w = 0, p = 0, on = o.mouse }
  E.mouse = function(on)
    if on ~= nil then return true end
    if not MS.on then return nil end
    return MS.x, MS.y, MS.b, MS.w, true
  end
  E.mousep = function(i) return MS.on and MS.p >> (i or 0) & 1 == 1 end
  E.keyhelp = function(list, title)
    for _, e in ipairs(list) do
      if type(e) == "table" then
        assert(not e[1]:find("f11") and not e[1]:find("f12"), "keyhelp: a key the kernel keeps: " .. e[1])
      end
    end
    keyhelp_list, keyhelp_title = list, title
    return 0
  end
  E.save = function(t) saved_t = t; return true end
  E.saved = function() return saved_t end
  E.cart_arg = function() return arg_t end
  -- a tool: cart_run and cart_tool leave it (the session plays the game, or
  -- would open the other tool: on the RGB30 none, the session would end)
  E.cart_run = function(p) ran = p; quitted = true end
  E.cart_tool = function(name, p) tooled = { name, p }; quitted = true end
  E.cart_build = function(path, kind)
    if not path:upper():match("%.BME$") then return false, path .. ": only a project (.bme) is built" end
    local dir, base = path:match("^(.*)/([^/.]+)%.[^.]*$")
    local ext = kind == "b16" and ".B16" or ".BM"
    local game = dir .. "/" .. base:upper() .. ext       -- bm_build / bm_build_b16: next to it, its 8.3 name
    local d = assert(read_file(host(path)))
    local f = assert(io.open(host(game), "wb")); f:write(d); f:close()
    add_file(game)
    built = game
    return true, game
  end
  E.timeslice = function() end
  E.code_tokens = function(s)
    local n = 0
    for _ in s:gmatch("[%w_]+") do n = n + 1 end
    return n
  end
  E.camera3d = nop
  E.project3d = function(x, y, z) return 244 + x * 10, 184 - y * 10, 5 end
  E.sget = function(x, y)
    if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return nil end
    return sheet.px[y * sheet.w + x]
  end
  E.sset = function(x, y, c)
    assert(math.type(x) == "integer" and math.type(y) == "integer", "sset: whole pixels")
    if x < 0 or y < 0 or x >= sheet.w or y >= sheet.h then return end
    sheet.px[y * sheet.w + x] = c
  end
  local function lay(l)
    if l == nil or l == 1 or l == lnames[1] then return mapc end
    if type(l) == "string" then
      for i, n in ipairs(lnames) do if n == l then return mlay[i] end end
      error("the map has no layer \"" .. l .. "\"")
    end
    return assert(mlay[l], "the map has no layer " .. tostring(l))
  end
  E.mget = function(x, y, l) return lay(l)[math.floor(y) * 4096 + math.floor(x)] or 0 end
  E.mset = function(x, y, v, l)
    assert(type(v) == "number", "mset: a number")
    lay(l)[y * 4096 + x] = v ~= 0 and v or nil
  end
  E.mlayers = function(list)
    if list then
      assert(#list >= 1 and #list <= 8, "mlayers: 1 to 8 layers")
      local old = {}
      for i, n in ipairs(lnames) do old[n] = i == 1 and mapc or mlay[i] end
      local nn, nl = {}, {}
      for i, e in ipairs(list) do nn[i], nl[i] = e, old[e] or {} end
      lnames, mlay, mapc = nn, nl, nl[1]
    end
    return table.move(lnames, 1, #lnames, 1, {})
  end
  E.fget = function(n, f)
    local v = flg[n] or 0
    if f == nil then return v end
    return v >> f & 1 == 1
  end
  E.fset = function(n, f, on)
    local bit = 1 << f
    flg[n] = on and (flg[n] or 0) | bit or (flg[n] or 0) & ~bit
  end
  E.mflags = function() return 0 end
  E.zones = function() return {} end
  E.zone = function() return nil end
  E.zspr = function(name) error("the sheet has no sprite zone \"" .. tostring(name) .. "\"") end
  E.zboxes = E.zspr
  E.net_ip = function() return nil end
  E.udp_open = function() return nil, "no network" end
  E.online = function() return false end
  E.note, E.noteoff = nop, nop
  E.SQUARE, E.TRIANGLE, E.SAW, E.NOISE, E.SINE = 0, 1, 2, 3, 4
  E.cart_data = function(t, ...)
    assert(t == 8 or t == 9, "cart_data: 8 (MESH) or 9 (ANIM)")
    if select("#", ...) == 0 then return sec[t] end
    local data = ...
    if data == "" then data = nil end
    sec[t] = data
    return true
  end
  E.model = function(name)
    for i, m in ipairs(mesh_models(sec[8])) do
      if m.name == name or i == name then
        local r = anim_rigs(sec[9])[m.name]
        if r and r.nv ~= m.nv then r = nil end
        return setmetatable({ model = m, rig = r }, MESH_MT)
      end
    end
    return nil
  end
  E.models = function()
    local out = {}
    for i, m in ipairs(mesh_models(sec[8])) do out[i] = m.name end
    return out
  end
  E.mesh = function(v, f) return setmetatable({ v = v, f = f }, MESH_MT) end
  E.mesh_cube = function() return setmetatable({ v = {}, f = {} }, MESH_MT) end
  E.mesh_sphere = E.mesh_cube
  E.draw3d = function(m) assert(getmetatable(m) == MESH_MT, "draw3d: a mesh"); draws = draws + 1 end
  E.clips = function(m)
    local out = {}
    for i, c in ipairs(m.rig and m.rig.clips or {}) do out[i] = { name = c.name, length = c.length, loop = c.loop } end
    return out
  end
  E.animate = function(m) assert(m.rig, "this mesh has no skeleton"); return 0 end
  E.bounds3d = function(m)
    if not m.model then return -0.5, -0.5, -0.5, 0.5, 0.5, 0.5 end
    local lo, hi = { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 }
    for _, p in ipairs(m.model.verts) do
      for k = 1, 3 do lo[k] = math.min(lo[k], p[k]); hi[k] = math.max(hi[k], p[k]) end
    end
    return lo[1], lo[2], lo[3], hi[1], hi[2], hi[3]
  end
  E.ls = function(dir)
    local out = {}
    for _, n in ipairs(on_sd[dir] or {}) do
      local d = read_file(host((dir == "/" and "" or dir) .. "/" .. n))
      out[#out + 1] = { name = n:upper(), size = d and #d or 0, dir = false }
    end
    return out
  end
  E.cart_load = function(path)
    local data = read_file(host(path))
    if not data then return nil, "no such file" end
    sec, mapc, lnames, mlay, flg = {}, {}, { "main" }, {}, {}
    local lua = ""
    for _, s in ipairs(sections_of(data)) do
      local t = s[1]
      if t == 6 and s[2]:sub(1, 4) ~= "BMAU" then t = 8 elseif t == 7 then t = 9 end
      if t == 8 or t == 9 then sec[t] = s[2] end
      if t == 1 then lua = s[2] end
      if t == 3 then
        local w = string.unpack("<I2", s[2])
        for i = 0, (#s[2] - 4) // 2 - 1 do
          local v = string.unpack("<I2", s[2], 5 + i * 2)
          if v ~= 0 then mapc[(i // w) * 4096 + i % w] = v end
        end
      end
    end
    local w, h, px = decode_sheet(data)
    sheet = { w = w, h = h, px = px }
    return { title = data:sub(25, 72):match("^[^\0]*"), author = data:sub(73, 104):match("^[^\0]*"),
             res = res_of(data), lua = lua, sheet_w = w, sheet_h = h, map_w = 256, map_h = 256 }
  end
  E.cart_new = function()
    sec, mapc, sheet, lnames, mlay, flg = {}, {}, { w = 256, h = 256, px = {} }, { "main" }, {}, {}
  end
  -- cart_save as the runtime's for a tool (write_target): any file but a
  -- game, a project of an earlier run too; a game (.bm, .b16) is read only:
  -- the console asks (yes here) and writes its editable copy next to it
  -- (bm_copy_name: NAME.BME, NAME1.BME...), a new one becomes its project;
  -- true and the file written when it is not the one asked
  E.cart_save = function(path, t)
    assert(type(t.lua) == "string", "cart_save: the code")
    local asked = path
    local up = path:upper()
    if up:match("%.BM$") or up:match("%.B16$") then
      local dir, base = path:match("^(.*)/([^/.]+)%.[^.]*$")
      if exists(path) then
        local stem = base:upper():sub(1, 8)
        path = nil
        for k = 0, 9 do
          local p = dir .. "/" .. (k == 0 and stem or stem:sub(1, 7) .. k) .. ".BME"
          if not exists(p) then path = p; break end
        end
        if not path then
          refused = refused + 1
          return false, asked .. " is a game, read only, and its copy has no free name"
        end
        copies = copies + 1
      else
        path = dir .. "/" .. base .. ".BME"
      end
    end
    local px = {}
    for i = 0, sheet.w * sheet.h - 1 do
      local c = sheet.px[i]
      px[#px + 1] = c and string.char(c >> 16 & 255, c >> 8 & 255, c & 255, 255) or "\0\0\0\0"
    end
    local cells = { string.pack("<I2I2", 256, 256) }
    for i = 0, 256 * 256 - 1 do cells[#cells + 1] = string.pack("<I2", mapc[(i // 256) * 4096 + i % 256] or 0) end
    local secs = { { 1, t.lua }, { 2, string.pack("<I2I2", sheet.w, sheet.h) .. table.concat(px) },
                   { 3, table.concat(cells) } }
    if sec[8] then secs[#secs + 1] = { 8, sec[8] } end
    if sec[9] then secs[#secs + 1] = { 9, sec[9] } end
    local f = assert(io.open(host(path), "wb"))
    f:write(pack_cart(t.title or "", t.author or "", t.res or "640x360", secs))
    f:close()
    add_file(path)
    saves, last_save = (saves or 0) + 1, { path = path, t = t }
    if path ~= asked then return true, path end
    return true
  end
  -- the sound banks of the files (cart_put_audio writes one, cart_audio reads
  -- it: the AUDIO section of the project), and the last preview played
  E.cart_put_audio = function(path, bank)
    audio_files[path:upper()] = bank
    audio_puts = audio_puts + 1
    return true
  end
  E.cart_audio = function(path)
    if path == nil then return nil end
    return audio_files[path:upper()] or false
  end
  E.audio_bank = function(bank) audio_last = { bank = bank } end
  E.audio_play = function(v, sound, note, vol, fx, ms) audio_last = { v = v, sound = sound, note = note, vol = vol, fx = fx, ms = ms, bank = audio_last and audio_last.bank } end
  E.sfx = function(n) audio_last = { sfx = n, bank = audio_last and audio_last.bank } end
  -- the assistant's knowledge: the Pi's kernel (ask, entries, recipes) or,
  -- as on the RGB30, only the music
  if o.ai then
    E.ai = {
      list = function(kinds) return entries_of(kinds) end,
      ask = function(q, opt)
        local hits = {}
        for _, e in ipairs(entries_of(opt and opt.kinds)) do hits[#hits + 1] = { id = e.id, title = e.title, kind = e.kind, score = 0.9 } end
        return hits, 40
      end,
      entry = function(id) return ENTRIES[id] end,
      near = function() return nil end,
      sprite = function(req, opt)
        local n = opt and opt.size or 16
        local s = { w = n, h = n, name = "slime", gen = "slime", px = {} }
        for i = 1, n * n do s.px[i] = (i % 3 == 0) and -1 or 0x30C040 end
        return s
      end,
      mesh = function(q, opt)
        local faces = {}
        local function quad(a, b, c, d, col) faces[#faces + 1] = { p = { a, b, c, d }, c = col, b = { 1, 1, 1, 1 } } end
        quad({ 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 }, 0xD83A3A)
        quad({ 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 }, 0xD83A3A)
        return { gen = "cube", name = "cube", seed = opt and opt.seed or 1, faces = faces }
      end,
    }
  else
    E.ai = { music = function() return nil end }
  end
  -- the kernel's libraries; "words" is a build of the Pi's: on the RGB30 an
  -- empty chunk (require gives true), or o.words
  E.require = function(name)
    if loaded[name] ~= nil then return loaded[name] end
    if name == "words" then loaded[name] = o.words or true; return loaded[name] end
    local file = ({ assist = "/src/ai/assist.lua", bm3d = "/src/script/bm3d.lua", bmlib = "/src/script/bmlib.lua",
                    bmnet = "/src/script/bmnet.lua", bmui = "/src/script/bmui.lua", padtype = "/src/ai/padtype.lua",
                    predict = "/src/ai/predict.lua" })[name]
    if not file then error("require: no " .. name .. " here") end
    loaded[name] = assert(loadfile(ROOT .. file, "t", E))(name)
    if loaded[name] == nil then loaded[name] = true end
    return loaded[name]
  end
end

-- the globals the SDK reads that the stand-ins do not have (SDK_DEBUG=1):
-- a name the console has not either would be a mistake of the SDK's
local MISSING = {}
local function run_sdk(arg, o)
  new_env(arg, o)
  setmetatable(E, { __index = function(_, k) MISSING[k] = true end })
  g_begin("load")
  assert(loadfile(CART, "t", E))()
  E._init()
end

local tag = "start"
local function frames(n)
  for _ = 1, n or 1 do
    frame = frame + 1
    texts = {}
    g_begin(tag)
    E._update()
    E._draw()
    for k, v in pairs(held) do prev_held[k] = v end
    for k in pairs(prev_held) do if not held[k] then prev_held[k] = nil end end
  end
end

local function screen()
  local lines = {}
  for _, t in ipairs(texts) do lines[#lines + 1] = t[1] end
  return table.concat(lines, "\n")
end
local function sees(s) return screen():find(s, 1, true) ~= nil end
local function status()
  local out = {}
  for _, t in ipairs(texts) do if t[3] == 336 or t[3] == 320 then out[#out + 1] = t[1] end end
  return table.concat(out, " ")
end
local function tabs()
  local out = {}
  for _, t in ipairs(texts) do if t[3] == 0 then out[#out + 1] = t[1] end end
  return table.concat(out, " ")
end

-- the pad: a press (held a frame, then let go), several at once
local function press(...)
  local names = { ... }
  for _, n in ipairs(names) do held[n] = true end
  frames(1)
  for _, n in ipairs(names) do held[n] = nil end
  frames(1)
end
local function with(mod, ...)                  -- a button held while others are pressed
  held[mod] = true
  frames(1)
  press(...)
  held[mod] = nil
  frames(1)
end
local function key(...)
  for _, k in ipairs({ ... }) do keyq[#keyq + 1] = k; frames(1) end
end

----------------------------------------------------------------- the checks

-- a fresh start on the RGB30: no file, the project page of a new game,
-- 360x360 and the target .b16
saved_t = nil
run_sdk(nil)
tag = "project"
frames(30)
check(sees("PROJECT") and sees("title") and sees("New game"), "the hub: the project's rows")
check(sees("360x360") and sees(".b16"), "a new project: 360x360, the target .b16")
check(sees("CONTENTS") and sees("tokens"), "the hub: the contents measured")
check(not sees("OPEN IN") and not sees("bm Code"), "no programs of the suite: the RGB30's kernel has none")
check(tabs():find("[L1]", 1, true) and tabs():find("project", 1, true) and tabs():find("[R1]", 1, true),
      "the tabs: L1, the pages, R1 with the pad: " .. tabs())
check(sees("[ok]") and sees("do it") and sees("[X]"), "the hints: the pad's buttons")
check(keyhelp_title == "bm SDK" and keyhelp_list, "F12: the keys of the page")

-- the pages with L1 / R1, not around; Y + left / right too
local order = {}
for _ = 1, 6 do press("r1"); order[#order + 1] = tabs() end
check(sees("Exit bm SDK"), "R1 four times: the menu, the last page")
press("r1")
check(sees("Exit bm SDK"), "R1 on the last page: it stays (no wrap)")
for _ = 1, 5 do press("l1") end
check(sees("CONTENTS"), "L1 back to the project")
with("y", "right")
check(sees("type") and not sees("PROJECT"), "Y + right: the code: " .. status())
with("y", "left")
with("y", "down")
tag = "devkit"
frames(5)
check(sees("dev kit") and sees("data") and sees("not saved yet"), "Y + down: the dev kit")
check(sees("try") and sees("not tried yet: X > Try the game"), "the dev kit: how to try it: " .. screen():gsub("\n", " | "))
with("y", "up")
check(sees("CONTENTS"), "Y + up: back to the project")

-- Start: the menu and back
press("start")
tag = "menu"
check(sees("Exit bm SDK") and sees("Continue") and sees("Save as..."), "Start: the menu")
check(sees("every action of the page") and sees("[START]"), "the menu: the pad's keys")
press("start")
check(sees("CONTENTS"), "Start again: back to the page")

-- the title with the pad: ok on the row, padtype's keyboard, Start takes it
tag = "ask"
press("down")                                    -- author, then back to the title
press("up")
press("ok")
check(sees("title") and sees("New game_") or sees("New game"), "ok on the title: the line of text")
check(sees("[START]") and sees("done"), "the dialog's hints: Start takes the line")
for _ = 1, 8 do press("back") end               -- "New game" erased, a character at a time
press("ok")                                      -- the keyboard's key: q
press("right")
press("ok")                                      -- w
press("start")
tag = "project"
frames(2)
check(sees("qw"), "the title typed with the pad: " .. (sees("qw") and "qw" or screen():sub(1, 80)))

-- back on an empty line closes the dialog
press("ok")
press("back"); press("back")                     -- "qw" erased
press("back")                                    -- an empty line: closed
frames(1)
check(not (sees("[START]") and sees("done")), "back on an empty line: the dialog closes")
check(sees("qw"), "closing does not change the title")

-- the actions of every page (X): each entry run with ok, then back out of
-- whatever it opened (a list: back; a line of text: back until it is
-- empty, then once more), and the SDK is on a page with its tabs
local function dialog_open() return sees("take it") or (sees("[START]") and sees("done")) end
local function close_all()
  for _ = 1, 80 do
    if not dialog_open() then break end
    press("back")
  end
  frames(1)
end
-- the rows of the open list: its texts in the dialog's column (x 24)
local function list_rows()
  local out, after = {}, false
  for _, t in ipairs(texts) do
    if t[1]:find("^actions: ") then after = true
    elseif after and t[2] == 24 and t[3] >= 32 and t[3] < 320 then out[#out + 1] = t[1] end
  end
  return out
end

local pages = {
  { "project", "the project", function() end },
  { "devkit", "the dev kit", function() with("y", "down") end },
  { "code", "the code", function() press("r1") end },
  { "sprites", "the sprites", function() press("r1"); press("r1") end },
  { "map", "the map", function() press("r1"); press("r1"); with("y", "down") end },
  { "3D", "3D", function() press("r1"); press("r1"); press("r1") end },
  { "sounds", "the sounds", function() for _ = 1, 4 do press("r1") end end },
  { "sfx", "the effects", function() for _ = 1, 4 do press("r1") end; with("y", "down") end },
}
for _, pg in ipairs(pages) do
  run_sdk(nil)
  frames(5)
  tag = pg[1]
  pg[3]()
  frames(2)
  press("x")
  tag = pg[1] .. " actions"
  local title = "actions: " .. pg[2]
  check(sees(title), "X on " .. pg[1] .. ": " .. title)
  -- the whole list: down until the first row is chosen again
  local seen_rows, n = {}, 0
  for _ = 1, 60 do
    for _, r in ipairs(list_rows()) do if not seen_rows[r] then seen_rows[r] = true; n = n + 1 end end
    press("down")
  end
  check(seen_rows["Save as..."] and seen_rows["Open..."] and seen_rows["Menu"],
        pg[1] .. ": the actions end with save, open, menu")
  check(seen_rows["Try the game"] and seen_rows["Build the game .bm"], pg[1] .. ": try and build (a tool)")
  check(not seen_rows["Open in bm Studio"] and not seen_rows["Open in bm Mesh"], pg[1] .. ": no other program")
  press("back")
  check(not dialog_open(), pg[1] .. ": back closes the actions")
  -- each entry: X, down i times, ok, out of any dialog
  local ran_n = 0
  for i = 0, n - 1 do
    run_sdk(nil)
    frames(3)
    pg[3]()
    frames(1)
    tag = pg[1] .. " action " .. i
    press("x")
    for _ = 1, i do press("down") end
    press("ok")
    frames(3)
    close_all()
    if quitted then break end
    check(not dialog_open() and tabs():find("menu", 1, true) ~= nil, pg[1] .. " action " .. i .. ": back on a page")
    ran_n = ran_n + 1
  end
  check(ran_n == n and n > 5, pg[1] .. ": all its " .. n .. " actions run (" .. ran_n .. ")")
end
check(not numeric_btn, "the SDK never asks a button by number: " .. tostring(numeric_btn))


-- the sounds page (APP-6d/e/f): the project's sound bank on the pad. The
-- bank is written with the project (cart_put_audio after cart_save), what
-- the file had after the effects is kept byte for byte, an edit is heard.
local function bank_bytes(wave, tail)
  local tone = {}
  for i = 1, 21 do tone[i] = 0 end
  tone[12] = 40
  return "BMAU" .. string.pack("<BBBBBBxxxxxx", 2, 1, 0, 1, 0, 0)
         .. string.pack("<c8BBBBBBBbBBBbxxxx", "TEST", wave, 128, 200, 1, 0, 255, 10, 0, 0, 0, 0, 0)
         .. string.char(table.unpack(tone)) .. "\0\0\0" .. tail
end
local function settled()                          -- the status line, once the message is gone
  frames(250)
  return status()
end
local function open_sounds(arg)
  if not arg then saved_t = nil end              -- a new project, not the last one
  run_sdk(arg)
  frames(3)
  for _ = 1, 4 do press("r1") end
  frames(2)
end
do
  tag = "snd"
  audio_files, audio_puts, audio_last = {}, 0, nil
  open_sounds(nil)
  check(sees("SOUNDS") and sees("(empty)") and tabs():find("snd", 1, true), "R1 x4: the sounds page: " .. tabs())
  check(settled():find("sound 0", 1, true) and settled():find("SQUARE", 1, true), "sound 0: SQUARE: " .. status())
  for _ = 1, 4 do press("right") end
  check(sees("SINE") and settled():find("SINE", 1, true), "right x4: wave SINE: " .. status())
  check(sees("*"), "an edit: the file is not saved")
  press("ok")
  check(audio_last and audio_last.sound == 0 and audio_last.note == 69 and audio_last.vol == 255 and audio_last.ms == 350
        and type(audio_last.bank) == "string" and audio_last.bank:sub(1, 4) == "BMAU",
        "ok plays A4 of slot 0 with the bank as it is now")
  press("down")
  for _ = 1, 3 do press("right") end
  press("r2")
  check(settled():find("sound 1", 1, true), "R2: sound 1: " .. status())
  press("l2")
  key("^s")
  key("\n")                                      -- the name MYGAME.BME is accepted
  local got = audio_files["/CARTS/MYGAME.BME"]
  check(got and got:sub(1, 4) == "BMAU" and got:byte(5) == 2 and got:byte(6) == 1, "Ctrl+S writes the bank with the project")
  check(got and got:byte(25) == 4 and got:byte(27) == 203, "the bank: sound 0 is SINE, its volume 203: "
        .. tostring(got and got:byte(27)))
  check(not tabs():find("*", 1, true), "saved: no star on the tabs")
  local puts = audio_puts
  key("^s")
  check(audio_puts == puts + 1, "saving again writes the bank again (after the first edit)")

  -- a bank of the file: read, edited, written with what follows the effects as it was
  local tail = string.pack("<BBxx", 16, 0) .. "TAIL"
  local orig = bank_bytes(4, tail)
  saved_t = nil
  run_sdk(nil); frames(3)
  key("^S")
  for _ = 1, 12 do key("\b") end
  for c in ("TB"):gmatch(".") do key(c) end
  key("\n")
  audio_files["/CARTS/TB.BME"] = orig
  local n0 = audio_puts
  open_sounds({ path = "/carts/TB.BME", back = false })
  check(sees("TEST") and settled():find("SINE", 1, true), "the file's bank: sound 0 TEST, SINE: " .. status())
  key("^s")
  frames(2)
  check(audio_puts == n0 and audio_files["/CARTS/TB.BME"] == orig, "saving without touching the sounds leaves the bank alone")
  press("right")
  check(settled():find("METAL", 1, true), "right: the next wave: " .. status())
  press("left")
  key("^s")
  check(audio_files["/CARTS/TB.BME"] == orig, "an edit undone: pack(parse(bank)) is the same bytes")
  press("right")
  key("^s")
  local now = audio_files["/CARTS/TB.BME"]
  check(now ~= orig and now:sub(-#tail) == tail and now:byte(8) == 1 and now:byte(25) == 5,
        "an edit: the wave is METAL and the patterns after the effects are kept")

  -- a bank the SDK cannot read: the page says so and nothing is written over it
  audio_files["/CARTS/TB.BME"] = "BMAUxxxxxxxxxxxxxxxxxxxx"
  open_sounds({ path = "/carts/TB.BME", back = false })
  check(sees("cannot be read"), "a broken bank: the page says so")
  press("right"); press("ok")
  key("^s")
  frames(2)
  check(audio_files["/CARTS/TB.BME"] == "BMAUxxxxxxxxxxxxxxxxxxxx", "a broken bank is not written over")

  -- the effects: Y + down is the other view; ok puts a note (A4 of the sound
  -- chosen on the sounds page) and plays it, up / down change it, back clears
  open_sounds(nil)
  with("y", "down")
  tag = "sfx"
  check(sees("EFFECTS") and tabs():find("sfx", 1, true), "Y + down: the effects: " .. tabs())
  press("ok")
  check(audio_last and audio_last.sound == 0 and audio_last.note == 69 and audio_last.vol == 255 and audio_last.ms == 350,
        "ok on a step: A4 of sound 0 is put and heard")
  press("up")
  check(settled():find("A#4", 1, true), "up: the note, A#4: " .. status())
  press("right")
  press("ok")
  check(audio_last.note == 70, "the next step takes the last note (70): " .. tostring(audio_last.note))
  press("back")
  check(settled():find("empty", 1, true), "back: the step is empty")
  press("left"); press("r2")
  check(settled():find("effect 1", 1, true), "R2: effect 1: " .. status())
  press("l2")
  key("+")
  key("^s")
  key("\n")
  local fx = audio_files["/CARTS/MYGAME.BME"]
  check(fx and fx:byte(7) == 1 and fx:byte(6) == 0, "the effect is written: one effect, no sound")
  check(fx and string.unpack("<H", fx, 25) == 70 and string.unpack("<I4", fx, 33) == (70 | 255 << 16),
        "its speed 70 ms and its first step A#4 (note 70, sound 0, volume 255)")
  with("y", "up")
  check(sees("SOUNDS"), "Y + up: the sounds again")
  key("f7")
  check(sees("EFFECTS"), "F7 on the sounds page: the effects")
  key("f7")
  check(sees("SOUNDS"), "F7 again: the sounds")
  key("f1"); key("f7")
  check(sees("SOUNDS") or sees("EFFECTS"), "F7: the page from another")

  -- sounds from a file (the Lib, key i): a file of bm/lib, then one of its sounds
  on_sd["/bm/lib"] = { "kit.bm" }
  audio_files["/BM/LIB/KIT.BM"] = bank_bytes(5, "")      -- sound 0: TEST, METAL
  open_sounds(nil)
  key("i")
  check(sees("sounds from a file") and sees("KIT.BM"), "i: the files with sounds")
  press("ok")
  check(sees("a sound of KIT.BM") and sees("TEST"), "ok: the sounds of the file")
  press("ok")
  check(sees("TEST") and settled():find("METAL", 1, true), "the sound is in slot 0: " .. status())
  check(sees("*"), "an import is an edit")
  key("i")
  press("ok"); press("ok")
  check(settled():find("sound 1", 1, true), "again: the next empty slot (1): " .. status())
  on_sd["/bm/lib"] = nil
end

-- the code page with the pad: the cross moves, ok types with padtype's keyboard
run_sdk(nil)
frames(3)
tag = "code"
press("r1")
press("down")
press("down")
press("ok")
tag = "code typing"
frames(3)
check(status():find("PAD keyboard", 1, true) or sees("pad typing: keyboard"), "ok on the code: typing with the keyboard: " .. status())
local lines0 = #texts
press("ok")                                      -- the keyboard's key (q)
press("start")
check(sees("Exit bm SDK"), "Start while typing: the menu")
press("start")
frames(2)
check(screen():find("q", 1, true) ~= nil, "a key typed into the code")
press("select")                                  -- keyboard -> compose
frames(2)
check(status():find("compose", 1, true) or sees("compose"), "Select: the composition: " .. status())
press("up")                                      -- a consonant (t), then it waits for its double
frames(30)
press("select")                                  -- compose -> off
frames(2)
check(not status():find("PAD", 1, true), "Select again: typing off: " .. status())
press("back")
check(sees("Exit bm SDK"), "back on the code (not typing): the menu")
check(lines0 > 0, "the code drawn")

-- with the word lists (a console that has them, as the Pi): the
-- composition comes first, and its prediction completes a syllable
local fake = { uni = "function 10 f\nlocal 9\nend 8\nthen 7\n", big = "" }
run_sdk(nil, { words = { lua = fake, it = fake, en = fake } })
frames(3)
press("r1")
press("ok")
frames(2)
check(status():find("PAD compose", 1, true) or sees("pad typing: compose"), "with the word lists: compose first: " .. status())
press("select"); press("select")
frames(2)
check(not status():find("PAD", 1, true), "compose, keyboard, off: " .. status())

-- the 2D page with the pad: ok draws, back picks, L2 / R2 the colour, Y the sheet
run_sdk(nil)
frames(3)
key("^n")
tag = "templates"
check(sees("new project from a template"), "Ctrl+N: the templates")
press("down")
press("ok")
frames(2)
check(sees("Platform 2D"), "the platform from the pad")
press("r1"); press("r1")
tag = "sprites"
check(sees("SPRITES"), "R1 R1: the sprites")
local before = sheet.px[9]
press("right")
press("r2")
press("ok")
check(sheet.px[9] ~= nil and sheet.px[9] ~= before, "ok draws a pixel")
press("back")
press("l2")
held.y = true; frames(1); held.y = nil; frames(2)
check(sees("on the sheet"), "Y alone: choosing on the sheet")
press("right")
press("ok")
check(not sees("on the sheet"), "ok: back to the canvas")
with("y", "down")
tag = "map"
check(sees("MAP"), "Y + down: the map")
press("ok")
check(mapc[0] ~= nil, "ok places the tile")
held.y = true; frames(1); held.y = nil; frames(2)
tag = "map tiles"
check(sees("the tile: the cross, then ok"), "Y alone: the tiles")
press("ok")
tag = "map"
key("z", "\t")
check(sees("USED 16x16"), "Tab with 16x16 brush: the used 16x16 tiles palette")
key("z")
check(status():find("full sprite sheet", 1, true), "z in Tab picker: full sprite sheet")
key("\n", "-")
check(sees("4px") or status():find("1:2", 1, true), "-: map zoom out to 4px")
key("m")
check(sees("OVERVIEW") and sees("256x256"), "m: 256x256 overview in one screen")
key("m", "z")
press("r2")
press("back")

-- every template: its game at 360x360 for 400 frames on the stand-ins
local function play_template(code, name)
  local G2 = {}
  for k, v in pairs(E) do G2[k] = v end
  G2._init, G2._update, G2._draw = nil, nil, nil
  G2.save = function() return true end
  G2.saved = function() return nil end
  local h = {}
  G2.btn = function(i) return h[i] == true end
  G2.btnp = function(i) return h[i] == true end
  G2.keyp = function() return nil end
  local fn, err = load(code, "=" .. name, "t", G2)
  check(fn, name .. ": compiles: " .. tostring(err))
  if not fn then return end
  G.off = true
  local ok, e = pcall(function()
    fn()
    if G2._init then G2._init() end
    for f = 1, 400 do
      h = { [0] = f % 90 < 30, [1] = f % 90 >= 45, [2] = f % 50 < 10, [3] = f % 70 < 10, [4] = f % 37 == 0 }
      if G2._update then G2._update() end
      if G2._draw then G2._draw() end
    end
  end)
  G.off = false
  check(ok, name .. ": runs 400 frames at 360x360: " .. tostring(e))
end

-- the Stages 2D template on a fresh G2 (as play_template): every stage ends
-- at once (mget = 7). Run 1 plays to the end and saves; run 2, same store,
-- continues at stage 3 (down at frame 2, ok at frame 10).
local function stages_flow(code)
  local store = {}                               -- what save(t) keeps
  local logs, ok_now, down_now = {}, false, false
  local function play(n, pulse)
    local G2 = {}
    for k, v in pairs(E) do G2[k] = v end
    G2._init, G2._update, G2._draw = nil, nil, nil
    G2.btn = function() return false end
    G2.btnp = function(i) return (i == "ok" and ok_now) or (i == 3 and down_now) or false end
    G2.mget = function() return 7 end
    G2.log = function(...)
      local parts = {}
      for i = 1, select("#", ...) do parts[#parts + 1] = tostring((select(i, ...))) end
      logs[#logs + 1] = table.concat(parts, " ")
    end
    G2.save = function(t) store.v = t; return true end
    G2.saved = function() return store.v end
    local fn, err = load(code, "=Stages 2D", "t", G2)
    check(fn, "Stages 2D: compiles: " .. tostring(err))
    if not fn then return end
    G.off = true
    local ok, e = pcall(function()
      fn()
      if G2._init then G2._init() end
      for f = 1, n do
        ok_now, down_now = pulse(f)
        if G2._update then G2._update() end
        if G2._draw then G2._draw() end
      end
    end)
    G.off = false
    check(ok, "Stages 2D: runs " .. n .. " frames: " .. tostring(e))
  end

  -- run 1: ok every 10th frame, to the end and the save
  play(200, function(f) return f % 10 == 0, false end)
  local best = store.v and store.v.best
  local want = {
    "stages: title best -", "stages: stage 1", "stages: clear 1", "stages: stage 2",
    "stages: clear 2", "stages: stage 3", "stages: clear 3",
    function(l) return l:sub(1, 15) == "stages: ending " end,
    "stages: saved",
  }
  local j, missing = 1, nil
  for _, w in ipairs(want) do
    local hit = false
    while j <= #logs and not hit do
      local l = logs[j]
      if type(w) == "function" then hit = w(l) else hit = l == w end
      j = j + 1
    end
    if not hit then
      missing = type(w) == "string" and w or "stages: ending "
      break
    end
  end
  check(missing == nil, "Stages 2D run 1: in order, the stages, the ending, the save; missing: " .. tostring(missing))
  check(logs[#logs] == "stages: title best " .. tostring(best),
        "Stages 2D run 1: the title with the best last: " .. tostring(logs[#logs]))
  check(math.type(best) == "integer", "Stages 2D: the best is an integer: " .. tostring(best))
  check(store.v ~= nil and store.v.reached == 3, "Stages 2D: reached 3 in the save: " .. tostring(store.v and store.v.reached))

  -- run 2, same store: the title shows the best, then CONTINUE at stage 3
  logs = {}
  play(200, function(f) return f == 10, f == 2 end)
  check(logs[1] == "stages: title best " .. tostring(best),
        "Stages 2D run 2: the title shows the best: " .. tostring(logs[1]))
  local cont = false
  for _, l in ipairs(logs) do if l == "stages: stage 3" then cont = true end end
  check(cont, "Stages 2D run 2: CONTINUE STAGE 3: stage 3 in the logs")
end

-- saving with the pad: Save as from the actions, the name typed, Start
local names = { "Empty 2D", "Platform 2D", "Top-down 2D", "Shooter 2D", "Versus 2D", "Online 2D", "3D scene",
                "3D with models", "Stages 2D" }
for i, nm in ipairs(names) do
  run_sdk(nil)
  frames(2)
  key("^n")
  for _ = 2, i do press("down") end
  press("ok")
  frames(2)
  tag = "save " .. nm
  press("start")                                -- the menu: Continue, New, Open, Save, Save as...
  for _ = 1, 4 do press("down") end
  press("ok")
  check(sees("file name"), nm .. ": Save as asks the name")
  for _ = 1, 10 do press("back") end           -- "MYGAME.BME" erased
  -- typed with the keyboard: t, then the digit row's i
  press("up")                                  -- the digit row: 1 2 3 ...
  for _ = 2, i do press("right") end
  press("ok")
  press("start")
  frames(2)
  check(last_save and last_save.path == "/carts/" .. i .. ".BME", nm .. ": saved as " .. i .. ".BME: " ..
        tostring(last_save and last_save.path))
  if last_save then play_template(last_save.t.lua, nm) end
  if nm == "Stages 2D" and last_save then stages_flow(last_save.t.lua) end
end

-- saving again in the same run: the same file; in a new run too (a tool
-- writes its project where it is: no new name, the runtime never refuses)
run_sdk(nil)                                     -- (the last project comes back: saved())
frames(3)
check(sees("9.BME") or sees("Stages 2D"), "the last project (9.BME, Stages 2D) opens by itself: " .. tabs())
key("^S")
check(sees("file name"), "Ctrl+Shift+S: Save as")
for _ = 1, 12 do key("\b") end
for c in ("KEEP"):gmatch(".") do key(c) end
key("\n")
check(last_save.path == "/carts/KEEP.BME", "saved as KEEP.BME: " .. last_save.path)
key("f2", "-")
key("^s")
check(last_save.path == "/carts/KEEP.BME" and refused == 0, "Ctrl+S again in the same run: the same file")
run_sdk(nil)
frames(3)
check(sees("KEEP.BME") or tabs():find("KEEP", 1, true), "a new run: KEEP.BME again: " .. tabs())
local n_saves = saves
key("f2", "-")
key("^s")
frames(2)
check(not sees("file name") and saves == n_saves + 1 and last_save.path == "/carts/KEEP.BME",
      "a new run: Ctrl+S writes KEEP.BME again, no new name: " .. last_save.path)
check(refused == 0 and copies == 0, "the runtime never refused a save, no copy of a project (" .. refused .. ")")

-- the build and the try: the project's game next to it (the Games tab
-- lists the game: the message says so); F5 leaves for the console to play it
key("^b")
check(built == "/carts/KEEP.BM", "Ctrl+B builds the game: " .. tostring(built))
check(status():find("built /carts/KEEP.BM", 1, true) and status():find("it is in Games", 1, true),
      "Ctrl+B says where the game is: " .. status())
key("^B")
check(built == "/carts/KEEP.B16", "Ctrl+Shift+B exports .b16: " .. tostring(built))
check(not quitted and not ran, "building does not leave the SDK")
key("f5")
check(ran == "/carts/KEEP.BME" and quitted, "F5 tries the project (the SDK leaves for it): " .. tostring(ran))

-- the suite's other programs are not in the RGB30's kernel: not offered,
-- and their keys say so with no cart_tool (it would end the session)
run_sdk({ path = "/carts/KEEP.BME", back = false })
frames(3)
check(not sees("open in") and not sees("OPEN IN"), "no row of the suite's programs")
key("1")
check(not tooled and not quitted and status():find("bm Code is on the Pi", 1, true), "1: bm Code is not here: " .. status())
key("f4")
frames(2)
key("\n")
check(not tooled and not quitted and status():find("bm Studio is on the Pi", 1, true),
      "Enter on the 3D page: bm Studio is not here: " .. status())
press("x")
check(sees("The code to draw it") and not sees("Open in bm Studio"), "the 3D actions: no other program")
press("back")
run_sdk({ path = "/carts/KEEP.BME" }, { ai = true })
frames(3)
key("f4", "f6")
frames(2)
check(sees("Cubo"), "the assistant on a console that has it (3D recipes)")
key("esc")
frames(2)

-- a game from bm/ (Open, or the menu's "Open in bm SDK"): it opens as it
-- is; saving it, the console makes its editable copy next to it, a project
-- that builds a game in bm/, which the Games tab shows; the 3D page, models
run_sdk(nil)
frames(3)
key("^o")
check(sees("/bm/VILLAGE.B16"), "Open: the games in bm/")
press("ok")                                      -- (the first row: /bm/ comes before /carts/)
frames(2)
check(sees("Studio Village"), "the game opened")
check(status():find("editable copy", 1, true), "a game: saving makes its editable copy: " .. status())
tag = "3D village"
press("r1"); press("r1"); press("r1")
frames(3)
local nmodels = #E.models()
check(nmodels > 3 and sees("MODELS " .. nmodels), "the 3D page: the models (" .. nmodels .. ")")
check(draws > 0, "a model drawn")
press("down"); press("down"); press("right"); press("ok")
press("l2"); press("r2")
press("x")
check(sees("The code to draw it"), "the 3D actions")
press("back")
key("f6")
check(status():find("assistant is not on this console", 1, true), "F6 on the RGB30: no assistant: " .. status())
key("^s")
check(copies == 1 and last_save.path == "/bm/VILLAGE.BME", "Ctrl+S: the editable copy next to the game: " ..
      last_save.path)
check(status():find("saved /bm/VILLAGE.BME", 1, true), "the SDK goes on with the copy: " .. status())
key("^b")
check(built == "/bm/VILLAGE.BM" and status():find("it is in Games", 1, true), "Ctrl+B: the game in bm/: " .. status())
check(refused == 0, "the runtime never refused a save (" .. refused .. ")")

-- the keyboard: the same pages with its keys, the hints with keys
run_sdk(nil, { keyboard = true })
tag = "keyboard"
frames(3)
key("x")                                         -- (a key on the project page does nothing harmful)
for _, k in ipairs({ "f1", "f1", "f2", "f3", "f3", "f4", "esc" }) do
  key(k)
  frames(2)
end
check(sees("Exit bm SDK") and sees("Ctrl+Shift+S"), "the menu with the keyboard's shortcuts")
key("esc", "f2")
for c in ("x = 1"):gmatch(".") do key(c) end
check(sees("x = 1") or sees("x"), "typing on the keyboard")

-- a long message: on two rows, over the hints (the menu's "Open in bm
-- SDK" on a game: cart_arg() = {path, back = false})
run_sdk({ path = "/bm/VILLAGE.B16", back = false })
frames(2)
local r320, r336
for _, t in ipairs(texts) do
  if t[3] == 320 and t[1]:find("^opened VILLAGE.B16") then r320 = t[1] end
  if t[3] == 336 and t[1]:find("copy", 1, true) then r336 = t[1] end
end
check(r320 and r336, "a long message on the two status rows: " .. status())

-- the mouse (a Bluetooth one): the tabs, the context menu inside 360
run_sdk(nil, { mouse = true })
tag = "mouse"
frames(3)
local function mouse(x, y, b, w)
  local was = MS.b
  MS.x, MS.y, MS.b, MS.w = x, y, b or 0, w or 0
  MS.p = MS.b & ~was
  frames(1)
  MS.p, MS.w = 0, 0
end
local function click(x, y, b) mouse(x, y, 0); mouse(x, y, b or 1); mouse(x, y, 0); frames(1) end
click(340, 200, 2)
check(sees("Save as...") and sees("Menu"), "the right button: the context menu")
press("back")
check(not sees("Save as...") and sees("PROJECT"), "back on the pad closes the mouse's menu (and only that)")
click(340, 200, 2)
click(10, 10)
local cx
for _, t in ipairs(texts) do if t[1] == "code" and t[3] == 0 then cx = t[2] end end
if cx then click(cx + 4, 8) end
check(sees("type") and not sees("PROJECT"), "a click on the tab: the code: " .. status())

-- the dev kit of a .b16 that uses what the profile leaves out (the shooter:
-- math.random), every row of it inside the page
run_sdk(nil)
frames(2)
key("^n")
for _ = 1, 3 do press("down") end
press("ok")
tag = "devkit shooter"
with("y", "down")
frames(30)
check(sees("math.random") and sees("things a .b16 will not have"), "the dev kit: what a .b16 leaves out")

-- back from a try (a tool session): the numbers on the project page and in
-- the dev kit; an error: the code page on its line
run_sdk({ path = "/carts/KEEP.BME", back = true, run = { frames = 600, secs = 10, fps = 59.9, ms = 6.1, ms_max = 7.5,
                                                         slow = 0, lua_kb = 120, lua_peak_kb = 180, data_kb = 330,
                                                         instr_max = 12000, tokens = 321, tris = 40, gpu = false } })
tag = "after a try"
frames(3)
check(status():find("back from the game: 59.9 fps", 1, true), "back: the run's numbers: " .. status())
with("y", "left"); with("y", "left")
frames(30)
check(sees("LAST TRY") and sees("59.9 fps"), "the hub: the last try")
with("y", "down")
frames(2)
check(sees("59.9 fps") and sees("RAM 510k"), "the dev kit: the last try")
run_sdk({ path = "/carts/KEEP.BME", back = true, error = "main.lua:3: attempt to call a nil value (global 'boom')" })
tag = "error"
frames(2)
check(status():find("the game stopped", 1, true), "back from an error: " .. status())
frames(400)
check(status():find("boom", 1, true), "the error on the status rows: " .. status())

----------------------------------------------------------------- the frames' geometry

for _, f in ipairs(G.fails) do io.write("FAIL layout: " .. f .. "\n") end
if os.getenv("SDK_DEBUG") then
  local list = {}
  for s, c in pairs(CUTS) do list[#list + 1] = string.format("%2d %s", c, s) end
  table.sort(list)
  io.write("cut by layout.fit:\n  " .. table.concat(list, "\n  ") .. "\n")
  local m = {}
  for k in pairs(MISSING) do m[#m + 1] = k end
  table.sort(m)
  io.write("globals read but not stood in for: " .. table.concat(m, " ") .. "\n")
  local a = {}
  for k, x in pairs(ASSIST_OUT) do a[#a + 1] = k .. "@" .. x end
  io.write("the assistant's panel past 360 (cut by the screen): " .. table.concat(a, " ") .. "\n")
end
checks = checks + 1
if #G.fails > 0 then fails = fails + 1 end

io.write(string.format("sdk_b16_host: %d checks, %d failed\n", checks, fails))
os.exit(fails == 0 and 0 or 1)
