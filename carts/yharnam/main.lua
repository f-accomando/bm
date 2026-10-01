-- Yharnam: an endless gothic town at night, seen from above (3/4 view),
-- 256x256. The town is made while you walk: every screen-sized block is
-- built off screen before it comes into view, from its coordinates alone
-- (the same streets when you come back). Winding streets cross from block
-- to block; districts of houses, cemeteries, gardens, squares, pyres and
-- chapels. Light as in Dank Tomb (PICO-8): every pixel has a light level
-- and its colour goes through a fade table (fades, dark_begin, glow,
-- dark_end); warm gas lamps, lit windows, braziers and pyres with fire.
-- Arrows: walk (8 directions). B (held): run. A: light a dark street lamp.
-- Sprites and tiles: mkassets.py (the hunter, the props and the town are
-- drawn there in code).

-- [atlas begin] written by mkassets.py: do not edit by hand
local SHEET_W = 1024
local HUNTER = { w = 56, h = 60, ax = 28, ay = 50 }
local GROUND = {
  cobble = { 7680, 7682, 7684, 7686 },
  cobble_warm = { 7688, 7690 },
  cobble_moss = { 7692, 7694 },
  cobble_wet = { 7696, 7698 },
  setts = { 7700, 7702, 7704 },
  flags = { 7706, 7708, 7710, 7712, 7714 },
  flags_dark = { 7716, 7718, 7720, 7722, 7724 },
  pave = { 7726, 7728, 7730 },
  grass = { 7732, 7734, 7736 },
  grass_dark = { 7738, 7740 },
  dirt = { 7742, 7744 },
  gravel = { 7746, 7748 },
  marble = { 7750, 7752 },
  planks = { 7754 },
  water = { 7756, 7758, 7760, 7762 },
}
local CURB = { 7764, 7766, 7768, 7770, 7772, 7774, 7776, 7778, 7780, 7782, 7784, 7786, 7788, 7790, 7792 }
local GRASS_EDGE = { 7794, 7796, 7798, 7800, 7802, 7804, 7806, 7936, 7938, 7940, 7942, 7944, 7946, 7948, 7950 }
local DECAL = {
  puddle = { 7952, 7954, 7956 },
  leaves = { 7958, 7960 },
  blood = { 7962, 7964 },
  crack = { 7966, 7968 },
  drain = { 7970 },
  manhole = { 7972 },
  bones = { 7974 },
  straw = { 7976 },
}
local FACADE = {
  brick = {
    plain = { 7978, 7980, 7982, 7984, 7986 },
    left = { 7988, 7990, 7992, 7994, 7996 },
    right = { 7998, 8000, 8002, 8004, 8006 },
    win = { 8008, 8010, 8012, 8014, 8016 },
    win_u = { 8018, 8020, 8022, 8024, 8026 },
    win_g = { 8028, 8030, 8032, 8034, 8036 },
    win_ug = { 8038, 8040, 8042, 8044, 8046 },
    door = { 8048, 8050, 8052, 8054, 8056 },
    door_lit = { 8058, 8060, 8062, 8192, 8194 },
  },
  ashlar = {
    plain = { 8196, 8198, 8200, 8202, 8204 },
    left = { 8206, 8208, 8210, 8212, 8214 },
    right = { 8216, 8218, 8220, 8222, 8224 },
    win = { 8226, 8228, 8230, 8232, 8234 },
    win_u = { 8236, 8238, 8240, 8242, 8244 },
    win_g = { 8246, 8248, 8250, 8252, 8254 },
    win_ug = { 8256, 8258, 8260, 8262, 8264 },
    door = { 8266, 8268, 8270, 8272, 8274 },
    door_lit = { 8276, 8278, 8280, 8282, 8284 },
  },
  stucco = {
    plain = { 8286, 8288, 8290, 8292, 8294 },
    left = { 8296, 8298, 8300, 8302, 8304 },
    right = { 8306, 8308, 8310, 8312, 8314 },
    win = { 8316, 8318, 8448, 8450, 8452 },
    win_u = { 8454, 8456, 8458, 8460, 8462 },
    win_g = { 8464, 8466, 8468, 8470, 8472 },
    win_ug = { 8474, 8476, 8478, 8480, 8482 },
    door = { 8484, 8486, 8488, 8490, 8492 },
    door_lit = { 8494, 8496, 8498, 8500, 8502 },
  },
  dark = {
    plain = { 8504, 8506, 8508, 8510, 8512 },
    left = { 8514, 8516, 8518, 8520, 8522 },
    right = { 8524, 8526, 8528, 8530, 8532 },
    win = { 8534, 8536, 8538, 8540, 8542 },
    win_u = { 8544, 8546, 8548, 8550, 8552 },
    win_g = { 8554, 8556, 8558, 8560, 8562 },
    win_ug = { 8564, 8566, 8568, 8570, 8572 },
    door = { 8574, 8704, 8706, 8708, 8710 },
    door_lit = { 8712, 8714, 8716, 8718, 8720 },
  },
}
local ROOF = {
  slate = { ridge = { l = 8722, m = 8728, r = 8734 }, slope = { l = 8724, m = 8730, r = 8736 }, eaves = { l = 8726, m = 8732, r = 8738 }, slope2 = { l = 8740, m = 8742, r = 8744 } },
  tile = { ridge = { l = 8746, m = 8752, r = 8758 }, slope = { l = 8748, m = 8754, r = 8760 }, eaves = { l = 8750, m = 8756, r = 8762 }, slope2 = { l = 8764, m = 8766, r = 8768 } },
  lead = { ridge = { l = 8770, m = 8776, r = 8782 }, slope = { l = 8772, m = 8778, r = 8784 }, eaves = { l = 8774, m = 8780, r = 8786 }, slope2 = { l = 8788, m = 8790, r = 8792 } },
  copper = { ridge = { l = 8794, m = 8800, r = 8806 }, slope = { l = 8796, m = 8802, r = 8808 }, eaves = { l = 8798, m = 8804, r = 8810 }, slope2 = { l = 8812, m = 8814, r = 8816 } },
}
local SPR = {
  lamp = { 994, 0, 14, 75, 7, 71 },
  lamp_off = { 1009, 0, 14, 75, 7, 71 },
  brazier = { 795, 204, 24, 34, 12, 30 },
  pyre = { 771, 130, 34, 63, 16, 56 },
  candles = { 862, 255, 9, 15, 7, 11 },
  candles2 = { 838, 255, 12, 17, 4, 13 },
  grave_round = { 820, 204, 18, 34, 9, 29 },
  grave_round2 = { 839, 204, 18, 34, 9, 29 },
  grave_cross = { 772, 204, 22, 36, 11, 31 },
  grave_celtic = { 747, 204, 24, 37, 12, 32 },
  grave_obelisk = { 944, 130, 20, 52, 10, 46 },
  grave_broken = { 816, 255, 21, 22, 12, 17 },
  grave_slab = { 892, 204, 22, 30, 11, 18 },
  angel = { 734, 130, 36, 71, 18, 63 },
  mausoleum = { 838, 0, 68, 88, 34, 70 },
  coffin = { 673, 255, 54, 28, 23, 20 },
  coffin_up = { 965, 130, 26, 52, 13, 47 },
  carriage = { 806, 130, 97, 63, 62, 51 },
  barrel = { 915, 204, 24, 30, 12, 24 },
  crate = { 728, 255, 20, 27, 10, 21 },
  crates = { 688, 204, 23, 41, 13, 35 },
  fountain = { 907, 0, 86, 79, 43, 54 },
  well = { 904, 130, 30, 54, 15, 45 },
  bench = { 940, 204, 42, 29, 21, 25 },
  chimney = { 858, 204, 16, 32, 8, 28 },
  chimney1 = { 875, 204, 16, 32, 8, 28 },
  spire = { 673, 0, 44, 129, 22, 117 },
  tree = { 761, 0, 76, 101, 55, 93 },
  tree2 = { 673, 130, 60, 73, 38, 65 },
  tree3 = { 718, 0, 42, 105, 30, 97 },
  bush = { 749, 255, 34, 26, 16, 22 },
  bush2 = { 784, 255, 31, 26, 14, 19 },
  fence_x = { 712, 204, 34, 39, 17, 36 },
  fence_y = { 935, 130, 8, 54, 4, 44 },
  fence_post = { 673, 204, 14, 50, 7, 46 },
  bollard = { 851, 255, 10, 16, 5, 13 },
  cage = { 992, 130, 32, 51, 16, 41 },
}
local PALETTE = {
  0x081828, 0x100818, 0x101018, 0x101818, 0x102018, 0x102840, 0x181010, 0x181018,
  0x181020, 0x181820, 0x182020, 0x182028, 0x182030, 0x183030, 0x201018, 0x202028,
  0x202820, 0x203018, 0x203020, 0x203050, 0x204058, 0x204860, 0x281810, 0x282018,
  0x282030, 0x282830, 0x283040, 0x285048, 0x301818, 0x302018, 0x302828, 0x303038,
  0x303040, 0x303838, 0x304028, 0x304058, 0x381818, 0x382010, 0x382020, 0x383030,
  0x383840, 0x384820, 0x384828, 0x403028, 0x403040, 0x404048, 0x404858, 0x405070,
  0x406880, 0x407060, 0x407088, 0x482820, 0x483020, 0x483820, 0x484850, 0x485050,
  0x485838, 0x502820, 0x504038, 0x505058, 0x582820, 0x583830, 0x585860, 0x585868,
  0x586038, 0x586070, 0x586838, 0x602820, 0x603818, 0x604838, 0x604850, 0x605850,
  0x606068, 0x606878, 0x681018, 0x684828, 0x684830, 0x687068, 0x689880, 0x703828,
  0x704028, 0x706050, 0x706870, 0x707078, 0x785840, 0x786038, 0x786050, 0x787060,
  0x787078, 0x804030, 0x806848, 0x807880, 0x808898, 0x884030, 0x885820, 0x887068,
  0x888070, 0x888890, 0x888898, 0x901820, 0x902020, 0x905038, 0x906840, 0x908068,
  0x908888, 0x909890, 0x987028, 0x98A0B0, 0xA04818, 0xA09070, 0xA09080, 0xA09898,
  0xA0A0A0, 0xA86040, 0xA86048, 0xA87050, 0xB0A890, 0xB0A8A0, 0xB8A080, 0xB8B8B8,
  0xC84810, 0xC8C0B8, 0xC8C8C0, 0xD0C0A8, 0xD86820, 0xD87828, 0xE0A888, 0xE0D0A8,
  0xE0D8C0, 0xE0E0D8, 0xE8E8E0, 0xF0B048, 0xF89828, 0xF8D878, 0xF8D880, 0xF8E888,
  0xF8F0C0, 0xF8F8D0,
}
local GLOWING = { [0xA04818] = true, [0xC84810] = true, [0xD87828] = true, [0xF0B048] = true, [0xF89828] = true, [0xF8D880] = true, [0xF8E888] = true, [0xF8F0C0] = true, [0xF8F8D0] = true }
-- [atlas end]

