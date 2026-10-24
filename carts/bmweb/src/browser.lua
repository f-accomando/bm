-- bmweb: the browser state. No draw calls and no input reads: the view feeds
-- it actions and draws what it holds. It reaches the kernel only through
-- `api`, so tests/web/cart.lua runs it on a PC with a mock.
--
-- Modes: "home" (bookmarks), "loading" (a request runs, the old page stays),
-- "render" (the body is being laid out), "page", "image" (a picture opened
-- as the page), "download" (a file waits for A or B), "error".
local url = require("url")
local pagemod = require("page")
local layout = require("layout")
local loader = require("loader")
local files = require("files")
local atlas = require("atlas")
local css = require("css")

local M = {}

M.MAX_HTML = 1048576     -- bytes of one page; more is an error from the kernel
M.MAX_IMAGE = 204800     -- bytes of one picture
M.MAX_HISTORY = 32
M.MAX_MARKS = 32
M.SLICE = 0.006          -- seconds of layout work in one frame
M.CHUNK = 2048
M.SUB_GAP = 1            -- seconds between two requests for the style sheets and scripts of one page
M.MAX_CSS = 4            -- style sheets fetched for a page
M.MAX_JS = 6             -- scripts fetched for a page
M.MAX_CSS_BYTES = 131072
M.MAX_JS_BYTES = 262144
-- Hosts of trackers and ad scripts: their scripts are not fetched.
local BLOCKED = { "google-analytics.com", "googletagmanager.com", "doubleclick.net", "googlesyndication.com",
  "facebook.net", "connect.facebook.com", "hotjar.com", "scorecardresearch.com", "adservice.google.com",
  "analytics.google.com", "segment.com", "mixpanel.com", "amplitude.com", "optimizely.com", "criteo.com",
  "taboola.com", "outbrain.com", "clarity.ms", "newrelic.com", "nr-data.net", "sentry.io" }

local B = {}
B.__index = B

local ESC = { ["&"] = "&amp;", ["<"] = "&lt;", [">"] = "&gt;", ['"'] = "&quot;" }
local function esc(s) return (tostring(s):gsub('[&<>"]', ESC)) end

