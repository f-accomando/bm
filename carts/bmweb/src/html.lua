-- bmweb: HTML tokenizer (chunked) and tree builder. Pure Lua.
-- The tokenizer takes chunks with feed(), so a page never has to be in RAM
-- as one string. The builder keeps a small stack with the implied end rules
-- of HTML (p, li, dt, dd, tr, td, th, option) and the size caps.
local M = {}

M.LIMITS = {
  max_bytes = 1048576, -- input bytes read; the rest is cut
  max_nodes = 20000,   -- nodes created
  max_depth = 64,      -- open elements at once
  max_token = 4096,    -- bytes of one tag or comment
}

local NAMED = {
  amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = "\u{A0}",
  copy = "\u{A9}", reg = "\u{AE}", trade = "\u{2122}", hellip = "\u{2026}",
  mdash = "\u{2014}", ndash = "\u{2013}", lsquo = "\u{2018}", rsquo = "\u{2019}",
  ldquo = "\u{201C}", rdquo = "\u{201D}", bull = "\u{2022}", middot = "\u{B7}",
  laquo = "\u{AB}", raquo = "\u{BB}", deg = "\u{B0}", times = "\u{D7}",
  divide = "\u{F7}", euro = "\u{20AC}", pound = "\u{A3}", yen = "\u{A5}",
  cent = "\u{A2}", sect = "\u{A7}", para = "\u{B6}", plusmn = "\u{B1}",
  frac12 = "\u{BD}", larr = "\u{2190}", rarr = "\u{2192}", uarr = "\u{2191}",
  darr = "\u{2193}", hearts = "\u{2665}", szlig = "\u{DF}", ntilde = "\u{F1}",
  ccedil = "\u{E7}", agrave = "\u{E0}", aacute = "\u{E1}", egrave = "\u{E8}",
  eacute = "\u{E9}", ouml = "\u{F6}", uuml = "\u{FC}", auml = "\u{E4}",
}

local function codepoint(n)
  if n == 0 or n > 0x10FFFF or (n >= 0xD800 and n <= 0xDFFF) then n = 0xFFFD end
  return utf8.char(n)
end

-- Replace character references. An unknown reference stays as typed.
local function decode(s)
  if not s:find("&", 1, true) then return s end
  return (s:gsub("&(#?[%w]+);", function(name)
    if name:sub(1, 1) == "#" then
      local n
      if name:match("^#[xX]%x+$") then n = tonumber(name:sub(3), 16)
      elseif name:match("^#%d+$") then n = tonumber(name:sub(2)) end
      if not n then return nil end
      return codepoint(n)
    end
    return NAMED[name]
  end))
end
M.decode = decode

-- Tags whose content is raw text. "drop" ones emit no text token.
local RAW = { script = "drop", iframe = "drop", style = "raw", title = "rcdata", textarea = "rcdata" }
M.VOID = { br = true, hr = true, img = true, input = true, meta = true, link = true,
  base = true, area = true, col = true, embed = true, source = true, track = true, wbr = true }

---------------------------------------------------------------------------
-- Tokenizer
---------------------------------------------------------------------------
local T = {}
T.__index = T

-- emit(tok): tok = {t="start", name, attrs, self} | {t="end", name} | {t="text", s}
function M.tokenizer(emit, opts)
  local lim = M.LIMITS
  opts = opts or {}
  return setmetatable({
    emit = emit, buf = "", raw = nil, skip = nil, bytes = 0, cut = false, done = false,
    max_bytes = opts.max_bytes or lim.max_bytes, max_token = opts.max_token or lim.max_token,
    dropped = 0, -- tags and comments dropped for size
  }, T)
end