local W, H = SCREEN_W, SCREEN_H          -- 256 x 256
local TS, CS = 16, 16                    -- tile pixels; chunk tiles (a chunk is one screen)
local CPX = TS * CS
local RING = 4                           -- the map holds 4 x 4 chunks
local RT = RING * CS                     -- ... that is 64 x 64 tiles
local SW8 = SHEET_W // 8                 -- map cells per sheet row
local SEED = 1887
local LEVELS, AMBIENT = 8, 1
local MARGIN = 112                       -- chunks this close to the view are made ahead
local floor, sin, cos, sqrt, abs, min, max = math.floor, math.sin, math.cos, math.sqrt, math.abs, math.min, math.max
local PI = math.pi

----------------------------------------------------------------- numbers

local function hash(a, b, c)
  local h = (a * 374761393 + b * 668265263 + c * 2246822519 + SEED * 3266489917) & 0xFFFFFFFF
  h = ((h ~ (h >> 15)) * 2246822519) & 0xFFFFFFFF
  h = ((h ~ (h >> 13)) * 3266489917) & 0xFFFFFFFF
  return h ~ (h >> 16)
end

local function h01(a, b, c) return hash(a, b, c) / 4294967296.0 end

-- a counter run through a mixing function: every number is well mixed,
-- the first ones after the seed too
local function newrng(seed)
  local s = seed & 0xFFFFFFFF
  return function()
    s = (s + 0x9E3779B9) & 0xFFFFFFFF
    local z = ((s ~ (s >> 16)) * 0x85EBCA6B) & 0xFFFFFFFF
    z = ((z ~ (z >> 13)) * 0xC2B2AE35) & 0xFFFFFFFF
    return (z ~ (z >> 16)) / 4294967296.0
  end
end

