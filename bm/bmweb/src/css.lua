-- bmweb: a small CSS. Pure Lua.
-- Selectors: tag, .class, #id, "*", [attr], [attr=v ~= ^= $= *=], :first-child, :last-child,
-- :nth-child(n|odd|even|an+b), :only-child, :empty, :root, :not(simple), and the combinators
-- " ", ">", "+", "~". A rule with another pseudo-class or a pseudo-element (:hover, ::before)
-- is dropped. In streaming mode the siblings are gone: "+" and "~" see the one sibling
-- before, ":last-child" and ":empty" see only the kept tree.
-- Properties: color, background(-color), display, margin*, padding*, font-weight, font-style,
-- text-align, text-decoration, visibility, text-transform, white-space, border* (a frame),
-- width, max-width, and custom properties var(--x, fallback), calc() of px/em/%.
-- Units: px, em, rem, pt, vw, %. @media (min/max-width) is evaluated for the screen
-- width; other at-rules are skipped.
local M = {}

M.LIMITS = {
  max_css = 65536,  -- bytes of CSS per page; the rest is ignored
  max_rules = 4000,
}
M.EM = 12

local NAMED_COLORS = {
  black = 0x000000, white = 0xFFFFFF, red = 0xFF0000, green = 0x008000, blue = 0x0000FF,
  yellow = 0xFFFF00, cyan = 0x00FFFF, aqua = 0x00FFFF, magenta = 0xFF00FF, fuchsia = 0xFF00FF,
  gray = 0x808080, grey = 0x808080, silver = 0xC0C0C0, maroon = 0x800000, olive = 0x808000,
  lime = 0x00FF00, teal = 0x008080, navy = 0x000080, purple = 0x800080, orange = 0xFFA500,
  brown = 0xA52A2A, pink = 0xFFC0CB, gold = 0xFFD700, indigo = 0x4B0082, violet = 0xEE82EE,
  coral = 0xFF7F50, salmon = 0xFA8072, khaki = 0xF0E68C, crimson = 0xDC143C, tomato = 0xFF6347,
  turquoise = 0x40E0D0, beige = 0xF5F5DC, ivory = 0xFFFFF0, lavender = 0xE6E6FA, tan = 0xD2B48C,
  darkgray = 0xA9A9A9, darkgrey = 0xA9A9A9, lightgray = 0xD3D3D3, lightgrey = 0xD3D3D3,
  dimgray = 0x696969, dimgrey = 0x696969, whitesmoke = 0xF5F5F5, gainsboro = 0xDCDCDC,
  darkblue = 0x00008B, darkgreen = 0x006400, darkred = 0x8B0000, lightblue = 0xADD8E6,
  lightgreen = 0x90EE90, steelblue = 0x4682B4, royalblue = 0x4169E1, skyblue = 0x87CEEB,
  orangered = 0xFF4500, forestgreen = 0x228B22, slategray = 0x708090, slategrey = 0x708090,
  firebrick = 0xB22222, chocolate = 0xD2691E, darkorange = 0xFF8C00, deeppink = 0xFF1493,
  midnightblue = 0x191970, seagreen = 0x2E8B57, aliceblue = 0xF0F8FF, mintcream = 0xF5FFFA,
  snow = 0xFFFAFA, linen = 0xFAF0E6, wheat = 0xF5DEB3, plum = 0xDDA0DD, orchid = 0xDA70D6,
}

local function hsl_rgb(h, s, l)
  h = (h % 360) / 360
  local function f(n)
    local k = (n + h * 12) % 12
    local a = s * math.min(l, 1 - l)
    return l - a * math.max(-1, math.min(k - 3, 9 - k, 1))
  end
  return (math.floor(f(0) * 255 + 0.5) << 16) | (math.floor(f(8) * 255 + 0.5) << 8) | math.floor(f(4) * 255 + 0.5)
end

-- Returns an integer 0xRRGGBB, false for "transparent", or nil if unknown.
local function parse_color(v)
  v = v:lower()
  if v == "transparent" then return false end
  if NAMED_COLORS[v] then return NAMED_COLORS[v] end
  local h = v:match("^#(%x+)$")
  if h then
    if #h == 3 or #h == 4 then
      local a = #h == 4 and h:sub(4, 4) or nil
      if a == "0" then return false end
      return tonumber((h:sub(1, 3):gsub(".", "%0%0")), 16)
    end
    if #h == 6 then return tonumber(h, 16) end
    if #h == 8 then
      if h:sub(7) == "00" then return false end
      return tonumber(h:sub(1, 6), 16)
    end
    return nil
  end
  local r, g, b = v:match("^rgba?%(%s*(%d+)[%s,]+(%d+)[%s,]+(%d+)")
  if r then
    local a = v:match("[,/]%s*([%d.]+%%?)%s*%)$")
    if a and (tonumber((a:gsub("%%", ""))) or 1) == 0 then return false end
    return (math.min(255, tonumber(r)) << 16) | (math.min(255, tonumber(g)) << 8) | math.min(255, tonumber(b))
  end
  local hh, ss, ll = v:match("^hsla?%(%s*([%d.]+)[%a]*[%s,]+([%d.]+)%%[%s,]+([%d.]+)%%")
  if hh then
    return hsl_rgb(tonumber(hh), math.min(1, tonumber(ss) / 100), math.min(1, tonumber(ll) / 100))
  end
  return nil
