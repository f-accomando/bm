-- online() (2026-10-04): PS in a game played online asks the player leaving
-- "Leave the match?" over the game, which goes on and sees none of the
-- buttons; back stays, ok or PS again calls _leave() and the game ends (not
-- suspended). Run by bmhost with tests/online/input.txt (make test-online).

local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("online: FAIL " .. what) end
end
local function report()
  log(string.format("online: %d/%d checks passed", checks - fails, checks))
end

local f, asked, asks, seen = 0, 0, 0, {}
local was_asking, saw_left_asking, saw_b, saw_left_after = false, false, false, false

function _init()
  check(online() == false, "not online at the start")
  check(online(true, "You are the host: the match ends for all.") == false, "online(true) gives the old state")
  check(online() == true, "online now")
end

function _update()
  f = f + 1
  local _, asking = online()
  if asking then
    asked = asked + 1
    if not was_asking then asks = asks + 1 end
    if btn("left") or btn(0) or pad() ~= 0 or keydown(0x29) then saw_left_asking = true end
    local x, y = stick()
    if x ~= 0 or y ~= 0 then saw_left_asking = true end
  else
    if btnp("b") or btnp("back") then saw_b = true end
    if asks >= 1 and btn("left") then saw_left_after = true end
  end
  was_asking = asking
  if f > 600 then
    check(false, "the game never left (asked " .. asks .. " times)")
    report()
    quit()
  end
end

-- yes: the game tells the server, then it ends
function _leave()
  check(asks == 3, "asked three times before leaving (got " .. asks .. ")")
  check(asked > 20, "the game went on while asked (" .. asked .. " frames)")
  check(not saw_left_asking, "no buttons, sticks nor keys for the game while asked")
  check(not saw_b, "B (stay) never reaches the game, held after the question either")
  check(saw_left_after, "the buttons come back after the question")
  check(online() == true, "still online inside _leave()")
  report()
end

function _draw()
  cls(0x203040)
  print("online test " .. f, 8, 8, 0xFFFFFF)
end
