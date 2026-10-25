-- Host test of the bmweb cart against the test catalog web/models (mock content): the index page and a
-- model page are parsed and laid out, each .bmm goes through the cart's download path, and a fake kernel
-- checks file_write the way the real one does (src/bm/runtime.c file_write_model): the name rule, the
-- BMRES magic, kind 1 and the CRC-32 of the file. Run from the repository root through tests/web/run.sh.
package.path = "carts/bmweb/src/?.lua;" .. package.path

local browser = require("browser")
local files = require("files")

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

local function slurp(path)
  local f = assert(io.open(path, "rb"), path)
  local s = f:read("a")
  f:close()
  return s
end

local NAMES = { "house", "tree", "crate", "walker" }
local BASE = "https://models.test/"
local MODEL = {}
local SITE = {
  [BASE .. "robots.txt"] = { type = "text/plain", body = slurp("web/models/robots.txt") },
  [BASE .. "models/robots.txt"] = { type = "text/plain", body = slurp("web/models/robots.txt") },
  [BASE .. "models/index.html"] = { body = slurp("web/models/index.html") },
}
for _, n in ipairs(NAMES) do
  MODEL[n] = slurp("web/models/" .. n .. ".bmm")
  SITE[BASE .. "models/" .. n .. ".html"] = { body = slurp("web/models/" .. n .. ".html") }
  SITE[BASE .. "models/" .. n .. ".png"] = { type = "image/png", body = slurp("web/models/" .. n .. ".png") }
  SITE[BASE .. "models/" .. n .. ".bmm"] = { type = "application/octet-stream", body = MODEL[n] }
end

-- CRC-32 as the BMRES header holds it (offset 20, over the bytes from 128 on)
local crc_table = {}
for i = 0, 255 do
  local c = i
  for _ = 1, 8 do c = (c & 1) ~= 0 and (0xEDB88320 ~ (c >> 1)) or (c >> 1) end
  crc_table[i] = c
end
local function crc32(s, from)
  local c = 0xFFFFFFFF
  for i = from, #s do c = crc_table[(c ~ s:byte(i)) & 0xFF] ~ (c >> 8) end
  return c ~ 0xFFFFFFFF
end
local function valid_model(s)
  if #s < 128 then return false, "too short" end
  if s:sub(1, 8) ~= "BMRES\0\0\0" then return false, "bad magic" end
  if s:byte(13) ~= 1 or s:byte(14) ~= 0 then return false, "not a model" end
  if string.unpack("<I4", s, 21) ~= crc32(s, 129) then return false, "bad CRC" end
  return true
end

