-- Titan Clash EX: 2D Giant Mecha Fighting Game for RGB30 (.b16)
-- High-impact 16-bit Mecha Fighting Game with Tag Team System,
-- Modular Mecha Frames, Super Combos, and Full Aerial Combat.
-- Native 360x360 Resolution (1:1 aspect ratio, integer-scaled 2x on RGB30).

local W, H = SCREEN_W or 360, SCREEN_H or 360
local floor, ceil, abs, min, max, sqrt = math.floor, math.ceil, math.abs, math.min, math.max, math.sqrt
local sin, cos, pi, random = math.sin, math.cos, math.pi, math.random
local fmt, sub = string.format, string.sub
local insert, remove = table.insert, table.remove

local GROUND = 312          -- floor level (gives 312 px of vertical air combat!)
local ARENA_W = 960         -- wide battle arena for tactical spacing
local GRAV = 0.52           -- gravity acceleration

-- Buttons (SNES / PowKiddy RGB30 controller layout):
-- D-pad: BL(0), BR(1), BU(2), BD(3)
-- Face buttons: BA(4)=Light Kick, BB(5)=Heavy Kick, BX(6)=Light Punch, BY(7)=Heavy Punch
-- Menus: BSTART(8), BSELECT(9)
-- Shoulder buttons (RGB30 L1/R1 / DS4 L1/R1):
local BL, BR, BU, BD = 0, 1, 2, 3
local BA, BB, BX, BY = 4, 5, 6, 7
local BSTART, BSELECT = 8, 9
local BL1, BR1 = 10, 11

-- Modules
local Data, Fighter, Fx, Stage, Hud, Cpu, Scr, Snd = {}, {}, {}, {}, {}, {}, {}, {}

-- Global match & engine state
local G = {
  frame = 0,
  shake = 0,
  debug = false,
  camx = 0,
  sy = 0,
  freeze = 0,          -- hitstop or super freeze frames
  slowmo = 0,          -- slow motion countdown
  stage = 1,           -- current arena stage (1: Harbor, 2: Dock, 3: Orbital)
  mode = "arcade",     -- "arcade", "tag", "versus", "cpu"
  teams = {},          -- team 1 and team 2 fighter tables
  fighters = {},       -- currently active fighters in combat
}

