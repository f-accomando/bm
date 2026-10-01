-- The assistant's panel (M30): a library built into the kernel that any
-- development tool opens with a key of its own.
--
--   local assist = require "assist"
--   -- when the tool's key is pressed:
--   assist.open{ mode = "code", ctx = word_under_cursor,
--                on_insert = function(code) ... end }
--   function _update()
--     if assist.update() then return end   -- open: it has the keys
--     ...
--   end
--   function _draw()
--     ...                                   -- the tool
--     assist.draw()                         -- the panel on top, if open
--   end
--
-- Modes: "code" (API, how-to, errors), "sprite" (sprite recipes), "error"
-- (an error message: what it means, a typo), "any". It answers while you
-- type; Enter (A) hands the code to on_insert or the sprite to on_sprite
-- ({w, h, px = {0xRRGGBB or -1, ...}}), Esc (B) closes. Nothing runs while
-- it is closed.

local M = {}

local C_PANEL, C_BAR, C_LINE = 0x1C2030, 0x2A3048, 0x3A4060
local C_TEXT, C_DIM, C_ACC, C_ERR, C_SEL = 0xE0E4F0, 0x8088A0, 0xFFC050, 0xFF6060, 0x3050A0
local C_KW, C_API, C_STR, C_NUM, C_COM = 0xFF7AB0, 0x70D0FF, 0x90E070, 0xFFB060, 0x707C98
local KINDS = { code = "api,howto,error,tip", sprite = "sprite", error = "error,api,howto",
                any = "api,howto,error,tip,sprite" }
local TAG = { api = "API", howto = "how-to", error = "error", tip = "tip", sprite = "sprite" }

local st                                 -- nil while closed

local KEYWORDS = {}
for w in ("and break do else elseif end false for function goto if in local nil not or repeat return then true until while"):gmatch("%S+") do
  KEYWORDS[w] = true
end
local API                                -- names of the API, for the colours

-- ---------------------------------------------------------------- helpers

