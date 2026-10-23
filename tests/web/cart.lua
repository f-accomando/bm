-- Host tests of the bmweb cart, stages 2b to 5: the browser state, the loader (pace and robots.txt),
-- the downloads and the view, all against a mock of the kernel API (no network).
-- Run from the repository root through tests/web/run.sh.
--   BMWEB_MAIN=path/to/main.lua   also loads the joined cart (build/build.py output) with stubs
package.path = "carts/bmweb/src/?.lua;" .. package.path

local browser = require("browser")
local view = require("view")
local robots = require("robots")
local files = require("files")
local cp437 = require("cp437")
local url = require("url")

local checks, fails = 0, 0
local function check(cond, msg)
  checks = checks + 1
  if not cond then fails = fails + 1; print("FAIL: " .. msg) end
end
local function eq(got, want, msg)
  checks = checks + 1
  if got ~= want then
    fails = fails + 1
    print(string.format("FAIL: %s\n    got:  %s\n    want: %s", msg, tostring(got), tostring(want)))
  end
end

---------------------------------------------------------------------------
-- The mock kernel
---------------------------------------------------------------------------
local function mock(site)
  local m = { now = 100, log = {}, reqs = {}, written = {}, saves = {}, imgs = {}, open = {}, nextid = 0, site = site }
  local api = {}
  api.time = function() return m.now end
  api.web_get = function(u, opts)
    if not u:match("^https://") then return nil, "only https:// addresses are allowed" end
    if m.fail_start then return nil, m.fail_start end
    m.nextid = m.nextid + 1
    local r = m.site[u]
    m.reqs[#m.reqs + 1] = { url = u, t = m.now, max = opts and opts.max }
    m.open[m.nextid] = { url = u, polls = 0, r = r }
    return m.nextid
  end
  api.web_poll = function(id)
    local o = m.open[id]
    if not o then return nil, "no such request" end
    o.polls = o.polls + 1
    local r = o.r
    if r and r.error then return "error", 0 end
    if not r then return "done", 0 end            -- not on the site: a 404 with no body
    if o.polls < (r.polls or 2) then return "busy", 100, r.body and #r.body end
    return "done", r.body and #r.body or 0, r.body and #r.body
  end
  api.web_info = function(id)
    local o = m.open[id]
    if not o then return nil end
    local r = o.r
    if not r then return { state = "done", status = 404, type = "text/plain", url = o.url, size = 0 } end
    if r.error then return { state = "error", error = r.error } end
    return { state = "done", status = r.status or 200, type = r.type or "text/html", url = r.final or o.url,
             size = #(r.body or "") }
  end
  api.web_body = function(id)
    local o = m.open[id]
    if not o then return nil, "no such request" end
    return o.r and o.r.body or ""
  end
  api.web_cancel = function(id)
    local had = m.open[id] ~= nil
    m.open[id] = nil
    return had
  end
  api.image_load = function(a, sx, sy, mw, mh)
    local id = type(a) == "number" and a or nil
    if id and not m.open[id] then return nil, "no such request" end
    local body = id and m.open[id].r.body or a
    if body:sub(1, 3) == "BAD" then return nil, "not a JPEG, PNG or WebP picture" end
    m.imgs[#m.imgs + 1] = { sx = sx, sy = sy, mw = mw, mh = mh }
    return { x = sx, y = sy, w = math.min(16, mw or 16), h = math.min(8, mh or 8) }
  end
  api.file_write = function(name, a)
    if m.write_busy and m.write_busy > 0 then m.write_busy = m.write_busy - 1; return nil, "fat_writing: try again" end
    if not name:match("^[%w_%-]+%.%w%w?%w?$") or #name > 12 then return nil, "bad name" end
    local body = type(a) == "number" and (m.open[a] and m.open[a].r.body) or a
    if body == nil then return nil, "no such request" end
    m.written[name] = body
    return true, "bm/web/" .. name
  end
  api.save = function(t, slot) m.saves[slot or 1] = t; return true end
  api.saved = function(slot) return m.saves[slot or 1] end
  return m, api
end

-- Run frames (1/60 s each) until f() is true. Returns the frames used.
local function run(m, b, f, max)
  for i = 1, max or 3000 do
    if f() then return i end
    m.now = m.now + 1 / 60
    b:update()
  end
  return nil
end
local function settle(m, b)
  return run(m, b, function() return b.mode ~= "loading" and b.mode ~= "render" end)
end
local function fresh(site, opts)
  local m, api = mock(site)
  opts = opts or {}
  opts.view_h = opts.view_h or 100
  local b = browser.new(api, opts)
  return m, api, b
end

local HOME = [[<html><head><title>Home</title></head><body><h1>Hello</h1>
<p>Text with <a href="/two">two</a> and <a href="/private/x">private</a> and <a href="/model.glb">model</a>
and <a href="/img">pics</a>.</p></body></html>]]
local SITE = {
  ["https://example.test/robots.txt"] = { type = "text/plain", body = "User-agent: *\nDisallow: /private\nCrawl-delay: 5\n" },
  ["https://example.test/"] = { body = HOME },
  ["https://example.test/two"] = { body = "<html><title>Two</title><body><p>Second page <a href='/'>back home</a></p>" ..
    string.rep("<p>filler line of text</p>", 60) .. "</body></html>" },
  ["https://example.test/model.glb"] = { type = "model/gltf-binary", body = "glTF" .. string.rep("\0", 64) },
  ["https://example.test/img"] = { body = '<html><body><img src="/a.png" width="40" height="30" alt="A"><img src="/b.png" alt="B">' ..
    '<img src="/c.png"></body></html>' },
  ["https://example.test/a.png"] = { type = "image/png", body = "PNGDATA" },
  ["https://example.test/b.png"] = { type = "application/octet-stream", body = "BADDATA" },
  ["https://example.test/c.png"] = { type = "image/png", body = "PNGDATA2" },
  ["https://example.test/form"] = { body = '<html><body><form action="/search" method="get"><input type="text" name="q">' ..
    '<input type="submit" value="Go"></form></body></html>' },
  ["https://example.test/search?q=low+poly"] = { body = "<html><body><p>results</p></body></html>" },
  ["https://example.test/pic.png"] = { type = "image/png", body = "PNGDATA" },
  ["https://example.test/redir"] = { final = "https://example.test/two", body = "<html><body>redirected</body></html>" },
  ["https://example.test/boom"] = { error = "connection refused" },
  ["https://example.test/note.txt"] = { type = "text/plain", body = "a < b & c" },
  ["https://other.test/robots.txt"] = { status = 404, type = "text/plain", body = "" },
}

---------------------------------------------------------------------------
-- cp437, robots, files
---------------------------------------------------------------------------
eq(cp437.conv("abc"), "abc", "cp437 ascii")
eq(cp437.conv("caf\u{e9}"), "caf\130", "cp437 e acute")
eq(cp437.conv("\u{201c}x\u{201d} \u{2014} \u{2026}"), '"x" - .', "cp437 quotes")
eq(cp437.conv("\u{4e2d}"), "?", "cp437 unknown")
eq(#cp437.conv("\u{c0}\u{20ac}\u{4e2d}"), 3, "cp437 one byte per code point")
eq(cp437.conv("\255\254"), "??", "cp437 bad utf8")

do
  local g = robots.parse("User-agent: *\nDisallow: /a\nAllow: /a/ok\nDisallow: /*.pdf$\nCrawl-delay: 99\n" ..
    "User-agent: bm\nDisallow: /bmonly\n")
  check(robots.allowed(g, "/a/x"), "robots: group bm wins over *, /a is open")
  check(not robots.allowed(g, "/bmonly/x"), "robots: group bm rule")
  local g2 = robots.parse("User-agent: *\nDisallow: /a\nAllow: /a/ok\nDisallow: /*.pdf$\nCrawl-delay: 99\n")
  check(not robots.allowed(g2, "/a/x"), "robots: disallow prefix")
  check(robots.allowed(g2, "/a/ok/1"), "robots: longer allow wins")
  check(not robots.allowed(g2, "/docs/x.pdf"), "robots: wildcard and anchor")
  check(robots.allowed(g2, "/docs/x.pdf?z"), "robots: anchor not matched")
  eq(g2.delay, robots.MAX_DELAY, "robots: crawl-delay capped")
  check(robots.allowed(robots.parse(""), "/anything"), "robots: empty allows")
  check(robots.allowed(robots.parse("User-agent: *\nDisallow:\n"), "/x"), "robots: empty disallow allows")
end

do
  for _, u in ipairs({ "https://x.test/a/Model.glb", "https://x.test/", "https://x.test/p?x=1", "https://x.test/very-long-file-name.tar.gz",
                       "https://x.test/evil.bm", "https://x.test/evil.BME", "https://x.test/e.b16", "https://x.test/.hidden" }) do
    local n = files.name(u, "application/octet-stream")
    check(n:match("^[a-z0-9]+%.[a-z0-9]+$") and #n <= 12 and #n:match("^[^.]+") <= 8, "8.3 name " .. n)
    local ext = n:match("%.(.+)$")
    check(ext ~= "bm" and ext ~= "bme" and ext ~= "b16", "no cart extension " .. n)
  end
  eq(files.name("https://x.test/a/m.glb", ""):match("%.(%w+)$"), "glb", "name keeps the extension")
  eq(files.name("https://x.test/x", "model/gltf-binary"):match("%.(%w+)$"), "glb", "name from the type")
  check(files.name("https://x.test/a", "") ~= files.name("https://x.test/b", ""), "name hash differs")
  eq(files.kind("text/html; charset=utf-8"), "html", "kind html")
  eq(files.kind("application/octet-stream"), "sniff", "kind sniff")
  eq(files.kind("model/gltf-binary"), "download", "kind download")
  eq(files.kind("image/gif"), "download", "kind gif is a download")
  eq(files.sniff("\137PNG\r\n\26\nxx"), "png", "sniff png")
  eq(files.sniff("  <!DOCTYPE html>"), "html", "sniff html")
  eq(files.sniff("glTF\2\0\0\0"), "glb", "sniff glb")
  eq(files.sniff("\0\1\2\3"), nil, "sniff unknown")
  eq(files.note_name("a.txt"), "a.nte", "note name for a txt")
  eq(files.note_name("a.glb"), "a.txt", "note name")
end

---------------------------------------------------------------------------
-- Navigation, pace and robots.txt
---------------------------------------------------------------------------
do
  local m, _, b = fresh(SITE)
  eq(b.mode, "home", "starts at home")
  check(b:go("example.test/"), "go starts")
  eq(b.mode, "loading", "loading")
  check(settle(m, b), "page loads")
  eq(b.mode, "page", "page mode")
  eq(b.url, "https://example.test/", "url")
  eq(b.title, "Home", "title")
  eq(#b.hist, 1, "one history entry")
  eq(m.reqs[1].url, "https://example.test/robots.txt", "robots.txt first")
  eq(m.reqs[2].url, "https://example.test/", "page second")
  check(m.reqs[2].t - m.reqs[1].t >= 5 - 1e-6, "crawl-delay of robots.txt respected: " .. (m.reqs[2].t - m.reqs[1].t))
  eq(m.reqs[2].max, browser.MAX_HTML, "page cap passed to the kernel")
  eq(next(m.open), nil, "no result left in the kernel")
  check(b.secure, "https lock")
  check(#b.targets >= 4, "link targets")

  -- follow the link "two"
  b:move_focus(1)
  eq(b.page.links[b.targets[b.focus].id].href, "https://example.test/two", "first link")
  b:activate()
  check(settle(m, b), "link loads")
  eq(b.url, "https://example.test/two", "followed")
  eq(#m.reqs, 3, "robots.txt is read once per host")
  check(m.reqs[3].t - m.reqs[2].t >= 5 - 1e-6, "pace between two pages")
  eq(#b.hist, 2, "two entries")
  b:scroll_by(40)
  eq(b.scroll, 40, "scrolled")
  b:back()
  check(settle(m, b), "back loads")
  eq(b.url, "https://example.test/", "back url")
  eq(b.pos, 1, "pos 1")
  b:forward()
  check(settle(m, b), "forward loads")
  eq(b.url, "https://example.test/two", "forward url")
  eq(b.scroll, 40, "scroll restored")
  eq(#b.hist, 2, "history stays")
  -- no request leaves while one runs
  check(not m.open[1], "all freed")
end

do -- disallowed path
  local m, _, b = fresh(SITE)
  b:go("https://example.test/private/x")
  check(settle(m, b), "settles")
  eq(b.mode, "error", "blocked is an error page")
  for _, r in ipairs(m.reqs) do check(not r.url:find("private", 1, true), "the blocked path is never requested") end
  check(b.status:find("robots", 1, true), "status names robots.txt")
  b:back()
  eq(b.mode, "home", "back from an error with no history goes home")
end

do -- 404, redirect, error, text
  local m, _, b = fresh(SITE)
  b:go("https://other.test/missing")
  check(settle(m, b), "404 settles")
  eq(b.mode, "page", "a 404 with no body is still a page")
  check(b.status:find("404", 1, true), "status 404")
  b:go("https://example.test/redir")
  check(settle(m, b), "redirect settles")
  eq(b.url, "https://example.test/two", "final URL after the redirect is shown")
  b:go("https://example.test/boom")
  check(settle(m, b), "error settles")
  eq(b.mode, "error", "error page")
  check(b.url == "https://example.test/boom", "error url")
  b:back()
  check(settle(m, b), "back from an error")
  eq(b.url, "https://example.test/two", "back reopens the last good page")
  b:go("https://example.test/note.txt")
  check(settle(m, b), "text settles")
  eq(b.mode, "page", "text is a page")
  local joined = {}
  for _, l in ipairs(b.page.lines) do for _, r in ipairs(l.runs) do joined[#joined + 1] = r.text or "" end end
  check(table.concat(joined, " "):find("a < b & c", 1, true), "plain text kept as text")
end

do -- refused addresses never reach the kernel
  local m, _, b = fresh(SITE)
  for _, u in ipairs({ "javascript:alert(1)", "http://example.test/", "file:///etc/passwd", "ftp://x.test/", "data:text/html,x",
                       "https://user:pw@example.test/", "" }) do
    b:go(u)
    eq(b.mode, "error", "refused: " .. u)
  end
  eq(#m.reqs, 0, "no request for a refused address")
  m.fail_start = "no network"
  b:go("example.test")
  check(settle(m, b), "no network settles")
  eq(b.mode, "error", "no network is an error page")
  eq(b.status, "Cannot load", "message for no network")
end

do -- stop while loading
  local m, _, b = fresh(SITE)
  b:go("https://example.test/")
  for _ = 1, 3 do m.now = m.now + 1 / 60; b:update() end
  eq(b.mode, "loading", "still loading")
  b:stop()
  eq(b.mode, "home", "stop returns to the old page")
  eq(next(m.open), nil, "stop frees the kernel result")
  eq(b.loader.job, nil, "no job")
  local n = #m.reqs
  for _ = 1, 600 do m.now = m.now + 1 / 60; b:update() end
  eq(#m.reqs, n, "nothing starts by itself (no prefetch)")
end

---------------------------------------------------------------------------
-- Pictures
---------------------------------------------------------------------------
do
  local m, _, b = fresh(SITE)
  b:go("https://example.test/img")
  settle(m, b)
  eq(#b.page.images, 3, "three images in the page")
  local n = #m.reqs
  for _ = 1, 600 do m.now = m.now + 1 / 60; b:update() end
  eq(#m.reqs, n, "pictures wait for the user")
  eq(b:load_images(), 3, "three queued")
  check(run(m, b, function() return not b:images_busy() end), "pictures finish")
  check(b.page.images[1].tex ~= nil, "first picture decoded")
  check(b.page.images[2].failed, "bad picture marked")
  check(b.page.images[3].tex ~= nil, "third picture decoded")
  eq(#m.imgs, 2, "image_load calls")
  check(m.imgs[1].mw == 160 and m.imgs[1].mh == 120, "picture cut to the slot")
  check(m.imgs[1].sx ~= m.imgs[2].sx or m.imgs[1].sy ~= m.imgs[2].sy, "pictures in different slots")
  local reqs = {}
  for _, r in ipairs(m.reqs) do if r.url:find("%.png$") then reqs[#reqs + 1] = r end end
  eq(#reqs, 3, "three picture requests")
  for i = 2, #reqs do check(reqs[i].t - reqs[i - 1].t >= 3 - 1e-6, "picture pace " .. i) end
  eq(reqs[1].max, browser.MAX_IMAGE, "picture cap")
  eq(next(m.open), nil, "all results freed")
  eq(b:load_images(), 0, "nothing more to load")
  -- navigating away drops the queue
  b:go("https://example.test/")
  settle(m, b)
  eq(b.atlas.n, 0, "atlas reset by a new page")
end

do -- a picture as the page
  local m, _, b = fresh(SITE)
  b:go("https://example.test/pic.png")
  check(settle(m, b), "picture settles")
  eq(b.mode, "image", "image mode")
  check(b.img and b.img.w == 16, "decoded into the sheet")
  eq(m.imgs[1].sx, 0, "at the sheet corner")
  eq(next(m.open), nil, "freed")
end

---------------------------------------------------------------------------
-- Downloads
---------------------------------------------------------------------------
do
  local m, _, b = fresh(SITE)
  b:go("https://example.test/")
  settle(m, b)
  b:go("https://example.test/model.glb")
  check(settle(m, b), "download settles")
  eq(b.mode, "download", "download prompt")
  check(b.dl.name:match("^model%w%w%.glb$"), "8.3 name " .. tostring(b.dl.name))
  eq(next(m.written), nil, "nothing written before A")
  m.write_busy = 2
  b:save_download()
  check(run(m, b, function() return b.mode ~= "download" end, 20), "saved after a busy card")
  eq(b.mode, "page", "back to the page")
  local name = b.saved_path:match("([^/]+)$")
  eq(m.written[name], SITE["https://example.test/model.glb"].body, "file body")
  local note = m.written[name:gsub("%.glb$", ".txt")]
  check(note and note:find("https://example.test/model.glb", 1, true), "note beside the file")
  check(note:find("https://example.test/", 1, true), "note has the page it came from")
  eq(next(m.open), nil, "result freed")
  -- B discards
  b:go("https://example.test/model.glb")
  settle(m, b)
  b:back()
  eq(b.mode, "page", "discard returns")
  local n = 0
  for _ in pairs(m.written) do n = n + 1 end
  eq(n, 2, "nothing more written")
  eq(next(m.open), nil, "discard frees")
  -- a failing write
  b:go("https://example.test/model.glb")
  settle(m, b)
  local fw = b.api.file_write
  b.api.file_write = function() return nil, "over the cap" end
  b:save_download()
  b:update()
  eq(b.mode, "download", "a failed write keeps the prompt")
  check(b.status:find("over the cap", 1, true), "failure shown")
  b.api.file_write = fw
  b:discard()
end

---------------------------------------------------------------------------
-- Forms, bookmarks
---------------------------------------------------------------------------
do
  local m, _, b = fresh(SITE)
  b:go("https://example.test/form")
  settle(m, b)
  b:move_focus(1)
  local act, c = b:activate()
  eq(act, "edit", "text field asks for text")
  b:set_value(c, "low poly")
  b:move_focus(1)
  b:activate()
  check(settle(m, b), "form loads")
  eq(b.url, "https://example.test/search?q=low+poly", "form URL")
end

do
  local m, api, b = fresh(SITE)
  b:go("https://example.test/")
  settle(m, b)
  b:toggle_mark()
  check(b:marked(), "marked")
  eq(m.saves[1].marks[1], "https://example.test/", "saved")
  local b2 = browser.new(api, { view_h = 100 })
  eq(#b2.marks, 1, "bookmark reloaded")
  local found
  for _, l in ipairs(b2.page.links) do if l.href == "https://example.test/" then found = true end end
  check(found, "home lists the bookmark")
  b:toggle_mark()
  check(not b:marked(), "unmarked")
  m.saves[1] = { marks = { "javascript:x", 5, "https://ok.test/" } }
  local b3 = browser.new(api, {})
  eq(#b3.marks, 1, "bad bookmarks dropped")
end

---------------------------------------------------------------------------
-- The view with a stub of the draw functions
---------------------------------------------------------------------------
do
  local m, api, b = fresh(SITE, { width = view.PAGE_W, cw = view.CW, lh = view.LH, view_h = view.VIEW_H })
  local prints, keys, pressed, held = {}, {}, {}, {}
  local mouse = { 0, 0, 0, 0 }
  local clicked = false
  local g = setmetatable({
    print = function(s, x, y) prints[#prints + 1] = s end,
    keyp = function() return table.remove(keys, 1) end,
    btnp = function(n) return pressed[n] end,
    btn = function(n) return held[n] end,
    mouse = function(on) if on ~= nil then return true end return mouse[1], mouse[2], mouse[3], mouse[4] end,
    mousep = function() return clicked end,
  }, { __index = function() return function() end end })
  local v = view.new(g, b)
  local function frame()
    m.now = m.now + 1 / 60
    v:update()
    v:draw()
    pressed, clicked = {}, false
  end
  frame()
  local seen = table.concat(prints, "|")
  check(seen:find("bm Web", 1, true), "home drawn")
  -- type an address with the keyboard
  pressed.y = true
  frame()
  check(v.edit ~= nil, "Y opens the address editor")
  for _, ch in ipairs({ "e", "x", "a", "m", "p", "l", "e", ".", "t", "e", "s", "t", "/", "\n" }) do keys[#keys + 1] = ch end
  frame()
  eq(v.edit, nil, "Enter closes the editor")
  check(run(m, b, function() frame(); return b.mode == "page" end), "page loaded through the view")
  prints = {}
  frame()
  seen = table.concat(prints, "|")
  check(seen:find("Hello", 1, true), "page text drawn")
  -- the on-screen keyboard with the pad
  pressed.y = true
  frame()
  pressed.down = true
  frame()
  pressed.ok = true
  frame()
  check(#v.edit.text > #"https://example.test/", "A types a key")
  pressed.select = true
  frame()
  eq(v.edit, nil, "Select cancels")
  -- follow a link by the pad
  held.right = true
  frame()
  held.right = nil
  check(b.focus ~= nil, "right focuses a link")
  pressed.ok = true
  frame()
  eq(b.mode, "loading", "A follows it")
  check(run(m, b, function() frame(); return b.mode == "page" end), "second page")
  -- mouse click on a link
  b:scroll_to(0)
  local l = b.page.links[1]
  mouse = { view.OX + l.boxes[1].x + 1, view.TOP + l.boxes[1].y + 1, 0, 0 }
  clicked = true
  frame()
  eq(b.mode, "loading", "click follows a link")
  check(run(m, b, function() frame(); return b.mode == "page" end), "page after the click")
  -- the download box draws
  b:go("https://example.test/model.glb")
  check(run(m, b, function() frame(); return b.mode == "download" end), "download mode")
  prints = {}
  frame()
  check(table.concat(prints, "|"):find("Save this file", 1, true), "download box drawn")
  pressed.back = true
  frame()
  eq(b.mode, "page", "B discards")
  -- scroll and keys do not error
  held.down = true
  for _ = 1, 10 do frame() end
  held = {}
  keys = { "pgdn", "pgup", "end", "home", "\t", "^d", "^h", "esc", "x" }
  for _ = 1, 4 do frame() end
  check(true, "keys run")
end

---------------------------------------------------------------------------
-- The joined cart (BMWEB_MAIN): loads and runs with stubs
---------------------------------------------------------------------------
local joined = os.getenv("BMWEB_MAIN")
if joined then
  local f = assert(io.open(joined, "rb"))
  local src = f:read("a")
  f:close()
  local m = mock(SITE)
  local prints = 0
  local env = setmetatable({
    require = require, time = function() return m.now end, log = function() end,
    print = function() prints = prints + 1 end, keyp = function() return nil end,
    btn = function() return false end, btnp = function() return false end,
    mouse = function() return 0, 0, 0, 0 end, mousep = function() return false end,
    save = function() return true end, saved = function() return nil end,
    screen = function() return true end, font = function() end, cart_sheet = function() return 640, 512 end,
  }, { __index = function(t, k)
    if k == "_G" then return t end
    if _G[k] ~= nil and (k == "ipairs" or k == "pairs" or k == "string" or k == "table" or k == "math" or k == "utf8"
        or k == "type" or k == "tostring" or k == "tonumber" or k == "select" or k == "setmetatable" or k == "next"
        or k == "pcall" or k == "error" or k == "rawget" or k == "assert") then return _G[k] end
    return function() end
  end })
  -- the joined cart must find its modules by itself: the kernel require is not asked for them
  env.require = function(name) error("kernel require asked for " .. name) end
  local chunk = assert(load(src, "bmweb-joined", "t", env))
  chunk()
  check(type(env._update) == "function" and type(env._draw) == "function", "joined cart defines _update and _draw")
  for _ = 1, 5 do env._update(); env._draw() end
  check(prints > 0, "joined cart draws text")
end

print(string.format("cart tests: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
