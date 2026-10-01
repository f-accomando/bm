-- nano8: plays PICO-8 style cartridges (.p8 and .p8.png) on bm.
--
-- The cart's code is written in a dialect of Lua; Xl turns it into Lua 5.4
-- and Env gives it the functions it expects, most of them the machine of
-- the kernel's "n8" library (memory, drawing, text, sound). Vm runs it a
-- frame at a time, In maps the keyboard and the controllers onto its six
-- buttons, Ui is the console around it: the list of carts with their
-- labels, the pause menu, the controls, the errors.
--
-- The source is split in carts/nano8/src/*.lua, joined by build.py: this
-- first file holds the shared locals, every other file is a `do ... end`
-- block that fills the module tables below.

local W, H = SCREEN_W, SCREEN_H
local floor, min, max, abs = math.floor, math.min, math.max, math.abs
local fmt, sub, byte, char, find = string.format, string.sub, string.byte, string.char, string.find
local insert, remove, concat, sort = table.insert, table.remove, table.concat, table.sort

local Xl = {}       -- the dialect translator
local Env = {}      -- the cart's globals
local In = {}       -- buttons: keyboard and controller mapping
local Vm = {}       -- a cart running
local Ui = {}       -- screens
local Cfg = {}      -- settings and saved data (cartdata), on the SD card

-- where the carts are looked for, in this order
local DIRS = { "/carts/nano8", "/nano8", "/carts", "/" }

-- the console's colours
local C = {
  bg = 0x101420, panel = 0x1c2233, line = 0x39425c, text = 0xe8ecf4, dim = 0x8a93ab,
  accent = 0xff4f78, ok = 0x39d98a, warn = 0xffc045, err = 0xff5a5a, sel = 0x2b3550,
}

local function clamp(v, a, b) return v < a and a or v > b and b or v end
