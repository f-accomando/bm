-- bmweb: the page driver. It joins the tokenizer, the tree builder, the CSS
-- rules and the layout. In streaming mode (the default) a block container
-- (div, section, main ...) is laid out as it opens and closes, and each
-- closed child is laid out and dropped at once, so the tree never holds the
-- whole page. A table, a list or a form stays in the tree until it closes.
-- With stream = false the whole tree is built first (used by the tests to
-- check that both ways give the same page).
local html = require("html")
local css = require("css")
local layout = require("layout")

local M = {}

local FLUSH = {}
for w in ("div section main article header footer nav aside center"):gmatch("%S+") do FLUSH[w] = true end

-- opts: url, width, cw, lh, stream, allow_http, and the limits of html.lua,
-- css.lua and layout.lua (max_bytes max_nodes max_depth max_token max_css
-- max_lines max_images).
function M.new(opts)
  opts = opts or {}
  local rules = css.rules(opts)
  local lay = layout.new({
    width = opts.width, cw = opts.cw, lh = opts.lh, url = opts.url, rules = rules,
    allow_http = opts.allow_http, max_lines = opts.max_lines, max_images = opts.max_images,
  })
  local stream = opts.stream ~= false
  local root_st = css.compute({ tag = "html", attrs = {}, kids = {} }, nil, nil)
  local b -- builder, set below

  local hooks = {
    max_nodes = opts.max_nodes, max_depth = opts.max_depth,
    on_style = function(text) rules:add(text) end,
  }

  if stream then
    local function deliver(p, keep_last)
      local kids = p.kids
      local n = #kids - (keep_last and 1 or 0)
      for i = 1, n do lay:node(kids[i], p.st) end
      if keep_last then p.kids = { kids[#kids] } else p.kids = {} end
    end
    local function drop_last(p)
      local kids = p.kids
      kids[#kids] = nil
    end
    hooks.on_body = function(body)
      if b.base_href then lay:set_base(b.base_href) end
      body.st = css.compute(body, rules, root_st)
      body.sm = true
      lay:open_block(body, body.st)
    end
    hooks.on_open = function(node)
      local parent = node.parent
      if parent and parent.sm == true and FLUSH[node.tag] then
        deliver(parent, true)
        local st = css.compute(node, rules, parent.st)
        if st.display == "none" or st.hidden then
          node.sm = "hide"
        else
          node.sm, node.st = true, st
          lay:open_block(node, st)
        end
      end
    end
    hooks.on_close = function(node)
      local parent = node.parent
      if node.sm == true then
        deliver(node)
        lay:close_block()
        if parent then drop_last(parent) end
      elseif node.sm == "hide" then
        if parent then drop_last(parent) end
      elseif parent and parent.sm == true then
        deliver(parent)
      end
    end
  end

  b = html.builder(hooks)
  local tk = html.tokenizer(b.token, opts)
  local P = { lay = lay, rules = rules, builder = b, tokenizer = tk }

  function P:feed(chunk)
    if b.cut or lay.full then return end
    tk:feed(chunk)
  end

  -- Finish the input and return the page (see layout.lua for its fields).
  function P:finish()
    tk:finish()
    b.finish()
    if not stream and b.body then
      if b.base_href then lay:set_base(b.base_href) end
      local st = css.compute(b.body, rules, root_st)
      lay:open_block(b.body, st)
      for _, k in ipairs(b.body.kids) do lay:node(k, st) end
      lay:close_block()
    end
    local page = lay:finish()
    page.title = b.title
    page.charset = b.charset
    page.cut = tk.cut or b.cut or lay.cut
    page.cut_reason = (tk.cut and "bytes") or b.cut_reason or (lay.cut and "lines") or nil
    page.tree = (not stream) and b.root or nil
    page.stats = {
      bytes = tk.bytes, nodes = b.nodes, depth_cut = b.depth_cut, scripts = b.scripts,
      css_bytes = rules.bytes, css_rules = rules.n, css_cut = rules.cut, css_dropped = rules.dropped,
      tags_dropped = tk.dropped, refused = lay.refused,
    }
    return page
  end

  return P
end

-- Load a whole string (cut into chunks of opts.chunk bytes if given).
function M.load(src, opts)
  local p = M.new(opts)
  local chunk = opts and opts.chunk
  if chunk then
    for i = 1, #src, chunk do p:feed(src:sub(i, i + chunk - 1)) end
  else
    p:feed(src)
  end
  return p:finish()
end

return M
