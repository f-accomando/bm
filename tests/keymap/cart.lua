-- keymap(), btn() and btnp() by name, controller(), prompt() for a player
-- (2026-10-04): run by bmhost with tests/keymap/input.txt (make test-keymap)

local checks, fails = 0, 0
local function check(ok, what)
  checks = checks + 1
  if not ok then fails = fails + 1; log("keymap: FAIL " .. what) end
end

local MAP = { jump = "a", fire = { "x", "r1" }, menu = "start", yes = "ok" }
local f, phase, held_at = 0, 1, nil

function _init()
  keymap(MAP)
  local m = keymap()
  check(m.jump[1] == "a" and m.fire[1] == "x" and m.fire[2] == "r1" and m.yes[1] == "ok",
        "keymap() gives the table back")
  check(not pcall(keymap, { jump = "zz" }), "an unknown button is an error")
  check(not pcall(keymap, { a = "b" }), "a button's name is not an action")
  keymap(MAP)
  check(not pcall(btn, "nothing"), "btn() of a name that is no button nor action: an error")
  local c = controller(1)
  check(c.kind == "keyboard" and c.layout == "keyboard" and c.ok == "a" and c.back == "b",
        "player 1: the keyboard, ok is A, back is B")
  check(controller(2).kind == "ds4" and controller(2).layout == "ds4", "player 2: a DS4")
  check(controller(3).kind == "xbox" and controller(3).layout == "xbox", "player 3: an Xbox pad")
  check(controller(4).kind == "none", "player 4: nobody")
  -- prompt(): the key of the keyboard (space, wide) for player 1, the
  -- DS4's cross for player 2, the same chip for "ok" and "A" there
  local kw = prompt("jump", false, 1, 1)
  local dw = prompt("jump", false, 1, 2)
  check(kw > dw, "jump: the space bar for player 1, wider than player 2's cross")
  check(prompt("ok", false, 1, 2) == prompt("A", false, 1, 2), "ok is player 2's A")
  check(prompt("fire", false, 1, 3) == prompt("X", false, 1, 3), "fire is X (its first button)")
  check(not pcall(prompt, "nojump"), "prompt() of no button nor action: an error")
end

function _update()
  f = f + 1
  if phase == 1 and btn("jump", 1) then
    check(btnp("jump", 1) and btn("yes", 1) and btn("ok") and not btn("back") and btn(4, 1),
          "A of player 1: jump, yes, ok, btn(4)")
    check(not btn("jump", 2), "player 2 did not jump")
    phase, held_at = 2, f
  elseif phase == 2 and f == held_at + 1 then
    check(btn("jump", 1) and not btnp("jump", 1), "A held: not pressed again")
    phase = 3
  elseif phase == 3 and btn("fire", 2) then
    check(btnp("fire") and btnp("fire", 2) and not btn("fire", 1) and btn("r1", 2), "R1 of player 2: fire")
    phase = 4
  elseif phase == 4 and btn("back") then
    check(btnp("b", 1) and not btn("ok"), "B: back")
    phase = 5
  elseif phase == 5 and btn("menu", 3) then
    check(btnp("menu", 3) and btn("start"), "Start of player 3: menu")
    log(string.format("keymap: %d/%d checks passed", checks - fails, checks))
    quit()
  elseif f > 400 then
    check(false, "the presses of the script never came (phase " .. phase .. ")")
    log(string.format("keymap: %d/%d checks passed", checks - fails, checks))
    quit()
  end
end

function _draw()
  cls(0)
  print("keymap test", 8, 8, 0xFFFFFF)
end