local function clamp(v, a, b) return v < a and a or (v > b and b or v) end
local function approach(v, t, d) if v < t then return min(t, v + d) end return max(t, v - d) end
local function sign(v) return v < 0 and -1 or 1 end
local function choose(t) return t[random(#t)] end
local function lerp(a, b, t) return a + (b - a) * t end

-- a piece of the sheet {sx, sy, w, h, dx, dy} with its anchor at (x, y)
local function piece(r, x, y, flip)
  if not r then return end
  if flip then
    sspr(r[1], r[2], r[3], r[4], x - r[5] - r[3], y + r[6], true)
  else
    sspr(r[1], r[2], r[3], r[4], x + r[5], y + r[6])
  end
end

local function pieces(list, x, y, flip)
  if not list then return end
  for i = 1, #list do piece(list[i], x, y, flip) end
end

local function sprite(name, x, y, flip)
  local r = SPR[name]
  if r then piece(r, x, y, flip) end
end

-- text rendering with drop shadow
local function text(s, x, y, c, scale)
  scale = scale or 1
  print(s, x + scale, y + scale, 0x000000, scale)
  print(s, x, y, c or 0xFFFFFF, scale)
end

local function text_c(s, x, y, c, scale)
  scale = scale or 1
  text(s, floor(x - #s * 4 * scale), y, c, scale)
end

local function text_r(s, x, y, c, scale)
  scale = scale or 1
  text(s, floor(x - #s * 8 * scale), y, c, scale)
end

-- decorative sci-fi panel
local function panel(x, y, w, h, fill, border, corner)
  rectfill(x, y, w, h, fill or 0x0C1018)
  rect(x, y, w, h, border or 0x485E7E)
  if corner then
    rectfill(x, y, 4, 1, 0xFFD040)
    rectfill(x, y, 1, 4, 0xFFD040)
    rectfill(x + w - 4, y, 4, 1, 0xFFD040)
    rectfill(x + w - 1, y, 1, 4, 0xFFD040)
    rectfill(x, y + h - 1, 4, 1, 0xFFD040)
    rectfill(x, y + h - 4, 1, 4, 0xFFD040)
    rectfill(x + w - 4, y + h - 1, 4, 1, 0xFFD040)
    rectfill(x + w - 1, y + h - 4, 1, 4, 0xFFD040)
  end
end

-- procedural pilot portrait rendering
local function draw_pilot_face(id, x, y, flip)
  -- 26x26 detailed mecha anime pilot portrait box
  rectfill(x, y, 26, 26, 0x0A0D14)
  rect(x, y, 26, 26, 0x485E7E)
  
  if id == 1 then
    -- Pilot: KAI (Vanguard pilot - Brown spiky hair, green headband, determined eyes)
    rectfill(x + 5, y + 2, 16, 7, 0x4A2C18)    -- hair top
    rectfill(x + 3, y + 6, 4, 10, 0x4A2C18)    -- hair left
    rectfill(x + 19, y + 6, 4, 10, 0x4A2C18)   -- hair right
    rectfill(x + 6, y + 7, 14, 12, 0xF8C8A0)   -- skin
    rectfill(x + 5, y + 6, 16, 3, 0x28A850)    -- headband
    rectfill(x + 7, y + 11, 4, 3, 0x183050)   -- left eye
    rectfill(x + 15, y + 11, 4, 3, 0x183050)  -- right eye
    pset(x + 8, y + 12, 0xFFFFFF)
    pset(x + 16, y + 12, 0xFFFFFF)
    rectfill(x + 11, y + 17, 4, 1, 0xB07050)   -- mouth
    rectfill(x + 4, y + 19, 18, 6, 0x1E3A58)   -- pilot suit collar
  elseif id == 2 then
    -- Pilot: ELENA (Valkyrie pilot - Silver hair, blue cyber-visor)
    rectfill(x + 4, y + 2, 18, 6, 0xD0E0EC)    -- silver hair
    rectfill(x + 2, y + 6, 5, 14, 0xB0C4D8)   -- side hair
    rectfill(x + 19, y + 6, 5, 14, 0xB0C4D8)  -- side hair
    rectfill(x + 6, y + 7, 14, 12, 0xFCE0C8)  -- skin
    rectfill(x + 6, y + 10, 14, 4, 0x1AB0E8)  -- cyber visor
    rectfill(x + 8, y + 11, 10, 2, 0x78F0FF)  -- visor glow
    rectfill(x + 11, y + 17, 4, 1, 0xC88070)  -- mouth
    rectfill(x + 4, y + 19, 18, 6, 0x384870)  -- flight suit
  elseif id == 3 then
    -- Pilot: BRONSON (Behemoth pilot - Rugged veteran, cybernetic eyepiece, facial scar)
    rectfill(x + 5, y + 2, 16, 5, 0x222428)    -- dark crew cut
    rectfill(x + 5, y + 6, 16, 13, 0xE8B488)  -- skin
    rectfill(x + 7, y + 10, 4, 3, 0x202428)   -- normal eye
    rectfill(x + 14, y + 9, 6, 5, 0xE03020)   -- glowing cyber eye
    pset(x + 16, y + 11, 0xFFFFA0)            -- eye core
    line(x + 8, y + 14, x + 11, y + 17, 0x904030) -- scar
    rectfill(x + 10, y + 17, 6, 2, 0x703020)  -- determined mouth
    rectfill(x + 3, y + 19, 20, 6, 0x483018)  -- heavy armor collar
  else
    -- Pilot: DREADNOUGHT / ZERO (Rival / Boss - Crimson glowing visor, dark helmet)
    rectfill(x + 4, y + 2, 18, 16, 0x181820)  -- dark helmet
    rectfill(x + 6, y + 9, 14, 5, 0xE02030)   -- sinister red visor
    rectfill(x + 8, y + 10, 10, 2, 0xFFA0A0)  -- visor laser scan
    rectfill(x + 8, y + 15, 10, 3, 0x303440)  -- faceplate respirator
    rectfill(x + 3, y + 18, 20, 7, 0x242838)  -- heavy blast armor
  end
end