local function irange(r, a, b) return a + floor(r() * (b - a + 1)) end
local function pick(r, t) return t[floor(r() * #t) + 1] end
local function key2(a, b) return a * 1000003 + b end
local function fdiv(a, b) return floor(a) // b end   -- an integer, from pixels that may not be

----------------------------------------------------------------- the street plan
-- Streets run between the chunks, from corner to corner: every chunk is a
-- block with a street on each side (a few are missing), winding, of its own
-- width and paving. Corners, sides and districts depend only on their
-- coordinates, so neighbouring chunks agree on the streets they share.

local PV_COBBLE, PV_SETTS, PV_FLAGS, PV_ALLEY, PV_WARM, PV_MARBLE, PV_DARK, PV_WET = 1, 2, 3, 4, 5, 6, 7, 8

local corners, edges, districts = {}, {}, {}

local function corner(i, j)
  local k = key2(i, j)
  local c = corners[k]
  if not c then
    local h = hash(i, j, 1)
    c = { x = i * CS + (h % 5) - 2, y = j * CS + ((h >> 3) % 5) - 2,
          plaza = (h >> 6) % 100 < 18, r = 3.2 + ((h >> 13) % 3) * 0.6 }
    corners[k] = c
  end
  return c
end

-- the street from corner (i, j) eastwards (dir 0) or southwards (dir 1)
local function edge(i, j, dir)
  local k = key2(i, j) * 2 + dir
  local e = edges[k]
  if e == nil then
    local h = hash(i, j, 2 + dir)
    if h % 100 < 12 and not (abs(i) <= 1 and abs(j) <= 1) then
      e = false
    else
      local a = corner(i, j)
      local b = dir == 0 and corner(i + 1, j) or corner(i, j + 1)
      local w = 3 + (h >> 8) % 3
      local p = (h >> 24) % 6
      e = { a = a, b = b, dir = dir, hw = w / 2,
            A = ((h >> 12) % 61) / 10 - 3.0, B = ((h >> 18) % 21) / 10 - 1.0,
            pave = w == 5 and PV_SETTS or (p == 0 and PV_WARM or p == 1 and PV_WET or PV_COBBLE) }
      -- how far across its line the street can reach: tiles further are not on it
      local va, vb = dir == 0 and a.y or a.x, dir == 0 and b.y or b.x
      local reach = abs(e.A) + abs(e.B) + e.hw + 0.5
      e.lo, e.hi = min(va, vb) - reach, max(va, vb) + reach
    end
    edges[k] = e
  end
  return e
end

local function edge_dist(e, x, y)
  local a, b = e.a, e.b
  local u, v, ua, va, ub, vb
  if e.dir == 0 then u, v, ua, va, ub, vb = x, y, a.x, a.y, b.x, b.y
  else u, v, ua, va, ub, vb = y, x, a.y, a.x, b.y, b.x end
  if u < ua or u > ub then
    local du, dv
    if u < ua then du, dv = u - ua, v - va else du, dv = u - ub, v - vb end
    return sqrt(du * du + dv * dv)
  end
  local t = (u - ua) / (ub - ua)
  local c = va + (vb - va) * t + e.A * sin(PI * t) + e.B * sin(2 * PI * t)
  return abs(v - c)
end

local NAME_A = { "Ash", "Raven", "Gaunt", "Black", "Mourn", "Hollow", "Wither", "Crane", "Vigil", "Ember",
  "Gallows", "Iron", "Bell", "Thorn", "Grave", "Lantern", "Crow", "Cinder", "Moth", "Pale", "Weeping",
  "Lamb", "Tallow", "Bone", "Fog", "Wick", "Rook", "Gloam", "Candle", "Mire", "Saint Agnes", "Old Mill" }
local NAME_B = { " Lane", " Row", " Street", " Close", " Walk", " Yard", " Court", " Alley", " Hill", " Way" }
local SAINTS = { "St. Agnes", "St. Cyril", "St. Edith", "St. Ludo", "St. Mora", "St. Bram", "the Vigil" }

local function district(cx, cy)
  local k = key2(cx, cy)
  local d = districts[k]
  if d then return d end
  local h = hash(cx, cy, 7)
  local v = (h % 1000) / 1000
  local kind = "town"
  if cx == 0 and cy == 0 then kind = "square"
  elseif v < 0.10 then kind = "cemetery"
  elseif v < 0.17 then kind = "park"
  elseif v < 0.24 then kind = "square"
  elseif v < 0.29 then kind = "pyre"
  elseif v < 0.34 then kind = "chapel" end
  d = { kind = kind }
  if kind == "square" then d.plaza, d.prx, d.pry, d.pcy = PV_FLAGS, 5.6, 5.0, 8
  elseif kind == "pyre" then d.plaza, d.prx, d.pry, d.pcy = PV_FLAGS, 5.2, 4.6, 8
  elseif kind == "chapel" then d.plaza, d.prx, d.pry, d.pcy = PV_MARBLE, 5.4, 2.4, 13.0 end
  if kind == "town" and (h >> 12) % 100 < 40 then
    d.alley = { dir = (h >> 20) % 2, p = 5 + (h >> 22) % 6, k = 1 + (h >> 25) % 2, ph = (h >> 27) % 6 }
  end
  local a = NAME_A[(h >> 3) % #NAME_A + 1]
  local s = SAINTS[(h >> 9) % #SAINTS + 1]
  if kind == "cemetery" then d.name = a .. " Cemetery"
  elseif kind == "park" then d.name = a .. " Gardens"
  elseif kind == "square" then d.name = a .. " Square"
  elseif kind == "pyre" then d.name = "The " .. a .. " Pyre"
  elseif kind == "chapel" then d.name = "Chapel of " .. s
  else d.name = a .. NAME_B[(h >> 14) % #NAME_B + 1] end
  districts[k] = d
  return d
end

-- the sides of chunk (cx, cy), and the streets that meet at its corners
local function near_edges(cx, cy)
  local t = {}
  local list = {
    { cx, cy, 0 }, { cx, cy + 1, 0 }, { cx, cy, 1 }, { cx + 1, cy, 1 },
    { cx - 1, cy, 0 }, { cx + 1, cy, 0 }, { cx - 1, cy + 1, 0 }, { cx + 1, cy + 1, 0 },
    { cx, cy - 1, 1 }, { cx + 1, cy - 1, 1 }, { cx, cy + 1, 1 }, { cx + 1, cy + 1, 1 },
  }
  for _, l in ipairs(list) do
    local e = edge(l[1], l[2], l[3])
    if e then t[#t + 1] = e end
  end
  return t
end

-- paving of world tile (tx, ty), 0 = not a street
local function road_at(tx, ty, ne)
  local x, y = tx + 0.5, ty + 0.5
  local cx, cy = tx // CS, ty // CS
  for dj = 0, 1 do
    for di = 0, 1 do
      local c = corner(cx + di, cy + dj)
      if c.plaza then
        local dx, dy = x - c.x, y - c.y
        if dx * dx + dy * dy < c.r * c.r then return PV_FLAGS end
      end
    end
  end
  local d = district(cx, cy)
  local lx, ly = x - cx * CS, y - cy * CS
  if d.plaza then
    local dx, dy = (lx - 8) / d.prx, (ly - d.pcy) / d.pry
    if dx * dx + dy * dy < 1 then return d.plaza end
  end
  local al = d.alley
  if al then
    local u, v = lx, ly
    if al.dir == 1 then u, v = ly, lx end
    if abs(v - (al.p + 1.4 * sin(u / CS * PI * al.k + al.ph))) < 1.0 then return PV_ALLEY end
  end
  ne = ne or near_edges(cx, cy)
  for i = 1, #ne do
    local e = ne[i]
    local v = e.dir == 0 and y or x
    if v > e.lo and v < e.hi and edge_dist(e, x, y) < e.hw then return e.pave end
  end
  return 0
end

----------------------------------------------------------------- chunks

local K_FREE, K_ROAD, K_WALK, K_HOUSE, K_YARD, K_GRASS, K_PATH = 0, 1, 2, 3, 4, 5, 6
local FMATS = { "brick", "ashlar", "stucco", "dark", "brick", "stucco" }
local RMATS = { "slate", "tile", "lead", "copper", "slate", "slate", "tile" }
local GRAVES = { "grave_round", "grave_round2", "grave_cross", "grave_celtic", "grave_obelisk", "grave_broken",
  "grave_slab" }

local lamp_state = {}                    -- lamps lit by the hunter: key -> true
local lamps_lit = 0
local chunks, slots = {}, {}

-- objects: { s = sprite, x, y (the base, world pixels), z = sort key, ... }
local function add_obj(ch, name, x, y, z, col)
  local s = SPR[name]
  local o = { s = s, x = x, y = y, z = z or y, name = name }
  ch.objs[#ch.objs + 1] = o
  if col then ch.cols[#ch.cols + 1] = { x = x, y = y + (col.dy or 0), rx = col[1], ry = col[2] } end
  return o
end

local function add_light(ch, x, y, r, lv, fl, dither)
  ch.lights[#ch.lights + 1] = { x = x, y = y, r = r, lv = lv, fl = fl or 0, d = dither or 0.5,
    seed = (x * 7 + y * 13) % 97 }
end

local function gen(ch)
  local cx, cy = ch.cx, ch.cy
  local tx0, ty0 = cx * CS, cy * CS
  local px0, py0 = tx0 * TS, ty0 * TS
  local d = district(cx, cy)
  local r = newrng(hash(cx, cy, 11))
  local ne = near_edges(cx, cy)

  -- the streets over the chunk and one tile around it
  local road = {}
  for ly = -1, CS do
    local row = (ly + 1) * 18 + 2
    for lx = -1, CS do road[row + lx] = road_at(tx0 + lx, ty0 + ly, ne) end
  end
  local function RD(lx, ly) return road[(ly + 1) * 18 + 2 + lx] end

  local kind, solid, A, B = {}, {}, {}, {}
  ch.kind, ch.solid = kind, solid
  for ly = 0, CS - 1 do
    for lx = 0, CS - 1 do
      local i = ly * CS + lx + 1
      solid[i] = false
      if RD(lx, ly) > 0 then
        kind[i] = K_ROAD
      else
        local nr = false
        for dy = -1, 1 do
          for dx = -1, 1 do
            if RD(lx + dx, ly + dy) > 0 then nr = true end
          end
        end
        kind[i] = nr and K_WALK or K_FREE
      end
    end
  end
  local function K(lx, ly) return kind[ly * CS + lx + 1] end
  local function wx(lx) return px0 + lx * TS + 8 end
  local function wy(ly) return py0 + ly * TS + 8 end
  local used = {}                       -- cells taken by a prop
  local function take(lx, ly) used[ly * CS + lx + 1] = true end
  local function freecell(lx, ly, k)
    return lx >= 0 and ly >= 0 and lx < CS and ly < CS and K(lx, ly) == (k or K_FREE)
           and not used[ly * CS + lx + 1]
  end

  ------------------------------------------------- buildings
  local function rect_free(x0, y0, w, h)
    if x0 < 0 or y0 < 0 or x0 + w > CS or y0 + h > CS then return false end
    for y = y0, y0 + h - 1 do
      for x = x0, x0 + w - 1 do
        if kind[y * CS + x + 1] ~= K_FREE then return false end
      end
    end
    return true
  end

  local function building(x0, y0, w, h, mat, roof, big)
    local cells = {}
    for y = y0, y0 + h - 1 do
      for x = x0, x0 + w - 1 do
        kind[y * CS + x + 1] = K_HOUSE
        solid[y * CS + x + 1] = true
      end
    end
    local function T(i, j, c) cells[j * w + i + 1] = c end
    local rows = h - 5
    local door = irange(r, 1, w - 2)
    local lit_p = big and 0.55 or 0.3
    for i = 0, w - 1 do
      local side = i == 0 and "l" or i == w - 1 and "r" or "m"
      local R = ROOF[roof]
      for j = 0, rows - 1 do
        local t
        if j == 0 then t = R.ridge[side]
        elseif j == rows - 1 then t = R.eaves[side]
        else t = (j % 2 == 1) and R.slope[side] or R.slope2[side] end
        T(i, j, t)
      end
      local fk
      if i == 0 then fk = "left"
      elseif i == w - 1 then fk = "right"
      elseif i == door then fk = r() < 0.5 and "door_lit" or "door"
      elseif r() < 0.15 and not big then fk = "plain"
      else
        local u, g = r() < lit_p, r() < lit_p
        fk = (u and g) and "win_ug" or u and "win_u" or g and "win_g" or "win"
      end
      local fc = FACADE[mat][fk]
      for j = 0, 4 do T(i, rows + j, fc[j + 1]) end
      local fx, fb = wx(x0 + i), py0 + (y0 + h) * TS
      if fk == "win_g" or fk == "win_ug" then add_light(ch, fx, fb - 20, 26, 3, 0, 0.7) end
      if fk == "door_lit" then add_light(ch, fx, fb - 6, 24, 3, 0, 0.7) end
    end
    local bottom = py0 + (y0 + h) * TS
    local o = { bld = cells, x = px0 + x0 * TS, y = bottom, top = py0 + y0 * TS, w = w, h = h, z = bottom }
    ch.objs[#ch.objs + 1] = o
    -- chimneys on the ridge, some smoking
    local n = big and 0 or irange(r, 0, 2)
    for k = 1, n do
      local i = irange(r, 0, w - 1)
      local c = add_obj(ch, r() < 0.5 and "chimney" or "chimney1", wx(x0 + i), py0 + y0 * TS + 14, bottom + 0.5)
      if r() < 0.45 then ch.smokes[#ch.smokes + 1] = { x = c.x, y = c.y - 30 } end
    end
    if big then
      add_obj(ch, "spire", px0 + (x0 + w / 2) * TS, py0 + (y0 + 1) * TS + 6, bottom + 0.5)
    end
  end

  local function place_buildings(big)
    if big then
      for yb = CS - 4, 7, -1 do
        for w = 11, 8, -1 do
          for x = 0, CS - w do
            if rect_free(x, yb - 7, w, 8) then
              building(x, yb - 7, w, 8, "dark", "lead", true)
              goto placed
            end
          end
        end
      end
      ::placed::
    end
    for yb = CS - 1, 6, -1 do
      local x = 0
      while x < CS do
        local h = r() < 0.55 and 7 or 8
        if yb - h + 1 >= 0 and rect_free(x, yb - h + 1, 3, h) then
          local w, wmax = 3, irange(r, 4, 7)
          while w < wmax and rect_free(x, yb - h + 1, w + 1, h) do w = w + 1 end
          building(x, yb - h + 1, w, h, pick(r, FMATS), pick(r, RMATS))
          x = x + w + (r() < 0.2 and 1 or 0)
        else
          x = x + 1
        end
      end
    end
  end

  ------------------------------------------------- districts
  local yard_props = { { "tree", 0.025, { 6, 3 } }, { "tree2", 0.02, { 5, 3 } }, { "bush", 0.05, { 9, 4 } },
    { "bush2", 0.04, { 8, 4 } }, { "barrel", 0.035, { 6, 3 } }, { "crates", 0.025, { 6, 3 } },
    { "crate", 0.03, { 6, 3 } }, { "well", 0.006, { 10, 5 } } }

  local function scatter(list, k, ground_cb)
    for ly = 0, CS - 1 do
      for lx = 0, CS - 1 do
        if freecell(lx, ly, k) then
          for _, p in ipairs(list) do
            if r() < p[2] then
              take(lx, ly)
              add_obj(ch, p[1], wx(lx) + irange(r, -3, 3), wy(ly) + irange(r, -2, 4), nil, p[3])
              break
            end
          end
        end
      end
    end
  end

  local kd = d.kind
  if kd == "town" or kd == "chapel" then
    place_buildings(kd == "chapel")
    for i = 1, CS * CS do if kind[i] == K_FREE then kind[i] = K_YARD end end
    scatter(yard_props, K_YARD)
  elseif kd == "cemetery" then
    for i = 1, CS * CS do if kind[i] == K_FREE then kind[i] = K_GRASS end end
    -- gravel paths across, then the fence along the street
    for ly = 0, CS - 1 do
      for lx = 0, CS - 1 do
        if K(lx, ly) == K_GRASS and (lx == 7 or lx == 8 or ly == 8) then kind[ly * CS + lx + 1] = K_PATH end
      end
    end
    -- a mausoleum
    if r() < 0.7 then
      for t = 1, 20 do
        local x, y = irange(r, 1, CS - 6), irange(r, 1, CS - 5)
        local ok = true
        for yy = y, y + 3 do for xx = x, x + 4 do if not freecell(xx, yy, K_GRASS) then ok = false end end end
        if ok then
          for yy = y, y + 3 do for xx = x, x + 4 do take(xx, yy); solid[yy * CS + xx + 1] = yy >= y + 1 end end
          add_obj(ch, "mausoleum", px0 + (x + 2.5) * TS, py0 + (y + 4) * TS - 2)
          add_light(ch, px0 + (x + 2.5) * TS, py0 + (y + 4) * TS + 4, 26, 3, 0.2)
          add_obj(ch, "candles2", px0 + (x + 1) * TS, py0 + (y + 4) * TS + 6)
          break
        end
      end
    end
    for ly = 1, CS - 2, 3 do
      for lx = 1, CS - 2, 2 do
        if freecell(lx, ly, K_GRASS) and r() < 0.8 then
          take(lx, ly)
          local g = pick(r, GRAVES)
          add_obj(ch, g, wx(lx), wy(ly) + 4, nil, { 7, 3 })
          if r() < 0.18 then
            add_obj(ch, r() < 0.5 and "candles" or "candles2", wx(lx) + 8, wy(ly) + 9)
            add_light(ch, wx(lx) + 8, wy(ly) + 6, 30, 4, 0.25, 0.8)
          end
        end
      end
    end
    for k = 1, irange(r, 1, 2) do
      local lx, ly = irange(r, 1, 14), irange(r, 1, 14)
      if freecell(lx, ly, K_GRASS) then take(lx, ly); add_obj(ch, "angel", wx(lx), wy(ly) + 6, nil, { 9, 5 }) end
    end
    scatter({ { "tree3", 0.03, { 5, 3 } }, { "tree", 0.02, { 6, 3 } }, { "bush2", 0.02, { 8, 4 } } }, K_GRASS)
    -- railings where the cemetery meets the pavement, a gap for the paths
    for ly = 0, CS - 1 do
      for lx = 0, CS - 1 do
        local k = K(lx, ly)
        if k == K_GRASS and not used[ly * CS + lx + 1] then
          local n = ly > 0 and K(lx, ly - 1) == K_WALK
          local s = ly < CS - 1 and K(lx, ly + 1) == K_WALK
          local w = lx > 0 and K(lx - 1, ly) == K_WALK
          local e = lx < CS - 1 and K(lx + 1, ly) == K_WALK
          if n or s then
            add_obj(ch, "fence_x", wx(lx), py0 + ly * TS + (n and 3 or 15), nil, { 9, 3 })
          elseif w or e then
            add_obj(ch, "fence_y", px0 + lx * TS + (w and 2 or 14), wy(ly) + 8, nil, { 3, 9 })
          end
        end
      end
    end
  elseif kd == "park" then
    for i = 1, CS * CS do if kind[i] == K_FREE then kind[i] = K_GRASS end end
    for ly = 0, CS - 1 do
      for lx = 0, CS - 1 do
        if K(lx, ly) == K_GRASS and ((lx >= 7 and lx <= 8) or (ly >= 7 and ly <= 8)) then
          kind[ly * CS + lx + 1] = K_PATH
        end
      end
    end
    local cxp, cyp = px0 + 8 * TS, py0 + 8 * TS
    if r() < 0.5 then
      add_obj(ch, "fountain", cxp, cyp + 12, nil, { 34, 10 })
    else
      add_obj(ch, "well", cxp, cyp + 8, nil, { 12, 5 })
    end
    for y = 6, 9 do for x = 5, 10 do take(x, y) end end
    for _, p in ipairs({ { 7, 4 }, { 9, 11 } }) do
      if freecell(p[1], p[2], K_PATH) then take(p[1], p[2]); ch.lampspots[#ch.lampspots + 1] = p end
    end
    scatter({ { "tree", 0.05, { 6, 3 } }, { "tree2", 0.04, { 5, 3 } }, { "tree3", 0.03, { 5, 3 } },
              { "bush", 0.07, { 9, 4 } }, { "bush2", 0.06, { 8, 4 } } }, K_GRASS)
    scatter({ { "bench", 0.06, { 12, 3 } } }, K_PATH)
  elseif kd == "square" or kd == "pyre" or kd == "chapel" then
    -- (the chapel's front is the marble square; see the buildings above)
  end
  if kd == "square" then
    local cxp, cyp = px0 + 8 * TS, py0 + 8 * TS
    if r() < 0.6 or (cx == 0 and cy == 0) then
      add_obj(ch, "fountain", cxp, cyp + 12, nil, { 34, 10 })
    else
      add_obj(ch, "angel", cxp, cyp + 8, nil, { 10, 5 })
    end
    local first = irange(r, 0, 1)
    for k = first, 3, 2 do
      local a = k * PI / 2 + PI / 4
      local bx, by = cxp + cos(a) * 62, cyp + sin(a) * 50 + 10
      if road_at((fdiv(bx, TS)), (fdiv(by, TS)), ne) > 0 then
        add_obj(ch, "brazier", bx, by, nil, { 8, 4 })
        ch.fires[#ch.fires + 1] = { x = bx, y = by - 25, w = 7, rate = 1.2, big = false }
        add_light(ch, bx, by - 8, 62, 6, 1.0)
      end
    end
    if r() < 0.5 then add_obj(ch, "bench", cxp - 48, cyp - 40, nil, { 12, 3 }) end
  elseif kd == "pyre" then
    local cxp, cyp = px0 + 8 * TS, py0 + 8 * TS
    add_obj(ch, "pyre", cxp, cyp + 14, nil, { 16, 7 })
    ch.fires[#ch.fires + 1] = { x = cxp, y = cyp + 4, w = 14, rate = 4.0, big = true }
    add_light(ch, cxp, cyp + 4, 112, 7, 1.0, 0.6)
    add_light(ch, cxp, cyp, 72, 7, 1.0, 0.3)
    local spots = {}
    for k = 1, 10 do
      local a = r() * 2 * PI
      local dd = 50 + r() * 22
      local ox, oy = cxp + cos(a) * dd, cyp + 14 + sin(a) * dd * 0.7
      local ok = road_at(fdiv(ox, TS), fdiv(oy, TS), ne) > 0 and #spots < 6
      for _, q in ipairs(spots) do
        if abs(q[1] - ox) < 40 and abs(q[2] - oy) < 22 then ok = false end
      end
      if ok then
        spots[#spots + 1] = { ox, oy }
        local name = pick(r, { "coffin", "coffin", "coffin_up", "cage", "barrel" })
        add_obj(ch, name, ox, oy, nil, name == "coffin" and { 14, 4 } or { 8, 4 })
      end
    end
  elseif kd == "chapel" then
    for k = -1, 1, 2 do
      local bx, by = px0 + (8 + k * 3.6) * TS, py0 + 13.6 * TS
      if road_at(fdiv(bx, TS), fdiv(by, TS), ne) > 0 then
        add_obj(ch, "brazier", bx, by, nil, { 8, 4 })
        ch.fires[#ch.fires + 1] = { x = bx, y = by - 25, w = 7, rate = 1.2 }
        add_light(ch, bx, by - 8, 60, 6, 1.0)
      end
    end
    local ax, ay = px0 + 8 * TS, py0 + 13.2 * TS
    if road_at(fdiv(ax, TS), fdiv(ay, TS), ne) > 0 then add_obj(ch, "angel", ax, ay + 8, nil, { 9, 5 }) end
  end

  ------------------------------------------------- street furniture
  -- gas lamps along the pavements, some of them dark
  for ly = 0, CS - 1 do
    for lx = 0, CS - 1 do
      if K(lx, ly) == K_WALK and not used[ly * CS + lx + 1] and h01(tx0 + lx, ty0 + ly, 31) < 0.08 then
        ch.lampspots[#ch.lampspots + 1] = { lx, ly }
      end
    end
  end
  local placed = {}
  for _, p in ipairs(ch.lampspots) do
    local ok = true
    for _, q in ipairs(placed) do
      if abs(q[1] - p[1]) + abs(q[2] - p[2]) < 8 then ok = false end
    end
    if ok then
      placed[#placed + 1] = p
      take(p[1], p[2])
      local x, y = wx(p[1]), wy(p[2]) + 4
      local key = key2(tx0 + p[1], ty0 + p[2])
      local o = add_obj(ch, "lamp", x, y, nil, { 4, 2 })
      o.lamp = key
      if lamp_state[key] == nil then lamp_state[key] = h01(tx0 + p[1], ty0 + p[2], 33) < 0.72 end
      ch.lamps[#ch.lamps + 1] = { x = x, y = y, key = key }
    end
  end
  -- odds and ends on the pavements and in the street
  for ly = 0, CS - 1 do
    for lx = 0, CS - 1 do
      if freecell(lx, ly, K_WALK) then
        local v = r()
        local below = ly > 0 and K(lx, ly - 1) == K_HOUSE
        if v < 0.03 and below then
          take(lx, ly); add_obj(ch, pick(r, { "barrel", "crate", "crates" }), wx(lx), wy(ly), nil, { 6, 3 })
        elseif v < 0.037 then
          take(lx, ly); add_obj(ch, "bollard", wx(lx), wy(ly) + 4, nil, { 3, 2 })
        elseif v < 0.055 then
          take(lx, ly); add_obj(ch, "coffin_up", wx(lx), wy(ly), nil, { 7, 3 })
        end
      end
    end
  end
  if kd == "town" and r() < 0.22 then
    -- an abandoned carriage, where the street is wide enough
    for t = 1, 30 do
      local lx, ly = irange(r, 0, CS - 4), irange(r, 1, CS - 2)
      local ok = true
      for yy = ly - 1, ly + 1 do for xx = lx, lx + 3 do if not freecell(xx, yy, K_ROAD) then ok = false end end end
      if ok then
        for yy = ly - 1, ly + 1 do for xx = lx, lx + 3 do take(xx, yy) end end
        add_obj(ch, "carriage", px0 + (lx + 1.6) * TS, wy(ly) + 4, nil, { 34, 6 })
        break
      end
    end
  end
  if kd == "town" and r() < 0.25 then
    for t = 1, 12 do
      local lx, ly = irange(r, 0, CS - 1), irange(r, 0, CS - 1)
      if freecell(lx, ly, K_ROAD) then take(lx, ly); add_obj(ch, "coffin", wx(lx), wy(ly) + 4, nil, { 14, 4 }); break end
    end
  end

  ------------------------------------------------- the ground, overlays
  local G = GROUND
  for ly = 0, CS - 1 do
    for lx = 0, CS - 1 do
      local i = ly * CS + lx + 1
      local k = kind[i]
      local tx, ty = tx0 + lx, ty0 + ly
      local hv = hash(tx, ty, 21)
      if k == K_ROAD then
        local pv = RD(lx, ly)
        local set = pv == PV_SETTS and G.setts or pv == PV_FLAGS and G.flags or pv == PV_ALLEY and G.cobble_moss
          or pv == PV_WARM and G.cobble_warm or pv == PV_MARBLE and G.marble or pv == PV_DARK and G.flags_dark
          or pv == PV_WET and G.cobble_wet or G.cobble
        A[i] = set[hv % #set + 1]
        -- the kerb where the street meets the pavement
        local m = 0
        if RD(lx, ly - 1) == 0 then m = m | 1 end
        if RD(lx + 1, ly) == 0 then m = m | 2 end
        if RD(lx, ly + 1) == 0 then m = m | 4 end
        if RD(lx - 1, ly) == 0 then m = m | 8 end
        if m > 0 and pv ~= PV_MARBLE then
          B[i] = CURB[m]
        elseif (hv >> 8) % 100 < 9 then
          local dv = (hv >> 16) % 100
          local set2 = dv < 30 and DECAL.puddle or dv < 48 and DECAL.leaves or dv < 62 and DECAL.crack
            or dv < 72 and DECAL.drain or dv < 78 and DECAL.manhole or dv < 88 and DECAL.blood
            or dv < 95 and DECAL.straw or DECAL.bones
          B[i] = set2[(hv >> 24) % #set2 + 1]
        end
      elseif k == K_WALK then
        A[i] = G.pave[hv % #G.pave + 1]
        if (hv >> 8) % 100 < 6 then
          local set2 = ((hv >> 16) % 2 == 0) and DECAL.leaves or DECAL.crack
          B[i] = set2[(hv >> 24) % #set2 + 1]
        end
      elseif k == K_HOUSE then
        A[i] = G.dirt[1]                   -- never seen: the house is drawn over it
      elseif k == K_GRASS then
        local set = kd == "cemetery" and G.grass_dark or G.grass
        A[i] = set[hv % #set + 1]
      elseif k == K_PATH then
        A[i] = G.gravel[hv % #G.gravel + 1]
      else   -- yards: grass, earth and gravel in patches
        local n = sin(tx * 0.9 + ty * 0.4) + sin(ty * 1.1 - tx * 0.3) + ((hv % 100) / 100 - 0.5)
        if n > 0.5 then A[i] = G.grass[hv % #G.grass + 1]; kind[i] = K_GRASS
        elseif n > -0.6 then A[i] = G.dirt[hv % #G.dirt + 1]
        else A[i] = G.gravel[hv % #G.gravel + 1] end
      end
    end
  end
  -- grass spilling over its neighbours (inside the chunk)
  for ly = 0, CS - 1 do
    for lx = 0, CS - 1 do
      local i = ly * CS + lx + 1
      local k = kind[i]
      if k ~= K_GRASS and k ~= K_HOUSE and not B[i] then
        local m = 0
        if ly > 0 and K(lx, ly - 1) == K_GRASS then m = m | 1 end
        if lx < CS - 1 and K(lx + 1, ly) == K_GRASS then m = m | 2 end
        if ly < CS - 1 and K(lx, ly + 1) == K_GRASS then m = m | 4 end
        if lx > 0 and K(lx - 1, ly) == K_GRASS then m = m | 8 end
        if m > 0 then B[i] = GRASS_EDGE[m] end
      end
    end
  end

  -- into the map (layer 1: cells 0-127, layer 2: 128-255)
  local rx0, ry0 = (cx % RING) * CS * 2, (cy % RING) * CS * 2
  for ly = 0, CS - 1 do
    local my = ry0 + ly * 2
    for lx = 0, CS - 1 do
      local i = ly * CS + lx + 1
      local mx = rx0 + lx * 2
      local a, b = A[i] or G.cobble[1], B[i] or 0
      mset(mx, my, a); mset(mx + 1, my, a + 1); mset(mx, my + 1, a + SW8); mset(mx + 1, my + 1, a + SW8 + 1)
      if b > 0 then
        mset(128 + mx, my, b); mset(129 + mx, my, b + 1); mset(128 + mx, my + 1, b + SW8)
        mset(129 + mx, my + 1, b + SW8 + 1)
      else
        mset(128 + mx, my, 0); mset(129 + mx, my, 0); mset(128 + mx, my + 1, 0); mset(129 + mx, my + 1, 0)
      end
    end
  end
  ch.ready = true
end

local function new_chunk(cx, cy)
  local k = key2(cx, cy)
  local sk = (cy % RING) * RING + (cx % RING)
  local old = slots[sk]
  if old and old ~= k then chunks[old] = nil end
  slots[sk] = k
  local ch = { cx = cx, cy = cy, ready = false, objs = {}, cols = {}, lights = {}, fires = {}, smokes = {},
               lamps = {}, lampspots = {} }
  chunks[k] = ch
  return ch
end

local function chunk(cx, cy) return chunks[key2(cx, cy)] end

-- one chunk at a time is made in the background, a slice per frame
local job, job_ch
local gen_frames = 0

local function finish_job()
  while job and coroutine.status(job) ~= "dead" do
    local ok, err = coroutine.resume(job)
    if not ok then error(err) end
  end
  job, job_ch = nil, nil
end

local function ensure(cx, cy)
  local ch = chunk(cx, cy)
  if ch and ch.ready then return ch end
  if job_ch and job_ch.cx == cx and job_ch.cy == cy then
    finish_job()
    return job_ch or chunk(cx, cy)
  end
  ch = new_chunk(cx, cy)
  gen(ch)
  return ch
end

local cam_x, cam_y = 0, 0

local function stream()
  -- what is on screen has to be there now
  for cy = fdiv(cam_y, CPX), fdiv(cam_y + H - 1, CPX) do
    for cx = fdiv(cam_x, CPX), fdiv(cam_x + W - 1, CPX) do ensure(cx, cy) end
  end
  if job then
    local ok, err = coroutine.resume(job)
    if not ok then error(err) end
    gen_frames = gen_frames + 1
    if coroutine.status(job) == "dead" then job, job_ch = nil, nil end
    return
  end
  -- the next one around the view, nearest first
  local best, bd
  local mx, my = cam_x + W / 2, cam_y + H / 2
  for cy = fdiv(cam_y - MARGIN, CPX), fdiv(cam_y + H + MARGIN, CPX) do
    for cx = fdiv(cam_x - MARGIN, CPX), fdiv(cam_x + W + MARGIN, CPX) do
      local ch = chunk(cx, cy)
      if not (ch and ch.ready) then
        local dx, dy = (cx + 0.5) * CPX - mx, (cy + 0.5) * CPX - my
        local dd = dx * dx + dy * dy
        if not bd or dd < bd then best, bd = { cx, cy }, dd end
      end
    end
  end
  if best then
    job_ch = new_chunk(best[1], best[2])
    job = coroutine.create(gen)
    timeslice(job, 40)
    local ok, err = coroutine.resume(job, job_ch)
    if not ok then error(err) end
    if coroutine.status(job) == "dead" then job, job_ch = nil, nil end
  end
end

----------------------------------------------------------------- the hunter

local DIRS = { [-1] = { [-1] = 5, [0] = 4, [1] = 3 }, [0] = { [-1] = 6, [0] = 0, [1] = 2 },
               [1] = { [-1] = 7, [0] = 0, [1] = 1 } }          -- [dy][dx]: S SE E NE N NW W SW
local P = { x = 0, y = 0, dir = 0, frame = 0, tick = 0, moving = false, step = 0 }
local IDLE_T = { 26, 16, 26, 16 }

local function solid_at(x, y)
  local tx, ty = fdiv(x, TS), fdiv(y, TS)
  local ch = chunk(tx // CS, ty // CS)
  if not (ch and ch.ready) then return true end
  return ch.solid[(ty % CS) * CS + (tx % CS) + 1]
end

local function blocked(x, y)
  if solid_at(x - 6, y - 3) or solid_at(x + 6, y - 3) or solid_at(x - 6, y + 2) or solid_at(x + 6, y + 2) then
    return true
  end
  local pcx, pcy = fdiv(x, CPX), fdiv(y, CPX)
  for cy = pcy - 1, pcy + 1 do
    for cx = pcx - 1, pcx + 1 do
      local ch = chunk(cx, cy)
      if ch and ch.ready then
        for _, c in ipairs(ch.cols) do
          local dx, dy = (x - c.x) / (c.rx + 5), (y - c.y) / (c.ry + 3)
          if dx * dx + dy * dy < 1 then return true end
        end
      end
    end
  end
  return false
end

-- the free spot of a street nearest to the middle of the screen
local function start_here()
  local mx, my = cam_x + W / 2, cam_y + H * 0.62
  for rad = 0, 12 do
    for dy = -rad, rad do
      for dx = -rad, rad do
        if abs(dx) == rad or abs(dy) == rad then
          local x, y = (fdiv(mx, TS) + dx) * TS + 8, (fdiv(my, TS) + dy) * TS + 8
          local tx, ty = fdiv(x, TS), fdiv(y, TS)
          local ch = chunk(tx // CS, ty // CS)
          if ch and ch.ready and ch.kind[(ty % CS) * CS + tx % CS + 1] == K_ROAD and not blocked(x, y) then
            P.x, P.y = x, y
            return
          end
        end
      end
    end
  end
end

local function place_player()
  -- the start: on a street of the first square, south of its fountain
  ensure(0, 0)
  for rad = 0, 10 do
    for dy = -rad, rad do
      for dx = -rad, rad do
        local x, y = (8 + dx) * TS + 8, (12 + dy) * TS + 8
        if not blocked(x, y) then P.x, P.y = x, y; return end
      end
    end
  end
  P.x, P.y = 8 * TS + 8, 12 * TS + 8
end

----------------------------------------------------------------- particles

local parts = {}
local MAXP = 200
local FLAME = { 0xF8F8D8, 0xF8E890, 0xF8C050, 0xF89030, 0xE06020, 0xA83818, 0x602010 }

local function spawn(x, y, vx, vy, life, kind)
  if #parts >= MAXP then return end
  parts[#parts + 1] = { x = x, y = y, vx = vx, vy = vy, t = 0, life = life, kind = kind }
end

local function update_parts()
  local n = #parts
  local i = 1
  while i <= n do
    local p = parts[i]
    p.t = p.t + 1
    if p.t >= p.life then
      parts[i] = parts[n]
      parts[n] = nil
      n = n - 1
    else
      p.x = p.x + p.vx
      p.y = p.y + p.vy
      if p.kind == 1 then
        p.vx = p.vx * 0.94 + (math.random() - 0.5) * 0.12
        p.vy = p.vy * 0.985 - 0.012
      elseif p.kind == 2 then
        p.vx = p.vx + (math.random() - 0.5) * 0.08
        p.vy = p.vy * 0.995 - 0.004
      elseif p.kind == 3 then
        p.vx = p.vx * 0.99 + 0.004
        p.vy = p.vy * 0.995
      else
        p.vy = p.vy + 0.03
      end
      i = i + 1
    end
  end
end

----------------------------------------------------------------- sound

local function sfx_step() note(0, 70 + math.random(30), 45, NOISE, 22) end
local function sfx_light()
  note(1, 523, 120, TRIANGLE, 70); note(2, 784, 400, TRIANGLE, 50); note(3, 2400, 300, NOISE, 25)
end
local bell_t, wind_t, crackle_t = 240, 60, 0
local CHORDS = { { 110, 131, 165 }, { 98, 117, 147 }, { 87, 110, 131 }, { 82, 104, 123 } }
local chord_i, chord_t = 0, 0

local function update_sound(fire_near)
  bell_t = bell_t - 1
  if bell_t <= 0 then
    bell_t = 1500 + math.random(900)
    note(7, 196, 3200, METAL, 30)
    note(6, 98, 3600, SINE, 34)
  end
  wind_t = wind_t - 1
  if wind_t <= 0 then
    wind_t = 200 + math.random(260)
    note(5, 300 + math.random(200), 2600, NOISE, 9)
  end
  chord_t = chord_t - 1
  if chord_t <= 0 then
    chord_i = chord_i % #CHORDS + 1
    chord_t = 300
    local c = CHORDS[chord_i]
    note(4, c[1], 5200, TRIANGLE, 22)
    note(3, c[2 + chord_i % 2], 5000, SINE, 14)
  end
  if fire_near and fire_near < 140 then
    crackle_t = crackle_t - 1
    if crackle_t <= 0 then
      crackle_t = 3 + math.random(14)
      note(2, 1400 + math.random(2400), 25 + math.random(30), NOISE, floor(60 * (1 - fire_near / 140)))
    end
  end
end

----------------------------------------------------------------- the frame


local state = "title"
local t = 0
local banner, banner_t, here = nil, 0, nil
local near_lamp

local function lamp_at(key) return lamp_state[key] end

local function update_play()
  local dx, dy = 0, 0
  if btn(0) then dx = dx - 1 end
  if btn(1) then dx = dx + 1 end
  if btn(2) then dy = dy - 1 end
  if btn(3) then dy = dy + 1 end
  local run = btn(5)
  P.moving = dx ~= 0 or dy ~= 0
  if P.moving then
    P.dir = DIRS[dy][dx]
    local sp = run and 1.55 or 0.92
    if dx ~= 0 and dy ~= 0 then sp = sp * 0.7071 end
    local nx, ny = P.x + dx * sp, P.y + dy * sp
    local mx, my = false, false
    if not blocked(nx, P.y) then P.x, mx = nx, true end
    if not blocked(P.x, ny) then P.y, my = ny, true end
    -- slide around posts and corners
    if dx ~= 0 and not mx and dy == 0 then
      for _, o in ipairs({ -1, 1 }) do
        if not blocked(nx, P.y + o * sp) then P.x, P.y = nx, P.y + o * sp; break end
      end
    elseif dy ~= 0 and not my and dx == 0 then
      for _, o in ipairs({ -1, 1 }) do
        if not blocked(P.x + o * sp, ny) then P.x, P.y = P.x + o * sp, ny; break end
      end
    end
    P.tick = P.tick + 1
    if P.tick >= (run and 4 or 6) then
      P.tick = 0
      P.frame = (P.frame + 1) % 8
      if P.frame == 2 or P.frame == 6 then sfx_step() end
    end
    P.idle, P.idle_t = 0, 0
  else
    P.idle_t = (P.idle_t or 0) + 1
    if P.idle_t >= IDLE_T[(P.idle or 0) + 1] then P.idle_t = 0; P.idle = ((P.idle or 0) + 1) % 4 end
    P.frame = 0
  end
  -- the camera follows, a little ahead of the hunter
  local tx, ty = P.x - W / 2, P.y - H * 0.62
  cam_x = cam_x + (tx - cam_x) * 0.12
  cam_y = cam_y + (ty - cam_y) * 0.12
  -- a new district: its name
  local pcx, pcy = fdiv(P.x, CPX), fdiv(P.y, CPX)
  local d = district(pcx, pcy)
  if d ~= here then
    here = d
    banner, banner_t = d.name, 200
  end
  if banner_t > 0 then banner_t = banner_t - 1 end
  -- lamps in reach
  near_lamp = nil
  for cy = pcy - 1, pcy + 1 do
    for cx = pcx - 1, pcx + 1 do
      local ch = chunk(cx, cy)
      if ch and ch.ready then
        for _, l in ipairs(ch.lamps) do
          if not lamp_state[l.key] and abs(l.x - P.x) < 22 and abs(l.y - P.y) < 18 then near_lamp = l end
        end
      end
    end
  end
  if near_lamp and btnp(4) then
    lamp_state[near_lamp.key] = true
    lamps_lit = lamps_lit + 1
    sfx_light()
    for k = 1, 24 do
      local a = math.random() * 2 * PI
      spawn(near_lamp.x, near_lamp.y - 66, cos(a) * 0.8, sin(a) * 0.8 - 0.6, 30 + math.random(20), 4)
    end
  end
end

-- for the tests on the PC (tests/yharnam/sim.lua)
YHARNAM = { road_at = road_at, district = district, chunk = chunk, ensure = ensure, player = P,
            blocked = blocked, lamps = lamp_state, kinds = { road = K_ROAD, walk = K_WALK, house = K_HOUSE },
            camera = function() return cam_x, cam_y end, CS = CS, TS = TS, SPR = SPR, FACADE = FACADE,
            teleport = function(x, y)
              P.x, P.y = x, y
              cam_x, cam_y = x - W / 2, y - H * 0.62
              state = "play"
            end }

function _init()
  -- the fade tables: every colour of the sheet at the 8 light levels.
  -- Level 1 is the night (dim, blue: the twilight of a dead town), 7 the
  -- warm light under a lamp; glass of lit windows, flames and embers keep
  -- their colour at every level: they make their own light.
  local rows = {}
  for _, c in ipairs(PALETTE) do
    local row = { c }
    local r, g, b = c >> 16, (c >> 8) & 255, c & 255
    local lum = 0.30 * r + 0.59 * g + 0.11 * b
    for k = 0, LEVELS - 1 do
      local o = c
      if not GLOWING[c] then
        local nr, ng, nb = r * 0.13 + lum * 0.03, g * 0.14 + lum * 0.05, b * 0.20 + lum * 0.12
        if k == 0 then nr, ng, nb = nr * 0.45, ng * 0.45, nb * 0.55 end
        local wr, wg, wb = min(255, r * 1.10 + 8), min(255, g * 0.97 + 2), b * 0.76
        local f = k <= 1 and 0 or ((k - 1) / (LEVELS - 2)) ^ 1.15
        local fr, fg, fb = nr + (wr - nr) * f, ng + (wg - ng) * f, nb + (wb - nb) * f
        o = ((floor(fr) // 8 * 8) << 16) | ((floor(fg) // 8 * 8) << 8) | (floor(fb) // 8 * 8)
      end
      row[k + 2] = o
    end
    rows[#rows + 1] = row
  end
  fades(rows)
  place_player()
  cam_x, cam_y = P.x - W / 2, P.y - H * 0.62
end

function _update()
  t = t + 1
  if state == "title" then
    -- after a moment the camera drifts over the town: the hunter starts
    -- wherever it is when A is pressed
    if t > 150 then
      cam_x, cam_y = cam_x + 0.9, cam_y + 0.45
    end
    if btnp(4) or btnp(5) then
      state = "play"
      if t > 150 then start_here() end
    end
    P.idle_t = (P.idle_t or 0) + 1
    if P.idle_t >= IDLE_T[(P.idle or 0) + 1] then P.idle_t = 0; P.idle = ((P.idle or 0) + 1) % 4 end
  else
    update_play()
  end
  stream()
  update_parts()
end

----------------------------------------------------------------- drawing

local function draw_map()
  local tx0, ty0 = fdiv(cam_x, TS), fdiv(cam_y, TS)
  local tx1, ty1 = fdiv(cam_x + W - 1, TS), fdiv(cam_y + H - 1, TS)
  local ty = ty0
  while ty <= ty1 do
    local ry = ty % RT
    local nty = min(ty1 - ty + 1, RT - ry)
    local tx = tx0
    while tx <= tx1 do
      local rx = tx % RT
      local ntx = min(tx1 - tx + 1, RT - rx)
      map(rx * 2, ry * 2, tx * TS, ty * TS, ntx * 2, nty * 2)
      map(128 + rx * 2, ry * 2, tx * TS, ty * TS, ntx * 2, nty * 2)
      tx = tx + ntx
    end
    ty = ty + nty
  end
end

local vis = {}
local function visible_chunks(margin)
  local out = {}
  for cy = fdiv(cam_y - margin, CPX), fdiv(cam_y + H + margin, CPX) do
    for cx = fdiv(cam_x - margin, CPX), fdiv(cam_x + W + margin, CPX) do
      local ch = chunk(cx, cy)
      if ch and ch.ready then out[#out + 1] = ch end
    end
  end
  return out
end

local function draw_scene()
  camera(cam_x, cam_y)
  draw_map()
  -- objects and the hunter, back to front
  local n = 0
  local x0, y0, x1, y1 = cam_x - 64, cam_y - 16, cam_x + W + 64, cam_y + H + 140
  local near = visible_chunks(140)
  for _, ch in ipairs(near) do
    for _, o in ipairs(ch.objs) do
      if o.bld then
        if o.x < cam_x + W and o.x + o.w * TS > cam_x and o.top < cam_y + H and o.y > cam_y then
          n = n + 1; vis[n] = o
        end
      elseif o.x > x0 and o.x < x1 and o.y > y0 and o.y < y1 then
        n = n + 1; vis[n] = o
      end
    end
  end
  if state ~= "title" or t <= 150 then
    n = n + 1
    vis[n] = P
    P.z = P.y
  end
  for i = n + 1, #vis do vis[i] = nil end
  table.sort(vis, function(a, b) return (a.z or a.y) < (b.z or b.y) end)
  for i = 1, n do
    local o = vis[i]
    if o.bld then
      local c = o.bld
      local i0, i1 = max(0, fdiv(cam_x - o.x, TS)), min(o.w - 1, fdiv(cam_x + W - 1 - o.x, TS))
      local j0, j1 = max(0, fdiv(cam_y - o.top, TS)), min(o.h - 1, fdiv(cam_y + H - 1 - o.top, TS))
      for j = j0, j1 do
        local y = o.top + j * TS
        for i = i0, i1 do spr(c[j * o.w + i + 1], o.x + i * TS, y, 2, 2) end
      end
    elseif o == P then
      local col, row = P.moving and (4 + P.frame) or (P.idle or 0), P.dir
      sspr(col * HUNTER.w, row * HUNTER.h, HUNTER.w, HUNTER.h, floor(P.x) - HUNTER.ax, floor(P.y) - HUNTER.ay)
    else
      local s = o.s
      if o.lamp and not lamp_state[o.lamp] then s = SPR.lamp_off end
      sspr(s[1], s[2], s[3], s[4], floor(o.x) - s[5], floor(o.y) - s[6])
    end
  end
  -- smoke: drawn before the lights, so only fires and lamps show it
  for _, p in ipairs(parts) do
    if p.kind == 3 then
      local a = p.t / p.life
      circfill(p.x, p.y, 1 + a * 4, a < 0.5 and 0x585058 or 0x403840)
    end
  end
  -- the lights
  dark_begin(AMBIENT)
  local fire_near
  for _, ch in ipairs(near) do
    for _, l in ipairs(ch.lights) do
      local rr = l.r
      if l.fl > 0 then
        rr = rr * (1 - l.fl * 0.08 + l.fl * 0.05 * sin(t * 0.31 + l.seed) + l.fl * 0.04 * sin(t * 0.73 + l.seed * 3))
      end
      if l.x + rr > cam_x and l.x - rr < cam_x + W and l.y + rr > cam_y and l.y - rr < cam_y + H then
        glow(l.x, l.y, rr, l.lv, l.d)
      end
    end
    for _, l in ipairs(ch.lamps) do
      if lamp_state[l.key] then
        local rr = 64 + sin(t * 0.05 + l.x) * 1.0
        if l.x + rr > cam_x and l.x - rr < cam_x + W and l.y + rr > cam_y and l.y - rr < cam_y + H then
          glow(l.x, l.y - 6, rr, 7, 0.5)
        end
      end
    end
    for _, f in ipairs(ch.fires) do
      local dd = abs(f.x - P.x) + abs(f.y - P.y)
      if not fire_near or dd < fire_near then fire_near = dd end
    end
  end
  glow(P.x, P.y - 14, 42, 4, 0.7)               -- what the hunter's eyes make out
  dark_end()
  -- flames and embers make their own light: drawn after it. First the
  -- white-hot heart of every fire, then the tongues and sparks
  for _, ch in ipairs(near) do
    for _, f in ipairs(ch.fires) do
      if f.x > cam_x - 32 and f.x < cam_x + W + 32 and f.y > cam_y - 32 and f.y < cam_y + H + 32 then
        local fl = sin(t * 0.37 + f.x) + sin(t * 0.91 + f.y)
        if f.big then
          circfill(f.x, f.y + 4, 9 + fl, 0xA83818)
          circfill(f.x - 3, f.y + 2, 6 + fl * 0.5, 0xE06020)
          circfill(f.x + 4, f.y + 3, 5 - fl * 0.5, 0xF89030)
          circfill(f.x, f.y, 4 + fl * 0.4, 0xF8C050)
          circfill(f.x, f.y - 1, 2, 0xF8F8D8)
        else
          circfill(f.x, f.y + 3, 4 + fl * 0.3, 0xE06020)
          circfill(f.x, f.y + 2, 3, 0xF8C050)
          rectfill(f.x - 1, f.y + 1, 2, 2, 0xF8F8D8)
        end
      end
    end
  end
  for _, p in ipairs(parts) do
    local k = p.kind
    if k == 1 then
      local a = p.t / p.life
      local c = FLAME[min(#FLAME, 1 + floor(a * #FLAME))]
      if a < 0.22 then rectfill(p.x - 1, p.y - 1, 3, 3, c)
      elseif a < 0.5 then rectfill(p.x, p.y, 2, 2, c)
      else pset(p.x, p.y, c) end
    elseif k == 2 then
      if (p.t // 3 + p.life) % 4 ~= 0 then pset(p.x, p.y, p.t < p.life * 0.6 and 0xF8B040 or 0xC05020) end
    elseif k == 4 then
      pset(p.x, p.y, 0xF8E8A0)
    end
  end
  camera()
  return fire_near
end

local function emit()
  for _, ch in ipairs(visible_chunks(48)) do
    for _, f in ipairs(ch.fires) do
      f.acc = (f.acc or 0) + f.rate
      while f.acc >= 1 do
        f.acc = f.acc - 1
        local x = f.x + (math.random() - 0.5) * 2 * f.w
        local y = f.y + (math.random() - 0.5) * (f.big and 16 or 4)
        spawn(x, y, (math.random() - 0.5) * 0.3, -0.5 - math.random() * (f.big and 1.1 or 0.7),
              14 + math.random(f.big and 30 or 16), 1)
        if math.random() < (f.big and 0.12 or 0.05) then
          spawn(x, y - 6, (math.random() - 0.5) * 0.6, -0.6 - math.random() * 0.8, 50 + math.random(60), 2)
        end
        if f.big and math.random() < 0.08 then
          spawn(f.x + (math.random() - 0.5) * 10, f.y - 40, 0.1, -0.35, 140, 3)
        end
      end
    end
    for _, s in ipairs(ch.smokes) do
      if math.random() < 0.06 then spawn(s.x + math.random(-1, 1), s.y, 0.05, -0.22 - math.random() * 0.1, 160, 3) end
    end
  end
end

local function shadow_print(s, x, y, c)
  print(s, x + 1, y + 1, 0x000000)
  print(s, x, y, c)
end

function _draw()
  emit()
  local fire_near = draw_scene()
  if state == "title" then
    if t <= 150 then
      rectfill(0, 16, W, 32, 0x000000)
      print("YHARNAM", 96, 16, 0xE8C878)
      print("a town of endless night", 40, 32, 0x8890A0)
      rectfill(0, 208, W, 32, 0x000000)
      print("arrows walk  B run", 56, 224, 0x8890A0)
    else
      rectfill(0, 208, W, 16, 0x000000)
    end
    print("A: START", 96, 208, (t // 30) % 2 == 0 and 0xE8E0D0 or 0x988870)
  else
    update_sound(fire_near)
    if banner_t > 0 and banner then
      local w = #banner * 8
      local x = (W - w) // 2 // 8 * 8
      rectfill(x - 8, 16, w + 16, 16, 0x000000)
      print(banner, x, 16, banner_t > 30 and 0xD8C8A0 or 0x786850)
    end
    if near_lamp then
      rectfill(48, 224, 160, 16, 0x000000)
      print("A: light the lamp", 56, 224, 0xE8C878)
    end
    if lamps_lit > 0 then
      shadow_print("lamps " .. lamps_lit, 8, 240, 0xC8A060)
    end
  end
end
