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
-- it is closed. With the pad, Share writes the question with chords
-- (require "padtype"), with the words of the questions to the assistant.

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

-- the writing with the pad (src/ai/padtype.lua), for the question: the
-- words of Italian and of the questions of the knowledge base
local padtype
local function pad_lib()
  if padtype == nil then
    local ok, m = pcall(require, "padtype")
    padtype = ok and m or false
  end
  return padtype or nil
end

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
  st.w = st.w or (W - 2 * st.x) // fw * fw
  st.h = st.h or (H - 2 * st.y) // fh * fh
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

-- the question, written with the pad: Start (Enter) hands over the answer,
-- Start + up / down chooses it
local act, variant                       -- (below)
local ask_host = { lang = { it = 1, ask = 2 }, name = "ask", prose = true }
function ask_host.before() return st and st.q or "" end
function ask_host.insert(s)
  if not st then return end
  st.q = (st.q .. s):sub(1, st.cols - 4)
  refresh()
end
function ask_host.erase(n)
  if not st then return end
  st.q = st.q:sub(1, #st.q - n)
  refresh()
end
function ask_host.newline() act() end
function ask_host.move(d)
  if d == "up" then choose(st.sel - 1) elseif d == "down" then choose(st.sel + 1)
  else variant(d == "left" and -1 or 1) end
end
function ask_host.undo() end

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
  local pt = pad_lib()
  if pt and pt.is_on() then               -- the chords go on, in the question
    pt.wait()
    pt.refresh(ask_host)
  end
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
act = function()
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

variant = function(d)
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
  local pt = pad_lib()
  if pt and pt.is_on() then
    pt.update(ask_host)
    return true
  end
  if pt and btnp(9) then                 -- Share: the chords write the question
    pt.on(true)
    pt.refresh(ask_host)
    return true
  end
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
  local pt = pad_lib()
  local ghost = pt and pt.is_on() and pt.ghost()
  if ghost then print(ghost, tx + (2 + #st.q) * fw, qy, pt.C_GHOST) end
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
  if pt and pt.is_on() then
    keys = "Share: chords off  Start: insert  Start+Up/Dn: choose"
    local pw, ph = pt.size()
    pt.draw(x + w - pw, fy - ph)
  end
  print(keys:sub(1, st.cols), tx, fy, C_DIM)
  if fw ~= tw or fh ~= th then font(tw == 6 and "6x12" or th == 14 and "8x14" or "8x16") end
end

-- ---------------------------------------------------------------- code actions
--
-- assist.act(request, lines, at): what a line "#entry: request #" at line
-- `at` asks for. The network chooses between actions on the code (kind
-- "action": ternary, comment, log, indent, rename...), examples to insert
-- (howto, api) and sprites (drawn as code). Returns
--   { lines = the new lines (the #entry line gone), cursor =, message =,
--     ok = true/false, explain = { lines of text }? }
-- or nil if the request means nothing it knows.

-- the code of a line without strings and comments; st.long: inside a
-- --[[ ]] comment or a [[ ]] string that goes on
local function strip(l, st)
  local out, i, n = {}, 1, #l
  while i <= n do
    if st.long then
      local e = l:find("]]", i, true)
      if not e then break end
      st.long, i = false, e + 2
    else
      local c = l:sub(i, i)
      if l:sub(i, i + 3) == "--[[" then st.long, i = true, i + 4
      elseif l:sub(i, i + 1) == "--" then break
      elseif l:sub(i, i + 1) == "[[" then st.long, i = true, i + 2; out[#out + 1] = '""'
      elseif c == '"' or c == "'" then
        local j = i + 1
        while j <= n and l:sub(j, j) ~= c do j = j + (l:sub(j, j) == "\\" and 2 or 1) end
        out[#out + 1] = '""'
        i = j + 1
      else
        out[#out + 1] = c
        i = i + 1
      end
    end
  end
  return table.concat(out)
end

local OPENW = { ["function"] = true, ["do"] = true, ["if"] = true, ["repeat"] = true }
local CLOSEW = { ["end"] = true, ["until"] = true }

-- the blocks of the code: the functions {start, stop}, and the depth at the
-- start of each line (functions, if, do, repeat, tables and parentheses)
local function analyze(lines)
  local st, stack, funcs, depth = {}, {}, {}, {}
  for i, l in ipairs(lines) do
    depth[i] = #stack
    local code = strip(l, st)
    local p = 1
    while true do
      local a, b, tok = code:find("([%a_][%w_]*)", p)
      local ca = code:find("[{}%(%)]", p)
      if ca and (not a or ca < a) then
        local ch = code:sub(ca, ca)
        if ch == "{" or ch == "(" then stack[#stack + 1] = { kind = ch, line = i }
        elseif #stack > 0 then table.remove(stack) end
        p = ca + 1
      elseif a then
        if OPENW[tok] then stack[#stack + 1] = { kind = tok, line = i }
        elseif CLOSEW[tok] and #stack > 0 then
          local o = table.remove(stack)
          if o.kind == "function" then funcs[#funcs + 1] = { start = o.line, stop = i } end
        end
        p = b + 1
      else
        break
      end
    end
  end
  return funcs, depth
end

-- the function around line `at` (the innermost), else the first one after
local function target_function(lines, at)
  local funcs = analyze(lines)
  local best
  for _, f in ipairs(funcs) do
    if f.start <= at and at <= f.stop and (not best or f.start > best.start) then best = f end
  end
  if best then return best end
  for _, f in ipairs(funcs) do
    if f.start >= at and (not best or f.start < best.start) then best = f end
  end
  return best
end

local function header(l)
  local name, params = l:match("function%s+([%w_.:]+)%s*%((.-)%)")
  if not name then
    name, params = l:match("([%w_.]+)%s*=%s*function%s*%((.-)%)")
  end
  local ps = {}
  for p in (params or ""):gmatch("[%w_%.]+") do ps[#ps + 1] = p end
  return name or "function", ps
end

local function ind(l) return l:match("^ *") end

local function copy(t) return table.move(t, 1, #t, 1, {}) end

local function api_used(lines, a, b)
  if not API then return {} end
  local seen, out = {}, {}
  local st = {}
  for i = a, b do
    local code = strip(lines[i], st)
    for w in code:gmatch("([%a_][%w_]*)%s*%(") do
      if API[w] and not seen[w] then seen[w] = true; out[#out + 1] = w end
    end
  end
  return out
end

local function short_title(id)
  local e = ai.entry(id)
  local t = e and e.title or ""
  return t:match("%- (.*)$") or t
end

local A = {}            -- the actions, by their gen

A.ternary = function(lines, f)
  local n, skipped = 0, 0
  local i = f.start
  while i <= f.stop do
    local l = lines[i]
    local sp, cond, x, a, b = l:match("^(%s*)if%s+(.-)%s+then%s+([%w_.%[%]]+)%s*=%s*(.-)%s+else%s+%3%s*=%s*(.-)%s+end%s*$")
    local ret
    if not sp then
      sp, cond, a, b = l:match("^(%s*)if%s+(.-)%s+then%s+return%s+(.-)%s+else%s+return%s+(.-)%s+end%s*$")
      ret = sp ~= nil
    end
    local span = 1
    if not sp and i + 4 <= f.stop then
      local s1, c1 = l:match("^(%s*)if%s+(.-)%s+then%s*$")
      if s1 and lines[i + 2]:match("^%s*else%s*$") and lines[i + 4]:match("^%s*end%s*$") then
        local x1, a1 = lines[i + 1]:match("^%s*([%w_.%[%]]+)%s*=%s*(.-)%s*$")
        local x2, b1 = lines[i + 3]:match("^%s*([%w_.%[%]]+)%s*=%s*(.-)%s*$")
        local r1 = lines[i + 1]:match("^%s*return%s+(.-)%s*$")
        local r2 = lines[i + 3]:match("^%s*return%s+(.-)%s*$")
        if x1 and x1 == x2 and x1 ~= "local" then
          sp, cond, x, a, b, span = s1, c1, x1, a1, b1, 5
        elseif r1 and r2 then
          sp, cond, a, b, ret, span = s1, c1, r1, r2, true, 5
        end
      end
    end
    if sp and (a == "nil" or a == "false" or a:find("%-%-") or b:find("%-%-")) then
      skipped, sp = skipped + 1, nil      -- "c and nil or b" is never nil: not the same
    end
    if sp then
      local new = sp .. (ret and "return " or x .. " = ") .. cond .. " and " .. a .. " or " .. b
      for _ = 1, span do table.remove(lines, i) end
      table.insert(lines, i, new)
      f.stop = f.stop - span + 1
      n = n + 1
    end
    i = i + 1
  end
  if n == 0 then
    return false, skipped > 0 and "if/else with nil or false: cannot become \"a and b or c\""
                              or "no if/else with one assignment or return in this function"
  end
  return true, n .. " if/else -> \"cond and a or b\" (a must never be nil or false)"
end

A.comment = function(lines, f)
  local name, ps = header(lines[f.start])
  local sp = ind(lines[f.start])
  local out = { sp .. "-- " .. name .. "(" .. table.concat(ps, ", ") .. ")" }
  local used = api_used(lines, f.start + 1, f.stop)
  if #used > 0 then
    local line = sp .. "--   uses:"
    for k, w in ipairs(used) do
      local piece = " " .. w .. " (" .. short_title(w) .. ")" .. (k < #used and "," or "")
      if #line + #piece > 72 then out[#out + 1] = line; line = sp .. "--  " end
      line = line .. piece
    end
    out[#out + 1] = line
  end
  local st = {}
  for i = f.start + 1, f.stop do
    local r = strip(lines[i], st):match("^%s*return%s+(.+)$")
    if r then out[#out + 1] = sp .. "--   returns " .. r; break end
  end
  for k = #out, 1, -1 do table.insert(lines, f.start, out[k]) end
  return true, "comment added above " .. name
end

A.log = function(lines, f)
  local name, ps = header(lines[f.start])
  local args = { '"' .. name .. '"' }
  for _, p in ipairs(ps) do
    if p ~= "..." then args[#args + 1] = '"' .. p .. '=", ' .. p end
  end
  table.insert(lines, f.start + 1, ind(lines[f.start]) .. "  log(" .. table.concat(args, ", ") .. ")")
  return true, "log() at the start of " .. name .. " (Dev > Log shows it)"
end

A.remove_log = function(lines, f)
  local n = 0
  for i = f.stop, f.start, -1 do
    if lines[i]:match("^%s*log%(.*%)%s*$") then table.remove(lines, i); n = n + 1 end
  end
  if n == 0 then return false, "no log() line in this function" end
  return true, n .. " log() lines removed"
end

A.nil_check = function(lines, f)
  local name, ps = header(lines[f.start])
  local checks = {}
  for _, p in ipairs(ps) do
    if p ~= "..." and p ~= "self" then checks[#checks + 1] = p .. " == nil" end
  end
  if #checks == 0 then return false, name .. " has no parameters to check" end
  table.insert(lines, f.start + 1, ind(lines[f.start]) .. "  if " .. table.concat(checks, " or ") ..
               " then return end")
  return true, name .. ": returns at once if a parameter is nil"
end

A.make_local = function(lines, f)
  local l = lines[f.start]
  if l:match("^%s*local%s+function") then return false, "already local" end
  local sp, rest = l:match("^(%s*)function%s+([%w_]+%s*%(.*)$")
  if not sp then return false, "only \"function name(...)\" can become local" end
  lines[f.start] = sp .. "local function " .. rest
  return true, "local: faster, visible only below it in this file"
end

A.indent = function(lines, f, whole)
  local a, b = whole and 1 or f.start, whole and #lines or f.stop
  local _, depth = analyze(lines)
  local base = whole and 0 or #ind(lines[a]) // 2 - depth[a]
  local st = {}
  local n = 0
  for i = a, b do
    local code = strip(lines[i], st)
    local text = lines[i]:gsub("^%s+", "")
    if text == "" then
      lines[i] = ""
    else
      local d = depth[i] + base
      local first = code:match("^%s*([%a_]+)") or code:match("^%s*([%]%)}])")
      if first and (CLOSEW[first] or first == "else" or first == "elseif" or first == "}" or first == ")") then
        d = d - 1
      end
      local new = string.rep("  ", math.max(0, d)) .. text
      if new ~= lines[i] then n = n + 1 end
      lines[i] = new
    end
  end
  return true, n == 0 and "already indented" or n .. " lines indented"
end

A.comment_out = function(lines, f)
  for i = f.start, f.stop do lines[i] = ind(lines[i]) .. "-- " .. lines[i]:gsub("^%s+", "") end
  return true, "commented out (\"#entry: uncomment #\" brings it back)"
end

A.uncomment = function(lines, f, whole, at)
  -- the commented lines around `at`
  local a, b = at, at - 1
  while a > 1 and lines[a - 1]:match("^%s*%-%- ?") do a = a - 1 end
  while b < #lines and lines[b + 1]:match("^%s*%-%- ?") do b = b + 1 end
  if b < a then return false, "no commented lines here" end
  for i = a, b do lines[i] = lines[i]:gsub("^(%s*)%-%- ?", "%1", 1) end
  return true, (b - a + 1) .. " lines uncommented"
end

A.optimize = function(lines, f)
  local used = api_used(lines, f.start + 1, f.stop)
  if #used == 0 then return false, "no API function used in this function" end
  while #used > 8 do table.remove(used) end
  local list = table.concat(used, ", ")
  table.insert(lines, f.start, ind(lines[f.start]) .. "local " .. list .. " = " .. list)
  return true, "API functions as locals: faster in hot loops"
end

A.rename = function(lines, f, whole, at, req)
  local a, b = req:match("[Rr]inomina%s+([%a_][%w_]*)%s+in%s+([%a_][%w_]*)")
  if not a then a, b = req:match("[Rr]ename%s+([%a_][%w_]*)%s+to%s+([%a_][%w_]*)") end
  if not a then a, b = req:match("([%a_][%w_]*)%s+in%s+([%a_][%w_]*)%s*$") end
  if not a then a, b = req:match("([%a_][%w_]*)%s+to%s+([%a_][%w_]*)%s*$") end
  if not a then a, b = req:match("([%a_][%w_]*)%s*%->%s*([%a_][%w_]*)") end
  if not a then return false, "write: rinomina vecchio in nuovo" end
  whole = whole or req:find("ovunque") or req:find("everywhere") or req:find("file")
  local i0, i1 = whole and 1 or f.start, whole and #lines or f.stop
  local n = 0
  for i = i0, i1 do
    local l, k = lines[i]:gsub("%f[%w_]" .. a .. "%f[^%w_]", b)
    lines[i], n = l, n + k
  end
  if n == 0 then return false, a .. " is not used " .. (whole and "in the file" or "in this function") end
  return true, n .. " x " .. a .. " -> " .. b
end

A.explain = function(lines, f)
  local name, ps = header(lines[f.start])
  local out = { name .. "(" .. table.concat(ps, ", ") .. "): lines " .. f.start .. "-" .. f.stop }
  local used = api_used(lines, f.start + 1, f.stop)
  if #used > 0 then
    out[#out + 1] = "uses:"
    for _, w in ipairs(used) do out[#out + 1] = "  " .. w .. ": " .. short_title(w) end
  end
  -- the globals it changes, the functions of this file it calls
  local locals, globals, seen = {}, {}, {}
  for _, p in ipairs(ps) do locals[p] = true end
  local defined = {}
  for _, g in ipairs(analyze(lines)) do
    local n = header(lines[g.start])
    defined[n] = true
  end
  local calls, cseen = {}, {}
  local st = {}
  for i = f.start + 1, f.stop do
    local code = strip(lines[i], st)
    for v in code:gmatch("local%s+([%w_,%s]+)") do
      for w in v:gmatch("[%w_]+") do locals[w] = true end
    end
    local x = code:match("^%s*([%a_][%w_]*)%s*=[^=]")
    if x and not locals[x] and not seen[x] then seen[x] = true; globals[#globals + 1] = x end
    for w in code:gmatch("([%a_][%w_.]*)%s*%(") do
      if defined[w] and w ~= name and not cseen[w] then cseen[w] = true; calls[#calls + 1] = w end
    end
  end
  if #globals > 0 then out[#out + 1] = "changes (global): " .. table.concat(globals, ", ") end
  if #calls > 0 then out[#out + 1] = "calls: " .. table.concat(calls, ", ") end
  st = {}
  for i = f.start + 1, f.stop do
    local r = strip(lines[i], st):match("^%s*return%s+(.+)$")
    if r then out[#out + 1] = "returns " .. r; break end
  end
  return true, "about " .. name, out
end

-- a sprite as code: a palette of letters, the rows, sset into the sheet
local function sprite_code(sp)
  local letters, pal, n = {}, {}, 0
  local abc = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
  local rows = {}
  for y = 0, sp.h - 1 do
    local row = {}
    for x = 0, sp.w - 1 do
      local c = sp.px[y * sp.w + x + 1]
      if c < 0 then row[#row + 1] = "."
      else
        if not letters[c] then
          n = n + 1
          letters[c] = abc:sub(n, n)
          pal[#pal + 1] = letters[c] .. " = " .. string.format("0x%06X", c)
        end
        row[#row + 1] = letters[c]
      end
    end
    rows[#rows + 1] = '  "' .. table.concat(row) .. '",'
  end
  local out = { "-- " .. sp.name .. " " .. sp.w .. "x" .. sp.h .. " (assistant): into the sheet at 0, 0",
                "local pal = {" }
  local line = " "
  for k, p in ipairs(pal) do
    if #line + #p + 2 > 70 then out[#out + 1] = line; line = " " end
    line = line .. " " .. p .. (k < #pal and "," or "")
  end
  out[#out + 1] = line
  out[#out + 1] = "}"
  out[#out + 1] = "local art = {"
  for _, r in ipairs(rows) do out[#out + 1] = r end
  out[#out + 1] = "}"
  out[#out + 1] = "for y, row in ipairs(art) do"
  out[#out + 1] = "  for x = 1, #row do"
  out[#out + 1] = "    local c = pal[row:sub(x, x)]"
  out[#out + 1] = "    if c then sset(x - 1, y - 1, c) else sset(x - 1, y - 1) end"
  out[#out + 1] = "  end"
  out[#out + 1] = "end"
  return table.concat(out, "\n")
end

function M.act(request, lines, at)
  if not API then
    API = {}
    for _, e in ipairs(ai.list("api")) do API[e.id] = true end
  end
  local hits = ai.ask(request, { n = 3, kinds = "action,howto,api,sprite" })
  local top = hits[1]
  -- "rinomina a in b" / "rename a to b": the words say it all
  local r = request:lower()
  if (r:find("rinomina") or r:find("rename")) and (r:find("%s+in%s+[%a_]") or r:find("%s+to%s+[%a_]")) then
    top = { id = "act.rename", score = 1 }
  end
  if not top then return nil end
  local e = ai.entry(top.id)
  -- it changes the code: only when it is quite sure
  if top.score < 0.3 then
    return { lines = lines, ok = false, cursor = at,
             message = "not sure: \"" .. e.title .. "\"? say it with other words" }
  end
  local out = copy(lines)
  local sp = ind(out[at])
  table.remove(out, at)                   -- the #entry line goes
  if e.kind == "action" then
    local fn = A[e.gen]
    if not fn then return { lines = lines, ok = false, message = e.title .. ": not done yet" } end
    local whole = request:find("tutto il file") or request:find("whole file") or request:find("ovunque")
    local f = target_function(out, math.min(at, #out))
    if not f then
      if e.gen ~= "indent" and e.gen ~= "rename" and e.gen ~= "uncomment" then
        return { lines = lines, ok = false, message = "no function here or below for: " .. e.title }
      end
      f, whole = { start = 1, stop = #out }, true
    end
    local ok, msg, explain = fn(out, f, whole, math.min(at, #out), request)
    if not ok then return { lines = lines, ok = false, message = msg, cursor = at } end
    return { lines = out, ok = true, message = msg, explain = explain, cursor = math.min(at, #out),
             entry = e.id }
  end
  local code
  if e.kind == "sprite" then
    local s = ai.sprite(request, { gen = e.gen })
    code = s and sprite_code(s)
  else
    code = e.code
  end
  if not code or code == "" then
    return { lines = lines, ok = false, message = e.title .. ": no code to insert" }
  end
  local n = 0
  for l in (code .. "\n"):gmatch("(.-)\n") do
    table.insert(out, at + n, l == "" and "" or sp .. l)
    n = n + 1
  end
  return { lines = out, ok = true, message = "inserted: " .. e.title, cursor = at, entry = e.id }
end

return M
