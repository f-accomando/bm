-- The HUD: at the top life and armour of each robot, the clock between
-- them and the rounds won; at the bottom energy (dashes and the sword) and
-- the heat of the guns. P1 on the left, P2 mirrored on the right.
--
-- The armour bar is as long as the armour: the heavy one is twice the
-- light. At half it cracks (the notch), at zero the shoulder is gone:
-- the robot's picture says the same thing as the bar.

local BAR_W = 256
local LIFE = { 0xF8D030, 0xFFF4A0, 0xC08818 }
local ARMOR = { 0x58B4FF, 0xBFEAFF, 0x2A64D0 }
local ARMOR_CRACKED = { 0x8FA8C8, 0xD8E4F4, 0x56688A }
local ENERGY = { 0x40E8A0, 0xB0FFD8, 0x1E9A68 }
local HEAT = { 0xFFB43C, 0xFFF0A8, 0xC05818 }
local HOT = { 0xFF4A28, 0xFFC0A0, 0xA01808 }
local EMPTY, FRAME, DMG = 0x2A1418, 0xC8D0E0, 0xE03A28
Hud.P_COL = { 0x58B4FF, 0xFF6A50 }

-- a bar filled to k (0..1); `trail` is a second level shown in red behind
-- (the damage just taken); `right`: it fills from the right
local function bar(x, y, w, h, k, col, right, trail)
  rectfill(x - 2, y - 2, w + 4, h + 4, 0x000000)
  rect(x - 1, y - 1, w + 2, h + 2, FRAME)
  rectfill(x, y, w, h, EMPTY)
  local function fill(kk, c1, c2, c3)
    local fw = floor(w * clamp(kk, 0, 1) + 0.5)
    if fw <= 0 then return end
    local fx = right and x + w - fw or x
    rectfill(fx, y, fw, h, c1)
    rectfill(fx, y, fw, 1, c2)
    if h > 4 then rectfill(fx, y + h - 2, fw, 2, c3) end
  end
  if trail and trail > k then fill(trail, DMG, 0xFF9A80, 0x901808) end
  fill(k, col[1], col[2], col[3])
end
Hud.bar = bar

-- the clock: two big digits
local DIGIT = { [0] = "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9" }
local function clock(secs)
  panel(284, 8, 72, 46, 0x0C0E16, 0x8A96B0)
  local s = clamp(secs, 0, 99)
  sprite(DIGIT[s // 10], 305, 31)
  sprite(DIGIT[s % 10], 335, 31)
end

local function side(f, i, wins)
  local right = i == 2
  local x = right and W - 20 - BAR_W or 20
  -- life
  bar(x, 14, BAR_W, 14, f.life / 1000, LIFE, right, f.life_show / 1000)
  -- armour: as long as it is strong
  local aw = floor(BAR_W * f.armor_max / Data.ARMOR.heavy.armor)
  local ax = right and x + BAR_W - aw or x
  local cracked = f.shoulder ~= "ok"
  bar(ax, 33, aw, 6, f.armor / f.armor_max, cracked and ARMOR_CRACKED or ARMOR, right)
  local notch = right and ax + aw - floor(aw / 2) or ax + floor(aw / 2)
  rectfill(notch, 31, 1, 10, 0xFFFFFF)
  -- name, configuration, rounds won
  local label = (f.cpu and "CPU" or "P" .. i)
  local conf = f.A.name .. "/" .. f.W.name
  if f.shoulder == "gone" and G.frame % 40 < 26 then conf = "ARMOR BROKEN" end
  local col = f.shoulder == "gone" and 0xFF6A50 or 0xE0E6F0
  if right then
    text(label, W - 20 - #label * 8, 44, Hud.P_COL[i])
    text(conf, W - 28 - (#label + #conf) * 8, 44, col)
  else
    text(label, 20, 44, Hud.P_COL[i])
    text(conf, 28 + #label * 8, 44, col)
  end
  for k = 1, 2 do
    local mx = right and 372 + (k - 1) * 14 or 268 - (k - 1) * 14
    circfill(mx, 50, 5, 0x000000)
    circfill(mx, 50, 4, k <= wins and 0xFFD040 or 0x3A3444)
    if k <= wins then pset(mx - 1, 48, 0xFFFFFF) end
  end
  -- energy and heat at the bottom
  local bx = right and W - 72 - 150 or 72
  local function row(name, y, k, cols, blink)
    local tx = right and W - 20 - #name * 8 or 20
    if not blink or G.frame % 20 < 12 then text(name, tx, y, 0xE0E6F0) end
    bar(bx, y + 4, 150, 8, k, cols, right)
  end
  row("ENERGY", 322, f.energy / 100, ENERGY)
  if f.W.special == "slash" then
    -- the sword needs this much
    local k = Data.WEAPON.sword.energy / 100
    local nx = right and bx + 150 - floor(150 * k) or bx + floor(150 * k)
    rectfill(nx, 324, 1, 12, f.energy >= Data.WEAPON.sword.energy and 0xFFFFFF or 0x806070)
  else
    local over = f.overheat > 0
    row(over and "HOT!" or "HEAT", 340, f.heat / 100, over and HOT or HEAT, over)
  end
  -- the combo
  if f.combo_t and f.combo_show >= 2 then
    local s = f.combo_show .. " HITS"
    local cx = right and W - 24 - #s * 16 or 24
    text(s, cx, 90, 0xFFD040, 2)
  end
end

function Hud.draw(m, a, b)
  side(a, 1, m.wins[1])
  side(b, 2, m.wins[2])
  clock(m.clock)
end
