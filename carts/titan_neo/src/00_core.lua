-- Titan Clash Neo - High quality .b16 version
-- One elite mecha, one epic stage. NeoGeo arcade feel.

local W, H = 360, 360
local floor, min, max, abs = math.floor, math.min, math.max, math.abs
local sin, cos, random = math.sin, math.cos, math.random

local GROUND = 300
local ARENA_W = 800

local BL, BR, BU, BD, BA, BB, BX, BY = 0, 1, 2, 3, 4, 5, 6, 7

local G = { frame = 0, camx = 0, shake = 0, sy = 0 }

function clamp(v, a, b) return v < a and a or (v > b and b or v) end
