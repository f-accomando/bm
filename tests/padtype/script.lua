-- An input script for bmhost (tests/host/bmhost.c) that copies a text of
-- Pad Typing (carts/typing) to the end: the presses padtype's coach says,
-- simulated here frame by frame with the same clock as bmhost (frame n is
-- at n/60 s), then written as "frame pad 1 buttons" lines, with shots.
--   luahost tests/padtype/script.lua build LANG TEXT OUT.txt [MODE]
-- The menu starts as a fresh SD card leaves it: Italiano, compose, 250 ms,
-- text 1, hints on; the script moves to LANG and TEXT, then Start.

local build, lang, text, out, mode = arg[1] or "build", arg[2] or "it", tonumber(arg[3] or 1), arg[4], arg[5]
package.path = build .. "/?.lua;src/ai/?.lua;" .. package.path
local pt = require "padtype"
local B = pt.BITS
local NAMES = { [B.LEFT] = "left", [B.RIGHT] = "right", [B.UP] = "up", [B.DOWN] = "down", [B.A] = "a",
                [B.B] = "b", [B.X] = "x", [B.Y] = "y", [B.START] = "start", [B.SELECT] = "select",
                [B.L1] = "l1", [B.R1] = "r1", [B.L2] = "l2", [B.R2] = "r2", [B.L3] = "l3", [B.R3] = "r3" }

local lines, frame_n, last = {}, 30, nil
local function emit(bits)
  if bits ~= last then
    local t = {}
    for bit, name in pairs(NAMES) do if bits & bit ~= 0 then t[#t + 1] = name end end
    table.sort(t)
    lines[#lines + 1] = frame_n .. " pad 1 " .. (#t > 0 and table.concat(t, " ") or "none")
    last = bits
  end
end
local function shot(name) lines[#lines + 1] = frame_n .. " shot " .. name end
local function idle(n) for _ = 1, n do emit(0); frame_n = frame_n + 1 end end
local function tapmenu(bit) emit(bit); frame_n = frame_n + 3; idle(3) end

-- the menu: language (row 1), mode (row 2), text (row 4)
idle(30)
shot("menu")
local LANGS = { it = 0, en = 1, lua = 2 }
for _ = 1, LANGS[lang] do tapmenu(B.RIGHT) end
if mode == "keyboard" then tapmenu(B.DOWN); tapmenu(B.RIGHT); tapmenu(B.UP) end
tapmenu(B.DOWN); tapmenu(B.DOWN); tapmenu(B.DOWN)
for _ = 2, text do tapmenu(B.RIGHT) end
idle(30)                                  -- the dictionaries: read in the menu
tapmenu(B.START)

-- the text, as the cart writes it
local target = pt.TEXTS[lang][text]
pt.clear(true)
pt.on(mode or "compose")
pt.set({ delay = 0.25, fallback = "keyboard" })
local host = pt.text_host(lang)
host.now = function() return frame_n / 60 end
local function frame(bits)
  emit(bits)
  frame_n = frame_n + 1
  pt.update(host, bits)
end
local function press(tap, hold, times)
  hold = hold or 0
  if hold ~= 0 then frame(hold) end
  for _ = 1, times or 1 do
    frame(hold | tap); frame(hold | tap); frame(hold)
  end
  frame(0)
end
local steps, shots = 0, { [12] = "typing", [30] = "typing2" }
while true do
  local s = pt.coach(host, target)
  if not s then break end
  if s.wait then
    frame(0)
  else
    steps = steps + 1
    if shots[steps] then shot(shots[steps]) end
    if s.both then
      frame(B.L1)
      for _ = 1, 20 do frame(B.L1 | B.R1) end
      frame(0)
    elseif s.hold and s.hold ~= 0 and steps > 40 and not shots.layer then
      shots.layer = true                  -- a trigger held: the overlay shows its layer
      frame(s.hold); frame(s.hold); frame(s.hold)
      shot("layer")
      press(s.tap, s.hold, s.double and 2 or 1)
    else
      press(s.tap, s.hold, s.double and 2 or 1)
    end
  end
end
for _ = 1, 40 do frame(0) end
shot("result")
idle(10)
lines[#lines + 1] = frame_n .. " quit"
local f = assert(io.open(out, "w"))
f:write(table.concat(lines, "\n"), "\n")
f:close()
print(string.format("%s text %d: %d steps, %d presses, %d frames (%.0f s)", lang, text, steps, pt.presses(),
                    frame_n, frame_n / 60))
