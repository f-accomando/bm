-- bmweb: page scripts. This module puts a browser-like world (window, document, elements,
-- events, timers, storage) around the JavaScript engine in js.lua. The engine knows nothing of
-- pages; this file knows nothing of the engine's inside. Rules:
--   * A script can read and change the tree of the page and run timers. It cannot reach the
--     network (fetch and XMLHttpRequest fail), the files, or the kernel. A navigation it asks for
--     (location.href = ...) is a request to the browser, which opens the address like a click.
--   * Every script run is bounded by a step budget (js.lua) and by the caps below.
--   * The tree is the one of html.lua (nodes: tag, attrs, kids, parent; text nodes: tag "#text").
-- Use: local rt = jsdom.new(api, opts); local js = rt:begin(info) gives the table page.lua
-- takes as `opts.js`; rt:loaded(page), rt:tick(now), rt:click(page, x, y), rt:take_nav(), rt:close().
local js = require("js")
local html = require("html")
local css = require("css")
local url = require("url")
local layout = require("layout")

local M = {}

M.BUDGET_LOAD = 6000000   -- steps for all the scripts of a page load
M.BUDGET_EVENT = 400000   -- steps for one handler or timer
M.MAX_STORAGE = 65536     -- bytes of localStorage and sessionStorage together
M.MAX_COOKIE = 4096
M.MAX_HTML = 262144       -- bytes one innerHTML assignment may parse
M.MAX_NODES = 20000

local R = {}
R.__index = R

local null = js.null
local function str(v) return v == nil and "" or tostring(v) end

local function lower(s) return (str(s):lower()) end

local VOID = html.VOID

---------------------------------------------------------------------------
-- Tree helpers
---------------------------------------------------------------------------
local function is_el(n) return n and n.tag and n.tag ~= "#text" end

local function reindex(parent)
  local idx, prev
  for _, k in ipairs(parent.kids) do
    if is_el(k) then
      idx = (idx or 0) + 1
      k.idx, k.prev = idx, prev
      prev = k
    end
  end
  parent.ecount, parent.last_el = idx or 0, prev
end

local function detach(node)
  local p = node.parent
  if not p then return end
  for i, k in ipairs(p.kids) do
    if k == node then table.remove(p.kids, i); break end
  end
  node.parent = nil
  reindex(p)
end