-- The mock kernel: web_get / web_poll / web_info / web_body / web_cancel / image_load as in cart.lua, and a
-- file_write with the kernel's rules. m.calls lists every file_write call: { name, bytes, ok, result }.
local function mock(site)
  local m = { now = 100, reqs = {}, open = {}, nextid = 0, site = site, calls = {}, written = {}, models = {},
              imgs = {} }
  local api = {}
  api.time = function() return m.now end
  api.web_get = function(u, opts)
    m.nextid = m.nextid + 1
    m.reqs[#m.reqs + 1] = { url = u, max = opts and opts.max }
    m.open[m.nextid] = { url = u, polls = 0, r = m.site[u] }
    return m.nextid
  end
  api.web_poll = function(id)
    local o = m.open[id]
    if not o then return nil, "no such request" end
    o.polls = o.polls + 1
    if not o.r then return "done", 0 end
    if o.polls < 2 then return "busy", 100, #o.r.body end
    return "done", #o.r.body, #o.r.body
  end
  api.web_info = function(id)
    local o = m.open[id]
    if not o then return nil end
    if not o.r then return { state = "done", status = 404, type = "text/plain", url = o.url, size = 0 } end
    return { state = "done", status = 200, type = o.r.type or "text/html", url = o.url, size = #o.r.body }
  end
  api.web_body = function(id)
    local o = m.open[id]
    return o and o.r and o.r.body or ""
  end
  api.web_cancel = function(id)
    local had = m.open[id] ~= nil
    m.open[id] = nil
    return had
  end
  api.image_load = function(a, sx, sy, mw, mh)
    local body = type(a) == "number" and m.open[a].r.body or a
    if body:sub(1, 8) ~= "\137PNG\r\n\26\n" then return nil, "not a picture" end
    m.imgs[#m.imgs + 1] = { sx = sx, sy = sy }
    return { x = sx, y = sy, w = 16, h = 8 }
  end
  api.file_write = function(name, a)
    local body = type(a) == "number" and (m.open[a] and m.open[a].r.body) or a
    local call = { name = name, bytes = body }
    m.calls[#m.calls + 1] = call
    local function fail(why) call.ok, call.result = false, why; return nil, why end
    if body == nil then return fail("no such request") end
    local base, ext = name:match("^([%w_%-]+)%.(%w+)$")
    if not base or #base > 8 or #ext > 3 then return fail("bad name") end
    ext = ext:lower()
    if ext == "bm" or ext == "bme" or ext == "b16" then return fail("a cartridge cannot write .bm, .bme or .b16 files") end
    if ext == "bmm" then
      local ok, why = valid_model(body)
      if not ok then return fail(why) end
      if m.models[name] then return fail("a model of that name is there already") end
      m.models[name] = body
      call.ok, call.result = true, "bmm/" .. name
      return true, "bmm/" .. name
    end
    m.written[name] = body
    call.ok, call.result = true, "bm/web/" .. name
    return true, "bm/web/" .. name
  end
  api.save = function(t, slot) return true end
  api.saved = function() return nil end
  return m, api
end

local function run(m, b, f, max)
  for i = 1, max or 3000 do
    if f() then return i end
    m.now = m.now + 1 / 60
    b:update()
  end
  return nil
end
local function settle(m, b)
  return run(m, b, function() return b.mode ~= "loading" and b.mode ~= "render" and b.mode ~= "subres" end)
end
local function fresh(site)
  local m, api = mock(site or SITE)
  return m, api, browser.new(api, { view_h = 100 })
end
local function follow(b, href)
  for i, t in ipairs(b.targets) do
    if t.kind == "link" and b.page.links[t.id].href == href then
      b.focus = i
      b:activate()
      return true
    end
  end
  return false
end
local function count(t) local n = 0 for _ in pairs(t) do n = n + 1 end return n end

-- the fake kernel accepts the committed models and refuses the rest (the rule the cart relies on)
for _, n in ipairs(NAMES) do
  check(valid_model(MODEL[n]), n .. ": the fake check accepts the committed model")
end

---------------------------------------------------------------------------
-- The index page and a model page: parsed and laid out
---------------------------------------------------------------------------
do
  local m, _, b = fresh()
  check(b:go(BASE .. "models/index.html"), "index: go")
  check(settle(m, b), "index: loads")
  eq(b.mode, "page", "index: page mode")
  eq(b.title, "Models (test)", "index: title")
  eq(#b.page.images, 4, "index: four preview pictures")
  local hrefs = {}
  for _, l in ipairs(b.page.links) do hrefs[l.href] = true end
  for _, n in ipairs(NAMES) do check(hrefs[BASE .. "models/" .. n .. ".html"], "index: link to " .. n) end
  check(#b.targets >= 8, "index: link targets " .. #b.targets)
  check(b.page.h and b.page.h > 0, "index: laid out, height " .. tostring(b.page.h))
  -- the pictures load on request, each decoded with image_load
  eq(b:load_images(), 4, "index: four pictures queued")
  check(run(m, b, function() return not b:images_busy() end), "index: pictures finish")
  for i, im in ipairs(b.page.images) do check(im.tex ~= nil, "index: picture " .. i .. " decoded") end
  -- the card of the house leads to its page
  check(follow(b, BASE .. "models/house.html"), "index: the house link is a target")
  check(settle(m, b), "model page loads")
  eq(b.url, BASE .. "models/house.html", "model page url")
  eq(b.title, "House", "model page title")
  eq(#b.page.images, 1, "model page: the preview")
  local dl = false
  for _, l in ipairs(b.page.links) do if l.href == BASE .. "models/house.bmm" then dl = true end end
  check(dl, "model page: the Download link points to house.bmm")
  -- follow the Download link: the download box, no write yet
  check(follow(b, BASE .. "models/house.bmm"), "model page: the download link is a target")
  check(settle(m, b), "download settles")
  eq(b.mode, "download", "download box")
  eq(count(m.calls), 0, "nothing written before the answer")
  b:discard()
  eq(count(m.calls), 0, "nothing written after a drop")
end

---------------------------------------------------------------------------
-- Each .bmm through the download path
---------------------------------------------------------------------------
for _, n in ipairs(NAMES) do
  local m, _, b = fresh()
  b:go(BASE .. "models/" .. n .. ".html")
  settle(m, b)
  b:go(BASE .. "models/" .. n .. ".bmm")
  check(settle(m, b), n .. ": settles")
  eq(b.mode, "download", n .. ": download box")
  eq(b.dl.model, true, n .. ": known as a model by its magic")
  eq(b.dl.name, n .. ".bmm", n .. ": name")
  check(b.status:find("/bmm/" .. n .. ".bmm", 1, true), n .. ": the box says /bmm: " .. b.status)
  b:save_download()
  check(run(m, b, function() return b.mode ~= "download" end, 30), n .. ": saved")
  eq(#m.calls, 2, n .. ": file_write called for the model and its note")
  eq(m.calls[1].name, n .. ".bmm", n .. ": file_write name")
  eq(m.calls[1].bytes, MODEL[n], n .. ": file_write got the whole file")
  eq(m.calls[1].ok, true, n .. ": the fake kernel accepted it")
  eq(b.saved_path, "bmm/" .. n .. ".bmm", n .. ": path")
  check(b.status:find("saved in /bmm/" .. n .. ".bmm", 1, true), n .. ": status: " .. b.status)
  check(not b.status:find("name made", 1, true), n .. ": the name was the URL's own")
  eq(m.calls[2].name, n .. ".txt", n .. ": the note goes to bm/web, not /bmm")
  check(m.written[n .. ".txt"] and m.written[n .. ".txt"]:find(BASE .. "models/" .. n .. ".bmm", 1, true), n .. ": note text")
  eq(count(m.models), 1, n .. ": one model on the card")
  eq(next(m.open), nil, n .. ": result freed")
end

---------------------------------------------------------------------------
-- The magic decides, not the type or the extension
---------------------------------------------------------------------------
do
  -- a model with a wrong MIME type and an odd URL
  for _, ctype in ipairs({ "model/x-bmm", "application/x-bmm", "text/x-unknown" }) do
    local site = { [BASE .. "dl?id=7"] = { type = ctype, body = MODEL.crate } }
    local m, _, b = fresh(site)
    b:go(BASE .. "dl?id=7")
    settle(m, b)
    eq(b.mode, "download", ctype .. ": download box")
    eq(b.dl.model, true, ctype .. ": recognised by the magic")
    eq(b.dl.name, "dl.bmm", ctype .. ": a valid name from the URL")
    check(b.dl.derived, ctype .. ": the name was made from the address")
    b:save_download()
    run(m, b, function() return b.mode ~= "download" end, 30)
    eq(m.calls[1].name, "dl.bmm", ctype .. ": file_write name")
    check(b.status:find("saved in /bmm/dl.bmm", 1, true) and b.status:find("name made from the address", 1, true),
      ctype .. ": status says the name was made: " .. b.status)
  end
  -- a model at a .bm URL: it is a model, saved as .bmm; never a .bm
  local site = { [BASE .. "evil.bm"] = { type = "application/octet-stream", body = MODEL.tree } }
  local m, _, b = fresh(site)
  b:go(BASE .. "evil.bm")
  settle(m, b)
  eq(b.dl.name, "evil.bmm", ".bm URL with a model: the name is .bmm")
  b:save_download()
  run(m, b, function() return b.mode ~= "download" end, 30)
  for _, c in ipairs(m.calls) do check(not c.name:lower():match("%.bm$") and not c.name:lower():match("%.bme$"), "no .bm write: " .. c.name) end
  -- the name rule: lower case, no odd characters, 8 characters at most
  eq(select(1, files.model_name("https://x.test/My%20Robot_Walker.v2.bmm?x=1")), "my20robo.bmm", "name from a messy URL")
  eq(select(1, files.model_name("https://x.test/")), "model.bmm", "name with no file part")
  eq(select(1, files.model_name("https://x.test/a-b_c.bmm")), "a-b_c.bmm", "a valid name is kept")
  eq(select(2, files.model_name("https://x.test/a-b_c.bmm")), true, "a valid name is flagged as the URL's own")
  eq(select(2, files.model_name("https://x.test/A.bmm")), false, "an upper case name is made lower case: flagged")
  check(files.model_alt("house.bmm", "https://x.test/h"):match("^[a-z0-9_%-]+%.bmm$") ~= nil, "alt name is valid")
  check(#files.model_alt("longname.bmm", "https://x.test/h") <= 12, "alt name has 8 characters at most")
end

---------------------------------------------------------------------------
-- A model that is there: another name, once; a second refusal is shown
---------------------------------------------------------------------------
do
  local m, _, b = fresh()
  for round = 1, 3 do
    b:go(BASE .. "models/house.bmm")
    settle(m, b)
    b:save_download()
    run(m, b, function() return b.mode ~= "download" or (b.dl and not b.dl.saving) end, 40)
    if round == 1 then
      check(b.saved_path == "bmm/house.bmm", "round 1: house.bmm")
    elseif round == 2 then
      check(b.saved_path and b.saved_path:match("^bmm/house%w%w%.bmm$"), "round 2: another name: " .. tostring(b.saved_path))
      check(b.status:find("name made from the address", 1, true), "round 2: says so")
    else
      eq(b.mode, "download", "round 3: still the box")
      check(b.status:find("not saved", 1, true) and b.status:find("there already", 1, true), "round 3: refused: " .. b.status)
      b:discard()
    end
  end
  eq(count(m.models), 2, "two models on the card, none replaced")
  eq(m.models["house.bmm"], MODEL.house, "house.bmm is unchanged")
end

---------------------------------------------------------------------------
-- Negative: bad bytes with a .bmm name, a .bm name
---------------------------------------------------------------------------
do
  local truncated = MODEL.house:sub(1, #MODEL.house - 40)
  local flipped = MODEL.house:sub(1, 300) .. string.char(MODEL.house:byte(301) ~ 0x55) .. MODEL.house:sub(302)
  local site = {
    [BASE .. "cut.bmm"] = { type = "application/octet-stream", body = truncated },      -- magic, cut short
    [BASE .. "flip.bmm"] = { type = "model/x-bmm", body = flipped },                    -- magic, CRC wrong
    [BASE .. "junk.bmm"] = { type = "application/octet-stream", body = string.rep("not a model ", 40) },
    [BASE .. "tiny.bmm"] = { type = "application/octet-stream", body = "BMRES" },
    [BASE .. "x.bm"] = { type = "application/octet-stream", body = string.rep("BMCART junk ", 20) },
    [BASE .. "x.bme"] = { type = "application/octet-stream", body = "something" },
    [BASE .. "x.b16"] = { type = "application/x-foo", body = "something" },
  }
  for _, u in ipairs({ "cut.bmm", "flip.bmm", "junk.bmm", "tiny.bmm" }) do
    local m, _, b = fresh(site)
    b:go(BASE .. u)
    settle(m, b)
    eq(b.mode, "download", u .. ": the box opens")
    b:save_download()
    for _ = 1, 10 do m.now = m.now + 1 / 60; b:update() end
    eq(b.mode, "download", u .. ": a refused write keeps the box")
    check(b.status:find("not saved", 1, true), u .. ": the refusal is shown: " .. b.status)
    eq(count(m.models), 0, u .. ": nothing in /bmm")
    eq(count(m.written), 0, u .. ": nothing in bm/web")
    for _, c in ipairs(m.calls) do check(c.ok == false, u .. ": every call was refused (" .. c.name .. ")") end
    check(m.calls[1] and m.calls[1].name:match("%.bmm$"), u .. ": it was asked as a model")
    b:discard()
    eq(next(m.open), nil, u .. ": result freed")
  end
  for _, u in ipairs({ "x.bm", "x.bme", "x.b16" }) do
    local m, _, b = fresh(site)
    b:go(BASE .. u)
    settle(m, b)
    eq(b.mode, "download", u .. ": the box opens")
    check(not b.dl.name:lower():match("%.bm$") and not b.dl.name:lower():match("%.bme$") and not b.dl.name:lower():match("%.b16$"),
      u .. ": the cart never asks for a cartridge name: " .. b.dl.name)
    eq(b.dl.model, nil, u .. ": not a model")
    b:save_download()
    run(m, b, function() return b.mode ~= "download" end, 30)
    for _, c in ipairs(m.calls) do
      check(not c.name:lower():match("%.bm$") and not c.name:lower():match("%.bme$") and not c.name:lower():match("%.b16$"), u .. ": no call with a cartridge name: " .. c.name)
    end
    eq(count(m.models), 0, u .. ": nothing in /bmm")
  end
  -- the fake kernel itself refuses a cartridge name (the rule the cart must never need)
  local _, api = mock(SITE)
  check(api.file_write("x.bm", "abc") == nil, "kernel rule: .bm refused")
  check(api.file_write("x.bmm", MODEL.house:sub(1, 200)) == nil, "kernel rule: a cut .bmm refused")
end

print(string.format("models cart tests: %d checks, %d failed", checks, fails))
os.exit(fails == 0 and 0 or 1)
