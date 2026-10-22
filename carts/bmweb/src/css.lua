-- bmweb: minimal CSS. Pure Lua.
-- Selectors: tag, .class, #id, "*", compounds of those, and descendant chains
-- ("a b c"). A rule with anything else (>, +, ~, [attr], :pseudo) is dropped.
-- Properties: color, background-color, display, margin*, padding*,
-- font-weight, font-style, text-align, text-decoration, visibility.
-- Units: px, em (1 em = 12 px, the line height), %. At-rules are skipped.
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
}

-- Returns an integer 0xRRGGBB, false for "transparent", or nil if unknown.
local function parse_color(v)
  v = v:lower()
  if v == "transparent" then return false end
  if NAMED_COLORS[v] then return NAMED_COLORS[v] end
  local h = v:match("^#(%x+)$")
  if h then
    if #h == 3 then return tonumber(h:gsub(".", "%0%0"), 16) end
    if #h == 6 then return tonumber(h, 16) end
    return nil
  end
  local r, g, b = v:match("^rgba?%(%s*(%d+)%s*,%s*(%d+)%s*,%s*(%d+)")
  if r then
    return (math.min(255, tonumber(r)) << 16) | (math.min(255, tonumber(g)) << 8) | math.min(255, tonumber(b))
  end
  return nil
end
M.parse_color = parse_color

-- A length: a number of px, a string "N%" or nil if not supported.
local function parse_len(v)
  v = v:lower()
  if v == "0" or v == "auto" then return 0 end
  local n, u = v:match("^(%-?[%d.]+)(%a*%%?)$")
  n = tonumber(n)
  if not n then return nil end
  if n < 0 then n = 0 end
  if u == "px" then return math.floor(n + 0.5) end
  if u == "em" or u == "rem" then return math.floor(n * M.EM + 0.5) end
  if u == "%" then return n .. "%" end
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
  for tok in value:gmatch("%S+") do
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

-- Parse "a: b; c: d" into a table of computed-style fields (only the
-- supported properties). Used for style="" and for rule bodies.
function M.parse_decls(text)
  local out = {}
  for prop, value in text:gmatch("([%w%-]+)%s*:%s*([^;]+)") do
    prop = prop:lower()
    value = value:gsub("!%s*important", ""):gsub("^%s+", ""):gsub("%s+$", "")
    local lv = value:lower()
    if lv:find("var(", 1, true) or lv:find("calc(", 1, true) then
      -- not supported: ignore the declaration
    elseif prop == "color" then
      local c = parse_color(value); if c then out.color = c end
    elseif prop == "background-color" or prop == "background" then
      local c = parse_color(value:match("^%S+") or "")
      if c ~= nil then out.bg = c end
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
    elseif prop == "text-decoration" or prop == "text-decoration-line" then
      if lv:find("underline", 1, true) then out.ul = true elseif lv:find("none", 1, true) then out.ul = false end
    elseif prop == "text-align" then
      if lv == "left" or lv == "center" or lv == "right" then out.align = lv
      elseif lv == "justify" or lv == "start" then out.align = "left" elseif lv == "end" then out.align = "right" end
    elseif prop == "visibility" then
      out.hidden = (lv == "hidden" or lv == "collapse")
    end
  end
  return out
end

---------------------------------------------------------------------------
-- Selectors
---------------------------------------------------------------------------
-- Parse one selector (no comma). Returns {compound, ...}, specificity or nil.
local function parse_selector(sel)
  local chain, spec = {}, 0
  for tok in sel:gmatch("%S+") do
    if tok == ">" or tok == "+" or tok == "~" or tok:find("[%[%]:>+~(]") then return nil end
    local c = { classes = {} }
    local rest = tok
    local tag, tail = rest:match("^([%a][%w%-]*)(.*)$")
    if tag then c.tag = tag:lower(); rest = tail; spec = spec + 1
    elseif rest:sub(1, 1) == "*" then rest = rest:sub(2) end
    while rest ~= "" do
      local kind, name, tail2 = rest:match("^([#.])([%w%-_]+)(.*)$")
      if not kind then return nil end
      if kind == "#" then
        if c.id then return nil end
        c.id = name; spec = spec + 10000
      else c.classes[#c.classes + 1] = name; spec = spec + 100 end
      rest = tail2
    end
    chain[#chain + 1] = c
  end
  if #chain == 0 or #chain > 6 then return nil end
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

local function match_compound(c, node)
  if c.tag and c.tag ~= node.tag then return false end
  if c.id and (node.attrs and node.attrs.id) ~= c.id then return false end
  if #c.classes > 0 then
    local cs = class_set(node)
    for _, name in ipairs(c.classes) do if not cs[name] then return false end end
  end
  return true
end

local function match(chain, node)
  if not match_compound(chain[#chain], node) then return false end
  local cur = node.parent
  for i = #chain - 1, 1, -1 do
    while cur and not match_compound(chain[i], cur) do cur = cur.parent end
    if not cur then return false end
    cur = cur.parent
  end
  return true
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
  local i, n = 1, #text
  while i <= n do
    local brace = text:find("{", i, true)
    if not brace then break end
    local head = text:sub(i, brace - 1):gsub("^%s+", ""):gsub("%s+$", "")
    local close = skip_block(text, brace)
    if head:sub(1, 1) == "@" then
      -- at-rule: skipped with its block (@media, @font-face, @keyframes ...)
      -- an @import or @charset ends with ";" before the "{" of the next rule
      local semi = head:find(";", 1, true)
      if semi then
        head = head:sub(semi + 1):gsub("^%s+", "")
        if head:sub(1, 1) ~= "@" and head ~= "" then
          local body = text:sub(brace + 1, close - 2)
          self:add_selectors(head, body)
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
  thead tbody tfoot caption hr option]]):gmatch("%S+") do BLOCKS[w] = true end

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
  code = { color = 0x333333 }, kbd = { color = 0x333333 }, samp = { color = 0x333333 },
}
M.UA = UA

local INHERIT = { "color", "bold", "italic", "ul", "align", "hidden" }

local function apply(st, decls)
  for k, v in pairs(decls) do st[k] = v end
end

local function by_spec(a, b)
  if a.spec ~= b.spec then return a.spec < b.spec end
  return a.order < b.order
end

-- Compute the style of an element. ps is the parent's computed style (or
-- nil for the root). Fields: color, bg, display, mt mr mb ml, pt pr pb pl,
-- bold, italic, ul, align, hidden.
function M.compute(node, rules, ps)
  local st = {
    color = ps and ps.color or 0x000000,
    bold = ps and ps.bold or false,
    italic = ps and ps.italic or false,
    ul = ps and ps.ul or false,
    align = ps and ps.align or "left",
    hidden = ps and ps.hidden or false,
    display = BLOCKS[node.tag] and "block" or "inline",
    mt = 0, mr = 0, mb = 0, ml = 0, pt = 0, pr = 0, pb = 0, pl = 0,
  }
  local tag = node.tag
  local ua = UA[tag]
  if ua then apply(st, ua) end
  local attrs = node.attrs
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