local function attach(parent, node, at)
  detach(node)
  node.parent = parent
  if at then table.insert(parent.kids, at, node) else parent.kids[#parent.kids + 1] = node end
  node.cls = nil
  reindex(parent)
end

local function text_of(node, out)
  if node.tag == "#text" then out[#out + 1] = node.text
  elseif node.tag ~= "script" and node.tag ~= "style" or true then
    for _, k in ipairs(node.kids) do text_of(k, out) end
  end
  return out
end

local function esc_text(s) return (s:gsub("[&<>]", { ["&"] = "&amp;", ["<"] = "&lt;", [">"] = "&gt;" })) end
local function esc_attr(s) return (s:gsub('[&"<>]', { ["&"] = "&amp;", ['"'] = "&quot;", ["<"] = "&lt;", [">"] = "&gt;" })) end

local function serialize(node, out, inner_only, depth)
  depth = depth or 0
  if depth > 64 then return end
  if node.tag == "#text" then out[#out + 1] = esc_text(node.text); return end
  if not inner_only then
    out[#out + 1] = "<" .. node.tag
    local keys = {}
    for k in pairs(node.attrs or {}) do keys[#keys + 1] = k end
    table.sort(keys)
    for _, k in ipairs(keys) do
      local v = node.attrs[k]
      out[#out + 1] = v == true and (" " .. k) or (" " .. k .. '="' .. esc_attr(str(v)) .. '"')
    end
    out[#out + 1] = ">"
    if VOID[node.tag] then return end
  end
  for _, k in ipairs(node.kids) do serialize(k, out, false, depth + 1) end
  if not inner_only then out[#out + 1] = "</" .. node.tag .. ">" end
end

-- Parse an HTML fragment into a list of nodes (not attached).
local function parse_fragment(src, budget)
  if #src > M.MAX_HTML then src = src:sub(1, M.MAX_HTML) end
  local b = html.parse("<body>" .. src .. "</body>", { max_nodes = budget or 5000 })
  local out = {}
  if b.body then
    for _, k in ipairs(b.body.kids) do out[#out + 1] = k end
    b.body.kids = {}
  end
  return out
end

local function clone(node, deep, parent)
  local n
  if node.tag == "#text" then
    n = { tag = "#text", text = node.text, parent = parent }
    return n
  end
  local attrs = {}
  for k, v in pairs(node.attrs or {}) do attrs[k] = v end
  n = { tag = node.tag, attrs = attrs, kids = {}, parent = parent }
  if deep then
    for _, k in ipairs(node.kids) do n.kids[#n.kids + 1] = clone(k, true, n) end
    reindex(n)
  end
  return n
end

local function count_nodes(node)
  local n = 1
  for _, k in ipairs(node.kids or {}) do n = n + count_nodes(k) end
  return n
end

---------------------------------------------------------------------------
-- The style attribute as a table (for element.style)
---------------------------------------------------------------------------
local function style_parse(text)
  local order, map = {}, {}
  for decl in (text or ""):gmatch("[^;]+") do
    local k, v = decl:match("^%s*([%w%-]+)%s*:%s*(.-)%s*$")
    if k then
      k = k:lower()
      if map[k] == nil then order[#order + 1] = k end
      map[k] = v
    end
  end
  return order, map
end

local function style_write(order, map)
  local t = {}
  for _, k in ipairs(order) do
    if map[k] ~= nil and map[k] ~= "" then t[#t + 1] = k .. ": " .. map[k] end
  end
  return table.concat(t, "; ")
end

local function camel_to_css(name)
  return (name:gsub("%u", function(c) return "-" .. c:lower() end))
end

local STYLE_PROPS = {
  "display", "visibility", "color", "backgroundColor", "background", "width", "height", "maxWidth", "minWidth",
  "maxHeight", "minHeight", "margin", "marginTop", "marginRight", "marginBottom", "marginLeft", "padding",
  "paddingTop", "paddingRight", "paddingBottom", "paddingLeft", "fontWeight", "fontStyle", "fontSize",
  "fontFamily", "lineHeight", "textAlign", "textDecoration", "textTransform", "whiteSpace", "opacity",
  "position", "top", "left", "right", "bottom", "zIndex", "overflow", "overflowX", "overflowY", "cursor",
  "border", "borderTop", "borderRight", "borderBottom", "borderLeft", "borderColor", "borderWidth",
  "borderStyle", "borderRadius", "boxShadow", "float", "clear", "flex", "flexDirection", "justifyContent",
  "alignItems", "transform", "transition", "animation", "listStyle", "listStyleType", "verticalAlign",
  "pointerEvents", "userSelect", "content", "outline", "boxSizing",
}

---------------------------------------------------------------------------
-- A page's world
---------------------------------------------------------------------------
function M.new(api, opts)
  return setmetatable({ api = api, opts = opts or {}, S = nil, dirty = false, nav = nil, log_last = nil }, R)
end

function R:take_nav()
  local n = self.nav
  self.nav = nil
  return n
end

function R:close()
  self.S = nil
  self.dirty = false
  self.nav = nil
end

local function note(self, msg)
  self.log_last = msg
end

-- Run a JS function from Lua; an error is noted, never raised.
local function safe(self, fn, this, ...)
  local rt = self.S.rt
  rt.budget = M.BUDGET_EVENT
  local ok, v = rt:call_safe(fn, this, ...)
  if not ok then note(self, "script error: " .. tostring(v)) end
  return ok, v
end

---------------------------------------------------------------------------
-- Build the world
---------------------------------------------------------------------------
local function build(self, info, tree_b, rules, lay)
  local rt = js.new({ budget = M.BUDGET_LOAD })
  local S = {
    rt = rt, info = info, url = info.url, b = tree_b, rules = rules, lay = lay,
    root = tree_b.root, body = tree_b.body, head = tree_b.head,
    wraps = setmetatable({}, { __mode = "k" }),
    listeners = setmetatable({}, { __mode = "k" }),
    props = setmetatable({}, { __mode = "k" }),   -- on<event> handlers set by property
    storage = { local_ = {}, session = {}, bytes = 0 },
    cookie = "", ready = "loading", scroll_to = nil, hist_len = 1,
    docloaded = false,
  }
  self.S = S
  S.dockey, S.winkey = {}, {}    -- the targets "document" and "window" in the event path
  rt.on_console = function(level, text)
    if level == "error" or level == "warn" then note(self, "console." .. level .. ": " .. text) end
  end
  rt.now_ms = (self.api.time() or 0) * 1000
  return S, rt
end

local function nodeof(this)
  return type(this) == "table" and this.host or nil
end

local function wrap(S, node)
  if node == nil then return null end
  local w = S.wraps[node]
  if w then return w end
  local rt = S.rt
  w = rt:new_object(node.tag == "#text" and S.textproto or S.elproto)
  w.host = node
  S.wraps[node] = w
  return w
end

local function list_of(S, nodes)
  local rt = S.rt
  local l = {}
  for i, n in ipairs(nodes) do l[i] = wrap(S, n) end
  return rt:new_array(l)
end

local function is_selector_node(n) return is_el(n) end

local function collect(node, test, out, limit)
  for _, k in ipairs(node.kids) do
    if is_el(k) then
      if test(k) then out[#out + 1] = k; if limit and #out >= limit then return true end end
      if collect(k, test, out, limit) then return true end
    end
  end
  return false
end

local function find_by_id(node, id)
  for _, k in ipairs(node.kids) do
    if is_el(k) then
      if k.attrs and k.attrs.id == id then return k end
      local f = find_by_id(k, id)
      if f then return f end
    end
  end
  return nil
end

---------------------------------------------------------------------------
-- Events
---------------------------------------------------------------------------
local function listeners_of(S, key, typ, create)
  local m = S.listeners[key]
  if not m then
    if not create then return nil end
    m = {}
    S.listeners[key] = m
  end
  local l = m[typ]
  if not l and create then l = {}; m[typ] = l end
  return l
end

local function parent_key(S, key)
  if key == S.winkey then return nil end
  if key == S.dockey then return S.winkey end
  local p = key.parent
  if p then return p end
  if key == S.root then return S.dockey end
  return nil
end

local function key_obj(S, key)
  if key == S.winkey then return S.window end
  if key == S.dockey then return S.document end
  return wrap(S, key)
end

local function make_event(S, typ, props, bubbles, cancelable)
  local rt = S.rt
  local e = rt:new_object(S.evproto)
  e.host = { prevented = false, stopped = false, immediate = false }
  rt:set(e, "type", typ)
  rt:set(e, "bubbles", bubbles ~= false)
  rt:set(e, "cancelable", cancelable ~= false)
  rt:set(e, "timeStamp", (S.rt.now_ms or 0) + 0.0)
  rt:set(e, "isTrusted", false)
  if props then for k, v in pairs(props) do rt:set(e, k, v) end end
  return e
end

-- Run the listeners of `typ` on the path from `key` up. Returns true if the default was prevented.
local function dispatch(self, key, evt)
  local S = self.S
  local rt = S.rt
  local st = evt.host
  local typ = rt:get(evt, "type")
  rt:set(evt, "target", key_obj(S, key))
  local bubbles = rt:to_boolean(rt:get(evt, "bubbles"))
  local cur, first = key, true
  local depth = 0
  while cur and depth < 80 do
    rt:set(evt, "currentTarget", key_obj(S, cur))
    local list = listeners_of(S, cur, typ)
    local handler = S.props[cur] and S.props[cur][typ]
    if handler then safe(self, handler, key_obj(S, cur), evt) end
    if list and not st.immediate then
      local copy = {}
      for i, l in ipairs(list) do copy[i] = l end
      for _, l in ipairs(copy) do
        if st.immediate then break end
        if l.once then
          for i, x in ipairs(list) do if x == l then table.remove(list, i); break end end
        end
        safe(self, l.fn, key_obj(S, cur), evt)
      end
    end
    if st.stopped or st.immediate then break end
    if not bubbles then break end
    cur = parent_key(S, cur)
    first = false
    depth = depth + 1
  end
  rt:set(evt, "currentTarget", null)
  return st.prevented
end

-- Compile an inline handler (onclick="...") once per element and type.
local function inline_handler(self, node, typ)
  local S = self.S
  local code = node.attrs and node.attrs["on" .. typ]
  if not code or code == "" then return nil end
  S.props[node] = S.props[node] or {}
  local h = S.props[node][typ]
  if h == nil then
    local rt = S.rt
    rt.budget = M.BUDGET_EVENT
    local ok, f = rt:run("(function(event){" .. code .. "\n})", "on" .. typ)
    if ok and rt:is_callable(f) then h = f else h = false end
    S.props[node][typ] = h
  end
  return h or nil
end

local function fire(self, key, typ, props, bubbles, cancelable)
  local S = self.S
  if is_el(key) then inline_handler(self, key, typ) end
  local e = make_event(S, typ, props, bubbles, cancelable)
  return dispatch(self, key, e)
end

---------------------------------------------------------------------------
-- The classes: element, document, window
---------------------------------------------------------------------------
local function setup(self, S)
  local rt = S.rt
  local G = rt.global

  local function def(obj, name, f) rt:set(obj, name, rt:native(f, name)) end
  local function acc(obj, name, getter, setter) rt:define_accessor(obj, name, getter, setter) end
  local function mutated() self.dirty = true end

  local elproto = rt:new_object()
  local textproto = rt:new_object(elproto)
  S.elproto, S.textproto = elproto, textproto
  S.evproto = rt:new_object()

  -- ---- events ----
  def(S.evproto, "preventDefault", function(this)
    local st = nodeof(this)
    if st and rt:to_boolean(rt:get(this, "cancelable")) then st.prevented = true end
  end)
  def(S.evproto, "stopPropagation", function(this) local st = nodeof(this); if st then st.stopped = true end end)
  def(S.evproto, "stopImmediatePropagation", function(this)
    local st = nodeof(this)
    if st then st.stopped, st.immediate = true, true end
  end)
  acc(S.evproto, "defaultPrevented", function(this) local st = nodeof(this); return st and st.prevented or false end)

  local function event_ctor(name, extra)
    local f = rt:native(function(_, typ, init)
      local props = {}
      local bubbles, cancelable = false, false
      if type(init) == "table" then
        bubbles = rt:to_boolean(rt:get(init, "bubbles"))
        cancelable = rt:to_boolean(rt:get(init, "cancelable"))
        for _, k in ipairs(extra or {}) do
          local v = rt:get(init, k)
          if v ~= nil then props[k] = v end
        end
      end
      return make_event(S, str(typ), props, bubbles, cancelable)
    end, name)
    rt:set(G, name, f)
  end
  event_ctor("Event")
  event_ctor("CustomEvent", { "detail" })
  event_ctor("UIEvent", { "detail" })
  event_ctor("MouseEvent", { "clientX", "clientY", "button", "buttons", "ctrlKey", "shiftKey", "altKey" })
  event_ctor("KeyboardEvent", { "key", "code", "ctrlKey", "shiftKey", "altKey", "keyCode" })
  event_ctor("InputEvent", { "data", "inputType" })
  event_ctor("FocusEvent")

  local function add_listener(key)
    return function(_, typ, fn, opts)
      if not rt:is_callable(fn) then return end
      typ = str(typ)
      local once = type(opts) == "table" and rt:to_boolean(rt:get(opts, "once"))
      local list = listeners_of(S, key, typ, true)
      for _, l in ipairs(list) do if l.fn == fn then return end end
      if #list < 100 then list[#list + 1] = { fn = fn, once = once } end
    end
  end
  local function remove_listener(key)
    return function(_, typ, fn)
      local list = listeners_of(S, key, str(typ))
      if not list then return end
      for i, l in ipairs(list) do if l.fn == fn then table.remove(list, i); return end end
    end
  end
  local function dispatch_event(key)
    return function(_, evt)
      if type(evt) ~= "table" or type(evt.host) ~= "table" then return false end
      return not dispatch(self, key, evt)
    end
  end
  local function event_target(obj, key)
    def(obj, "addEventListener", add_listener(key))
    def(obj, "removeEventListener", remove_listener(key))
    def(obj, "dispatchEvent", dispatch_event(key))
  end

  -- on<event> properties
  local ON = { "click", "change", "input", "submit", "load", "error", "keydown", "keyup", "keypress", "mouseover",
    "mouseout", "mousedown", "mouseup", "mousemove", "focus", "blur", "dblclick", "contextmenu", "scroll",
    "resize", "touchstart", "touchend", "select", "reset", "unload", "hashchange", "popstate", "beforeunload" }
  local function on_props(obj, keyof)
    for _, typ in ipairs(ON) do
      acc(obj, "on" .. typ, function(this)
        local key = keyof(this)
        local h = key and S.props[key] and S.props[key][typ]
        return h or null
      end, function(this, v)
        local key = keyof(this)
        if not key then return end
        S.props[key] = S.props[key] or {}
        S.props[key][typ] = rt:is_callable(v) and v or false
      end)
    end
  end

  -- ---- nodes ----
  def(elproto, "addEventListener", function(this, ...) return add_listener(nodeof(this))(this, ...) end)
  def(elproto, "removeEventListener", function(this, ...) return remove_listener(nodeof(this))(this, ...) end)
  def(elproto, "dispatchEvent", function(this, evt) return dispatch_event(nodeof(this))(this, evt) end)
  on_props(elproto, nodeof)

  local function tag_name(n) return n.tag == "#text" and "#text" or n.tag:upper() end
  acc(elproto, "nodeType", function(this) local n = nodeof(this); return n.tag == "#text" and 3.0 or 1.0 end)
  acc(elproto, "nodeName", function(this) return tag_name(nodeof(this)) end)
  acc(elproto, "tagName", function(this) return tag_name(nodeof(this)) end)
  acc(elproto, "localName", function(this) return nodeof(this).tag end)
  acc(elproto, "ownerDocument", function() return S.document end)
  acc(elproto, "isConnected", function(this)
    local n = nodeof(this)
    while n.parent do n = n.parent end
    return n == S.root
  end)

  local function attrs_of(n) n.attrs = n.attrs or {}; return n.attrs end

  def(elproto, "getAttribute", function(this, name)
    local n = nodeof(this)
    local v = n.attrs and n.attrs[lower(name)]
    if v == nil then return null end
    if v == true then return "" end
    return str(v)
  end)
  def(elproto, "setAttribute", function(this, name, value)
    local n = nodeof(this)
    name = lower(name)
    attrs_of(n)[name] = str(value)
    if name == "class" then n.cls = nil end
    mutated()
  end)
  def(elproto, "removeAttribute", function(this, name)
    local n = nodeof(this)
    name = lower(name)
    if n.attrs then n.attrs[name] = nil end
    if name == "class" then n.cls = nil end
    mutated()
  end)
  def(elproto, "hasAttribute", function(this, name)
    local n = nodeof(this)
    return n.attrs ~= nil and n.attrs[lower(name)] ~= nil
  end)
  def(elproto, "toggleAttribute", function(this, name, force)
    local n = nodeof(this)
    name = lower(name)
    local has = attrs_of(n)[name] ~= nil
    local want = force == nil and not has or rt:to_boolean(force)
    if want then n.attrs[name] = n.attrs[name] or "" else n.attrs[name] = nil end
    mutated()
    return want
  end)
  def(elproto, "getAttributeNames", function(this)
    local l = {}
    for k in pairs(nodeof(this).attrs or {}) do l[#l + 1] = k end
    table.sort(l)
    return rt:new_array(l)
  end)

  -- reflected attributes
  local function reflect(prop, attr)
    attr = attr or prop:lower()
    acc(elproto, prop, function(this)
      local v = nodeof(this).attrs and nodeof(this).attrs[attr]
      if v == nil or v == true then return v == true and "" or "" end
      return str(v)
    end, function(this, v)
      attrs_of(nodeof(this))[attr] = str(v)
      mutated()
    end)
  end
  for _, p in ipairs({ "id", "title", "lang", "dir", "name", "type", "src", "alt", "href", "rel", "target",
    "placeholder", "action", "method", "htmlFor", "role", "tabIndex", "width", "height", "accessKey" }) do
    reflect(p, p == "htmlFor" and "for" or (p == "tabIndex" and "tabindex" or (p == "accessKey" and "accesskey" or nil)))
  end
  acc(elproto, "className", function(this) return str(nodeof(this).attrs and nodeof(this).attrs.class) end,
    function(this, v) attrs_of(nodeof(this)).class = str(v); nodeof(this).cls = nil; mutated() end)
  local function bool_prop(prop, attr)
    attr = attr or prop:lower()
    acc(elproto, prop, function(this) local a = nodeof(this).attrs; return a ~= nil and a[attr] ~= nil end,
      function(this, v)
        local n = nodeof(this)
        attrs_of(n)[attr] = rt:to_boolean(v) and "" or nil
        mutated()
      end)
  end
  for _, p in ipairs({ "hidden", "disabled", "readOnly", "required", "selected", "open", "autofocus", "multiple" }) do
    bool_prop(p, p == "readOnly" and "readonly" or nil)
  end

  -- values of form controls: shared with the browser through S.host
  local function control_value(n)
    local t = n.tag
    if t == "textarea" then return table.concat(text_of(n, {})) end
    if t == "select" then
      local sel, first
      local opts = {}
      collect(n, function(k) return k.tag == "option" end, opts)
      for _, o in ipairs(opts) do
        first = first or o
        if o.attrs and o.attrs.selected ~= nil then sel = o end
      end
      sel = sel or first
      if not sel then return "" end
      return str(sel.attrs and sel.attrs.value or table.concat(text_of(sel, {})):gsub("^%s+", ""):gsub("%s+$", ""))
    end
    local v = n.attrs and n.attrs.value
    if v == nil then return "" end
    return str(v)
  end
  acc(elproto, "value", function(this) return control_value(nodeof(this)) end, function(this, v)
    local n = nodeof(this)
    v = str(v)
    if n.tag == "textarea" then
      n.kids = { { tag = "#text", text = v, parent = n } }
    elseif n.tag == "select" then
      local opts = {}
      collect(n, function(k) return k.tag == "option" end, opts)
      for _, o in ipairs(opts) do
        local ov = o.attrs and o.attrs.value or table.concat(text_of(o, {}))
        attrs_of(o).selected = (ov == v) and "" or nil
      end
    else
      attrs_of(n).value = v
    end
    if S.host and S.host.set_value then S.host.set_value(n, v) end
    mutated()
  end)
  acc(elproto, "checked", function(this) local a = nodeof(this).attrs; return a ~= nil and a.checked ~= nil end,
    function(this, v)
      local n = nodeof(this)
      attrs_of(n).checked = rt:to_boolean(v) and "" or nil
      mutated()
    end)
  acc(elproto, "selectedIndex", function(this)
    local opts = {}
    collect(nodeof(this), function(k) return k.tag == "option" end, opts)
    for i, o in ipairs(opts) do if o.attrs and o.attrs.selected ~= nil then return (i - 1) + 0.0 end end
    return 0.0
  end)

  -- text and html
  acc(elproto, "textContent", function(this)
    local n = nodeof(this)
    if n.tag == "#text" then return n.text end
    return table.concat(text_of(n, {}))
  end, function(this, v)
    local n = nodeof(this)
    v = str(v)
    if n.tag == "#text" then n.text = v
    else
      for _, k in ipairs(n.kids) do k.parent = nil end
      n.kids = v ~= "" and { { tag = "#text", text = v, parent = n } } or {}
      reindex(n)
    end
    mutated()
  end)
  acc(elproto, "innerText", function(this)
    local n = nodeof(this)
    if n.tag == "#text" then return n.text end
    return table.concat(text_of(n, {}))
  end, function(this, v)
    local n = nodeof(this)
    for _, k in ipairs(n.kids) do k.parent = nil end
    v = str(v)
    n.kids = v ~= "" and { { tag = "#text", text = v, parent = n } } or {}
    reindex(n)
    mutated()
  end)
  acc(elproto, "data", function(this) return nodeof(this).text or "" end, function(this, v)
    local n = nodeof(this)
    if n.tag == "#text" then n.text = str(v); mutated() end
  end)
  acc(elproto, "nodeValue", function(this) local n = nodeof(this); return n.tag == "#text" and n.text or null end,
    function(this, v) local n = nodeof(this); if n.tag == "#text" then n.text = str(v); mutated() end end)
  acc(elproto, "innerHTML", function(this)
    local out = {}
    serialize(nodeof(this), out, true)
    return table.concat(out)
  end, function(this, v)
    local n = nodeof(this)
    local kids = parse_fragment(str(v))
    for _, k in ipairs(n.kids) do k.parent = nil end
    n.kids = {}
    local total = 0
    for _, k in ipairs(kids) do
      total = total + count_nodes(k)
      if total > M.MAX_NODES then break end
      k.parent = n
      n.kids[#n.kids + 1] = k
    end
    reindex(n)
    mutated()
  end)
  acc(elproto, "outerHTML", function(this)
    local out = {}
    serialize(nodeof(this), out, false)
    return table.concat(out)
  end)

  -- tree walking
  local function sib(n, d)
    local p = n.parent
    if not p then return nil end
    for i, k in ipairs(p.kids) do if k == n then return p.kids[i + d] end end
  end
  local function el_sib(n, d)
    local p = n.parent
    if not p then return nil end
    for i, k in ipairs(p.kids) do
      if k == n then
        local j = i + d
        while p.kids[j] do
          if is_el(p.kids[j]) then return p.kids[j] end
          j = j + d
        end
        return nil
      end
    end
  end
  local function elements(n)
    local l = {}
    for _, k in ipairs(n.kids) do if is_el(k) then l[#l + 1] = k end end
    return l
  end
  acc(elproto, "parentNode", function(this) return wrap(S, nodeof(this).parent) end)
  acc(elproto, "parentElement", function(this)
    local p = nodeof(this).parent
    return is_el(p) and wrap(S, p) or null
  end)
  acc(elproto, "childNodes", function(this) return list_of(S, nodeof(this).kids or {}) end)
  acc(elproto, "children", function(this) return list_of(S, elements(nodeof(this))) end)
  acc(elproto, "childElementCount", function(this) return #elements(nodeof(this)) + 0.0 end)
  acc(elproto, "firstChild", function(this) return wrap(S, (nodeof(this).kids or {})[1]) end)
  acc(elproto, "lastChild", function(this) local k = nodeof(this).kids or {}; return wrap(S, k[#k]) end)
  acc(elproto, "firstElementChild", function(this) return wrap(S, elements(nodeof(this))[1]) end)
  acc(elproto, "lastElementChild", function(this) local e = elements(nodeof(this)); return wrap(S, e[#e]) end)
  acc(elproto, "nextSibling", function(this) return wrap(S, sib(nodeof(this), 1)) end)
  acc(elproto, "previousSibling", function(this) return wrap(S, sib(nodeof(this), -1)) end)
  acc(elproto, "nextElementSibling", function(this) return wrap(S, el_sib(nodeof(this), 1)) end)
  acc(elproto, "previousElementSibling", function(this) return wrap(S, el_sib(nodeof(this), -1)) end)
  def(elproto, "hasChildNodes", function(this) return #(nodeof(this).kids or {}) > 0 end)
  def(elproto, "contains", function(this, other)
    local o = nodeof(other)
    local n = nodeof(this)
    while o do
      if o == n then return true end
      o = o.parent
    end
    return false
  end)

  -- changing the tree
  local function as_node(v)
    local n = nodeof(v)
    if n then return n end
    if v ~= nil and v ~= null then return { tag = "#text", text = rt:to_string(v) } end
  end
  local function check_room(parent, node)
    local total = count_nodes(node)
    if total > M.MAX_NODES then return false end
    return true
  end
  local function insert_at(parent, node, at)
    if node.tag == "#frag" then
      local kids = {}
      for i, k in ipairs(node.kids) do kids[i] = k end
      for i, k in ipairs(kids) do attach(parent, k, at and (at + i - 1) or nil) end
      node.kids = {}
      return
    end
    if not check_room(parent, node) then return end
    -- a node cannot go inside itself
    local p = parent
    while p do if p == node then return end p = p.parent end
    attach(parent, node, at)
  end
  local function index_of(parent, node)
    for i, k in ipairs(parent.kids) do if k == node then return i end end
  end
  def(elproto, "appendChild", function(this, child)
    local n, c = nodeof(this), as_node(child)
    if not c then return child end
    insert_at(n, c)
    mutated()
    return child
  end)
  def(elproto, "append", function(this, ...)
    local n = nodeof(this)
    for i = 1, select("#", ...) do
      local c = as_node((select(i, ...)))
      if c then insert_at(n, c) end
    end
    mutated()
  end)
  def(elproto, "prepend", function(this, ...)
    local n = nodeof(this)
    local at = 1
    for i = 1, select("#", ...) do
      local c = as_node((select(i, ...)))
      if c then insert_at(n, c, at); at = at + 1 end
    end
    mutated()
  end)
  def(elproto, "insertBefore", function(this, child, ref)
    local n, c = nodeof(this), as_node(child)
    if not c then return child end
    local r = nodeof(ref)
    local at = r and index_of(n, r) or nil
    insert_at(n, c, at)
    mutated()
    return child
  end)
  def(elproto, "removeChild", function(this, child)
    local c = nodeof(child)
    if c and c.parent == nodeof(this) then detach(c); mutated() end
    return child
  end)
  def(elproto, "replaceChild", function(this, new, old)
    local n, c, o = nodeof(this), as_node(new), nodeof(old)
    if c and o and o.parent == n then
      local at = index_of(n, o)
      detach(o)
      insert_at(n, c, at)
      mutated()
    end
    return old
  end)
  def(elproto, "remove", function(this)
    local n = nodeof(this)
    if n.parent then detach(n); mutated() end
  end)
  local function around(after)
    return function(this, ...)
      local n = nodeof(this)
      local p = n.parent
      if not p then return end
      local at = index_of(p, n) + (after and 1 or 0)
      for i = 1, select("#", ...) do
        local c = as_node((select(i, ...)))
        if c then
          local was = c.parent == p and index_of(p, c) or nil
          insert_at(p, c, at)
          at = at + 1
        end
      end
      mutated()
    end
  end
  def(elproto, "before", around(false))
  def(elproto, "after", around(true))
  def(elproto, "replaceWith", function(this, new)
    local n = nodeof(this)
    local p = n.parent
    local c = as_node(new)
    if p and c then
      local at = index_of(p, n)
      detach(n)
      insert_at(p, c, at)
      mutated()
    end
  end)
  def(elproto, "cloneNode", function(this, deep)
    local c = clone(nodeof(this), rt:to_boolean(deep))
    return wrap(S, c)
  end)
  def(elproto, "normalize", function() end)

  -- queries
  local function query_all(root, sel, limit)
    local chains = css.selector(str(sel))
    local out = {}
    if not chains then return out, false end
    collect(root, function(k) return css.matches(chains, k) end, out, limit)
    return out, true
  end
  def(elproto, "querySelector", function(this, sel)
    local l, ok = query_all(nodeof(this), sel, 1)
    if not ok then rt:throw("SyntaxError", "unsupported selector: " .. str(sel)) end
    return wrap(S, l[1])
  end)
  def(elproto, "querySelectorAll", function(this, sel)
    local l, ok = query_all(nodeof(this), sel, 2000)
    if not ok then rt:throw("SyntaxError", "unsupported selector: " .. str(sel)) end
    return list_of(S, l)
  end)
  def(elproto, "matches", function(this, sel)
    local chains = css.selector(str(sel))
    return chains ~= nil and css.matches(chains, nodeof(this))
  end)
  def(elproto, "closest", function(this, sel)
    local chains = css.selector(str(sel))
    local n = nodeof(this)
    while chains and n and is_el(n) do
      if css.matches(chains, n) then return wrap(S, n) end
      n = n.parent
    end
    return null
  end)
  def(elproto, "getElementsByTagName", function(this, tag)
    tag = lower(tag)
    local l = {}
    collect(nodeof(this), function(k) return tag == "*" or k.tag == tag end, l, 2000)
    return list_of(S, l)
  end)
  def(elproto, "getElementsByClassName", function(this, cls)
    local want = {}
    for c in str(cls):gmatch("%S+") do want[#want + 1] = c end
    local l = {}
    collect(nodeof(this), function(k)
      local have = k.attrs and k.attrs.class
      if not have then return false end
      local set = {}
      for c in have:gmatch("%S+") do set[c] = true end
      for _, w in ipairs(want) do if not set[w] then return false end end
      return #want > 0
    end, l, 2000)
    return list_of(S, l)
  end)

  -- classList
  local classproto = rt:new_object()
  local function classes(n)
    local list, set = {}, {}
    for c in str(n.attrs and n.attrs.class):gmatch("%S+") do
      if not set[c] then set[c] = true; list[#list + 1] = c end
    end
    return list, set
  end
  local function write_classes(n, list)
    attrs_of(n).class = table.concat(list, " ")
    n.cls = nil
    mutated()
  end
  def(classproto, "contains", function(this, c) local _, set = classes(nodeof(this)); return set[str(c)] == true end)
  def(classproto, "add", function(this, ...)
    local n = nodeof(this)
    local list, set = classes(n)
    for i = 1, select("#", ...) do
      local c = str((select(i, ...)))
      if c ~= "" and not set[c] then set[c] = true; list[#list + 1] = c end
    end
    write_classes(n, list)
  end)
  def(classproto, "remove", function(this, ...)
    local n = nodeof(this)
    local list = classes(n)
    local drop = {}
    for i = 1, select("#", ...) do drop[str((select(i, ...)))] = true end
    local keep = {}
    for _, c in ipairs(list) do if not drop[c] then keep[#keep + 1] = c end end
    write_classes(n, keep)
  end)
  def(classproto, "toggle", function(this, c, force)
    local n = nodeof(this)
    c = str(c)
    local list, set = classes(n)
    local want
    if force == nil then want = not set[c] else want = rt:to_boolean(force) end
    if want and not set[c] then list[#list + 1] = c
    elseif not want and set[c] then
      local keep = {}
      for _, x in ipairs(list) do if x ~= c then keep[#keep + 1] = x end end
      list = keep
    end
    write_classes(n, list)
    return want
  end)
  def(classproto, "replace", function(this, old, new)
    local n = nodeof(this)
    local list = classes(n)
    old, new = str(old), str(new)
    local found = false
    for i, c in ipairs(list) do if c == old then list[i] = new; found = true end end
    if found then write_classes(n, list) end
    return found
  end)
  def(classproto, "item", function(this, i) return (classes(nodeof(this)))[(tonumber(i) or 0) + 1] or null end)
  def(classproto, "toString", function(this) return str(nodeof(this).attrs and nodeof(this).attrs.class) end)
  acc(classproto, "length", function(this) return #classes(nodeof(this)) + 0.0 end)
  acc(classproto, "value", function(this) return str(nodeof(this).attrs and nodeof(this).attrs.class) end)
  acc(elproto, "classList", function(this)
    local o = rt:new_object(classproto)
    o.host = nodeof(this)
    return o
  end)

  -- dataset: a copy of the data-* attributes (reads only)
  acc(elproto, "dataset", function(this)
    local o = rt:new_object()
    for k, v in pairs(nodeof(this).attrs or {}) do
      if k:sub(1, 5) == "data-" then
        local name = k:sub(6):gsub("%-(%a)", function(c) return c:upper() end)
        rt:set(o, name, v == true and "" or str(v))
      end
    end
    return o
  end)

  -- style
  local styleproto = rt:new_object()
  local function style_of(this) return nodeof(this) end
  local function style_get(n, cssname)
    local _, map = style_parse(n.attrs and n.attrs.style)
    return map[cssname] or ""
  end
  local function style_set(n, cssname, value)
    local order, map = style_parse(n.attrs and n.attrs.style)
    if map[cssname] == nil then order[#order + 1] = cssname end
    map[cssname] = value ~= nil and str(value) or ""
    attrs_of(n).style = style_write(order, map)
    mutated()
  end
  for _, p in ipairs(STYLE_PROPS) do
    local cssname = camel_to_css(p)
    acc(styleproto, p, function(this) return style_get(style_of(this), cssname) end,
      function(this, v) style_set(style_of(this), cssname, v) end)
  end
  acc(styleproto, "cssText", function(this) return str(style_of(this).attrs and style_of(this).attrs.style) end,
    function(this, v) attrs_of(style_of(this)).style = str(v); mutated() end)
  def(styleproto, "setProperty", function(this, name, v) style_set(style_of(this), lower(name), v) end)
  def(styleproto, "getPropertyValue", function(this, name) return style_get(style_of(this), lower(name)) end)
  def(styleproto, "removeProperty", function(this, name)
    local n = style_of(this)
    local old = style_get(n, lower(name))
    style_set(n, lower(name), "")
    return old
  end)
  acc(elproto, "style", function(this)
    local o = rt:new_object(styleproto)
    o.host = nodeof(this)
    return o
  end)

  -- layout numbers: the page is laid out after the scripts, so the sizes are rough
  local function rect(this)
    local o = rt:new_object()
    for _, k in ipairs({ "x", "y", "left", "top", "right", "bottom", "width", "height" }) do rt:set(o, k, 0.0) end
    local n = nodeof(this)
    rt:set(o, "width", (n and n.tag == "body" and S.info.width or 0) + 0.0)
    return o
  end
  def(elproto, "getBoundingClientRect", rect)
  def(elproto, "getClientRects", function() return rt:new_array({}) end)
  for _, p in ipairs({ "offsetWidth", "offsetHeight", "clientWidth", "clientHeight", "scrollWidth", "scrollHeight",
    "offsetTop", "offsetLeft", "clientTop", "clientLeft" }) do
    acc(elproto, p, function() return 0.0 end)
  end
  acc(elproto, "scrollTop", function() return 0.0 end, function(this, v)
    local n = nodeof(this)
    if n and (n.tag == "body" or n.tag == "html") then S.scroll_to = tonumber(v) or 0 end
  end)
  acc(elproto, "scrollLeft", function() return 0.0 end, function() end)
  acc(elproto, "offsetParent", function() return null end)
  for _, m in ipairs({ "focus", "blur", "scrollIntoView", "scroll", "scrollTo", "select", "showModal", "close", "setSelectionRange" }) do
    def(elproto, m, function(this)
      if m == "scrollIntoView" then S.scroll_node = nodeof(this) end
      if m == "showModal" then attrs_of(nodeof(this)).open = ""; mutated() end
      if m == "close" then attrs_of(nodeof(this)).open = nil; mutated() end
    end)
  end
  def(elproto, "click", function(this)
    local n = nodeof(this)
    local prevented = fire(self, n, "click", nil, true, true)
    if not prevented and n.tag == "a" and n.attrs and n.attrs.href then
      local r = url.resolve(S.url, n.attrs.href, {})
      if r then self.nav = r end
    end
  end)
  def(elproto, "submit", function(this)
    local n = nodeof(this)
    if n.tag == "form" and S.host and S.host.submit then S.host.submit(n) end
  end)
  def(elproto, "requestSubmit", function(this)
    local n = nodeof(this)
    if n.tag == "form" and not fire(self, n, "submit", nil, true, true) and S.host and S.host.submit then S.host.submit(n) end
  end)
  def(elproto, "reset", function() end)
  def(elproto, "animate", function() return rt:new_object() end)
  def(elproto, "attachShadow", function() return rt:new_object() end)
  def(elproto, "insertAdjacentHTML", function(this, where, markup)
    local n = nodeof(this)
    where = lower(where)
    local kids = parse_fragment(str(markup))
    local parent, at = n, nil
    if where == "beforebegin" or where == "afterend" then
      parent = n.parent
      if not parent then return end
      at = index_of(parent, n) + (where == "afterend" and 1 or 0)
    elseif where == "afterbegin" then at = 1 end
    for i, k in ipairs(kids) do
      insert_at(parent, k, at and (at + i - 1) or nil)
    end
    mutated()
  end)
  def(elproto, "insertAdjacentElement", function(this, where, el)
    local n, c = nodeof(this), as_node(el)
    if not c then return el end
    where = lower(where)
    if where == "beforebegin" and n.parent then insert_at(n.parent, c, index_of(n.parent, n))
    elseif where == "afterend" and n.parent then insert_at(n.parent, c, index_of(n.parent, n) + 1)
    elseif where == "afterbegin" then insert_at(n, c, 1)
    else insert_at(n, c) end
    mutated()
    return el
  end)
  def(elproto, "insertAdjacentText", function(this, where, text)
    local n = nodeof(this)
    local c = { tag = "#text", text = str(text) }
    where = lower(where)
    if where == "beforebegin" and n.parent then insert_at(n.parent, c, index_of(n.parent, n))
    elseif where == "afterend" and n.parent then insert_at(n.parent, c, index_of(n.parent, n) + 1)
    elseif where == "afterbegin" then insert_at(n, c, 1)
    else insert_at(n, c) end
    mutated()
  end)

  -- ---- document ----
  local docproto = rt:new_object()
  local document = rt:new_object(docproto)
  document.host = S.dockey
  S.document = document
  def(docproto, "addEventListener", add_listener(S.dockey))
  def(docproto, "removeEventListener", remove_listener(S.dockey))
  def(docproto, "dispatchEvent", dispatch_event(S.dockey))
  on_props(docproto, function() return S.dockey end)

  local function new_element(tag)
    tag = lower(tag)
    if not tag:match("^[%a][%w%-]*$") then rt:throw("InvalidCharacterError", "bad tag name") end
    return { tag = tag, attrs = {}, kids = {} }
  end
  def(docproto, "createElement", function(_, tag) return wrap(S, new_element(tag)) end)
  def(docproto, "createElementNS", function(_, _, tag) return wrap(S, new_element(tag)) end)
  def(docproto, "createTextNode", function(_, text) return wrap(S, { tag = "#text", text = str(text) }) end)
  def(docproto, "createComment", function() return wrap(S, { tag = "#text", text = "" }) end)
  def(docproto, "createDocumentFragment", function() return wrap(S, { tag = "#frag", attrs = {}, kids = {} }) end)
  def(docproto, "getElementById", function(_, id)
    return wrap(S, find_by_id(S.root, str(id)))
  end)
  def(docproto, "querySelector", function(_, sel)
    local l, ok = query_all(S.root, sel, 1)
    if not ok then rt:throw("SyntaxError", "unsupported selector: " .. str(sel)) end
    return wrap(S, l[1])
  end)
  def(docproto, "querySelectorAll", function(_, sel)
    local l, ok = query_all(S.root, sel, 2000)
    if not ok then rt:throw("SyntaxError", "unsupported selector: " .. str(sel)) end
    return list_of(S, l)
  end)
  def(docproto, "getElementsByTagName", function(_, tag) return rt:call(rt:get(elproto, "getElementsByTagName"), wrap(S, S.root), tag) end)
  def(docproto, "getElementsByClassName", function(_, c) return rt:call(rt:get(elproto, "getElementsByClassName"), wrap(S, S.root), c) end)
  def(docproto, "getElementsByName", function(_, name)
    local l = {}
    collect(S.root, function(k) return k.attrs and k.attrs.name == str(name) end, l, 500)
    return list_of(S, l)
  end)
  def(docproto, "createEvent", function(_, kind) return make_event(S, "", nil, false, false) end)
  def(docproto, "hasFocus", function() return true end)
  def(docproto, "write", function(_, ...)
    local parts = {}
    for i = 1, select("#", ...) do parts[#parts + 1] = str((select(i, ...))) end
    local kids = parse_fragment(table.concat(parts))
    local body = S.body or S.root
    for _, k in ipairs(kids) do insert_at(body, k) end
    mutated()
  end)
  def(docproto, "writeln", function(_, ...) rt:call(rt:get(docproto, "write"), document, ...) end)
  def(docproto, "open", function() end)
  def(docproto, "close", function() end)
  acc(docproto, "documentElement", function() return wrap(S, S.root) end)
  acc(docproto, "body", function() return wrap(S, S.body) end)
  acc(docproto, "head", function() return wrap(S, S.head) end)
  acc(docproto, "nodeType", function() return 9.0 end)
  acc(docproto, "readyState", function() return S.ready end)
  acc(docproto, "title", function() return S.b.title or "" end, function(_, v) S.b.title = str(v) end)
  acc(docproto, "URL", function() return S.url end)
  acc(docproto, "documentURI", function() return S.url end)
  acc(docproto, "baseURI", function() return S.url end)
  acc(docproto, "referrer", function() return "" end)
  acc(docproto, "domain", function() return (url.parse(S.url) or {}).host or "" end)
  acc(docproto, "characterSet", function() return "UTF-8" end)
  acc(docproto, "contentType", function() return "text/html" end)
  acc(docproto, "compatMode", function() return "CSS1Compat" end)
  acc(docproto, "hidden", function() return false end)
  acc(docproto, "visibilityState", function() return "visible" end)
  acc(docproto, "activeElement", function() return wrap(S, S.body) end)
  acc(docproto, "currentScript", function() return S.current_script and wrap(S, S.current_script) or null end)
  acc(docproto, "scripts", function()
    local l = {}
    collect(S.root, function(k) return k.tag == "script" end, l, 500)
    return list_of(S, l)
  end)
  acc(docproto, "forms", function()
    local l = {}
    collect(S.root, function(k) return k.tag == "form" end, l, 500)
    return list_of(S, l)
  end)
  acc(docproto, "images", function()
    local l = {}
    collect(S.root, function(k) return k.tag == "img" end, l, 2000)
    return list_of(S, l)
  end)
  acc(docproto, "links", function()
    local l = {}
    collect(S.root, function(k) return k.tag == "a" and k.attrs and k.attrs.href end, l, 2000)
    return list_of(S, l)
  end)
  acc(docproto, "cookie", function() return S.cookie end, function(_, v)
    -- only name=value is kept; attributes after ";" are ignored; the jar is per page view
    local pair = str(v):match("^%s*([^;]*)")
    local name = pair and pair:match("^([^=]*)=")
    if not name then return end
    local jar = {}
    local order = {}
    for kv in S.cookie:gmatch("[^;]+") do
      local k, val = kv:match("^%s*([^=]*)=(.*)$")
      if k then jar[k] = val; order[#order + 1] = k end
    end
    if jar[name] == nil then order[#order + 1] = name end
    jar[name] = pair:match("=(.*)$")
    local out = {}
    for _, k in ipairs(order) do if jar[k] ~= nil then out[#out + 1] = k .. "=" .. jar[k] end end
    S.cookie = table.concat(out, "; "):sub(1, M.MAX_COOKIE)
  end)

  -- ---- window ----
  local window = G
  S.window = window
  window.host = S.winkey
  rt:set(G, "window", G)
  rt:set(G, "self", G)
  rt:set(G, "top", G)
  rt:set(G, "parent", G)
  rt:set(G, "frames", G)
  rt:set(G, "globalThis", G)
  rt:set(G, "document", document)
  def(G, "addEventListener", add_listener(S.winkey))
  def(G, "removeEventListener", remove_listener(S.winkey))
  def(G, "dispatchEvent", dispatch_event(S.winkey))
  on_props(G, function() return S.winkey end)

  -- location
  local loc = rt:new_object()
  local function loc_parts()
    return url.parse(S.url) or {}
  end
  local function request_nav(u)
    local r = url.resolve(S.url, str(u), {})
    if r and r:match("^https://") then
      if url.strip_fragment(r) ~= url.strip_fragment(S.url) then self.nav = r end
    end
  end
  acc(loc, "href", function() return S.url end, function(_, v) request_nav(v) end)
  acc(loc, "protocol", function() return (loc_parts().scheme or "https") .. ":" end)
  acc(loc, "hostname", function() return loc_parts().host or "" end)
  acc(loc, "host", function() local p = loc_parts(); return (p.host or "") .. (p.port and (":" .. p.port) or "") end)
  acc(loc, "port", function() return str(loc_parts().port) end)
  acc(loc, "pathname", function() local p = loc_parts().path; return (p == nil or p == "") and "/" or p end)
  acc(loc, "search", function() local q = loc_parts().query; return q and q ~= "" and ("?" .. q) or "" end)
  acc(loc, "hash", function() local f = S.url:match("#.*$"); return f or "" end)
  acc(loc, "origin", function() local p = loc_parts(); return (p.scheme or "https") .. "://" .. (p.host or "") .. (p.port and (":" .. p.port) or "") end)
  def(loc, "assign", function(_, u) request_nav(u) end)
  def(loc, "replace", function(_, u) request_nav(u) end)
  def(loc, "reload", function() self.nav = S.url end)
  def(loc, "toString", function() return S.url end)
  rt:set(G, "location", loc)
  rt:set(document, "location", loc)

  -- navigator, screen, history, performance
  local nav = rt:new_object()
  rt:set(nav, "userAgent", "bm-browser/1.0 (+https://github.com/f-accomando/bm)")
  rt:set(nav, "language", "en")
  rt:set(nav, "languages", rt:new_array({ "en" }))
  rt:set(nav, "platform", "bm")
  rt:set(nav, "onLine", true)
  rt:set(nav, "cookieEnabled", true)
  rt:set(nav, "hardwareConcurrency", 1.0)
  rt:set(nav, "maxTouchPoints", 0.0)
  rt:set(nav, "doNotTrack", "1")
  def(nav, "sendBeacon", function() return false end)
  rt:set(G, "navigator", nav)
  local scr = rt:new_object()
  rt:set(scr, "width", 640.0); rt:set(scr, "height", 360.0)
  rt:set(scr, "availWidth", 640.0); rt:set(scr, "availHeight", 360.0); rt:set(scr, "colorDepth", 24.0)
  rt:set(G, "screen", scr)
  rt:set(G, "innerWidth", (S.info.width or 628) + 0.0)
  rt:set(G, "innerHeight", 314.0)
  rt:set(G, "outerWidth", 640.0); rt:set(G, "outerHeight", 360.0)
  rt:set(G, "devicePixelRatio", 1.0)
  rt:set(G, "scrollX", 0.0); rt:set(G, "scrollY", 0.0)
  rt:set(G, "pageXOffset", 0.0); rt:set(G, "pageYOffset", 0.0)
  rt:set(G, "name", "")
  local hist = rt:new_object()
  rt:set(hist, "state", null)
  acc(hist, "length", function() return S.hist_len + 0.0 end)
  def(hist, "pushState", function(_, state) rt:set(hist, "state", state == nil and null or state); S.hist_len = S.hist_len + 1 end)
  def(hist, "replaceState", function(_, state) rt:set(hist, "state", state == nil and null or state) end)
  for _, m in ipairs({ "back", "forward", "go" }) do def(hist, m, function() end) end
  rt:set(G, "history", hist)
  local perf = rt:new_object()
  def(perf, "now", function() return (self.api.time() - (S.t0 or 0)) * 1000 end)
  def(perf, "mark", function() end)
  def(perf, "measure", function() end)
  def(perf, "getEntriesByType", function() return rt:new_array({}) end)
  rt:set(G, "performance", perf)

  -- storage (in memory: nothing is written)
  local function storage(which)
    local o = rt:new_object()
    local data, keys = S.storage[which], nil
    local order = {}
    def(o, "getItem", function(_, k) local v = data[str(k)]; if v == nil then return null end return v end)
    def(o, "setItem", function(_, k, v)
      k, v = str(k), str(v)
      local old = data[k]
      local delta = #k + #v - (old and (#k + #old) or 0)
      if S.storage.bytes + delta > M.MAX_STORAGE then rt:throw("QuotaExceededError", "storage is full") end
      S.storage.bytes = S.storage.bytes + delta
      data[k] = v
    end)
    def(o, "removeItem", function(_, k)
      k = str(k)
      if data[k] ~= nil then S.storage.bytes = S.storage.bytes - #k - #data[k]; data[k] = nil end
    end)
    def(o, "clear", function()
      for k, v in pairs(data) do S.storage.bytes = S.storage.bytes - #k - #v; data[k] = nil end
    end)
    def(o, "key", function(_, i)
      local l = {}
      for k in pairs(data) do l[#l + 1] = k end
      table.sort(l)
      return l[(tonumber(i) or 0) + 1] or null
    end)
    acc(o, "length", function() local n = 0; for _ in pairs(data) do n = n + 1 end; return n + 0.0 end)
    return o
  end
  rt:set(G, "localStorage", storage("local_"))
  rt:set(G, "sessionStorage", storage("session"))

  -- dialogs and misc window functions
  def(G, "alert", function(_, msg) note(self, "alert: " .. str(msg)) end)
  def(G, "confirm", function() return false end)
  def(G, "prompt", function() return null end)
  def(G, "print", function() end)
  def(G, "focus", function() end)
  def(G, "blur", function() end)
  def(G, "stop", function() end)
  def(G, "close", function() end)
  def(G, "open", function(_, u) if u ~= nil then request_nav(u) end; return null end)
  local function scroller(_, a, b)
    local y = b
    if type(a) == "table" then y = rt:get(a, "top") end
    if y ~= nil then S.scroll_to = tonumber(y) or 0 end
  end
  def(G, "scrollTo", scroller)
  def(G, "scroll", scroller)
  def(G, "scrollBy", function(_, a, b)
    local y = b
    if type(a) == "table" then y = rt:get(a, "top") end
    S.scroll_by = (S.scroll_by or 0) + (tonumber(y) or 0)
  end)
  def(G, "getComputedStyle", function(_, el)
    local n = nodeof(el)
    local o = rt:new_object()
    if n then
      local chain, cur = {}, n
      while cur and cur.tag do table.insert(chain, 1, cur); cur = cur.parent end
      local st
      for _, c in ipairs(chain) do st = css.compute(c, S.rules, st) end
      if st then
        rt:set(o, "display", st.display == "inline" and "inline" or (st.display == "none" and "none" or "block"))
        rt:set(o, "visibility", st.hidden and "hidden" or "visible")
        rt:set(o, "color", string.format("rgb(%d, %d, %d)", (st.color >> 16) & 255, (st.color >> 8) & 255, st.color & 255))
        rt:set(o, "fontWeight", st.bold and "700" or "400")
        rt:set(o, "fontStyle", st.italic and "italic" or "normal")
        rt:set(o, "textAlign", st.align or "left")
      end
    end
    rt:set(o, "width", "auto"); rt:set(o, "height", "auto"); rt:set(o, "opacity", "1")
    def(o, "getPropertyValue", function(this, name)
      local v = rt:get(this, (str(name):gsub("%-(%a)", function(c) return c:upper() end)))
      return v == nil and "" or v
    end)
    return o
  end)
  def(G, "matchMedia", function(_, q)
    local o = rt:new_object()
    rt:set(o, "matches", css.media_ok(str(q)))
    rt:set(o, "media", str(q))
    def(o, "addEventListener", function() end)
    def(o, "removeEventListener", function() end)
    def(o, "addListener", function() end)
    def(o, "removeListener", function() end)
    return o
  end)
  def(G, "btoa", function(_, s)
    s = str(s)
    local b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    return ((s:gsub(".", function(x)
      local r, byte = "", x:byte()
      for i = 8, 1, -1 do r = r .. (byte % 2 ^ i - byte % 2 ^ (i - 1) > 0 and "1" or "0") end
      return r
    end) .. "0000"):gsub("%d%d%d?%d?%d?%d?", function(x)
      if #x < 6 then return "" end
      local c = 0
      for i = 1, 6 do c = c + (x:sub(i, i) == "1" and 2 ^ (6 - i) or 0) end
      return b:sub(c + 1, c + 1)
    end) .. ({ "", "==", "=" })[#s % 3 + 1])
  end)
  def(G, "atob", function(_, s)
    s = str(s):gsub("[^%w%+/=]", "")
    local b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    return (s:gsub("=", ""):gsub(".", function(x)
      local r, f = "", (b:find(x, 1, true) or 1) - 1
      for i = 6, 1, -1 do r = r .. (f % 2 ^ i - f % 2 ^ (i - 1) > 0 and "1" or "0") end
      return r
    end):gsub("%d%d%d%d%d%d%d%d", function(x)
      local c = 0
      for i = 1, 8 do c = c + (x:sub(i, i) == "1" and 2 ^ (8 - i) or 0) end
      return string.char(c)
    end))
  end)

  -- No network from a script: the page was opened by the user, anything else is a request the
  -- user did not make.
  def(G, "fetch", function()
    local p = rt:get(G, "Promise")
    local err = rt:construct(rt:get(G, "TypeError"), "network requests from scripts are not allowed in bm Web")
    return rt:call(rt:get(p, "reject"), p, err)
  end)
  local xhr = rt:native(function()
    local o = rt:new_object()
    for _, m in ipairs({ "open", "setRequestHeader", "abort", "addEventListener", "removeEventListener" }) do
      def(o, m, function() end)
    end
    def(o, "send", function(this)
      local h = rt:get(this, "onerror")
      if rt:is_callable(h) then
        rt:call(G.setTimeout and rt:get(G, "setTimeout") or h, null, h, 0)
      end
    end)
    rt:set(o, "readyState", 0.0)
    rt:set(o, "status", 0.0)
    rt:set(o, "responseText", "")
    return o
  end, "XMLHttpRequest")
  rt:set(G, "XMLHttpRequest", xhr)
  rt:set(G, "WebSocket", rt:native(function() rt:throw("SecurityError", "WebSocket is not available") end, "WebSocket"))

  -- observers: an IntersectionObserver reports at once that everything is visible, which makes
  -- lazy loaders show their content; the others do nothing
  local function observer(name, fires)
    local ctor = rt:native(function(_, cb)
      local o = rt:new_object()
      def(o, "observe", function(this, target)
        if fires and rt:is_callable(cb) then
          local entry = rt:new_object()
          rt:set(entry, "isIntersecting", true)
          rt:set(entry, "intersectionRatio", 1.0)
          rt:set(entry, "target", target)
          local tmo = rt:get(G, "setTimeout")
          rt:call(tmo, null, rt:native(function() safe(self, cb, o, rt:new_array({ entry }), o) end), 0)
        end
      end)
      def(o, "unobserve", function() end)
      def(o, "disconnect", function() end)
      def(o, "takeRecords", function() return rt:new_array({}) end)
      return o
    end, name)
    rt:set(G, name, ctor)
  end
  observer("IntersectionObserver", true)
  observer("MutationObserver", false)
  observer("ResizeObserver", false)

  -- Image(), URL, URLSearchParams, DOMParser-free helpers
  rt:set(G, "Image", rt:native(function(_, w, h)
    local n = new_element("img")
    if w ~= nil then n.attrs.width = str(math.floor(tonumber(w) or 0)) end
    if h ~= nil then n.attrs.height = str(math.floor(tonumber(h) or 0)) end
    return wrap(S, n)
  end, "Image"))
  rt:set(G, "HTMLElement", rt:native(function() end, "HTMLElement"))
  rt:set(G, "Element", rt:native(function() end, "Element"))
  rt:set(G, "Node", rt:native(function() end, "Node"))

  local function url_obj(u, base)
    local full = url.resolve(base or S.url, str(u), {}) or str(u)
    local p = url.parse(full) or {}
    local o = rt:new_object()
    rt:set(o, "href", full)
    rt:set(o, "protocol", (p.scheme or "") .. ":")
    rt:set(o, "hostname", p.host or "")
    rt:set(o, "host", (p.host or "") .. (p.port and (":" .. p.port) or ""))
    rt:set(o, "port", str(p.port))
    rt:set(o, "pathname", p.path == "" and "/" or (p.path or "/"))
    rt:set(o, "search", p.query and p.query ~= "" and ("?" .. p.query) or "")
    rt:set(o, "hash", full:match("#.*$") or "")
    rt:set(o, "origin", (p.scheme or "") .. "://" .. (p.host or ""))
    def(o, "toString", function() return full end)
    return o
  end
  rt:set(G, "URL", rt:native(function(_, u, base)
    if u == nil then rt:throw("TypeError", "Invalid URL") end
    return url_obj(u, base ~= nil and str(base) or nil)
  end, "URL"))

  local function decode_component(s)
    return (s:gsub("+", " "):gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end))
  end
  local function encode_component(s)
    return (s:gsub("[^%w%-_%.~]", function(c) return string.format("%%%02X", c:byte()) end))
  end
  rt:set(G, "URLSearchParams", rt:native(function(_, init)
    local pairs_ = {}
    local o = rt:new_object()
    local text = str(init):gsub("^%?", "")
    for kv in text:gmatch("[^&]+") do
      local k, v = kv:match("^([^=]*)=?(.*)$")
      pairs_[#pairs_ + 1] = { decode_component(k), decode_component(v) }
    end
    def(o, "get", function(_, k) for _, p in ipairs(pairs_) do if p[1] == str(k) then return p[2] end end return null end)
    def(o, "getAll", function(_, k)
      local l = {}
      for _, p in ipairs(pairs_) do if p[1] == str(k) then l[#l + 1] = p[2] end end
      return rt:new_array(l)
    end)
    def(o, "has", function(_, k) for _, p in ipairs(pairs_) do if p[1] == str(k) then return true end end return false end)
    def(o, "set", function(_, k, v)
      k = str(k)
      for _, p in ipairs(pairs_) do if p[1] == k then p[2] = str(v); return end end
      pairs_[#pairs_ + 1] = { k, str(v) }
    end)
    def(o, "append", function(_, k, v) pairs_[#pairs_ + 1] = { str(k), str(v) } end)
    def(o, "delete", function(_, k)
      local keep = {}
      for _, p in ipairs(pairs_) do if p[1] ~= str(k) then keep[#keep + 1] = p end end
      pairs_ = keep
    end)
    def(o, "toString", function()
      local l = {}
      for _, p in ipairs(pairs_) do l[#l + 1] = encode_component(p[1]) .. "=" .. encode_component(p[2]) end
      return table.concat(l, "&")
    end)
    def(o, "forEach", function(_, fn)
      for _, p in ipairs(pairs_) do safe(self, fn, null, p[2], p[1]) end
    end)
    return o
  end, "URLSearchParams"))

  rt:set(G, "CSS", (function()
    local o = rt:new_object()
    def(o, "supports", function() return false end)
    def(o, "escape", function(_, s) return str(s) end)
    return o
  end)())
  def(G, "requestIdleCallback", function(_, fn) return rt:call(rt:get(G, "setTimeout"), null, fn, 1) end)
  def(G, "cancelIdleCallback", function() end)
end

---------------------------------------------------------------------------
-- The life of a page
---------------------------------------------------------------------------
-- info: url, fetched (script address -> text), width. Returns the table page.lua takes as `js`.
function R:begin(info)
  self:close()
  self.dirty = false
  local me = self
  local hooks = {}
  function hooks.run(tree_b, rules, lay)
    local S, rt = build(me, info, tree_b, rules, lay)
    S.t0 = me.api.time()
    setup(me, S)
    S.host = {
      set_value = function(node, v)
        if me.set_value_cb then me.set_value_cb(node, v) end
      end,
      submit = function(node)
        if me.submit_cb then me.submit_cb(node) end
      end,
    }
    -- scripts in document order
    local list = {}
    collect(S.root, function(k) return k.tag == "script" end, list)
    local errors = 0
    for _, node in ipairs(list) do
      local a = node.attrs or {}
      local ty = a.type and a.type:lower():gsub(";.*$", "") or ""
      local ok_type = ty == "" or ty == "text/javascript" or ty == "application/javascript"
        or ty == "text/ecmascript" or ty == "application/ecmascript" or ty == "text/jscript"
        or ty == "application/x-javascript"
      if ok_type and a.nomodule == nil then
        local code
        if a.src and a.src ~= "" then
          code = info.fetched[a.src] or info.fetched[url.resolve(info.url, a.src, {}) or ""]
        else
          code = table.concat(text_of(node, {}))
        end
        if code and code ~= "" then
          S.current_script = node
          rt.budget = M.BUDGET_LOAD
          local ok, err = rt:run(code, a.src or "inline")
          if not ok then
            errors = errors + 1
            if errors <= 1 then note(me, "script error: " .. tostring(err)) end
          end
          rt:drain()
        end
      end
    end
    S.current_script = nil
    -- the page is parsed: DOMContentLoaded, then load
    S.ready = "interactive"
    fire(me, S.dockey, "readystatechange", nil, false, false)
    fire(me, S.dockey, "DOMContentLoaded", nil, true, false)
    S.ready = "complete"
    fire(me, S.dockey, "readystatechange", nil, false, false)
    fire(me, S.winkey, "load", nil, false, false)
    rt:drain()
    S.docloaded = true
  end
  function hooks.error(err)
    note(me, "script error: " .. tostring(err))
  end
  return hooks
end

-- The browser lays the page out and shows it.
function R:loaded(page)
  local S = self.S
  if not S then return end
  S.page = page
end

function R:relaid(page)
  local S = self.S
  if S then S.page = page end
end

function R:tick(now)
  local S = self.S
  if not S then return end
  local rt = S.rt
  rt.now_ms = now * 1000
  rt.budget = M.BUDGET_EVENT
  local n = rt:tick(rt.now_ms)
  if n and n > 0 then
    self.dirty = true
    rt:drain()
  end
end

-- A click on the page at (x, y) in page coordinates. True if a script took it.
function R:click(page, x, y)
  local S = self.S
  if not S then return false end
  local node = layout.hit_node(page, x, y)
  if not node then return false end
  local props = { clientX = x + 0.0, clientY = y + 0.0, button = 0.0, detail = 1.0 }
  local prevented = fire(self, node, "click", props, true, true)
  S.rt:drain()
  if S.scroll_to then
    self.scroll_request, S.scroll_to = S.scroll_to, nil
  end
  return prevented
end

-- The user typed a value into a text control.
function R:user_input(node, text)
  local S = self.S
  if not S or not node then return end
  node.attrs = node.attrs or {}
  if node.tag == "textarea" then
    node.kids = { { tag = "#text", text = text, parent = node } }
  else
    node.attrs.value = text
  end
  fire(self, node, "input", nil, true, false)
  fire(self, node, "change", nil, true, false)
  S.rt:drain()
end

function R:user_toggle(node, checked)
  local S = self.S
  if not S or not node then return end
  node.attrs = node.attrs or {}
  node.attrs.checked = checked and "" or nil
  fire(self, node, "change", nil, true, false)
  S.rt:drain()
end

-- A form is about to be sent. True if a script stopped it.
function R:submit(form_node)
  local S = self.S
  if not S or not form_node then return false end
  local prevented = fire(self, form_node, "submit", nil, true, true)
  S.rt:drain()
  return prevented
end

return M
