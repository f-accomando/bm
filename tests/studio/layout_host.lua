-- lib.layout of bmlib (the elastic layout) on the PC: the regions on the
-- text grid at 360x360, 640x360 and 1280x720, the size read at each call,
-- on_resize and poll when screen() changes the size, and want (a big screen
-- only where the GPU draws).
--
--   luahost tests/studio/layout_host.lua ROOT        (make test-studio)

local ROOT = arg[1] or "."
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

local gpu, asked = false, {}
local env = setmetatable({
  SCREEN_W = 360, SCREEN_H = 360,
  font = function() return 8, 16 end,
  gpu3d = function() return gpu end,
  screen = function(w, h) asked[#asked + 1] = w .. "x" .. h; return true end,
}, { __index = _G })
local lib = assert(loadfile(ROOT .. "/src/script/bmlib.lua", "t", env))()
local LY = lib.layout
check(LY ~= nil, "lib.layout exists")

local scr = LY.screen()
check(scr.x == 0 and scr.y == 0 and scr.w == 360 and scr.h == 360, "screen(): the whole screen")
local t = LY.split(scr, "v", { 16, "*", 16, 24 })
check(#t == 4 and t[2].y == 16 and t[2].h == 304 and t[3].y == 320 and t[4].y == 336 and t[4].h == 24,
      "split v: tabs, body 304, hints at 320, status at 336")
local h = LY.split(t[2], "h", { "16c", "*" })
check(h[1].w == 128 and h[2].x == 128 and h[2].w == 232, "split h: 16 columns and the rest")
local s = LY.split(scr, "h", { "*", "*", "*" })
check(s[1].w % 8 == 0 and s[2].w % 8 == 0 and s[1].w + s[2].w + s[3].w == 360 and s[2].x == s[1].w,
      "shares on whole columns, the last one the rest")
local v = LY.split(LY.rect(0, 16, 360, 304), "v", { "2r", "*", "2*" })
check(v[1].h == 32 and v[2].h % 16 == 0 and v[2].h + v[3].h == 272 and v[3].h >= 2 * v[2].h - 16, "rows and weights")
check(not pcall(LY.split, scr, "v", { 300, 100 }), "split: more than fits is an error")
check(not pcall(LY.split, scr, "x", { "*" }), "split: \"v\" or \"h\"")
check(not pcall(LY.split, scr, "v", { "3q" }), "split: a size it does not know")
check(LY.narrow(360) and not LY.narrow(640) and LY.narrow(), "narrow: 360 yes, 640 no, the screen's")
check(LY.cols(scr) == 45 and LY.rows(scr) == 22, "45 columns, 22 rows")
check(LY.snap(1) == 8 and LY.snap(8) == 8 and LY.snap(0) == 0, "snap: the next column")
local x, y = LY.row(LY.rect(10, 20, 80, 64), 3)
check(x == 10 and y == 52, "row: the third row")
local w = LY.wrap("the game stopped: see the line in red, Ctrl+G jumps there", 20)
local ok = #w >= 3
for _, l in ipairs(w) do if #l > 20 then ok = false end end
check(ok, "wrap: lines of at most 20")
check(#LY.wrap("a b c d e f g", 3, 2) == 2 and LY.wrap("abcdefgh", 3)[1] == "abc", "wrap: n lines, long words cut")
check(LY.fit("abcdef", 3) == "abc" and LY.fit("ab", 3) == "ab" and LY.fit("ab", 0) == "", "fit")
check(LY.inside(LY.rect(8, 8, 8, 8), scr) and not LY.inside(LY.rect(350, 0, 16, 8), scr), "inside")
check(LY.overlap(LY.rect(0, 0, 10, 10), LY.rect(9, 9, 5, 5)) and not LY.overlap(LY.rect(0, 0, 8, 8), LY.rect(8, 0, 8, 8)),
      "overlap")
local p, wk = LY.side_by_side(scr)
check(p.w == 128 and wk.x == 128 and wk.w == 232, "side_by_side narrow: 16 columns")

-- the size is read at each call: the same code at 1280x720
local hits = {}
LY.on_resize(function(sw, sh) hits[#hits + 1] = sw .. "x" .. sh end)
check(not LY.poll() and #hits == 0, "poll: nothing changed, nothing called")
env.SCREEN_W, env.SCREEN_H = 1280, 720
scr = LY.screen()
check(scr.w == 1280 and LY.cols(scr) == 160 and LY.rows(scr) == 45, "1280x720: 160 columns, 45 rows")
p, wk = LY.side_by_side(scr)
check(p.w == 240 and wk.x == 240 and wk.w == 1040, "side_by_side wide: 30 columns, the rest")
local body = LY.split(scr, "v", { "*", 16, 24 })
check(body[1].h == 680 and body[2].y == 680 and body[3].y == 696, "bars at the bottom of 720")
check(LY.poll() and #hits == 1 and hits[1] == "1280x720", "poll: the change calls on_resize once")
check(not LY.poll() and #hits == 1, "poll: the same size again calls nothing")
local now = {}
LY.on_resize(function(sw, sh) now[#now + 1] = sw end, true)
check(#now == 1 and now[1] == 1280, "on_resize with now: runs at once")
env.SCREEN_W, env.SCREEN_H = 640, 360
LY.poll()
check(#hits == 2 and #now == 2 and now[2] == 640, "poll: every function is called")

-- want: a big screen only where the GPU draws
gpu = false
check(LY.want(640, 360) and #asked == 0, "want: the size it has is true, no call")
check(not LY.want(1280, 720) and #asked == 0, "want: 1280x720 without the GPU is refused, no call")
gpu = true
check(LY.big_ok(), "big_ok: the GPU draws")
check(LY.want(1280, 720) and asked[1] == "1280x720", "want: 1280x720 with the GPU asks the console")
check(LY.want(480, 270) and asked[2] == "480x270", "want: a smaller size asks the console")

io.write(string.format("layout_host: %d checks, %d failed\n", checks, fails))
os.exit(fails == 0 and 0 or 1)
