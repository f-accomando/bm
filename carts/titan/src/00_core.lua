-- Titan Clash: a fighting game of giant robots (M20, first playable base).
-- One robot, VANGUARD, for everyone; light or heavy armour, a sword or
-- guns on the arms. 1 player against the computer, 2 players, or the
-- computer against itself. Sprites are pre-rendered (mkrobot.py).
--
-- This first file declares what all the others share.

local W, H = SCREEN_W or 640, SCREEN_H or 360
local floor, ceil, abs, min, max, sqrt = math.floor, math.ceil, math.abs, math.min, math.max, math.sqrt
local sin, cos, pi, random = math.sin, math.cos, math.pi, math.random
local fmt, sub = string.format, string.sub
local insert, remove = table.insert, table.remove

local GROUND = 316          -- screen row of the floor
local ARENA_W = 1024        -- the arena is wider than the screen
local GRAV = 0.5            -- pixels per frame per frame

-- buttons: directions, then the four attack buttons laid out like a SNES
-- pad: X (left) light punch, Y (top) heavy punch, A (bottom) light kick,
-- B (right) heavy kick
local BL, BR, BU, BD, BA, BB, BX, BY, BSTART, BSELECT = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9

-- the modules (each file fills one)
local Data, Fighter, Fx, Stage, Hud, Cpu, Scr, Snd = {}, {}, {}, {}, {}, {}, {}, {}
local G = { frame = 0, shake = 0, debug = false,
            camx = 0,          -- the camera's left edge in the arena
            sy = 0 }           -- the screen's shake now (pixels up or down)

local function clamp(v, a, b) return v < a and a or (v > b and b or v) end
local function approach(v, t, d) if v < t then return min(t, v + d) end return max(t, v - d) end
local function sign(v) return v < 0 and -1 or 1 end
local function choose(t) return t[random(#t)] end

-- a piece of the sheet {sx, sy, w, h, dx, dy} with its anchor at (x, y);
-- flipped: mirrored around x
local function piece(r, x, y, flip)
  if flip then
    sspr(r[1], r[2], r[3], r[4], x - r[5] - r[3], y + r[6], true)
  else
    sspr(r[1], r[2], r[3], r[4], x + r[5], y + r[6])
  end
end
local function pieces(list, x, y, flip)
  for i = 1, #list do piece(list[i], x, y, flip) end
end
local function sprite(name, x, y, flip)
  local r = SPR[name]
  if r then piece(r, x, y, flip) end
end

-- text: the console font, with a dark shadow; centred on x
local function text(s, x, y, c, scale)
  scale = scale or 1
  print(s, x + scale, y + scale, 0x000000, scale)
  print(s, x, y, c or 0xFFFFFF, scale)
end
local function text_c(s, x, y, c, scale)
  scale = scale or 1
  text(s, floor(x - #s * 4 * scale), y, c, scale)
end

-- a panel: dark fill and a light border
local function panel(x, y, w, h, fill, border)
  rectfill(x, y, w, h, fill or 0x10141C)
  rect(x, y, w, h, border or 0x5A6A86)
end
