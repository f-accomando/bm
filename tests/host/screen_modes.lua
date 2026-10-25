-- screen(): the list of modes of a square panel (bmhost --square) and of a TV
-- (bmhost), and the size after the change. The cartridge's header is 360x360.
-- Run by `make test-screen-modes`; each run prints "screen_modes: N/N checks passed".

local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("screen_modes: FAIL " .. what) end
end

local function size(label)
  local w, h = screen()
  log(string.format("screen_modes: %s -> %dx%d", label, w, h))
  return w, h
end

local f, square = 0, false

function _init()
  square = (screen(1)) == 360
  log("screen_modes: panel " .. (square and "square" or "16:9"))
end

function _update()
  f = f + 1
  if f == 1 then
    local w, h = size("header")
    check(w == 360 and h == 360, "the header size is 360x360")
    if square then
      -- Overbit indexes this list: the first two entries never move
      local a, b = screen(1)
      check(a == 360 and b == 360, "screen(1) is 360x360")
      a, b = screen(2)
      check(a == 720 and b == 720, "screen(2) is 720x720")
      a, b = screen(3)
      log(string.format("screen_modes: screen(3) = %sx%s", tostring(a), tostring(b)))
      check(a == 720 and b == 405, "screen(3) is 720x405")
      check(screen(4) == nil, "screen(4) is nil")
      check(screen(720, 405) == true, "screen(720, 405) is accepted")
      check(screen(640, 360) == false, "screen(640, 360) is not a mode of a square panel")
      screen(720, 405)
    else
      local a, b = screen(1)
      check(a == 320 and b == 180, "screen(1) is 320x180")
      a, b = screen(3)
      check(a == 480 and b == 270, "screen(3) is 480x270")
      check(screen(720, 405) == false, "screen(720, 405) is no mode of the TV list")
      check(screen(640, 360) == true, "screen(640, 360) is accepted")
      screen(640, 360)
    end
  elseif f == 4 then
    local w, h = size(square and "screen(720, 405)" or "screen(640, 360)")
    if square then
      check(w == 720 and h == 405, "the size is 720x405 after screen(720, 405)")
      check(SCREEN_W == 720 and SCREEN_H == 405, "SCREEN_W and SCREEN_H follow")
      local a, b = screen(1)
      check(a == 360 and b == 360, "screen(1) stays 360x360 in the wide mode")
      screen(360, 360)
    else
      check(w == 640 and h == 360, "the size is 640x360 after screen(640, 360)")
      screen(320, 180)
    end
  elseif f == 7 then
    local w, h = size("back")
    if square then
      check(w == 360 and h == 360, "back to 360x360")
    else
      check(w == 320 and h == 180, "back to 320x180")
    end
    log(string.format("screen_modes: %d/%d checks passed", checks - fails, checks))
    quit()
  end
end

function _draw()
  cls(0)
  print("screen modes", 8, 8, 0xFFFFFF)
end