end
M.parse_color = parse_color

-- Screen width for vw and @media (set by the page driver).
M.SCREEN_W = 640
local UNIT = { px = 1, em = M.EM, rem = M.EM, pt = 4 / 3, pc = 16, cm = 37.8, mm = 3.78, ["in"] = 96, ex = 6, ch = 6 }

-- calc(): + - * / over numbers with px-like units. A "%" only works alone. Returns px or nil.
local function calc_eval(expr)
  expr = expr:gsub("%s+", "")
  local i = 1
  local sum, term, atom
  function atom()
    local c = expr:sub(i, i)
    if c == "(" then
      i = i + 1
      local v = sum()
      if expr:sub(i, i) ~= ")" then return nil end
      i = i + 1
      return v
    end
    local n, u = expr:match("^(%-?[%d.]+)(%a*)", i)
    if not n then return nil end
    i = i + #n + #u
    n = tonumber(n)
    if not n then return nil end
    if u == "" then return n end
    if u == "vw" then return n * M.SCREEN_W / 100 end
    local f = UNIT[u]
    return f and n * f or nil
  end
  function term()
    local v = atom()
    while v do
      local c = expr:sub(i, i)
      if c == "*" then i = i + 1; local w = atom(); v = w and v * w or nil
      elseif c == "/" then i = i + 1; local w = atom(); v = (w and w ~= 0) and v / w or nil
      else break end
    end
    return v
  end
  function sum()
    local v = term()
    while v do
      local c = expr:sub(i, i)
      if c == "+" then i = i + 1; local w = term(); v = w and v + w or nil
      elseif c == "-" then i = i + 1; local w = term(); v = w and v - w or nil
      else break end
    end
    return v
  end
  local v = sum()
  if v and i == #expr + 1 then return v end
  return nil
end

-- A length: a number of px, a string "N%" or nil if not supported.
local function parse_len(v)
  v = v:lower()
  if v == "0" or v == "auto" then return 0 end
  local inner = v:match("^calc%((.*)%)$")
  if inner then
    if inner:find("%", 1, true) then
      local pct = inner:match("^%s*(%-?[%d.]+)%%%s*$")
      return pct and (tonumber(pct) .. "%") or nil
    end
    local r = calc_eval(inner)
    if not r then return nil end
    if r < 0 then r = 0 end
    return math.floor(r + 0.5)
  end
  local n, u = v:match("^(%-?[%d.]+)(%a*%%?)$")
  n = tonumber(n)
  if not n then return nil end
  if n < 0 then n = 0 end
  if u == "%" then return n .. "%" end
  if u == "vw" then return math.floor(n * M.SCREEN_W / 100 + 0.5) end
  if u == "" then return nil end
  local f = UNIT[u]
  if f then return math.floor(n * f + 0.5) end
  return nil
end

-- A stored length is a number or "N%". Resolve it against a base width.
function M.px(v, base)
  if type(v) == "string" then return math.floor(tonumber(v:sub(1, -2)) * base / 100) end
  return v or 0
end

local SIDES = { "t", "r", "b", "l" }