-- Scan a tag that starts at s[i] == "<". Returns tok, next_index; or nil when
-- the buffer ends inside the tag; or false, next_index for a text "<"; or
-- "bogus", i for <!doctype>, <?xml?> and </>.
local function scan_tag(s, i)
  local n = #s
  local c = s:sub(i + 1, i + 1)
  if c == "" then return nil end
  local is_end = c == "/"
  local p = i + (is_end and 2 or 1)
  local name, e = s:match("^(%a[^%s/>]*)()", p)
  if not name then
    if is_end and p > n then return nil end
    if is_end or c == "!" or c == "?" then return "bogus", i end
    return false, i + 1
  end
  if e > n then return nil end
  name = name:lower()
  if is_end then
    local gt = s:find(">", e, true)
    if not gt then return nil end
    return { t = "end", name = name }, gt + 1
  end
  local attrs, count, selfc = {}, 0, false
  p = e
  while true do
    p = s:find("[^%s]", p)
    if not p then return nil end
    local ch = s:sub(p, p)
    if ch == ">" then p = p + 1; break
    elseif ch == "/" then
      if p + 1 > n then return nil end
      if s:sub(p + 1, p + 1) == ">" then selfc = true; p = p + 2; break end
      p = p + 1
    elseif ch == "=" then p = p + 1
    else
      local an, e2 = s:match("^([^%s=/>]+)()", p)
      local val = ""
      local q = s:find("[^%s]", e2)
      if not q then return nil end
      if s:sub(q, q) == "=" then
        q = s:find("[^%s]", q + 1)
        if not q then return nil end
        local qc = s:sub(q, q)
        if qc == '"' or qc == "'" then
          local close = s:find(qc, q + 1, true)
          if not close then return nil end
          val = s:sub(q + 1, close - 1); p = close + 1
        else
          local v, e3 = s:match("^([^%s>]*)()", q)
          if e3 > n then return nil end
          val = v; p = e3
        end
      else
        p = e2
      end
      an = an:lower()
      if attrs[an] == nil and count < 32 then
        if #val > 2048 then val = val:sub(1, 2048) end
        attrs[an] = decode(val); count = count + 1
      end
    end
  end
  return { t = "start", name = name, attrs = attrs, self = selfc }, p
end

-- Find the close tag of a raw text element from index i (case-insensitive).
local function find_close(s, i, name)
  local len = #name
  local k = i
  while true do
    k = s:find("</", k, true)
    if not k then return nil end
    if s:sub(k + 2, k + 1 + len):lower() == name then
      local nxt = s:sub(k + 2 + len, k + 2 + len)
      if nxt == "" or not nxt:match("%w") then return k end
    end
    k = k + 2
  end
end

function T:raw_text(text, name)
  local mode = RAW[name]
  if text == "" or mode == "drop" then return end
  if mode == "rcdata" then text = decode(text) end
  self.emit({ t = "text", s = text })
end

