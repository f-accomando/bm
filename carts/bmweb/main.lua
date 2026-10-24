-- bm Web: a text browser for HTML5 pages with CSS and JavaScript (see docs/design/web-browser-cart.md).
-- This file is the entry point. The modules are in src/ and build/build.py joins them with this
-- file into one script: a cartridge has one. The kernel does the network (web_get), the picture
-- decoding (image_load) and the file write (file_write); the cart holds no site name. The mouse and
-- the keyboard are the main input (view.lua); the pad works too.
local browser = require("browser")
local view = require("view")
local atlas = require("atlas")
-- The script engine is optional: a build without it browses pages and ignores their scripts.
local has_js, jsdom = pcall(require, "jsdom")
if not has_js then jsdom = nil end

local function none(what)
  return function() return nil, "this kernel has no " .. what end
end

local api = {
  web_get = web_get or none("web_get"), web_poll = web_poll or none("web_poll"),
  web_info = web_info or none("web_info"), web_body = web_body or none("web_body"),
  web_cancel = web_cancel or none("web_cancel"), image_load = image_load or none("image_load"),
  file_write = file_write or none("file_write"), time = time, save = save, saved = saved,
}

screen(view.W, view.H)
font("6x12")
cart_sheet(atlas.W, atlas.H)

local b = browser.new(api, { width = view.PAGE_W, cw = view.CW, lh = view.LH, view_h = view.VIEW_H, jsdom = jsdom })
local v = view.new(_G, b)

function _update() v:update() end
function _draw() v:draw() end

log("bmweb: ready")