-- opts: width (px of the page area), cw, lh, allow_http, gap.
function M.new(api, opts)
  opts = opts or {}
  local self = setmetatable({
    api = api, width = opts.width or 632, cw = opts.cw or 6, lh = opts.lh or 12,
    allow_http = opts.allow_http,
    loader = loader.new(api, { gap = opts.gap, allow_http = opts.allow_http }),
    atlas = atlas.new(), hist = {}, pos = 0, marks = {}, values = {},
    mode = "home", mode0 = "home", url = "", page = nil, scroll = 0, focus = nil, targets = {},
    status = "", title = "", secure = false, nav = nil, dl = nil, at_error = false,
    imgq = nil, imgjob = nil, rend = nil, frame = 0, view_h = opts.view_h or 300,
    js_on = opts.js ~= false, sub = nil, found = nil, find_text = nil, pdrv = nil, relayout_at = 0,
  }, B)
  if opts.jsdom then
    local rt = opts.jsdom.new(api, { width = self.width })
    self.jsrt = rt
    -- a script sets a field value, or sends a form
    rt.set_value_cb = function(node, v)
      for _, c in ipairs(self.page and self.page.controls or {}) do
        if c.node == node then
          if c.name then self.values[c.name] = v end
          c.value = v
        end
      end
    end
    rt.submit_cb = function(node)
      for id, f in ipairs(self.page and self.page.forms or {}) do
        if f.node == node then
          local u = layout.form_url(self.page, id, self.values, nil, { allow_http = self.allow_http })
          if u then self:open(u) end
        end
      end
    end
  end
  local s = api.saved and api.saved(1)
  if type(s) == "table" and type(s.marks) == "table" then
    for i = 1, math.min(#s.marks, M.MAX_MARKS) do
      if type(s.marks[i]) == "string" and url.check(s.marks[i], { allow_http = opts.allow_http }) then
        self.marks[#self.marks + 1] = s.marks[i]
      end
    end
  end
  self:show_home()
  return self
end

---------------------------------------------------------------------------
-- Pages made by the cart itself (home, errors)
---------------------------------------------------------------------------
function B:set_page(page, u, mode)
  self.page, self.url, self.mode = page, u or "", mode or "page"
  self.title = page and page.title or ""
  self.scroll, self.focus, self.values = 0, nil, {}
  self.found = nil
  if self.jsrt and not (page and page.has_js) then self.jsrt:close() end
  self.atlas = atlas.new()
  self.secure = self.url:sub(1, 8) == "https://"
  self.img = nil
  self:build_targets()
end

local function synth(self, body)
  return pagemod.load("<html><body>" .. body .. "</body></html>",
    { url = "https://bmweb.invalid/", width = self.width, cw = self.cw, lh = self.lh })
end

function B:show_home()
  local t = { "<h2>bm Web</h2><p>Press Y (or Ctrl+L) and type an address. Only https pages open.</p>" }
  if #self.marks > 0 then
    t[#t + 1] = "<h3>Bookmarks</h3><ul>"
    for _, m in ipairs(self.marks) do t[#t + 1] = '<li><a href="' .. esc(m) .. '">' .. esc(m) .. "</a></li>" end
    t[#t + 1] = "</ul>"
  else
    t[#t + 1] = "<p>No bookmarks yet. Start adds the page you read.</p>"
  end
  self:set_page(synth(self, table.concat(t)), "", "home")
  self.status = "home"
  self.at_error = false
end

function B:show_error(u, title, text)
  local p = synth(self, "<h2>" .. esc(title) .. "</h2><p>" .. esc(text) .. "</p><p>" .. esc(u) .. "</p>")
  self:set_page(p, u, "error")
  self.status = title
  self.at_error = true
end

---------------------------------------------------------------------------
-- Targets: links and controls in reading order
---------------------------------------------------------------------------
function B:build_targets()
  local t = {}
  local p = self.page
  if p then
    for id, l in ipairs(p.links) do
      local b = l.boxes[1]
      if b then t[#t + 1] = { kind = "link", id = id, x = b.x, y = b.y, box = b } end
    end
    for id, c in ipairs(p.controls) do
      if c.box and c.kind ~= "hidden" and c.kind ~= "button" then
        t[#t + 1] = { kind = "ctl", id = id, x = c.box.x, y = c.box.y, box = c.box }
      end
    end
    table.sort(t, function(a, b)
      if a.y ~= b.y then return a.y < b.y end
      return a.x < b.x
    end)
  end
  self.targets = t
end

function B:focused()
  return self.focus and self.targets[self.focus] or nil
end

function B:max_scroll()
  local h = self.page and self.page.h or 0
  local m = h - self.view_h
  return m > 0 and m or 0
end

function B:scroll_to(y)
  local m = self:max_scroll()
  if y < 0 then y = 0 elseif y > m then y = m end
  self.scroll = y
end

function B:scroll_by(dy) self:scroll_to(self.scroll + dy) end

-- Move the focus to the next (d = 1) or previous (d = -1) target. The page
-- scrolls to keep it in view.
function B:move_focus(d)
  local n = #self.targets
  if n == 0 then return end
  local vh = self.view_h
  local i = self.focus
  if not i then
    if d > 0 then
      i = n
      for k = 1, n do if self.targets[k].y >= self.scroll then i = k; break end end
    else
      i = 1
      for k = n, 1, -1 do if self.targets[k].y < self.scroll + vh then i = k; break end end
    end
  else
    i = i + d
    if i < 1 then i = 1 elseif i > n then i = n end
  end
  self.focus = i
  local t = self.targets[i]
  if t.y < self.scroll then self:scroll_to(t.y - 8)
  elseif t.y + t.box.h > self.scroll + vh then self:scroll_to(t.y + t.box.h - vh + 8) end
end

-- Focus the target under a point of the page (mouse). Returns it.
function B:pick(x, y)
  local kind, id = layout.hit(self.page, x, y)
  if not kind then return nil end
  for i, t in ipairs(self.targets) do
    if t.kind == kind and t.id == id then self.focus = i; return t end
  end
  return nil
end

-- A click of the left button on the page at (x, y). A page script sees it first (an onclick
-- or a click listener may take it); then a link opens and a control reacts. Returns what
-- `activate` returns ("edit", control) or nil.
function B:click_page(x, y)
  if self.page and self.page.has_js and self.jsrt then
    if self.jsrt:click(self.page, x, y) then return nil end
  end
  local t = self:pick(x, y)
  if t then return self:activate() end
  return nil
end

function B:can_back()
  if self.mode == "loading" or self.mode == "render" or self.mode == "subres" then return true end
  if self.mode == "home" then return false end
  return true
end

function B:can_forward()
  return self.hist[self.pos + 1] ~= nil and self.mode ~= "loading"
end

-- Scripts on or off. The page is read again so that it shows what the new setting gives.
function B:toggle_js()
  self.js_on = not self.js_on
  self.status = self.js_on and "page scripts on" or "page scripts off"
  if self.mode == "page" and self.url ~= "" then self:open(self.url, { hist = false, scroll = self.scroll }) end
end

-- Find text in the page (case does not matter). `next` goes on from the last match.
function B:find(text, next)
  if not text or text == "" then return false end
  local p = self.page
  if not p then return false end
  local want = text:lower()
  self.find_text = text
  local n = #p.lines
  local start = 1
  local f = self.found
  if next and f and f.text == text then start = f.line + 1 end
  for k = 0, n - 1 do
    local i = (start - 1 + k) % n + 1
    local line = p.lines[i]
    for _, r in ipairs(line.runs) do
      if r.text and r.text:lower():find(want, 1, true) then
        self.found = { text = text, line = i, run = r }
        self:scroll_to(line.y - 40)
        self.status = string.format("found at line %d", i)
        return true
      end
    end
  end
  self.found = nil
  self.status = "not found: " .. text
  return false
end

---------------------------------------------------------------------------
-- Navigation
---------------------------------------------------------------------------
function B:stop()
  self.loader:cancel()
  if self.dl and self.dl.id then self.api.web_cancel(self.dl.id) end
  self.dl, self.rend, self.sub = nil, nil, nil
  self.imgq, self.imgjob = nil, nil
  if self.nav then
    self.nav = nil
    self.mode = self.mode0
    self.status = "stopped"
  end
end

local function save_pos(self)
  local h = self.hist[self.pos]
  if h and self.mode == "page" then h.scroll, h.focus = self.scroll, self.focus end
end

-- Open a full URL. opts: hist = false keeps the history as it is (back and
-- forward), scroll and focus = what to restore.
function B:open(u, opts)
  opts = opts or {}
  local clean, why = url.check(url.strip_fragment(u), { allow_http = self.allow_http })
  if not clean then
    self:show_error(u, "Cannot open", why or "bad address")
    return false
  end
  save_pos(self)
  self:stop()
  local job, e = self.loader:get(clean, "page", { max = M.MAX_HTML })
  if not job then
    self:show_error(clean, "Cannot open", e or "failed")
    return false
  end
  self.nav = { url = clean, push = opts.hist ~= false, scroll = opts.scroll, focus = opts.focus, from = self.url }
  self.mode0 = self.mode
  self.mode = "loading"
  self.status = "starting"
  return true
end

-- Text typed in the address bar.
function B:go(text)
  local u, why = url.from_input(text, { allow_http = self.allow_http })
  if not u then
    self:show_error(text or "", "Cannot open", why or "bad address")
    return false
  end
  return self:open(u)
end

function B:reload()
  if self.url ~= "" and self.mode ~= "loading" then self:open(self.url, { hist = false, scroll = self.scroll }) end
end

function B:back()
  if self.mode == "loading" or self.mode == "render" then self:stop(); return end
  if self.mode == "download" then self:discard(); return end
  save_pos(self)
  local target
  if self.mode == "error" or self.mode == "home" then target = self.pos else target = self.pos - 1 end
  local h = self.hist[target]
  if not h then
    if self.mode ~= "home" then self.pos = 0; self:show_home() end
    return
  end
  self.pos = target
  self:open(h.url, { hist = false, scroll = h.scroll, focus = h.focus })
end

function B:forward()
  local h = self.hist[self.pos + 1]
  if not h or self.mode == "loading" then return end
  save_pos(self)
  self.pos = self.pos + 1
  self:open(h.url, { hist = false, scroll = h.scroll, focus = h.focus })
end

function B:push_history(u)
  for i = #self.hist, self.pos + 1, -1 do self.hist[i] = nil end
  self.hist[#self.hist + 1] = { url = u, scroll = 0 }
  if #self.hist > M.MAX_HISTORY then table.remove(self.hist, 1) end
  self.pos = #self.hist
end

function B:home()
  self:stop()
  save_pos(self)
  self:show_home()
end

---------------------------------------------------------------------------
-- Activating a target
---------------------------------------------------------------------------
-- Returns "edit", control when the view must ask for text, else nil.
function B:activate()
  local t = self:focused()
  if not t then return nil end
  local p = self.page
  if t.kind == "link" then
    local l = p.links[t.id]
    if l then self:open(l.href) end
    return nil
  end
  local c = p.controls[t.id]
  if not c then return nil end
  if c.kind == "text" or c.kind == "textarea" then
    return "edit", c
  elseif c.kind == "checkbox" then
    c.checked = not c.checked
    if self.jsrt and p.has_js then self.jsrt:user_toggle(c.node, c.checked) end
  elseif c.kind == "radio" then
    for _, o in ipairs(p.controls) do
      if o.kind == "radio" and o.form == c.form and o.name == c.name then o.checked = false end
    end
    c.checked = true
    if self.jsrt and p.has_js then self.jsrt:user_toggle(c.node, true) end
  elseif c.kind == "select" then
    c.selected = (c.selected % #c.options) + 1
    c.value = c.options[c.selected].value
    if self.jsrt and p.has_js and c.node then self.jsrt:user_input(c.node, c.value) end
  elseif c.kind == "submit" then
    self:send_form(c.form, t.id)
  end
  return nil
end

function B:set_value(c, text)
  if c.name then self.values[c.name] = text end
  c.value = text
  if c.node and self.jsrt and self.page and self.page.has_js then self.jsrt:user_input(c.node, text) end
end

-- Send a form: a script may stop it with preventDefault on the submit event.
function B:send_form(form_id, pressed)
  local p = self.page
  local f = form_id and p.forms[form_id]
  if f and f.node and self.jsrt and p.has_js and self.jsrt:submit(f.node) then return false end
  local u, why = layout.form_url(p, form_id, self.values, pressed, { allow_http = self.allow_http })
  if u then return self:open(u) end
  self.status = why or "cannot send"
  return false
end

-- Enter pressed in a text field: send its form.
function B:submit_form_of(c)
  if not c.form then return false end
  return self:send_form(c.form, nil)
end

---------------------------------------------------------------------------
-- Bookmarks
---------------------------------------------------------------------------
function B:marked()
  for _, m in ipairs(self.marks) do if m == self.url then return true end end
  return false
end

function B:toggle_mark()
  if self.url == "" or self.mode ~= "page" then self.status = "nothing to mark"; return end
  for i, m in ipairs(self.marks) do
    if m == self.url then
      table.remove(self.marks, i)
      self.status = "bookmark removed"
      self.api.save({ marks = self.marks }, 1)
      return
    end
  end
  if #self.marks >= M.MAX_MARKS then self.status = "too many bookmarks"; return end
  self.marks[#self.marks + 1] = self.url
  self.status = "bookmark added"
  self.api.save({ marks = self.marks }, 1)
end

---------------------------------------------------------------------------
-- Pictures: on demand, one at a time, at the pace of the loader
---------------------------------------------------------------------------
-- Queue the pictures of the page, the one nearest the top of the view first.
-- Returns how many are queued.
function B:load_images()
  local p = self.page
  if not p or self.mode ~= "page" or self.imgq then return 0 end
  local q = {}
  for i, im in ipairs(p.images) do
    if not im.tex and not im.failed then q[#q + 1] = i end
  end
  local function dist(i)
    local y = p.images[i].box.y
    return y >= self.scroll and (y - self.scroll) or (self.scroll - y + 100000)
  end
  table.sort(q, function(a, b) return dist(a) < dist(b) end)
  while #q > atlas.MAX - self.atlas.n do q[#q] = nil end
  if #q == 0 then self.status = "no pictures to load"; return 0 end
  self.imgq, self.imgtotal = q, #q
  return #q
end

function B:images_busy() return self.imgq ~= nil end

local function image_step(self)
  local l = self.loader
  if not self.imgjob then
    if l.job then return end
    local idx = table.remove(self.imgq, 1)
    if not idx then self.imgq, self.imgtotal = nil, nil; self.status = "pictures done"; return end
    local im = self.page.images[idx]
    local job = l:get(im.src, "image", { max = M.MAX_IMAGE })
    if not job then im.failed = true; return end
    self.imgjob = { job = job, idx = idx }
    return
  end
  local ij = self.imgjob
  local done = l:step()
  if not done then
    self.status = "picture " .. (self.imgtotal - #self.imgq) .. "/" .. self.imgtotal .. " " .. tostring(ij.job.phase)
    return
  end
  local im = self.page.images[ij.idx]
  self.imgjob = nil
  if done.phase == "done" and done.res.status == 200 then
    local sx, sy = atlas.alloc(self.atlas)
    local img, why
    if sx then img, why = self.api.image_load(done.res.id, sx, sy, atlas.IMG_W, atlas.IMG_H) end
    if img then im.tex = img else im.failed, im.why = true, why or "no room" end
  else
    im.failed = true
    im.why = done.err or (done.blocked and "robots.txt") or ("status " .. tostring(done.res and done.res.status))
  end
  l:release(done)
end

---------------------------------------------------------------------------
-- Downloads: the file is fetched first, then A writes it (B drops it)
---------------------------------------------------------------------------
function B:save_download()
  local d = self.dl
  if not d or d.saving then return false end
  d.saving = true
  self.status = "saving " .. d.name
  return true
end

local function leave_download(self)
  self.dl = nil
  if self.at_error then self.mode = "error"
  elseif self.url == "" then self.mode = "home"
  else self.mode = "page" end
end

function B:discard()
  if self.dl and self.dl.id then self.api.web_cancel(self.dl.id) end
  leave_download(self)
  self.status = "not saved"
end

local function download_step(self)
  local d = self.dl
  if not d or not d.saving then return end
  local ok, path = self.api.file_write(d.name, d.id)
  if not ok then
    local why = tostring(path)
    if why:find("fat_writing", 1, true) or why:find("another write", 1, true) then
      d.tries = (d.tries or 0) + 1
      if d.tries < 120 then return end
    end
    d.saving = false
    self.status = "not saved: " .. why
    return
  end
  local note = files.note_name(d.name)
  self.api.file_write(note, files.note(d.url, d.from, d.type, d.size))
  self.api.web_cancel(d.id)
  self.status = "saved " .. path .. " (" .. d.size .. " bytes), note " .. note
  self.saved_path = path
  d.id = nil
  leave_download(self)
end

---------------------------------------------------------------------------
-- A finished request for a page
---------------------------------------------------------------------------
local function commit(self, page, u, status, nav)
  if nav.push then
    self:push_history(u)
  elseif self.hist[self.pos] then
    self.hist[self.pos].url = u
  end
  self:set_page(page, u, "page")
  self.at_error = false
  if nav.scroll then self:scroll_to(nav.scroll) end
  if nav.focus and self.targets[nav.focus] then self.focus = nav.focus end
  local bits = { tostring(status or "?") }
  if page.cut then bits[#bits + 1] = "page cut (" .. tostring(page.cut_reason) .. ")" end
  self.status = table.concat(bits, " ")
end

---------------------------------------------------------------------------
-- Style sheets and scripts of a page
---------------------------------------------------------------------------
local prescan, start_render

local function ci(name)
  return (name:gsub("%a", function(c) return "[" .. c:lower() .. c:upper() .. "]" end))
end

local function attr(tag, name)
  local n = ci(name)
  return tag:match("%f[%w]" .. n .. '%s*=%s*"([^"]*)"') or tag:match("%f[%w]" .. n .. "%s*=%s*'([^']*)'")
    or tag:match("%f[%w]" .. n .. "%s*=%s*([^%s>]+)")
end

local function blocked_host(u)
  local host = u:match("^https?://([^/:?#]+)")
  if not host then return false end
  host = host:lower()
  for _, b in ipairs(BLOCKED) do
    if host == b or host:sub(-#b - 1) == "." .. b then return true end
  end
  return false
end

local function js_type(t)
  if not t or t == "" then return true end
  t = t:lower():gsub(";.*$", "")
  return t == "text/javascript" or t == "application/javascript" or t == "text/ecmascript"
    or t == "application/ecmascript" or t == "text/jscript" or t == "application/x-javascript"
end

-- Find the style sheets and scripts a page asks for in its first 256 KB. Returns the list
-- {kind = "css" | "js", url, media, max} and whether the page has any script.
function prescan(self, body, base)
  local head = body:sub(1, 262144)
  local list, ncss, njs = {}, 0, 0
  local ropts = { allow_http = self.allow_http }
  for tag in head:gmatch("<" .. ci("link") .. "%s[^>]*>") do
    local rel = (attr(tag, "rel") or ""):lower()
    local href = attr(tag, "href")
    if href and rel:find("stylesheet", 1, true) and not rel:find("alternate", 1, true) and ncss < M.MAX_CSS then
      local media = attr(tag, "media")
      if not media or media == "" or css.media_ok(media) then
        local u = url.resolve(base, href, ropts)
        if u and u:match("^https://") then
          ncss = ncss + 1
          list[#list + 1] = { kind = "css", url = u, max = M.MAX_CSS_BYTES }
        end
      end
    end
  end
  local has_script = false
  for tag in head:gmatch("<" .. ci("script") .. "[%s>][^>]*>?") do
    has_script = true
    local src = attr(tag, "src")
    local ty = attr(tag, "type")
    if src and njs < M.MAX_JS and js_type(ty) and not tag:lower():find("nomodule", 1, true) then
      local u = url.resolve(base, src, ropts)
      if u and u:match("^https://") and not blocked_host(u) then
        njs = njs + 1
        list[#list + 1] = { kind = "js", url = u, max = M.MAX_JS_BYTES, src = src }
      end
    end
  end
  return list, has_script
end

-- Make the page driver and begin the layout. sub: the fetched sheets and scripts (or nil).
function start_render(self, rend, sub)
  local sheets = {}
  if sub then for _, c in ipairs(sub.css) do sheets[#sheets + 1] = c end end
  local o = { url = rend.url, width = self.width, cw = self.cw, lh = self.lh, sheets = sheets }
  if rend.use_js then
    o.js = self.jsrt:begin({ url = rend.url, fetched = sub and sub.js or {}, width = self.width })
  end
  rend.p = pagemod.new(o)
  self.rend = rend
  self.mode = "render"
  self.status = "laying out"
end

local function sub_step(self)
  local s = self.sub
  local job = self.loader.job
  if not job then
    s.i = s.i + 1
    local it = s.list[s.i]
    if not it then
      self.sub = nil
      start_render(self, s.rend, s)
      return
    end
    s.cur = it
    local j = self.loader:get(it.url, "sub", { max = it.max, gap = M.SUB_GAP })
    self.status = string.format("loading %s %d/%d", it.kind == "css" and "style sheet" or "script", s.i, #s.list)
    if not j then s.cur = nil end
    return
  end
  local done = self.loader:step()
  if done then
    local it = s.cur
    if done.phase == "done" and done.res.status == 200 and it then
      local text = self.api.web_body(done.res.id) or ""
      if it.kind == "css" then s.css[#s.css + 1] = text else s.js[it.src] = text; s.js[it.url] = text end
    end
    self.loader:release(done)
    s.cur = nil
  else
    local ph = job.phase
    local what = string.format("%s %d/%d", s.cur and s.cur.kind == "css" and "style" or "script", s.i, #s.list)
    if ph == "robots-pause" or ph == "pause" then
      self.status = string.format("waiting %.1f s (pace) %s", job.left or 0, what)
    else
      self.status = "loading " .. what
    end
  end
end

local function got_page(self, job)
  local nav = self.nav
  if job.phase == "blocked" then
    self.loader:release(job)
    self.nav = nil
    self:show_error(nav.url, "Blocked by robots.txt", "The site asks robots not to fetch this path (rule " ..
      tostring(job.blocked) .. "). bm Web follows robots.txt.")
    return
  elseif job.phase == "error" then
    self.loader:release(job)
    self.nav = nil
    self:show_error(nav.url, "Cannot load", job.err or "request failed")
    return
  end
  local res = job.res
  local kind = files.kind(res.type)
  local body
  if kind == "sniff" then
    if (res.size or 0) <= 262144 then
      body = self.api.web_body(res.id) or ""
      kind = files.sniff(body:sub(1, 32)) or "download"
      if kind == "glb" or kind == "gif" then kind = "download" end
    else
      kind = "download"
    end
  end
  if kind == "html" or kind == "text" then
    body = body or self.api.web_body(res.id) or ""
    self.loader:release(job)
    if kind == "text" then body = "<pre>" .. esc(body) .. "</pre>" end
    local rend = { body = body, pos = 1, url = res.url, status = res.status, nav = nav }
    local list, has_script = {}, false
    if kind == "html" then list, has_script = prescan(self, body, res.url) end
    rend.use_js = self.js_on and has_script and self.jsrt ~= nil
    if #list > 0 then
      self.sub = { list = list, i = 0, css = {}, js = {}, rend = rend }
      self.mode = "subres"
      self.status = "loading style sheets and scripts"
    else
      start_render(self, rend, nil)
    end
  elseif kind == "image" or kind == "png" or kind == "jpeg" or kind == "webp" then
    -- A picture opened as the page: decode it into the top left of the sheet.
    local img, why = self.api.image_load(body or res.id, 0, 0, 640, 384)
    self.loader:release(job)
    self.nav = nil
    if not img then
      self:show_error(nav.url, "Cannot show the picture", why or "bad picture")
      return
    end
    if nav.push then self:push_history(res.url) end
    self:set_page(synth(self, "<p>" .. esc(res.url) .. "</p>"), res.url, "image")
    self.img = img
    self.at_error = false
    self.status = img.w .. "x" .. img.h .. " " .. files.mime(res.type)
  else
    -- Any other file: ask before it is written. The cart now owns the result.
    local name = files.name(res.url, res.type)
    self.dl = { id = res.id, name = name, size = res.size or 0, type = files.mime(res.type),
                url = res.url, from = nav.from }
    job.res.id = nil
    self.loader.job = nil
    self.nav = nil
    self.mode = "download"
    self.status = "save " .. name .. "?"
  end
end

---------------------------------------------------------------------------
-- The frame
---------------------------------------------------------------------------
function B:update()
  self.frame = self.frame + 1
  local api = self.api
  if self.mode == "download" then
    download_step(self)
    return
  end
  if self.sub then
    sub_step(self)
    return
  end
  local r = self.rend
  if r then
    local t0 = api.time()
    repeat
      r.p:feed(r.body:sub(r.pos, r.pos + M.CHUNK - 1))
      r.pos = r.pos + M.CHUNK
    until r.pos > #r.body or api.time() - t0 >= M.SLICE
    if r.pos > #r.body then
      local page = r.p:finish()
      self.rend, self.nav = nil, nil
      self.pdrv = page.has_js and r.p or nil
      commit(self, page, r.url, r.status, r.nav)
      if page.has_js and self.jsrt then self.jsrt:loaded(page) end
    else
      self.status = string.format("laying out %d%%", 100 * r.pos // #r.body)
    end
    return
  end
  if self.nav and self.loader.job then
    local job = self.loader.job
    local done = self.loader:step()
    if done then
      got_page(self, done)
    else
      local ph = job.phase
      if ph == "robots-pause" or ph == "pause" then
        self.status = string.format("waiting %.1f s (pace)", job.left or 0)
      elseif ph == "robots" then self.status = "reading robots.txt"
      elseif ph == "connect" then self.status = "connecting"
      elseif ph == "wait" then self.status = "waiting for the host"
      else
        local s = job.size or 0
        self.status = job.length and string.format("loading %d/%d KB", s // 1024, job.length // 1024)
          or string.format("loading %d KB", s // 1024)
      end
    end
    return
  end
  if self.imgq then image_step(self) end
  if self.pdrv and self.page and self.page.has_js and self.jsrt then self:script_step() end
end

-- Timers of the page scripts, a navigation they ask for, and the layout after they changed
-- the page. At most one new layout every 0.25 s.
function B:script_step()
  local rt = self.jsrt
  local now = self.api.time()
  rt:tick(now)
  local nav = rt:take_nav()
  if nav then
    self:open(nav)
    return
  end
  if rt.log_last then self.status = rt.log_last; rt.log_last = nil end
  if rt.scroll_request then self:scroll_to(rt.scroll_request); rt.scroll_request = nil end
  if rt.dirty and now - self.relayout_at >= 0.25 then
    rt.dirty = false
    self.relayout_at = now
    local old = self.page
    local np = self.pdrv:relayout()
    if np then
      -- keep the pictures that are already decoded
      local byurl = {}
      for _, im in ipairs(old.images) do if im.tex or im.failed then byurl[im.src] = im end end
      for _, im in ipairs(np.images) do
        local o = byurl[im.src]
        if o then im.tex, im.failed = o.tex, o.failed end
      end
      self.page = np
      self.title = np.title or self.title
      self:build_targets()
      self:scroll_to(self.scroll)
      self.focus = nil
      self.found = nil
      rt:relaid(np)
    end
  end
end

return M