function T:run(final)
  local s, i, n = self.buf, 1, #self.buf
  local emit = self.emit
  while i <= n do
    if self.skip then
      -- Dropping a comment or a too long tag until the terminator.
      local term = self.skip
      local e = s:find(term, i, true)
      if e then i = e + #term; self.skip = nil
      else
        i = math.max(i, n - #term + 2)
        break
      end
    elseif self.raw then
      local raw = self.raw
      local j = find_close(s, i, raw)
      if not j then
        if final then
          self:raw_text(s:sub(i), raw); i = n + 1
        else
          local upto = n - (#raw + 2)
          if upto >= i and RAW[raw] == "rcdata" then
            -- do not cut a character reference in two
            local amp = s:sub(i, upto):find("&[%w#]*$")
            if amp and upto - (i + amp - 1) < 12 then upto = i + amp - 2 end
          end
          if upto >= i then self:raw_text(s:sub(i, upto), raw); i = upto + 1 end
          break
        end
      else
        self:raw_text(s:sub(i, j - 1), raw)
        local gt = s:find(">", j, true)
        if not gt then
          if final then i = n + 1 else i = j; break end
        else
          self.raw = nil
          emit({ t = "end", name = raw })
          i = gt + 1
        end
      end
    elseif s:byte(i) ~= 60 then -- not "<": text
      local lt = s:find("<", i, true)
      local stop = lt and lt - 1 or n
      local chunk = s:sub(i, stop)
      local hold
      if not lt and not final then
        local amp = chunk:find("&[%w#]*$")
        if amp and #chunk - amp < 12 then hold = amp end
      end
      if hold then chunk = chunk:sub(1, hold - 1) end
      if chunk ~= "" then emit({ t = "text", s = decode(chunk) }) end
      if hold then i = i + hold - 1; break end
      i = stop + 1
    elseif s:sub(i, i + 3) == "<!--" then
      local e = s:find("-->", i + 4, true)
      if e then i = e + 3
      elseif final then i = n + 1
      elseif n - i > self.max_token then self.skip = "-->"; i = n - 1; self.dropped = self.dropped + 1; break
      else break end
    else
      local tok, nxt = scan_tag(s, i)
      if tok == nil then
        if final then i = n + 1
        elseif n - i + 1 > self.max_token then
          -- A tag this long is dropped up to the next ">".
          self.skip = ">"; self.dropped = self.dropped + 1; i = n + 1
        else break end
      elseif tok == false then
        emit({ t = "text", s = "<" }); i = nxt
      elseif tok == "bogus" then
        local gt = s:find(">", i, true)
        if gt then i = gt + 1 elseif final then i = n + 1 else self.skip = ">"; i = n + 1 end
      elseif nxt - i > self.max_token then
        self.dropped = self.dropped + 1; i = nxt
      else
        emit(tok)
        i = nxt
        if tok.t == "start" and RAW[tok.name] and not tok.self then self.raw = tok.name end
      end
    end
  end
  self.buf = i > n and "" or s:sub(i)
end

function T:feed(chunk)
  if self.done then return end
  local room = self.max_bytes - self.bytes
  if #chunk > room then chunk = chunk:sub(1, room); self.cut = true end
  self.bytes = self.bytes + #chunk
  self.buf = self.buf .. chunk
  self:run(self.cut)
  if self.cut then self.done = true end
end

function T:finish()
  if self.done then return end
  self:run(true)
  self.done = true
end

---------------------------------------------------------------------------
-- Tree builder
---------------------------------------------------------------------------
local function set(list)
  local t = {}
  for w in list:gmatch("%S+") do t[w] = true end
  return t
end

local KNOWN = set[[html head title body h1 h2 h3 h4 h5 h6 p div span br hr a ul ol li dl dt dd pre
  code b strong i em u blockquote img table tr th td thead tbody tfoot caption form input button
  select option textarea label nav header main footer section article aside figure figcaption
  address center small s del sub sup abbr cite q mark kbd samp var tt big svg style]]
-- Elements whose whole subtree is dropped.
local SKIP = set"script noscript iframe video audio canvas object embed template"
local HEADTAGS = set"title style"
-- Tags that close an open p.
local CLOSES_P = set[[p div ul ol dl h1 h2 h3 h4 h5 h6 table pre blockquote hr form section
  article aside header footer nav main figure address center]]
local P_BARRIER = set"table td th body html button caption"
-- name -> {tags it closes, barrier tags}
local AUTOCLOSE = {
  li = { set"li", set"ul ol table body" },
  dt = { set"dt dd", set"dl table body" },
  dd = { set"dt dd", set"dl table body" },
  tr = { set"tr td th", set"table" },
  td = { set"td th", set"tr table" },
  th = { set"td th", set"tr table" },
  thead = { set"tr td th thead tbody tfoot", set"table" },
  tbody = { set"tr td th thead tbody tfoot", set"table" },
  tfoot = { set"tr td th thead tbody tfoot", set"table" },
  option = { set"option", set"select" },
}
-- An end tag stops at these unless it is one of them.
local END_BARRIER = set"td th table body html"

-- opts: limits, plus hooks on_style(text), on_open(node), on_close(node),
-- on_body(node). A hook may unlink nodes it has used (streaming).
function M.builder(opts)
  opts = opts or {}
  local lim = M.LIMITS
  local b = {
    max_nodes = opts.max_nodes or lim.max_nodes,
    max_depth = opts.max_depth or lim.max_depth,
    nodes = 0, depth_cut = 0, scripts = 0, cut = false, cut_reason = nil,
    title = nil, base_href = nil, charset = nil,
    root = { tag = "html", kids = {}, attrs = {} },
  }
  local stack = { b.root }
  local phantom = {} -- start tags past the depth cap: their end tags are ignored
  local skip = nil

  local function stop(reason)
    if not b.cut then b.cut = true; b.cut_reason = reason end
  end

  local function new_node(tag, attrs, parent)
    if b.nodes >= b.max_nodes then stop("nodes"); return nil end
    b.nodes = b.nodes + 1
    local n = { tag = tag, attrs = attrs, kids = {}, parent = parent }
    local kids = parent.kids
    kids[#kids + 1] = n
    return n
  end

  local function kid_text(node)
    local t = {}
    for _, k in ipairs(node.kids) do if k.tag == "#text" then t[#t + 1] = k.text end end
    return table.concat(t)
  end

  local function finish(node)
    if node.tag == "style" then
      local text = kid_text(node)
      node.kids = {}
      if opts.on_style then opts.on_style(text) end
    elseif node.tag == "title" and not b.title then
      b.title = (kid_text(node):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", ""))
    end
    if opts.on_close then opts.on_close(node) end
  end

  local function pop_to(k)
    for j = #stack, k, -1 do
      local node = stack[j]
      stack[j] = nil
      finish(node)
    end
  end

  local function ensure_body()
    if b.body then return end
    for j = #stack, 2, -1 do
      if stack[j].tag == "head" then pop_to(j) break end
    end
    local body = new_node("body", {}, b.root)
    if not body then return end
    b.body = body
    stack[#stack + 1] = body
    if opts.on_body then opts.on_body(body) end
  end

  local function ensure_head()
    if b.head or b.body then return end
    b.head = new_node("head", {}, b.root)
    if b.head then stack[#stack + 1] = b.head end
  end

  local function autoclose(name)
    if CLOSES_P[name] then
      for j = #stack, 2, -1 do
        local t = stack[j].tag
        if t == "p" then pop_to(j) break end
        if P_BARRIER[t] then break end
      end
    end
    local rule = AUTOCLOSE[name]
    if rule then
      -- close down to the lowest matching element before a barrier
      local found
      for j = #stack, 2, -1 do
        local t = stack[j].tag
        if rule[1][t] then found = j
        elseif rule[2][t] then break end
      end
      if found then pop_to(found) end
    end
  end

  local function text(s)
    if b.cut or skip then return end
    local top = stack[#stack]
    if not b.body then
      local t = top.tag
      if t ~= "title" and t ~= "style" then
        if not s:find("%S") then return end
        ensure_body()
        top = stack[#stack]
      end
    end
    if top.tag == "html" then ensure_body(); top = stack[#stack] end
    local kids = top.kids
    local last = kids[#kids]
    if last and last.tag == "#text" then last.text = last.text .. s; return end
    if b.nodes >= b.max_nodes then stop("nodes"); return end
    b.nodes = b.nodes + 1
    kids[#kids + 1] = { tag = "#text", text = s, parent = top }
  end

  local function open(tok)
    local name, attrs = tok.name, tok.attrs
    if skip then
      if name == skip.tag and not tok.self and not M.VOID[name] then skip.depth = skip.depth + 1 end
      return
    end
    if b.cut then return end
    if name == "html" then return end
    if name == "head" then ensure_head(); return end
    if name == "body" then
      ensure_body()
      if b.body then for k, v in pairs(attrs) do if b.body.attrs[k] == nil then b.body.attrs[k] = v end end end
      return
    end
    if name == "base" then if not b.base_href then b.base_href = attrs.href end; return end
    if name == "meta" then
      local cs = attrs.charset or ((attrs["http-equiv"] or ""):lower() == "content-type"
        and (attrs.content or ""):match("charset=([%w%-_]+)"))
      if cs and not b.charset then b.charset = cs:lower() end
      return
    end
    if name == "link" then return end
    if SKIP[name] then
      if name == "script" then b.scripts = b.scripts + 1 end
      if not tok.self then skip = { tag = name, depth = 1 } end
      return
    end
    if HEADTAGS[name] and not b.body then ensure_head() else ensure_body() end
    if not KNOWN[name] then name = "span" end
    autoclose(name)
    local top = stack[#stack]
    if top.tag == "html" then ensure_body(); top = stack[#stack] end
    local void = M.VOID[name]
    if not void and #stack >= b.max_depth then
      b.depth_cut = b.depth_cut + 1
      phantom[name] = (phantom[name] or 0) + 1
      return
    end
    local node = new_node(name, attrs, top)
    if not node then return end
    if name == "svg" then
      -- drawn as a box with its aria-label; the inner tags are dropped
      if not tok.self then skip = { tag = "svg", depth = 1 } end
      if opts.on_open then opts.on_open(node) end
      finish(node)
      return
    end
    if opts.on_open then opts.on_open(node) end
    if void then finish(node) else stack[#stack + 1] = node end
  end

  local function close(name)
    if skip then
      if name == skip.tag then
        skip.depth = skip.depth - 1
        if skip.depth == 0 then skip = nil end
      end
      return
    end
    if b.cut then return end
    if name == "html" or name == "body" then return end
    if name == "head" then
      for j = #stack, 2, -1 do if stack[j].tag == "head" then pop_to(j) break end end
      return
    end
    if not KNOWN[name] then name = "span" end
    if (phantom[name] or 0) > 0 then phantom[name] = phantom[name] - 1; return end
    local barrier = not END_BARRIER[name]
    for j = #stack, 2, -1 do
      local t = stack[j].tag
      if t == name then pop_to(j) return end
      if barrier and END_BARRIER[t] then return end
    end
  end

  function b.token(tok)
    local k = tok.t
    if k == "text" then text(tok.s)
    elseif k == "start" then open(tok)
    else close(tok.name) end
  end

  -- Close every open element at the end of the input.
  function b.finish()
    if #stack > 1 then pop_to(2) end
  end

  return b
end

-- Parse a whole string, or a list of chunks, into a tree. Returns the
-- builder (b.root is the tree).
function M.parse(src, opts)
  opts = opts or {}
  local b = M.builder(opts)
  local tk = M.tokenizer(b.token, opts)
  if type(src) == "table" then
    for _, c in ipairs(src) do tk:feed(c) end
  else
    tk:feed(src)
  end
  tk:finish()
  b.finish()
  if tk.cut and not b.cut then b.cut = true; b.cut_reason = "bytes" end
  b.bytes = tk.bytes
  b.dropped = tk.dropped
  return b
end

return M
