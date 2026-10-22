-- controls= of bm/config.txt (the player's remap, CONTROLS-1): run by bmhost with
-- BMHOST_CONFIG="controls=c1:x t2:l1 s1:y pa:se mc:x" (make test-controls)

local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("controls: FAIL " .. what) end
end

local f, phase = 0, 1

function _init()
  local c = controls()
  check(c.scheme == "scroller" and c.c1 == "x" and c.t2 == "l1" and c.s1 == "y" and c.pa == "se" and c.mc == "x" and
        c.c2 == "default" and c.s8 == "default" and c.mo == "default", "controls(): the settings, default for the rest")
  keymap({ act1 = "ok", act2 = "y", pause = "start", other = "a" })
  local m = keymap()
  check(m.act1[1] == "x" and #m.act1 == 1 and m.act2[1] == "y" and m.pause[1] == "select" and m.other[1] == "a",
        "scroller: act1 and the pause replaced, the rest the game's")
  controls("topdown")
  m = keymap()
  check(m.act1[1] == "ok" and m.act2[1] == "l1" and m.pause[1] == "select", "topdown: act2 replaced, act1 the game's again")
  controls("stick")
  m = keymap()
  check(m.act1[1] == "y" and m.act2[1] == "y", "stick: act1 replaced")
  check(controls().scheme == "stick", "controls(): the scheme")
  check(not pcall(controls, "bogus"), "an unknown scheme is an error")
  controls("scroller")
end

function _update()
  f = f + 1
  if phase == 1 and btn("x", 1) then
    check(btn("act1") and btnp("act1") and not btn("act2") and not btn("ok"), "X held: act1 (the player's), not the game's ok")
    phase = 2
  elseif phase == 2 and btn("select", 1) then
    check(btn("pause"), "Select held: the pause the player set")
    log(string.format("controls: %d/%d checks passed", checks - fails, checks))
    quit()
  elseif f > 200 then
    check(false, "the presses of the script never came (phase " .. phase .. ")")
    log(string.format("controls: %d/%d checks passed", checks - fails, checks))
    quit()
  end
end

function _draw()
  cls(0)
  print("controls test", 8, 8, 0xFFFFFF)
end