local function box_values(prefix, value, out)
  local vals = {}
  value = value:gsub("%b()", function(m) return (m:gsub("%s+", "\1")) end)
  for tok in value:gmatch("%S+") do
    tok = tok:gsub("\1", " ")
    local l = parse_len(tok)
    if l == nil then return end
    vals[#vals + 1] = l
  end
  local n = #vals
  if n == 0 or n > 4 then return end
  local t, r, b, l = vals[1], vals[2] or vals[1], vals[3] or vals[1], vals[4] or vals[2] or vals[1]
  out[prefix .. "t"], out[prefix .. "r"], out[prefix .. "b"], out[prefix .. "l"] = t, r, b, l
end

local DISPLAY = {
  none = "none", block = "block", flex = "block", grid = "block", ["list-item"] = "block",
  table = "block", inline = "inline", ["inline-block"] = "inline", ["inline-flex"] = "inline",
  ["inline-grid"] = "inline",
}

local TEXT_TRANSFORM = { uppercase = "upper", lowercase = "lower", capitalize = "cap", none = false }
local WHITE_SPACE = { pre = "pre", ["pre-wrap"] = "pre", ["pre-line"] = "pre", normal = false, nowrap = false }

-- Split a declaration list on ";" outside of () and quotes.
local function split_decls(text)
  local out, depth, q, start = {}, 0, nil, 1
  for i = 1, #text do
    local c = text:sub(i, i)
    if q then
      if c == q then q = nil end
    elseif c == '"' or c == "'" then q = c
    elseif c == "(" then depth = depth + 1
    elseif c == ")" then depth = depth > 0 and depth - 1 or 0
    elseif c == ";" and depth == 0 then
      out[#out + 1] = text:sub(start, i - 1)
      start = i + 1
    end
  end
  out[#out + 1] = text:sub(start)
  return out
end

-- Border shorthand: "1px solid #ccc". Returns the colour (or false for none) and the width.
local function parse_border(value)
  local lv = value:lower()
  if lv:find("^none") or lv:find("^hidden") or lv == "0" then return false end
  local color, width = nil, 1
  for tok in value:gmatch("%S+") do
    local c = parse_color(tok)
    if c ~= nil and not tok:lower():match("^%d") then color = c
    else
      local l = parse_len(tok)
      if type(l) == "number" then width = l end
    end
  end
  if width < 1 then return false end
  return color or -1, width
end

local function set_border(out, sides, value)
  local color, width = parse_border(value)
  for sd in sides:gmatch(".") do
    out["b" .. sd] = color and (width >= 1 and true or false) or false
  end
  if color then out.bdc = color end
end

local function set_one(out, prop, value)
  local lv = value:lower()
  if prop == "color" then
    local c = parse_color(value); if c then out.color = c end
  elseif prop == "background-color" or prop == "background" then
    if lv:find("gradient(", 1, true) or lv:find("url(", 1, true) then
      -- a picture or a gradient: take the first colour in it, if any
      local c = lv:match("#%x%x%x%x%x%x") or lv:match("#%x%x%x%f[%X]")
      if c then out.bg = parse_color(c) end
    else
      for tok in value:gmatch("%S+") do
        local c = parse_color(tok)
        if c ~= nil then out.bg = c; break end
      end
      if lv == "none" then out.bg = false end
    end
  elseif prop == "display" then
    local d = DISPLAY[lv]; if d then out.display = d end
  elseif prop == "margin" then box_values("m", value, out)
  elseif prop == "padding" then box_values("p", value, out)
  elseif prop:match("^margin%-%a+$") or prop:match("^padding%-%a+$") then
    local kind, side = prop:match("^(%a)%a*%-(%a+)$")
    local key = ({ top = "t", right = "r", bottom = "b", left = "l" })[side]
    local l = parse_len(value)
    if key and l ~= nil then out[kind .. key] = l end
  elseif prop == "font-weight" then
    local w = tonumber(lv)
    if lv == "bold" or lv == "bolder" or (w and w >= 600) then out.bold = true
    elseif lv == "normal" or lv == "lighter" or (w and w < 600) then out.bold = false end
  elseif prop == "font-style" then
    if lv == "italic" or lv == "oblique" then out.italic = true elseif lv == "normal" then out.italic = false end
  elseif prop == "font" then
    if lv:find("bold", 1, true) then out.bold = true end
    if lv:find("italic", 1, true) then out.italic = true end
  elseif prop == "text-decoration" or prop == "text-decoration-line" then
    if lv:find("underline", 1, true) then out.ul = true elseif lv:find("none", 1, true) then out.ul = false end
  elseif prop == "text-align" then
    if lv == "left" or lv == "center" or lv == "right" then out.align = lv
    elseif lv == "justify" or lv == "start" then out.align = "left" elseif lv == "end" then out.align = "right" end
  elseif prop == "visibility" then
    out.hidden = (lv == "hidden" or lv == "collapse")
  elseif prop == "text-transform" then
    local t = TEXT_TRANSFORM[lv]; if t ~= nil then out.tt = t end
  elseif prop == "white-space" then
    local w = WHITE_SPACE[lv]; if w ~= nil then out.ws = w end
  elseif prop == "width" or prop == "max-width" then
    local l = parse_len(value)
    if l ~= nil and l ~= 0 then out[prop == "width" and "w" or "maxw"] = l end
  elseif prop == "border" then set_border(out, "trbl", value)
  elseif prop == "border-top" then set_border(out, "t", value)
  elseif prop == "border-right" then set_border(out, "r", value)
  elseif prop == "border-bottom" then set_border(out, "b", value)
  elseif prop == "border-left" then set_border(out, "l", value)
  elseif prop == "border-color" then
    local c = parse_color(value:match("^%S+") or ""); if c then out.bdc = c end
  elseif prop == "border-style" or prop == "border-width" then
    if lv == "none" or lv == "0" then out.bt, out.br, out.bb, out.bl = false, false, false, false end
  elseif prop == "list-style" or prop == "list-style-type" then
    if lv:find("none", 1, true) then out.nolist = true end
  end
end

-- Parse "a: b; c: d" into a table of computed-style fields (only the
-- supported properties). Used for style="" and for rule bodies. A value with
-- var() waits in out.__var (list of {prop, value}) until the style is computed;
-- a custom property "--x: v" goes to out.__vars.
function M.parse_decls(text)
  local out = {}
  for _, decl in ipairs(split_decls(text)) do
    local prop, value = decl:match("^%s*([%w%-]+)%s*:%s*(.-)%s*$")
    if prop then
      prop = prop:lower()
      value = value:gsub("%s*!%s*important%s*$", "")
      if prop:sub(1, 2) == "--" then
        local vars = out.__vars or {}
        out.__vars = vars
        vars[prop] = value
      elseif value:lower():find("var(", 1, true) then
        local list = out.__var or {}
        out.__var = list
        list[#list + 1] = { prop, value }
      else
        set_one(out, prop, value)
      end
    end
  end
  return out
end

-- Replace var(--x, fallback) in a value with the custom properties of the style.
local function subst_vars(value, vars)
  for _ = 1, 4 do
    local changed = false
    value = value:gsub("var%(%s*(%-%-[%w%-_]+)%s*,?%s*([^()]*)%)", function(name, fb)
      changed = true
      local v = vars and vars[name]
      if v == nil then v = fb ~= "" and fb or nil end
      return v or "\0invalid"
    end)
    if not changed then break end
  end
  if value:find("\0invalid", 1, true) or value:lower():find("var(", 1, true) then return nil end
  return value
end

---------------------------------------------------------------------------
-- Selectors
---------------------------------------------------------------------------
-- The tree is complete (not streamed): ":last-child" and the like can be answered.
local full_tree = false

local function elem_parent(node)
  local p = node.parent
  if p and p.tag and p.tag ~= "#text" then return p end
  return nil
end

local function prev_sibling(node)
  return node.prev
end

-- 1-based place of the node among the elements of its parent, and the count (nil if unknown).
local function position(node)
  local n = node.idx
  if not n then return 1, nil end
  local total
  if full_tree then
    local p = node.parent
    total = p and p.ecount
    if not total then
      total = 0
      for _, k in ipairs(p and p.kids or {}) do if k.tag ~= "#text" then total = total + 1 end end
    end
  end
  return n, total
end

local function nth_test(a, b, n)
  if a == 0 then return n == b end
  local d = n - b
  return d % a == 0 and d // a >= 0
end

local function parse_nth(arg)
  arg = arg:lower():gsub("%s+", "")
  if arg == "odd" then return 2, 1 end
  if arg == "even" then return 2, 0 end
  local a, b = arg:match("^([%+%-]?%d*)n([%+%-]%d+)$")
  if a then
    if a == "" or a == "+" then a = 1 elseif a == "-" then a = -1 else a = tonumber(a) end
    return a, tonumber(b)
  end
  a = arg:match("^([%+%-]?%d*)n$")
  if a then
    if a == "" or a == "+" then a = 1 elseif a == "-" then a = -1 else a = tonumber(a) end
    return a, 0
  end
  local n = tonumber(arg)
  if n then return 0, n end
  return nil
end

local ATTR_OPS = { ["="] = true, ["~="] = true, ["^="] = true, ["$="] = true, ["*="] = true, ["|="] = true }

-- Parse one compound ("a.b#c[d=e]:first-child"). Returns the compound, the specificity added,
-- or nil if something in it is not supported.
local function parse_compound(tok)
  local c = { classes = {}, attrs = {}, tests = {} }
  local spec = 0
  local i, n = 1, #tok
  local tag = tok:match("^([%a][%w%-]*)")
  if tag then c.tag = tag:lower(); i = #tag + 1; spec = 1
  elseif tok:sub(1, 1) == "*" then i = 2 end
  while i <= n do
    local ch = tok:sub(i, i)
    if ch == "#" or ch == "." then
      local name = tok:match("^[%w%-_]+", i + 1)
      if not name then return nil end
      i = i + 1 + #name
      if ch == "#" then
        if c.id then return nil end
        c.id = name; spec = spec + 10000
      else c.classes[#c.classes + 1] = name; spec = spec + 100 end
    elseif ch == "[" then
      local close = tok:find("]", i, true)
      if not close then return nil end
      local body = tok:sub(i + 1, close - 1)
      local name, op, val = body:match("^%s*([%w%-_:]+)%s*([~%^%$%*|]?=)%s*(.-)%s*$")
      if not name then
        name = body:match("^%s*([%w%-_:]+)%s*$")
        if not name then return nil end
      elseif not ATTR_OPS[op] then return nil end
      if val then
        val = val:gsub("%s+[iIsS]$", "")
        local q = val:sub(1, 1)
        if (q == '"' or q == "'") and val:sub(-1) == q then val = val:sub(2, -2) end
      end
      c.attrs[#c.attrs + 1] = { name = name:lower(), op = op, val = val }
      spec = spec + 100
      i = close + 1
    elseif ch == ":" then
      if tok:sub(i + 1, i + 1) == ":" then return nil end -- ::before and the like
      local name = tok:match("^[%a%-]+", i + 1)
      if not name then return nil end
      i = i + 1 + #name
      name = name:lower()
      local arg
      if tok:sub(i, i) == "(" then
        local depth, j = 0, i
        while j <= n do
          local cj = tok:sub(j, j)
          if cj == "(" then depth = depth + 1 elseif cj == ")" then depth = depth - 1; if depth == 0 then break end end
          j = j + 1
        end
        if depth ~= 0 then return nil end
        arg = tok:sub(i + 1, j - 1)
        i = j + 1
      end
      spec = spec + 100
      if name == "first-child" then c.tests[#c.tests + 1] = { "first" }
      elseif name == "last-child" then c.tests[#c.tests + 1] = { "last" }
      elseif name == "only-child" then c.tests[#c.tests + 1] = { "only" }
      elseif name == "empty" then c.tests[#c.tests + 1] = { "empty" }
      elseif name == "root" then c.tests[#c.tests + 1] = { "root" }
      elseif name == "nth-child" and arg then
        local a, b = parse_nth(arg)
        if not a then return nil end
        c.tests[#c.tests + 1] = { "nth", a, b }
      elseif name == "nth-last-child" and arg then
        local a, b = parse_nth(arg)
        if not a then return nil end
        c.tests[#c.tests + 1] = { "nthlast", a, b }
      elseif name == "not" and arg then
        local inner = parse_compound((arg:gsub("^%s+", ""):gsub("%s+$", "")))
        if not inner then return nil end
        c.tests[#c.tests + 1] = { "not", inner }
      elseif name == "is" or name == "where" then
        return nil
      else
        return nil -- :hover :focus :visited :checked ... never match here
      end
    else
      return nil
    end
  end
  return c, spec
end

-- Parse one selector (no comma). Returns {compound, ...}, specificity or nil. A compound has
-- `comb`, the combinator that joins it to the one before: " ", ">", "+" or "~".
local function parse_selector(sel)
  -- split on spaces and on > + ~ outside of [] and ()
  local toks, depth, cur = {}, 0, {}
  local function flush()
    if #cur > 0 then toks[#toks + 1] = table.concat(cur); cur = {} end
  end
  for i = 1, #sel do
    local ch = sel:sub(i, i)
    if ch == "[" or ch == "(" then depth = depth + 1; cur[#cur + 1] = ch
    elseif ch == "]" or ch == ")" then depth = depth - 1; cur[#cur + 1] = ch
    elseif depth > 0 then cur[#cur + 1] = ch
    elseif ch:find("%s") then flush()
    elseif ch == ">" or ch == "+" or ch == "~" then flush(); toks[#toks + 1] = ch
    else cur[#cur + 1] = ch end
  end
  flush()
  if depth ~= 0 then return nil end
  local chain, spec = {}, 0
  local comb = " "
  for _, tok in ipairs(toks) do
    if tok == ">" or tok == "+" or tok == "~" then
      if #chain == 0 or comb ~= " " then return nil end
      comb = tok
    else
      local c, sp = parse_compound(tok)
      if not c then return nil end
      c.comb = comb
      comb = " "
      spec = spec + sp
      chain[#chain + 1] = c
    end
  end
  if #chain == 0 or comb ~= " " or #chain > 8 then return nil end
  return chain, spec
end

local function class_set(node)
  local cs = node.cls
  if not cs then
    cs = {}
    local v = node.attrs and node.attrs.class
    if v then for c in v:gmatch("%S+") do cs[c] = true end end
    node.cls = cs
  end
  return cs
end

local function attr_ok(a, node)
  local v = node.attrs and node.attrs[a.name]
  if v == nil then return false end
  local op, want = a.op, a.val
  if not op then return true end
  if op == "=" then return v == want end
  if want == "" then return false end
  if op == "^=" then return v:sub(1, #want) == want end
  if op == "$=" then return v:sub(-#want) == want end
  if op == "*=" then return v:find(want, 1, true) ~= nil end
  if op == "~=" then return (" " .. v:gsub("%s+", " ") .. " "):find(" " .. want .. " ", 1, true) ~= nil end
  if op == "|=" then return v == want or v:sub(1, #want + 1) == want .. "-" end
  return false
end

local match_compound

local function test_ok(t, node)
  local k = t[1]
  if k == "first" then local n = position(node); return n == 1
  elseif k == "last" then
    if not full_tree then return false end
    local n, total = position(node); return total ~= nil and n == total
  elseif k == "only" then
    if not full_tree then return false end
    local n, total = position(node); return n == 1 and total == 1
  elseif k == "empty" then
    if not full_tree then return false end
    return #node.kids == 0
  elseif k == "root" then return node.tag == "html"
  elseif k == "nth" then
    local n = position(node); return nth_test(t[2], t[3], n)
  elseif k == "nthlast" then
    if not full_tree then return false end
    local n, total = position(node)
    return total ~= nil and nth_test(t[2], t[3], total - n + 1)
  elseif k == "not" then return not match_compound(t[2], node)
  end
  return false
end

function match_compound(c, node)
  if c.tag and c.tag ~= node.tag then return false end
  if c.id and (node.attrs and node.attrs.id) ~= c.id then return false end
  if #c.classes > 0 then
    local cs = class_set(node)
    for _, name in ipairs(c.classes) do if not cs[name] then return false end end
  end
  for _, a in ipairs(c.attrs) do if not attr_ok(a, node) then return false end end
  for _, t in ipairs(c.tests) do if not test_ok(t, node) then return false end end
  return true
end

local function match_at(chain, i, node)
  if not match_compound(chain[i], node) then return false end
  if i == 1 then return true end
  local comb = chain[i].comb
  if comb == ">" then
    local p = elem_parent(node)
    return p ~= nil and match_at(chain, i - 1, p)
  elseif comb == " " then
    local p = elem_parent(node)
    while p do
      if match_at(chain, i - 1, p) then return true end
      p = elem_parent(p)
    end
    return false
  elseif comb == "+" then
    local pv = prev_sibling(node)
    return pv ~= nil and match_at(chain, i - 1, pv)
  else -- "~"
    local pv = prev_sibling(node)
    while pv do
      if match_at(chain, i - 1, pv) then return true end
      pv = prev_sibling(pv)
    end
    return false
  end
end

local function match(chain, node)
  return match_at(chain, #chain, node)
end

-- For scripts (querySelector, matches, closest): compile a selector list and test a node.
-- Returns a list of chains, or nil if a part of the list is not supported.
function M.selector(list)
  local out = {}
  for sel in (list .. ","):gmatch("([^,]*),") do
    sel = sel:gsub("^%s+", ""):gsub("%s+$", "")
    if sel ~= "" then
      local chain = parse_selector(sel)
      if not chain then return nil end
      out[#out + 1] = chain
    end
  end
  if #out == 0 then return nil end
  return out
end

function M.matches(chains, node)
  if not node.tag or node.tag == "#text" then return false end
  local saved = full_tree
  full_tree = true
  local ok = false
  for i = 1, #chains do
    if match(chains[i], node) then ok = true; break end
  end
  full_tree = saved
  return ok
end

---------------------------------------------------------------------------
-- Rule set
---------------------------------------------------------------------------
local Rules = {}
Rules.__index = Rules

function M.rules(opts)
  opts = opts or {}
  return setmetatable({
    by_id = {}, by_class = {}, by_tag = {}, any = {},
    n = 0, bytes = 0, cut = false,
    max_css = opts.max_css or M.LIMITS.max_css,
    max_rules = opts.max_rules or M.LIMITS.max_rules,
    dropped = 0, -- rules with selectors that are not supported
    depth = 0, full = false,
  }, Rules)
end

local function skip_block(text, i)
  -- i is the index of "{"; returns the index after the matching "}"
  local depth = 0
  local n = #text
  while i <= n do
    local j = text:find("[{}]", i)
    if not j then return n + 1 end
    if text:sub(j, j) == "{" then depth = depth + 1 else depth = depth - 1 end
    i = j + 1
    if depth == 0 then return i end
  end
  return n + 1
end

-- Is an @media query true for the screen? (min-width, max-width, screen, all; the rest is false.)
function M.media_ok(q)
  q = q:lower()
  for part in (q .. ","):gmatch("([^,]*),") do
    local ok, any = true, false
    part = part:gsub("^%s*(only)%s+", ""):gsub("%f[%a]not%f[%A]", "\1neg")
    if part:find("\1neg", 1, true) then ok = false end
    for word in part:gmatch("%a+") do
      if word == "print" or word == "speech" then ok = false end
    end
    for feat, val in part:gmatch("%(%s*([%a%-]+)%s*:%s*([^)]*)%)") do
      any = true
      local n = parse_len(val:gsub("%s+$", ""))
      if feat == "min-width" then ok = ok and type(n) == "number" and M.SCREEN_W >= n
      elseif feat == "max-width" then ok = ok and type(n) == "number" and M.SCREEN_W <= n
      elseif feat == "min-height" or feat == "max-height" then ok = ok and true
      elseif feat == "prefers-color-scheme" then ok = ok and val:find("light", 1, true) ~= nil
      elseif feat == "prefers-reduced-motion" then ok = ok and val:find("reduce", 1, true) ~= nil
      elseif feat == "hover" then ok = ok and val:find("hover", 1, true) ~= nil
      elseif feat == "pointer" then ok = ok and val:find("fine", 1, true) ~= nil
      else ok = false end
    end
    if part:find("%(%s*[%a%-]+%s*%)") then ok = false end
    if ok then return true end
  end
  return false
end

function Rules:add_rule(chain, spec, decls)
  local last = chain[#chain]
  local entry = { chain = chain, spec = spec, order = self.n, decls = decls }
  local bucket
  if last.id then
    bucket = self.by_id[last.id] or {}; self.by_id[last.id] = bucket
  elseif #last.classes > 0 then
    local k = last.classes[1]
    bucket = self.by_class[k] or {}; self.by_class[k] = bucket
  elseif last.tag then
    bucket = self.by_tag[last.tag] or {}; self.by_tag[last.tag] = bucket
  else bucket = self.any end
  bucket[#bucket + 1] = entry
end

-- Add the text of a style sheet. The total is capped by max_css.
function Rules:add(text)
  local room = self.max_css - self.bytes
  if room <= 0 then self.cut = true; return end
  if #text > room then text = text:sub(1, room); self.cut = true end
  self.bytes = self.bytes + #text
  text = text:gsub("/%*.-%*/", ""):gsub("/%*.*$", "")
  self:add_inner(text)
end

function Rules:add_inner(text)
  local i, n = 1, #text
  while i <= n do
    local brace = text:find("{", i, true)
    if not brace then break end
    local head = text:sub(i, brace - 1):gsub("^%s+", ""):gsub("%s+$", "")
    local close = skip_block(text, brace)
    if head:sub(1, 1) == "@" then
      local semi = head:find(";", 1, true)
      if semi then
        -- an @import or @charset ends with ";" before the "{" of the next rule
        head = head:sub(semi + 1):gsub("^%s+", "")
        if head:sub(1, 1) ~= "@" and head ~= "" then
          self:add_selectors(head, text:sub(brace + 1, close - 2))
        end
      elseif head:lower():match("^@media") then
        if M.media_ok(head:sub(7)) and self.depth < 3 then
          self.depth = self.depth + 1
          local inner_bytes = self.bytes
          self:add_inner(text:sub(brace + 1, close - 2))
          self.bytes = inner_bytes
          self.depth = self.depth - 1
        end
      elseif head:lower():match("^@supports") then
        if self.depth < 3 then
          self.depth = self.depth + 1
          local inner_bytes = self.bytes
          self:add_inner(text:sub(brace + 1, close - 2))
          self.bytes = inner_bytes
          self.depth = self.depth - 1
        end
      end
    else
      self:add_selectors(head, text:sub(brace + 1, close - 2))
    end
    i = close
  end
end

function Rules:add_selectors(head, body)
  local decls
  for sel in head:gmatch("[^,]+") do
    local chain, spec = parse_selector(sel)
    if not chain then self.dropped = self.dropped + 1
    elseif self.n >= self.max_rules then self.cut = true; return
    else
      decls = decls or M.parse_decls(body)
      if next(decls) then
        self.n = self.n + 1
        self:add_rule(chain, spec, decls)
      end
    end
  end
end

local function collect(bucket, node, out)
  if not bucket then return end
  for i = 1, #bucket do
    local r = bucket[i]
    if match(r.chain, node) then out[#out + 1] = r end
  end
end

---------------------------------------------------------------------------
-- Default sheet and computed style
---------------------------------------------------------------------------
local BLOCKS = {}
for w in ([[html body div p h1 h2 h3 h4 h5 h6 ul ol li dl dt dd pre blockquote table tr
  form nav header main footer section article aside figure figcaption address center
  thead tbody tfoot caption hr option details summary dialog fieldset legend hgroup menu search
  video audio canvas iframe]]):gmatch("%S+") do BLOCKS[w] = true end

local UA = {
  body = { mt = 4, mr = 4, mb = 4, ml = 4 },
  p = { mt = 12, mb = 12 },
  h1 = { bold = true, mt = 12, mb = 12 },
  h2 = { bold = true, mt = 12, mb = 12 },
  h3 = { bold = true, mt = 12, mb = 12 },
  h4 = { bold = true, mt = 12, mb = 12 },
  h5 = { bold = true, mt = 12, mb = 12 },
  h6 = { bold = true, mt = 12, mb = 12 },
  ul = { mt = 12, mb = 12, pl = 18 },
  ol = { mt = 12, mb = 12, pl = 18 },
  dl = { mt = 12, mb = 12 },
  dd = { ml = 18 },
  blockquote = { mt = 12, mb = 12, ml = 18, mr = 18 },
  pre = { mt = 12, mb = 12 },
  hr = { mt = 6, mb = 6 },
  a = { color = 0x0000EE, ul = true },
  b = { bold = true }, strong = { bold = true },
  i = { italic = true }, em = { italic = true }, cite = { italic = true },
  u = { ul = true }, ins = { ul = true },
  th = { bold = true, align = "center" },
  caption = { align = "center" },
  center = { align = "center" },
  summary = { bold = true },
  legend = { bold = true },
  fieldset = { mt = 6, mb = 6, bt = true, br = true, bb = true, bl = true, bdc = 0x808080, pt = 4, pl = 4, pr = 4, pb = 4 },
  mark = { bg = 0xFFFF00 },
  dialog = { mt = 6, mb = 6, bt = true, br = true, bb = true, bl = true, bdc = 0x000000, pt = 4, pl = 4, pr = 4, pb = 4, bg = 0xFFFFFF },
  menu = { mt = 12, mb = 12, pl = 18 },
  code = { color = 0x333333 }, kbd = { color = 0x333333 }, samp = { color = 0x333333 },
}
M.UA = UA

local function apply(st, decls)
  local vars, deferred = decls.__vars, decls.__var
  if vars then
    local mine = {}
    if st.vars then for k, v in pairs(st.vars) do mine[k] = v end end
    for k, v in pairs(vars) do mine[k] = v end
    st.vars = mine
  end
  for k, v in pairs(decls) do
    if k ~= "__vars" and k ~= "__var" then st[k] = v end
  end
  if deferred then
    for _, d in ipairs(deferred) do
      local v = subst_vars(d[2], st.vars)
      if v then
        local t = M.parse_decls(d[1] .. ":" .. v)
        for k, val in pairs(t) do
          if k ~= "__vars" and k ~= "__var" then st[k] = val end
        end
      end
    end
  end
end

local function by_spec(a, b)
  if a.spec ~= b.spec then return a.spec < b.spec end
  return a.order < b.order
end

-- Compute the style of an element. ps is the parent's computed style (or
-- nil for the root). Fields: color, bg, display, mt mr mb ml, pt pr pb pl,
-- bold, italic, ul, align, hidden, tt (text-transform), ws ("pre"), w, maxw,
-- bt br bb bl (border sides) with bdc (border colour), nolist, vars (custom properties).
function M.compute(node, rules, ps)
  local st = {
    color = ps and ps.color or 0x000000,
    bold = ps and ps.bold or false,
    italic = ps and ps.italic or false,
    ul = ps and ps.ul or false,
    align = ps and ps.align or "left",
    hidden = ps and ps.hidden or false,
    vars = ps and ps.vars or nil,
    tt = ps and ps.tt or false,
    ws = ps and ps.ws or false,
    nolist = ps and ps.nolist or false,
    display = BLOCKS[node.tag] and "block" or "inline",
    mt = 0, mr = 0, mb = 0, ml = 0, pt = 0, pr = 0, pb = 0, pl = 0,
  }
  local tag = node.tag
  local ua = UA[tag]
  if ua then apply(st, ua) end
  local attrs = node.attrs
  full_tree = rules and rules.full or false
  if rules and attrs then
    local found = {}
    collect(rules.by_tag[tag], node, found)
    collect(rules.any, node, found)
    if attrs.id then collect(rules.by_id[attrs.id], node, found) end
    if attrs.class then
      for c in attrs.class:gmatch("%S+") do collect(rules.by_class[c], node, found) end
    end
    if #found > 1 then table.sort(found, by_spec) end
    for i = 1, #found do apply(st, found[i].decls) end
  end
  if attrs then
    if attrs.style then apply(st, M.parse_decls(attrs.style)) end
    -- Utility class "hidden" and the hidden attribute hide the element.
    if attrs.hidden then st.display = "none" end
    local cls = attrs.class
    if cls and (" " .. cls .. " "):find(" hidden ", 1, true) then st.display = "none" end
  end
  return st
end

return M