local function wrap(text, cols, out)
  for para in (text .. "\n"):gmatch("(.-)\n") do
    if para == "" then
      out[#out + 1] = { t = "" }
    else
      local line = ""
      for word in para:gmatch("%S+") do
        if #line + #word + 1 > cols and line ~= "" then
          out[#out + 1] = { t = line }
          line = word
        else
          line = line == "" and word or line .. " " .. word
        end
      end
      out[#out + 1] = { t = line }
    end
  end
end

local function safe(f, ...)
  local ok, a, b = pcall(f, ...)
  if ok then return a, b end
  st.msg = tostring(a)
  return nil
end

local function first_line(s)
  return (s:match("^[^\n]*") or s)
end

-- the hint for an error message: the line, and a name that looks mistyped
local function error_hint(msg)
  local line = msg:match(":(%d+):")
  local name = msg:match("global '([%w_]+)'") or msg:match("field '([%w_]+)'") or
               msg:match("method '([%w_]+)'")
  local hint
  if name then
    local near, d = safe(ai.near, name)
    if near and d > 0 then hint = "did you mean " .. near .. "?  (" .. name .. " does not exist)" end
  end
  if line then hint = "line " .. line .. (hint and ": " .. hint or "") end
  return hint
end

-- ---------------------------------------------------------------- state

local function layout()
  local W, H = SCREEN_W, SCREEN_H
  -- the font of the tool (font()): text on its cells, x a multiple of the
  -- width, y of the height
  st.fw, st.fh = font()
  local fw, fh = st.fw, st.fh
  st.x = st.x or (W >= 640 and 3 * fw or 0)
  st.y = st.y or (W >= 640 and fh or 0)
  st.w = st.w or (W - 2 * st.x)
  st.h = st.h or (H - 2 * st.y)
  st.cols = st.w // fw - 2
  st.rows = st.h // fh
  st.list_rows = math.max(2, math.min(6, (st.rows - 6) // 3))
  st.detail_rows = st.rows - 5 - st.list_rows
end

local function choose(i)
  local n = #st.hits
  if n == 0 then st.sel, st.entry, st.lines, st.sprite = 1, nil, {}, nil; return end
  st.sel = math.max(1, math.min(n, i))
  if st.sel < st.top then st.top = st.sel end
  if st.sel >= st.top + st.list_rows then st.top = st.sel - st.list_rows + 1 end
  local hit = st.hits[st.sel]
  st.entry = safe(ai.entry, hit.id)
  st.scroll, st.lines, st.sprite = 0, {}, nil
  local e = st.entry
  if not e then return end
  if e.kind == "sprite" then
    st.sprite = safe(ai.sprite, st.q ~= "" and st.q or e.gen,
                     { gen = e.gen, size = st.size, seed = st.seed, palette = st.palette })
    st.variants = {}
    for k = 1, 3 do
      st.variants[k] = safe(ai.sprite, st.q ~= "" and st.q or e.gen,
                            { gen = e.gen, size = st.size, seed = st.seed + k, palette = st.palette })
    end
    -- beside the picture: narrower
    local z = st.size >= 32 and 3 or st.size >= 16 and 5 or 8
    wrap(e.text or "", st.cols - (st.size * z + 2 * st.fw) // st.fw, st.lines)
    return
  end
  if e.text ~= "" then wrap(e.text, st.cols, st.lines) end
  if e.code ~= "" then
    st.lines[#st.lines + 1] = { t = "" }
    for l in (e.code .. "\n"):gmatch("(.-)\n") do st.lines[#st.lines + 1] = { t = l, code = true } end
  end
  if #e.see > 0 then
    st.lines[#st.lines + 1] = { t = "" }
    st.lines[#st.lines + 1] = { t = "see also: " .. table.concat(e.see, ", "), dim = true }
  end
end

local function refresh()
  local q = st.q:gsub("^%s+", "")
  st.msg = nil
  if q == "" and not st.ctx then
    -- nothing asked: everything, to browse (with the pad, too)
    st.hits = safe(ai.list, KINDS[st.mode]) or {}
    st.us = nil
  else
    st.hits, st.us = safe(ai.ask, q ~= "" and q or st.ctx, { n = 8, ctx = st.ctx, kinds = KINDS[st.mode] })
    st.hits = st.hits or {}
    -- then what the best answer points to ("see also"), if not there yet
    local best = st.hits[1] and safe(ai.entry, st.hits[1].id)
    if best then
      local seen = {}
      for _, h in ipairs(st.hits) do seen[h.id] = true end
      for _, id in ipairs(best.see) do
        local e = not seen[id] and safe(ai.entry, id)
        if e and KINDS[st.mode]:find(e.kind, 1, true) then
          st.hits[#st.hits + 1] = { id = e.id, title = e.title, kind = e.kind, score = 0, related = true }
          seen[id] = true
        end
      end
    end
    st.unsure = not st.hits[1] or st.hits[1].score < 0.3
  end
  st.top = 1
  choose(1)
end

-- ---------------------------------------------------------------- API

function M.is_open() return st ~= nil end

function M.open(o)
  o = o or {}
  st = {
    mode = KINDS[o.mode or "any"] and (o.mode or "any") or "any",
    q = o.query or "", ctx = o.ctx ~= "" and o.ctx or nil,
    on_insert = o.on_insert, on_sprite = o.on_sprite, on_close = o.on_close,
    palette = o.palette, size = o.size or 16, seed = 1,
    x = o.x, y = o.y, w = o.w, h = o.h,
    hits = {}, sel = 1, top = 1, scroll = 0, lines = {}, frame = 0,
    held = {}, rep = {},
  }
  if o.error then
    st.mode = "error"
    st.hint = error_hint(o.error)
    st.q = first_line(o.error):gsub("^[^:]*:%d+:%s*", "")
  end
  layout()
  if not API then
    API = {}
    for _, e in ipairs(safe(ai.list, "api") or {}) do API[e.id] = true end
  end
  refresh()
end

function M.close()
  local s = st
  st = nil
  if s and s.on_close then s.on_close() end
end

-- Enter / A: the code to the tool, or the sprite
local function act()
  local e = st.entry
  if not e then return end
  if e.kind == "sprite" then
    if st.sprite and st.on_sprite then
      local s, cb = st.sprite, st.on_sprite
      M.close()
      cb(s)
    else
      st.msg = "this tool takes no sprites"
    end
  elseif e.code ~= "" and st.on_insert then
    local code, cb = e.code, st.on_insert
    M.close()
    cb(code)
  elseif e.code ~= "" then
    st.msg = "this tool takes no code"
  end
end

local function variant(d)
  if st.entry and st.entry.kind == "sprite" then
    st.seed = math.max(1, st.seed + d)
    choose(st.sel)
  else
    st.scroll = math.max(0, math.min(math.max(0, #st.lines - st.detail_rows), st.scroll + d * st.detail_rows))
  end
end

-- one key from keyp(); true if the panel used it
function M.key(k)
  if not st then return false end
  if k == "esc" then M.close()
  elseif k == "\n" then act()
  elseif k == "up" then choose(st.sel - 1)
  elseif k == "down" then choose(st.sel + 1)
  elseif k == "left" then variant(-1)
  elseif k == "right" then variant(1)
  elseif k == "pgup" then st.scroll = math.max(0, st.scroll - st.detail_rows)
  elseif k == "pgdn" then st.scroll = math.max(0, math.min(#st.lines - st.detail_rows, st.scroll + st.detail_rows))
  elseif k == "\b" then
    if st.q ~= "" then st.q = st.q:sub(1, -2); refresh() end
  elseif k == "^u" then st.q = ""; st.ctx = nil; refresh()
  elseif k == "\t" then
    -- the next mode: code, sprite, any
    st.mode = st.mode == "code" and "sprite" or st.mode == "sprite" and "any" or "code"
    refresh()
  elseif #k == 1 and k:byte() >= 32 then
    if #st.q < st.cols - 4 then st.q = st.q .. k; refresh() end
  else
    return false
  end
  return true
end

-- a pad button: the first press, then again every few frames while held
local function pressed(b)
  if btnp(b) then st.rep[b] = 0; return true end
  if btn(b) then
    st.rep[b] = (st.rep[b] or 0) + 1
    return st.rep[b] > 18 and st.rep[b] % 4 == 0
  end
  st.rep[b] = nil
  return false
end

-- once per frame: the keys typed and the pad. true while open.
function M.update()
  if not st then return false end
  st.frame = st.frame + 1
  local k = keyp()
  while k and st do
    M.key(k)
    k = st and keyp()
  end
  if not st then return true end
  if pressed(2) then choose(st.sel - 1) end
  if pressed(3) then choose(st.sel + 1) end
  if pressed(0) then variant(-1) end
  if pressed(1) then variant(1) end
  if btnp(4) then act() end
  if st and btnp(5) then M.close() end
  if st and btnp(6) then M.key("\t") end
  return true
end

-- ---------------------------------------------------------------- drawing

local function code_line(s, x, y, maxc, fw)
  s = s:sub(1, maxc)
  local i, n = 1, #s
  while i <= n do
    local c = s:sub(i, i)
    local j, col
    if s:sub(i, i + 1) == "--" then j, col = n, C_COM
    elseif c == '"' or c == "'" then
      j = s:find(c, i + 1, true) or n
      col = C_STR
    elseif c:match("[%a_]") then
      j = s:find("[^%w_]", i) or n + 1
      j = j - 1
      local w = s:sub(i, j)
      col = KEYWORDS[w] and C_KW or API[w] and C_API or C_TEXT
    elseif c:match("%d") then
      j = (s:find("[^%wx%.]", i) or n + 1) - 1
      col = C_NUM
    else
      j = (s:find("[%w_\"'%-]", i + 1) or n + 1) - 1
      if j < i then j = i end
      col = C_TEXT
    end
    print(s:sub(i, j), x + (i - 1) * fw, y, col)
    i = j + 1
  end
end

local function draw_sprite(sp, x, y, z)
  if not sp then return end
  local w, px = sp.w, sp.px
  for j = 0, sp.h - 1 do
    local i = 0
    while i < w do
      local c = px[j * w + i + 1]
      local k = i + 1
      while k < w and px[j * w + k + 1] == c do k = k + 1 end
      if c >= 0 then rectfill(x + i * z, y + j * z, (k - i) * z, z, c) end
      i = k
    end
  end
end

local function checker(x, y, w, h, s)
  rectfill(x, y, w, h, 0x5A606C)
  for j = 0, h // s - 1 do
    for i = j % 2, w // s - 1, 2 do rectfill(x + i * s, y + j * s, s, s, 0x707884) end
  end
end

function M.draw()
  if not st then return end
  -- in the font the panel was laid out with, then the tool's again
  local tw, th = font()
  local fw, fh = st.fw, st.fh
  if fw ~= tw or fh ~= th then font(fw == 6 and "6x12" or fh == 14 and "8x14" or "8x16") end
  local x, y, w, h = st.x, st.y, st.w, st.h
  local tx = x + fw
  rectfill(x, y, w, h, C_PANEL)
  rect(x, y, w, h, C_LINE)
  -- title
  rectfill(x, y, w, fh, C_BAR)
  print("Assistant", tx, y, C_ACC)
  local right = st.mode
  if st.us then right = string.format("%s  %.1f ms", st.mode, st.us / 1000) end
  print(right, x + w - fw - #right * fw, y, C_DIM)
  -- question
  local qy = y + fh
  local cursor = (st.frame // 30) % 2 == 0 and "_" or " "
  print("?", tx, qy, C_ACC)
  print(st.q .. cursor, tx + 2 * fw, qy, C_TEXT)
  if st.q == "" and st.ctx then print("(" .. st.ctx .. ")", tx + 4 * fw, qy, C_DIM) end
  local ly = qy + fh
  if st.hint or st.msg then
    print((st.msg or st.hint):sub(1, st.cols), tx, ly, st.msg and C_ERR or C_ACC)
  elseif st.unsure and #st.hits > 0 and st.us then
    print("not sure what you mean: maybe one of these", tx, ly, C_DIM)
  elseif st.q == "" and not st.ctx then
    print(("type a question, or choose (" .. #st.hits .. " topics)"):sub(1, st.cols), tx, ly, C_DIM)
  end
  -- answers
  ly = ly + fh
  if #st.hits == 0 then
    print("no answer: try other words (sprite, map, sound, jump...)", tx, ly, C_DIM)
  end
  for r = 0, st.list_rows - 1 do
    local i = st.top + r
    local hit = st.hits[i]
    if not hit then break end
    local ry = ly + r * fh
    if i == st.sel then rectfill(x + 4, ry, w - 8, fh, C_SEL) end
    local tag = (hit.related and "see " or "") .. (TAG[hit.kind] or hit.kind)
    print(hit.title:sub(1, st.cols - 12), tx, ry, i == st.sel and 0xFFFFFF or hit.related and C_DIM or C_TEXT)
    print(tag, x + w - fw - #tag * fw, ry, C_DIM)
  end
  if #st.hits > st.list_rows then
    local bh = st.list_rows * fh
    local thumb = math.max(4, bh * st.list_rows // #st.hits)
    local ty = ly + (bh - thumb) * (st.top - 1) // math.max(1, #st.hits - st.list_rows)
    rectfill(x + w - 3, ty, 2, thumb, C_LINE)
  end
  -- details
  local dy = ly + st.list_rows * fh
  line(x + 4, dy - 1, x + w - 5, dy - 1, C_LINE)
  local e = st.entry
  if e and e.kind == "sprite" then
    local z = st.size >= 32 and 3 or st.size >= 16 and 5 or 8
    local box = st.size * z
    local avail = st.detail_rows * fh - fh
    while box > avail and z > 1 do z = z - 1; box = st.size * z end
    checker(tx, dy + 4, box, box, z * 2)
    draw_sprite(st.sprite, tx, dy + 4, z)
    local vx = tx + box + 2 * fw
    local vz = math.max(1, z // 2)
    for k = 1, 3 do
      local v = st.variants[k]
      if v then
        checker(vx, dy + 4, v.w * vz, v.h * vz, vz * 2)
        draw_sprite(v, vx, dy + 4, vz)
        vx = vx + v.w * vz + 8
      end
    end
    local iy = dy + (st.size * vz + 4 + fh - 1) // fh * fh
    print((st.sprite and st.sprite.name or e.title) .. "  #" .. st.seed, tx + box + 2 * fw, iy, C_TEXT)
    for k = 1, math.min(#st.lines, (dy + st.detail_rows * fh - iy) // fh - 1) do
      print(st.lines[k].t, tx + box + 2 * fw, iy + k * fh, C_DIM)
    end
  else
    for r = 1, st.detail_rows do
      local l = st.lines[st.scroll + r]
      if not l then break end
      local ry = dy + (r - 1) * fh
      if l.code then code_line(l.t, tx, ry, st.cols, fw)
      else print(l.t, tx, ry, l.dim and C_DIM or C_TEXT) end
    end
    if #st.lines > st.detail_rows then
      local more = st.scroll + st.detail_rows < #st.lines and "PgDn: more" or "PgUp: back"
      print(more, x + w - fw - #more * fw, dy + (st.detail_rows - 1) * fh, C_DIM)
    end
  end
  -- keys
  local fy = y + h - fh
  rectfill(x, fy, w, fh, C_BAR)
  local keys
  if e and e.kind == "sprite" then
    keys = "Enter/A: use  </>: variant  Up/Dn: choose  Tab/X: mode  Esc/B: close"
  else
    keys = "Enter/A: insert  Up/Dn: choose  PgDn: more  Tab/X: mode  Esc/B: close"
  end
  print(keys:sub(1, st.cols), tx, fy, C_DIM)
  if fw ~= tw or fh ~= th then font(tw == 6 and "6x12" or th == 14 and "8x14" or "8x16") end
end

return M
