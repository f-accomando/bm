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

local M = {}

M.MAX_HTML = 1048576     -- bytes of one page; more is an error from the kernel
M.MAX_IMAGE = 204800     -- bytes of one picture
M.MAX_HISTORY = 32
M.MAX_MARKS = 32
M.SLICE = 0.006          -- seconds of layout work in one frame
M.CHUNK = 2048

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
  }, B)
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

---------------------------------------------------------------------------
-- Navigation
---------------------------------------------------------------------------
function B:stop()
  self.loader:cancel()
  if self.dl and self.dl.id then self.api.web_cancel(self.dl.id) end
  self.dl, self.rend = nil, nil
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
  elseif c.kind == "radio" then
    for _, o in ipairs(p.controls) do
      if o.kind == "radio" and o.form == c.form and o.name == c.name then o.checked = false end
    end
    c.checked = true
  elseif c.kind == "select" then
    c.selected = (c.selected % #c.options) + 1
    c.value = c.options[c.selected].value
  elseif c.kind == "submit" then
    local u, why = layout.form_url(p, c.form, self.values, t.id, { allow_http = self.allow_http })
    if u then self:open(u) else self.status = why or "cannot send" end
  end
  return nil
end

function B:set_value(c, text)
  if c.name then self.values[c.name] = text end
  c.value = text
end

-- Enter pressed in a text field: send its form.
function B:submit_form_of(c)
  if not c.form then return false end
  local u, why = layout.form_url(self.page, c.form, self.values, nil, { allow_http = self.allow_http })
  if u then return self:open(u) end
  self.status = why or "cannot send"
  return false
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
    self.rend = {
      p = pagemod.new({ url = res.url, width = self.width, cw = self.cw, lh = self.lh }),
      body = body, pos = 1, url = res.url, status = res.status, nav = nav,
    }
    self.mode = "render"
    self.status = "laying out"
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
      commit(self, page, r.url, r.status, r.nav)
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
end

return M
